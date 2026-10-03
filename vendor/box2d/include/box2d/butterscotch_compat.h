/* a few butterscotch additions: C99, single threaded builds on consoles */
#ifndef B2_BUTTERSCOTCH_COMPAT_H
#define B2_BUTTERSCOTCH_COMPAT_H
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define B2_JOIN_INNER(a, b) a##b
#define B2_JOIN(a, b) B2_JOIN_INNER(a, b)
#define _Static_assert(condition, message) typedef char B2_JOIN(b2_static_assert_, __LINE__)[(condition) ? 1 : -1]
#endif
#endif
