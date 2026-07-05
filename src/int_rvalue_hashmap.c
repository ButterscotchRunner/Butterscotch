#include "int_rvalue_hashmap.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 8

static void allocEmpty(IntRValueHashMap* map, uint32_t capacity) {
    // memset 0xFF makes every key int32_t = -1 (the empty sentinel). The value bytes also become 0xFF but are unread for empty slots, so no harm.
    map->entries = (IntRValueEntry *)safeMalloc(capacity * sizeof(IntRValueEntry));
    memset(map->entries, 0xFF, capacity * sizeof(IntRValueEntry));
    map->capacity = capacity;
    map->mask = capacity - 1;
    map->count = 0;
}

// Reinserts an existing entry into a freshly-grown table. Skips the resize check and the count update because the caller already accounts for both.
static void rawInsert(IntRValueHashMap* map, int32_t key, RValue value) {
    uint32_t idx = ((uint32_t) key * 0x9E3779B9u) & map->mask;
    while (map->entries[idx].key != INT_RVALUE_HASHMAP_EMPTY_KEY) {
        idx = (idx + 1) & map->mask;
    }
    map->entries[idx].key = key;
    map->entries[idx].value = value;
}

static void grow(IntRValueHashMap* map) {
    uint32_t oldCapacity = map->capacity;
    IntRValueEntry* oldEntries = map->entries;
    uint32_t newCapacity = oldCapacity == 0 ? INITIAL_CAPACITY : oldCapacity * 2;
    allocEmpty(map, newCapacity);
    if (oldEntries != nullptr) {
        repeat(oldCapacity, i) {
            int32_t k = oldEntries[i].key;
            if (k != INT_RVALUE_HASHMAP_EMPTY_KEY) {
                rawInsert(map, k, oldEntries[i].value);
                map->count++;
            }
        }
        free(oldEntries);
    }
}

void IntRValueHashMap_freeAllValues(IntRValueHashMap* map) {
    if (map->entries != nullptr) {
        repeat(map->capacity, i) {
            if (map->entries[i].key != INT_RVALUE_HASHMAP_EMPTY_KEY) {
                RValue_free(&map->entries[i].value);
            }
        }
        free(map->entries);
        map->entries = nullptr;
    }
    map->capacity = 0;
    map->mask = 0;
    map->count = 0;
}

RValue* IntRValueHashMap_findSlot(IntRValueHashMap* map, int32_t key) {
    if (map->capacity == 0) return nullptr;
    uint32_t idx = ((uint32_t) key * 0x9E3779B9u) & map->mask;
    while (true) {
        int32_t slotKey = map->entries[idx].key;
        if (slotKey == key) return &map->entries[idx].value;
        if (slotKey == INT_RVALUE_HASHMAP_EMPTY_KEY) return nullptr;
        idx = (idx + 1) & map->mask;
    }
}

RValue* IntRValueHashMap_getOrInsertUndefined(IntRValueHashMap* map, int32_t key) {
    requireMessage(key != INT_RVALUE_HASHMAP_EMPTY_KEY, "IntRValueHashMap_getOrInsertUndefined: key -1 collides with the empty-slot sentinel");

    // Resize before probing so we are guaranteed to find an empty slot. Threshold: load factor 0.75.
    if ((map->count + 1) * 4 > map->capacity * 3) {
        grow(map);
    }

    uint32_t idx = ((uint32_t) key * 0x9E3779B9u) & map->mask;
    while (true) {
        int32_t slotKey = map->entries[idx].key;
        if (slotKey == key) return &map->entries[idx].value;
        if (slotKey == INT_RVALUE_HASHMAP_EMPTY_KEY) {
            map->entries[idx].key = key;
            map->entries[idx].value = RValue_makeUndefined();
            map->count++;
            return &map->entries[idx].value;
        }
        idx = (idx + 1) & map->mask;
    }
}
