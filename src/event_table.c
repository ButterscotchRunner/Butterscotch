#include "event_table.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===[ EventSlotMap ]===

void EventSlotMap_build(EventSlotMap* outMap, DataWin* dw) {
    requireNotNull(outMap);
    requireNotNull(dw);

    repeat(OBJT_EVENT_TYPE_COUNT, t) {
        outMap->denseLookup[t] = nullptr;
        outMap->maxSubtypeByType[t] = -1;
    }
    outMap->slotCount = 0;

    uint32_t objectCount = dw->objt.count;

    // Pass 1: find max subtype seen per event type (across DECLARED events on every object, not parent-resolved).
    // The remap deliberately ignores inheritance: a parent's events end up declared on the parent itself, so we already see them.
    repeat(objectCount, oi) {
        GameObject* go = &dw->objt.objects[oi];
        repeat(OBJT_EVENT_TYPE_COUNT, t) {
            ObjectEventList* el = &go->eventLists[t];
            repeat(el->eventCount, ei) {
                int32_t sub = (int32_t) el->events[ei].eventSubtype;
                if (sub > outMap->maxSubtypeByType[t]) outMap->maxSubtypeByType[t] = sub;
            }
        }
    }

    // Pass 2: allocate denseLookup arrays sized to (maxSubtype + 1) per type, fill with -1.
    repeat(OBJT_EVENT_TYPE_COUNT, t) {
        int32_t maxSub = outMap->maxSubtypeByType[t];
        if (0 > maxSub) continue;
        size_t entryCount = (size_t)(maxSub + 1);
        outMap->denseLookup[t] = (int16_t *)safeMalloc(entryCount * sizeof(int16_t));
        repeat(entryCount, i) outMap->denseLookup[t][i] = -1;
    }

    // Pass 3: assign dense slot indexes deterministically.
    // Iteration order is (eventType ascending, eventSubtype ascending), so slots are stable across runs of the same data.win.
    // The "if not yet assigned" check makes this idempotent for duplicate declarations.
    int32_t nextSlot = 0;
    repeat(OBJT_EVENT_TYPE_COUNT, t) {
        int32_t maxSub = outMap->maxSubtypeByType[t];
        if (0 > maxSub) continue;
        int16_t* table = outMap->denseLookup[t];
        repeat(objectCount, oi) {
            GameObject* go = &dw->objt.objects[oi];
            ObjectEventList* el = &go->eventLists[t];
            repeat(el->eventCount, ei) {
                int32_t sub = (int32_t) el->events[ei].eventSubtype;
                if (table[sub] >= 0) continue; // already assigned a slot
                table[sub] = (int16_t) nextSlot++;
            }
        }
    }
    outMap->slotCount = nextSlot;
}

void EventSlotMap_destroy(EventSlotMap* m) {
    if (m == nullptr) return;
    repeat(OBJT_EVENT_TYPE_COUNT, t) {
        free(m->denseLookup[t]);
        m->denseLookup[t] = nullptr;
        m->maxSubtypeByType[t] = -1;
    }
    m->slotCount = 0;
}

// ===[ ResolvedEventTable ]===

// Walk parent chain for one object and fill scratchCodeId / scratchOwner with the resolved handler for every slot the object responds to (-1 marks unused).
// Closer descendants override ancestors (we visit child first and skip already-set slots).
static void resolveHandlersForObject(DataWin* dw, const EventSlotMap* slotMap, int32_t startObj, int32_t* scratchCodeId, int16_t* scratchOwner) {
    int32_t slotCount = slotMap->slotCount;
    repeat(slotCount, s) scratchCodeId[s] = -1;

    int32_t cur = startObj;
    int32_t depth = 0;
    int32_t objectCount = (int32_t) dw->objt.count;
    while (cur >= 0 && objectCount > cur && 32 > depth) {
        GameObject* go = &dw->objt.objects[cur];
        repeat(OBJT_EVENT_TYPE_COUNT, t) {
            int16_t* table = slotMap->denseLookup[t];
            if (table == nullptr) continue;
            int32_t maxSub = slotMap->maxSubtypeByType[t];
            ObjectEventList* el = &go->eventLists[t];
            repeat(el->eventCount, ei) {
                int32_t sub = (int32_t) el->events[ei].eventSubtype;
                if (0 > sub || sub > maxSub) continue;
                int32_t slot = table[sub];
                if (0 > slot) continue;
                if (scratchCodeId[slot] >= 0) continue; // a closer descendant already won
                if (el->events[ei].actionCount > 0 && el->events[ei].actions[0].codeId >= 0) {
                    scratchCodeId[slot] = el->events[ei].actions[0].codeId;
                    scratchOwner[slot] = (int16_t) cur;
                }
            }
        }
        cur = go->parentId;
        depth++;
    }
}

void ResolvedEventTable_build(ResolvedEventTable* outTable, DataWin* dw, const EventSlotMap* slotMap) {
    requireNotNull(outTable);
    requireNotNull(dw);
    requireNotNull(slotMap);

    int32_t objectCount = (int32_t) dw->objt.count;
    int32_t slotCount = slotMap->slotCount;

    if (objectCount > MAX_EVENT_TABLE_OBJECT_COUNT) {
        fprintf(stderr, "ResolvedEventTable: objectCount=%d exceeds max %d!\n", objectCount, MAX_EVENT_TABLE_OBJECT_COUNT);
        abort();
    }

    outTable->objectCount = objectCount;
    outTable->slotCount = slotCount;
    outTable->byObject = nullptr;
    outTable->byObjectStart = nullptr;
    outTable->bySlot = nullptr;
    outTable->bySlotStart = nullptr;
    outTable->totalEntries = 0;

    // Pass 1: count resolved entries per object so we can allocate flat arrays.
    int32_t* scratchCodeId = (int32_t *)safeMalloc((size_t) slotCount * sizeof(int32_t));
    int16_t* scratchOwner = (int16_t *)safeMalloc((size_t) slotCount * sizeof(int16_t));

    outTable->byObjectStart = (uint32_t *)safeMalloc((size_t)(objectCount + 1) * sizeof(uint32_t));
    outTable->byObjectStart[0] = 0;

    uint32_t totalEntries = 0;
    repeat(objectCount, oi) {
        resolveHandlersForObject(dw, slotMap, (int32_t) oi, scratchCodeId, scratchOwner);
        uint32_t count = 0;
        repeat(slotCount, s) if (scratchCodeId[s] >= 0) count++;
        totalEntries += count;
        outTable->byObjectStart[oi + 1] = totalEntries;
    }
    outTable->totalEntries = totalEntries;

    // Pass 2: fill byObject. Walking slots in ascending order means each object's range is already sorted by slot (no extra qsort needed).
    outTable->byObject = (ObjectEventEntry *)safeMalloc((size_t) totalEntries * sizeof(ObjectEventEntry));

    uint32_t cursor = 0;
    repeat(objectCount, oi) {
        resolveHandlersForObject(dw, slotMap, (int32_t) oi, scratchCodeId, scratchOwner);
        repeat(slotCount, s) {
            if (0 > scratchCodeId[s]) continue;
            outTable->byObject[cursor].slot = (uint16_t) s;
            outTable->byObject[cursor].ownerObjectId = scratchOwner[s];
            outTable->byObject[cursor].codeId = scratchCodeId[s];
            cursor++;
        }
    }

    free(scratchCodeId);
    free(scratchOwner);

    // Pass 3: histogram + prefix sum + scatter to build bySlot. Walking byObject in object-major order means each slot's range ends up sorted by concreteObjectId for free.
    outTable->bySlotStart = (uint32_t *)safeMalloc((size_t)(slotCount + 1) * sizeof(uint32_t));
    repeat(slotCount + 1, i) outTable->bySlotStart[i] = 0;
    repeat(totalEntries, i) {
        outTable->bySlotStart[outTable->byObject[i].slot + 1]++;
    }
    repeat(slotCount, s) {
        outTable->bySlotStart[s + 1] += outTable->bySlotStart[s];
    }

    outTable->bySlot = (SlotResponderEntry *)safeMalloc((size_t) totalEntries * sizeof(SlotResponderEntry));
    uint32_t* slotCursor = (uint32_t *)safeMalloc((size_t) slotCount * sizeof(uint32_t));
    memset(slotCursor, 0, (size_t) slotCount * sizeof(uint32_t));

    repeat(objectCount, oi) {
        uint32_t lo = outTable->byObjectStart[oi];
        uint32_t hi = outTable->byObjectStart[oi + 1];
        for (uint32_t i = lo; hi > i; i++) {
            ObjectEventEntry* e = &outTable->byObject[i];
            uint32_t dst = outTable->bySlotStart[e->slot] + slotCursor[e->slot]++;
            outTable->bySlot[dst].concreteObjectId = (int16_t) oi;
            outTable->bySlot[dst].ownerObjectId = e->ownerObjectId;
            outTable->bySlot[dst].codeId = e->codeId;
        }
    }
    free(slotCursor);
}

void ResolvedEventTable_free(ResolvedEventTable* t) {
    if (t == nullptr) return;
    free(t->byObject); t->byObject = nullptr;
    free(t->byObjectStart); t->byObjectStart = nullptr;
    free(t->bySlot); t->bySlot = nullptr;
    free(t->bySlotStart); t->bySlotStart = nullptr;
    t->objectCount = 0;
    t->slotCount = 0;
    t->totalEntries = 0;
}
