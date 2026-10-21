#include "native.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

_Noreturn void fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("error: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    exit(EXIT_FAILURE);
}

void *allocate(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) fail("allocation size overflow");
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) fail("out of memory allocating %zu bytes", count * size);
    return p;
}

void *resize_array(void *ptr, size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) fail("allocation size overflow");
    void *p = realloc(ptr, count && size ? count * size : 1);
    if (!p) fail("out of memory resizing to %zu bytes", count * size);
    return p;
}

char *copy_string(const char *text) {
    size_t n = strlen(text) + 1;
    char *result = allocate(n, 1);
    memcpy(result, text, n);
    return result;
}

char *format_string(const char *format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (size < 0) fail("cannot format string");
    char *result = allocate((size_t)size + 1, 1);
    vsnprintf(result, (size_t)size + 1, format, copy);
    va_end(copy);
    return result;
}

Slice section(Slice s, size_t at, size_t size) {
    if (at > s.size || size > s.size - at)
        fail("truncated binary data at offset %zu (need %zu bytes, file size %zu)", at, size, s.size);
    return (Slice){s.data ? s.data + at : NULL, size};
}
uint8_t u8(Slice s, size_t at) { return section(s, at, 1).data[0]; }
uint16_t u16(Slice s, size_t at) {
    const uint8_t *p = section(s, at, 2).data;
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
uint32_t u32(Slice s, size_t at) {
    const uint8_t *p = section(s, at, 4).data;
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int32_t i32(Slice s, size_t at) { uint32_t v = u32(s, at); int32_t r; memcpy(&r, &v, 4); return r; }
float f32(Slice s, size_t at) { uint32_t v = u32(s, at); float r; memcpy(&r, &v, 4); return r; }

void append(Buffer *out, const void *data, size_t size) {
    if (size > SIZE_MAX - out->size) fail("binary output size overflow");
    size_t need = out->size + size;
    if (need > out->capacity) {
        size_t cap = out->capacity ? out->capacity : 256;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        out->data = resize_array(out->data, cap, 1);
        out->capacity = cap;
    }
    if (size) memcpy(out->data + out->size, data, size);
    out->size = need;
}
void put8(Buffer *out, unsigned v) {
    if (v > UINT8_MAX) fail("value %u exceeds 8-bit output field", v);
    uint8_t byte = (uint8_t)v;
    append(out, &byte, 1);
}
void put16(Buffer *out, uint64_t v, bool big) {
    if (v > UINT16_MAX) fail("value %llu exceeds 16-bit output field", (unsigned long long)v);
    uint8_t p[2] = {(uint8_t)(v >> (big ? 8 : 0)), (uint8_t)(v >> (big ? 0 : 8))};
    append(out, p, 2);
}
void put32(Buffer *out, uint64_t v, bool big) {
    if (v > UINT32_MAX) fail("value %llu exceeds 32-bit output field", (unsigned long long)v);
    for (unsigned i = 0; i < 4; ++i) put8(out, (unsigned)(v >> ((big ? 3-i : i) * 8)) & 255);
}
void put64(Buffer *out, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) put8(out, (unsigned)(v >> (i * 8)) & 255);
}

Buffer read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) fail("cannot open '%s': %s", path, strerror(errno));
    Buffer result = {0};
    uint8_t bytes[65536];
    size_t n;
    while ((n = fread(bytes, 1, sizeof bytes, file))) append(&result, bytes, n);
    if (ferror(file)) fail("cannot read '%s': %s", path, strerror(errno));
    if (fclose(file)) fail("cannot close '%s': %s", path, strerror(errno));
    return result;
}
void write_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) fail("cannot create '%s': %s", path, strerror(errno));
    if (size && fwrite(data, 1, size, file) != size)
        fail("cannot write '%s': %s", path, strerror(errno));
    if (fclose(file)) fail("cannot close '%s': %s", path, strerror(errno));
}
char *join_path(const char *dir, const char *name) {
    return format_string("%s%s%s", dir, *dir && dir[strlen(dir)-1] != '/' ? "/" : "", name);
}
void mkdirs(const char *path) {
    if (!*path) fail("empty output directory");
    char *copy = copy_string(path);
    for (char *p = copy + 1;; ++p) {
        if (*p && *p != '/') continue;
        char saved = *p;
        *p = '\0';
        if (mkdir(copy, 0777) && errno != EEXIST)
            fail("cannot create directory '%s': %s", copy, strerror(errno));
        struct stat st;
        if (stat(copy, &st) || !S_ISDIR(st.st_mode)) fail("'%s' is not a directory", copy);
        *p = saved;
        if (!saved) break;
    }
    free(copy);
}
