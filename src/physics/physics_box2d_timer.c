/* Box2D profiling hooks use the runner's portable montonic clock */
#include "common.h"
#include "gettime.h"
#include "stdio_compat.h"
#include <box2d/base.h>
uint64_t b2GetTicks(void) { return nowNanos(); }
float b2GetMilliseconds(uint64_t start) { return (float)((int64_t)(nowNanos() - start) / 1000000.0); }
float b2GetMillisecondsAndReset(uint64_t* start) {
    uint64_t now = nowNanos(); float result = (float)((int64_t)(now - *start) / 1000000.0); *start = now; return result;
}
void b2Yield(void) { YIELD(); }
uint32_t b2Hash(uint32_t hash, const uint8_t* data, int count) {
    for (int i = 0; i < count; ++i) hash = (hash << 5) + hash + data[i];
    return hash;
}
