/* a few butterscotch additions: C99, single threaded builds on consoles */
#ifndef B2_BUTTERSCOTCH_COMPAT_H
#define B2_BUTTERSCOTCH_COMPAT_H
#include <math.h>
#ifdef NO_SQRTF
static inline float b2CompatSqrtf(float x) { return (float)sqrt((double)x); }
#define sqrtf b2CompatSqrtf
#endif
#ifdef NO_REMAINDERF
static inline float b2CompatRemainderf(float x, float y) {
    double divisor = fabs((double)y);
    double r = fmod((double)x, divisor);
    double magnitude = fabs(r);
    double half = 0.5 * divisor;
    if (magnitude > half || (magnitude == half && fabs(fmod((double)x, 2.0 * divisor)) > divisor))
        r = r > 0 ? r - divisor : r + divisor;
    return (float)r;
}
#define remainderf b2CompatRemainderf
#endif
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define B2_JOIN_INNER(a, b) a##b
#define B2_JOIN(a, b) B2_JOIN_INNER(a, b)
#ifndef _Static_assert
#define _Static_assert(condition, message) typedef char B2_JOIN(b2_static_assert_, __LINE__)[(condition) ? 1 : -1]
#endif
#endif
#endif
