#ifndef BUTTERSCOTCH_PROCESSOR_API_H
#define BUTTERSCOTCH_PROCESSOR_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *input, *output, *debug;
    bool ps3;
    int atlas_size;
    char **patterns;
    size_t pattern_count;
} Options;

void process(const Options *options);

#endif
