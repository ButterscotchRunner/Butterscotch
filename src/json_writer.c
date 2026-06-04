#include "json_writer.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===[ Internal Helpers ]===

static void writeCommaIfNeeded(JsonWriter* writer) {
    if (writer->needsComma) {
        StringBuilder_appendChar(&writer->out, ',');
    }
}

static void writeEscapedString(JsonWriter* writer, const char* str) {
    StringBuilder_appendChar(&writer->out, '"');
    for (const char* p = str; *p != '\0'; p++) {
        unsigned char c = (unsigned char) *p;
        switch (c) {
            case '"':  StringBuilder_append(&writer->out, "\\\""); break;
            case '\\': StringBuilder_append(&writer->out, "\\\\"); break;
            case '\b': StringBuilder_append(&writer->out, "\\b");  break;
            case '\f': StringBuilder_append(&writer->out, "\\f");  break;
            case '\n': StringBuilder_append(&writer->out, "\\n");  break;
            case '\r': StringBuilder_append(&writer->out, "\\r");  break;
            case '\t': StringBuilder_append(&writer->out, "\\t");  break;
            default:
                if (32 > c) {
                    StringBuilder_appendFormat(&writer->out, "\\u%04x", c);
                } else {
                    StringBuilder_appendChar(&writer->out, (char) c);
                }
                break;
        }
    }
    StringBuilder_appendChar(&writer->out, '"');
}

// ===[ Lifecycle ]===

JsonWriter JsonWriter_create(void) {
    return (JsonWriter) {
        .out = StringBuilder_create(256),
        .needsComma = false,
    };
}

void JsonWriter_free(JsonWriter* writer) {
    StringBuilder_free(&writer->out);
}

// ===[ Structure ]===

void JsonWriter_beginObject(JsonWriter* writer) {
    writeCommaIfNeeded(writer);
    StringBuilder_appendChar(&writer->out, '{');
    writer->needsComma = false;
}

void JsonWriter_endObject(JsonWriter* writer) {
    StringBuilder_appendChar(&writer->out, '}');
    writer->needsComma = true;
}

void JsonWriter_beginArray(JsonWriter* writer) {
    writeCommaIfNeeded(writer);
    StringBuilder_appendChar(&writer->out, '[');
    writer->needsComma = false;
}

void JsonWriter_endArray(JsonWriter* writer) {
    StringBuilder_appendChar(&writer->out, ']');
    writer->needsComma = true;
}

// ===[ Object Keys ]===

void JsonWriter_key(JsonWriter* writer, const char* key) {
    writeCommaIfNeeded(writer);
    writeEscapedString(writer, key);
    StringBuilder_appendChar(&writer->out, ':');
    writer->needsComma = false;
}

// ===[ Values ]===

void JsonWriter_string(JsonWriter* writer, const char* value) {
    writeCommaIfNeeded(writer);
    if (value == nullptr) {
        StringBuilder_append(&writer->out, "null");
    } else {
        writeEscapedString(writer, value);
    }
    writer->needsComma = true;
}

void JsonWriter_int(JsonWriter* writer, int64_t value) {
    writeCommaIfNeeded(writer);
    StringBuilder_appendFormat(&writer->out, "%lld", (long long) value);
    writer->needsComma = true;
}

void JsonWriter_double(JsonWriter* writer, double value) {
    writeCommaIfNeeded(writer);
    StringBuilder_appendFormat(&writer->out, "%.17g", value);
    writer->needsComma = true;
}

void JsonWriter_rawValue(JsonWriter* writer, const char* formattedValue) {
    writeCommaIfNeeded(writer);
    StringBuilder_append(&writer->out, formattedValue);
    writer->needsComma = true;
}

void JsonWriter_bool(JsonWriter* writer, bool value) {
    writeCommaIfNeeded(writer);
    StringBuilder_append(&writer->out, value ? "true" : "false");
    writer->needsComma = true;
}

void JsonWriter_null(JsonWriter* writer) {
    writeCommaIfNeeded(writer);
    StringBuilder_append(&writer->out, "null");
    writer->needsComma = true;
}

// ===[ Property Convenience ]===

void JsonWriter_propertyString(JsonWriter* writer, const char* key, const char* value) {
    JsonWriter_key(writer, key);
    JsonWriter_string(writer, value);
}

void JsonWriter_propertyInt(JsonWriter* writer, const char* key, int64_t value) {
    JsonWriter_key(writer, key);
    JsonWriter_int(writer, value);
}

void JsonWriter_propertyDouble(JsonWriter* writer, const char* key, double value) {
    JsonWriter_key(writer, key);
    JsonWriter_double(writer, value);
}

void JsonWriter_propertyBool(JsonWriter* writer, const char* key, bool value) {
    JsonWriter_key(writer, key);
    JsonWriter_bool(writer, value);
}

void JsonWriter_propertyNull(JsonWriter* writer, const char* key) {
    JsonWriter_key(writer, key);
    JsonWriter_null(writer);
}

// ===[ Output ]===

const char* JsonWriter_getOutput(const JsonWriter* writer) {
    return StringBuilder_data(&writer->out);
}

char* JsonWriter_copyOutput(const JsonWriter* writer) {
    return safeStrdup(StringBuilder_data(&writer->out));
}

size_t JsonWriter_getLength(const JsonWriter* writer) {
    return StringBuilder_length(&writer->out);
}
