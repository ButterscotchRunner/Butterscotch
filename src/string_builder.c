#include "string_builder.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define STRING_BUILDER_MIN_CAPACITY 16

StringBuilder StringBuilder_create(size_t initialCapacity) {
    if (STRING_BUILDER_MIN_CAPACITY > initialCapacity) initialCapacity = STRING_BUILDER_MIN_CAPACITY;
    char* buffer = safeMalloc(initialCapacity);
    buffer[0] = '\0';
    return (StringBuilder) {
        .buffer = buffer,
        .length = 0,
        .capacity = initialCapacity,
    };
}

void StringBuilder_free(StringBuilder* sb) {
    free(sb->buffer);
    sb->buffer = nullptr;
    sb->length = 0;
    sb->capacity = 0;
}

void StringBuilder_clear(StringBuilder* sb) {
    sb->length = 0;
    if (sb->buffer != nullptr) sb->buffer[0] = '\0';
}

void StringBuilder_ensureCapacity(StringBuilder* sb, size_t additionalBytes) {
    size_t required = sb->length + additionalBytes + 1;
    if (sb->capacity >= required) return;

    size_t newCapacity = sb->capacity;
    while (required > newCapacity) {
        newCapacity *= 2;
    }
    sb->buffer = safeRealloc(sb->buffer, newCapacity);
    sb->capacity = newCapacity;
}

void StringBuilder_appendChar(StringBuilder* sb, char c) {
    StringBuilder_ensureCapacity(sb, 1);
    sb->buffer[sb->length++] = c;
    sb->buffer[sb->length] = '\0';
}

void StringBuilder_appendBytes(StringBuilder* sb, const char* data, size_t len) {
    if (len == 0) return;
    StringBuilder_ensureCapacity(sb, len);
    memcpy(sb->buffer + sb->length, data, len);
    sb->length += len;
    sb->buffer[sb->length] = '\0';
}

void StringBuilder_append(StringBuilder* sb, const char* str) {
    if (str == nullptr) return;
    StringBuilder_appendBytes(sb, str, strlen(str));
}

void StringBuilder_appendFormat(StringBuilder* sb, const char* fmt, ...) {
    // First pass: measure required length with a throwaway vsnprintf on the existing tail.
    va_list ap;
    va_start(ap, fmt);
    size_t available = sb->capacity - sb->length;
    int needed = vsnprintf(sb->buffer + sb->length, available, fmt, ap);
    va_end(ap);
    if (0 >= needed) return;

    // If it fits, we're done (vsnprintf already wrote the bytes and the null terminator).
    if (available > (size_t) needed) {
        sb->length += (size_t) needed;
        return;
    }

    // Didn't fit: grow and retry.
    StringBuilder_ensureCapacity(sb, (size_t) needed);
    va_start(ap, fmt);
    vsnprintf(sb->buffer + sb->length, sb->capacity - sb->length, fmt, ap);
    va_end(ap);
    sb->length += (size_t) needed;
}

const char* StringBuilder_data(const StringBuilder* sb) {
    return sb->buffer;
}

size_t StringBuilder_length(const StringBuilder* sb) {
    return sb->length;
}

char* StringBuilder_toString(const StringBuilder* sb) {
    return safeStrdup(sb->buffer);
}
