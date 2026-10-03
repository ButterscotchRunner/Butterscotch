/* a few butterscotch additions: C99, single threaded builds on consoles */
#ifndef B2_BUTTERSCOTCH_COMPAT_H
#define B2_BUTTERSCOTCH_COMPAT_H
#include <math.h>
#ifdef NO_SQRTF
static inline float b2CompatSqrtf(float x) { return (float)sqrt((double)x); }
#define sqrtf b2CompatSqrtf
#endif
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define B2_JOIN_INNER(a, b) a##b
#define B2_JOIN(a, b) B2_JOIN_INNER(a, b)
#ifndef _Static_assert
#define _Static_assert(condition, message) typedef char B2_JOIN(b2_static_assert_, __LINE__)[(condition) ? 1 : -1]
#endif
#endif
#endif
