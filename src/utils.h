#ifndef _BS_UTILS_H_
#define _BS_UTILS_H_

#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "math_compat.h"

#include "real_type.h"

#ifdef PLATFORM_PS2
#include <malloc.h>
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
    #define TYPEOF(x) typeof(x)
#elif defined(_MSC_VER) && defined(__cplusplus) && __cplusplus >= 201103L
    #define TYPEOF(x) std::remove_reference<decltype(x)>::type
#elif defined(__GNUC__) || defined(__clang__) || \
    (defined(__TINYC__) && __TINYC__ >= 913) || \
    (defined(_MSC_VER) && _MSC_VER >= 1940 && !defined(__cplusplus))
    #define TYPEOF(x) __typeof__(x)
#else
    #define TYPEOF(x) long long
#endif

#if defined(__GNUC__) && (__GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96))
#define LIKELY(cond)   __builtin_expect(!!(cond), 1)
#define UNLIKELY(cond) __builtin_expect(!!(cond), 0)
#else
#define LIKELY(cond) (cond)
#define UNLIKELY(cond) (cond)
#endif

#define forEach(type, item, array, count) \
    for (TYPEOF(count) item##_i_ = 0; item##_i_ < (count); ++item##_i_) \
    for (type* item = &(array)[item##_i_]; item; item = NULL)

#define forEachIndexed(type, item, index, array, count) \
    for (TYPEOF(count) index = 0; index < (count); ++index) \
    for (type* item = &(array)[index]; item; item = NULL)

#define repeat(n, it) for (TYPEOF(n) it = 0; it < (n); ++it)

#define require(condition) \
    do { \
        if (!(condition)) { \
        fprintf(stderr, "Requirement failed at %s:%d\n", __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

#define requireMessage(condition, message) \
do { \
if (!(condition)) { \
fprintf(stderr, "Requirement failed at %s:%d: %s\n", __FILE__, __LINE__, message); \
abort(); \
} \
} while (0)

static inline void requireMessageFormatted(const char *file, int line, bool condition, const char *fmt, ...) {
    if (condition)
        return;
    va_list args;
    fprintf(stderr, "Requirement failed at %s:%d: ", file, line);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}

static inline void* requireNotNullFunction(void* ptr, const char* file, int line, const char* name) {
    if (!ptr) {
        fprintf(stderr, "%s:%d: requireNotNull failed: '%s'\n", file, line, name);
        abort();
    }
    return ptr;
}
#define requireNotNull(ptr) requireNotNullFunction((void*)ptr, __FILE__, __LINE__, #ptr)
#define requireNotNullMessage(ptr, msg) requireNotNullFunction((void*)ptr, __FILE__, __LINE__, msg)

// Call when memory is running low.  This triggers low memory alarm callback
// functions which evict caches from memory to free it up.
bool lowMemoryAlarm();

// Registers a low memory alarm callback.  When a low memory alarm is triggered,
// the function provided will be called, and it should free memory if possible.
void registerLowMemoryAlarmCallback(bool(*callbackFunction)(void));

// Safe allocation macros - check for nullptr and abort with file/line info
static inline void *safeMallocFunction(size_t size, const char *file, int line) {
    while (true) {
        void* ret = malloc(size);
        if (LIKELY(ret)) {
            return ret;
        }
        if (!lowMemoryAlarm()) {
            fprintf(stderr, "FATAL: malloc(%zu) failed at %s:%d\n", size, file, line);
            abort();
        }
    }
}
#define safeMalloc(size) safeMallocFunction(size, __FILE__, __LINE__)

static inline void *safeCallocFunction(size_t count, size_t size, const char *file, int line) {
    while (true) {
        void *ret = calloc(count, size);
        if (LIKELY(ret)) {
            return ret;
        }
        if (!lowMemoryAlarm()) {
            fprintf(stderr, "FATAL: calloc(%zu, %zu) failed at %s:%d\n", count, size, file, line);
            abort();
        }
    }
}
#define safeCalloc(count, size) safeCallocFunction(count, size, __FILE__, __LINE__)

static inline void *safeReallocFunction(void *ptr, size_t size, const char *file, int line) {
    while (true) {
        void *ret = realloc(ptr, size);
        if (LIKELY(ret)) {
            return ret;
        }
        if (!lowMemoryAlarm()) {
            fprintf(stderr, "FATAL: realloc(%zu) failed at %s:%d\n", size, file, line);
            abort();
        }
    }
}
#define safeRealloc(ptr, size) safeReallocFunction(ptr, size, __FILE__, __LINE__)

#ifdef PLATFORM_PS2

static inline void *safeMemalignFunction(size_t alignment, size_t size, const char *file, int line) {
    while (true) {
        void *ret = memalign(alignment, size);
        if (LIKELY(ret)) {
            return ret;
        }
        if (!lowMemoryAlarm()) {
            fprintf(stderr, "FATAL: memalign(%zu, %zu) failed at %s:%d\n", alignment, size, file, line);
            abort();
        }
    }
}
#define safeMemalign(alignment, size) safeMemalignFunction(alignment, size, __FILE__, __LINE__)

#endif

// Reads exactly n bytes or aborts with the "pathForError" that caused the error.
static inline void safeFreadFunction(void *dst, size_t n, FILE *read_file, const char *pathForError, const char *file, int line) {
    if (fread(dst, 1, n, read_file) != n) {
        fprintf(stderr, "FATAL: failed to read %zu bytes from %s at %s:%d\n", n, pathForError, file, line);
        abort();
    }
}
#define safeFread(dst, n, file, pathForError) safeFreadFunction(dst, n, file, pathForError, __FILE__, __LINE__)

static inline char *safeStrdupFunction(const char *str, const char *file, int line) {
    char *ret = strdup(str);
    if (!ret) {
        fprintf(stderr, "FATAL: strdup() failed at %s:%d\n", file, line);
        abort();
    }
    return ret;
}
#define safeStrdup(str) safeStrdupFunction(str, __FILE__, __LINE__)

#define ZERO_STRUCT(s) memset(&(s), 0, sizeof(s))

// Truncates to 6 decimal places, matching the HTML5 runner's ClampFloat
static inline GMLReal clampFloat(GMLReal f) {
    return ((GMLReal) ((int64_t) (f * 1000000.0))) / 1000000.0;
}

#define BGR_B(c) (((c) >> 16) & 0xFF)
#define BGR_G(c) (((c) >>  8) & 0xFF)
#define BGR_R(c) (((c) >>  0) & 0xFF)
#define BGR_A(c) (((c) >> 24) & 0xFF)

// Mixes 2 colors with a blend factor
static inline int32_t Color_lerp(int32_t color1, int32_t color2, float blending) {
    int32_t r1 = BGR_R(color1), g1 = BGR_G(color1), b1 = BGR_B(color1);
    int32_t r2 = BGR_R(color2), g2 = BGR_G(color2), b2 = BGR_B(color2);
    float inv = 1.0f - blending;
    int32_t r = (int32_t)((float) r2 * blending + (float) r1 * inv) & 0xFF;
    int32_t g = (int32_t)((float) g2 * blending + (float) g1 * inv) & 0xFF;
    int32_t b = (int32_t)((float) b2 * blending + (float) b1 * inv) & 0xFF;
    return r | (g << 8) | (b << 16);
}

#define shcopyFromTo(src, dst)                        \
do {                                        \
(dst) = NULL;                           \
for (int i = 0; i < shlen(src); i++)    \
shput((dst), (src)[i].key, (src)[i].value); \
} while (0)

typedef struct {
    char* key;
    bool value;
} StringBooleanEntry;

#endif /* _BS_UTILS_H_ */
