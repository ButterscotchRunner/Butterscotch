#pragma once

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#include "real_type.h"

#if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)) \
    || defined(__GNUC__) || defined(__clang__) || defined(__TINYC__)
    // The "typeof((typeof(x))0" is used to remove the "const" from the typeof
    #define TYPEOF(x) typeof((typeof(x))0)
#else
    #define TYPEOF(x) long long
#endif

#define forEach(type, item, array, count) \
    for (TYPEOF(count) item##_i_ = 0; item##_i_ < (long long)(count); item##_i_++) \
    for (type* item = &(array)[item##_i_]; item; item = 0)

#define forEachIndexed(type, item, index, array, count) \
    for (TYPEOF(count) index = 0; index < (long long)(count); index++) \
    for (type* item = &(array)[index]; item; item = 0)

#define repeat(n, it) for (TYPEOF(n) it = 0; it < (long long)(n); it++)

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

#define requireMessageFormatted(condition, fmt, ...) \
do { \
if (!(condition)) { \
fprintf(stderr, "Requirement failed at %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
abort(); \
} \
} while (0)

static inline void* requireNotNullFunction(void* ptr, char* file, int line, char* name) {
    if (ptr == nullptr) {
        fprintf(stderr, "%s:%d: requireNotNull failed: '%s'\n", file, line, name);
        abort();
    }
    return ptr;
}
#define requireNotNull(ptr) requireNotNullFunction((void*)ptr, __FILE__, __LINE__, #ptr)

#define requireNotNullMessage(ptr, msg) requireNotNullFunction((void*)ptr, __FILE__, __LINE__, msg)

// Safe allocation macros - check for nullptr and abort with file/line info
static inline void* safeMallocFunction(size_t size, char* file, int line) {
    void* _ptr = malloc(size);
    if (_ptr == nullptr) {
        fprintf(stderr, "FATAL: malloc(%zu) failed at %s:%d\n", size, file, line);
        abort();
    }
    return _ptr;
}
#define safeMalloc(size) safeMallocFunction(size, __FILE__, __LINE__)

static inline void* safeCallocFunction(size_t count, size_t size, char* file, int line) {
    void* _ptr = calloc(count, size);
    if (_ptr == nullptr) {
        fprintf(stderr, "FATAL: calloc(%zu, %zu) failed at %s:%d\n", count, size, file, line);
        abort();
    }
    return _ptr;
}
#define safeCalloc(count, size) safeCallocFunction(count, size, __FILE__, __LINE__)

static inline void* safeReallocFunction(void* ptr, size_t size, char* file, int line) {
    void* _ptr = realloc(ptr, size);
    if (_ptr == nullptr) {
        fprintf(stderr, "FATAL: realloc(%zu) failed at %s:%d\n", size, file, line);
        abort();
    }
    return _ptr;
}
#define safeRealloc(ptr, size) safeReallocFunction(ptr, size, __FILE__, __LINE__)

#if defined(PLATFORM_PS2)
#define safeMemalign(alignment, size) ({ \
    void* _ptr = memalign(alignment, size); \
    if (_ptr == nullptr) { \
        fprintf(stderr, "FATAL: memalign(%zu, %zu) failed at %s:%d\n", (size_t)(alignment), (size_t)(size), __FILE__, __LINE__); \
        abort(); \
    } \
    _ptr; \
})
#endif

static inline char* safeStrdupFunction(const char* str, char* file, int line) {
    char* _ptr = strdup(str);
    if (_ptr == nullptr) {
        fprintf(stderr, "FATAL: strdup() failed at %s:%d\n", file, line);
        abort();
    }
    return _ptr;
}
#define safeStrdup(str) safeStrdupFunction(str, __FILE__, __LINE__)

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
    int32_t r = lrintf((float) r2 * blending + (float) r1 * inv) & 0xFF;
    int32_t g = lrintf((float) g2 * blending + (float) g1 * inv) & 0xFF;
    int32_t b = lrintf((float) b2 * blending + (float) b1 * inv) & 0xFF;
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