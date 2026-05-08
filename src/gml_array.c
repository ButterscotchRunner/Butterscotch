#include "gml_array.h"
#include "rvalue.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ensureRowCapacity(GMLArray* arr, int32_t minRows) {
    if (arr->rowCapacity >= minRows) return;
    int32_t newCap = arr->rowCapacity > 0 ? arr->rowCapacity : 4;
    while (minRows > newCap) newCap *= 2;
    arr->rows = safeRealloc(arr->rows, (uint32_t) newCap * sizeof(GMLArrayRow));
    for (int32_t i = arr->rowCapacity; newCap > i; i++) {
        arr->rows[i] = (GMLArrayRow){ .length = 0, .capacity = 0, .data = nullptr };
    }
    arr->rowCapacity = newCap;
}

static void growRow(GMLArrayRow* row, int32_t minLength) {
    if (row->length >= minLength) return;
    if (minLength > row->capacity) {
        int32_t newCap = row->capacity > 0 ? row->capacity : 4;
        while (minLength > newCap) newCap *= 2;
        row->data = safeRealloc(row->data, (uint32_t) newCap * sizeof(RValue));
        row->capacity = newCap;
    }
    // GameMaker fills uninitialized array slots with 0 (real).
    // Example: If you do "a[10] = 1", all values between 0..9 in the array MUST be read back as 0.
    for (int32_t i = row->length; minLength > i; i++) {
        row->data[i] = RValue_makeReal(0);
    }
    row->length = minLength;
}

GMLArray* GMLArray_create(int32_t initialLength) {
    GMLArray* arr = safeCalloc(1, sizeof(GMLArray));
    arr->refCount = 1;
    arr->owner = nullptr;
    if (initialLength > 0) {
        ensureRowCapacity(arr, 1);
        growRow(&arr->rows[0], initialLength);
        arr->rowCount = 1;
    }
    return arr;
}

void GMLArray_incRef(GMLArray* arr) {
    if (arr == nullptr) return;
    arr->refCount++;
}

void GMLArray_decRef(GMLArray* arr) {
    if (arr == nullptr) return;
    require(arr->refCount > 0);
    arr->refCount--;
    if (arr->refCount > 0) return;

    repeat(arr->rowCount, r) {
        GMLArrayRow* row = &arr->rows[r];
        repeat(row->length, c) {
            RValue_free(&row->data[c]);
        }
        free(row->data);
    }
    free(arr->rows);
    free(arr);
}

GMLArray* GMLArray_clone(GMLArray* src, void* newOwner) {
    if (src == nullptr) return nullptr;
    GMLArray* dst = safeCalloc(1, sizeof(GMLArray));
    dst->refCount = 1;
    dst->owner = newOwner;
    if (src->rowCount > 0) {
        ensureRowCapacity(dst, src->rowCount);
        dst->rowCount = src->rowCount;
        repeat(src->rowCount, r) {
            GMLArrayRow* srcRow = &src->rows[r];
            GMLArrayRow* dstRow = &dst->rows[r];
            if (srcRow->length == 0) continue;
            growRow(dstRow, srcRow->length);
            repeat(srcRow->length, c) {
                RValue srcVal = srcRow->data[c];
                // Duplicate owned strings: for nested arrays, share the inner array (bump refCount).
                // Inner arrays get their own CoW check on first write through the new outer slot.
                if (srcVal.type == RVALUE_STRING && srcVal.ownsReference && srcVal.string != nullptr) {
                    dstRow->data[c] = RValue_makeOwnedString(safeStrdup(srcVal.string));
                } else if (srcVal.type == RVALUE_ARRAY && srcVal.array != nullptr) {
                    GMLArray_incRef(srcVal.array);
                    dstRow->data[c] = srcVal;
                    dstRow->data[c].ownsReference = true;
#if IS_BC17_OR_HIGHER_ENABLED
                } else if (srcVal.type == RVALUE_METHOD && srcVal.method != nullptr) {
                    GMLMethod_incRef(srcVal.method);
                    dstRow->data[c] = srcVal;
                    dstRow->data[c].ownsReference = true;
#endif
                } else if (srcVal.type == RVALUE_STRUCT && srcVal.structInst != nullptr) {
                    Instance_structIncRef(srcVal.structInst);
                    dstRow->data[c] = srcVal;
                    dstRow->data[c].ownsReference = true;
                } else {
                    dstRow->data[c] = srcVal;
                    dstRow->data[c].ownsReference = false;
                }
            }
        }
    }
    return dst;
}

void GMLArray_growTo(GMLArray* arr, int32_t minLength) {
    if (arr == nullptr || minLength <= 0) return;
    int32_t idx = minLength - 1;
    int32_t row = idx / GML_ARRAY_STRIDE;
    int32_t col = idx % GML_ARRAY_STRIDE;
    ensureRowCapacity(arr, row + 1);
    if (row + 1 > arr->rowCount) arr->rowCount = row + 1;
    growRow(&arr->rows[row], col + 1);
}

