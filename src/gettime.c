#include "gettime.h"

#if defined(PLATFORM_PS2)
#include <timer.h>
#elif defined(PLATFORM_PS3)
#include <sys/systime.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#ifndef CLOCK_MONOTONIC
#include <sys/time.h>
#endif
#endif

uint64_t nowNanos(void) {
#if defined(PLATFORM_PS2)
    // kBUSCLK is bus clock ticks per second (~147 MHz).
    // Split to avoid u64 overflow in ticks * 1e9.
    uint64_t t = (uint64_t) GetTimerSystemTime();
    uint64_t clk = (uint64_t) kBUSCLK;
    uint64_t sec = t / clk;
    uint64_t rem = t % clk;
    return sec * 1000000000 + (rem * 1000000000) / clk;
#elif defined(PLATFORM_PS3)
    return ((double)__builtin_ppc_get_timebase()/(double)sysGetTimebaseFrequency());
#elif defined(_WIN32)
    static uint64_t freq = 0;
    if (freq == 0) {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart == 0) {
            /* No high-res counter: fall back to GetTickCount (1 ms res). */
            return (int64_t)GetTickCount() * 1000000;
        }
        freq = (uint64_t)f.QuadPart;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t count = (uint64_t) now.QuadPart;
    uint64_t whole = count / freq;
    uint64_t frac  = count % freq;
    return whole * 1000000000 + frac * 1000000000 / freq;
#elif defined(__APPLE__)
    /*
     * clock_gettime() was only introduced in macOS 10.12 in 2016, this path works on every
     * macOS version, maybe even rhapsody. It is also actually accurate to nanosecond precision,
     * unlike clock_gettime(), which secretly truncates down to the nearest multiple of 1000.
     * It is probably also faster than clock_gettime(), since the kernel does all this same
     * logic behind the scenes, plus some other stuff. The only difference in behavior is that
     * it doesn't count time spent sleeping, which is probably undesirable for this anyway.
     */
    static double mach_time_factor = 0.0;
    if (mach_time_factor == 0.0) {
        mach_timebase_info_data_t machinfo;
        mach_timebase_info(&machinfo);
        mach_time_factor = (double)machinfo.numer / machinfo.denom;
    }
    return mach_absolute_time() * mach_time_factor;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000 + (uint64_t) ts.tv_nsec;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t) tv.tv_sec * 1000000000 + (uint64_t) tv.tv_usec * 1000;
#endif
}
