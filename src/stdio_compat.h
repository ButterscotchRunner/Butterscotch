#ifndef _BS_COMPAT_STDIO_H_
#define _BS_COMPAT_STDIO_H_

#include <stdio.h>

#ifdef NO_SNPRINTF
#include <printf.h>
#define snprintf snprintf_
#define vsnprintf vsnprintf_
#endif

#endif /* _BS_COMPAT_STDIO_H_ */
