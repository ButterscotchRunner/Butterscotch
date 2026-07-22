#ifndef _BS_MATH_COMPAT_H_
#define _BS_MATH_COMPAT_H_

#include <math.h>
#include "real_type.h"

/* This header is for compatibility with systems that don't have full C99 math libraries. */

/*
 * This fmin and fmax don't handle some semantics with negative zero correctly.
 * Handling them correctly would add complexity and hurt performance and it probably doesn't matter anyway.
 */
#ifdef NO_FMIN

#undef GMLReal_fmin
static GMLReal GMLReal_fmin(GMLReal a, GMLReal b) {
    if (a != a) return b;
    if (b != b) return a;
    return a < b ? a : b;
}

#endif

#ifdef NO_FMAX

#undef GMLReal_fmax
static GMLReal GMLReal_fmax(GMLReal a, GMLReal b) {
    if (a != a) return b;
    if (b != b) return a;
    return a > b ? a : b;
}

#endif

#ifdef NO_ROUND

#undef GMLReal_round
static GMLReal GMLReal_round(GMLReal x) {
    if (x >= 9007199254740992.0 || x <= -9007199254740992.0) return x;
    if (x >= 0.0) return (GMLReal)((int64_t)(x + 0.5));
    else          return (GMLReal)((int64_t)(x - 0.5));
}

#endif

#ifdef NO_LOG2

#undef GMLReal_log2
static GMLReal GMLReal_log2(GMLReal x) { return log(x) * 1.4426950408889634; }

#endif

#ifdef NO_LROUND

static long lround(double x) {
    if (x >= 9007199254740992.0 || x <= -9007199254740992.0) return (long)x;
    if (x >= 0.0) return (long)((int64_t)(x + 0.5));
    else          return (long)((int64_t)(x - 0.5));
}

#endif

#ifdef NO_SQRTF

static float sqrtf(float x) {
    return sqrt(x);
}

#endif

#ifdef NO_FABSF

static float fabsf(float x) {
    return fabs(x);
}

#endif

#ifdef NO_FMODF

static float fmodf(float x, float y) {
    return fmod(x, y);
}

#endif

#ifdef NO_SINF

static float sinf(float x) { return sin(x); }

#endif

#ifdef NO_COSF

static float cosf(float x) { return cos(x); }

#endif

#ifdef NO_ROUNDF

static float roundf(float x) {
    if (x >= 2147483648.0f || x <= -2147483648.0f) return x;
    if (x >= 0.0f) return (float)((int32_t)(x + 0.5f));
    else           return (float)((int32_t)(x - 0.5f));
}

#endif

#ifndef INFINITY
#define INFINITY (1.0f / 0.0f)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif /* _BS_MATH_COMPAT_H_ */
