#include <stdint.h>
#include <stdlib.h>
#include "utils.h"
#include "random.h"

void Random_setSeed(Random* m, uint32_t seed) {
    m->state[0] = seed;
    for (int i = 1; 16 > i; ++i)
        m->state[i] = i + 0x6C078965 * (m->state[i - 1] ^ (m->state[i - 1] >> 30));
}

Random Random_create(uint32_t seed) {
    Random ret;
    ret.index = 0;
    Random_setSeed(&ret, seed);
    return ret;
}

uint32_t Random_nextUInt32(Random* m) {
    uint32_t a, b, c, d;

    a = m->state[m->index];
    c = m->state[(m->index + 13) & 15];
    b = a ^ c ^ (a << 16) ^ (c << 15);
    c = m->state[(m->index + 9) & 15];
    c ^= c >> 11;
    a = m->state[m->index] = b ^ c;
    d = a ^ ((a << 5) & 0xDA442D24);
    m->index = (m->index + 15) & 15;
    a = m->state[m->index];
    m->state[m->index] = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);

    return m->state[m->index];
}
