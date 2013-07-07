/*
   ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (objgrind.h) only.  The rest of Valgrind is licensed under the
   terms of the GNU General Public License, version 2, unless
   otherwise indicated.  See the COPYING file in the source
   distribution for details.

   ----------------------------------------------------------------

   This file is part of Objgrind.

   Copyright (C) 2013 Narihiro Nakamura.  All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. The origin of this software must not be misrepresented; you must 
      not claim that you wrote the original software.  If you use this 
      software in a product, an acknowledgment in the product 
      documentation would be appreciated but is not required.

   3. Altered source versions must be plainly marked as such, and must
      not be misrepresented as being the original software.

   4. The name of the author may not be used to endorse or promote 
      products derived from this software without specific prior written 
      permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
   OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   ----------------------------------------------------------------

   Notice that the above BSD-style license applies to this one file
   (objgrind.h) only.  The entire rest of Valgrind is licensed under
   the terms of the GNU General Public License, version 2.  See the
   COPYING file in the source distribution for details.

   ---------------------------------------------------------------- 
*/


#ifndef __OBJGRIND_H
#define __OBJGRIND_H

#include "valgrind.h"

typedef
   enum { 
      VG_USERREQ__MAKE_NOCHECK = VG_USERREQ_TOOL_BASE('O','G'),
      VG_USERREQ__MAKE_UNWRITABLE,
      VG_USERREQ__MAKE_UNREFERABLE,
      VG_USERREQ__ADD_REFCHECK_FIELD,
      VG_USERREQ__REMOVE_REFCHECK_FIELD,

      VG_USERREQ__CHECK_UNWRITABLE,

   } Vg_ObjgrindClientRequest;

#define VALGRIND_MAKE_NOCHECK(_qzz_addr,_qzz_len)               \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,     \
                            VG_USERREQ__MAKE_NOCHECK,           \
                            (_qzz_addr), (_qzz_len), 0, 0, 0)

#define VALGRIND_MAKE_UNWRITABLE(_qzz_addr,_qzz_len)            \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,     \
                            VG_USERREQ__MAKE_UNWRITABLE,        \
                            (_qzz_addr), (_qzz_len), 0, 0, 0)

#define VALGRIND_MAKE_UNREFERABLE(_qzz_addr,_qzz_len)           \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,     \
                            VG_USERREQ__MAKE_UNREFERABLE,       \
                            (_qzz_addr), (_qzz_len), 0, 0, 0)

#define VALGRIND_ADD_REFCHECK_FIELD(_qzz_addr)                  \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,     \
                            VG_USERREQ__ADD_REFCHECK_FIELD,     \
                            (_qzz_addr), 0, 0, 0, 0)

#define VALGRIND_REMOVE_REFCHECK_FIELD(_qzz_addr)               \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,     \
                            VG_USERREQ__REMOVE_REFCHECK_FIELD,  \
                            (_qzz_addr), 0, 0, 0, 0)

#define VALGRIND_CHECK_UNWRITABLE(_qzz_addr)                    \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,     \
                            VG_USERREQ__CHECK_UNWRITABLE,       \
                            (_qzz_addr), 0, 0, 0, 0)

#endif
