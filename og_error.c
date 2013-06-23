#include "pub_tool_basics.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_poolalloc.h"     // For mc_include.h
#include "pub_tool_hashtable.h"     // For mc_include.h
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_debuginfo.h"     // VG_(get_dataname_and_offset)
#include "pub_tool_xarray.h"

#include "og_error.h"

static Bool og_compare_error_contexts(VgRes res, Error* e1, Error* e2)
{
    /* Guaranteed by calling function */
    tl_assert(VG_(get_error_kind)(e1) == VG_(get_error_kind)(e2));
   
    switch (VG_(get_error_kind)(e1)) {
    case UnwritableErr:
    case UnreferableErr:
        return (VG_(get_error_address)(e1) == VG_(get_error_address)(e2) ? True : False);
    default: 
        VG_(printf)("Error:\n  unknown error code %d\n",
                    VG_(get_error_kind)(e1));
        VG_(tool_panic)("unknown error code in og_compare_error_contexts");
    }
    return False;
}

/* Do a printf-style operation on either the XML or normal output
   channel, depending on the setting of VG_(clo_xml).
*/
static void emit_WRK ( const HChar* format, va_list vargs )
{
   if (VG_(clo_xml)) {
      VG_(vprintf_xml)(format, vargs);
   } else {
      VG_(vmessage)(Vg_UserMsg, format, vargs);
   }
}
static void emit ( const HChar* format, ... ) PRINTF_CHECK(1, 2);
static void emit ( const HChar* format, ... )
{
   va_list vargs;
   va_start(vargs, format);
   emit_WRK(format, vargs);
   va_end(vargs);
}

static void og_tool_error_before_pp (Error* err) {
    /* Noop */
}

static void og_tool_error_pp (Error* err) {
    const Bool xml  = VG_(clo_xml); /* a shorthand */

    switch (VG_(get_error_kind)(err)) {
    case UnwritableErr:
        if (xml) {
            emit("<kind>%s</kind>", STR_UnwritableError);
            VG_(pp_ExeContext)( VG_(get_error_where)(err) );
        }
        else {
            emit(STR_UnwritableError);
            VG_(pp_ExeContext)( VG_(get_error_where)(err) );
        }
        break;
    case UnreferableErr:
        if (xml) {
            emit("<kind>%s</kind>", STR_UnreferableError);
            VG_(pp_ExeContext)( VG_(get_error_where)(err) );
        }
        else {
            emit(STR_UnreferableError);
            VG_(pp_ExeContext)( VG_(get_error_where)(err) );
        }
        break;
    default:
        VG_(printf)("Error:\n  unknown Objgrind error code %d\n",
                    VG_(get_error_kind)(err));
        VG_(tool_panic)("unknown error code in og_tool_error_pp)");
    }
}

static UInt og_tool_error_update_extra(Error* e)
{
    /* TODO: add extra error for each kind */
    return 0;
}


static Bool og_is_recognized_suppression(const HChar* const name,
                                         Supp* const supp)
{
   OgErrorKind skind = 0;

   if (VG_(strcmp)(name, STR_UnwritableError) == 0)
      skind = UnwritableErr;
   else if (VG_(strcmp)(name, STR_UnreferableError) == 0)
      skind = UnreferableErr;
   else
      return False;

   VG_(set_supp_kind)(supp, skind);
   return True;
}

static Bool og_error_matches_suppression(Error* const e, Supp* const supp)
{
    return VG_(get_supp_kind)(supp) == VG_(get_error_kind)(e);
}

static
Bool og_read_extra_suppression_info(Int fd, HChar** bufpp,
                                     SizeT* nBufp, Supp* supp)
{
   return True;
}

static const HChar* og_get_error_name(Error* e)
{
   switch (VG_(get_error_kind)(e))
   {
   case UnwritableErr:  return VGAPPEND(STR_, UnwritableError);
   case UnreferableErr: return VGAPPEND(STR_, UnreferableError);
   default:
      tl_assert(0);
   }
   return 0;
}

static
Bool og_get_extra_suppression_info(Error* e,
                                   /*OUT*/HChar* buf, Int nBuf)
{
   return False;
}


void OG_(register_error_handlers)(void)
{
   VG_(needs_tool_errors)(og_compare_error_contexts,
                          og_tool_error_before_pp,
                          og_tool_error_pp,
                          True,
                          og_tool_error_update_extra,
                          og_is_recognized_suppression,
                          og_read_extra_suppression_info,
                          og_error_matches_suppression,
                          og_get_error_name,
                          og_get_extra_suppression_info);
}
