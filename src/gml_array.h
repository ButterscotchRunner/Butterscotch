#pragma once
#include <stdint.h>
#include <stddef.h>
#include "common.h"
#include "rvalue.h"

// ===[ GMLArray - Refcounted 2D jagged RValue array ]===
// Matches the native GMS 1.4 (BC16) runner storage: an array of rows, each row a dynamic RValue buffer.
// Flat indices from bytecode split as row = idx / GML_ARRAY_STRIDE, col = idx % GML_ARRAY_STRIDE.
// - 1D accesses (a[i], i < 32000) all land in row 0.
// - 2D accesses use packed index `i * 32000 + j` (native GMS convention); splits to row i, col j.
//
// BC16 (GMS 1.4): "owner" stores a pointer to the RValue slot that "owns" the array (first slot to write). Write through a different slot with refCount > 1 triggers a fork (matches native `SET_RValue`).
// BC17+ (GMS 2.3): "owner" stores an opaque scope token set by BREAK_SETOWNER. Write with mismatching current owner triggers a fork (matches native `SET_RValue_Array` + `g_CurrentArrayOwner`).
//
// "b = a" bumps refCount and shares, never clones eagerly. All forking happens lazily on write.

#define GML_ARRAY_STRIDE 32000

typedef struct GMLArrayRow {
    int32_t length;
    int32_t capacity;
    RValue* data;
} GMLArrayRow;

struct GMLArray {
    int32_t refCount;
    int32_t rowCount; // Highest touched row index + 1.
    int32_t rowCapacity; // Allocated slots in rows[].
    void* owner;
    GMLArrayRow* rows;
};

GMLArray* GMLArray_create(int32_t initialLength);
void GMLArray_incRef(GMLArray* arr);
// Decrement refCount. If it reaches 0, free all inner RValues + row buffers + struct. Safe on nullptr.
void GMLArray_decRef(GMLArray* arr);
// Deep copy. Every inner owned-string is strdup'd. Nested arrays have their refCount bumped (shared by default).
// New array starts at refCount=1, same shape as src, owner=newOwner.
GMLArray* GMLArray_clone(GMLArray* src, void* newOwner);
// Ensure flat index (minLength - 1) is writable: grow row (idx / STRIDE) to at least (col + 1) entries, filling gaps with RVALUE_UNDEFINED.
void GMLArray_growTo(GMLArray* arr, int32_t minLength);

// Pointer to the slot at flat index, or nullptr if out of range. Call GMLArray_growTo first if writing.

static inline RValue* GMLArray_slot(GMLArray* arr, int32_t index) {
    if (arr == nullptr || 0 > index) return nullptr;
    if (GML_ARRAY_STRIDE > index) {
        // Fast path: For the common 32000 > idx (row 0), skip the div/mod entirely.
        if (arr->rowCount == 0) return nullptr;
        GMLArrayRow* row0 = &arr->rows[0];
        if (index >= row0->length) return nullptr;
        return &row0->data[index];
    }
    int32_t row = index / GML_ARRAY_STRIDE;
    int32_t col = index % GML_ARRAY_STRIDE;
    if (row >= arr->rowCount) return nullptr;
    GMLArrayRow* r = &arr->rows[row];
    if (col >= r->length) return nullptr;
    return &r->data[col];
}

// Length of row 0.
static inline int32_t GMLArray_length1D(const GMLArray* arr) {
    if (arr == nullptr || arr->rowCount == 0) return 0;
    return arr->rows[0].length;
}

// Number of rows.
static inline int32_t GMLArray_height2D(const GMLArray* arr) {
    if (arr == nullptr) return 0;
    return arr->rowCount;
}

// Length of a specific row.
static inline int32_t GMLArray_rowLength(const GMLArray* arr, int32_t row) {
    if (arr == nullptr || row < 0 || row >= arr->rowCount) return 0;
    return arr->rows[row].length;
}
