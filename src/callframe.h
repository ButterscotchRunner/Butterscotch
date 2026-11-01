#ifndef _BS_CALLFRAME_H_
#define _BS_CALLFRAME_H_

typedef struct CallFrame {
    uint32_t codeIndex;
    Instance* initialSelf;
    Instance* initialOther;
    Instance* self;
    // "other" instance for collision events
    Instance* other;
    int32_t eventObjectIndex;
    int32_t eventType;
    int32_t eventSubtype;
    RValue* localVars;
    uint32_t localVarsCount;
    RValue* scriptArgs;
    int32_t scriptArgsCount;
    // BC17+: varID -> localVars slot lookup for the current code. Points into codeLocalsSlotMaps[currentCodeIndex] for BC17+, nullptr for BC16.
    IntIntHashMap* currentCodeLocalsSlotMap;
    struct CallFrame* parent;
} CallFrame;

static inline GameObject* CallFrame_getGameObject(DataWin* dataWin, CallFrame callFrame) {
    return &dataWin->objt.objects[callFrame.eventObjectIndex];
}

#endif