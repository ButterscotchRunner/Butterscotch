#include "int_int_hashmap.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16

static void allocEmpty(IntIntHashMap* map, uint32_t capacity) {
    // memset 0xFF makes every key int32_t = -1 (the empty sentinel). The value bytes also become 0xFF but are unread for empty slots, so no harm.
    map->entries = (IntIntEntry *)safeMalloc(capacity * sizeof(IntIntEntry));
    memset(map->entries, 0xFF, capacity * sizeof(IntIntEntry));
    map->capacity = capacity;
    map->mask = capacity - 1;
    map->count = 0;
}

// Reinserts an existing (key, value) into a freshly-grown table. Skips the resize check and the count update because the caller has already accounted for both.
static void rawInsert(IntIntHashMap* map, int32_t key, uint32_t value) {
    uint32_t idx = ((uint32_t) key * 0x9E3779B9u) & map->mask;
    while (map->entries[idx].key != INT_INT_HASHMAP_EMPTY_KEY) {
        idx = (idx + 1) & map->mask;
    }
    map->entries[idx].key = key;
    map->entries[idx].value = value;
}

static void grow(IntIntHashMap* map) {
    uint32_t oldCapacity = map->capacity;
    IntIntEntry* oldEntries = map->entries;
    uint32_t newCapacity = oldCapacity == 0 ? INITIAL_CAPACITY : oldCapacity * 2;
    allocEmpty(map, newCapacity);
    if (oldEntries != nullptr) {
        repeat(oldCapacity, i) {
            int32_t k = oldEntries[i].key;
            if (k != INT_INT_HASHMAP_EMPTY_KEY) {
                rawInsert(map, k, oldEntries[i].value);
                map->count++;
            }
        }
        free(oldEntries);
    }
}

void IntIntHashMap_free(IntIntHashMap* map) {
    if (map->entries != nullptr) {
        free(map->entries);
        map->entries = nullptr;
    }
    map->capacity = 0;
    map->mask = 0;
    map->count = 0;
}

uint32_t IntIntHashMap_getOrInsertSequential(IntIntHashMap* map, int32_t key) {
    requireMessage(key != INT_INT_HASHMAP_EMPTY_KEY, "IntIntHashMap_getOrInsertSequential: key -1 collides with the empty-slot sentinel");

    // Resize before probing so we always find an empty slot. Threshold: load factor 0.75.
    if ((map->count + 1) * 4 > map->capacity * 3) {
        grow(map);
    }

    uint32_t idx = ((uint32_t) key * 0x9E3779B9u) & map->mask;
    while (true) {
        int32_t slotKey = map->entries[idx].key;
        if (slotKey == key) return map->entries[idx].value;
        if (slotKey == INT_INT_HASHMAP_EMPTY_KEY) {
            uint32_t newSlot = map->count;
            map->entries[idx].key = key;
            map->entries[idx].value = newSlot;
            map->count = newSlot + 1;
            return newSlot;
        }
        idx = (idx + 1) & map->mask;
    }
}
