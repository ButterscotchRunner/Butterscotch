#ifndef _BS_STDIO_H_
#define _BS_STDIO_H_

#include_next <stdio.h>

#include "nanoprintf.h"
#define snprintf npf_vsnprintf

#endif /* _BS_STDIO_H_ */
