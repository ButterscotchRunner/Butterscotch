#include "gml_array.h"
#include "rvalue.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ensureLegacyRowCapacity(GMLArray* arr, int32_t minRows) {
    require(arr->type == GML_LEGACY_ARRAY);
    if (arr->legacy.rowCapacity >= minRows) return;
    int32_t newCap = arr->legacy.rowCapacity > 0 ? arr->legacy.rowCapacity : 4;
    while (minRows > newCap) newCap *= 2;
    arr->legacy.rows = safeRealloc(arr->legacy.rows, (uint32_t) newCap * sizeof(GMLArrayRow));
    memset(arr->legacy.rows + arr->legacy.rowCapacity, 0, (newCap - arr->legacy.rowCapacity) * sizeof(GMLArrayRow));
    arr->legacy.rowCapacity = newCap;
}

static void growLegacyRow(GMLArrayRow* row, int32_t minLength) {
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

GMLArray* GMLArray_create(int32_t wadVersion, int32_t initialLength) {
    GMLArrayType type = wadVersion >= 17 ? GML_MODERN_ARRAY : GML_LEGACY_ARRAY;
#ifndef ENABLE_WAD17
    require(type == GML_LEGACY_ARRAY);
#endif
    GMLArray* arr = safeCalloc(1, sizeof(GMLArray));
    arr->refCount = 1;
    arr->type = type;
    arr->owner = nullptr;
    GMLArray_growTo(arr, initialLength);
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

    if (arr->type == GML_LEGACY_ARRAY) {
        repeat(arr->legacy.rowCount, r) {
            GMLArrayRow* row = &arr->legacy.rows[r];
            repeat(row->length, c) {
                RValue_free(&row->data[c]);
            }
            free(row->data);
        }
        free(arr->legacy.rows);
    } else {
        repeat(arr->modern.length, r) {
            RValue_free(&arr->modern.data[r]);
        }
        free(arr->modern.data);
    }
    free(arr);
}

GMLArray* GMLArray_clone(GMLArray* src, void* newOwner) {
    if (src == nullptr) return nullptr;
    GMLArray* dst = safeCalloc(1, sizeof(GMLArray));
    dst->refCount = 1;
    dst->owner = newOwner;
    dst->type = src->type;
    if (src->type == GML_LEGACY_ARRAY) {
        if (src->legacy.rowCount > 0) {
            ensureLegacyRowCapacity(dst, src->legacy.rowCount);
            dst->legacy.rowCount = src->legacy.rowCount;
            repeat(src->legacy.rowCount, r) {
                GMLArrayRow* srcRow = &src->legacy.rows[r];
                GMLArrayRow* dstRow = &dst->legacy.rows[r];
                if (srcRow->length == 0) continue;
                growLegacyRow(dstRow, srcRow->length);
                repeat(srcRow->length, c) {
                    RValue srcVal = srcRow->data[c];
                    dstRow->data[c] = RValue_makeIndependent(srcVal);
                }
            }
        }
    } else {
        GMLArray_growTo(dst, src->modern.length);
        repeat(src->modern.length, i) {
            dst->modern.data[i] = RValue_makeIndependent(src->modern.data[i]);
        }
    }
    return dst;
}

void GMLArray_growTo(GMLArray* arr, int32_t minLength) {
    if (arr == nullptr || minLength <= 0) return;
    if (arr->type == GML_LEGACY_ARRAY) {
        int32_t idx = minLength - 1;
        int32_t row = idx / GML_LEGACY_ARRAY_STRIDE;
        int32_t col = idx % GML_LEGACY_ARRAY_STRIDE;
        ensureLegacyRowCapacity(arr, row + 1);
        if (row + 1 > arr->legacy.rowCount) arr->legacy.rowCount = row + 1;
        growLegacyRow(&arr->legacy.rows[row], col + 1);
    } else {
        if (arr->modern.length > minLength) return; // never shrink; already long enough
        if (minLength > arr->modern.capacity) {
            int32_t newCap = arr->modern.capacity > 0 ? arr->modern.capacity : 4;
            while (minLength > newCap) newCap *= 2; // geometric growth -> amortized O(1) appends
            arr->modern.data = safeRealloc(arr->modern.data, (uint32_t) newCap * sizeof(RValue));
            arr->modern.capacity = newCap;
        }
        // GameMaker fills uninitialized array slots with 0 (real)
        // Example: If you do "a[10] = 1", all values between 0..9 in the array MUST be read back as 0
        for (int32_t i = arr->modern.length; minLength > i; i++) {
            arr->modern.data[i] = RValue_makeReal(0);
        }
        arr->modern.length = minLength;
    }
}