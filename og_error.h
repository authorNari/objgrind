
#ifndef __OG_ERROR_H
#define __OG_ERROR_H

#include "pub_tool_basics.h"      // Addr

#define OG_(str) VGAPPEND(vgOg_, str)

typedef enum {
#define STR_UnwritableError  "UnwritableMemoryError"
   UnwritableErr = 1,
#define STR_UnreferableError  "UnreferableError"
   UnreferableErr,
} OgErrorKind;

void OG_(register_error_handlers)(void);

#endif
