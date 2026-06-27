#ifndef _BS_STRING_BUILDER_H_
#define _BS_STRING_BUILDER_H_

#include <common.h>
#include <stdint.h>
#include <stddef.h>

// Java-style growable string builder. Owns a heap buffer that doubles in capacity when appends would overflow it. Always keeps a null terminator.
typedef struct {
    char* buffer;
    size_t length;
    size_t capacity;
} StringBuilder;

// Creates a StringBuilder with the given initial capacity (clamped to at least 16 bytes). The buffer starts as an empty null-terminated string.
StringBuilder StringBuilder_create(size_t initialCapacity);

// Frees the buffer. After this call the StringBuilder must not be used.
void StringBuilder_free(StringBuilder* sb);

// Ensures the buffer can hold at least `additionalBytes` more content (plus a null terminator) without reallocating. Doubles capacity until it fits.
void StringBuilder_ensureCapacity(StringBuilder* sb, size_t additionalBytes);

// Appends a single character.
void StringBuilder_appendChar(StringBuilder* sb, char c);

// Appends "len" bytes from "data" (may include embedded zeros; the buffer is still null-terminated after).
void StringBuilder_appendBytes(StringBuilder* sb, const char* data, size_t len);

// Appends a null-terminated C string.
void StringBuilder_append(StringBuilder* sb, const char* str);

// Appends a printf-formatted string.
void StringBuilder_appendFormat(StringBuilder* sb, const char* fmt, ...);

// Returns the current null-terminated buffer. Pointer remains valid until the next append.
const char* StringBuilder_data(const StringBuilder* sb);

// Current content length in bytes (not counting the null terminator).
size_t StringBuilder_length(const StringBuilder* sb);

// Returns a heap-allocated copy of the current contents. The StringBuilder is unchanged and may still be appended to or freed. The caller must free() the returned pointer.
char* StringBuilder_toString(const StringBuilder* sb);

#endif /* _BS_STRING_BUILDER_H_ */
