#pragma once

#include <stdbool.h>
#ifndef nullptr
#define nullptr NULL
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) || defined(__BIG_ENDIAN__)
#define IS_BIG_ENDIAN
#endif

#if defined(__has_c_attribute)
    #if __has_c_attribute(maybe_unused)
        #define MAYBE_UNUSED [[maybe_unused]]
    #endif
#endif

#ifndef MAYBE_UNUSED
    #if defined(__GNUC__) || defined(__clang__)
        #define MAYBE_UNUSED __attribute__((unused))
    #else
        #define MAYBE_UNUSED
    #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define ALIGN(x) __attribute__((aligned(x)));
#else
    #define ALIGN(x)
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER) && _MSC_VER >= 1400 // VS2005 or later
    #define NOINLINE __declspec(noinline)
#else
    #define NOINLINE
#endif