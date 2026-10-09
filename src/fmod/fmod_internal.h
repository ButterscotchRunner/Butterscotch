#ifndef BS_FMOD_INTERNAL_H
#define BS_FMOD_INTERNAL_H

#include "fmod.h"
#include "fsb.h"
#include "fmod_reader.h"
#include "stb_ds.h"
#include <string.h>

#define FM_TICKS 48000.0
#define FM_CACHE_LIMIT (64u * 1024u * 1024u)
#define FM_MAX_METADATA (16u * 1024u * 1024u)
#define FM_MAX_RECORDS 262144
#define FM_MAX_CLIPS 2048
#define FM_MAX_INSTANCES 4096
#define FM_MAX_EFFECT_PARAMETERS 256
#define FM_HANDLE_LIMIT 16777216

// offsets for bank format 142/140. references are 16-byte ids on disk;
// list headers need decoding before the entries can be read.
enum {
    FM_GUID_SIZE = 16,
    FM_EVENT_TIMELINE = 32,
    FM_EVENT_INPUT_BUS = 48,
    FM_EVENT_MASTER_BUS = 64,
    FM_EVENT_PARAMETERS = 93,
    FM_TIMELINE_LISTS = 16,
    FM_TIMED_ITEM_START = 16,
    FM_TIMED_ITEM_DURATION = 20,
    FM_TIMED_ITEM_SIZE = 24,
    FM_INSTRUMENT_BUS = 86,
    FM_INSTRUMENT_VOLUME = 16,
    FM_INSTRUMENT_PITCH = 20,
    FM_BUS_PARENT = 18,
    FM_WAVE_SAMPLE = 22,
    FM_PARAMETER_GLOBAL = 16,
    FM_PARAMETER_NAME_LENGTH = 24,
    FM_PARAMETER_NAME = 26,
    FM_CURVE_POINTS = 32,
    FM_PROPERTY_ID = 8,
    FM_PROPERTY_MAPPING = 6,
    FM_PROPERTY_CONTROLLERS = 24,
    FM_MODULATOR_GUID = 2,
    FM_MODULATOR_TYPE = 38,
    FM_MODULATOR_MODE = 42,
    FM_MODULATOR_AMOUNT = 46,
    FM_RANDOM_MODULATOR_SIZE = 50,
    FM_EFFECT_TYPE = 16,
    FM_EFFECT_BYPASS = 20,
    FM_EFFECT_SIZE = 21,
    FM_ENVELOPE_INITIAL = 46,
    FM_ENVELOPE_PEAK = 50,
    FM_ENVELOPE_SUSTAIN = 54,
    FM_ENVELOPE_ATTACK = 58,
    FM_ENVELOPE_HOLD = 62,
    FM_ENVELOPE_DECAY = 66,
    FM_ENVELOPE_RELEASE = 70,
    FM_ENVELOPE_ATTACK_CURVE = 74,
    FM_ENVELOPE_DECAY_CURVE = 78,
    FM_ENVELOPE_RELEASE_CURVE = 82,
    FM_ENVELOPE_FINAL = 86,
    FM_ENVELOPE_SIZE = 90,
    FM_TRANSITION_START = 32,
    FM_TRANSITION_END = 36,
    FM_TRANSITION_CONDITION_SIZE = 40,
    FM_TRANSITION_CONDITIONS = 44,
    FM_CONDITION_PARAMETER = 4,
    FM_CONDITION_LOW = 24,
    FM_CONDITION_HIGH = 28,
    FM_CONDITION_SIZE = 32,
};

enum {
    FM_EFFECT_FLOAT = 0,
    FM_EFFECT_INTEGER = 1,
    FM_EFFECT_BOOLEAN = 2,
    FM_EFFECT_DATA = 3,
};

enum {
    FM_CURVE_EXPONENTIAL = 0,
    FM_CURVE_SYMMETRIC = 1,
    FM_CURVE_SQUARED = 2,
    FM_CURVE_STEPPED = 3,
};

typedef struct {
    uint8_t b[FM_GUID_SIZE];
} Guid;

typedef struct {
    uint8_t b[FM_GUID_SIZE + 4];
} Key;
typedef struct FmBank FmBank;

// record data lives in the bank's metadata buffer. once indexed, the records
// stay put until teardown, so an instance can keep pointers to them.
typedef struct {
    char tag[4];
    const uint8_t* data;
    size_t size;
    int parent;
    FmBank* bank;
} Record;

typedef struct {
    Key key;
    Record* value;
} RecordMap;

typedef struct {
    char* key;
    Guid value;
} NameMap;

typedef struct {
    uint32_t kind;
    union {
        float real;
        int32_t integer;
        bool boolean;
        struct {
            const uint8_t* data;
            uint32_t size;
        } buffer;
    } value;
} EffectParameter;

typedef struct {
    Record* owner;
    uint32_t type;
    bool bypass;
    EffectParameter* parameters;
} EffectDefinition;

typedef struct {
    Guid id;
    char* name;
    bool global;
    float min, max, initial, value;
    float current, seekUp, seekDown;
} Parameter;

typedef struct {
    Guid curve, parameter;
} CurveInput;

typedef struct {
    RecordMap* records;
    NameMap* names;
    Parameter* parameters;
    CurveInput* inputs;
} FmodBankIndex;

struct FmBank {
    char* path;
    uint8_t* metadata;
    uint32_t dataOffset;
    FsbSample* samples;
    Record* records;
    EffectDefinition* effects;
};

typedef struct {
    int parameter;
    float value;
    float current;
} LocalValue;

typedef struct {
    Record* modulator;
    float value;
} ModulationValue;

typedef struct {
    FmBank* bank;
    int sample;
    Record* instrument;
    Record* controllers[16];
    unsigned controllerCount;
    ModulationValue* modulation;
    double start, end;
    double activated;
    double sourcePosition;
    int voice;
    bool started, finished, virtualized;
    float lastGain;
    float lastPitch;
} Clip;

typedef struct EventInstance EventInstance;

typedef struct {
    Record* event;
    Record* instrument;
    double start, end;
    EventInstance* playback;
    bool finished;
} NestedEvent;

typedef struct {
    Record* owner;
    double activated;
} BusClock;

struct EventInstance {
    int handle;
    Record* event;
    Record* timeline;
    Clip* clips;
    LocalValue* values;
    NestedEvent* children;
    BusClock* clocks;
    EventInstance* parent;
    Record* parentInstrument;
    double parentActivated;
    unsigned depth;
    bool playing, paused, released, oneshot, spatial, stopping, held;
    float x, y;
    double position, length;
    double elapsed, stopElapsed, releaseDuration;
};

struct FmodSystem {
    AudioSystem* audio;
    FileSystem* fs;
    FmBank** banks;
    FmodBankIndex index;
    EventInstance** instances;
    unsigned random;
    int nextHandle, maxChannels, activeVoices;
    int instanceCount;
    size_t cacheBytes;
    uint64_t useCounter;
};

static inline bool tagIs(const Record* record, const char* tag) {
    return record && memcmp(record->tag, tag, 4) == 0;
}

static inline bool equalGuid(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, FM_GUID_SIZE) == 0;
}

static inline Record* lookup(FmodSystem* system, const char* tag, const uint8_t* id) {
    Key key;
    if (!id) return nullptr;
    memcpy(key.b, id, FM_GUID_SIZE);
    memcpy(key.b + FM_GUID_SIZE, tag, 4);
    return hmget(system->index.records, key);
}

static inline Record* findSiblingRecord(Record* owner, const char* tag) {
    if (!owner) return nullptr;
    for (ptrdiff_t i = owner->parent + 1; i < arrlen(owner->bank->records); i++) {
        Record* record = &owner->bank->records[i];
        if (record->parent < owner->parent) break;
        if (record->parent == owner->parent && tagIs(record, tag)) return record;
    }
    return nullptr;
}

static inline int findParameterIndex(FmodBankIndex* index, const uint8_t* id) {
    for (ptrdiff_t i = 0; i < arrlen(index->parameters); i++) {
        if (equalGuid(index->parameters[i].id.b, id)) return (int) i;
    }
    return -1;
}

void FmodBank_destroy(FmBank* bank);
void FmodBank_clearIndex(FmodBankIndex* index);
void FmodBank_evict(FmodSystem* system, FsbSample* protectedSample);
bool FmodBank_prepareSample(FmodSystem* system, FmBank* bank, int index);

#endif
