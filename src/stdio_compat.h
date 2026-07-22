#ifndef _BS_COMPAT_STDIO_H_
#define _BS_COMPAT_STDIO_H_

#include <stdio.h>

#ifdef NO_SNPRINTF
#include <nanoprintf.h>
#define snprintf npf_snprintf
#endif

#endif /* _BS_COMPAT_STDIO_H_ */
