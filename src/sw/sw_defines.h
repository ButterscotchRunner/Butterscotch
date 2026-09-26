#ifndef _SW_DEFINES_H
#define _SW_DEFINES_H

#include "common.h"
#include "sw_config.h"

#if defined(__GNUC__) && (__GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96))
#define LIKELY(cond)   __builtin_expect(!!(cond), 1)
#define UNLIKELY(cond) __builtin_expect(!!(cond), 0)
#else
#define LIKELY(cond) (cond)
#define UNLIKELY(cond) (cond)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER) && _MSC_VER >= 1200
#define FORCE_INLINE static __forceinline
#else
#define FORCE_INLINE static inline
#endif

#if defined(__cplusplus) && __cplusplus >= 201703L
#define UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif

#endif//_SW_DEFINES_H
