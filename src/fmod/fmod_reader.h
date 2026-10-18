#ifndef BS_FMOD_READER_H
#define BS_FMOD_READER_H
#include "common.h"
#include <stddef.h>
#include <string.h>

static inline uint16_t fm16(const uint8_t* p) {
    return (uint16_t) (p[0] | ((uint16_t) p[1] << 8));
}

static inline uint32_t fm24(const uint8_t* p) {
    return p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16);
}

static inline uint32_t fm32(const uint8_t* p) {
    return fm16(p) | ((uint32_t) fm16(p + 2) << 16);
}

static inline uint64_t fm64(const uint8_t* p) {
    return fm32(p) | ((uint64_t) fm32(p + 4) << 32);
}

static inline float fmfloat(const uint8_t* p) {
    uint32_t n = fm32(p);
    float value;
    memcpy(&value, &n, 4);
    return value;
}

static inline void fmput32(uint8_t* p, uint32_t n) {
    p[0] = (uint8_t) n;
    p[1] = (uint8_t) (n >> 8);
    p[2] = (uint8_t) (n >> 16);
    p[3] = (uint8_t) (n >> 24);
}

typedef struct {
    const uint8_t* data;
    size_t size, position;
    bool failed;
} FmReader;

static inline const uint8_t* fmread(FmReader* reader, size_t bytes) {
    if (reader->failed || reader->position > reader->size || bytes > reader->size - reader->position) {
        reader->failed = true;
        return nullptr;
    }
    const uint8_t* data = reader->data + reader->position;
    reader->position += bytes;
    return data;
}

// bit 15 means another word follows; the first word contributes 15 bits.
static inline uint32_t fmcount(FmReader* reader) {
    const uint8_t* data = fmread(reader, 2);
    if (!data) return 0;
    uint32_t value = fm16(data);
    if (value & 0x8000) {
        data = fmread(reader, 2);
        if (!data) return 0;
        value = (value & 0x7fff) | ((uint32_t) fm16(data) << 15);
    }
    return value;
}

static inline const uint8_t* fmlist(FmReader* reader, uint32_t* count, uint32_t* stride) {
    uint32_t encoded = fmcount(reader);
    *count = encoded >> 1;
    // this reads a contiguous list. markers and sustain points can have
    // different-sized entries, so those need to be walked one at a time.
    if (*count > 1 && !(encoded & 1)) {
        reader->failed = true;
        return nullptr;
    }
    *stride = *count ? fmcount(reader) : 0;
    if (*stride == 0 && *count) {
        reader->failed = true;
        return nullptr;
    }
    if (*count > reader->size / (*stride ? *stride : 1)) {
        reader->failed = true;
        return nullptr;
    }
    return fmread(reader, (size_t) *count * *stride);
}

typedef struct {
    FmReader* reader;
    uint32_t remaining;
    uint32_t stride;
    bool fixedStride;
} FmList;

static inline bool FmList_begin(FmList* list, FmReader* reader) {
    uint32_t encoded = fmcount(reader);
    list->reader = reader;
    list->remaining = encoded >> 1;
    list->fixedStride = (encoded & 1) != 0;
    list->stride = 0;
    if (list->fixedStride && list->remaining) {
        list->stride = fmcount(reader);
        if (!list->stride) reader->failed = true;
    }
    return !reader->failed;
}

static inline const uint8_t* FmList_next(FmList* list, uint32_t* bytes) {
    if (!list->remaining || list->reader->failed) return nullptr;
    *bytes = list->fixedStride ? list->stride : fmcount(list->reader);
    list->remaining--;
    return fmread(list->reader, *bytes);
}

#endif
