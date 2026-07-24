#ifndef _BS_STDINT_H_
#define _BS_STDINT_H_

#ifdef HAVE_SYS_TYPES_H
/* some systems have their own int*_t definitions here that might cause conflicts */
#include <sys/types.h>
#define int8_t __bs_int8_t
#define int16_t __bs_int16_t
#define int32_t __bs_int32_t
#define int64_t __bs_int64_t
#endif

#ifdef _MSC_VER
/* MSVC used to define intptr_t here, before it had stdint.h */
#include <crtdefs.h>
#define intptr_t __bs_intptr_t
#endif

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef int64_t int_fast64_t;

typedef int64_t intmax_t;

typedef long intptr_t;

#endif /* _BS_STDINT_H_ */
