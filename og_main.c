/*-------------------------------------------------------------------------*/
/*--- Objgrind: Memory checker for a programming language.    og_main.c ---*/
/*-------------------------------------------------------------------------*/

/*
   This file is part of Objgrind.

   Copyright (C) 2013 Narihiro Nakamura

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_aspacemgr.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_poolalloc.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_oset.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_threadstate.h"

#include "objgrind.h"   /* for client requests */
#include "og_error.h"


/*------------------------------------------------------------*/
/*--- Basic A bitmap functions from valgrind/memcheck      ---*/
/*    Some parts are edited. 2013/06/15                       */
/*------------------------------------------------------------*/

/* --------------- Basic configuration --------------- */

/* Only change this.  N_PRIMARY_MAP *must* be a power of 2. */

#if VG_WORDSIZE == 4

/* cover the entire address space */
#  define N_PRIMARY_BITS  16

#else

/* Just handle the first 64G fast and the rest via auxiliary
   primaries.  If you change this, Objgrind will assert at startup.
   See the definition of UNALIGNED_OR_HIGH for extensive comments. */
#  define N_PRIMARY_BITS  20

#endif

/* Do not change this. */
#define N_PRIMARY_MAP  ( ((UWord)1) << N_PRIMARY_BITS)

/* Do not change this. */
#define MAX_PRIMARY_ADDRESS (Addr)((((Addr)65536) * N_PRIMARY_MAP)-1)


/* --------------- Secondary map ---------------- */

/* Accessibilty bit pattern */
#define A_BITS2_NOCHECK      0x0      // 00b
#define A_BITS2_UNWRITABLE   0x1      // 01b
#define A_BITS2_UNREFERABLE  0x2      // 10b
#define A_BITS2_REFCHECK     0x3      // 11b

#define A_BITS8_NOCHECK      0x00     // 00_00_00_00b
#define A_BITS8_UNWRITABLE   0x55     // 01_01_01_01b
#define A_BITS8_UNREFERABLE  0xaa     // 10_10_10_10b

// These represent 64 bits of memory.
#define A_BITS16_NOCHECK     0x0000   // 00_00_00_00b x 2
#define A_BITS16_UNWRITABLE  0x5555   // 01_01_01_01b x 2
#define A_BITS16_UNREFERABLE 0xaaaa   // 10_10_10_10b x 2
#define A_BITS16_REFCHECK    0xffff   // 11_11_11_11b x 2

#define SM_CHUNKS             16384
#define SM_OFF(aaa)           (((aaa) & 0xffff) >> 2)
#define SM_OFF_16(aaa)        (((aaa) & 0xffff) >> 3)

/* The number of entries in the primary map can be altered.  However
   we hardwire the assumption that each secondary map covers precisely
   64k of address space. */
#define SM_SIZE 65536            /* DO NOT CHANGE */
#define SM_MASK (SM_SIZE-1)      /* DO NOT CHANGE */

/* # searches initiated in auxmap_L1, and # base cmps required */
static ULong n_auxmap_L1_searches  = 0;
static ULong n_auxmap_L1_cmps      = 0;
/* # of searches that missed in auxmap_L1 and therefore had to
   be handed to auxmap_L2. And the number of nodes inserted. */
static ULong n_auxmap_L2_searches  = 0;
static ULong n_auxmap_L2_nodes     = 0;


// Paranoia:  it's critical for performance that the requested inlining
// occurs.  So try extra hard.
#define INLINE    inline __attribute__((always_inline))

static INLINE Addr start_of_this_sm ( Addr a ) {
   return (a & (~SM_MASK));
}
static INLINE Bool is_start_of_sm ( Addr a ) {
   return (start_of_this_sm(a) == a);
}

typedef 
   struct {
      UChar abits8[SM_CHUNKS];
   }
   SecMap;

#define SM_DIST_NOCHECK   0
#define SM_DIST_UNWRITABLE 1
#define SM_DIST_UNREFERABLE 2

static SecMap sm_distinguished[3];

static INLINE Bool is_distinguished_sm ( SecMap* sm ) {
   return sm >= &sm_distinguished[0] && sm <= &sm_distinguished[2];
}

static SecMap* copy_for_writing ( SecMap* dist_sm )
{
   SecMap* new_sm;
   tl_assert(dist_sm == &sm_distinguished[0]
          || dist_sm == &sm_distinguished[1]
          || dist_sm == &sm_distinguished[2]);

   new_sm = VG_(malloc)("og.scm.1", sizeof(SecMap));
   if (new_sm == NULL)
      VG_(out_of_memory_NORETURN)( "objgrind:allocate new SecMap", 
                                   sizeof(SecMap) );
   VG_(memcpy)(new_sm, dist_sm, sizeof(SecMap));
   return new_sm;
}

/* --------------- Primary maps --------------- */

/* The main primary map.  This covers some initial part of the address
   space, addresses 0 .. (N_PRIMARY_MAP << 16)-1.  The rest of it is
   handled using the auxiliary primary map.  
*/
static SecMap* primary_map[N_PRIMARY_MAP];


/* An entry in the auxiliary primary map.  base must be a 64k-aligned
   value, and sm points at the relevant secondary map.  As with the
   main primary map, the secondary may be either a real secondary, or
   one of the three distinguished secondaries.  DO NOT CHANGE THIS
   LAYOUT: the first word has to be the key for OSet fast lookups.
*/
typedef
   struct { 
      Addr    base;
      SecMap* sm;
   }
   AuxMapEnt;

/* Tunable parameter: How big is the L1 queue? */
#define N_AUXMAP_L1 24

/* Tunable parameter: How far along the L1 queue to insert
   entries resulting from L2 lookups? */
#define AUXMAP_L1_INSERT_IX 12

static struct {
          Addr       base;
          AuxMapEnt* ent; // pointer to the matching auxmap_L2 node
       } 
       auxmap_L1[N_AUXMAP_L1];

static OSet* auxmap_L2 = NULL;

static void init_auxmap_L1_L2 ( void )
{
   Int i;
   for (i = 0; i < N_AUXMAP_L1; i++) {
      auxmap_L1[i].base = 0;
      auxmap_L1[i].ent  = NULL;
   }

   tl_assert(0 == offsetof(AuxMapEnt,base));
   tl_assert(sizeof(Addr) == sizeof(void*));
   auxmap_L2 = VG_(OSetGen_Create)( /*keyOff*/  offsetof(AuxMapEnt,base),
                                    /*fastCmp*/ NULL,
                                    VG_(malloc), "mc.iaLL.1", VG_(free) );
}

/* Check representation invariants; if OK return NULL; else a
   descriptive bit of text.  Also return the number of
   non-distinguished secondary maps referred to from the auxiliary
   primary maps. */

static const HChar* check_auxmap_L1_L2_sanity ( Word* n_secmaps_found )
{
   Word i, j;
   /* On a 32-bit platform, the L2 and L1 tables should
      both remain empty forever.

      On a 64-bit platform:
      In the L2 table:
       all .base & 0xFFFF == 0
       all .base > MAX_PRIMARY_ADDRESS
      In the L1 table:
       all .base & 0xFFFF == 0
       all (.base > MAX_PRIMARY_ADDRESS
            .base & 0xFFFF == 0
            and .ent points to an AuxMapEnt with the same .base)
           or
           (.base == 0 and .ent == NULL)
   */
   *n_secmaps_found = 0;
   if (sizeof(void*) == 4) {
      /* 32-bit platform */
      if (VG_(OSetGen_Size)(auxmap_L2) != 0)
         return "32-bit: auxmap_L2 is non-empty";
      for (i = 0; i < N_AUXMAP_L1; i++) 
        if (auxmap_L1[i].base != 0 || auxmap_L1[i].ent != NULL)
      return "32-bit: auxmap_L1 is non-empty";
   } else {
      /* 64-bit platform */
      UWord elems_seen = 0;
      AuxMapEnt *elem, *res;
      AuxMapEnt key;
      /* L2 table */
      VG_(OSetGen_ResetIter)(auxmap_L2);
      while ( (elem = VG_(OSetGen_Next)(auxmap_L2)) ) {
         elems_seen++;
         if (0 != (elem->base & (Addr)0xFFFF))
            return "64-bit: nonzero .base & 0xFFFF in auxmap_L2";
         if (elem->base <= MAX_PRIMARY_ADDRESS)
            return "64-bit: .base <= MAX_PRIMARY_ADDRESS in auxmap_L2";
         if (elem->sm == NULL)
            return "64-bit: .sm in _L2 is NULL";
         if (!is_distinguished_sm(elem->sm))
            (*n_secmaps_found)++;
      }
      if (elems_seen != n_auxmap_L2_nodes)
         return "64-bit: disagreement on number of elems in _L2";
      /* Check L1-L2 correspondence */
      for (i = 0; i < N_AUXMAP_L1; i++) {
         if (auxmap_L1[i].base == 0 && auxmap_L1[i].ent == NULL)
            continue;
         if (0 != (auxmap_L1[i].base & (Addr)0xFFFF))
            return "64-bit: nonzero .base & 0xFFFF in auxmap_L1";
         if (auxmap_L1[i].base <= MAX_PRIMARY_ADDRESS)
            return "64-bit: .base <= MAX_PRIMARY_ADDRESS in auxmap_L1";
         if (auxmap_L1[i].ent == NULL)
            return "64-bit: .ent is NULL in auxmap_L1";
         if (auxmap_L1[i].ent->base != auxmap_L1[i].base)
            return "64-bit: _L1 and _L2 bases are inconsistent";
         /* Look it up in auxmap_L2. */
         key.base = auxmap_L1[i].base;
         key.sm   = 0;
         res = VG_(OSetGen_Lookup)(auxmap_L2, &key);
         if (res == NULL)
            return "64-bit: _L1 .base not found in _L2";
         if (res != auxmap_L1[i].ent)
            return "64-bit: _L1 .ent disagrees with _L2 entry";
      }
      /* Check L1 contains no duplicates */
      for (i = 0; i < N_AUXMAP_L1; i++) {
         if (auxmap_L1[i].base == 0)
            continue;
	 for (j = i+1; j < N_AUXMAP_L1; j++) {
            if (auxmap_L1[j].base == 0)
               continue;
            if (auxmap_L1[j].base == auxmap_L1[i].base)
               return "64-bit: duplicate _L1 .base entries";
         }
      }
   }
   return NULL; /* ok */
}

static void insert_into_auxmap_L1_at ( Word rank, AuxMapEnt* ent )
{
   Word i;
   tl_assert(ent);
   tl_assert(rank >= 0 && rank < N_AUXMAP_L1);
   for (i = N_AUXMAP_L1-1; i > rank; i--)
      auxmap_L1[i] = auxmap_L1[i-1];
   auxmap_L1[rank].base = ent->base;
   auxmap_L1[rank].ent  = ent;
}

static INLINE AuxMapEnt* maybe_find_in_auxmap ( Addr a )
{
   AuxMapEnt  key;
   AuxMapEnt* res;
   Word       i;

   tl_assert(a > MAX_PRIMARY_ADDRESS);
   a &= ~(Addr)0xFFFF;

   /* First search the front-cache, which is a self-organising
      list containing the most popular entries. */

   if (LIKELY(auxmap_L1[0].base == a))
      return auxmap_L1[0].ent;
   if (LIKELY(auxmap_L1[1].base == a)) {
      Addr       t_base = auxmap_L1[0].base;
      AuxMapEnt* t_ent  = auxmap_L1[0].ent;
      auxmap_L1[0].base = auxmap_L1[1].base;
      auxmap_L1[0].ent  = auxmap_L1[1].ent;
      auxmap_L1[1].base = t_base;
      auxmap_L1[1].ent  = t_ent;
      return auxmap_L1[0].ent;
   }

   n_auxmap_L1_searches++;

   for (i = 0; i < N_AUXMAP_L1; i++) {
      if (auxmap_L1[i].base == a) {
         break;
      }
   }
   tl_assert(i >= 0 && i <= N_AUXMAP_L1);

   n_auxmap_L1_cmps += (ULong)(i+1);

   if (i < N_AUXMAP_L1) {
      if (i > 0) {
         Addr       t_base = auxmap_L1[i-1].base;
         AuxMapEnt* t_ent  = auxmap_L1[i-1].ent;
         auxmap_L1[i-1].base = auxmap_L1[i-0].base;
         auxmap_L1[i-1].ent  = auxmap_L1[i-0].ent;
         auxmap_L1[i-0].base = t_base;
         auxmap_L1[i-0].ent  = t_ent;
         i--;
      }
      return auxmap_L1[i].ent;
   }

   n_auxmap_L2_searches++;

   /* First see if we already have it. */
   key.base = a;
   key.sm   = 0;

   res = VG_(OSetGen_Lookup)(auxmap_L2, &key);
   if (res)
      insert_into_auxmap_L1_at( AUXMAP_L1_INSERT_IX, res );
   return res;
}

static AuxMapEnt* find_or_alloc_in_auxmap ( Addr a )
{
   AuxMapEnt *nyu, *res;

   /* First see if we already have it. */
   res = maybe_find_in_auxmap( a );
   if (LIKELY(res))
      return res;

   /* Ok, there's no entry in the secondary map, so we'll have
      to allocate one. */
   a &= ~(Addr)0xFFFF;

   nyu = (AuxMapEnt*) VG_(OSetGen_AllocNode)( auxmap_L2, sizeof(AuxMapEnt) );
   tl_assert(nyu);
   nyu->base = a;
   nyu->sm   = &sm_distinguished[SM_DIST_NOCHECK];
   VG_(OSetGen_Insert)( auxmap_L2, nyu );
   insert_into_auxmap_L1_at( AUXMAP_L1_INSERT_IX, nyu );
   n_auxmap_L2_nodes++;
   return nyu;
}

/* --------------- SecMap fundamentals --------------- */

// In all these, 'low' means it's definitely in the main primary map,
// 'high' means it's definitely in the auxiliary table.

static INLINE SecMap** get_secmap_low_ptr ( Addr a )
{
   UWord pm_off = a >> 16;
#  if VG_DEBUG_MEMORY >= 1
   tl_assert(pm_off < N_PRIMARY_MAP);
#  endif
   return &primary_map[ pm_off ];
}

static INLINE SecMap** get_secmap_high_ptr ( Addr a )
{
   AuxMapEnt* am = find_or_alloc_in_auxmap(a);
   return &am->sm;
}

static SecMap** get_secmap_ptr ( Addr a )
{
   return ( a <= MAX_PRIMARY_ADDRESS 
          ? get_secmap_low_ptr(a) 
          : get_secmap_high_ptr(a));
}

static INLINE SecMap* get_secmap_for_reading_low ( Addr a )
{
   return *get_secmap_low_ptr(a);
}

static INLINE SecMap* get_secmap_for_reading_high ( Addr a )
{
   return *get_secmap_high_ptr(a);
}

static INLINE SecMap* get_secmap_for_writing_low(Addr a)
{
   SecMap** p = get_secmap_low_ptr(a);
   if (UNLIKELY(is_distinguished_sm(*p)))
      *p = copy_for_writing(*p);
   return *p;
}

static INLINE SecMap* get_secmap_for_writing_high ( Addr a )
{
   SecMap** p = get_secmap_high_ptr(a);
   if (UNLIKELY(is_distinguished_sm(*p)))
      *p = copy_for_writing(*p);
   return *p;
}

/* Produce the secmap for 'a', either from the primary map or by
   ensuring there is an entry for it in the aux primary map.  The
   secmap may be a distinguished one as the caller will only want to
   be able to read it. 
*/
static INLINE SecMap* get_secmap_for_reading ( Addr a )
{
   return ( a <= MAX_PRIMARY_ADDRESS
          ? get_secmap_for_reading_low (a)
          : get_secmap_for_reading_high(a) );
}

/* Produce the secmap for 'a', either from the primary map or by
   ensuring there is an entry for it in the aux primary map.  The
   secmap may not be a distinguished one, since the caller will want
   to be able to write it.  If it is a distinguished secondary, make a
   writable copy of it, install it, and return the copy instead.  (COW
   semantics).
*/
static SecMap* get_secmap_for_writing ( Addr a )
{
   return ( a <= MAX_PRIMARY_ADDRESS
          ? get_secmap_for_writing_low (a)
          : get_secmap_for_writing_high(a) );
}

/* If 'a' has a SecMap, produce it.  Else produce NULL.  But don't
   allocate one if one doesn't already exist.  This is used by the
   leak checker.
*/
static SecMap* maybe_get_secmap_for ( Addr a )
{
   if (a <= MAX_PRIMARY_ADDRESS) {
      return get_secmap_for_reading_low(a);
   } else {
      AuxMapEnt* am = maybe_find_in_auxmap(a);
      return am ? am->sm : NULL;
   }
}

/* --------------- Fundamental functions --------------- */

static INLINE
void insert_abits2_into_abits8 ( Addr a, UChar abits2, UChar* abits8 )
{
   UInt shift =  (a & 3)  << 1;        // shift by 0, 2, 4, or 6
   *abits8  &= ~(0x3     << shift);   // mask out the two old bits
   *abits8  |=  (abits2 << shift);   // mask  in the two new bits
}

static INLINE
void insert_abits4_into_abits8 ( Addr a, UChar abits4, UChar* abits8 )
{
   UInt shift;
   tl_assert(VG_IS_2_ALIGNED(a));      // Must be 2-aligned
   shift     =  (a & 2)   << 1;        // shift by 0 or 4
   *abits8 &= ~(0xf      << shift);   // mask out the four old bits
   *abits8 |=  (abits4 << shift);    // mask  in the four new bits
}

static INLINE
UChar extract_abits2_from_abits8 ( Addr a, UChar abits8 )
{
   UInt shift = (a & 3) << 1;          // shift by 0, 2, 4, or 6
   abits8 >>= shift;                  // shift the two bits to the bottom
   return 0x3 & abits8;               // mask out the rest
}

static INLINE
UChar extract_abits4_from_abits8 ( Addr a, UChar abits8 )
{
   UInt shift;
   tl_assert(VG_IS_2_ALIGNED(a));      // Must be 2-aligned
   shift = (a & 2) << 1;               // shift by 0 or 4
   abits8 >>= shift;                  // shift the four bits to the bottom
   return 0xf & abits8;               // mask out the rest
}

// Note that these four are only used in slow cases.  The fast cases do
// clever things like combine the auxmap check (in
// get_secmap_{read,writ}able) with alignment checks.

// *** WARNING! ***
// Any time this function is called, if it is possible that abits2
// is equal to VA_BITS2_PARTDEFINED, then the corresponding entry in the
// sec-V-bits table must also be set!
static INLINE
void set_abits2 ( Addr a, UChar abits2 )
{
   SecMap* sm       = get_secmap_for_writing(a);
   UWord   sm_off   = SM_OFF(a);
   insert_abits2_into_abits8( a, abits2, &(sm->abits8[sm_off]) );
}

static INLINE
UChar get_abits2 ( Addr a )
{
   SecMap* sm       = get_secmap_for_reading(a);
   UWord   sm_off   = SM_OFF(a);
   UChar   abits8   = sm->abits8[sm_off];
   return extract_abits2_from_abits8(a, abits8);
}

// *** WARNING! ***
// Any time this function is called, if it is possible that any of the
// 4 2-bit fields in abits8 are equal to VA_BITS2_PARTDEFINED, then the 
// corresponding entry(s) in the sec-V-bits table must also be set!
static INLINE
UChar get_abits8_for_aligned_word32 ( Addr a )
{
   SecMap* sm       = get_secmap_for_reading(a);
   UWord   sm_off   = SM_OFF(a);
   UChar   abits8  = sm->abits8[sm_off];
   return abits8;
}

static INLINE
void set_abits8_for_aligned_word32 ( Addr a, UChar abits8 )
{
   SecMap* sm       = get_secmap_for_writing(a);
   UWord   sm_off   = SM_OFF(a);
   sm->abits8[sm_off] = abits8;
}


static void set_address_range_perms ( Addr a, SizeT lenT, UWord abits16,
                                      UWord dsm_num )
{
   UWord    sm_off, sm_off16;
   UWord    abits2 = abits16 & 0x3;
   SizeT    lenA, lenB, len_to_next_secmap;
   Addr     aNext;
   SecMap*  sm;
   SecMap** sm_ptr;
   SecMap*  example_dsm;

   /* Check the A bits make sense. */
   tl_assert(A_BITS16_NOCHECK  == abits16 ||
             A_BITS16_UNWRITABLE == abits16 ||
             A_BITS16_UNREFERABLE == abits16 ||
             A_BITS16_REFCHECK == abits16);

   if (lenT == 0)
      return;

   if (lenT > 256 * 1024 * 1024) {
      if (VG_(clo_verbosity) > 0 && !VG_(clo_xml)) {
         const HChar* s = "unknown???";
         if (abits16 == A_BITS16_NOCHECK ) s = "noobj";
         VG_(message)(Vg_UserMsg, "Warning: set address range perms: "
                                  "large range [0x%lx, 0x%lx) (%s)\n",
                                  a, a + lenT, s);
      }
   }

#ifndef PERF_FAST_SARP
   /*------------------ debug-only case ------------------ */
   {
      // Endianness doesn't matter here because all bytes are being set to
      // the same value.
      // Nb: We don't have to worry about updating the sec-V-bits table
      // after these set_abits2() calls because this code never writes
      // VA_BITS2_PARTDEFINED values.
      SizeT i;
      for (i = 0; i < lenT; i++) {
         set_abits2(a + i, abits2);
      }
      return;
   }
#endif

   aNext = start_of_this_sm(a) + SM_SIZE;
   len_to_next_secmap = aNext - a;
   if ( lenT <= len_to_next_secmap ) {
      lenA = lenT;
      lenB = 0;
   } else if (is_start_of_sm(a)) {
      lenA = 0;
      lenB = lenT;
      goto part2;
   } else {
      // Range spans two or more sec-maps, first one is partial.
      lenA = len_to_next_secmap;
      lenB = lenT - lenA;
   }

   //------------------------------------------------------------------------
   // Part 1: Deal with the first sec_map.  Most of the time the range will be
   // entirely within a sec_map and this part alone will suffice.  Also,
   // doing it this way lets us avoid repeatedly testing for the crossing of
   // a sec-map boundary within these loops.
   //------------------------------------------------------------------------

   // If it's distinguished, make it undistinguished if necessary.
   sm_ptr = get_secmap_ptr(a);
   if (is_distinguished_sm(*sm_ptr)) {
      if (*sm_ptr == example_dsm) {
         // Sec-map already has the V+A bits that we want, so skip.
         a    = aNext;
         lenA = 0;
      } else {
         *sm_ptr = copy_for_writing(*sm_ptr);
      }
   }
   sm = *sm_ptr;

   // 1 byte steps
   while (True) {
      if (VG_IS_8_ALIGNED(a)) break;
      if (lenA < 1)           break;
      sm_off = SM_OFF(a);
      insert_abits2_into_abits8( a, abits2, &(sm->abits8[sm_off]) );
      a    += 1;
      lenA -= 1;
   }
   // 8-aligned, 8 byte steps
   while (True) {
      if (lenA < 8) break;
      sm_off16 = SM_OFF_16(a);
      ((UShort*)(sm->abits8))[sm_off16] = abits16;
      a    += 8;
      lenA -= 8;
   }
   // 1 byte steps
   while (True) {
      if (lenA < 1) break;
      sm_off = SM_OFF(a);
      insert_abits2_into_abits8( a, abits2, &(sm->abits8[sm_off]) );
      a    += 1;
      lenA -= 1;
   }

   // We've finished the first sec-map.  Is that it?
   if (lenB == 0)
      return;

   //------------------------------------------------------------------------
   // Part 2: Fast-set entire sec-maps at a time.
   //------------------------------------------------------------------------
  part2:
   // 64KB-aligned, 64KB steps.
   // Nb: we can reach here with lenB < SM_SIZE
   tl_assert(0 == lenA);
   while (True) {
      if (lenB < SM_SIZE) break;
      tl_assert(is_start_of_sm(a));
      sm_ptr = get_secmap_ptr(a);
      if (!is_distinguished_sm(*sm_ptr)) {
         VG_(free)((void *)*sm_ptr);
      }
      // Make the sec-map entry point to the example DSM
      *sm_ptr = example_dsm;
      lenB -= SM_SIZE;
      a    += SM_SIZE;
   }

   // We've finished the whole sec-maps.  Is that it?
   if (lenB == 0)
      return;

   //------------------------------------------------------------------------
   // Part 3: Finish off the final partial sec-map, if necessary.
   //------------------------------------------------------------------------

   tl_assert(is_start_of_sm(a) && lenB < SM_SIZE);

   // If it's distinguished, make it undistinguished if necessary.
   sm_ptr = get_secmap_ptr(a);
   if (is_distinguished_sm(*sm_ptr)) {
      if (*sm_ptr == example_dsm) {
         return;
      } else {
         *sm_ptr = copy_for_writing(*sm_ptr);
      }
   }
   sm = *sm_ptr;

   // 8-aligned, 8 byte steps
   while (True) {
      if (lenB < 8) break;
      sm_off16 = SM_OFF_16(a);
      ((UShort*)(sm->abits8))[sm_off16] = abits16;
      a    += 8;
      lenB -= 8;
   }
   // 1 byte steps
   while (True) {
      if (lenB < 1) return;
      sm_off = SM_OFF(a);
      insert_abits2_into_abits8( a, abits2, &(sm->abits8[sm_off]) );
      a    += 1;
      lenB -= 1;
   }
}


/*------------------------------------------------------------*/
/*--- Event handlers called from generated code            ---*/
/*------------------------------------------------------------*/

static void
OG_(store_check8)(Addr a, UWord data8){
    if (get_abits2(a) == A_BITS2_UNWRITABLE) {
        VG_(maybe_record_error)(VG_(get_running_tid)(),
                                UnwritableErr, a, NULL, NULL);
    }
}

static void
OG_(store_check16)(Addr a, UWord data16){
    if (get_abits2(a) == A_BITS2_UNWRITABLE) {
        VG_(maybe_record_error)(VG_(get_running_tid)(),
                                UnwritableErr, a, NULL, NULL);
    }
}

static void
OG_(store_check32)(Addr a, UWord data32){
    if (get_abits2(a) == A_BITS2_UNWRITABLE) {
        VG_(maybe_record_error)(VG_(get_running_tid)(),
                                UnwritableErr, a, NULL, NULL);
    }
    else if (get_abits2(a) == A_BITS2_REFCHECK &&
             get_abits2(data32) == A_BITS2_UNREFERABLE) {
        VG_(maybe_record_error)(VG_(get_running_tid)(),
                                UnreferableErr, (Addr)data32, NULL, NULL);
    }
}

static void
OG_(store_check64)(Addr a, ULong data64, UInt wordSize){
    if (wordSize == 32) {
        OG_(store_check32)(a, (Word)data64);
        OG_(store_check32)(a, (Word)(data64 >> 32));
    }
    else {
        if (get_abits2(a) == A_BITS2_UNWRITABLE) {
            VG_(maybe_record_error)(VG_(get_running_tid)(),
                                    UnwritableErr, a, NULL, NULL);
        }
        else if (get_abits2(a) == A_BITS2_REFCHECK &&
                 get_abits2(data64) == A_BITS2_UNREFERABLE) {
            VG_(maybe_record_error)(VG_(get_running_tid)(),
                                    UnreferableErr, (Addr)data64, NULL, NULL);
        }
    }
}


/*------------------------------------------------------------*/
/*--- Instrument                                           ---*/
/*------------------------------------------------------------*/

typedef  IRExpr  IRAtom;

/* build various kinds of expressions */
#define triop(_op, _arg1, _arg2, _arg3) \
                                 IRExpr_Triop((_op),(_arg1),(_arg2),(_arg3))
#define binop(_op, _arg1, _arg2) IRExpr_Binop((_op),(_arg1),(_arg2))
#define unop(_op, _arg)          IRExpr_Unop((_op),(_arg))
#define mkU1(_n)                 IRExpr_Const(IRConst_U1(_n))
#define mkU8(_n)                 IRExpr_Const(IRConst_U8(_n))
#define mkU16(_n)                IRExpr_Const(IRConst_U16(_n))
#define mkU32(_n)                IRExpr_Const(IRConst_U32(_n))
#define mkU64(_n)                IRExpr_Const(IRConst_U64(_n))
#define mkV128(_n)               IRExpr_Const(IRConst_V128(_n))
#define mkexpr(_tmp)             IRExpr_RdTmp((_tmp))

static IRAtom* assignNew ( IRSB* bbOut, IRType ty, IRExpr* e )
{
   IRTemp   t;

   t = newIRTemp(bbOut->tyenv, ty);
   addStmtToIRSB(bbOut, IRStmt_WrTmp(t, e));
   return IRExpr_RdTmp(t);
}

static IRExpr* zwidenToHostWord (IRSB* bbOut, IRType tyH, IRAtom* atom)
{
   IRType ty;

   ty  = typeOfIRExpr(bbOut->tyenv, atom);

   if (tyH == Ity_I32) {
      switch (ty) {
         case Ity_I32:
            return atom;
         case Ity_I16:
            return assignNew(bbOut, tyH, unop(Iop_16Uto32, atom));
         case Ity_I8:
            return assignNew(bbOut, tyH, unop(Iop_8Uto32, atom));
         default:
            goto unhandled;
      }
   } else
   if (tyH == Ity_I64) {
      switch (ty) {
         case Ity_I32:
            return assignNew(bbOut, tyH, unop(Iop_32Uto64, atom));
         case Ity_I16:
            return assignNew(bbOut, tyH, unop(Iop_32Uto64, 
                   assignNew(bbOut, Ity_I32, unop(Iop_16Uto32, atom))));
         case Ity_I8:
            return assignNew(bbOut, tyH, unop(Iop_32Uto64, 
                   assignNew(bbOut, Ity_I32, unop(Iop_8Uto32, atom))));
         default:
            goto unhandled;
      }
   } else {
      goto unhandled;
   }
  unhandled:
   VG_(printf)("\nty = "); ppIRType(ty); VG_(printf)("\n");
   VG_(tool_panic)("zwidenToHostWord");
}

static void
insert_store_checker(IRSB* bbOut, IRAtom* addr, IRAtom* data, IRAtom* guard, IRType tyAddr)
{
    void* helper = NULL;
    const HChar* hname = NULL;
    IROp     mkAdd;
    IRType   ty;
    IRAtom*  wordSize = NULL;

    mkAdd = (tyAddr == Ity_I32) ? Iop_Add32 : Iop_Add64;
    tl_assert(tyAddr == Ity_I32 || tyAddr == Ity_I64);

    ty = typeOfIRExpr(bbOut->tyenv, data);

    switch (ty) {
    case Ity_V256:
    case Ity_V128:
    case Ity_I64: helper = &OG_(store_check64);
        hname = "OG_(store_check64)";
        break;
    case Ity_I32: helper = &OG_(store_check32);
        hname = "OG_(store_check32)";
        break;
    case Ity_I16: helper = &OG_(store_check16);
        hname = "OG_(store_check16)";
        break;
    case Ity_I8: helper = &OG_(store_check8);
        hname = "OG_(store_check8)";
        break;
    default: VG_(tool_panic)("objgrind:insert_store_checker");
    }

    wordSize = mkU32(tyAddr == Ity_I32 ? 32 : 64);

    if (UNLIKELY(ty == Ity_V256)) {
        IRDirty *diQ0,    *diQ1,    *diQ2,    *diQ3;
        IRAtom  *addrQ0,  *addrQ1,  *addrQ2,  *addrQ3;
        IRAtom  *dataQ0,  *dataQ1,  *dataQ2,  *dataQ3;
        IRAtom  *eBiasQ0, *eBiasQ1, *eBiasQ2, *eBiasQ3;

        eBiasQ0 = tyAddr==Ity_I32 ? mkU32(0) : mkU64(0);
        addrQ0  = assignNew(bbOut, tyAddr, binop(mkAdd, addr, eBiasQ0));
        dataQ0  = assignNew(bbOut, Ity_I64, unop(Iop_V256to64_0, data));
        diQ0    = unsafeIRDirty_0_N(0, 
            hname, VG_(fnptr_to_fnentry)( helper ), 
            mkIRExprVec_3( addrQ0, dataQ0, wordSize )
            );

        eBiasQ1 = tyAddr==Ity_I32 ? mkU32(8) : mkU64(8);
        addrQ1  = assignNew(bbOut, tyAddr, binop(mkAdd, addr, eBiasQ1));
        dataQ1  = assignNew(bbOut, Ity_I64, unop(Iop_V256to64_1, data));
        diQ1    = unsafeIRDirty_0_N(0, 
            hname, VG_(fnptr_to_fnentry)( helper ), 
            mkIRExprVec_3( addrQ1, dataQ1, wordSize )
            );

        eBiasQ2 = tyAddr==Ity_I32 ? mkU32(16) : mkU64(16);
        addrQ2  = assignNew(bbOut, tyAddr, binop(mkAdd, addr, eBiasQ2));
        dataQ2  = assignNew(bbOut, Ity_I64, unop(Iop_V256to64_2, data));
        diQ2    = unsafeIRDirty_0_N(0, 
            hname, VG_(fnptr_to_fnentry)( helper ), 
            mkIRExprVec_3( addrQ2, dataQ2, wordSize )
            );

        eBiasQ3 = tyAddr==Ity_I32 ? mkU32(24) : mkU64(24);
        addrQ3  = assignNew(bbOut, tyAddr, binop(mkAdd, addr, eBiasQ3));
        dataQ3  = assignNew(bbOut, Ity_I64, unop(Iop_V256to64_3, data));
        diQ3    = unsafeIRDirty_0_N(0, 
            hname, VG_(fnptr_to_fnentry)( helper ), 
            mkIRExprVec_3( addrQ3, dataQ3, wordSize )
            );

        if (guard)
            diQ0->guard = diQ1->guard = diQ2->guard = diQ3->guard = guard;

        addStmtToIRSB(bbOut, IRStmt_Dirty(diQ0));
        addStmtToIRSB(bbOut, IRStmt_Dirty(diQ1));
        addStmtToIRSB(bbOut, IRStmt_Dirty(diQ2));
        addStmtToIRSB(bbOut, IRStmt_Dirty(diQ3));
    }
    else if (UNLIKELY(ty == Ity_V128)) {
        IRDirty *diLo64, *diHi64;
        IRAtom  *addrLo64, *addrHi64;
        IRAtom  *dataLo64, *dataHi64;
        IRAtom  *eBiasLo64, *eBiasHi64;

        eBiasLo64 = tyAddr==Ity_I32 ? mkU32(0) : mkU64(0);
        addrLo64  = assignNew(bbOut, tyAddr, binop(mkAdd, addr, eBiasLo64) );
        dataLo64 = assignNew(bbOut, Ity_I64, unop(Iop_V128to64, data));
        diLo64    = unsafeIRDirty_0_N(0,
            hname, VG_(fnptr_to_fnentry)( helper ), 
            mkIRExprVec_2( addrLo64, dataLo64 )
            );
        eBiasHi64 = tyAddr==Ity_I32 ? mkU32(8) : mkU64(8);
        addrHi64  = assignNew(bbOut, tyAddr, binop(mkAdd, addr, eBiasHi64) );
        dataHi64 = assignNew(bbOut, Ity_I64, unop(Iop_V128HIto64, data));
        diHi64    = unsafeIRDirty_0_N(0,
            hname, VG_(fnptr_to_fnentry)( helper ), 
            mkIRExprVec_2( addrHi64, dataHi64 )
            );
        if (guard) diLo64->guard = guard;
        if (guard) diHi64->guard = guard;
        addStmtToIRSB(bbOut, IRStmt_Dirty(diLo64));
        addStmtToIRSB(bbOut, IRStmt_Dirty(diHi64));
    }
    else {
        IRDirty *di;
        IRAtom  *addrAct;

        addrAct = addr;

        if (ty == Ity_I64) {
          di = unsafeIRDirty_0_N(0,
              hname, VG_(fnptr_to_fnentry)( helper ), 
              mkIRExprVec_2( addrAct, data )
              );
        }
        else {
         di = unsafeIRDirty_0_N(0,
                 hname, VG_(fnptr_to_fnentry)( helper ), 
                 mkIRExprVec_2( addrAct,
                                zwidenToHostWord( bbOut, tyAddr, data ))
              );
        }
        if (guard) di->guard = guard;
        addStmtToIRSB(bbOut, IRStmt_Dirty(di));
    }
}

static
IRSB* og_instrument ( VgCallbackClosure* closure,
                      IRSB* bb_in,
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
   Int i;
   IRSB*    bbOut;

   /* Set up BB */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = deepCopyIRTypeEnv(bb_in->tyenv);
   bbOut->next     = deepCopyIRExpr(bb_in->next);
   bbOut->jumpkind = bb_in->jumpkind;
   bbOut->offsIP   = bb_in->offsIP;
    
   for (i = 0; i < bb_in->stmts_used; i++) {
      IRStmt* const st = bb_in->stmts[i];
      IRStoreG* sg = NULL;
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
       
      switch (st->tag) {
      case Ist_NoOp:
      case Ist_AbiHint:
      case Ist_Put:
      case Ist_PutI:
      case Ist_MBE:
      case Ist_IMark:
      case Ist_WrTmp:
      case Ist_LoadG:
      case Ist_Dirty:
      case Ist_LLSC:
      case Ist_Exit:
          addStmtToIRSB(bbOut, st);
          break;
      case Ist_Store:
          insert_store_checker(bbOut, st->Ist.Store.addr, st->Ist.Store.data, NULL, hWordTy);
          addStmtToIRSB(bbOut, st);
          break;
      case Ist_StoreG:
          sg = st->Ist.StoreG.details;
          insert_store_checker(bbOut, sg->addr, sg->data, sg->guard, hWordTy);
          addStmtToIRSB(bbOut, st);
          break;
      case Ist_CAS:
          /* TODO */
          addStmtToIRSB(bbOut, st);
          break;
         default:
            ppIRStmt(st);
            tl_assert(0);
      }
   }

   return bbOut;
}

/*------------------------------------------------------------*/
/*--- Client requests                                      ---*/
/*------------------------------------------------------------*/

static INLINE void
add_refcheck_field(Addr field)
{
    set_abits2(field, A_BITS2_REFCHECK);
}

static INLINE void
remove_refcheck_field(Addr field)
{
    set_abits2(field, A_BITS2_NOCHECK);
}

static Bool og_handle_client_request ( ThreadId tid, UWord* arg, UWord* ret )
{
   if (!VG_IS_TOOL_USERREQ('O','G',arg[0])
       && VG_USERREQ__MAKE_NOCHECK != arg[0]
       && VG_USERREQ__MAKE_UNWRITABLE != arg[0]
       && VG_USERREQ__MAKE_UNREFERABLE != arg[0]
       && VG_USERREQ__ADD_REFCHECK_FIELD != arg[0]
       && VG_USERREQ__REMOVE_REFCHECK_FIELD != arg[0])
      return False;

   switch (arg[0]) {
   case VG_USERREQ__MAKE_NOCHECK:
       set_address_range_perms(arg[1], arg[2], A_BITS16_NOCHECK, SM_DIST_NOCHECK);
       break;
   case VG_USERREQ__MAKE_UNWRITABLE:
       set_address_range_perms(arg[1], arg[2], A_BITS16_UNWRITABLE, SM_DIST_UNWRITABLE);
       break;
   case VG_USERREQ__MAKE_UNREFERABLE:
       set_address_range_perms(arg[1], arg[2], A_BITS16_UNREFERABLE, SM_DIST_UNREFERABLE);
       break;
   case VG_USERREQ__ADD_REFCHECK_FIELD:
       add_refcheck_field(arg[1]);
       break;
   case VG_USERREQ__REMOVE_REFCHECK_FIELD:
       remove_refcheck_field(arg[1]);
       break;
   case VG_USERREQ__CHECK_UNWRITABLE:
      *ret = (get_abits2(arg[1]) == A_BITS2_UNWRITABLE);
      break;

   default:
       VG_(message)(
           Vg_UserMsg, 
           "Warning: unknown objgrind client request code %llx\n",
           (ULong)arg[0]
           );
       return False;
   }
   return True;
}


/*------------------------------------------------------------*/
/*--- Setup and finalisation                               ---*/
/*------------------------------------------------------------*/

static void og_post_clo_init(void)
{
}

static void og_fini(Int exitcode)
{
}

static void og_pre_clo_init(void)
{
   Int     i;
   SecMap* sm;

   VG_(details_name)            ("Objgrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("Memory checker for a programming language");
   VG_(details_copyright_author)(
      "Copyright (C) 2013 Narihiro Nakamura");
   VG_(details_bug_reports_to)  ("www.github.com/authorNari/objgrind");

   VG_(needs_client_requests)     (og_handle_client_request);
   VG_(details_avg_translation_sizeB) ( 275 );

   VG_(basic_tool_funcs)        (og_post_clo_init,
                                 og_instrument,
                                 og_fini);
   OG_(register_error_handlers)();

   init_auxmap_L1_L2();

   /* Build the 3 distinguished secondaries */
   sm = &sm_distinguished[SM_DIST_NOCHECK];
   for (i = 0; i < SM_CHUNKS; i++) sm->abits8[i] = A_BITS8_NOCHECK;
   sm = &sm_distinguished[SM_DIST_UNWRITABLE];
   for (i = 0; i < SM_CHUNKS; i++) sm->abits8[i] = A_BITS8_UNWRITABLE;
   sm = &sm_distinguished[SM_DIST_UNREFERABLE];
   for (i = 0; i < SM_CHUNKS; i++) sm->abits8[i] = A_BITS8_UNREFERABLE;
   for (i = 0; i < N_PRIMARY_MAP; i++)
      primary_map[i] = &sm_distinguished[SM_DIST_NOCHECK];
}

VG_DETERMINE_INTERFACE_VERSION(og_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
