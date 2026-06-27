#ifndef _BS_INT_INT_HASHMAP_H_
#define _BS_INT_INT_HASHMAP_H_

#include "common.h"
#include "utils.h"
#include <stdint.h>
#include <stdbool.h>

// Open-addressing, linear-probing hashmap from int32_t to RValue.
//
// While stb_ds HashMaps are good, we use our own handrolled HashMap because it is FASTER on the PlayStation 2 (MIPS) target.
// We also implement functions tailored for Butterscotch, to avoid multiple calls to do a single thing (example: hmgeti -> hmput -> hmgeti).
// We also try packing the data more tightly to avoid cache misses, which are VERY expensive on the PlayStation 2 target.

#define INT_INT_HASHMAP_EMPTY_KEY ((int32_t) -1)

typedef struct {
    int32_t key;
    uint32_t value;
} IntIntEntry;

// All fields are public so iterators can scan the entries array directly. Capacity is always 0 or a power of two; mask is capacity - 1.
typedef struct {
    IntIntEntry* entries;
    uint32_t capacity;
    uint32_t mask;
    uint32_t count;
} IntIntHashMap;

// Releases the entries buffer. Safe to call on a zero-initialized map.
void IntIntHashMap_free(IntIntHashMap* map);

// Inserts (key, count) and returns the new value. Caller must guarantee key is not already present (used internally by getOrInsertSequential after the probe loop has confirmed absence).
// Out-of-line because it can grow the table.
uint32_t IntIntHashMap_getOrInsertSequential(IntIntHashMap* map, int32_t key);

// Returns the number of occupied slots.
static inline uint32_t IntIntHashMap_count(const IntIntHashMap* map) {
    return map->count;
}

// Fast inline get. Returns true and writes *outValue when found. The probe loop accesses one cache line per step (8 entries fit in a 64-byte line).
static inline bool IntIntHashMap_tryGet(const IntIntHashMap* map, int32_t key, uint32_t* outValue) {
    if (map->capacity == 0) return false;
    const IntIntEntry* entries = map->entries;
    uint32_t mask = map->mask;
    uint32_t idx = ((uint32_t) key * 0x9E3779B9u) & mask;
    while (true) {
        int32_t slotKey = entries[idx].key;
        if (slotKey == key) {
            *outValue = entries[idx].value;
            return true;
        }
        if (slotKey == INT_INT_HASHMAP_EMPTY_KEY) return false;
        idx = (idx + 1) & mask;
    }
}

#endif /* _BS_INT_INT_HASHMAP_H_ */
