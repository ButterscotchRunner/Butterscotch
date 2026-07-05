#ifndef _BS_RVALUE_H_
#define _BS_RVALUE_H_
#include <stdint.h>
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "real_type.h"
#include "stb_ds.h"
#include "utils.h"
#include "wad_versions.h"

// Forward declarations
struct GMLArray;
typedef struct GMLArray GMLArray;
void GMLArray_decRef(struct GMLArray* arr);
void GMLArray_incRef(struct GMLArray* arr);

struct Instance;
typedef struct Instance Instance;
void Instance_structIncRef(struct Instance* inst);
void Instance_structDecRef(struct Instance* inst);
uint32_t Instance_getInstanceId(struct Instance* inst);

#include "gml_method.h"

// ===[ GML Data Types (4-bit type codes) ]===
#define GML_TYPE_DOUBLE   0x0
#define GML_TYPE_FLOAT    0x1
#define GML_TYPE_INT32    0x2
#define GML_TYPE_INT64    0x3
#define GML_TYPE_BOOL     0x4
#define GML_TYPE_VARIABLE 0x5
#define GML_TYPE_STRING   0x6
#define GML_TYPE_INT16    0xF

// ===[ Asset Reference Types ]===
// See yyTypes.js for reference
typedef enum {
    ASSET_TYPE_OBJECT = 0,
    ASSET_TYPE_SPRITE = 1,
    ASSET_TYPE_SOUND = 2,
    ASSET_TYPE_ROOM = 3,
    ASSET_TYPE_PATH = 4,
    ASSET_TYPE_SCRIPT = 5,
    ASSET_TYPE_FONT = 6,
    ASSET_TYPE_TIMELINE = 7,
    ASSET_TYPE_SHADER = 8,
    ASSET_TYPE_SEQUENCE = 9,
    ASSET_TYPE_ANIMCURVE = 10,
    ASSET_TYPE_PARTICLESYSTEM = 11,
    ASSET_TYPE_TILEMAP = 12,
    ASSET_TYPE_TILESET = 13,
    ASSET_TYPE_INSTANCE = 14,
    ASSET_TYPE_PARTICLESYSTEMINSTANCE = 15
} AssetRefType;

// ===[ RValue - Tagged Union ]===
// When adding new elements to here, don't forget to update the "typeof" builtin!
typedef enum {
    RVALUE_UNDEFINED = 0,
    RVALUE_STRING = 1,
    RVALUE_INT32 = 2,
    RVALUE_INT64 = 3,
    RVALUE_BOOL = 4,
    RVALUE_REAL = 5,
    RVALUE_ARRAY = 6,
    RVALUE_METHOD = 7,
    RVALUE_STRUCT = 8,
    RVALUE_ASSETREF = 9,
} RValueType;

struct RValue {
    union {
        GMLReal real;
        int32_t int32;
#ifndef NO_RVALUE_INT64
        int64_t int64;
#endif
        const char* string;
        GMLArray* array;
#if IS_WAD17_OR_HIGHER_ENABLED
        GMLMethod* method;
#endif
        Instance* structInst;
    };
    // We use uint8_t for the type instead of RValueType because a enum value occupies 4 bytes, while uint8_t occupies 1 byte
    uint8_t type;
    // For RVALUE_STRING: true = the `string` buffer is owned and must be freed on RValue_free.
    // For RVALUE_ARRAY, RVALUE_METHOD, RVALUE_STRUCT: true = this RValue holds one strong ref and must decRef on RValue_free.
    // Non-owning ("weak") RValues are short-lived views returned by getters, caller must NOT free them.
    bool ownsReference;
    uint8_t gmlStackType; // GML data type from the instruction that pushed this value
    uint8_t assetRefType; // For RVALUE_ASSETREF: Indicates the asset type (AssetRefType)
} BS_ALIGN(8);

static inline RValue RValue_makeReal(GMLReal val) {
    RValue rv = {0};
    rv.type = RVALUE_REAL;
    rv.gmlStackType = GML_TYPE_DOUBLE;
    rv.real = val;
    return rv;
}

static inline RValue RValue_makeInt32(int32_t val) {
    RValue rv = {0};
    rv.type = RVALUE_INT32;
    rv.gmlStackType = GML_TYPE_INT32;
    rv.int32 = val;
    return rv;
}

static inline RValue RValue_makeInt64(int64_t val) {
    RValue rv = {0};
#ifdef NO_RVALUE_INT64
    // Values that don't fit in int32 get promoted to real instead of clamped, because clamping to INT32_MIN causes arithmetic overflow bugs
    // (example: Undertale's mercymod = -99999999999999 in the Asriel fight)
    if (val > INT32_MAX || INT32_MIN > val) {
        rv.type = RVALUE_REAL;
        rv.gmlStackType = GML_TYPE_DOUBLE;
        rv.real = val;
    } else {
        rv.type = RVALUE_INT32;
        rv.gmlStackType = GML_TYPE_INT32;
        rv.int32 = val;
    }
#else
    rv.type = RVALUE_INT64;
    rv.gmlStackType = GML_TYPE_INT64;
    rv.int64 = val;
#endif
    return rv;
}

static inline RValue RValue_makeBool(bool val) {
    RValue rv = {0};
    rv.type = RVALUE_BOOL;
    rv.gmlStackType = GML_TYPE_BOOL;
    rv.int32 = val ? 1 : 0;
    return rv;
}

static inline RValue RValue_makeString(const char* val) {
    RValue rv = {0};
    rv.type = RVALUE_STRING;
    rv.ownsReference = false;
    rv.gmlStackType = GML_TYPE_STRING;
    rv.string = val;
    return rv;
}

static inline RValue RValue_makeOwnedString(char* val) {
    RValue rv = {0};
    rv.type = RVALUE_STRING;
    rv.ownsReference = true;
    rv.gmlStackType = GML_TYPE_STRING;
    rv.string = val;
    return rv;
}

static inline RValue RValue_makeUndefined(void) {
    RValue rv = {0};
    rv.type = RVALUE_UNDEFINED;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    return rv;
}

// Takes ownership: refCount is NOT bumped (caller hands off its ref). The returned RValue decRefs on free.
// Use this when you have a freshly-allocated array (GMLArray_create) or after a GMLArray_incRef.
static inline RValue RValue_makeArray(GMLArray* arr) {
    RValue rv = {0};
    rv.type = RVALUE_ARRAY;
    rv.ownsReference = true;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    rv.array = arr;
    return rv;
}

// Weak view: does not own (no decRef on free). Callers that stash the value long-term must incRef + set ownsString.
static inline RValue RValue_makeArrayWeak(GMLArray* arr) {
    RValue rv = {0};
    rv.type = RVALUE_ARRAY;
    rv.ownsReference = false;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    rv.array = arr;
    return rv;
}

#if IS_WAD17_OR_HIGHER_ENABLED
// Takes ownership: refCount is NOT bumped (caller hands off its ref). The returned RValue decRefs on free.
static inline RValue RValue_makeMethod(GMLMethod* gmlMethod) {
    RValue rv = {0};
    rv.type = RVALUE_METHOD;
    rv.ownsReference = true;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    rv.method = gmlMethod;
    return rv;
}

// Takes ownership: refCount is NOT bumped (caller hands off its ref). The returned RValue decRefs on free.
static inline RValue RValue_makeMethodFromCodeIndexAndInstanceId(int32_t codeIndex, int32_t boundInstanceId) {
    return RValue_makeMethod(GMLMethod_create(codeIndex, boundInstanceId));
}

// Weak view: does not own (no decRef on free). Callers that stash the value long-term must incRef + set ownsString.
static inline RValue RValue_makeMethodWeak(GMLMethod* m) {
    RValue rv = {0};
    rv.type = RVALUE_METHOD;
    rv.ownsReference = false;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    rv.method = m;
    return rv;
}
#endif

// Weak view: does not own (no decRef on free). Callers that stash the value long-term must incRef + set ownsString.
static inline RValue RValue_makeStructWeak(Instance* inst) {
    RValue rv = {0};
    rv.type = RVALUE_STRUCT;
    rv.ownsReference = false;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    rv.structInst = inst;
    return rv;
}

// Takes ownership AND bumps the refCount. The returned RValue decRefs on free.
static inline RValue RValue_makeStructAndIncRef(Instance* inst) {
    RValue rv = {0};
    rv.type = RVALUE_STRUCT;
    rv.ownsReference = true;
    rv.gmlStackType = GML_TYPE_VARIABLE;
    rv.structInst = inst;
    Instance_structIncRef(inst);
    return rv;
}

static inline RValue RValue_makeAssetRef(int32_t assetIndex, uint8_t assetType) {
    RValue rv = {0};
    rv.type = RVALUE_ASSETREF;
    rv.assetRefType = assetType;
    rv.gmlStackType = GML_TYPE_INT32;
    rv.int32 = assetIndex;
    return rv;
}

// Makes "val" independent, that is...
// * For strings, it creates an owned copy of it (copying the original string underneath)
// * For arrays/methods/structs, it increments the reference count
// * For anything else, it does nothing because they don't need to be made independent
//
// Useful to store arbitrary RValues in containers (ds_map, ds_list, etc.)
// The caller's original RValue is unaffected and must still be freed normally.
static inline RValue RValue_makeIndependent(RValue val) {
    if (val.type == RVALUE_STRING && val.string != nullptr) {
        return RValue_makeOwnedString(safeStrdup(val.string));
    } else if (val.type == RVALUE_ARRAY && val.array != nullptr) {
        GMLArray_incRef(val.array);
        val.ownsReference = true;
        return val;
#if IS_WAD17_OR_HIGHER_ENABLED
    } else if (val.type == RVALUE_METHOD && val.method != nullptr) {
        GMLMethod_incRef(val.method);
        val.ownsReference = true;
        return val;
#endif
    } else if (val.type == RVALUE_STRUCT && val.structInst != nullptr) {
        Instance_structIncRef(val.structInst);
        val.ownsReference = true;
        return val;
    }
    requireMessageFormatted(__FILE__, __LINE__, !val.ownsReference, "Trying to make independent a RValue (type=%d) that owns a reference, but we don't handle it yet! Did you add a new refcounted value to Butterscotch without implementing RValue_makeIndependent for it?", val.type);
    return val;
}

// Steals the "val" ownership or, if it isn't owned, makes it independent.
static inline RValue RValue_stealOwnershipOrCopy(RValue val) {
    if (val.ownsReference)
        return val;
    return RValue_makeIndependent(val);
}

// Converts an RValue to a heap-allocated string representation.
// The caller must free the returned string
static inline char* RValue_toString(RValue val) {
    char buf[64];
    switch (val.type) {
        case RVALUE_REAL: {
            GMLReal r = val.real;
            if (isnan(r)) return safeStrdup("NaN");
            if (isinf(r)) return safeStrdup(r < (GMLReal) 0 ? "-inf" : "inf");
#ifdef USE_FLOAT_REALS
            const GMLReal INT_SAFE_BOUND = 9.2233715e18f; // largest float strictly < 2^63
#else
            const GMLReal INT_SAFE_BOUND = 9.2233720368547758e18;
#endif
            // Is this a integer?
            if (r >= -INT_SAFE_BOUND && r <= INT_SAFE_BOUND && r == (GMLReal) (int64_t) r) {
                snprintf(buf, sizeof(buf), "%lld", (long long) (int64_t) r);
            } else {
                // For anything else, we format to two decimal places
                snprintf(buf, sizeof(buf), "%.2f", (double) r);
            }
            return safeStrdup(buf);
        }
        case RVALUE_INT32:
            snprintf(buf, sizeof(buf), "%d", val.int32);
            return safeStrdup(buf);
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:
            snprintf(buf, sizeof(buf), "%lld", (long long) val.int64);
            return safeStrdup(buf);
#endif
        case RVALUE_STRING:
            return safeStrdup(val.string != nullptr ? val.string : "");
        case RVALUE_BOOL:
            return safeStrdup(val.int32 ? "1" : "0");
        case RVALUE_UNDEFINED:
            return safeStrdup("undefined");
        case RVALUE_ARRAY:
            snprintf(buf, sizeof(buf), "<array:%p>", (void*) val.array);
            return safeStrdup(buf);
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD:
            snprintf(buf, sizeof(buf), "<method:%d>", val.method->codeIndex);
            return safeStrdup(buf);
#endif
        case RVALUE_STRUCT:
            snprintf(buf, sizeof(buf), "<struct:%u>", val.structInst != nullptr ? Instance_getInstanceId(val.structInst) : 0);
            return safeStrdup(buf);
        case RVALUE_ASSETREF:
            snprintf(buf, sizeof(buf), "%d", val.int32);
            return safeStrdup(buf);
    }
    return safeStrdup("");
}

// Converts an RValue to a heap-allocated string representation, used for debug logs.
// The caller must free the returned string
static inline char* RValue_toStringFancy(RValue val) {
    switch (val.type) {
        case RVALUE_STRING: {
            char* valueAsString = RValue_toString(val);

            // length + quotes (2) + null terminator
            int newLength = strlen(valueAsString) + 3;
            char* valueWithQuotes = (char *)safeCalloc(newLength, sizeof(char));
            snprintf(valueWithQuotes, newLength, "\"%s\"", valueAsString);

            free(valueAsString);

            return valueWithQuotes;
        }
        default: {
            return RValue_toString(val);
        }
    }
}

// Converts an RValue to a heap-allocated string with a type tag prefix, used for trace-stack output.
// Examples: int32(42), real(3.14), "hello", bool(true), undefined, <array:0x...>
// The caller must free the returned string
static inline char* RValue_toStringTyped(RValue val) {
    char buf[128];
    switch (val.type) {
        case RVALUE_REAL:
            snprintf(buf, sizeof(buf), "real(%.16g)", (double) val.real);
            return safeStrdup(buf);
        case RVALUE_INT32:
            snprintf(buf, sizeof(buf), "int32(%d)", val.int32);
            return safeStrdup(buf);
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:
            snprintf(buf, sizeof(buf), "int64(%lld)", (long long) val.int64);
            return safeStrdup(buf);
#endif
        case RVALUE_STRING: {
            const char* str = val.string != nullptr ? val.string : "";
            size_t needed = strlen(str) + 3;
            char* result = (char *)safeCalloc(needed, sizeof(char));
            snprintf(result, needed, "\"%s\"", str);
            return result;
        }
        case RVALUE_BOOL:
            return safeStrdup(val.int32 ? "bool(true)" : "bool(false)");
        case RVALUE_UNDEFINED:
            return safeStrdup("undefined");
        case RVALUE_ARRAY:
            snprintf(buf, sizeof(buf), "<array:%p>", (void*) val.array);
            return safeStrdup(buf);
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD:
            snprintf(buf, sizeof(buf), "method(code=%d, inst=%d)", val.method->codeIndex, val.method->boundInstanceId);
            return safeStrdup(buf);
#endif
        case RVALUE_STRUCT:
            snprintf(buf, sizeof(buf), "struct(id=%u)", val.structInst != nullptr ? Instance_getInstanceId(val.structInst) : 0);
            return safeStrdup(buf);
        case RVALUE_ASSETREF:
            snprintf(buf, sizeof(buf), "assetref(type=%d, index=%d)", val.assetRefType, val.int32);
            return safeStrdup(buf);
    }
    return safeStrdup("???");
}

static inline void RValue_free(RValue* val) {
    if (val->type == RVALUE_STRING && val->ownsReference && val->string != nullptr) {
        free((void*) val->string);
        val->string = nullptr;
        val->ownsReference = false;
    } else if (val->type == RVALUE_ARRAY && val->ownsReference && val->array != nullptr) {
        GMLArray_decRef(val->array);
        val->array = nullptr;
        val->ownsReference = false;
#if IS_WAD17_OR_HIGHER_ENABLED
    } else if (val->type == RVALUE_METHOD && val->ownsReference && val->method != nullptr) {
        GMLMethod_decRef(val->method);
        val->method = nullptr;
        val->ownsReference = false;
#endif
    } else if (val->type == RVALUE_STRUCT && val->ownsReference && val->structInst != nullptr) {
        Instance_structDecRef(val->structInst);
        val->structInst = nullptr;
        val->ownsReference = false;
    }
}

static inline GMLReal RValue_toReal(RValue val) {
    switch (val.type) {
        case RVALUE_REAL:   return val.real;
        case RVALUE_INT32:  return (GMLReal) val.int32;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:  return (GMLReal) val.int64;
#endif
        case RVALUE_BOOL:   return (GMLReal) val.int32;
        case RVALUE_STRING: return GMLReal_strtod(val.string, nullptr);
        case RVALUE_ARRAY:  return 0.0;
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD: return 0.0;
#endif
        case RVALUE_STRUCT: return val.structInst != nullptr ? (GMLReal) Instance_getInstanceId(val.structInst) : 0.0;
        case RVALUE_ASSETREF: return (GMLReal) val.int32;
        default:            return 0.0;
    }
}

static inline int32_t RValue_toInt32(RValue val) {
    switch (val.type) {
        case RVALUE_REAL:   return (int32_t) val.real;
        case RVALUE_INT32:  return val.int32;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:  return (int32_t) val.int64;
#endif
        case RVALUE_BOOL:   return val.int32;
        case RVALUE_STRING: return (int32_t) GMLReal_strtod(val.string, nullptr);
        case RVALUE_ARRAY:  return 0;
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD: return 0;
#endif
        case RVALUE_STRUCT: return val.structInst != nullptr ? (int32_t) Instance_getInstanceId(val.structInst) : 0;
        case RVALUE_ASSETREF: return val.int32;
        default:            return 0;
    }
}

static inline int64_t RValue_toInt64(RValue val) {
    switch (val.type) {
        case RVALUE_REAL:   return (int64_t) val.real;
        case RVALUE_INT32:  return (int64_t) val.int32;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:  return val.int64;
#endif
        case RVALUE_BOOL:   return (int64_t) val.int32;
        case RVALUE_STRING: return (int64_t) GMLReal_strtod(val.string, nullptr);
        case RVALUE_ARRAY:  return 0;
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD: return 0;
#endif
        case RVALUE_STRUCT: return val.structInst != nullptr ? (int64_t) Instance_getInstanceId(val.structInst) : 0;
        case RVALUE_ASSETREF: return (int64_t) val.int32;
        default:            return 0;
    }
}

static inline bool RValue_toBool(RValue val) {
    switch (val.type) {
        case RVALUE_REAL:   return val.real > 0.5;
        case RVALUE_INT32:  return val.int32 > 0;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:  return val.int64 > 0;
#endif
        case RVALUE_BOOL:   return val.int32 != 0;
        case RVALUE_STRING: return val.string != nullptr && val.string[0] != '\0';
        case RVALUE_ARRAY:  return false;
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD: return true;
#endif
        case RVALUE_STRUCT: return val.structInst != nullptr;
        case RVALUE_ASSETREF: return val.int32 > 0;
        default:            return false;
    }
}

// Copies "val" into *slot by making the RValue independent. Caller retains "val".
static inline void RValue_copyIntoSlot(RValue* slot, RValue val) {
    RValue target = RValue_makeIndependent(val);
    // Free whatever was there.
    RValue_free(slot);
    *slot = target;
}

// Writes "val" into *slot. If the "val" is owning, we steal it, if it isn't, we copy it. Caller must NOT free "val".
static inline void RValue_writeIntoSlotStealingOwnershipOrCopying(RValue* slot, RValue val) {
    RValue target = RValue_stealOwnershipOrCopy(val);
    // Free whatever was there.
    RValue_free(slot);
    *slot = target;
}

#endif /* _BS_RVALUE_H_ */
