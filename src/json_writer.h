#ifndef _BS_JSON_WRITER_H_
#define _BS_JSON_WRITER_H_

#include "common.h"
#include "string_builder.h"
#include <stdint.h>
#include <stddef.h>

// ===[ JsonWriter Type ]===

typedef struct {
    StringBuilder out;
    bool needsComma;
} JsonWriter;

// ===[ Lifecycle ]===

JsonWriter JsonWriter_create(void);
void JsonWriter_free(JsonWriter* writer);

// ===[ Structure ]===

void JsonWriter_beginObject(JsonWriter* writer);
void JsonWriter_endObject(JsonWriter* writer);
void JsonWriter_beginArray(JsonWriter* writer);
void JsonWriter_endArray(JsonWriter* writer);

// ===[ Object Keys ]===

void JsonWriter_key(JsonWriter* writer, const char* key);

// ===[ Values ]===

void JsonWriter_string(JsonWriter* writer, const char* value);
void JsonWriter_int(JsonWriter* writer, int64_t value);
void JsonWriter_double(JsonWriter* writer, double value);
// Writes an already-formatted JSON value verbatim, with no quoting or escaping.
// The caller is responsible for it being a valid, self-contained JSON value (example: a number literal, escaped and quoted string value, "true"/"false"/"null", or a complete object/array).
void JsonWriter_rawValue(JsonWriter* writer, const char* formattedValue);
void JsonWriter_bool(JsonWriter* writer, bool value);
void JsonWriter_null(JsonWriter* writer);

// ===[ Property Convenience ]===

void JsonWriter_propertyString(JsonWriter* writer, const char* key, const char* value);
void JsonWriter_propertyInt(JsonWriter* writer, const char* key, int64_t value);
void JsonWriter_propertyDouble(JsonWriter* writer, const char* key, double value);
void JsonWriter_propertyBool(JsonWriter* writer, const char* key, bool value);
void JsonWriter_propertyNull(JsonWriter* writer, const char* key);

// ===[ Output ]===

const char* JsonWriter_getOutput(const JsonWriter* writer);
char* JsonWriter_copyOutput(const JsonWriter* writer);
size_t JsonWriter_getLength(const JsonWriter* writer);

#endif /* _BS_JSON_WRITER_H_ */
