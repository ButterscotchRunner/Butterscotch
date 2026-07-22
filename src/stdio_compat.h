#ifndef _BS_COMPAT_STDIO_H_
#define _BS_COMPAT_STDIO_H_

#include <stdio.h>

#ifdef NO_SNPRINTF
#define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD 1
#include <printf.h>
#endif

#endif /* _BS_COMPAT_STDIO_H_ */
