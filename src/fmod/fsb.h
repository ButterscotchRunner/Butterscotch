#ifndef BS_FSB_H
#define BS_FSB_H
#include "common.h"
#include <stddef.h>

typedef struct {
    uint32_t offset, bytes, frames, rate, channels, setup;
    uint32_t loopStart, loopEnd;
    float peak;
    char* name;
    uint8_t* ogg;
    size_t oggSize;
    unsigned references;
    uint64_t lastUse;
} FsbSample;

bool Fsb_parse(const uint8_t* header, size_t size, uint32_t* dataOffset, FsbSample** samples);
bool Fsb_rebuild(FsbSample* sample, const uint8_t* packets, size_t size);
void Fsb_free(FsbSample* samples);

#endif
