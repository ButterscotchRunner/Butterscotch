#ifndef _CLOCK_GETTIME_MACOS_
#define _CLOCK_GETTIME_MACOS_

/* Apple didn't add clock_gettime to macOS until 10.13, here's an emulation I had laying around */

#ifdef __APPLE__

#if !(((defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) ||             \
        defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)) &&                \
       __ENVIRONMENT_OS_VERSION_MIN_REQUIRED__ >= 100000) ||                   \
      (defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) &&               \
       __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101200) ||             \
      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) &&               \
       __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ >= 30000))

#include <dlfcn.h>
#include <errno.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <sys/time.h>

#ifndef CLOCK_REALTIME
typedef int clockid_t;
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif
#ifndef CLOCK_MONOTONIC_RAW_APPROX
#define CLOCK_MONOTONIC_RAW_APPROX 5
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 6
#endif
#ifndef CLOCK_UPTIME_RAW
#define CLOCK_UPTIME_RAW 8
#endif
#ifndef CLOCK_UPTIME_RAW_APPROX
#define CLOCK_UPTIME_RAW_APPROX 9
#endif
#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 12
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 16
#endif

#define clock_gettime __butterscotch_clock_gettime
#define mach_time_factor __butterscotch_mach_time_factor

extern uint64_t __thread_selfusage(void);

static double mach_time_factor = 0.0;

#if (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) &&                \
     __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ >= 80000) ||               \
    (defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) &&                 \
     __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101000) ||               \
    defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) ||                     \
    defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__)

#define HIRES_THREAD_TIME

#endif

static int clock_gettime(clockid_t clockid, struct timespec *ts) {

#if (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) &&                \
     __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ >= 20000) ||               \
    !defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)

  static bool init = false;
  static int (*func)(clockid_t, struct timespec *);

  if (!init) {
    func = (int (*)(clockid_t, struct timespec *))dlsym(RTLD_NEXT,
                                                        "clock_gettime");
    init = true;
  }

  if (func)
    return func(clockid, ts);

#endif

  uint64_t mach_time;

  switch (clockid) {
  case CLOCK_REALTIME: {
    struct timeval tv;
    int ret = gettimeofday(&tv, NULL);
    if (ret == -1)
      return -1;
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
  }
  case CLOCK_PROCESS_CPUTIME_ID: {
    struct rusage ru;
    int ret = getrusage(RUSAGE_SELF, &ru);
    timeradd(&ru.ru_utime, &ru.ru_stime, &ru.ru_utime);
    ts->tv_sec = ru.ru_utime.tv_sec;
    ts->tv_nsec = ru.ru_utime.tv_usec * 1000;
    return ret;
  }
#ifdef HIRES_THREAD_TIME
  case CLOCK_THREAD_CPUTIME_ID:
    mach_time = __thread_selfusage();
    break;
  case CLOCK_MONOTONIC_RAW_APPROX:
  case CLOCK_UPTIME_RAW_APPROX:
    mach_time = mach_approximate_time();
    break;
#else
  case CLOCK_THREAD_CPUTIME_ID: {
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    thread_port_t thread = mach_thread_self();
    int thread_info_ret =
        thread_info(thread, THREAD_BASIC_INFO, (thread_info_t)(&info), &count);
    mach_port_deallocate(mach_task_self(), thread);
    if (thread_info_ret != 0)
      return -1;

    ts->tv_sec = info.user_time.seconds + info.system_time.seconds;
    ts->tv_nsec =
        (info.user_time.microseconds + info.system_time.microseconds) * 1000;
    if (ts->tv_nsec >= 1000000000) {
      ts->tv_sec += 1;
      ts->tv_nsec -= 1000000000;
    }
    if (ts->tv_sec == 0 && ts->tv_nsec == 0)
      ts->tv_nsec = 1;

    return 0;
  }
  case CLOCK_MONOTONIC_RAW_APPROX:
  case CLOCK_UPTIME_RAW_APPROX:
#endif
  case CLOCK_MONOTONIC:
  case CLOCK_MONOTONIC_RAW:
  case CLOCK_UPTIME_RAW:
    mach_time = mach_absolute_time();
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  if (mach_time_factor == 0.0) {
    mach_timebase_info_data_t machinfo;
    if (mach_timebase_info(&machinfo) != KERN_SUCCESS)
      return -1;
    if (machinfo.numer == machinfo.denom)
      mach_time_factor = 1.0;
    else
      mach_time_factor = (double)machinfo.numer / machinfo.denom;
  }
  uint64_t nsec = mach_time * mach_time_factor;

  ts->tv_sec = nsec / 1000000000;
  ts->tv_nsec = nsec % 1000000000;
  return 0;
}

#undef mach_time_factor

#endif

#endif

#endif
