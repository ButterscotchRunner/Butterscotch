#ifndef _BS_EVENT_TABLE_H_
#define _BS_EVENT_TABLE_H_

#include "common.h"
#include "data_win.h"
#include <stdint.h>

// ===[ Event dispatch acceleration tables (Runner-owned) ]===
//
// Replaces the per-dispatch parent-chain walk in findEventCodeIdAndOwner with O(1) and O(small) lookups precomputed once at Runner startup.
//
// Two structures cooperate:
//   * EventSlotMap remaps the sparse (eventType, eventSubtype) pair space (DRAW
//     subtypes 0/64/72-77, COLLISION subtype = target objectIndex, etc.) into
//     a dense slot index. The mapping is per-DataWin so unused slots are not
//     reserved, keeping bitsets and per-slot indexes tight.
//   * ResolvedEventTable stores, per (object, slot) that has a handler after
//     parent-chain resolution, the CODE chunk handler id and the parent owner.
//     Indexed both by object (CSR by object, sorted by slot) and by slot
//     (CSR by slot, sorted by concrete objectIndex) so per-instance probes
//     and mass dispatch are both small linear scans over hot, packed memory.

#define MAX_EVENT_TABLE_OBJECT_COUNT 32767

// Dense remap from sparse (eventType, eventSubtype) pairs to compact slot indexes.
typedef struct {
    // denseLookup[eventType] is sized to (maxSubtypeByType[eventType] + 1) int16_t
    // entries; -1 marks unused subtypes. Fully self-contained per eventType, so
    // overall memory is the sum of (maxSubtype + 1) over event types actually used.
    int16_t* denseLookup[OBJT_EVENT_TYPE_COUNT];
    int32_t maxSubtypeByType[OBJT_EVENT_TYPE_COUNT]; // -1 if eventType is not used by any object
    int32_t slotCount;
} EventSlotMap;

// One entry per (object, slot) pair that has a handler after parent-chain resolution.
// Packed to 8 bytes so 8 entries fit in a 64-byte PS2 cache line.
typedef struct {
    uint16_t slot;
    int16_t ownerObjectId;
    int32_t codeId;
} ObjectEventEntry;

// Inverted index entry. Same data as ObjectEventEntry but viewed slot-major for mass dispatch.
// Packed to 8 bytes so 8 entries fit in a 64-byte PS2 cache line.
typedef struct {
    int16_t concreteObjectId;
    int16_t ownerObjectId;
    int32_t codeId;
} SlotResponderEntry;

typedef struct {
    // CSR per object: byObject[byObjectStart[obj] .. byObjectStart[obj + 1]) sorted by slot.
    ObjectEventEntry* byObject;
    uint32_t* byObjectStart; // length = objectCount + 1

    // CSR per slot: bySlot[bySlotStart[slot] .. bySlotStart[slot + 1]) sorted by concreteObjectId.
    SlotResponderEntry* bySlot;
    uint32_t* bySlotStart; // length = slotCount + 1

    int32_t objectCount;
    int32_t slotCount;
    uint32_t totalEntries;
} ResolvedEventTable;

void EventSlotMap_build(EventSlotMap* outMap, DataWin* dw);
void EventSlotMap_destroy(EventSlotMap* m);

void ResolvedEventTable_build(ResolvedEventTable* outTable, DataWin* dw, const EventSlotMap* slotMap);
void ResolvedEventTable_free(ResolvedEventTable* t);

// Returns the responder entries for a slot. *outCount is set to the number of entries (0 if no object listens for this slot).
static inline SlotResponderEntry* ResolvedEventTable_slotEntries(const ResolvedEventTable* t, int32_t slot, uint32_t* outCount) {
    uint32_t lo = t->bySlotStart[slot];
    uint32_t hi = t->bySlotStart[slot + 1];
    *outCount = hi - lo;
    return t->bySlot + lo;
}

// O(1) slot lookup. Returns -1 if (eventType, eventSubtype) has no listeners in this DataWin. Inlined into hot dispatch paths.
static inline int32_t EventSlotMap_lookup(const EventSlotMap* m, int32_t eventType, int32_t eventSubtype) {
    if ((uint32_t) eventType >= OBJT_EVENT_TYPE_COUNT) return -1;
    int32_t maxSub = m->maxSubtypeByType[eventType];
    if (0 > eventSubtype || eventSubtype > maxSub) return -1;
    return m->denseLookup[eventType][eventSubtype];
}

// Per-instance probe: returns the resolved CODE handler id for (obj, slot),
// or -1 if the object does not respond. *outOwner (if non-null) receives the
// parent-chain owner objectIndex (-1 when not found).
//
// Linear scan over the object's CSR range. With ~3-4 entries per object on
// real games, this fits in one D-cache line on PS2.
static inline int32_t ResolvedEventTable_lookup(const ResolvedEventTable* t, int32_t obj, int32_t slot, int32_t* outOwner) {
    if ((uint32_t) obj >= (uint32_t) t->objectCount || 0 > slot) {
        if (outOwner != nullptr) *outOwner = -1;
        return -1;
    }
    uint32_t lo = t->byObjectStart[obj];
    uint32_t hi = t->byObjectStart[obj + 1];
    uint16_t slotKey = (uint16_t) slot;
    for (uint32_t i = lo; hi > i; i++) {
        if (t->byObject[i].slot == slotKey) {
            if (outOwner != nullptr) *outOwner = t->byObject[i].ownerObjectId;
            return t->byObject[i].codeId;
        }
    }
    if (outOwner != nullptr) *outOwner = -1;
    return -1;
}

#endif /* _BS_EVENT_TABLE_H_ */
