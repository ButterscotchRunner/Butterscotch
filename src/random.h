#ifndef _BS_RANDOM_H_
#define _BS_RANDOM_H_

#include <stdint.h>

typedef struct {
    uint32_t state[16];
    uint32_t index;
} Random;

void Random_setSeed(Random* m, uint32_t seed);
Random Random_create(uint32_t seed);
uint32_t Random_nextUInt32(Random* m);

#define BS_RAND_MAX 0xFFFFFFFF

#endif /* _BS_RANDOM_H_ */
