#include "vm_builtins.h"
#include "binary_utils.h"
#include "instance.h"
#include "json_reader.h"
#include "json_writer.h"
#include "real_type.h"
#include "runner.h"
#include "runner_gamepad.h"
#include "matrix_math.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "rvalue.h"
#include "stb_ds.h"
#include "text_utils.h"
#include "collision.h"
#include "ini.h"
#include "audio_system.h"
#include "file_system.h"
#include "md5.h"
#include "sha1.h"
#include "base64.h"
#include "gettime.h"

#define MAX_BACKGROUNDS 8

// ===[ STUBS MACROS ]===

#define STUB_RETURN_ZERO(name) \
    static RValue builtin_##name(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeReal(0.0); \
    }

#define STUB_RETURN_TRUE(name) \
    static RValue builtin_##name(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeBool(true); \
    }

#define STUB_RETURN_FALSE(name) \
    static RValue builtin_##name(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeBool(false); \
    }

#define STUB_RETURN_VALUE(name, value) \
    static RValue builtin_##name(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeReal(value); \
    }

#define STUB_RETURN_UNDEFINED(name) \
    static RValue builtin_##name(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeUndefined(); \
    }

// ===[ STUB LOGGING ]===

#ifdef ENABLE_VM_STUB_LOGS
static void logStubbedFunction(VMContext* ctx, const char* funcName) {
    const char* callerName = VM_getCallerName(ctx);
    char* dedupKey = VM_createDedupKey(callerName, funcName);

    if (ctx->alwaysLogStubbedFunctions || 0 > shgeti(ctx->loggedStubbedFuncs, dedupKey)) {
        // shput stores the key pointer, so don't free it when inserting
        shput(ctx->loggedStubbedFuncs, dedupKey, true);
        fprintf(stderr, "VM: [%s] Stubbed function \"%s\"!\n", callerName, funcName);
    } else {
        free(dedupKey);
    }
}

static void logSemiStubbedFunction(VMContext* ctx, const char* funcName) {
    const char* callerName = VM_getCallerName(ctx);
    char* dedupKey = VM_createDedupKey(callerName, funcName);

    if (ctx->alwaysLogStubbedFunctions || 0 > shgeti(ctx->loggedStubbedFuncs, dedupKey)) {
        // shput stores the key pointer, so don't free it when inserting
        shput(ctx->loggedStubbedFuncs, dedupKey, true);
        fprintf(stderr, "VM: [%s] Semi-Stubbed function \"%s\"!\n", callerName, funcName);
    } else {
        free(dedupKey);
    }
}
#else
#define logStubbedFunction(ctx, funcName) ((void) 0)
#define logSemiStubbedFunction(ctx, funcName) ((void) 0)
#endif

// Forward declarations
static int32_t resolveLayerIdArg(Runner* runner, RValue arg);

// ===[ DS_MAP SYSTEM ]===

static int32_t dsMapCreate(Runner* runner) {
    DsMapEntry* newMap = nullptr;
    int32_t id = (int32_t) arrlen(runner->dsMapPool);
    arrput(runner->dsMapPool, newMap);
    return id;
}

static DsMapEntry** dsMapGet(Runner* runner, int32_t id) {
    if (id < 0 || (int32_t) arrlen(runner->dsMapPool) <= id) return nullptr;
    return &runner->dsMapPool[id];
}

// ===[ DS_LIST SYSTEM ]===

static int32_t dsListCreate(Runner* runner) {
    // Reuse a freed slot if available, matching native GameMaker behavior.
    // Yes, some games (example: DELTARUNE Chapter 3's obj_board_playercamera_Other_10) rely on ds_list_create reusing the id of a list just destroyed.
    int32_t poolSize = (int32_t) arrlen(runner->dsListPool);
    repeat(poolSize, i) {
        if (runner->dsListPool[i].freed) {
            runner->dsListPool[i].freed = false;
            runner->dsListPool[i].items = nullptr;
            return i;
        }
    }
    DsList newList = {0};
    int32_t id = poolSize;
    arrput(runner->dsListPool, newList);
    return id;
}

static DsList* dsListGet(Runner* runner, int32_t id) {
    if (0 > id || id >= (int32_t) arrlen(runner->dsListPool)) return nullptr;
    if (runner->dsListPool[id].freed) return nullptr;
    return &runner->dsListPool[id];
}

// ===[ DS_QUEUE SYSTEM ]===

static int32_t dsQueueCreate(Runner* runner) {
    int32_t poolSize = (int32_t) arrlen(runner->dsQueuePool);
    repeat(poolSize, i) {
        if (runner->dsQueuePool[i].freed) {
            runner->dsQueuePool[i].freed = false;
            runner->dsQueuePool[i].items = nullptr;
            return i;
        }
    }
    DsQueue q = {0};
    int32_t id = poolSize;
    arrput(runner->dsQueuePool, q);
    return id;
}

static DsQueue* dsQueueGet(Runner* runner, int32_t id) {
    if (0 > id || id >= (int32_t) arrlen(runner->dsQueuePool)) return nullptr;
    if (runner->dsQueuePool[id].freed) return nullptr;
    return &runner->dsQueuePool[id];
}

// ===[ DS_STACK SYSTEM ]===

static int32_t dsStackCreate(Runner* runner) {
    int32_t poolSize = (int32_t) arrlen(runner->dsStackPool);
    repeat(poolSize, i) {
        if (runner->dsStackPool[i].freed) {
            runner->dsStackPool[i].freed = false;
            runner->dsStackPool[i].items = nullptr;
            return i;
        }
    }
    DsStack s = {0};
    int32_t id = poolSize;
    arrput(runner->dsStackPool, s);
    return id;
}

static DsStack* dsStackGet(Runner* runner, int32_t id) {
    if (0 > id || id >= (int32_t) arrlen(runner->dsStackPool)) return nullptr;
    if (runner->dsStackPool[id].freed) return nullptr;
    return &runner->dsStackPool[id];
}

// ===[ BUILT-IN VARIABLE GET/SET ]===

static bool isValidAlarmIndex(int alarmIndex) {
    return alarmIndex >= 0 && GML_ALARM_COUNT > alarmIndex;
}

// Sorted (strcmp-order, LC_ALL=C) table of built-in variable names -> enum IDs.
// We use bsearch instead of a HashMap because we don't have *that* many builtin var entries, so it is faster to use bsearch than a HashMap.
// IMPORTANT: Entries MUST stay sorted by name for bsearch to work!
typedef struct {
    const char* name;
    int16_t id;
} BuiltinVarEntry;

static const BuiltinVarEntry BUILTIN_VAR_TABLE[] = {
    { "alarm", BUILTIN_VAR_ALARM },
    { "application_surface", BUILTIN_VAR_APPLICATION_SURFACE },
    { "argument", BUILTIN_VAR_ARGUMENT },
    { "argument0", BUILTIN_VAR_ARGUMENT0 },
    { "argument1", BUILTIN_VAR_ARGUMENT1 },
    { "argument10", BUILTIN_VAR_ARGUMENT10 },
    { "argument11", BUILTIN_VAR_ARGUMENT11 },
    { "argument12", BUILTIN_VAR_ARGUMENT12 },
    { "argument13", BUILTIN_VAR_ARGUMENT13 },
    { "argument14", BUILTIN_VAR_ARGUMENT14 },
    { "argument15", BUILTIN_VAR_ARGUMENT15 },
    { "argument2", BUILTIN_VAR_ARGUMENT2 },
    { "argument3", BUILTIN_VAR_ARGUMENT3 },
    { "argument4", BUILTIN_VAR_ARGUMENT4 },
    { "argument5", BUILTIN_VAR_ARGUMENT5 },
    { "argument6", BUILTIN_VAR_ARGUMENT6 },
    { "argument7", BUILTIN_VAR_ARGUMENT7 },
    { "argument8", BUILTIN_VAR_ARGUMENT8 },
    { "argument9", BUILTIN_VAR_ARGUMENT9 },
    { "argument_count", BUILTIN_VAR_ARGUMENT_COUNT },
    { "async_load", BUILTIN_VAR_ASYNC_LOAD },
    { "background_alpha", BUILTIN_VAR_BACKGROUND_ALPHA },
    { "background_color", BUILTIN_VAR_BACKGROUND_COLOR },
    { "background_colour", BUILTIN_VAR_BACKGROUND_COLOUR },
    { "background_height", BUILTIN_VAR_BACKGROUND_HEIGHT },
    { "background_hspeed", BUILTIN_VAR_BACKGROUND_HSPEED },
    { "background_index", BUILTIN_VAR_BACKGROUND_INDEX },
    { "background_visible", BUILTIN_VAR_BACKGROUND_VISIBLE },
    { "background_vspeed", BUILTIN_VAR_BACKGROUND_VSPEED },
    { "background_width", BUILTIN_VAR_BACKGROUND_WIDTH },
    { "background_x", BUILTIN_VAR_BACKGROUND_X },
    { "background_xscale", BUILTIN_VAR_BACKGROUND_XSCALE },
    { "background_y", BUILTIN_VAR_BACKGROUND_Y },
    { "background_yscale", BUILTIN_VAR_BACKGROUND_YSCALE },
    { "bbox_bottom", BUILTIN_VAR_BBOX_BOTTOM },
    { "bbox_left", BUILTIN_VAR_BBOX_LEFT },
    { "bbox_right", BUILTIN_VAR_BBOX_RIGHT },
    { "bbox_top", BUILTIN_VAR_BBOX_TOP },
    { "buffer_bool", BUILTIN_VAR_BUFFER_BOOL },
    { "buffer_f16", BUILTIN_VAR_BUFFER_F16 },
    { "buffer_f32", BUILTIN_VAR_BUFFER_F32 },
    { "buffer_f64", BUILTIN_VAR_BUFFER_F64 },
    { "buffer_fast", BUILTIN_VAR_BUFFER_FAST },
    { "buffer_fixed", BUILTIN_VAR_BUFFER_FIXED },
    { "buffer_grow", BUILTIN_VAR_BUFFER_GROW },
    { "buffer_s16", BUILTIN_VAR_BUFFER_S16 },
    { "buffer_s32", BUILTIN_VAR_BUFFER_S32 },
    { "buffer_s8", BUILTIN_VAR_BUFFER_S8 },
    { "buffer_seek_end", BUILTIN_VAR_BUFFER_SEEK_END },
    { "buffer_seek_relative", BUILTIN_VAR_BUFFER_SEEK_RELATIVE },
    { "buffer_seek_start", BUILTIN_VAR_BUFFER_SEEK_START },
    { "buffer_string", BUILTIN_VAR_BUFFER_STRING },
    { "buffer_text", BUILTIN_VAR_BUFFER_TEXT },
    { "buffer_u16", BUILTIN_VAR_BUFFER_U16 },
    { "buffer_u32", BUILTIN_VAR_BUFFER_U32 },
    { "buffer_u64", BUILTIN_VAR_BUFFER_U64 },
    { "buffer_u8", BUILTIN_VAR_BUFFER_U8 },
    { "buffer_wrap", BUILTIN_VAR_BUFFER_WRAP },
    { "current_day", BUILTIN_VAR_CURRENT_DAY },
    { "current_hour", BUILTIN_VAR_CURRENT_HOUR },
    { "current_minute", BUILTIN_VAR_CURRENT_MINUTE },
    { "current_month", BUILTIN_VAR_CURRENT_MONTH },
    { "current_second", BUILTIN_VAR_CURRENT_SECOND },
    { "current_time", BUILTIN_VAR_CURRENT_TIME },
    { "current_weekday", BUILTIN_VAR_CURRENT_WEEKDAY },
    { "current_year", BUILTIN_VAR_CURRENT_YEAR },
    { "debug_mode", BUILTIN_VAR_DEBUG_MODE },
    { "delta_time", BUILTIN_VAR_DELTA_TIME },
    { "depth", BUILTIN_VAR_DEPTH },
    { "direction", BUILTIN_VAR_DIRECTION },
    { "false", BUILTIN_VAR_FALSE },
    { "fps", BUILTIN_VAR_FPS },
    { "friction", BUILTIN_VAR_FRICTION },
    { "gp_axislh", BUILTIN_VAR_GP_AXIS_LH },
    { "gp_axislv", BUILTIN_VAR_GP_AXIS_LV },
    { "gp_axisrh", BUILTIN_VAR_GP_AXIS_RH },
    { "gp_axisrv", BUILTIN_VAR_GP_AXIS_RV },
    { "gp_face1", BUILTIN_VAR_GP_FACE1 },
    { "gp_face2", BUILTIN_VAR_GP_FACE2 },
    { "gp_face3", BUILTIN_VAR_GP_FACE3 },
    { "gp_face4", BUILTIN_VAR_GP_FACE4 },
    { "gp_home", BUILTIN_VAR_GP_HOME },
    { "gp_padd", BUILTIN_VAR_GP_PADD },
    { "gp_padl", BUILTIN_VAR_GP_PADL },
    { "gp_padr", BUILTIN_VAR_GP_PADR },
    { "gp_padu", BUILTIN_VAR_GP_PADU },
    { "gp_select", BUILTIN_VAR_GP_SELECT },
    { "gp_shoulderl", BUILTIN_VAR_GP_SHOULDERL },
    { "gp_shoulderlb", BUILTIN_VAR_GP_SHOULDERLB },
    { "gp_shoulderr", BUILTIN_VAR_GP_SHOULDERR },
    { "gp_shoulderrb", BUILTIN_VAR_GP_SHOULDERRB },
    { "gp_start", BUILTIN_VAR_GP_START },
    { "gp_stickl", BUILTIN_VAR_GP_STICKL },
    { "gp_stickr", BUILTIN_VAR_GP_STICKR },
    { "gravity", BUILTIN_VAR_GRAVITY },
    { "gravity_direction", BUILTIN_VAR_GRAVITY_DIRECTION },
    { "health", BUILTIN_VAR_HEALTH },
    { "hspeed", BUILTIN_VAR_HSPEED },
    { "id", BUILTIN_VAR_ID },
    { "image_alpha", BUILTIN_VAR_IMAGE_ALPHA },
    { "image_angle", BUILTIN_VAR_IMAGE_ANGLE },
    { "image_blend", BUILTIN_VAR_IMAGE_BLEND },
    { "image_index", BUILTIN_VAR_IMAGE_INDEX },
    { "image_number", BUILTIN_VAR_IMAGE_NUMBER },
    { "image_single", BUILTIN_VAR_IMAGE_SINGLE },
    { "image_speed", BUILTIN_VAR_IMAGE_SPEED },
    { "image_xscale", BUILTIN_VAR_IMAGE_XSCALE },
    { "image_yscale", BUILTIN_VAR_IMAGE_YSCALE },
    { "instance_count", BUILTIN_VAR_INSTANCE_COUNT },
    { "instance_id", BUILTIN_VAR_INSTANCE_ID },
    { "keyboard_key", BUILTIN_VAR_KEYBOARD_KEY },
    { "keyboard_lastchar", BUILTIN_VAR_KEYBOARD_LASTCHAR },
    { "keyboard_lastkey", BUILTIN_VAR_KEYBOARD_LASTKEY },
    { "keyboard_string", BUILTIN_VAR_KEYBOARD_STRING },
    { "layer", BUILTIN_VAR_LAYER },
    { "lives", BUILTIN_VAR_LIVES },
    { "mask_index", BUILTIN_VAR_MASK_INDEX },
    { "mouse_button", BUILTIN_VAR_MOUSE_BUTTON },
    { "mouse_lastbutton", BUILTIN_VAR_MOUSE_LASTBUTTON },
    { "mouse_x", BUILTIN_VAR_MOUSE_X },
    { "mouse_y", BUILTIN_VAR_MOUSE_Y },
    { "object_index", BUILTIN_VAR_OBJECT_INDEX },
    { "os_3ds", BUILTIN_VAR_OS_3DS },
    { "os_amazon", BUILTIN_VAR_OS_AMAZON },
    { "os_android", BUILTIN_VAR_OS_ANDROID },
    { "os_bb10", BUILTIN_VAR_OS_BB10 },
    { "os_ios", BUILTIN_VAR_OS_IOS },
    { "os_linux", BUILTIN_VAR_OS_LINUX },
    { "os_llvm_android", BUILTIN_VAR_OS_LLVM_ANDROID },
    { "os_llvm_ios", BUILTIN_VAR_OS_LLVM_IOS },
    { "os_llvm_linux", BUILTIN_VAR_OS_LLVM_LINUX },
    { "os_llvm_macosx", BUILTIN_VAR_OS_LLVM_MACOSX },
    { "os_llvm_psp", BUILTIN_VAR_OS_LLVM_PSP },
    { "os_llvm_symbian", BUILTIN_VAR_OS_LLVM_SYMBIAN },
    { "os_llvm_win32", BUILTIN_VAR_OS_LLVM_WIN32 },
    { "os_llvm_winphone", BUILTIN_VAR_OS_LLVM_WINPHONE },
    { "os_macosx", BUILTIN_VAR_OS_MACOSX },
    { "os_ps3", BUILTIN_VAR_OS_PS3 },
    { "os_ps4", BUILTIN_VAR_OS_PS4 },
    { "os_psp", BUILTIN_VAR_OS_PSP },
    { "os_psvita", BUILTIN_VAR_OS_PSVITA },
    { "os_switch", BUILTIN_VAR_OS_SWITCH },
    { "os_symbian", BUILTIN_VAR_OS_SYMBIAN },
    { "os_tizen", BUILTIN_VAR_OS_TIZEN },
    { "os_type", BUILTIN_VAR_OS_TYPE },
    { "os_unknown", BUILTIN_VAR_OS_UNKNOWN },
    { "os_uwp", BUILTIN_VAR_OS_UWP },
    { "os_wiiu", BUILTIN_VAR_OS_WIIU },
    { "os_win32", BUILTIN_VAR_OS_WIN32 },
    { "os_win8native", BUILTIN_VAR_OS_WIN8NATIVE },
    { "os_windows", BUILTIN_VAR_OS_WINDOWS },
    { "os_winphone", BUILTIN_VAR_OS_WINPHONE },
    { "os_xbox360", BUILTIN_VAR_OS_XBOX360 },
    { "os_xboxone", BUILTIN_VAR_OS_XBOXONE },
    { "path_action_continue", BUILTIN_VAR_PATH_ACTION_CONTINUE },
    { "path_action_restart", BUILTIN_VAR_PATH_ACTION_RESTART },
    { "path_action_reverse", BUILTIN_VAR_PATH_ACTION_REVERSE },
    { "path_action_stop", BUILTIN_VAR_PATH_ACTION_STOP },
    { "path_endaction", BUILTIN_VAR_PATH_ENDACTION },
    { "path_index", BUILTIN_VAR_PATH_INDEX },
    { "path_orientation", BUILTIN_VAR_PATH_ORIENTATION },
    { "path_position", BUILTIN_VAR_PATH_POSITION },
    { "path_positionprevious", BUILTIN_VAR_PATH_POSITIONPREVIOUS },
    { "path_scale", BUILTIN_VAR_PATH_SCALE },
    { "path_speed", BUILTIN_VAR_PATH_SPEED },
    { "persistent", BUILTIN_VAR_PERSISTENT },
    { "pi", BUILTIN_VAR_PI },
    { "room", BUILTIN_VAR_ROOM },
    { "room_first", BUILTIN_VAR_ROOM_FIRST },
    { "room_height", BUILTIN_VAR_ROOM_HEIGHT },
    { "room_last", BUILTIN_VAR_ROOM_LAST },
    { "room_persistent", BUILTIN_VAR_ROOM_PERSISTENT },
    { "room_speed", BUILTIN_VAR_ROOM_SPEED },
    { "room_width", BUILTIN_VAR_ROOM_WIDTH },
    { "score", BUILTIN_VAR_SCORE },
    { "solid", BUILTIN_VAR_SOLID },
    { "speed", BUILTIN_VAR_SPEED },
    { "sprite_height", BUILTIN_VAR_SPRITE_HEIGHT },
    { "sprite_index", BUILTIN_VAR_SPRITE_INDEX },
    { "sprite_width", BUILTIN_VAR_SPRITE_WIDTH },
    { "sprite_xoffset", BUILTIN_VAR_SPRITE_XOFFSET },
    { "sprite_yoffset", BUILTIN_VAR_SPRITE_YOFFSET },
    { "timeline_index", BUILTIN_VAR_TIMELINE_INDEX },
    { "timeline_loop", BUILTIN_VAR_TIMELINE_LOOP },
    { "timeline_position", BUILTIN_VAR_TIMELINE_POSITION },
    { "timeline_running", BUILTIN_VAR_TIMELINE_RUNNING },
    { "timeline_speed", BUILTIN_VAR_TIMELINE_SPEED },
    { "true", BUILTIN_VAR_TRUE },
    { "undefined", BUILTIN_VAR_UNDEFINED },
    { "view_angle", BUILTIN_VAR_VIEW_ANGLE },
    { "view_camera", BUILTIN_VAR_CAMERA_VIEW },
    { "view_current", BUILTIN_VAR_VIEW_CURRENT },
    { "view_enabled", BUILTIN_VAR_VIEW_ENABLED },
    { "view_hborder", BUILTIN_VAR_VIEW_HBORDER },
    { "view_hport", BUILTIN_VAR_VIEW_HPORT },
    { "view_hspeed", BUILTIN_VAR_VIEW_HSPEED },
    { "view_hview", BUILTIN_VAR_VIEW_HVIEW },
    { "view_object", BUILTIN_VAR_VIEW_OBJECT },
    { "view_surface_id", BUILTIN_VAR_VIEW_SURFACE_ID },
    { "view_vborder", BUILTIN_VAR_VIEW_VBORDER },
    { "view_visible", BUILTIN_VAR_VIEW_VISIBLE },
    { "view_vspeed", BUILTIN_VAR_VIEW_VSPEED },
    { "view_wport", BUILTIN_VAR_VIEW_WPORT },
    { "view_wview", BUILTIN_VAR_VIEW_WVIEW },
    { "view_xport", BUILTIN_VAR_VIEW_XPORT },
    { "view_xview", BUILTIN_VAR_VIEW_XVIEW },
    { "view_yport", BUILTIN_VAR_VIEW_YPORT },
    { "view_yview", BUILTIN_VAR_VIEW_YVIEW },
    { "visible", BUILTIN_VAR_VISIBLE },
    { "vspeed", BUILTIN_VAR_VSPEED },
    { "working_directory", BUILTIN_VAR_WORKING_DIRECTORY },
    { "x", BUILTIN_VAR_X },
    { "xprevious", BUILTIN_VAR_XPREVIOUS },
    { "xstart", BUILTIN_VAR_XSTART },
    { "y", BUILTIN_VAR_Y },
    { "yprevious", BUILTIN_VAR_YPREVIOUS },
    { "ystart", BUILTIN_VAR_YSTART },
};

static int compareBuiltinVarEntry(const void* keyPtr, const void* entryPtr) {
    const char* key = (const char*) keyPtr;
    const BuiltinVarEntry* entry = (const BuiltinVarEntry*) entryPtr;
    return strcmp(key, entry->name);
}

// Resolves a built-in variable name to its enum ID
int16_t VMBuiltins_resolveBuiltinVarId(const char* name) {
    size_t count = sizeof(BUILTIN_VAR_TABLE) / sizeof(BUILTIN_VAR_TABLE[0]);
    BuiltinVarEntry* hit = (BuiltinVarEntry*) bsearch(name, BUILTIN_VAR_TABLE, count, sizeof(BuiltinVarEntry), compareBuiltinVarEntry);
    return hit == nullptr ? BUILTIN_VAR_UNKNOWN : hit->id;
}

void VMBuiltins_checkIfBuiltinVarTableIsSorted(void) {
    size_t count = sizeof(BUILTIN_VAR_TABLE) / sizeof(BUILTIN_VAR_TABLE[0]);
    for (size_t i = 1; count > i; i++) {
        int cmp = strcmp(BUILTIN_VAR_TABLE[i - 1].name, BUILTIN_VAR_TABLE[i].name);
        requireMessageFormatted(__FILE__, __LINE__, cmp < 0, "BUILTIN_VAR_TABLE not strictly sorted at index %zu: '%s' vs '%s' (cmp=%d). Re-sort (LC_ALL=C) or remove duplicates!", i, BUILTIN_VAR_TABLE[i - 1].name, BUILTIN_VAR_TABLE[i].name, cmp);
    }
}

#if defined(PLATFORM_PS3)
#include <sys/systime.h>
#endif
// Indicates when a variable should be routed via structGet/structSet instead of resolving it using the default path.
// See GameMaker-HTML5's "g_instance_names" table for reference (GameMaker-HTML5/scripts/yyVariable.js),
static bool isInstanceScopedBuiltinVar(int16_t builtinVarId) {
    switch (builtinVarId) {
        case BUILTIN_VAR_X:
        case BUILTIN_VAR_Y:
        case BUILTIN_VAR_XPREVIOUS:
        case BUILTIN_VAR_YPREVIOUS:
        case BUILTIN_VAR_XSTART:
        case BUILTIN_VAR_YSTART:
        case BUILTIN_VAR_HSPEED:
        case BUILTIN_VAR_VSPEED:
        case BUILTIN_VAR_DIRECTION:
        case BUILTIN_VAR_SPEED:
        case BUILTIN_VAR_FRICTION:
        case BUILTIN_VAR_GRAVITY:
        case BUILTIN_VAR_GRAVITY_DIRECTION:
        case BUILTIN_VAR_OBJECT_INDEX:
        case BUILTIN_VAR_ID:
        case BUILTIN_VAR_ALARM:
        case BUILTIN_VAR_SOLID:
        case BUILTIN_VAR_VISIBLE:
        case BUILTIN_VAR_PERSISTENT:
        case BUILTIN_VAR_DEPTH:
        case BUILTIN_VAR_BBOX_LEFT:
        case BUILTIN_VAR_BBOX_RIGHT:
        case BUILTIN_VAR_BBOX_TOP:
        case BUILTIN_VAR_BBOX_BOTTOM:
        case BUILTIN_VAR_SPRITE_INDEX:
        case BUILTIN_VAR_IMAGE_SINGLE:
        case BUILTIN_VAR_IMAGE_NUMBER:
        case BUILTIN_VAR_SPRITE_WIDTH:
        case BUILTIN_VAR_SPRITE_HEIGHT:
        case BUILTIN_VAR_SPRITE_XOFFSET:
        case BUILTIN_VAR_SPRITE_YOFFSET:
        case BUILTIN_VAR_IMAGE_XSCALE:
        case BUILTIN_VAR_IMAGE_YSCALE:
        case BUILTIN_VAR_IMAGE_ANGLE:
        case BUILTIN_VAR_IMAGE_ALPHA:
        case BUILTIN_VAR_IMAGE_BLEND:
        case BUILTIN_VAR_IMAGE_SPEED:
        case BUILTIN_VAR_IMAGE_INDEX:
        case BUILTIN_VAR_MASK_INDEX:
        case BUILTIN_VAR_PATH_INDEX:
        case BUILTIN_VAR_PATH_POSITION:
        case BUILTIN_VAR_PATH_POSITIONPREVIOUS:
        case BUILTIN_VAR_PATH_SPEED:
        case BUILTIN_VAR_PATH_SCALE:
        case BUILTIN_VAR_PATH_ORIENTATION:
        case BUILTIN_VAR_PATH_ENDACTION:
        case BUILTIN_VAR_TIMELINE_INDEX:
        case BUILTIN_VAR_TIMELINE_POSITION:
        case BUILTIN_VAR_TIMELINE_SPEED:
        case BUILTIN_VAR_TIMELINE_RUNNING:
        case BUILTIN_VAR_TIMELINE_LOOP:
        case BUILTIN_VAR_LAYER:
        // case BUILTIN_VAR_MANAGED:
        // case BUILTIN_VAR_IN_COLLISION_TREE:
        // case BUILTIN_VAR_EVENT_DATA:
        // case BUILTIN_VAR_IAP_DATA:
        // case BUILTIN_VAR_PHY_ROTATION:
        // case BUILTIN_VAR_PHY_POSITION_X:
        // case BUILTIN_VAR_PHY_POSITION_Y:
        // case BUILTIN_VAR_PHY_ANGULAR_VELOCITY:
        // case BUILTIN_VAR_PHY_LINEAR_VELOCITY_X:
        // case BUILTIN_VAR_PHY_LINEAR_VELOCITY_Y:
        // case BUILTIN_VAR_PHY_SPEED_X:
        // case BUILTIN_VAR_PHY_SPEED_Y:
        // case BUILTIN_VAR_PHY_SPEED:
        // case BUILTIN_VAR_PHY_ANGULAR_DAMPING:
        // case BUILTIN_VAR_PHY_LINEAR_DAMPING:
        // case BUILTIN_VAR_PHY_BULLET:
        // case BUILTIN_VAR_PHY_FIXED_ROTATION:
        // case BUILTIN_VAR_PHY_ACTIVE:
        // case BUILTIN_VAR_PHY_MASS:
        // case BUILTIN_VAR_PHY_INERTIA:
        // case BUILTIN_VAR_PHY_COM_X:
        // case BUILTIN_VAR_PHY_COM_Y:
        // case BUILTIN_VAR_PHY_DYNAMIC:
        // case BUILTIN_VAR_PHY_KINEMATIC:
        // case BUILTIN_VAR_PHY_SLEEPING:
        // case BUILTIN_VAR_PHY_POSITION_XPREVIOUS:
        // case BUILTIN_VAR_PHY_POSITION_YPREVIOUS:
        // case BUILTIN_VAR_PHY_COLLISION_POINTS:
        // case BUILTIN_VAR_PHY_COLLISION_X:
        // case BUILTIN_VAR_PHY_COLLISION_Y:
        // case BUILTIN_VAR_PHY_COL_NORMAL_X:
        // case BUILTIN_VAR_PHY_COL_NORMAL_Y:
        // case BUILTIN_VAR_IN_SEQUENCE:
        // case BUILTIN_VAR_SEQUENCE_INSTANCE:
        // case BUILTIN_VAR_DRAWN_BY_SEQUENCE:
        // case BUILTIN_VAR_DISPLAY_AA:
        // case BUILTIN_VAR_WEBGL_ENABLED:
            return true;
        default:
            return false;
    }
}

RValue VMBuiltins_getVariable(VMContext* ctx, Instance* inst, int16_t builtinVarId, const char* name, int32_t arrayIndex) {
    Runner* runner = ctx->runner;
    requireNotNull(runner);

    // Structs: instance builtins are ordinary members.
    if (inst != nullptr && inst->objectIndex == STRUCT_OBJECT_INDEX && isInstanceScopedBuiltinVar(builtinVarId)) {
        return VM_structGet(ctx, inst, name, arrayIndex);
    }

    // In the past Butterscotch used cascading ifs for this, which in my opinion looked nicer AND GCC was converting the ifs into a jump table, so it was all well...
    // ...until the code changed enough and the GCC heuristic thought "you know what? let's drop the jump table!"
    // So that's why this (and setVariable) are a jump table
    switch (builtinVarId) {
        // File system
        case BUILTIN_VAR_WORKING_DIRECTORY: {
            FileSystem* fs = runner->fileSystem;
            return RValue_makeOwnedString(fs->vtable->resolvePath(fs, ""));
        }

        // OS constants
        case BUILTIN_VAR_OS_TYPE:
            return RValue_makeReal(runner->osType);
        case BUILTIN_VAR_OS_UNKNOWN:
            return RValue_makeReal(OS_UNKNOWN);
        case BUILTIN_VAR_OS_WIN32:
            return RValue_makeReal(OS_WINDOWS);
        case BUILTIN_VAR_OS_WINDOWS:
            return RValue_makeReal(OS_WINDOWS);
        case BUILTIN_VAR_OS_MACOSX:
            return RValue_makeReal(OS_MACOSX);
        case BUILTIN_VAR_OS_PSP:
            return RValue_makeReal(OS_PSP);
        case BUILTIN_VAR_OS_IOS:
            return RValue_makeReal(OS_IOS);
        case BUILTIN_VAR_OS_ANDROID:
            return RValue_makeReal(OS_ANDROID);
        case BUILTIN_VAR_OS_SYMBIAN:
            return RValue_makeReal(OS_SYMBIAN);
        case BUILTIN_VAR_OS_LINUX:
            return RValue_makeReal(OS_LINUX);
        case BUILTIN_VAR_OS_WINPHONE:
            return RValue_makeReal(OS_WINPHONE);
        case BUILTIN_VAR_OS_TIZEN:
            return RValue_makeReal(OS_TIZEN);
        case BUILTIN_VAR_OS_WIN8NATIVE:
            return RValue_makeReal(OS_WIN8NATIVE);
        case BUILTIN_VAR_OS_WIIU:
            return RValue_makeReal(OS_WIIU);
        case BUILTIN_VAR_OS_3DS:
            return RValue_makeReal(OS_3DS);
        case BUILTIN_VAR_OS_PSVITA:
            return RValue_makeReal(OS_PSVITA);
        case BUILTIN_VAR_OS_BB10:
            return RValue_makeReal(OS_BB10);
        case BUILTIN_VAR_OS_PS4:
            return RValue_makeReal(OS_PS4);
        case BUILTIN_VAR_OS_XBOXONE:
            return RValue_makeReal(OS_XBOXONE);
        case BUILTIN_VAR_OS_PS3:
            return RValue_makeReal(OS_PS3);
        case BUILTIN_VAR_OS_XBOX360:
            return RValue_makeReal(OS_XBOX360);
        case BUILTIN_VAR_OS_UWP:
            return RValue_makeReal(OS_UWP);
        case BUILTIN_VAR_OS_AMAZON:
            return RValue_makeReal(OS_AMAZON);
        case BUILTIN_VAR_OS_SWITCH:
            return RValue_makeReal(OS_SWITCH);
        case BUILTIN_VAR_OS_LLVM_WIN32:
            return RValue_makeReal(OS_LLVM_WIN32);
        case BUILTIN_VAR_OS_LLVM_MACOSX:
            return RValue_makeReal(OS_LLVM_MACOSX);
        case BUILTIN_VAR_OS_LLVM_PSP:
            return RValue_makeReal(OS_LLVM_PSP);
        case BUILTIN_VAR_OS_LLVM_IOS:
            return RValue_makeReal(OS_LLVM_IOS);
        case BUILTIN_VAR_OS_LLVM_ANDROID:
            return RValue_makeReal(OS_LLVM_ANDROID);
        case BUILTIN_VAR_OS_LLVM_SYMBIAN:
            return RValue_makeReal(OS_LLVM_SYMBIAN);
        case BUILTIN_VAR_OS_LLVM_LINUX:
            return RValue_makeReal(OS_LLVM_LINUX);
        case BUILTIN_VAR_OS_LLVM_WINPHONE:
            return RValue_makeReal(OS_LLVM_WINPHONE);
        case BUILTIN_VAR_ASYNC_LOAD:
            return RValue_makeReal((GMLReal) runner->asyncLoadMapId);

        // Per-instance properties
        case BUILTIN_VAR_IMAGE_SPEED:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->imageSpeed);
        case BUILTIN_VAR_IMAGE_INDEX:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->imageIndex);
        case BUILTIN_VAR_IMAGE_XSCALE:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->imageXscale);
        case BUILTIN_VAR_IMAGE_YSCALE:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->imageYscale);
        case BUILTIN_VAR_IMAGE_ANGLE:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->imageAngle);
        case BUILTIN_VAR_IMAGE_ALPHA:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->imageAlpha);
        case BUILTIN_VAR_IMAGE_BLEND:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->imageBlend);
        case BUILTIN_VAR_IMAGE_NUMBER: {
            if (inst == nullptr) break;
            if (inst->spriteIndex >= 0) {
                Sprite* sprite = &ctx->runner->dataWin->sprt.sprites[inst->spriteIndex];
                return RValue_makeReal((GMLReal) sprite->textureCount);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_IMAGE_SINGLE: {
            if (inst == nullptr) break;
            if (inst->imageSpeed == 0.0) {
                return RValue_makeReal((GMLReal) inst->imageIndex);
            }
            return RValue_makeReal(-1.0);
        }
        case BUILTIN_VAR_SPRITE_INDEX:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->spriteIndex);
        case BUILTIN_VAR_SPRITE_WIDTH: {
            if (inst == nullptr) break;
            if (inst->spriteIndex >= 0 && runner->dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
                return RValue_makeReal((GMLReal) runner->dataWin->sprt.sprites[inst->spriteIndex].width * inst->imageXscale);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_SPRITE_HEIGHT: {
            if (inst == nullptr) break;
            if (inst->spriteIndex >= 0 && runner->dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
                return RValue_makeReal((GMLReal) runner->dataWin->sprt.sprites[inst->spriteIndex].height * inst->imageYscale);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_SPRITE_XOFFSET: {
            if (inst == nullptr) break;
            if (inst->spriteIndex >= 0 && runner->dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
                return RValue_makeReal((GMLReal) runner->dataWin->sprt.sprites[inst->spriteIndex].originX * inst->imageXscale);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_SPRITE_YOFFSET: {
            if (inst == nullptr) break;
            if (inst->spriteIndex >= 0 && runner->dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
                return RValue_makeReal((GMLReal) runner->dataWin->sprt.sprites[inst->spriteIndex].originY * inst->imageYscale);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_BBOX_LEFT: {
            if (inst == nullptr) break;
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (!bbox.valid) return RValue_makeReal(inst->x);
            return RValue_makeReal(bbox.left);
        }
        case BUILTIN_VAR_BBOX_RIGHT: {
            if (inst == nullptr) break;
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (!bbox.valid) return RValue_makeReal(inst->x);
            // In compatibility mode the bbox is inclusive while our bbox is exclusive
            return RValue_makeReal(runner->collisionCompatibilityMode ? bbox.right - 1 : bbox.right);
        }
        case BUILTIN_VAR_BBOX_TOP: {
            if (inst == nullptr) break;
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (!bbox.valid) return RValue_makeReal(inst->y);
            return RValue_makeReal(bbox.top);
        }
        case BUILTIN_VAR_BBOX_BOTTOM: {
            if (inst == nullptr) break;
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (!bbox.valid) return RValue_makeReal(inst->y);
            // In compatibility mode the bbox is inclusive while our bbox is exclusive
            return RValue_makeReal(runner->collisionCompatibilityMode ? bbox.bottom - 1 : bbox.bottom);
        }
        case BUILTIN_VAR_VISIBLE:
            if (inst == nullptr) break;
            return RValue_makeBool(inst->visible);
        case BUILTIN_VAR_DEPTH:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->depth);
        case BUILTIN_VAR_LAYER:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->layer);
        case BUILTIN_VAR_X:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->x);
        case BUILTIN_VAR_Y:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->y);
        case BUILTIN_VAR_XPREVIOUS:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->xprevious);
        case BUILTIN_VAR_YPREVIOUS:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->yprevious);
        case BUILTIN_VAR_XSTART:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->xstart);
        case BUILTIN_VAR_YSTART:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->ystart);
        case BUILTIN_VAR_MASK_INDEX:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->maskIndex);
        case BUILTIN_VAR_ID:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->instanceId);
        case BUILTIN_VAR_OBJECT_INDEX:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->objectIndex);
        case BUILTIN_VAR_PERSISTENT:
            if (inst == nullptr) break;
            return RValue_makeBool(inst->persistent);
        case BUILTIN_VAR_SOLID:
            if (inst == nullptr) break;
            return RValue_makeBool(inst->solid);
        case BUILTIN_VAR_SPEED:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->speed);
        case BUILTIN_VAR_DIRECTION:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->direction);
        case BUILTIN_VAR_HSPEED:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->hspeed);
        case BUILTIN_VAR_VSPEED:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->vspeed);
        case BUILTIN_VAR_FRICTION:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->friction);
        case BUILTIN_VAR_GRAVITY:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->gravity);
        case BUILTIN_VAR_GRAVITY_DIRECTION:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->gravityDirection);
        case BUILTIN_VAR_ALARM: {
            if (inst == nullptr) break;
            if (isValidAlarmIndex(arrayIndex)) return RValue_makeReal((GMLReal) inst->alarm[arrayIndex]);
            return RValue_makeReal(-1.0);
        }

        // Path instance variables
        case BUILTIN_VAR_PATH_INDEX:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->pathIndex);
        case BUILTIN_VAR_PATH_POSITION:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->pathPosition);
        case BUILTIN_VAR_PATH_POSITIONPREVIOUS:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->pathPositionPrevious);
        case BUILTIN_VAR_PATH_SPEED:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->pathSpeed);
        case BUILTIN_VAR_PATH_SCALE:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->pathScale);
        case BUILTIN_VAR_PATH_ORIENTATION:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->pathOrientation);
        case BUILTIN_VAR_PATH_ENDACTION:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->pathEndAction);

        // Timeline instance variables
        case BUILTIN_VAR_TIMELINE_INDEX:
            if (inst == nullptr) break;
            return RValue_makeReal((GMLReal) inst->timelineIndex);
        case BUILTIN_VAR_TIMELINE_POSITION:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->timelinePosition);
        case BUILTIN_VAR_TIMELINE_SPEED:
            if (inst == nullptr) break;
            return RValue_makeReal(inst->timelineSpeed);
        case BUILTIN_VAR_TIMELINE_RUNNING:
            if (inst == nullptr) break;
            return RValue_makeBool(inst->timelineRunning);
        case BUILTIN_VAR_TIMELINE_LOOP:
            if (inst == nullptr) break;
            return RValue_makeBool(inst->timelineLoop);

        // Room properties
        case BUILTIN_VAR_ROOM:
            return RValue_makeReal((GMLReal) runner->currentRoomIndex);
        case BUILTIN_VAR_ROOM_FIRST:
            return RValue_makeReal((GMLReal) runner->dataWin->gen8.roomOrder[0]);
        case BUILTIN_VAR_ROOM_LAST:
            return RValue_makeReal((GMLReal) runner->dataWin->gen8.roomOrder[runner->dataWin->gen8.roomOrderCount - 1]);
        case BUILTIN_VAR_ROOM_SPEED:
            return RValue_makeReal((GMLReal) runner->currentRoom->speed);
        case BUILTIN_VAR_ROOM_WIDTH:
            return RValue_makeReal((GMLReal) runner->currentRoom->width);
        case BUILTIN_VAR_ROOM_HEIGHT:
            return RValue_makeReal((GMLReal) runner->currentRoom->height);
        case BUILTIN_VAR_ROOM_PERSISTENT:
            return RValue_makeBool(runner->currentRoom->persistent);

        // View properties
        case BUILTIN_VAR_VIEW_CURRENT:
            return RValue_makeReal((GMLReal) runner->viewCurrent);
        case BUILTIN_VAR_VIEW_ENABLED:
            return RValue_makeBool(runner->viewsEnabled);
        case BUILTIN_VAR_CAMERA_VIEW:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeReal((GMLReal) runner->views[arrayIndex].cameraId);
            return RValue_makeReal(-1.0);
        case BUILTIN_VAR_VIEW_XVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->viewX);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_YVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->viewY);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_WVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->viewWidth);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_HVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->viewHeight);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_XPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeReal((GMLReal) runner->views[arrayIndex].portX);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_VIEW_YPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeReal((GMLReal) runner->views[arrayIndex].portY);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_VIEW_WPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeReal((GMLReal) runner->views[arrayIndex].portWidth);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_VIEW_HPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeReal((GMLReal) runner->views[arrayIndex].portHeight);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_VIEW_VISIBLE:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeBool(runner->views[arrayIndex].enabled);
            return RValue_makeBool(false);
        case BUILTIN_VAR_VIEW_ANGLE: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->viewAngle);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_HBORDER: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->borderX);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_VBORDER: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->borderY);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_OBJECT: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->objectId);
            return RValue_makeReal(INSTANCE_NOONE);
        }
        case BUILTIN_VAR_VIEW_HSPEED: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->speedX);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_VSPEED: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) return RValue_makeReal((GMLReal) camera->speedY);
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_VIEW_SURFACE_ID:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) return RValue_makeReal((GMLReal) runner->views[arrayIndex].surfaceId);
            return RValue_makeReal(-1.0);

        // Background properties
        case BUILTIN_VAR_BACKGROUND_VISIBLE:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeBool(runner->backgrounds[arrayIndex].visible);
            return RValue_makeBool(false);
        case BUILTIN_VAR_BACKGROUND_INDEX:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].backgroundIndex);
            return RValue_makeReal(-1.0);
        case BUILTIN_VAR_BACKGROUND_X:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].x);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_BACKGROUND_Y:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].y);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_BACKGROUND_XSCALE:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].xScale);
            return RValue_makeReal(1.0);
        case BUILTIN_VAR_BACKGROUND_YSCALE:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].yScale);
            return RValue_makeReal(1.0);
        case BUILTIN_VAR_BACKGROUND_HSPEED:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].speedX);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_BACKGROUND_VSPEED:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].speedY);
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_BACKGROUND_WIDTH: {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) {
                int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, runner->backgrounds[arrayIndex].backgroundIndex);
                if (tpagIndex >= 0) return RValue_makeReal((GMLReal) runner->dataWin->tpag.items[tpagIndex].boundingWidth);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_BACKGROUND_HEIGHT: {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) {
                int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, runner->backgrounds[arrayIndex].backgroundIndex);
                if (tpagIndex >= 0) return RValue_makeReal((GMLReal) runner->dataWin->tpag.items[tpagIndex].boundingHeight);
            }
            return RValue_makeReal(0.0);
        }
        case BUILTIN_VAR_BACKGROUND_ALPHA:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((GMLReal) runner->backgrounds[arrayIndex].alpha);
            return RValue_makeReal(1.0);
        case BUILTIN_VAR_BACKGROUND_COLOR:
        case BUILTIN_VAR_BACKGROUND_COLOUR:
            return RValue_makeReal((GMLReal) runner->backgroundColor);

        // Timing
        case BUILTIN_VAR_CURRENT_DAY:
        case BUILTIN_VAR_CURRENT_HOUR:
        case BUILTIN_VAR_CURRENT_MINUTE:
        case BUILTIN_VAR_CURRENT_MONTH:
        case BUILTIN_VAR_CURRENT_SECOND:
        case BUILTIN_VAR_CURRENT_WEEKDAY:
        case BUILTIN_VAR_CURRENT_YEAR: {
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            switch (builtinVarId) {
                case BUILTIN_VAR_CURRENT_DAY:     return RValue_makeReal(t->tm_mday);
                case BUILTIN_VAR_CURRENT_HOUR:    return RValue_makeReal(t->tm_hour);
                case BUILTIN_VAR_CURRENT_MINUTE:  return RValue_makeReal(t->tm_min);
                case BUILTIN_VAR_CURRENT_MONTH:   return RValue_makeReal(t->tm_mon + 1);
                case BUILTIN_VAR_CURRENT_SECOND:  return RValue_makeReal(t->tm_sec);
                case BUILTIN_VAR_CURRENT_WEEKDAY: return RValue_makeReal(t->tm_wday);
                case BUILTIN_VAR_CURRENT_YEAR:    return RValue_makeReal(t->tm_year + 1900);
                default: abort(); // Should never happen
            }
        }
        case BUILTIN_VAR_CURRENT_TIME:
            return RValue_makeReal((nowNanos() - runner->gameStartTime) / 1000000.0);

        // Arguments
        case BUILTIN_VAR_ARGUMENT_COUNT:
            return RValue_makeReal((GMLReal) ctx->scriptArgCount);
        case BUILTIN_VAR_ARGUMENT: {
            if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > arrayIndex && arrayIndex >= 0) {
                RValue val = ctx->scriptArgs[arrayIndex];
                val.ownsReference = false;
                return val;
            }
            return RValue_makeUndefined();
        }
        case BUILTIN_VAR_ARGUMENT0 ... BUILTIN_VAR_ARGUMENT15: {
            int argNumber = builtinVarId - BUILTIN_VAR_ARGUMENT0;
            if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > argNumber) {
                RValue val = ctx->scriptArgs[argNumber];
                val.ownsReference = false;
                return val;
            }
            return RValue_makeUndefined();
        }

        // Keyboard
        case BUILTIN_VAR_KEYBOARD_KEY:
            return RValue_makeReal((GMLReal) runner->keyboard->lastKey);
        case BUILTIN_VAR_KEYBOARD_LASTCHAR:
            return RValue_makeString(runner->keyboard->lastChar);
        case BUILTIN_VAR_KEYBOARD_LASTKEY:
            return RValue_makeReal((GMLReal) runner->keyboard->lastKey);
        case BUILTIN_VAR_KEYBOARD_STRING:
            return RValue_makeString(runner->keyboard->string);

        case BUILTIN_VAR_MOUSE_BUTTON:
            return RValue_makeReal((GMLReal) RunnerMouse_getButton(runner->mouse));
        case BUILTIN_VAR_MOUSE_LASTBUTTON:
            return RValue_makeReal((GMLReal) RunnerMouse_getLastButton(runner->mouse));

        case BUILTIN_VAR_MOUSE_X: {
            GMLReal mouseRoomX, mouseRoomY;
            Runner_getMouseRoomPosition(runner, &mouseRoomX, &mouseRoomY);
            return RValue_makeReal(mouseRoomX);
        }
        case BUILTIN_VAR_MOUSE_Y: {
            GMLReal mouseRoomX, mouseRoomY;
            Runner_getMouseRoomPosition(runner, &mouseRoomX, &mouseRoomY);
            return RValue_makeReal(mouseRoomY);
        }

        // Surfaces
        case BUILTIN_VAR_APPLICATION_SURFACE:
            // Real surface ID on GL/GL-legacy (allocated lazily by Runner_beginFrame's ensureApplicationSurface call);
            // APPLICATION_SURFACE_ID (-1) on PS2 where the screen FB lives outside the chunk pool.
            return RValue_makeReal((GMLReal) runner->applicationSurfaceId);

        // Constants that GMS defines
        case BUILTIN_VAR_TRUE:
            return RValue_makeBool(true);
        case BUILTIN_VAR_FALSE:
            return RValue_makeBool(false);
        case BUILTIN_VAR_PI:
            return RValue_makeReal(3.14159265358979323846);
        case BUILTIN_VAR_UNDEFINED:
            return RValue_makeUndefined();

        // Path action constants
        case BUILTIN_VAR_PATH_ACTION_STOP:
            return RValue_makeReal(0.0);
        case BUILTIN_VAR_PATH_ACTION_RESTART:
            return RValue_makeReal(1.0);
        case BUILTIN_VAR_PATH_ACTION_CONTINUE:
            return RValue_makeReal(2.0);
        case BUILTIN_VAR_PATH_ACTION_REVERSE:
            return RValue_makeReal(3.0);

        // Buffer type constants
        case BUILTIN_VAR_BUFFER_FIXED:
            return RValue_makeReal(GML_BUFFER_FIXED);
        case BUILTIN_VAR_BUFFER_GROW:
            return RValue_makeReal(GML_BUFFER_GROW);
        case BUILTIN_VAR_BUFFER_WRAP:
            return RValue_makeReal(GML_BUFFER_WRAP);
        case BUILTIN_VAR_BUFFER_FAST:
            return RValue_makeReal(GML_BUFFER_FAST);

        // Buffer data type constants
        case BUILTIN_VAR_BUFFER_U8:
            return RValue_makeReal(GML_BUFTYPE_U8);
        case BUILTIN_VAR_BUFFER_S8:
            return RValue_makeReal(GML_BUFTYPE_S8);
        case BUILTIN_VAR_BUFFER_U16:
            return RValue_makeReal(GML_BUFTYPE_U16);
        case BUILTIN_VAR_BUFFER_S16:
            return RValue_makeReal(GML_BUFTYPE_S16);
        case BUILTIN_VAR_BUFFER_U32:
            return RValue_makeReal(GML_BUFTYPE_U32);
        case BUILTIN_VAR_BUFFER_S32:
            return RValue_makeReal(GML_BUFTYPE_S32);
        case BUILTIN_VAR_BUFFER_F16:
            return RValue_makeReal(GML_BUFTYPE_F16);
        case BUILTIN_VAR_BUFFER_F32:
            return RValue_makeReal(GML_BUFTYPE_F32);
        case BUILTIN_VAR_BUFFER_F64:
            return RValue_makeReal(GML_BUFTYPE_F64);
        case BUILTIN_VAR_BUFFER_BOOL:
            return RValue_makeReal(GML_BUFTYPE_BOOL);
        case BUILTIN_VAR_BUFFER_STRING:
            return RValue_makeReal(GML_BUFTYPE_STRING);
        case BUILTIN_VAR_BUFFER_U64:
            return RValue_makeReal(GML_BUFTYPE_U64);
        case BUILTIN_VAR_BUFFER_TEXT:
            return RValue_makeReal(GML_BUFTYPE_TEXT);

        // Buffer seek mode constants
        case BUILTIN_VAR_BUFFER_SEEK_START:
            return RValue_makeReal(GML_BUFFER_SEEK_START);
        case BUILTIN_VAR_BUFFER_SEEK_RELATIVE:
            return RValue_makeReal(GML_BUFFER_SEEK_RELATIVE);
        case BUILTIN_VAR_BUFFER_SEEK_END:
            return RValue_makeReal(GML_BUFFER_SEEK_END);

        // Gamepad constants
        case BUILTIN_VAR_GP_FACE1:
            return RValue_makeReal(GP_FACE1);
        case BUILTIN_VAR_GP_FACE2:
            return RValue_makeReal(GP_FACE2);
        case BUILTIN_VAR_GP_FACE3:
            return RValue_makeReal(GP_FACE3);
        case BUILTIN_VAR_GP_FACE4:
            return RValue_makeReal(GP_FACE4);
        case BUILTIN_VAR_GP_SHOULDERL:
            return RValue_makeReal(GP_SHOULDERL);
        case BUILTIN_VAR_GP_SHOULDERR:
            return RValue_makeReal(GP_SHOULDERR);
        case BUILTIN_VAR_GP_SHOULDERLB:
            return RValue_makeReal(GP_SHOULDERLB);
        case BUILTIN_VAR_GP_SHOULDERRB:
            return RValue_makeReal(GP_SHOULDERRB);
        case BUILTIN_VAR_GP_SELECT:
            return RValue_makeReal(GP_SELECT);
        case BUILTIN_VAR_GP_START:
            return RValue_makeReal(GP_START);
        case BUILTIN_VAR_GP_STICKL:
            return RValue_makeReal(GP_STICKL);
        case BUILTIN_VAR_GP_STICKR:
            return RValue_makeReal(GP_STICKR);
        case BUILTIN_VAR_GP_PADU:
            return RValue_makeReal(GP_PADU);
        case BUILTIN_VAR_GP_PADD:
            return RValue_makeReal(GP_PADD);
        case BUILTIN_VAR_GP_PADL:
            return RValue_makeReal(GP_PADL);
        case BUILTIN_VAR_GP_PADR:
            return RValue_makeReal(GP_PADR);
        case BUILTIN_VAR_GP_HOME:
            return RValue_makeReal(GP_HOME);
        case BUILTIN_VAR_GP_AXIS_LH:
            return RValue_makeReal(GP_AXIS_LH);
        case BUILTIN_VAR_GP_AXIS_LV:
            return RValue_makeReal(GP_AXIS_LV);
        case BUILTIN_VAR_GP_AXIS_RH:
            return RValue_makeReal(GP_AXIS_RH);
        case BUILTIN_VAR_GP_AXIS_RV:
            return RValue_makeReal(GP_AXIS_RV);

        case BUILTIN_VAR_INSTANCE_COUNT: {
            int32_t count = 0;
            int32_t instanceCount = (int32_t) arrlen(runner->instances);
            for (int32_t i = 0; instanceCount > i; i++) {
                if (runner->instances[i]->active) count++;
            }
            return RValue_makeReal((GMLReal) count);
        }
        case BUILTIN_VAR_INSTANCE_ID: {
            if (0 > arrayIndex) return RValue_makeReal((GMLReal) INSTANCE_NOONE);
            int32_t instanceCount = (int32_t) arrlen(runner->instances);
            int32_t active = 0;
            repeat(instanceCount, i) {
                Instance* candidate = runner->instances[i];
                if (!candidate->active) continue;
                if (active == arrayIndex) return RValue_makeReal((GMLReal) candidate->instanceId);
                active++;
            }
            return RValue_makeReal((GMLReal) INSTANCE_NOONE);
        }
        case BUILTIN_VAR_FPS:
            return RValue_makeReal(ctx->dataWin->gen8.gms2FPS);
        case BUILTIN_VAR_DEBUG_MODE:
            return RValue_makeBool(false);
        case BUILTIN_VAR_DELTA_TIME:
            return RValue_makeReal(runner->deltaTime);

        case BUILTIN_VAR_SCORE:
            return RValue_makeReal(runner->score);
        case BUILTIN_VAR_LIVES:
            return RValue_makeReal(runner->lives);
        case BUILTIN_VAR_HEALTH:
            return RValue_makeReal(runner->health);

        default:
            break;
    }

    fprintf(stderr, "VM: [%s] Unhandled built-in variable read '%s' (arrayIndex=%d)\n", ctx->currentCodeName, name, arrayIndex);
    return RValue_makeReal(0.0);
}

void VMBuiltins_setVariable(VMContext* ctx, Instance* inst, int16_t builtinVarId, const char* name, RValue val, int32_t arrayIndex) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: setVariable called but no runner!");
    requireNotNull(runner);

    // Structs: instance builtins are ordinary members.
    if (inst != nullptr && inst->objectIndex == STRUCT_OBJECT_INDEX && isInstanceScopedBuiltinVar(builtinVarId)) {
        VM_structSet(ctx, inst, name, val, arrayIndex);
        return;
    }

    switch (builtinVarId) {
        // Per-instance properties
        case BUILTIN_VAR_IMAGE_SPEED:
            if (inst == nullptr) break;
            inst->imageSpeed = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_IMAGE_INDEX: {
            if (inst == nullptr) break;
            inst->imageIndex = (float) RValue_toReal(val);
            return;
        }
        case BUILTIN_VAR_IMAGE_XSCALE: {
            if (inst == nullptr) break;
            float value = (float) RValue_toReal(val);
            bool changed = value != inst->imageXscale;
            if (changed) {
                inst->imageXscale = value;
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_IMAGE_YSCALE: {
            if (inst == nullptr) break;
            float value = (float) RValue_toReal(val);
            bool changed = value != inst->imageYscale;
            if (changed) {
                inst->imageYscale = value;
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_IMAGE_ANGLE: {
            if (inst == nullptr) break;
            float value = (float) RValue_toReal(val);
            bool changed = value != inst->imageAngle;
            if (changed) {
                inst->imageAngle = value;
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_IMAGE_ALPHA:
            if (inst == nullptr) break;
            inst->imageAlpha = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_IMAGE_BLEND:
            if (inst == nullptr) break;
            inst->imageBlend = (uint32_t) RValue_toReal(val);
            return;
        case BUILTIN_VAR_IMAGE_SINGLE: {
            if (inst == nullptr) break;
            float value = (float) RValue_toReal(val);
            if (value < 0.0) {
                inst->imageSpeed = 1.0;
            } else {
                inst->imageSpeed = 0.0;
                inst->imageIndex = value;
            }
            return;
        }
        case BUILTIN_VAR_SPRITE_INDEX: {
            if (inst == nullptr) break;
            int32_t value = RValue_toInt32(val);
            bool changed = value != inst->spriteIndex;
            if (changed) {
                inst->spriteIndex = value;
                // The native runner resets the image_index to zero if the new frame count is smaller than the current image_index
                if (value >= 0 && runner->dataWin->sprt.count > (uint32_t) value) {
                    int32_t newFrameCount = (int32_t) runner->dataWin->sprt.sprites[value].textureCount;
                    if (newFrameCount > 0 && (int32_t) inst->imageIndex >= newFrameCount) {
                        inst->imageIndex = 0;
                    }
                }
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_VISIBLE:
            if (inst == nullptr) break;
            inst->visible = RValue_toBool(val);
            return;
        case BUILTIN_VAR_DEPTH: {
            if (inst == nullptr) break;
            int32_t newDepth = RValue_toInt32(val);
            if (newDepth != inst->depth) {
                inst->depth = newDepth;
                ctx->runner->drawableListSortDirty = true;
            }
            return;
        }
        case BUILTIN_VAR_LAYER: {
            if (inst == nullptr) break;
            int32_t layerId = resolveLayerIdArg(runner, val);
            RuntimeLayer* rl = Runner_findRuntimeLayerById(runner, layerId);
            if (rl != nullptr) {
                if (inst->layer != layerId) {
                    Runner_removeInstanceLayerElement(runner, inst->instanceId);
                    Runner_addInstanceLayerElement(runner, layerId, inst->instanceId);
                }
                inst->layer = layerId;
                if (inst->depth != rl->depth) {
                    inst->depth = rl->depth;
                    runner->drawableListSortDirty = true;
                }
            }
            return;
        }
        case BUILTIN_VAR_X: {
            if (inst == nullptr) break;
            float value = (float) RValue_toReal(val);
            bool changed = value != inst->x;
            if (changed) {
                inst->x = (float) RValue_toReal(val);
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_Y: {
            if (inst == nullptr) break;
            float value = (float) RValue_toReal(val);
            bool changed = value != inst->y;
            if (changed) {
                inst->y = (float) RValue_toReal(val);
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_PERSISTENT:
            if (inst == nullptr) break;
            inst->persistent = RValue_toBool(val);
            return;
        case BUILTIN_VAR_SOLID:
            if (inst == nullptr) break;
            inst->solid = RValue_toBool(val);
            return;
        case BUILTIN_VAR_XPREVIOUS:
            if (inst == nullptr) break;
            inst->xprevious = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_YPREVIOUS:
            if (inst == nullptr) break;
            inst->yprevious = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_XSTART:
            if (inst == nullptr) break;
            inst->xstart = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_YSTART:
            if (inst == nullptr) break;
            inst->ystart = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_MASK_INDEX: {
            if (inst == nullptr) break;
            int32_t value = RValue_toInt32(val);
            bool changed = value != inst->maskIndex;
            if (changed) {
                inst->maskIndex = value;
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return;
        }
        case BUILTIN_VAR_SPEED:
            if (inst == nullptr) break;
            inst->speed = (float) RValue_toReal(val);
            Instance_computeComponentsFromSpeed(inst);
            return;
        case BUILTIN_VAR_DIRECTION: {
            if (inst == nullptr) break;
            GMLReal d = GMLReal_fmod(RValue_toReal(val), 360.0);
            if (d < 0.0) d += 360.0;
            inst->direction = (float) d;
            Instance_computeComponentsFromSpeed(inst);
            return;
        }
        case BUILTIN_VAR_HSPEED:
            if (inst == nullptr) break;
            inst->hspeed = (float) RValue_toReal(val);
            Instance_computeSpeedFromComponents(inst);
            return;
        case BUILTIN_VAR_VSPEED:
            if (inst == nullptr) break;
            inst->vspeed = (float) RValue_toReal(val);
            Instance_computeSpeedFromComponents(inst);
            return;
        case BUILTIN_VAR_FRICTION:
            if (inst == nullptr) break;
            inst->friction = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_GRAVITY:
            if (inst == nullptr) break;
            inst->gravity = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_GRAVITY_DIRECTION:
            if (inst == nullptr) break;
            inst->gravityDirection = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_ALARM: {
            if (inst == nullptr) break;
            if (isValidAlarmIndex(arrayIndex)) {
                int32_t newValue = RValue_toInt32(val);

#ifdef ENABLE_VM_TRACING
                if (inst->objectIndex >= 0 && (shgeti(ctx->alarmsToBeTraced, "*") != -1 || shgeti(ctx->alarmsToBeTraced, runner->dataWin->objt.objects[inst->objectIndex].name) != -1)) {
                    fprintf(stderr, "VM: [%s] Setting Alarm[%d] = %d (instanceId=%d)\n", runner->dataWin->objt.objects[inst->objectIndex].name, arrayIndex, newValue, inst->instanceId);
                }
#endif

                inst->alarm[arrayIndex] = newValue;
                if (newValue > 0) inst->activeAlarmMask |= (uint16_t) (1u << arrayIndex);
                else inst->activeAlarmMask &= (uint16_t) ~(1u << arrayIndex);
            }
            return;
        }

        // Path instance variables (writable)
        case BUILTIN_VAR_PATH_POSITION: {
            if (inst == nullptr) break;
            // Native GMS runner clamps path_position to [0.0, 1.0] on set
            float pos = (float) RValue_toReal(val);
            if (pos < 0.0f) pos = 0.0f;
            else if (pos > 1.0f) pos = 1.0f;
            inst->pathPosition = pos;
            return;
        }
        case BUILTIN_VAR_PATH_SPEED:
            if (inst == nullptr) break;
            inst->pathSpeed = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_PATH_SCALE:
            if (inst == nullptr) break;
            inst->pathScale = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_PATH_ORIENTATION:
            if (inst == nullptr) break;
            inst->pathOrientation = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_PATH_ENDACTION:
            if (inst == nullptr) break;
            inst->pathEndAction = RValue_toInt32(val);
            return;

        // Timeline instance variables
        case BUILTIN_VAR_TIMELINE_INDEX: {
            if (inst == nullptr) break;
            int32_t newIdx = RValue_toInt32(val);
            uint32_t tmlnCount = runner->dataWin->tmln.count;
            if (newIdx >= 0 && (uint32_t) newIdx >= tmlnCount) newIdx = -1;
            if (inst->timelineIndex != newIdx) {
                inst->timelineIndex = newIdx;
                inst->timelinePosition = 0.0f;
            }
            return;
        }
        case BUILTIN_VAR_TIMELINE_POSITION:
            if (inst == nullptr) break;
            inst->timelinePosition = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_TIMELINE_SPEED:
            if (inst == nullptr) break;
            inst->timelineSpeed = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_TIMELINE_LOOP:
            if (inst == nullptr) break;
            inst->timelineLoop = RValue_toBool(val);
            return;
        case BUILTIN_VAR_TIMELINE_RUNNING:
            if (inst == nullptr) break;
            inst->timelineRunning = RValue_toBool(val);
            return;

        // Keyboard variables
        case BUILTIN_VAR_KEYBOARD_KEY:
            runner->keyboard->lastKey = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_KEYBOARD_LASTCHAR:
            runner->keyboard->lastChar[0] = val.string[0];
            return;
        case BUILTIN_VAR_KEYBOARD_LASTKEY:
            runner->keyboard->lastKey = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_KEYBOARD_STRING: {
            const char* str = RValue_toString(val); 
            
            int32_t len = (int32_t)strlen(str);
            if (len > 1023) len = 1023;
            
            memcpy(runner->keyboard->string, str, len);
            runner->keyboard->string[len] = '\0';
            runner->keyboard->stringLen = len;
            return;
        }
        case BUILTIN_VAR_MOUSE_LASTBUTTON:
            runner->mouse->lastButton = RValue_toInt32(val);
            return;

        // View properties
        case BUILTIN_VAR_VIEW_XVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->viewX = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_YVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->viewY = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_WVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->viewWidth = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_HVIEW: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->viewHeight = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_XPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].portX = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_VIEW_YPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].portY = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_VIEW_WPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].portWidth = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_VIEW_HPORT:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].portHeight = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_VIEW_VISIBLE:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].enabled = RValue_toBool(val);
            return;
        case BUILTIN_VAR_VIEW_ENABLED:
            runner->viewsEnabled = RValue_toBool(val);
            return;
        case BUILTIN_VAR_CAMERA_VIEW:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].cameraId = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_VIEW_ANGLE: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->viewAngle = (float) RValue_toReal(val);
            return;
        }
        case BUILTIN_VAR_VIEW_HBORDER: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->borderX = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_VBORDER: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->borderY = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_OBJECT: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->objectId = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_HSPEED: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->speedX = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_VSPEED: {
            GMLCamera* camera = Runner_getCameraForView(runner, arrayIndex);
            if (camera != nullptr) camera->speedY = RValue_toInt32(val);
            return;
        }
        case BUILTIN_VAR_VIEW_SURFACE_ID:
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) runner->views[arrayIndex].surfaceId = RValue_toInt32(val);
            return;

        // Background properties
        case BUILTIN_VAR_BACKGROUND_VISIBLE:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].visible = RValue_toBool(val);
            return;
        case BUILTIN_VAR_BACKGROUND_INDEX:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].backgroundIndex = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_BACKGROUND_X:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].x = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_Y:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].y = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_XSCALE:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].xScale = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_YSCALE:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].yScale = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_HSPEED:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].speedX = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_VSPEED:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].speedY = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_ALPHA:
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].alpha = (float) RValue_toReal(val);
            return;
        case BUILTIN_VAR_BACKGROUND_COLOR:
        case BUILTIN_VAR_BACKGROUND_COLOUR:
            runner->backgroundColor = (uint32_t) RValue_toInt32(val);
            return;

        // Room properties
        case BUILTIN_VAR_ROOM:
            runner->pendingRoom = RValue_toInt32(val);
            return;
        case BUILTIN_VAR_ROOM_PERSISTENT:
            runner->currentRoom->persistent = RValue_toBool(val);
            return;
        case BUILTIN_VAR_ROOM_WIDTH:
            runner->currentRoom->width = (uint32_t) RValue_toInt32(val);
            return;
        case BUILTIN_VAR_ROOM_HEIGHT:
            runner->currentRoom->height = (uint32_t) RValue_toInt32(val);
            return;
        case BUILTIN_VAR_ROOM_SPEED:
            runner->currentRoom->speed = (uint32_t) RValue_toInt32(val);
            return;

        // Read-only variables (silently ignore with warning)
        case BUILTIN_VAR_OS_TYPE ... BUILTIN_VAR_OS_LLVM_WINPHONE:
        case BUILTIN_VAR_BUFFER_FIXED ... BUILTIN_VAR_BUFFER_SEEK_END:
        case BUILTIN_VAR_ID:
        case BUILTIN_VAR_OBJECT_INDEX:
        case BUILTIN_VAR_CURRENT_DAY:
        case BUILTIN_VAR_CURRENT_HOUR:
        case BUILTIN_VAR_CURRENT_MINUTE:
        case BUILTIN_VAR_CURRENT_MONTH:
        case BUILTIN_VAR_CURRENT_SECOND:
        case BUILTIN_VAR_CURRENT_TIME:
        case BUILTIN_VAR_CURRENT_WEEKDAY:
        case BUILTIN_VAR_CURRENT_YEAR:
        case BUILTIN_VAR_VIEW_CURRENT:
        case BUILTIN_VAR_PATH_INDEX:
        case BUILTIN_VAR_DEBUG_MODE:
        case BUILTIN_VAR_ROOM_FIRST:
        case BUILTIN_VAR_ROOM_LAST:
        case BUILTIN_VAR_GP_FACE1 ... BUILTIN_VAR_GP_AXIS_RV:
            fprintf(stderr, "VM: [%s] Attempted write to read-only built-in '%s'\n", ctx->currentCodeName, name);
            return;

        // argument[N] - array-style write to script arguments
        case BUILTIN_VAR_ARGUMENT:
            if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > arrayIndex && arrayIndex >= 0) {
                RValue_free(&ctx->scriptArgs[arrayIndex]);
                ctx->scriptArgs[arrayIndex] = val;
            }
            return;

        // Argument variables (argument0..argument15)
        case BUILTIN_VAR_ARGUMENT0 ... BUILTIN_VAR_ARGUMENT15: {
            int argNumber = builtinVarId - BUILTIN_VAR_ARGUMENT0;
            if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > argNumber) {
                RValue_free(&ctx->scriptArgs[argNumber]);
                ctx->scriptArgs[argNumber] = val;
            }
            return;
        }

        case BUILTIN_VAR_SCORE:
            runner->score = RValue_toReal(val);
            return;
        case BUILTIN_VAR_LIVES:
            Runner_setLives(runner, RValue_toReal(val));
            return;
        case BUILTIN_VAR_HEALTH:
            Runner_setHealth(runner, RValue_toReal(val));
            return;

        default:
            break;
    }

    fprintf(stderr, "VM: [%s] Unhandled built-in variable write '%s' (arrayIndex=%d)\n", ctx->currentCodeName, name, arrayIndex);
}

// ===[ BUILTIN FUNCTION IMPLEMENTATIONS ]===

static RValue builtin_show_debug_message(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "[show_debug_message] Expected at least 1 argument\n");
        return RValue_makeUndefined();
    }

    char* val = RValue_toString(args[0]);
    printf("Game: %s\n", val);
    free(val);

    return RValue_makeUndefined();
}

static RValue builtin_string_length(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeInt32(0);
    // GML converts non-string arguments to string before measuring length
    RValue value = args[0];
    // Fast path: If the RValue is already a string, just return its length instead of creating a copy
    if (value.type == RVALUE_STRING) {
        if (value.string == nullptr)
            return RValue_makeInt32(0);
        int32_t byteLen = (int32_t) strlen(value.string);
        return RValue_makeInt32(TextUtils_utf8CodepointCount(value.string, byteLen));
    }
    char* str = RValue_toString(value);
    int32_t byteLen = (int32_t) strlen(str);
    int32_t len = TextUtils_utf8CodepointCount(str, byteLen);
    free(str);
    return RValue_makeInt32(len);
}

// https://docs.vultr.com/clang/examples/remove-all-characters-in-a-string-except-alphabets
void filterAlphabets(char *str) {
    char result[strlen(str) + 1];
    int j = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if ((str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 'A' && str[i] <= 'Z')) {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';  // Null-terminate the result string
    strcpy(str, result);  // Optionally copy back to original string
}

static RValue builtin_string_letters(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeInt32(0);
    char* str = RValue_toString(args[0]);
    filterAlphabets(str);
    return RValue_makeString(str);
}

static RValue builtin_string_digits(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    int len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (result == NULL) return RValue_makeOwnedString(safeStrdup(""));

    int digitCount = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (isdigit((unsigned char) str[i])) result[digitCount++] = str[i];
    }

    free(str);
    result[digitCount] = '\0';

    if (digitCount == 0) {
        free(result);
        return RValue_makeOwnedString(safeStrdup(""));
    }

    char* exact_result = (char*)realloc(result, digitCount + 1);
    return RValue_makeOwnedString(exact_result ? exact_result : result);
}

static RValue builtin_string_lettersdigits(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    int len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (result == NULL) return RValue_makeOwnedString(safeStrdup(""));

    int count = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (isalnum((unsigned char) str[i])) result[count++] = str[i];
    }

    free(str);
    result[count] = '\0';

    if (count == 0) {
        free(result);
        return RValue_makeOwnedString(safeStrdup(""));
    }

    char* exact_result = (char*)realloc(result, count + 1);
    return RValue_makeOwnedString(exact_result ? exact_result : result);
}

static RValue builtin_string_byte_length(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeInt32(0);
    // GML converts non-string arguments to string before measuring length
    RValue value = args[0];
    // Fast path: If the RValue is already a string, just return its length instead of creating a copy
    if (value.type == RVALUE_STRING) {
        if (value.string == nullptr)
            return RValue_makeInt32(0);
        int32_t byteLen = (int32_t) strlen(value.string);
        return RValue_makeInt32(byteLen);
    }
    char* str = RValue_toString(value);
    int32_t byteLen = (int32_t) strlen(str);
    free(str);
    return RValue_makeInt32(byteLen);
}

static RValue builtin_real(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[0]));
}

static RValue builtin_string(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* result = RValue_toString(args[0]);
    return RValue_makeOwnedString(result);
}

static RValue builtin_floor(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_floor(RValue_toReal(args[0])));
}

static RValue builtin_ceil(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_ceil(RValue_toReal(args[0])));
}

static RValue builtin_round(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_bankersRound(RValue_toReal(args[0])));
}

static RValue builtin_abs(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_fabs(RValue_toReal(args[0])));
}

static RValue builtin_frac(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal val = RValue_toReal(args[0]);
    GMLReal truncated = (val >= 0.0) ? GMLReal_floor(val) : GMLReal_ceil(val);
    return RValue_makeReal(val - truncated);
}

static RValue builtin_sign(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal val = RValue_toReal(args[0]);
    GMLReal result = (val > 0.0) ? 1.0 : ((0.0 > val) ? -1.0 : 0.0);
    return RValue_makeReal(result);
}

static RValue builtin_max(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal result = -INFINITY;
    repeat(argCount, i) {
        GMLReal val = RValue_toReal(args[i]);
        if (val > result) result = val;
    }
    return RValue_makeReal(result);
}

static RValue builtin_min(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal result = INFINITY;
    repeat(argCount, i) {
        GMLReal val = RValue_toReal(args[i]);
        if (result > val) result = val;
    }
    return RValue_makeReal(result);
}

static int compareReals(const void* a, const void* b) {
    GMLReal lhs = *(const GMLReal*) a;
    GMLReal rhs = *(const GMLReal*) b;
    if (lhs > rhs) return 1;
    if (rhs > lhs) return -1;
    return 0;
}

static RValue builtin_median(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    // GMS docs cap median at 16 args; 32-element stack buffer gives 2x margin, with malloc fallback for safety.
    GMLReal stackBuf[32];
    GMLReal* buf = stackBuf;
    if (argCount > 32) buf = (GMLReal*) malloc(sizeof(GMLReal) * argCount);
    repeat(argCount, i) buf[i] = RValue_toReal(args[i]);
    qsort(buf, argCount, sizeof(GMLReal), compareReals);
    // Match HTML5: when argCount is even, return the upper of the two middle values (arr[argCount/2], not arr[argCount/2 - 1]).
    GMLReal result = buf[argCount / 2];
    if (stackBuf != buf) free(buf);
    return RValue_makeReal(result);
}

static RValue builtin_power(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_pow(RValue_toReal(args[0]), RValue_toReal(args[1])));
}

static RValue builtin_sqrt(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_sqrt(RValue_toReal(args[0])));
}

static RValue builtin_log2(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_log2(RValue_toReal(args[0])));
}

static RValue builtin_sqr(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal val = RValue_toReal(args[0]);
    return RValue_makeReal(val * val);
}

static RValue builtin_is_string(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_STRING);
}

static RValue builtin_is_real(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    bool result = args[0].type == RVALUE_REAL || args[0].type == RVALUE_INT32 || args[0].type == RVALUE_INT64 || args[0].type == RVALUE_BOOL;
    return RValue_makeBool(result);
}

static RValue builtin_is_nan(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_REAL && isnan(RValue_toReal(args[0])));
}

static RValue builtin_is_infinity(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_REAL && isinf(RValue_toReal(args[0])));
}

static RValue builtin_is_bool(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_BOOL);
}

static RValue builtin_is_array(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_ARRAY);
}

static RValue builtin_is_struct(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_STRUCT);
}

static RValue builtin_is_int32(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_INT32);
}

static RValue builtin_is_int64(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_INT64);
}

static RValue builtin_is_undefined(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(true);
    return RValue_makeBool(args[0].type == RVALUE_UNDEFINED);
}

#if IS_WAD17_OR_HIGHER_ENABLED
static RValue builtin_is_method(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_METHOD);
}

static RValue builtin_is_callable(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    RValue v = args[0];

    if (v.type == RVALUE_METHOD) return RValue_makeBool(v.method != nullptr);
    if (v.type == RVALUE_ASSETREF) return RValue_makeBool(v.assetRefType == ASSET_TYPE_SCRIPT);

    if (v.type == RVALUE_REAL || v.type == RVALUE_INT32 || v.type == RVALUE_INT64) {
        int32_t idx = RValue_toInt32(v);
        if (0 > idx) return RValue_makeBool(false);

        // BC17+: scriptName compiles to a FUNC-table index. Resolve via codeIndexByName or builtinMap.
        if (ctx->dataWin->func.functionCount > (uint32_t) idx) {
            const char* funcName = ctx->dataWin->func.functions[idx].name;
            if (funcName != nullptr) {
                if (shgeti(ctx->codeIndexByName, (char*) funcName) >= 0) return RValue_makeBool(true);
                if (shgeti(ctx->builtinMap, (char*) funcName) >= 0) return RValue_makeBool(true);
            }
        }

        // Fallback: SCPT index
        if (ctx->dataWin->scpt.count > (uint32_t) idx) {
            int32_t codeId = ctx->dataWin->scpt.scripts[idx].codeId;
            if (codeId >= 0 && ctx->dataWin->code.count > (uint32_t) codeId) return RValue_makeBool(true);
        }
        return RValue_makeBool(false);
    }

    return RValue_makeBool(false);
}
#endif

// ===[ STRING FUNCTIONS ]===

static RValue builtin_string_upper(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* result = RValue_toString(args[0]);
    for (char* p = result; *p; p++) *p = (char) toupper((unsigned char) *p);
    return RValue_makeOwnedString(result);
}

static RValue builtin_string_lower(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* result = RValue_toString(args[0]);
    for (char* p = result; *p; p++) *p = (char) tolower((unsigned char) *p);
    return RValue_makeOwnedString(result);
}

static RValue builtin_string_copy(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    int32_t len = RValue_toInt32(args[2]);
    if (0 >= len) {
        return RValue_makeOwnedString(safeStrdup(""));
    }

    char* str = RValue_toString(args[0]);
    int32_t pos = RValue_toInt32(args[1]) - 1; // GMS is 1-based
    int32_t strLen = (int32_t) strlen(str);

    if (0 > pos) pos = 0;

    int32_t byteStart = TextUtils_utf8AdvanceCodepoints(str, strLen, pos);
    if (byteStart >= strLen) {
        free(str);
        return RValue_makeOwnedString(safeStrdup(""));
    }

    int32_t byteEnd = byteStart + TextUtils_utf8AdvanceCodepoints(str + byteStart, strLen - byteStart, len);
    if (byteEnd > strLen) byteEnd = strLen;

    int32_t nbytes = byteEnd - byteStart;
    char* result = safeMalloc(nbytes + 1);
    memcpy(result, str + byteStart, (size_t) nbytes);
    result[nbytes] = '\0';

    free(str);

    return RValue_makeOwnedString(result);
}

static RValue builtin_string_format(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    if (args[0].type == RVALUE_UNDEFINED) return RValue_makeOwnedString(safeStrdup("undefined"));

    GMLReal val = RValue_toReal(args[0]);
    int32_t tot = RValue_toInt32(args[1]);
    int32_t dec = RValue_toInt32(args[2]);
    if (0 > dec) dec = 0;
    if (15 < dec) dec = 15;

    char numBuf[64];
    snprintf(numBuf, sizeof(numBuf), "%.*f", (int) dec, (double) val);

    const char* dot = strchr(numBuf, '.');
    int32_t intLen = (int32_t) (dot ? (dot - numBuf) : (int32_t) strlen(numBuf));

    int32_t leftPad = (tot > intLen) ? (tot - intLen) : 0;
    int32_t numLen = (int32_t) strlen(numBuf);
    int32_t totalLen = leftPad + numLen;

    char* result = safeMalloc(totalLen + 1);
    for (int32_t i = 0; leftPad > i; i++) result[i] = ' ';
    memcpy(result + leftPad, numBuf, (size_t) numLen);
    result[totalLen] = '\0';
    return RValue_makeOwnedString(result);
}

static RValue builtin_string_repeat(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    int32_t count = RValue_toInt32(args[1]);
    if (0 >= count || str[0] == '\0') {
        free(str);
        return RValue_makeOwnedString(safeStrdup(""));
    }

    size_t strLen = strlen(str);
    size_t totalLen = strLen * (size_t) count;
    char* result = safeMalloc(totalLen + 1);
    repeat(count, i) {
        memcpy(result + i * strLen, str, strLen);
    }
    result[totalLen] = '\0';
    free(str);
    return RValue_makeOwnedString(result);
}

static RValue builtin_string_count(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeInt32(0);
    char* substr = RValue_toString(args[0]);
    char* str = RValue_toString(args[1]);
    size_t strLen = strlen(str);
    size_t substrLen = strlen(substr);
    int32_t count = 0;

    if (substrLen > strLen) {
        free(substr);
        free(str);
        return RValue_makeInt32(0);
    }

    repeat(strLen, i) {
        if (strncmp(str + i, substr, substrLen) == 0)
            count++;
    }

    free(substr);
    free(str);
    return RValue_makeInt32(count);
}

static RValue builtin_ord(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING || args[0].string == nullptr || args[0].string[0] == '\0') {
        return RValue_makeReal(0.0);
    }
    const char* str = args[0].string;
    int32_t pos = 0;
    uint16_t cp = TextUtils_decodeUtf8(str, (int32_t)strlen(str), &pos);
    return RValue_makeReal((GMLReal) cp);
}

static RValue builtin_chr(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    uint32_t cp = (uint32_t) RValue_toInt32(args[0]);
    char buf[5];
    int32_t n = TextUtils_utf8EncodeCodepoint(cp, buf);
    if (0 >= n) return RValue_makeOwnedString(safeStrdup(""));
    buf[n] = '\0';
    return RValue_makeOwnedString(safeStrdup(buf));
}

static RValue builtin_string_pos(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    char* needle = RValue_toString(args[0]);
    char* haystack = RValue_toString(args[1]);
    char* found = strstr(haystack, needle);
    if (found == nullptr) {
        free(haystack);
        free(needle);
        return RValue_makeReal(0.0);
    }
    int32_t byteIndex = (int32_t) (found - haystack);
    int32_t charIndex = TextUtils_utf8CodepointCount(haystack, byteIndex) + 1; // 1-based codepoint index
    free(haystack);
    free(needle);
    return RValue_makeReal((GMLReal) charIndex);
}

// Appends a copy of [start, start + len) to the array as an owned string, growing it by one slot.
static void appendSplitSegment(GMLArray* arr, int32_t* count, const char* start, int32_t len) {
    char* segment = safeMalloc((size_t) len + 1);
    if (len > 0) memcpy(segment, start, (size_t) len);
    segment[len] = '\0';
    GMLArray_growTo(arr, *count + 1);
    RValue* slot = GMLArray_slot(arr, *count);
    *slot = RValue_makeOwnedString(segment);
    (*count)++;
}

static RValue builtin_string_split(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeArray(GMLArray_create(0));
    char* string = RValue_toString(args[0]);
    char* delimiter = RValue_toString(args[1]);
    bool removeEmpty = argCount > 2 ? RValue_toBool(args[2]) : false;
    // maxSplits is actually a real (the native runner compares it as a double), but how are you going to split something by... 0.5?
    GMLReal maxSplits = argCount > 3 ? RValue_toReal(args[3]) : (GMLReal) INT32_MAX;

    int32_t delimiterLen = (int32_t) strlen(delimiter);

    // Native runner returns an empty array when maxSplits was explicitly given and is <= 0, or when the delimiter is empty.
    if ((argCount > 3 && 0.0 >= maxSplits) || delimiterLen == 0) {
        free(string);
        free(delimiter);
        return RValue_makeArray(GMLArray_create(0));
    }

    GMLArray* out = GMLArray_create(0);
    int32_t count = 0;

    int32_t stringLen = (int32_t) strlen(string);
    const char* end = string + stringLen;
    const char* segmentStart = string; // Start of the current (not yet emitted) segment
    const char* cursor = string;
    int32_t splits = 0;

    // Keep splitting until we run out of room for another delimiter or hit maxSplits.
    // Like the native runner, we only test for the delimiter at UTF-8 codepoint boundaries.
    while (maxSplits > (GMLReal) splits && end - delimiterLen >= cursor) {
        if (memcmp(cursor, delimiter, (size_t) delimiterLen) == 0) {
            int32_t segmentLen = (int32_t) (cursor - segmentStart);
            if (!(removeEmpty && segmentLen == 0)) appendSplitSegment(out, &count, segmentStart, segmentLen);
            cursor += delimiterLen;
            segmentStart = cursor;
            splits++;
        } else {
            // Advance one codepoint so the next strncmp lands on a codepoint boundary.
            int32_t consumed = 0;
            TextUtils_decodeUtf8(cursor, (int32_t) (end - cursor), &consumed);
            if (0 >= consumed) consumed = 1;
            cursor += consumed;
        }
    }

    // Whatever is left becomes the final segment.
    int32_t tailLen = (int32_t) (end - segmentStart);
    if (!(removeEmpty && tailLen == 0)) appendSplitSegment(out, &count, segmentStart, tailLen);

    free(string);
    free(delimiter);
    return RValue_makeArray(out);
}

static RValue builtin_string_char_at(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    int32_t pos = RValue_toInt32(args[1]) - 1; // 1-based
    int32_t strLen = (int32_t) strlen(str);
    if (0 > pos || pos >= strLen) {
        free(str);
        return RValue_makeOwnedString(safeStrdup(""));
    }
    int32_t byteStart = TextUtils_utf8AdvanceCodepoints(str, strLen, pos);
    if (byteStart >= strLen) {
        free(str);
        return RValue_makeOwnedString(safeStrdup(""));
    }
    int32_t byteNext = byteStart;
    TextUtils_decodeUtf8(str, strLen, &byteNext);
    int32_t nbytes = byteNext - byteStart;
    char* out = safeMalloc(nbytes + 1);
    memcpy(out, str + byteStart, (size_t) nbytes);
    out[nbytes] = '\0';
    free(str);
    return RValue_makeOwnedString(out);
}

static RValue builtin_string_ord_at(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-1.0);
    char* str = RValue_toString(args[0]);
    int32_t pos = RValue_toInt32(args[1]) - 1; // 1-based
    int32_t strLen = (int32_t) strlen(str);
    if (strLen == 0) {
        free(str);
        return RValue_makeReal(-1.0);
    }
    if (0 > pos) pos = 0; // native clamps negative indices to the first character
    int32_t byteStart = TextUtils_utf8AdvanceCodepoints(str, strLen, pos);
    if (byteStart >= strLen) {
        free(str);
        return RValue_makeReal(-1.0);
    }
    int32_t offset = byteStart;
    uint16_t codepoint = TextUtils_decodeUtf8(str, strLen, &offset);
    free(str);
    return RValue_makeReal((GMLReal) codepoint);
}

static RValue builtin_string_delete(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    int32_t pos = RValue_toInt32(args[1]) - 1; // 1-based
    int32_t count = RValue_toInt32(args[2]);
    int32_t strLen = (int32_t) strlen(str);

    if (0 > pos || pos >= strLen || 0 >= count) return RValue_makeOwnedString(str);

    int32_t byteStart = TextUtils_utf8AdvanceCodepoints(str, strLen, pos);
    if (byteStart >= strLen) return RValue_makeOwnedString(str);

    int32_t byteEnd = byteStart + TextUtils_utf8AdvanceCodepoints(str + byteStart, strLen - byteStart, count);
    if (byteEnd > strLen) byteEnd = strLen;

    int32_t removeLen = byteEnd - byteStart;
    char* result = safeMalloc(strLen - removeLen + 1);
    memcpy(result, str, (size_t) byteStart);
    memcpy(result + byteStart, str + byteEnd, (size_t) (strLen - byteEnd));
    result[strLen - removeLen] = '\0';

    free(str);

    return RValue_makeOwnedString(result);
}

static RValue builtin_string_insert(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* substr = RValue_toString(args[0]);
    char* str = RValue_toString(args[1]);
    int32_t pos = RValue_toInt32(args[2]) - 1; // 1-based
    int32_t strLen = (int32_t) strlen(str);
    int32_t subLen = (int32_t) strlen(substr);

    if (0 > pos) pos = 0;
    int32_t bytePos = TextUtils_utf8AdvanceCodepoints(str, strLen, pos);
    if (bytePos > strLen) bytePos = strLen;

    char* result = safeMalloc(strLen + subLen + 1);
    memcpy(result, str, (size_t) bytePos);
    memcpy(result + bytePos, substr, (size_t) subLen);
    memcpy(result + bytePos + subLen, str + bytePos, (size_t) (strLen - bytePos));
    result[strLen + subLen] = '\0';

    free(substr);
    free(str);

    return RValue_makeOwnedString(result);
}

static RValue builtin_string_replace(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    char* needle = RValue_toString(args[1]);
    int32_t strLen = (int32_t) strlen(str);
    int32_t needleLen = (int32_t) strlen(needle);
    if (0 == needleLen) {
        free(needle);
        return RValue_makeOwnedString(str);
    }

    char* replacement = RValue_toString(args[2]);
    int32_t replacementLen = (int32_t) strlen(replacement);

    // There can be only ONE.
    char *appearance = strstr(str, needle);
    if (!appearance) {
        free(needle);
        free(replacement);
        return RValue_makeOwnedString(str);
    }

    int32_t newLen = strLen - needleLen + replacementLen;
    int32_t before = (int32_t) (appearance - str);
    char *outputString = safeMalloc(newLen + 1);

    memcpy(outputString, str, before);
    memcpy(outputString + before, replacement, replacementLen);
    strcpy(outputString + before + replacementLen, appearance + needleLen);

    free(str);
    free(needle);
    free(replacement);

    return RValue_makeOwnedString(outputString);
}

static RValue builtin_string_replace_all(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* str = RValue_toString(args[0]);
    char* needle = RValue_toString(args[1]);
    int32_t needleLen = (int32_t) strlen(needle);
    if (0 == needleLen) {
        free(needle);
        return RValue_makeOwnedString(str);
    }

    char* replacement = RValue_toString(args[2]);
    int32_t replacementLen = (int32_t) strlen(replacement);

    // Count occurrences to pre-allocate
    int32_t count = 0;
    const char* p = str;
    while ((p = strstr(p, needle)) != nullptr) { count++; p += needleLen; }

    int32_t strLen = (int32_t) strlen(str);
    int32_t resultLen = strLen + count * (replacementLen - needleLen);
    char* result = safeMalloc(resultLen + 1);
    char* out = result;
    p = str;
    const char* match;
    while ((match = strstr(p, needle)) != nullptr) {
        int32_t before = (int32_t) (match - p);
        memcpy(out, p, before);
        out += before;
        memcpy(out, replacement, replacementLen);
        out += replacementLen;
        p = match + needleLen;
    }
    strcpy(out, p);

    free(replacement);
    free(needle);
    free(str);

    return RValue_makeOwnedString(result);
}

// ===[ MATH FUNCTIONS ]===


static RValue builtin_arctan(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal y = RValue_toReal(args[0]);
    return RValue_makeReal(GMLReal_atan(y));
}

static RValue builtin_darctan(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal y = RValue_toReal(args[0]);
    return RValue_makeReal(GMLReal_atan(y) * (180.0 / M_PI));
}

static RValue builtin_darctan2(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    GMLReal y = RValue_toReal(args[0]);
    GMLReal x = RValue_toReal(args[1]);
    return RValue_makeReal(GMLReal_atan2(y, x) * (180.0 / M_PI));
}

static RValue builtin_sin(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_sin(RValue_toReal(args[0])));
}

static RValue builtin_arcsin(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_asin(RValue_toReal(args[0])));
}

static RValue builtin_cos(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_cos(RValue_toReal(args[0])));
}

static RValue builtin_arccos(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_acos(RValue_toReal(args[0])));
}

static RValue builtin_dsin(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_sin(RValue_toReal(args[0]) * (M_PI / 180.0)));
}

static RValue builtin_dcos(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(GMLReal_cos(RValue_toReal(args[0]) * (M_PI / 180.0)));
}

static RValue builtin_degtorad(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[0]) * (M_PI / 180.0));
}

static RValue builtin_radtodeg(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[0]) * (180.0 / M_PI));
}

static RValue builtin_clamp(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    GMLReal val = RValue_toReal(args[0]);
    GMLReal lo = RValue_toReal(args[1]);
    GMLReal hi = RValue_toReal(args[2]);
    if (lo > val) val = lo;
    if (val > hi) val = hi;
    return RValue_makeReal(val);
}

static RValue builtin_lerp(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    GMLReal a = RValue_toReal(args[0]);
    GMLReal b = RValue_toReal(args[1]);
    GMLReal t = RValue_toReal(args[2]);
    GMLReal result = a + (b - a) * t;
#ifdef USE_FLOAT_REALS
    // When using floats, floating point inaccuracies can cause games to softlock, so if the lerp did not do any meaningful movement, we'll *nudge* it a bit forward.
    // This COULD have unforeseen consequences, but it also fixes some games (example: DELTARUNE Chapter 2's pre-giga queen cutscene)
    if (result == a && a != b) result = GMLReal_nextafter(a, b);
#endif
    return RValue_makeReal(result);
}

static RValue builtin_point_distance(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeReal(0.0);
    GMLReal dx = RValue_toReal(args[2]) - RValue_toReal(args[0]);
    GMLReal dy = RValue_toReal(args[3]) - RValue_toReal(args[1]);
    return RValue_makeReal(GMLReal_sqrt(dx * dx + dy * dy));
}

static RValue builtin_point_in_rectangle(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (6 > argCount) return RValue_makeBool(false);
    GMLReal px = RValue_toReal(args[0]);
    GMLReal py = RValue_toReal(args[1]);
    GMLReal x1 = RValue_toReal(args[2]);
    GMLReal y1 = RValue_toReal(args[3]);
    GMLReal x2 = RValue_toReal(args[4]);
    GMLReal y2 = RValue_toReal(args[5]);
    return RValue_makeBool(px >= x1 && px <= x2 && py >= y1 && py <= y2);
}

static RValue builtin_point_in_circle(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeBool(false);
    GMLReal px = RValue_toReal(args[0]);
    GMLReal py = RValue_toReal(args[1]);
    GMLReal cx = RValue_toReal(args[2]);
    GMLReal cy = RValue_toReal(args[3]);
    GMLReal rad = RValue_toReal(args[4]);
    GMLReal dx = px - cx;
    GMLReal dy = py - cy;
    return RValue_makeBool(dx * dx + dy * dy <= rad * rad);
}

static RValue builtin_distance_to_point(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    GMLReal px = RValue_toReal(args[0]);
    GMLReal py = RValue_toReal(args[1]);

    Instance* inst = ctx->currentInstance;
    InstanceBBox bbox = Collision_computeBBox(ctx->runner, inst);
    GMLReal bboxLeft, bboxRight, bboxTop, bboxBottom;
    if (!bbox.valid) {
        // No sprite/mask: treat bbox as a single point at (x, y)
        bboxLeft = bboxRight = inst->x;
        bboxTop = bboxBottom = inst->y;
    } else {
        bboxLeft = bbox.left;
        bboxRight = bbox.right;
        bboxTop = bbox.top;
        bboxBottom = bbox.bottom;
    }

    // Distance from point to nearest edge of bbox (0 if inside)
    GMLReal xd = 0.0;
    GMLReal yd = 0.0;
    if (px > bboxRight)  xd = px - bboxRight;
    if (px < bboxLeft)   xd = px - bboxLeft;
    if (py > bboxBottom) yd = py - bboxBottom;
    if (py < bboxTop)    yd = py - bboxTop;

    return RValue_makeReal(GMLReal_sqrt(xd * xd + yd * yd));
}

// distance_to_object(obj)
// Returns the minimum bbox-to-bbox distance between the calling instance and the nearest instance of the given object.
static RValue builtin_distance_to_object(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);

    Runner* runner = ctx->runner;
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[0]));
    Instance* self = ctx->currentInstance;

    // Compute self bbox
    Sprite* selfSpr = Collision_getSprite(ctx->dataWin, self);
    if (selfSpr == nullptr) return RValue_makeReal(0.0);
    InstanceBBox selfBBox = Collision_computeBBox(ctx->runner, self);
    if (!selfBBox.valid) return RValue_makeReal(0.0);

    GMLReal minDistSq = 1e20;

    int32_t snapBase = Runner_pushInstancesForTarget(runner, targetObjIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (!inst->active || inst == self) continue;

        InstanceBBox otherBBox = Collision_computeBBox(ctx->runner, inst);
        if (!otherBBox.valid) continue;

        GMLReal xd = 0.0;
        GMLReal yd = 0.0;
        if (otherBBox.left > selfBBox.right)  xd = otherBBox.left - selfBBox.right;
        if (selfBBox.left > otherBBox.right)  xd = selfBBox.left - otherBBox.right;
        if (otherBBox.top > selfBBox.bottom)  yd = otherBBox.top - selfBBox.bottom;
        if (selfBBox.top > otherBBox.bottom)  yd = selfBBox.top - otherBBox.bottom;

        GMLReal distSq = xd * xd + yd * yd;
        if (minDistSq > distSq) minDistSq = distSq;
    }
    Runner_popInstanceSnapshot(runner, snapBase);

    return RValue_makeReal(GMLReal_sqrt(minDistSq));
}

static RValue builtin_point_direction(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeReal(0.0);
    GMLReal dx = RValue_toReal(args[2]) - RValue_toReal(args[0]);
    GMLReal dy = RValue_toReal(args[3]) - RValue_toReal(args[1]);
    return RValue_makeReal(GMLReal_atan2(-dy, dx) * (180.0 / M_PI));
}

static RValue builtin_angle_difference(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    GMLReal src = RValue_toReal(args[0]);
    GMLReal dest = RValue_toReal(args[1]);
    return RValue_makeReal(GMLReal_fmod(GMLReal_fmod(src - dest, 360.0) + 540.0, 360.0) - 180.0);
}

static RValue builtin_move_towards_point(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal targetX = RValue_toReal(args[0]);
    GMLReal targetY = RValue_toReal(args[1]);
    GMLReal spd = RValue_toReal(args[2]);
    Instance* inst = ctx->currentInstance;
    GMLReal dx = targetX - inst->x;
    GMLReal dy = targetY - inst->y;
    GMLReal dir = GMLReal_atan2(-dy, dx) * (180.0 / M_PI);
    if (dir < 0.0) dir += 360.0;
    inst->direction = (float) dir;
    inst->speed = (float) spd;
    Instance_computeComponentsFromSpeed(inst);
    return RValue_makeReal(0.0);
}

static RValue builtin_move_snap(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal hsnap = RValue_toReal(args[0]);
    GMLReal vsnap = RValue_toReal(args[1]);
    Instance* inst = ctx->currentInstance;
    if (hsnap > 0.0) {
        inst->x = (float) (GMLReal_floor((inst->x / hsnap) + 0.5) * hsnap);
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
    }
    if (vsnap > 0.0) {
        inst->y = (float) (GMLReal_floor((inst->y / vsnap) + 0.5) * vsnap);
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
    }
    return RValue_makeReal(0.0);
}

// For lengthdir: Anything that's 1e-4 > abs(result) should be coerced to 0 to avoid precision drift.
// If not, precision drift can cause a LOT of issues, especially on platforms that use floats instead of doubles.
static RValue builtin_lengthdir_x(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    GMLReal len = RValue_toReal(args[0]);
    GMLReal dir = RValue_toReal(args[1]) * (M_PI / 180.0);
    GMLReal result = len * GMLReal_cos(dir);
    if ((GMLReal) 1e-4 > GMLReal_fabs(result)) result = 0.0;
    return RValue_makeReal(result);
}

static RValue builtin_lengthdir_y(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    GMLReal len = RValue_toReal(args[0]);
    GMLReal dir = RValue_toReal(args[1]) * (M_PI / 180.0);
    GMLReal result = -len * GMLReal_sin(dir);
    if ((GMLReal) 1e-4 > GMLReal_fabs(result)) result = 0.0;
    return RValue_makeReal(result);
}

// ===[ MATRIX FUNCTIONS ]===

static bool rvalueIsMatrix(RValue rv) {
    if (rv.type != RVALUE_ARRAY) return false;
    if (GMLArray_length1D(rv.array) != 16) return false;
    repeat (16, i) {
        RValueType type = GMLArray_slot(rv.array, i)->type;
        if (type != RVALUE_REAL && type != RVALUE_INT32 && type != RVALUE_INT64)
            return false;
    }
    return true;
}
static bool matrixFromGml(Matrix4f *mat, GMLArray *arr) {
    if (GMLArray_length1D(arr) != 16) return false;
    repeat (16, i) {
        mat->m[i] = RValue_toReal(*GMLArray_slot(arr, i));
    }
    return true;
}
static GMLArray *matrixToGml(const Matrix4f *mat) {
    GMLArray *out = GMLArray_create(4 * 4);
    repeat (16, i) {
        *GMLArray_slot(out, i) = RValue_makeReal(mat->m[i]);
    }
    return out;
}
static RValue builtin_matrix_build_identity(MAYBE_UNUSED VMContext *ctx, MAYBE_UNUSED RValue *args, MAYBE_UNUSED int32_t argCount) {
    Matrix4f id;
    return RValue_makeArray(matrixToGml(Matrix4f_identity(&id)));
}
static RValue builtin_matrix_inverse(MAYBE_UNUSED VMContext *ctx, RValue *args, int32_t argCount) {
    if (argCount < 1 || argCount > 2) return RValue_makeUndefined();
    if (!rvalueIsMatrix(args[0])) return RValue_makeUndefined();

    bool toPrevMatrix = argCount == 2;
    GMLArray *destArray = toPrevMatrix ? args[1].array : nullptr;
    if (toPrevMatrix && !rvalueIsMatrix(args[1])) return RValue_makeUndefined();

    Matrix4f source, inverse;
    matrixFromGml(&source, args[0].array);
    if (!Matrix4f_inverse(&inverse, &source)) {
        return RValue_makeUndefined();
    } else if (!toPrevMatrix) {
        return RValue_makeArray(matrixToGml(&inverse));
    } else {
        repeat (16, i) {
            *GMLArray_slot(destArray, i) = RValue_makeReal(inverse.m[i]);
        }
        return RValue_makeArrayWeak(destArray);
    }
}

static RValue builtin_matrix_multiply(MAYBE_UNUSED VMContext *ctx, RValue *args, int32_t argCount) {
    if (argCount < 2 || argCount > 3) return RValue_makeUndefined();
    if (!rvalueIsMatrix(args[0]) || !rvalueIsMatrix(args[1])) return RValue_makeUndefined();

    bool toPrevMatrix = argCount == 3;
    GMLArray *destArray = toPrevMatrix ? args[2].array : nullptr;
    if (toPrevMatrix && !rvalueIsMatrix(args[2])) return RValue_makeUndefined();

    Matrix4f a, b, r;
    matrixFromGml(&a, args[0].array);
    matrixFromGml(&b, args[1].array);
    Matrix4f_multiply(&r, &a, &b);

    if (!toPrevMatrix) {
        return RValue_makeArray(matrixToGml(&r));
    } else {
        repeat (16, i) {
            *GMLArray_slot(destArray, i) = RValue_makeReal(r.m[i]);
        }
        return RValue_makeArrayWeak(destArray);
    }
}

static RValue builtin_matrix_build_projection_ortho(MAYBE_UNUSED VMContext *ctx, RValue *args, int32_t argCount) {
    if (argCount < 4 || argCount > 5) return RValue_makeUndefined();
    GMLReal width = RValue_toReal(args[0]);
    GMLReal height = RValue_toReal(args[1]);
    GMLReal znear = RValue_toReal(args[2]);
    GMLReal zfar = RValue_toReal(args[3]);

    bool toPrevMatrix = argCount == 5;
    GMLArray *destArray = toPrevMatrix ? args[4].array : nullptr;
    if (toPrevMatrix && !rvalueIsMatrix(args[4])) return RValue_makeUndefined();

    Matrix4f mat;

    memset(mat.m, 0, sizeof(mat.m));
    mat.m[Matrix_getIndex(0,0)] = 2.0f / width;
    mat.m[Matrix_getIndex(1,1)] = 2.0f / height;
    mat.m[Matrix_getIndex(2,2)] = 1.0f / (zfar - znear);
    mat.m[Matrix_getIndex(3,3)] = 1.0f;

    mat.m[Matrix_getIndex(2,3)] = znear / (znear - zfar);

    if (!toPrevMatrix) {
        return RValue_makeArray(matrixToGml(&mat));
    } else {
        repeat (16, i) {
            *GMLArray_slot(destArray, i) = RValue_makeReal(mat.m[i]);
        }
        return RValue_makeArrayWeak(destArray);
    }
}

static RValue builtin_matrix_build_projection_perspective_fov(MAYBE_UNUSED VMContext *ctx, RValue *args, int32_t argCount) {
    if (argCount < 4 || argCount > 5) return RValue_makeUndefined();
    GMLReal fov = RValue_toReal(args[0]) * (M_PI / 180.0);
    GMLReal aspect = RValue_toReal(args[1]);
    GMLReal znear = RValue_toReal(args[2]);
    GMLReal zfar = RValue_toReal(args[3]);

    bool toPrevMatrix = argCount == 5;
    GMLArray *destArray = toPrevMatrix ? args[4].array : nullptr;
    if (toPrevMatrix && !rvalueIsMatrix(args[4])) return RValue_makeUndefined();

    GMLReal scaleY = 1. / GMLReal_tan(fov / 2.);
    GMLReal scaleX = scaleY / aspect;

    Matrix4f mat;
    memset(mat.m, 0, sizeof(mat.m));

    mat.m[Matrix_getIndex(0, 0)] = scaleX;
    mat.m[Matrix_getIndex(1, 1)] = scaleY;
    mat.m[Matrix_getIndex(2, 2)] = zfar / (zfar - znear);
    mat.m[Matrix_getIndex(2, 3)] = -(zfar * znear) / (zfar - znear);
    mat.m[Matrix_getIndex(3, 2)] = 1.;

    if (!toPrevMatrix) {
        return RValue_makeArray(matrixToGml(&mat));
    } else {
        repeat (16, i) {
            *GMLArray_slot(destArray, i) = RValue_makeReal(mat.m[i]);
        }
        return RValue_makeArrayWeak(destArray);
    }
}

static RValue builtin_matrix_build_lookat(MAYBE_UNUSED VMContext *ctx, RValue *args, int32_t argCount) {
    if (argCount < 9 || argCount > 10) return RValue_makeUndefined();

    GMLReal xFrom = RValue_toReal(args[0]);
    GMLReal yFrom = RValue_toReal(args[1]);
    GMLReal zFrom = RValue_toReal(args[2]);

    GMLReal xTo = RValue_toReal(args[3]);
    GMLReal yTo = RValue_toReal(args[4]);
    GMLReal zTo = RValue_toReal(args[5]);

    GMLReal xUp = RValue_toReal(args[6]);
    GMLReal yUp = RValue_toReal(args[7]);
    GMLReal zUp = RValue_toReal(args[8]);
    GMLReal magUp = GMLReal_sqrt(xUp * xUp + yUp * yUp + zUp * zUp);
    xUp /= magUp;
    yUp /= magUp;
    zUp /= magUp;

    GMLReal xLook = xTo - xFrom;
    GMLReal yLook = yTo - yFrom;
    GMLReal zLook = zTo - zFrom;
    GMLReal magLook = GMLReal_sqrt(xLook * xLook + yLook * yLook + zLook * zLook);
    xLook /= magLook;
    yLook /= magLook;
    zLook /= magLook;

    // normalised cross product between Up and Look
    GMLReal xRight = yUp * zLook - zUp * yLook;
    GMLReal yRight = zUp * xLook - xUp * zLook;
    GMLReal zRight = xUp * yLook - yUp * xLook;
    GMLReal magRight = GMLReal_sqrt(xRight * xRight + yRight * yRight + zRight * zRight);
    xRight /= magRight;
    yRight /= magRight;
    zRight /= magRight;

    // normalised cross product between Look and Right
    xUp = yLook * zRight - zLook * yRight;
    yUp = zLook * xRight - xLook * zRight;
    zUp = xLook * yRight - yLook * xRight;
    magUp = GMLReal_sqrt(xUp * xUp + yUp * yUp + zUp * zUp);
    xUp /= magUp;
    yUp /= magUp;
    zUp /= magUp;

    GMLReal x, y, z;
    x = xFrom * xRight + yFrom * yRight + zFrom * zRight;
    y = xFrom * xUp + yFrom * yUp + zFrom * zUp;
    z = xFrom * xLook + yFrom * yLook + zFrom * zLook;

    Matrix4f matrix;
    Matrix4f_identity(&matrix);

    matrix.m[Matrix_getIndex(0, 0)] = xRight;
    matrix.m[Matrix_getIndex(0, 1)] = xUp;
    matrix.m[Matrix_getIndex(0, 2)] = xLook;

    matrix.m[Matrix_getIndex(1, 0)] = yRight;
    matrix.m[Matrix_getIndex(1, 1)] = yUp;
    matrix.m[Matrix_getIndex(1, 2)] = yLook;

    matrix.m[Matrix_getIndex(2, 0)] = zRight;
    matrix.m[Matrix_getIndex(2, 1)] = zUp;
    matrix.m[Matrix_getIndex(2, 2)] = zLook;

    matrix.m[Matrix_getIndex(3, 0)] = -x;
    matrix.m[Matrix_getIndex(3, 1)] = -y;
    matrix.m[Matrix_getIndex(3, 2)] = -z;

    bool toPrevMatrix = argCount == 10;
    GMLArray *destArray = toPrevMatrix ? args[9].array : nullptr;
    if (toPrevMatrix && !rvalueIsMatrix(args[9])) return RValue_makeUndefined();

    if (toPrevMatrix) {
        repeat (16, i) {
            *GMLArray_slot(destArray, i) = RValue_makeReal(matrix.m[i]);
        }
        return RValue_makeArrayWeak(destArray);
    } else {
        return RValue_makeArray(matrixToGml(&matrix));
    }
}

// ===[ RANDOM FUNCTIONS ]===


static RValue builtin_random(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal n = RValue_toReal(args[0]);
    return RValue_makeReal(((GMLReal) rand() / (GMLReal) RAND_MAX) * n);
}

static RValue builtin_random_range(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    GMLReal lo = RValue_toReal(args[0]);
    GMLReal hi = RValue_toReal(args[1]);
    return RValue_makeReal(lo + ((GMLReal) rand() / (GMLReal) RAND_MAX) * (hi - lo));
}

static RValue builtin_irandom(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t n = RValue_toInt32(args[0]);
    if (0 >= n) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) (rand() % (n + 1)));
}

static RValue builtin_irandom_range(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    int32_t lo = RValue_toInt32(args[0]);
    int32_t hi = RValue_toInt32(args[1]);
    if (lo > hi) { int32_t tmp = lo; lo = hi; hi = tmp; }
    int32_t range = hi - lo + 1;
    if (0 >= range) return RValue_makeReal((GMLReal) lo);
    return RValue_makeReal((GMLReal) (lo + rand() % range));
}

static RValue builtin_choose(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t idx = rand() % argCount;
    // Steal ownership: the caller's RValue_free of args[idx] becomes a no-op, and the returned value owns the ref instead.
    RValue val = args[idx];
    if (val.type == RVALUE_STRING && val.string != nullptr && !val.ownsReference) {
        return RValue_makeOwnedString(safeStrdup(val.string));
    }
    args[idx].ownsReference = false;
    return val;
}

static RValue builtin_randomize(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->hasFixedSeed) return RValue_makeUndefined();
    srand((unsigned int) time(nullptr) + (ctx->runner->frameCount * 2654435761u)); // 2654435761u = Knuth's multiplier
    return RValue_makeUndefined();
}

// ===[ ROOM FUNCTIONS ]===

static RValue builtin_game_get_speed(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t type = RValue_toInt32(args[0]);
    GMLReal fps = (GMLReal) ctx->runner->currentRoom->speed;
    // gamespeed_fps = 0, gamespeed_microseconds = 1
    if (type == 0) return RValue_makeReal(fps);
    return RValue_makeReal((GMLReal) 1000000.0 / fps);
}

static RValue builtin_room_exists(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t roomId = RValue_toInt32(args[0]);
    return RValue_makeBool(roomId >= 0 && (uint32_t) roomId < ctx->runner->dataWin->room.count);
}

static RValue builtin_room_get_name(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Room* room = &ctx->dataWin->room.rooms[RValue_toInt32(args[0])];
    return RValue_makeOwnedString(safeStrdup(room->name));
}

static RValue builtin_room_get_info(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t roomId = RValue_toInt32(args[0]);
    if (0 > roomId || (uint32_t) roomId >= ctx->dataWin->room.count) return RValue_makeUndefined();

    bool wantViews = (argCount > 1) ? RValue_toBool(args[1]) : true;
    bool wantInsts = (argCount > 2) ? RValue_toBool(args[2]) : true;
    bool wantLayers = (argCount > 3) ? RValue_toBool(args[3]) : true;
    bool wantLayerEls = (argCount > 4) ? RValue_toBool(args[4]) : true;
    bool wantTilemap = (argCount > 5) ? RValue_toBool(args[5]) : true;

    Room* room = &ctx->dataWin->room.rooms[roomId];
    DataWin_loadRoomPayload(ctx->dataWin, roomId);

    Instance* ret = Runner_createStruct(ctx->runner);
    VM_structSetAndFreeVal(ctx, ret, "width", RValue_makeInt32((int32_t) room->width), -1);
    VM_structSetAndFreeVal(ctx, ret, "height", RValue_makeInt32((int32_t) room->height), -1);
    VM_structSetAndFreeVal(ctx, ret, "persistent", RValue_makeBool(room->persistent), -1);
    VM_structSetAndFreeVal(ctx, ret, "colour", RValue_makeInt32((int32_t) room->backgroundColor), -1);
    VM_structSetAndFreeVal(ctx, ret, "creationCode", RValue_makeInt32(room->creationCodeId), -1);
    VM_structSetAndFreeVal(ctx, ret, "physicsWorld", RValue_makeBool(room->world), -1);
    if (room->world) {
        VM_structSetAndFreeVal(ctx, ret, "physicsGravityX", RValue_makeReal(room->gravityX), -1);
        VM_structSetAndFreeVal(ctx, ret, "physicsGravityY", RValue_makeReal(room->gravityY), -1);
        VM_structSetAndFreeVal(ctx, ret, "physicsPixToMeters", RValue_makeReal(room->metersPerPixel), -1);
    }
    VM_structSetAndFreeVal(ctx, ret, "enableViews", RValue_makeBool((room->flags & 1) != 0), -1);
    VM_structSetAndFreeVal(ctx, ret, "clearDisplayBuffer", RValue_makeBool(true), -1);
    VM_structSetAndFreeVal(ctx, ret, "clearViewportBackground", RValue_makeBool(true), -1);

    if (wantViews && room->views != nullptr) {
        GMLArray* views = GMLArray_create(MAX_VIEWS);
        repeat(MAX_VIEWS, i) {
            RoomView* v = &room->views[i];
            Instance* vs = Runner_createStruct(ctx->runner);
            VM_structSetAndFreeVal(ctx, vs, "visible", RValue_makeBool(v->enabled), -1);
            VM_structSetAndFreeVal(ctx, vs, "xview", RValue_makeInt32(v->viewX), -1);
            VM_structSetAndFreeVal(ctx, vs, "yview", RValue_makeInt32(v->viewY), -1);
            VM_structSetAndFreeVal(ctx, vs, "wview", RValue_makeInt32(v->viewWidth), -1);
            VM_structSetAndFreeVal(ctx, vs, "hview", RValue_makeInt32(v->viewHeight), -1);
            VM_structSetAndFreeVal(ctx, vs, "xport", RValue_makeInt32(v->portX), -1);
            VM_structSetAndFreeVal(ctx, vs, "yport", RValue_makeInt32(v->portY), -1);
            VM_structSetAndFreeVal(ctx, vs, "wport", RValue_makeInt32(v->portWidth), -1);
            VM_structSetAndFreeVal(ctx, vs, "hport", RValue_makeInt32(v->portHeight), -1);
            VM_structSetAndFreeVal(ctx, vs, "hborder", RValue_makeInt32((int32_t) v->borderX), -1);
            VM_structSetAndFreeVal(ctx, vs, "vborder", RValue_makeInt32((int32_t) v->borderY), -1);
            VM_structSetAndFreeVal(ctx, vs, "hspeed", RValue_makeInt32(v->speedX), -1);
            VM_structSetAndFreeVal(ctx, vs, "vspeed", RValue_makeInt32(v->speedY), -1);
            VM_structSetAndFreeVal(ctx, vs, "object", RValue_makeInt32(v->objectId), -1);
            VM_structSetAndFreeVal(ctx, vs, "cameraID", RValue_makeInt32(-1), -1);
            *GMLArray_slot(views, i) = RValue_makeStructAndIncRef(vs);
        }
        VM_structSetAndFreeVal(ctx, ret, "views", RValue_makeArray(views), -1);
    }

    if (wantInsts) {
        int32_t count = (int32_t) room->gameObjectCount;
        GMLArray* insts = GMLArray_create(count > 0 ? count : 1);
        repeat(count, i) {
            RoomGameObject* go = &room->gameObjects[i];
            Instance* is = Runner_createStruct(ctx->runner);
            VM_structSetAndFreeVal(ctx, is, "x", RValue_makeInt32(go->x), -1);
            VM_structSetAndFreeVal(ctx, is, "y", RValue_makeInt32(go->y), -1);
            const char* objName = (go->objectDefinition >= 0 && (uint32_t) go->objectDefinition < ctx->dataWin->objt.count) ? ctx->dataWin->objt.objects[go->objectDefinition].name : "";
            VM_structSetAndFreeVal(ctx, is, "object_index", RValue_makeOwnedString(safeStrdup(objName)), -1);
            VM_structSetAndFreeVal(ctx, is, "id", RValue_makeInt32((int32_t) go->instanceID), -1);
            VM_structSetAndFreeVal(ctx, is, "angle", RValue_makeReal(go->rotation), -1);
            VM_structSetAndFreeVal(ctx, is, "xscale", RValue_makeReal(go->scaleX), -1);
            VM_structSetAndFreeVal(ctx, is, "yscale", RValue_makeReal(go->scaleY), -1);
            VM_structSetAndFreeVal(ctx, is, "image_speed", RValue_makeReal(go->imageSpeed), -1);
            VM_structSetAndFreeVal(ctx, is, "image_index", RValue_makeInt32(go->imageIndex), -1);
            VM_structSetAndFreeVal(ctx, is, "colour", RValue_makeInt32((int32_t) go->color), -1);
            VM_structSetAndFreeVal(ctx, is, "creation_code", RValue_makeInt32(go->creationCode), -1);
            VM_structSetAndFreeVal(ctx, is, "pre_creation_code", RValue_makeInt32(go->preCreateCode), -1);
            *GMLArray_slot(insts, i) = RValue_makeStructAndIncRef(is);
        }
        VM_structSetAndFreeVal(ctx, ret, "instances", RValue_makeArray(insts), -1);
    }

    if (wantLayers && room->layers != nullptr) {
        int32_t count = (int32_t) room->layerCount;
        GMLArray* layers = GMLArray_create(count > 0 ? count : 1);
        repeat(count, i) {
            RoomLayer* lay = &room->layers[i];
            Instance* ls = Runner_createStruct(ctx->runner);
            VM_structSetAndFreeVal(ctx, ls, "name", RValue_makeOwnedString(safeStrdup(lay->name != nullptr ? lay->name : "")), -1);
            VM_structSetAndFreeVal(ctx, ls, "id", RValue_makeInt32((int32_t) lay->id), -1);
            VM_structSetAndFreeVal(ctx, ls, "type", RValue_makeInt32((int32_t) lay->type), -1);
            VM_structSetAndFreeVal(ctx, ls, "depth", RValue_makeInt32(lay->depth), -1);
            VM_structSetAndFreeVal(ctx, ls, "xoffset", RValue_makeReal(lay->xOffset), -1);
            VM_structSetAndFreeVal(ctx, ls, "yoffset", RValue_makeReal(lay->yOffset), -1);
            VM_structSetAndFreeVal(ctx, ls, "hspeed", RValue_makeReal(lay->hSpeed), -1);
            VM_structSetAndFreeVal(ctx, ls, "vspeed", RValue_makeReal(lay->vSpeed), -1);
            VM_structSetAndFreeVal(ctx, ls, "visible", RValue_makeBool(lay->visible), -1);

            if (wantLayerEls) {
                GMLArray* elements = nullptr;
                switch ((RoomLayerType) lay->type) {
                    case RoomLayerType_Background: {
                        elements = GMLArray_create(1);
                        GMLArray_growTo(elements, 1);
                        Instance* es = Runner_createStruct(ctx->runner);
                        RoomLayerBackgroundData* bg = lay->backgroundData;
                        VM_structSetAndFreeVal(ctx, es, "type", RValue_makeInt32((int32_t) lay->type), -1);
                        if (bg != nullptr) {
                            VM_structSetAndFreeVal(ctx, es, "visible", RValue_makeBool(bg->visible), -1);
                            VM_structSetAndFreeVal(ctx, es, "foreground", RValue_makeBool(bg->foreground), -1);
                            VM_structSetAndFreeVal(ctx, es, "sprite_index", RValue_makeInt32(bg->spriteIndex), -1);
                            VM_structSetAndFreeVal(ctx, es, "htiled", RValue_makeBool(bg->hTiled), -1);
                            VM_structSetAndFreeVal(ctx, es, "vtiled", RValue_makeBool(bg->vTiled), -1);
                            VM_structSetAndFreeVal(ctx, es, "stretch", RValue_makeBool(bg->stretch), -1);
                            VM_structSetAndFreeVal(ctx, es, "image_speed", RValue_makeReal(bg->animSpeed), -1);
                            VM_structSetAndFreeVal(ctx, es, "image_index", RValue_makeReal(bg->firstFrame), -1);
                            VM_structSetAndFreeVal(ctx, es, "speed_type", RValue_makeInt32((int32_t) bg->animSpeedType), -1);
                        }
                        *GMLArray_slot(elements, 0) = RValue_makeStructAndIncRef(es);
                        break;
                    }
                    case RoomLayerType_Instances: {
                        RoomLayerInstancesData* id = lay->instancesData;
                        int32_t ic = (id != nullptr) ? (int32_t) id->instanceCount : 0;
                        elements = GMLArray_create(ic > 0 ? ic : 1);
                        if (ic > 0) GMLArray_growTo(elements, ic);
                        for (int32_t j = 0; ic > j; j++) {
                            Instance* es = Runner_createStruct(ctx->runner);
                            VM_structSetAndFreeVal(ctx, es, "type", RValue_makeInt32((int32_t) lay->type), -1);
                            VM_structSetAndFreeVal(ctx, es, "inst_id", RValue_makeInt32((int32_t) id->instanceIds[j]), -1);
                            *GMLArray_slot(elements, j) = RValue_makeStructAndIncRef(es);
                        }
                        break;
                    }
                    case RoomLayerType_Tiles: {
                        elements = GMLArray_create(1);
                        GMLArray_growTo(elements, 1);
                        Instance* es = Runner_createStruct(ctx->runner);
                        RoomLayerTilesData* td = lay->tilesData;
                        VM_structSetAndFreeVal(ctx, es, "type", RValue_makeInt32((int32_t) lay->type), -1);
                        VM_structSetAndFreeVal(ctx, es, "x", RValue_makeInt32(0), -1);
                        VM_structSetAndFreeVal(ctx, es, "y", RValue_makeInt32(0), -1);
                        if (td != nullptr) {
                            VM_structSetAndFreeVal(ctx, es, "width", RValue_makeInt32((int32_t) td->tilesX), -1);
                            VM_structSetAndFreeVal(ctx, es, "height", RValue_makeInt32((int32_t) td->tilesY), -1);
                            VM_structSetAndFreeVal(ctx, es, "tileset_index", RValue_makeInt32(td->backgroundIndex), -1);
                            if (wantTilemap && td->tileData != nullptr) {
                                int32_t total = (int32_t) (td->tilesX * td->tilesY);
                                GMLArray* tiles = GMLArray_create(total > 0 ? total : 1);
                                if (total > 0) GMLArray_growTo(tiles, total);
                                for (int32_t k = 0; total > k; k++) {
                                    *GMLArray_slot(tiles, k) = RValue_makeInt32((int32_t) td->tileData[k]);
                                }
                                VM_structSetAndFreeVal(ctx, es, "tiles", RValue_makeArray(tiles), -1);
                            }
                        }
                        *GMLArray_slot(elements, 0) = RValue_makeStructAndIncRef(es);
                        break;
                    }
                    default:
                        // Asset/Path/Effect layers: emit an empty element list. Filling these out matches the HTML5 runner but isn't required for room_goto navigation.
                        elements = GMLArray_create(1);
                        break;
                }
                if (elements != nullptr) VM_structSetAndFreeVal(ctx, ls, "elements", RValue_makeArray(elements), -1);
            }

            *GMLArray_slot(layers, i) = RValue_makeStructAndIncRef(ls);
        }
        VM_structSetAndFreeVal(ctx, ret, "layers", RValue_makeArray(layers), -1);
    }

    return RValue_makeStructAndIncRef(ret);
}

static RValue builtin_room_goto_next(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_goto_next called but no runner!");

    int32_t nextPos = runner->currentRoomOrderPosition + 1;
    if ((int32_t) runner->dataWin->gen8.roomOrderCount > nextPos) {
        runner->pendingRoom = runner->dataWin->gen8.roomOrder[nextPos];
    } else {
        fprintf(stderr, "VM: room_goto_next - already at last room!\n");
    }
    return RValue_makeUndefined();
}

static RValue builtin_room_goto_previous(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_goto_previous called but no runner!");

    int32_t previousPos = runner->currentRoomOrderPosition - 1;
    if (previousPos >= 0) {
        runner->pendingRoom = runner->dataWin->gen8.roomOrder[previousPos];
    } else {
        fprintf(stderr, "VM: room_goto_previous - already at first room!\n");
    }
    return RValue_makeUndefined();
}

static RValue builtin_room_goto(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_goto called but no runner!");
    runner->pendingRoom = RValue_toInt32(args[0]);
    return RValue_makeUndefined();
}

static RValue builtin_room_restart(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_restart called but no runner!");
    runner->pendingRoom = runner->currentRoomIndex;
    return RValue_makeUndefined();
}

static RValue builtin_room_next(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_next called but no runner!");
    int32_t roomId = RValue_toInt32(args[0]);
    DataWin* dw = runner->dataWin;
    repeat(dw->gen8.roomOrderCount, i) {
        if (dw->gen8.roomOrder[i] == roomId && dw->gen8.roomOrderCount > i + 1) {
            return RValue_makeReal(dw->gen8.roomOrder[i + 1]);
        }
    }
    return RValue_makeReal(-1);
}

static RValue builtin_room_previous(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_previous called but no runner!");
    int32_t roomId = RValue_toInt32(args[0]);
    DataWin* dw = runner->dataWin;
    repeat(dw->gen8.roomOrderCount, i) {
        if (dw->gen8.roomOrder[i] == roomId && i > 0) {
            return RValue_makeReal(dw->gen8.roomOrder[i - 1]);
        }
    }
    return RValue_makeReal(-1);
}

static RValue builtin_room_set_persistent(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t roomId = RValue_toInt32(args[0]);
    bool persistent = RValue_toBool(args[1]);
    // The HTML5 room_set_persistent does do this (it checks if the room is null)
    if (0 > roomId || (uint32_t) roomId >= ctx->runner->dataWin->room.count) return RValue_makeUndefined();
    ctx->runner->dataWin->room.rooms[roomId].persistent = persistent;

    return RValue_makeUndefined();
}

// GMS2 camera compatibility - we treat view index as camera ID
static RValue builtin_view_get_camera(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal(runner->views[viewIndex].cameraId);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_view_get_visible(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeBool(runner->views[viewIndex].enabled);
    }
    return RValue_makeBool(false);
}

static RValue builtin_view_get_xport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal(runner->views[viewIndex].portX);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_view_get_yport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal(runner->views[viewIndex].portY);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_view_get_wport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal(runner->views[viewIndex].portWidth);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_view_get_hport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal(runner->views[viewIndex].portHeight);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_view_get_surface_id(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal(runner->views[viewIndex].surfaceId);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_view_set_visible(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        runner->views[viewIndex].enabled = RValue_toBool(args[1]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_view_set_xport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        runner->views[viewIndex].portX = RValue_toInt32(args[1]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_view_set_yport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        runner->views[viewIndex].portY = RValue_toInt32(args[1]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_view_set_wport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        runner->views[viewIndex].portWidth = RValue_toInt32(args[1]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_view_set_hport(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        runner->views[viewIndex].portHeight = RValue_toInt32(args[1]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_view_set_surface_id(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        runner->views[viewIndex].surfaceId = RValue_toInt32(args[1]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_camera_get_view_x(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->viewX);
    return RValue_makeReal(-1);
}

static RValue builtin_camera_get_view_y(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->viewY);
    return RValue_makeReal(-1);
}

static RValue builtin_camera_get_view_width(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->viewWidth);
    return RValue_makeReal(-1);
}

static RValue builtin_camera_get_view_height(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->viewHeight);
    return RValue_makeReal(-1);
}

static RValue builtin_camera_set_view_pos(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) {
        camera->viewX = RValue_toInt32(args[1]);
        camera->viewY = RValue_toInt32(args[2]);
    }
    return RValue_makeUndefined();
}

// TODO: We don't support the full matrix-based render pipeline yet, update this later!
static RValue builtin_camera_set_view_mat(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera == nullptr || !rvalueIsMatrix(args[1])) return RValue_makeUndefined();
    Matrix4f m;
    matrixFromGml(&m, args[1].array);
    // For an axis-aligned 2D camera the view-matrix translation encodes -(camera center).
    camera->viewMatCenterX = (int32_t) lround(-m.m[Matrix_getIndex(3, 0)]);
    camera->viewMatCenterY = (int32_t) lround(-m.m[Matrix_getIndex(3, 1)]);
    camera->viewX = camera->viewMatCenterX - camera->viewWidth / 2;
    camera->viewY = camera->viewMatCenterY - camera->viewHeight / 2;
    return RValue_makeUndefined();
}

static RValue builtin_camera_set_proj_mat(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera == nullptr || !rvalueIsMatrix(args[1])) return RValue_makeUndefined();
    Matrix4f m;
    matrixFromGml(&m, args[1].array);
    // Orthographic projection: m[0,0] = 2/width, m[1,1] = 2/height.
    GMLReal m00 = m.m[Matrix_getIndex(0, 0)];
    GMLReal m11 = m.m[Matrix_getIndex(1, 1)];
    if (m00 != 0.0) camera->viewWidth = (int32_t) lround(GMLReal_fabs(2.0 / m00));
    if (m11 != 0.0) camera->viewHeight = (int32_t) lround(GMLReal_fabs(2.0 / m11));
    camera->viewX = camera->viewMatCenterX - camera->viewWidth / 2;
    camera->viewY = camera->viewMatCenterY - camera->viewHeight / 2;
    return RValue_makeUndefined();
}

static RValue builtin_camera_get_view_target(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->objectId);
    return RValue_makeReal(-1);
}

static RValue builtin_camera_set_view_target(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) camera->objectId = RValue_toInt32(args[1]);
    return RValue_makeUndefined();
}

static RValue cameraGetViewBorder(VMContext* ctx, RValue* args, int32_t argCount, bool wantY) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal((wantY ? camera->borderY : camera->borderX));
    return RValue_makeReal(-1);
}

static RValue builtin_camera_get_view_border_x(VMContext* ctx, RValue* args, int32_t argCount) {
    return cameraGetViewBorder(ctx, args, argCount, false);
}

static RValue builtin_camera_get_view_border_y(VMContext* ctx, RValue* args, int32_t argCount) {
    return cameraGetViewBorder(ctx, args, argCount, true);
}

static RValue builtin_camera_set_view_border(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) {
        camera->borderX = (uint32_t) RValue_toInt32(args[1]);
        camera->borderY = (uint32_t) RValue_toInt32(args[2]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_camera_set_view_size(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) {
        camera->viewWidth = RValue_toInt32(args[1]);
        camera->viewHeight = RValue_toInt32(args[2]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_camera_set_view_speed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) {
        camera->speedX = RValue_toInt32(args[1]);
        camera->speedY = RValue_toInt32(args[2]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_camera_set_view_angle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) camera->viewAngle = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_camera_get_view_angle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal((GMLReal) camera->viewAngle);
    return RValue_makeReal(0.0);
}

static RValue builtin_camera_get_view_speed_x(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->speedX);
    return RValue_makeReal(-1);
}

static RValue builtin_camera_get_view_speed_y(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) return RValue_makeReal(camera->speedY);
    return RValue_makeReal(-1);
}

// Allocates a user camera in the first free userCameras slot. Returns its logical camera id, or -1 if the pool is full.
static int32_t allocUserCamera(Runner* runner) {
    repeat(MAX_USER_CAMERAS, slot) {
        GMLCamera* camera = &runner->userCameras[slot];
        if (!camera->allocated) {
            memset(camera, 0, sizeof(*camera));
            camera->allocated = true;
            camera->objectId = -1;
            return MAX_DEFAULT_ROOM_CAMERAS + slot;
        }
    }
    return -1;
}

static RValue builtin_camera_create(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeReal(allocUserCamera(runner));
}

static RValue builtin_camera_create_view(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = allocUserCamera(runner);
    if (0 > id) return RValue_makeReal(-1);
    GMLCamera* camera = Runner_getCameraById(runner, id);
    // camera_create_view(room_x, room_y, room_w, room_h, [angle, object, x_speed, y_speed, x_border, y_border])
    if (argCount > 0) camera->viewX = RValue_toInt32(args[0]);
    if (argCount > 1) camera->viewY = RValue_toInt32(args[1]);
    if (argCount > 2) camera->viewWidth = RValue_toInt32(args[2]);
    if (argCount > 3) camera->viewHeight = RValue_toInt32(args[3]);
    if (argCount > 4) camera->viewAngle = (float) RValue_toReal(args[4]);
    if (argCount > 5) camera->objectId = RValue_toInt32(args[5]);
    if (argCount > 6) camera->speedX = RValue_toInt32(args[6]);
    if (argCount > 7) camera->speedY = RValue_toInt32(args[7]);
    if (argCount > 8) camera->borderX = (uint32_t) RValue_toInt32(args[8]);
    if (argCount > 9) camera->borderY = (uint32_t) RValue_toInt32(args[9]);
    return RValue_makeReal(id);
}

static RValue builtin_camera_destroy(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    // Only user cameras (logical ids past the reserved default-camera range) can be destroyed; default room cameras are not.
    if (id < MAX_DEFAULT_ROOM_CAMERAS || MAX_CAMERAS <= id) return RValue_makeUndefined();
    runner->userCameras[id - MAX_DEFAULT_ROOM_CAMERAS].allocated = false;
    // Detach any view that referenced this camera so it doesn't dangle.
    repeat(MAX_VIEWS, vi) {
        if (runner->views[vi].cameraId == id) runner->views[vi].cameraId = -1;
    }
    return RValue_makeUndefined();
}

static RValue builtin_view_set_camera(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t viewIndex = RValue_toInt32(args[0]);
    int32_t cameraId = RValue_toInt32(args[1]);
    if (viewIndex < 0 || MAX_VIEWS <= viewIndex) return RValue_makeUndefined();
    // Accept -1 (detach) or any allocated camera id.
    if (cameraId == -1 || Runner_getCameraById(runner, cameraId) != nullptr) {
        runner->views[viewIndex].cameraId = cameraId;
    }
    return RValue_makeUndefined();
}

// The camera of the view currently being drawn (we have no camera_apply active-camera register, so this tracks view_current).
static RValue builtin_camera_get_active(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->viewCurrent >= 0 && MAX_VIEWS > runner->viewCurrent) {
        return RValue_makeReal(runner->views[runner->viewCurrent].cameraId);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_camera_get_default(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeReal(runner->views[0].cameraId);
}

// camera_apply(camera): makes the camera's projection active on the current render target, so subsequent draws use that camera's view instead of the target's default projection.
// Builds the world->clip matrix from the camera's scalars (custom view/proj matrices are alater stage).
// The viewport is left untouched.
static RValue builtin_camera_apply(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLCamera* camera = Runner_getCameraById(runner, RValue_toInt32(args[0]));
    if (camera != nullptr) {
        Matrix4f worldToClip;
        Matrix4f_viewProjection(&worldToClip, (float) camera->viewX, (float) camera->viewY, (float) camera->viewWidth, (float) camera->viewHeight, camera->viewAngle);
        runner->renderer->vtable->applyProjection(runner->renderer, &worldToClip);
    }
    return RValue_makeUndefined();
}

// ===[ VARIABLE FUNCTIONS ]===

#ifdef ENABLE_VM_TRACING
static const char* variableTraceObjectName(VMContext* ctx, Instance* inst) {
    if (0 > inst->objectIndex) return "<global_scope>";
    return ctx->dataWin->objt.objects[inst->objectIndex].name;
}
#endif

static RValue builtin_variable_global_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeReal(0.0);
    const char* name = args[0].string;
    ptrdiff_t idx = shgeti(ctx->globalVarNameMap, (char*) name);
    if (0 > idx) return RValue_makeReal(0.0);
    int32_t varID = ctx->globalVarNameMap[idx].value;
    if (ctx->globalVarCount > (uint32_t) varID && ctx->globalVars[varID].type != RVALUE_UNDEFINED) {
        return RValue_makeReal(1.0);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_variable_global_get(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeUndefined();
    const char* name = args[0].string;
    ptrdiff_t idx = shgeti(ctx->globalVarNameMap, (char*) name);
    if (0 > idx) return RValue_makeUndefined();
    int32_t varID = ctx->globalVarNameMap[idx].value;
    if (ctx->globalVarCount > (uint32_t) varID) {
        RValue val = ctx->globalVars[varID];
#ifdef ENABLE_VM_TRACING
        VM_checkIfVariableShouldBeTracedAndLog(ctx, "global", nullptr, name, val, false, -1, -1, " (variable_global_get)");
#endif
        // Duplicate owned strings
        if (val.type == RVALUE_STRING && val.ownsReference && val.string != nullptr) {
            return RValue_makeOwnedString(safeStrdup(val.string));
        }
        // Return a weak view: the global slot retains ownership. The caller's Pop will incRef into the destination slot.
        val.ownsReference = false;
        return val;
    }
    return RValue_makeUndefined();
}

static RValue builtin_variable_global_set(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[0].type != RVALUE_STRING) return RValue_makeUndefined();
    const char* name = args[0].string;
    ptrdiff_t idx = shgeti(ctx->globalVarNameMap, (char*) name);
    if (0 > idx) return RValue_makeUndefined();
    int32_t varID = ctx->globalVarNameMap[idx].value;
    if (ctx->globalVarCount > (uint32_t) varID) {
#ifdef ENABLE_VM_TRACING
        VM_checkIfVariableShouldBeTracedAndLog(ctx, "global", nullptr, name, args[1], true, -1, -1, " (variable_global_set)");
#endif
        RValue_free(&ctx->globalVars[varID]);
        ctx->globalVars[varID] = RValue_makeIndependent(args[1]);
    }
    return RValue_makeUndefined();
}

// ===[ VARIABLE_INSTANCE ]===

static void variableInstanceSetOn(VMContext* ctx, Instance* target, const char* name, RValue val, MAYBE_UNUSED const char* originBuiltin) {
#ifdef ENABLE_VM_TRACING
    char additional[48];
    snprintf(additional, sizeof(additional), " (%s)", originBuiltin);
    VM_checkIfVariableShouldBeTracedAndLog(ctx, variableTraceObjectName(ctx, target), "self", name, val, true, -1, target->instanceId, additional);
#endif
    int16_t builtinId = VMBuiltins_resolveBuiltinVarId(name);
    if (builtinId != BUILTIN_VAR_UNKNOWN) {
        VMBuiltins_setVariable(ctx, target, builtinId, name, val, -1);
        return;
    }

    // Lookup varID by name from VARI (self scope)
    ptrdiff_t slot = shgeti(ctx->selfVarNameMap, (char*) name);
    if (0 > slot) {
        // Not on the slot, register manually
        int32_t dynamicallyAllocatedVarID = VM_getOrAllocateSelfVarID(ctx, name);
        Instance_setSelfVar(target, dynamicallyAllocatedVarID, val);
        return;
    }
    Instance_setSelfVar(target, ctx->selfVarNameMap[slot].value, val);
}

static RValue variableInstanceGetOn(VMContext* ctx, Instance* target, const char* name, MAYBE_UNUSED const char* originBuiltin) {
    int16_t builtinId = VMBuiltins_resolveBuiltinVarId(name);
    if (builtinId != BUILTIN_VAR_UNKNOWN) {
        RValue val = VMBuiltins_getVariable(ctx, target, builtinId, name, -1);
#ifdef ENABLE_VM_TRACING
        char additional[48];
        snprintf(additional, sizeof(additional), " (%s, builtin)", originBuiltin);
        VM_checkIfVariableShouldBeTracedAndLog(ctx, variableTraceObjectName(ctx, target), "self", name, val, false, -1, target->instanceId, additional);
#endif
        // Duplicate RValue so caller-owned args cleanup does not affect it
        if (!val.ownsReference) {
            return RValue_makeIndependent(val);
        }
        return val;
    }
    ptrdiff_t slot = shgeti(ctx->selfVarNameMap, (char*) name);
    if (0 > slot) return RValue_makeUndefined();
    RValue val = Instance_getSelfVar(target, ctx->selfVarNameMap[slot].value);
#ifdef ENABLE_VM_TRACING
    char additional[48];
    snprintf(additional, sizeof(additional), " (%s)", originBuiltin);
    VM_checkIfVariableShouldBeTracedAndLog(ctx, variableTraceObjectName(ctx, target), "self", name, val, false, -1, target->instanceId, additional);
#endif
    return RValue_makeIndependent(val);
}

static inline bool variableScopedMatches(Instance* inst, bool structOnly) {
    return inst->active && (!structOnly || inst->objectIndex == STRUCT_OBJECT_INDEX);
}

static bool variableInstanceExistsOn(VMContext* ctx, Instance* target, const char* name) {
    if (VMBuiltins_resolveBuiltinVarId(name) != BUILTIN_VAR_UNKNOWN) return true;
    ptrdiff_t slot = shgeti(ctx->selfVarNameMap, (char*) name);
    if (0 > slot) return false;
    return IntRValueHashMap_contains(&target->selfVars, ctx->selfVarNameMap[slot].value);
}

static RValue variableScopedGet(VMContext* ctx, int32_t id, const char* name, bool structOnly, const char* originBuiltin) {
    Runner* runner = ctx->runner;

    if (id >= 100000) {
        Instance* inst = hmget(runner->instancesById, id);
        if (inst != nullptr && variableScopedMatches(inst, structOnly)) return variableInstanceGetOn(ctx, inst, name, originBuiltin);
        return RValue_makeUndefined();
    }

    // Object index: return value from first matching active instance.
    int32_t snapBase = Runner_pushInstancesOfObject(runner, id);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    RValue result = RValue_makeUndefined();
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (variableScopedMatches(inst, structOnly)) {
            result = variableInstanceGetOn(ctx, inst, name, originBuiltin);
            break;
        }
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return result;
}

static void variableScopedSet(VMContext* ctx, int32_t id, const char* name, RValue val, bool structOnly, const char* originBuiltin) {
    Runner* runner = ctx->runner;

    if (id >= 100000) {
        Instance* inst = hmget(runner->instancesById, id);
        if (inst != nullptr && variableScopedMatches(inst, structOnly)) variableInstanceSetOn(ctx, inst, name, val, originBuiltin);
        return;
    }

    // Object index: set on all matching active instances (including descendants). The setter can run user code, so iterate a snapshot.
    int32_t snapBase = Runner_pushInstancesOfObject(runner, id);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (variableScopedMatches(inst, structOnly)) variableInstanceSetOn(ctx, inst, name, val, originBuiltin);
    }
    Runner_popInstanceSnapshot(runner, snapBase);
}

static bool variableScopedExists(VMContext* ctx, int32_t id, const char* name, bool structOnly) {
    Runner* runner = ctx->runner;

    if (id >= 100000) {
        Instance* inst = hmget(runner->instancesById, id);
        if (inst != nullptr && variableScopedMatches(inst, structOnly)) return variableInstanceExistsOn(ctx, inst, name);
        return false;
    }

    int32_t snapBase = Runner_pushInstancesOfObject(runner, id);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    bool result = false;
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (variableScopedMatches(inst, structOnly)) {
            result = variableInstanceExistsOn(ctx, inst, name);
            break;
        }
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return result;
}

static RValue builtin_variable_instance_get(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[1].type != RVALUE_STRING) return RValue_makeUndefined();
    return variableScopedGet(ctx, RValue_toInt32(args[0]), args[1].string, false, "variable_instance_get");
}

static RValue builtin_variable_instance_set(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount || args[1].type != RVALUE_STRING) return RValue_makeUndefined();
    variableScopedSet(ctx, RValue_toInt32(args[0]), args[1].string, args[2], false, "variable_instance_set");
    return RValue_makeUndefined();
}

static RValue builtin_variable_instance_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[1].type != RVALUE_STRING) return RValue_makeBool(false);
    return RValue_makeBool(variableScopedExists(ctx, RValue_toInt32(args[0]), args[1].string, false));
}

static RValue builtin_variable_struct_get(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[1].type != RVALUE_STRING) return RValue_makeUndefined();
    return variableScopedGet(ctx, RValue_toInt32(args[0]), args[1].string, true, "variable_struct_get");
}

static RValue builtin_variable_struct_set(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount || args[1].type != RVALUE_STRING) return RValue_makeUndefined();
    // We can't use VM_structSetAndFreeVal directly here because we DO NOT resolve builtin variables from VM_structSetAndFreeVal
    variableScopedSet(ctx, RValue_toInt32(args[0]), args[1].string, args[2], true, "variable_struct_set");
    return RValue_makeUndefined();
}

static RValue builtin_variable_struct_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[1].type != RVALUE_STRING) return RValue_makeBool(false);
    return RValue_makeBool(variableScopedExists(ctx, RValue_toInt32(args[0]), args[1].string, true));
}

// ===[ METHOD ]===

#if IS_WAD17_OR_HIGHER_ENABLED
static RValue builtin_method(VMContext* ctx, MAYBE_UNUSED RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t boundInstance = RValue_toInt32(args[0]);
    int32_t rawArg = RValue_toInt32(args[1]);

    // In GMS2 BC17+, function references are pushed via `Push.i <funcIdx>` where funcIdx is an index into the FUNC chunk (patched in by patchReferenceOperands). Resolve funcIdx -> codeIndex via function name lookup (same flow as Call.i).
    int32_t codeIndex = rawArg;
    if (rawArg >= 0 && (uint32_t) rawArg < ctx->dataWin->func.functionCount) {
        const char* funcName = ctx->dataWin->func.functions[rawArg].name;
        if (funcName != nullptr) {
            ptrdiff_t idx = shgeti(ctx->codeIndexByName, (char*) funcName);
            if (idx >= 0) {
                codeIndex = ctx->codeIndexByName[idx].value;
            }
        }
    }

    // If binding to current self (-1), capture the actual instance ID
    if (boundInstance == -1 && ctx->currentInstance != nullptr) {
        boundInstance = ctx->currentInstance->instanceId;
    }

    return RValue_makeMethodFromCodeIndexAndInstanceId(codeIndex, boundInstance);
}
#endif

// ===[ SCRIPT EXECUTE ]===

static RValue builtin_script_execute(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();

    int32_t codeId;

#if IS_WAD17_OR_HIGHER_ENABLED
    if (args[0].type == RVALUE_METHOD) {
        // If it is a method value, we'll need to extract code index directly
        codeId = args[0].method->codeIndex;
    } else
#endif
    {
        // Numeric script/function index
        int32_t rawArg = RValue_toInt32(args[0]);
        codeId = -1;

#if IS_WAD17_OR_HIGHER_ENABLED
        // In GMS 2.3+, "scriptName" in source code is compiled as a FUNC-table index (same as builtin_method). Resolve funcIdx -> codeIndex via codeIndexByName
        if (DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 0, 0) && rawArg >= 0 && ctx->dataWin->func.functionCount > (uint32_t) rawArg) {
            const char* funcName = ctx->dataWin->func.functions[rawArg].name;
            if (funcName != nullptr) {
                ptrdiff_t idx = shgeti(ctx->codeIndexByName, (char*) funcName);
                if (idx >= 0) {
                    codeId = ctx->codeIndexByName[idx].value;
                } else {
                    // Not a user script - might be a builtin function reference
                    ptrdiff_t bidx = shgeti(ctx->builtinMap, (char*) funcName);
                    if (bidx >= 0) {
                        BuiltinFunc bf = ctx->builtinMap[bidx].value;
                        RValue* scriptArgs = (argCount > 1) ? &args[1] : nullptr;
                        return bf(ctx, scriptArgs, argCount - 1);
                    }
                }
            }
        }
#endif

        // Fallback: treat as SCPT index (BC16 and earlier, or when FUNC lookup failed)
        if (0 > codeId) {
            if (0 > rawArg || (uint32_t) rawArg >= ctx->dataWin->scpt.count) {
                fprintf(stderr, "VM: script_execute - invalid script index %d\n", rawArg);
                return RValue_makeUndefined();
            }
            codeId = ctx->dataWin->scpt.scripts[rawArg].codeId;
        }
    }

    if (0 > codeId || ctx->dataWin->code.count <= (uint32_t) codeId) {
        fprintf(stderr, "VM: script_execute - invalid codeId %d\n", codeId);
        return RValue_makeUndefined();
    }

    // Pass remaining args (skip the script index)
    RValue* scriptArgs = (argCount > 1) ? &args[1] : nullptr;
    int32_t scriptArgCount = argCount - 1;

    // If the method has a bound instance, temporarily swap currentInstance
    Instance* savedInstance = ctx->currentInstance;
#if IS_WAD17_OR_HIGHER_ENABLED
    if (args[0].type == RVALUE_METHOD && args[0].method->boundInstanceId >= 0) {
        Runner* runner = ctx->runner;
        Instance* bound = hmget(runner->instancesById, args[0].method->boundInstanceId);
        if (bound != nullptr) ctx->currentInstance = bound;
    }
#endif

    RValue result = VM_callCodeIndex(ctx, codeId, scriptArgs, scriptArgCount);

    ctx->currentInstance = savedInstance;
    return result;
}

// ===[ OS FUNCTIONS ]===

static RValue builtin_os_get_language(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeOwnedString(safeStrdup("en"));
}

static RValue builtin_os_get_region(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeOwnedString(safeStrdup("US"));
}

STUB_RETURN_FALSE(os_is_paused);

// ===[ XBOX ONE FUNCTIONS ]===

// xboxone_show_account_picker(pad_index, flags): shows the Xbox account picker (async).
static RValue builtin_xboxone_show_account_picker(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t asyncId = runner->xboxAsyncIdCounter++;
    runner->xboxAccountPickerPendingId = asyncId;
    runner->xboxAccountPickerPadIndex = (argCount > 0) ? (int32_t) RValue_toReal(args[0]) : 0;
    return RValue_makeReal((GMLReal) asyncId);
}

STUB_RETURN_TRUE(xboxone_user_is_signed_in);
STUB_RETURN_FALSE(xboxone_is_suspending);
STUB_RETURN_FALSE(xboxone_is_constrained);
STUB_RETURN_ZERO(xboxone_suspend);
STUB_RETURN_ZERO(xboxone_set_savedata_user);
STUB_RETURN_ZERO(xboxone_stats_add_user);
STUB_RETURN_ZERO(xboxone_achievements_set_progress);

static RValue builtin_environment_get_variable(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    char* name = RValue_toString(args[0]);
    if (name == nullptr) return RValue_makeOwnedString(safeStrdup(""));

    const char* value = getenv(name);
    free(name);

    if (value == nullptr) {
        return RValue_makeOwnedString(safeStrdup(""));
    }

    return RValue_makeOwnedString(safeStrdup(value));
}

// ===[ DS_MAP BUILTIN FUNCTIONS ]===

static inline ptrdiff_t getValueIndexInMap(DsMapEntry** mapPtr, RValue keyRvalue) {
    ptrdiff_t idx;
    if (keyRvalue.type == RVALUE_STRING && keyRvalue.string != nullptr) {
        // Fast path: No need to convert the RValue to a string if it is already a string
        idx = shgeti(*mapPtr, keyRvalue.string);
    } else {
        char* key = RValue_toString(keyRvalue);
        idx = shgeti(*mapPtr, key);
        free(key);
    }

    return idx;
}

static RValue builtin_ds_map_create(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeReal((GMLReal) dsMapCreate(runner));
}

static RValue builtin_ds_map_add(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    char* key = RValue_toString(args[1]);

    // Only add if key doesn't exist
    bool exists = shgeti(*mapPtr, key) != -1;

    if (exists) {
        free(key); // Key already exists, we didn't insert it
    } else {
        shput(*mapPtr, key, RValue_makeIndependent(args[2]));
    }

    return RValue_makeUndefined();
}

static RValue dsMapSetCommon(VMContext* ctx, RValue* args, int32_t argCount, bool returnPassedValue, bool returnCurrentOrNewValue) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    char* key = RValue_toString(args[1]);

    ptrdiff_t existingKeyIndex = shgeti(*mapPtr, key);

    RValue ret;
    if (returnCurrentOrNewValue) {
        if (existingKeyIndex != -1) {
            // We are going to steal the ownership :3
            ret = (*mapPtr)[existingKeyIndex].value;
        } else {
            ret = RValue_makeIndependent(args[2]);
        }
    } else {
        if (existingKeyIndex != -1) {
            // If it already exists, we'll get the current value and free it
            RValue_free(&(*mapPtr)[existingKeyIndex].value);
        }
    }

    shput(*mapPtr, key, RValue_makeIndependent(args[2]));

    if (existingKeyIndex != -1) {
        // If it already existed, then shput still owns the old key
        // So we'll need to free the created key
        free(key);
    }

    if (returnCurrentOrNewValue) {
        return ret;
    } else if (returnPassedValue) {
        return RValue_makeIndependent(args[2]);
    } else {
        return RValue_makeUndefined();
    }
}

static RValue builtin_ds_map_set(VMContext* ctx, RValue* args, int32_t argCount) {
    return dsMapSetCommon(ctx, args, argCount, false, false);
}

// This is an undocumented ds_map function, see GameMaker-HTML5's ds_map.js
// It is essentially the ds_map_set, but it returns the passed value
static RValue builtin_ds_map_set_pre(VMContext* ctx, RValue* args, int32_t argCount) {
    return dsMapSetCommon(ctx, args, argCount, true, false);
}

// This is an undocumented ds_map function, see GameMaker-HTML5's ds_map.js
// It is essentially the ds_map_set but it returns the OLD value (i'm old!), if not present then it returns the new value
static RValue builtin_ds_map_set_post(VMContext* ctx, RValue* args, int32_t argCount) {
    return dsMapSetCommon(ctx, args, argCount, false, true);
}

static RValue builtin_ds_map_replace(VMContext* ctx, RValue* args, int32_t argCount) {
    // ds_map_replace is the same as ds_map_set in GMS 1.4
    return builtin_ds_map_set(ctx, args, argCount);
}

static RValue builtin_ds_map_find_value(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    ptrdiff_t idx = getValueIndexInMap(mapPtr, args[1]);

    if (0 > idx) return RValue_makeUndefined();
    RValue val = (*mapPtr)[idx].value;
    if (val.type == RVALUE_STRING && val.string != nullptr) {
        return RValue_makeOwnedString(safeStrdup(val.string));
    }
    // Return a weak view: the map retains ownership. The caller's Pop will incRef into the destination slot.
    val.ownsReference = false;
    return val;
}

static RValue builtin_ds_map_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeReal(0.0);

    ptrdiff_t idx = getValueIndexInMap(mapPtr, args[1]);

    return RValue_makeReal(idx >= 0 ? 1.0 : 0.0);
}

static RValue builtin_ds_map_find_first(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr || shlen(*mapPtr) == 0) return RValue_makeUndefined();
    return RValue_makeOwnedString(safeStrdup((*mapPtr)[0].key));
}

static RValue builtin_ds_map_find_next(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    ptrdiff_t idx = getValueIndexInMap(mapPtr, args[1]);
    if (0 > idx || idx + 1 >= shlen(*mapPtr)) return RValue_makeUndefined();
    return RValue_makeOwnedString(safeStrdup((*mapPtr)[idx + 1].key));
}

static RValue builtin_ds_map_size(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) shlen(*mapPtr));
}

static RValue builtin_ds_map_destroy(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(runner, id);
    if (mapPtr == nullptr) return RValue_makeUndefined();
    // Free all keys and values
    for (ptrdiff_t i = 0; shlen(*mapPtr) > i; i++) {
        free((*mapPtr)[i].key);
        RValue_free(&(*mapPtr)[i].value);
    }
    shfree(*mapPtr);
    *mapPtr = nullptr;
    return RValue_makeUndefined();
}

// ===[ DS_LIST FUNCTIONS ]===

static RValue builtin_ds_list_create(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeReal((GMLReal) dsListCreate(runner));
}

static RValue builtin_ds_list_add(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeUndefined();
    // ds_list_add can take multiple values after the list id
    repeat(argCount - 1, i) {
        arrput(list->items, RValue_makeIndependent(args[i + 1]));
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_list_delete(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t pos = RValue_toInt32(args[1]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeUndefined();
    if (0 > pos || pos >= (int32_t) arrlen(list->items)) return RValue_makeUndefined();
    if (list->items[pos].type == RVALUE_STRING) RValue_free(&list->items[pos]);
    arrdel(list->items, pos);
    return RValue_makeUndefined();
}

static RValue builtin_ds_list_empty(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeBool(true);
    return RValue_makeBool(arrlen(list->items) == 0);
}

static RValue builtin_ds_list_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeUndefined();
    repeat(arrlen(list->items), i) {
        RValue_free(&list->items[i]);
    }
    arrfree(list->items);
    list->items = nullptr;
    list->freed = true;
    return RValue_makeUndefined();
}

static RValue builtin_ds_list_find_value(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t pos = RValue_toInt32(args[1]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeUndefined();
    if (0 > pos || pos >= (int32_t) arrlen(list->items)) return RValue_makeUndefined();
    return RValue_makeIndependent(list->items[pos]);
}

static RValue builtin_ds_list_size(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) arrlen(list->items));
}

static RValue builtin_ds_list_find_index(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeReal(-1.0);
    RValue needle = args[1];
    for (int32_t i = 0; (int32_t) arrlen(list->items) > i; i++) {
        RValue item = list->items[i];
        if (item.type != needle.type) continue;
        switch (item.type) {
            case RVALUE_REAL:
                if (item.real == needle.real) return RValue_makeReal((GMLReal) i);
                break;
            case RVALUE_INT32:
            case RVALUE_BOOL:
                if (item.int32 == needle.int32) return RValue_makeReal((GMLReal) i);
                break;
#ifndef NO_RVALUE_INT64
            case RVALUE_INT64:
                if (item.int64 == needle.int64) return RValue_makeReal((GMLReal) i);
                break;
#endif
            case RVALUE_STRING:
                if (item.string != nullptr && needle.string != nullptr && strcmp(item.string, needle.string) == 0) return RValue_makeReal((GMLReal) i);
                break;
            default:
                break;
        }
    }
    return RValue_makeReal(-1.0);
}

static RValue builtin_ds_list_shuffle(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeUndefined();
    for (int32_t i = 1; i < argCount; i++) {
        int32_t j = rand() % (i + 1);
        RValue temp = list->items[i];
        list->items[i] = list->items[j];
        list->items[j] = temp;
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_list_clear(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeUndefined();
    // Clear the contents but keep the slot alive.
    repeat(arrlen(list->items), i) {
        RValue_free(&list->items[i]);
    }
    arrfree(list->items);
    list->items = nullptr;
    return RValue_makeUndefined();
}

// Byte-cursor over a decoded ds_list blob.
// "error" latches so a single truncated read short-circuits the rest.
typedef struct {
    const uint8_t* data;
    int32_t size;
    int32_t pos;
    bool error;
} DsReadStream;

static uint32_t dsStreamReadU32(DsReadStream* s) {
    if (s->error || s->pos + 4 > s->size) { s->error = true; return 0; }
    uint32_t v = BinaryUtils_readUint32(s->data + s->pos);
    s->pos += 4;
    return v;
}

static int32_t dsStreamReadS32(DsReadStream* s) {
    return (int32_t) dsStreamReadU32(s);
}

static double dsStreamReadF64(DsReadStream* s) {
    if (s->error || s->pos + 8 > s->size) { s->error = true; return 0.0; }
    double v = BinaryUtils_readFloat64(s->data + s->pos);
    s->pos += 8;
    return v;
}

static int64_t dsStreamReadI64(DsReadStream* s) {
    if (s->error || s->pos + 8 > s->size) { s->error = true; return 0; }
    int64_t v = BinaryUtils_readInt64(s->data + s->pos);
    s->pos += 8;
    return v;
}

static int dsHexNibble(char c) {
    if (c >= '0' && '9' >= c) return c - '0';
    if (c >= 'A' && 'F' >= c) return c - 'A' + 10;
    if (c >= 'a' && 'f' >= c) return c - 'a' + 10;
    return -1;
}

// Wire "magic values" used for ds_list_read and ds_list_write
// See GameMaker-HTML5's variable_ReadValue/variable_WriteValue for reference
#define DS_STREAM_VALUE_REAL 0
#define DS_STREAM_VALUE_STRING 1
#define DS_STREAM_VALUE_ARRAY 2
#define DS_STREAM_VALUE_UNDEFINED 5
#define DS_STREAM_VALUE_INT32 7
#define DS_STREAM_VALUE_INT64 10
#define DS_STREAM_VALUE_BOOL 13

// Mirror of dsStreamWriteValue."version" selects how ARRAY is laid out:
// * 0 = current format (magic 303): flat "len + values".
// * 3 = older native format (magic 302): outer "len_1d", then per-row "len + values" (jagged 2D).
static RValue dsStreamReadValue(DsReadStream* s, int32_t version) {
    uint32_t tag = dsStreamReadU32(s);
    if (s->error) return RValue_makeUndefined();
    switch (tag) {
        case DS_STREAM_VALUE_REAL:
            return RValue_makeReal((GMLReal) dsStreamReadF64(s));
        case DS_STREAM_VALUE_INT32:
            return RValue_makeInt32(dsStreamReadS32(s));
        case DS_STREAM_VALUE_INT64:
        case 3: // VALUE_PTR: native serializes as int64; same wire shape.
            return RValue_makeInt64(dsStreamReadI64(s));
        case DS_STREAM_VALUE_BOOL:
            return RValue_makeBool(dsStreamReadF64(s) != (double) 0.0);
        case DS_STREAM_VALUE_STRING: {
            int32_t len = dsStreamReadS32(s);
            if (s->error || 0 > len || s->pos + len > s->size) { s->error = true; return RValue_makeUndefined(); }
            char* str = safeMalloc((size_t) len + 1);
            if (len > 0) memcpy(str, s->data + s->pos, (size_t) len);
            str[len] = '\0';
            s->pos += len;
            return RValue_makeOwnedString(str);
        }
        case DS_STREAM_VALUE_ARRAY: {
            GMLArray* arr = GMLArray_create(0);
            if (version == 3) {
                int32_t len1d = dsStreamReadS32(s);
                if (s->error || 0 > len1d) { s->error = true; GMLArray_decRef(arr); return RValue_makeUndefined(); }
                if (len1d == 1) {
                    int32_t len = dsStreamReadS32(s);
                    if (s->error || 0 > len) { s->error = true; GMLArray_decRef(arr); return RValue_makeUndefined(); }
                    if (len > 0) GMLArray_growTo(arr, len);
                    for (int32_t i = 0; len > i && !s->error; i++) {
                        RValue v = dsStreamReadValue(s, version);
                        RValue* slot = GMLArray_slot(arr, i);
                        if (slot != nullptr) { RValue_free(slot); *slot = v; } else { RValue_free(&v); }
                    }
                } else {
                    for (int32_t o = 0; len1d > o && !s->error; o++) {
                        int32_t len = dsStreamReadS32(s);
                        if (s->error || 0 > len) { s->error = true; break; }
                        if (len > 0) GMLArray_growTo(arr, o * GML_ARRAY_STRIDE + len);
                        for (int32_t i = 0; len > i && !s->error; i++) {
                            RValue v = dsStreamReadValue(s, version);
                            RValue* slot = GMLArray_slot(arr, o * GML_ARRAY_STRIDE + i);
                            if (slot != nullptr) { RValue_free(slot); *slot = v; } else { RValue_free(&v); }
                        }
                    }
                }
            } else {
                int32_t len = dsStreamReadS32(s);
                if (s->error || 0 > len) { s->error = true; GMLArray_decRef(arr); return RValue_makeUndefined(); }
                if (len > 0) GMLArray_growTo(arr, len);
                for (int32_t i = 0; len > i && !s->error; i++) {
                    RValue v = dsStreamReadValue(s, version);
                    RValue* slot = GMLArray_slot(arr, i);
                    if (slot != nullptr) { RValue_free(slot); *slot = v; } else { RValue_free(&v); }
                }
            }
            return RValue_makeArray(arr);
        }
        case DS_STREAM_VALUE_UNDEFINED:
        default:
            return RValue_makeUndefined();
    }
}

static RValue builtin_ds_list_read(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    if (args[1].type != RVALUE_STRING || args[1].string == nullptr || args[1].string[0] == '\0') {
        return RValue_makeBool(false);
    }
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeBool(false);

    const char* hex = args[1].string;
    int32_t hexLen = (int32_t) strlen(hex);
    if (2 > hexLen || (hexLen & 1) != 0) return RValue_makeBool(false);

    int32_t byteLen = hexLen / 2;
    uint8_t* bytes = safeMalloc((size_t) byteLen);
    repeat(byteLen, i) {
        int hi = dsHexNibble(hex[i * 2]);
        int lo = dsHexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) { free(bytes); return RValue_makeBool(false); }
        bytes[i] = (uint8_t) ((hi << 4) | lo);
    }

    DsReadStream s = {0};
    s.data = bytes;
    s.size = byteLen;
    s.pos = 0;
    s.error = false;
    uint32_t magic = dsStreamReadU32(&s);
    int32_t version;
    // 301 = ~BC13 (REAL/STRING/ARRAY only)
    // 302 = ~BC16 (adds INT32/INT64/BOOL/PTR)
    if (magic == 301 || magic == 302) {
        version = 3;
    } else if (magic == 303) {
        version = 0;
    } else {
        free(bytes);
        return RValue_makeBool(false);
    }

    int32_t len = dsStreamReadS32(&s);
    if (s.error || 0 > len) { free(bytes); return RValue_makeBool(false); }

    // Replace, don't append: matches native ds_list_read which clears the list first.
    repeat(arrlen(list->items), i) {
        RValue_free(&list->items[i]);
    }
    arrfree(list->items);
    list->items = nullptr;

    repeat(len, i) {
        RValue v = dsStreamReadValue(&s, version);
        if (s.error) { RValue_free(&v); free(bytes); return RValue_makeBool(false); }
        arrput(list->items, v);
    }

    free(bytes);
    return RValue_makeBool(true);
}

static void dsStreamAppendU32(uint8_t** buf, uint32_t val) {
    uint8_t tmp[4];
    BinaryUtils_writeUint32(tmp, val);
    repeat(4, i) arrput(*buf, tmp[i]);
}

static void dsStreamAppendF64(uint8_t** buf, double val) {
    uint8_t tmp[8];
    BinaryUtils_writeFloat64(tmp, val);
    repeat(8, i) arrput(*buf, tmp[i]);
}

#ifndef NO_RVALUE_INT64
static void dsStreamAppendI64(uint8_t** buf, int64_t val) {
    uint8_t tmp[8];
    BinaryUtils_writeInt64(tmp, val);
    repeat(8, i) arrput(*buf, tmp[i]);
}
#endif

static void dsStreamWriteValue(uint8_t** buf, RValue val) {
    switch (val.type) {
        case RVALUE_REAL:
            dsStreamAppendU32(buf, DS_STREAM_VALUE_REAL);
            dsStreamAppendF64(buf, (double) val.real);
            return;
        case RVALUE_INT32:
            dsStreamAppendU32(buf, DS_STREAM_VALUE_INT32);
            dsStreamAppendU32(buf, (uint32_t) val.int32);
            return;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:
            dsStreamAppendU32(buf, DS_STREAM_VALUE_INT64);
            dsStreamAppendI64(buf, val.int64);
            return;
#endif
        case RVALUE_BOOL:
            dsStreamAppendU32(buf, DS_STREAM_VALUE_BOOL);
            dsStreamAppendF64(buf, val.int32 != 0 ? 1.0 : 0.0);
            return;
        case RVALUE_STRING: {
            const char* str = val.string != nullptr ? val.string : "";
            int32_t len = (int32_t) strlen(str);
            dsStreamAppendU32(buf, DS_STREAM_VALUE_STRING);
            dsStreamAppendU32(buf, (uint32_t) len);
            repeat(len, i) arrput(*buf, (uint8_t) str[i]);
            return;
        }
        case RVALUE_ARRAY: {
            int32_t len = GMLArray_length1D(val.array);
            dsStreamAppendU32(buf, DS_STREAM_VALUE_ARRAY);
            dsStreamAppendU32(buf, (uint32_t) len);
            repeat(len, i) {
                RValue* slot = GMLArray_slot(val.array, i);
                dsStreamWriteValue(buf, slot != nullptr ? *slot : RValue_makeUndefined());
            }
            return;
        }
        case RVALUE_ASSETREF:
            // Asset refs are int32 indices; persist them as INT32 so a round-trip read recovers the index.
            dsStreamAppendU32(buf, DS_STREAM_VALUE_INT32);
            dsStreamAppendU32(buf, (uint32_t) val.int32);
            return;
        default:
            // Undefined / Struct / Method: native runner writes only the type tag with no payload.
            dsStreamAppendU32(buf, DS_STREAM_VALUE_UNDEFINED);
            return;
    }
}

// Appends each RValue in `items` (length `len`) to `buf` using the ds wire format.
static void dsStreamAppendValues(uint8_t** buf, const RValue* items, int32_t len) {
    repeat(len, i) {
        dsStreamWriteValue(buf, items[i]);
    }
}

// Consumes "buf" (stb_ds array): hex-encodes it, frees it, and returns the hex as an owned-string RValue.
static RValue dsStreamFinishToHexString(uint8_t* buf) {
    int32_t byteLen = (int32_t) arrlen(buf);
    char* hex = safeMalloc((size_t) byteLen * 2 + 1);
    static const char HEX_CHARS[] = "0123456789ABCDEF";
    repeat(byteLen, i) {
        hex[i * 2] = HEX_CHARS[(buf[i] >> 4) & 0xF];
        hex[i * 2 + 1] = HEX_CHARS[buf[i] & 0xF];
    }
    hex[byteLen * 2] = '\0';
    arrfree(buf);
    return RValue_makeOwnedString(hex);
}

static RValue builtin_ds_list_write(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    DsList* list = dsListGet(runner, id);
    if (list == nullptr) return RValue_makeOwnedString(safeStrdup(""));

    uint8_t* buf = nullptr;
    int32_t len = (int32_t) arrlen(list->items);
    dsStreamAppendU32(&buf, 303); // version tag (see GameMaker-HTML5 ds_list.js)
    dsStreamAppendU32(&buf, (uint32_t) len);
    dsStreamAppendValues(&buf, list->items, len);
    return dsStreamFinishToHexString(buf);
}

// ===[ DS_STACK FUNCTIONS ]===

static RValue builtin_ds_stack_create(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeReal((GMLReal) dsStackCreate(ctx->runner));
}

static RValue builtin_ds_stack_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr) return RValue_makeUndefined();
    repeat(arrlen(s->items), i) {
        RValue_free(&s->items[i]);
    }
    arrfree(s->items);
    s->items = nullptr;
    s->freed = true;
    return RValue_makeUndefined();
}

static RValue builtin_ds_stack_clear(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr) return RValue_makeUndefined();
    repeat(arrlen(s->items), i) {
        RValue_free(&s->items[i]);
    }
    arrfree(s->items);
    s->items = nullptr;
    return RValue_makeUndefined();
}

static RValue builtin_ds_stack_copy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t destId = RValue_toInt32(args[0]);
    int32_t srcId = RValue_toInt32(args[1]);
    DsStack* dest = dsStackGet(ctx->runner, destId);
    DsStack* src = dsStackGet(ctx->runner, srcId);
    if (dest == nullptr || src == nullptr) return RValue_makeUndefined();
    arrfree(dest->items);
    dest->items = nullptr;
    repeat(arrlen(src->items), i) {
        arrput(dest->items, RValue_makeIndependent(src->items[i]));
    }
    dest->freed = false;
    return RValue_makeUndefined();
}

static RValue builtin_ds_stack_size(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr) return RValue_makeUndefined();
    return RValue_makeReal((GMLReal) arrlen(s->items));
}

static RValue builtin_ds_stack_empty(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr) return RValue_makeBool(true);
    return RValue_makeBool(arrlen(s->items) == 0);
}

static RValue builtin_ds_stack_push(VMContext* ctx, RValue* args, int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr) return RValue_makeUndefined();
    
    for (int32_t i = argCount - 1; i >= 1; --i) {
        arrput(s->items, RValue_makeIndependent(args[i]));
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_stack_pop(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    
    if (s == nullptr || arrlen(s->items) == 0) return RValue_makeReal(0.0);
    
    int32_t lastIdx = arrlen(s->items) - 1;
    RValue head = s->items[lastIdx];
    arrdel(s->items, lastIdx); 
    
    return head;
}

static RValue builtin_ds_stack_top(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr || arrlen(s->items) == 0) return RValue_makeReal(0.0);
    return RValue_makeIndependent(s->items[arrlen(s->items) - 1]);
}

static RValue builtin_ds_stack_write(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsStack* s = dsStackGet(ctx->runner, id);
    if (s == nullptr) return RValue_makeUndefined();

    uint8_t* buf = nullptr;
    int32_t len = (int32_t) arrlen(s->items);
    // Wire format mirrors GameMaker-HTML5 ds_stack.js
    dsStreamAppendU32(&buf, 103);
    dsStreamAppendU32(&buf, (uint32_t) len);
    dsStreamAppendValues(&buf, s->items, len);
    return dsStreamFinishToHexString(buf);
}

static RValue builtin_ds_stack_read(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    
    if (args[1].type != RVALUE_STRING || args[1].string == nullptr || args[1].string[0] == '\0') {
        return RValue_makeBool(false);
    }
    
    DsStack* st = dsStackGet(ctx->runner, id);
    if (st == nullptr) return RValue_makeBool(false);

    const char* hex = args[1].string;
    int32_t hexLen = (int32_t) strlen(hex);
    if (2 > hexLen || (hexLen & 1) != 0) return RValue_makeBool(false);

    int32_t byteLen = hexLen / 2;
    uint8_t* bytes = safeMalloc((size_t) byteLen);
    repeat(byteLen, i) {
        int hi = dsHexNibble(hex[i * 2]);
        int lo = dsHexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) { 
            free(bytes); 
            return RValue_makeBool(false); 
        }
        bytes[i] = (uint8_t) ((hi << 4) | lo);
    }

    DsReadStream s = {0};
    s.data = bytes;
    s.size = byteLen;
    
    uint32_t magic = dsStreamReadU32(&s);
    int32_t version;
    if (magic == 102) {
        version = 3;
    } else if (magic == 103) {
        version = 0;
    } else {
        free(bytes);
        return RValue_makeBool(false);
    }

    int32_t len = dsStreamReadS32(&s);
    if (s.error || 0 > len) { 
        free(bytes); 
        return RValue_makeBool(false); 
    }

    // Replace stack contents
    repeat(arrlen(st->items), i) {
        RValue_free(&st->items[i]);
    }
    arrfree(st->items);
    st->items = nullptr;

    repeat(len, i) {
        RValue v = dsStreamReadValue(&s, version);
        if (s.error) { 
            RValue_free(&v); 
            free(bytes); 
            return RValue_makeBool(false); 
        }
        arrput(st->items, v);
    }

    free(bytes);
    return RValue_makeBool(true);
}

// ===[ DS_QUEUE FUNCTIONS ]===

static RValue builtin_ds_queue_create(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeReal((GMLReal) dsQueueCreate(ctx->runner));
}

static RValue builtin_ds_queue_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeUndefined();
    repeat(arrlen(q->items), i) {
        RValue_free(&q->items[i]);
    }
    arrfree(q->items);
    q->items = nullptr;
    q->freed = true;
    return RValue_makeUndefined();
}

static RValue builtin_ds_queue_clear(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeUndefined();
    repeat(arrlen(q->items), i) {
        RValue_free(&q->items[i]);
    }
    arrfree(q->items);
    q->items = nullptr;
    return RValue_makeUndefined();
}

static RValue builtin_ds_queue_copy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t destId = RValue_toInt32(args[0]);
    int32_t srcId = RValue_toInt32(args[1]);
    DsQueue* dest = dsQueueGet(runner, destId);
    DsQueue* src = dsQueueGet(runner, srcId);
    if (dest == nullptr || src == nullptr) return RValue_makeUndefined();
    repeat(arrlen(dest->items), i) {
        RValue_free(&dest->items[i]);
    }
    arrfree(dest->items);
    dest->items = nullptr;
    repeat(arrlen(src->items), i) {
        arrput(dest->items, RValue_makeIndependent(src->items[i]));
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_queue_size(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) arrlen(q->items));
}

static RValue builtin_ds_queue_empty(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeBool(true);
    return RValue_makeBool(arrlen(q->items) == 0);
}

static RValue builtin_ds_queue_enqueue(VMContext* ctx, RValue* args, int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeUndefined();
    repeat(argCount - 1, i) {
        arrput(q->items, RValue_makeIndependent(args[i + 1]));
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_queue_dequeue(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr || arrlen(q->items) == 0) return RValue_makeReal(0.0);
    RValue head = q->items[0];
    arrdel(q->items, 0);
    return head;
}

static RValue builtin_ds_queue_head(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr || arrlen(q->items) == 0) return RValue_makeReal(0.0);
    return RValue_makeIndependent(q->items[0]);
}

static RValue builtin_ds_queue_tail(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr || arrlen(q->items) == 0) return RValue_makeReal(0.0);
    return RValue_makeIndependent(q->items[arrlen(q->items) - 1]);
}

static RValue builtin_ds_queue_write(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeOwnedString(safeStrdup(""));

    uint8_t* buf = nullptr;
    int32_t len = (int32_t) arrlen(q->items);
    // Wire format mirrors GameMaker-HTML5 ds_queue.js: magic 203, last=len, first=0, count=len, then values head->tail.
    dsStreamAppendU32(&buf, 203);
    dsStreamAppendU32(&buf, (uint32_t) len);
    dsStreamAppendU32(&buf, 0);
    dsStreamAppendU32(&buf, (uint32_t) len);
    dsStreamAppendValues(&buf, q->items, len);
    return dsStreamFinishToHexString(buf);
}

static RValue builtin_ds_queue_read(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    if (args[1].type != RVALUE_STRING || args[1].string == nullptr || args[1].string[0] == '\0') {
        return RValue_makeBool(false);
    }
    DsQueue* q = dsQueueGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeBool(false);

    const char* hex = args[1].string;
    int32_t hexLen = (int32_t) strlen(hex);
    if (2 > hexLen || (hexLen & 1) != 0) return RValue_makeBool(false);

    int32_t byteLen = hexLen / 2;
    uint8_t* bytes = safeMalloc((size_t) byteLen);
    repeat(byteLen, i) {
        int hi = dsHexNibble(hex[i * 2]);
        int lo = dsHexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) { free(bytes); return RValue_makeBool(false); }
        bytes[i] = (uint8_t) ((hi << 4) | lo);
    }

    DsReadStream s = {0};
    s.data = bytes;
    s.size = byteLen;
    uint32_t magic = dsStreamReadU32(&s);
    int32_t version;
    if (magic == 202) {
        version = 3;
    } else if (magic == 203) {
        version = 0;
    } else {
        free(bytes);
        return RValue_makeBool(false);
    }

    // last = total values stored on the wire (loop count), first = how many at the start to skip, count = informational.
    int32_t last = dsStreamReadS32(&s);
    int32_t first = dsStreamReadS32(&s);
    (void) dsStreamReadS32(&s); // count
    if (s.error || 0 > last) { free(bytes); return RValue_makeBool(false); }

    // Replace queue contents.
    repeat(arrlen(q->items), i) {
        RValue_free(&q->items[i]);
    }
    arrfree(q->items);
    q->items = nullptr;

    repeat(last, i) {
        RValue v = dsStreamReadValue(&s, version);
        if (s.error) { RValue_free(&v); free(bytes); return RValue_makeBool(false); }
        if (first <= 0) {
            arrput(q->items, v);
        } else {
            RValue_free(&v);
        }
        first--;
    }

    free(bytes);
    return RValue_makeBool(true);
}

// ===[ DS_PRIORITY FUNCTIONS ]===

static int32_t dsPriorityCreate(Runner* runner) {
    int32_t poolSize = (int32_t) arrlen(runner->dsPriorityPool);
    repeat(poolSize, i) {
        if (runner->dsPriorityPool[i].freed) {
            runner->dsPriorityPool[i].freed = false;
            return i;
        }
    }
    DsPriority p = {0};
    int32_t id = poolSize;
    arrput(runner->dsPriorityPool, p);
    return id;
}

static DsPriority* dsPriorityGet(Runner* runner, int32_t id) {
    if (0 > id || id >= (int32_t) arrlen(runner->dsPriorityPool)) return nullptr;
    if (runner->dsPriorityPool[id].freed) return nullptr;
    return &runner->dsPriorityPool[id];
}

static RValue builtin_ds_priority_create(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeReal((GMLReal) dsPriorityCreate(ctx->runner));
}

static RValue builtin_ds_priority_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    repeat(arrlen(pQueue->items), i) {
        RValue_free(&pQueue->items[i].item);
        pQueue->items[i].depth = 0;
    }
    arrfree(pQueue->items);
    pQueue->items = nullptr;
    pQueue->freed = true;
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_clear(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    repeat(arrlen(pQueue->items), i) {
        RValue_free(&pQueue->items[i].item);
        pQueue->items[i].depth = 0;
    }
    arrfree(pQueue->items);
    pQueue->items = nullptr;
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_copy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t destId = RValue_toInt32(args[0]);
    int32_t srcId = RValue_toInt32(args[1]);
    DsPriority* dest = dsPriorityGet(runner, destId);
    DsPriority* src = dsPriorityGet(runner, srcId);
    if (dest == nullptr || src == nullptr) return RValue_makeUndefined();
    repeat(arrlen(dest->items), i) {
        RValue_free(&dest->items[i].item);
        dest->items[i].depth = 0;
    }
    arrfree(dest->items);
    dest->items = nullptr;
    repeat(arrlen(src->items), i) {
        DsPriorityItem item;
        item.item = RValue_makeIndependent(src->items[i].item);
        item.depth = src->items[i].depth;
        arrput(dest->items, item);
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_size(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) arrlen(pQueue->items));
}

static RValue builtin_ds_priority_empty(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeBool(true);
    return RValue_makeBool(arrlen(pQueue->items) == 0);
}

static RValue builtin_ds_priority_add(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    int32_t prio = RValue_toInt32(args[2]);

    DsPriorityItem item;
    item.depth = prio;
    item.item = RValue_makeIndependent(args[1]);

    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    arrput(pQueue->items, item);
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_change_priority(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    RValue val = args[1];
    int32_t prio = RValue_toInt32(args[2]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    for (int32_t i = 0; i < (int32_t) arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (&item->item == &val) {
            arrdel(pQueue->items, i);
            item->depth = prio;
            arrput(pQueue->items, *item);
            break;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_find_priority(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    RValue value = RValue_makeIndependent(args[1]);
    for (int32_t i = 0; i < (int32_t) arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (item->item.type == RVALUE_REAL && value.type == RVALUE_REAL) {
            if (GML_MATH_EPSILON > GMLReal_fabs(item->item.real - value.real)) {
                return RValue_makeReal((GMLReal) item->depth);
            }
        } else if (memcmp(&item->item, &value, sizeof(RValue)) == 0) {
            return RValue_makeReal((GMLReal) item->depth);
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_delete_value(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    RValue value = RValue_makeIndependent(args[1]);
    for (int32_t i = 0; i < (int32_t) arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (item->item.type == RVALUE_REAL && value.type == RVALUE_REAL) {
            if (GML_MATH_EPSILON > GMLReal_fabs(item->item.real - value.real)) {
                arrdel(pQueue->items, i);
                return RValue_makeUndefined();
            }
        } else if (memcmp(&item->item, &value, sizeof(RValue)) == 0) {
            arrdel(pQueue->items, i);
            return RValue_makeUndefined();
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_ds_priority_delete_min(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = (int32_t) RValue_toReal(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    if (arrlen(pQueue->items) <= 0) return RValue_makeUndefined();
    GMLReal minDepth = (GMLReal) INT32_MAX;
    DsPriorityItem* minNode = nullptr;
    int32_t minIndex = -1;
    for (int32_t i = 0; i < arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (item->depth < minDepth) {
            minDepth = (GMLReal) item->depth;
            minNode = item;
            minIndex = i;
        }
    }
    RValue result = RValue_makeIndependent(minNode->item);
    arrdel(pQueue->items, minIndex);
    return result;
}

static RValue builtin_ds_priority_find_min(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = (int32_t) RValue_toReal(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    if (arrlen(pQueue->items) <= 0) return RValue_makeUndefined();
    GMLReal minDepth = (GMLReal) INT32_MAX;
    DsPriorityItem* minNode = nullptr;
    for (int32_t i = 0; i < arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (item->depth < minDepth) {
            minDepth = (GMLReal) item->depth;
            minNode = item;
        }
    }
    return RValue_makeIndependent(minNode->item);
}

static RValue builtin_ds_priority_delete_max(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = (int32_t) RValue_toReal(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    if (arrlen(pQueue->items) <= 0) return RValue_makeUndefined();
    GMLReal maxDepth = (GMLReal) INT32_MIN;
    DsPriorityItem* maxNode = nullptr;
    int32_t maxIndex = -1;
    for (int32_t i = 0; i < arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (item->depth > maxDepth) {
            maxDepth = (GMLReal) item->depth;
            maxNode = item;
            maxIndex = i;
        }
    }
    RValue result = RValue_makeIndependent(maxNode->item);
    arrdel(pQueue->items, maxIndex);
    return result;
}

static RValue builtin_ds_priority_find_max(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = (int32_t) RValue_toReal(args[0]);
    DsPriority* pQueue = dsPriorityGet(ctx->runner, id);
    if (pQueue == nullptr) return RValue_makeUndefined();
    if (arrlen(pQueue->items) <= 0) return RValue_makeUndefined();
    GMLReal maxDepth = (GMLReal) INT32_MIN;
    DsPriorityItem* maxNode = nullptr;
    for (int32_t i = 0; i < arrlen(pQueue->items); i++) {
        DsPriorityItem* item = &pQueue->items[i];
        if (item->depth > maxDepth) {
            maxDepth = (GMLReal) item->depth;
            maxNode = item;
        }
    }
    return RValue_makeIndependent(maxNode->item);
}

static RValue builtin_ds_priority_write(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    DsPriority* q = dsPriorityGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeOwnedString(safeStrdup(""));

    uint8_t* buf = nullptr;
    int32_t len = (int32_t) arrlen(q->items);
    
    dsStreamAppendU32(&buf, 503);
    dsStreamAppendU32(&buf, (uint32_t) len);

    if (len > 0) {
        RValue* priArr = (RValue*) safeMalloc((size_t) len * sizeof(RValue));
        RValue* valArr = (RValue*) safeMalloc((size_t) len * sizeof(RValue));

        repeat(len, i) {
            priArr[i] = RValue_makeReal((double) q->items[i].depth);
            valArr[i] = q->items[i].item;
        }

        dsStreamAppendValues(&buf, priArr, len);
        dsStreamAppendValues(&buf, valArr, len);

        free(priArr);
        free(valArr);
    }
    return dsStreamFinishToHexString(buf);
}

static RValue builtin_ds_priority_read(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t id = RValue_toInt32(args[0]);
    
    if (args[1].type != RVALUE_STRING || args[1].string == nullptr || args[1].string[0] == '\0') {
        return RValue_makeBool(false);
    }
    
    DsPriority* q = dsPriorityGet(ctx->runner, id);
    if (q == nullptr) return RValue_makeBool(false);

    const char* hex = args[1].string;
    int32_t hexLen = (int32_t) strlen(hex);
    if (2 > hexLen || (hexLen & 1) != 0) return RValue_makeBool(false);

    int32_t byteLen = hexLen / 2;
    uint8_t* bytes = (uint8_t*) safeMalloc((size_t) byteLen);
    repeat(byteLen, i) {
        int hi = dsHexNibble(hex[i * 2]);
        int lo = dsHexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) { 
            free(bytes); 
            return RValue_makeBool(false); 
        }
        bytes[i] = (uint8_t) ((hi << 4) | lo);
    }

    DsReadStream s = {0};
    s.data = bytes;
    s.size = byteLen;
    
    uint32_t magic = dsStreamReadU32(&s);
    int32_t version;
    if (magic == 502) {
        version = 3;
    } else if (magic == 503) {
        version = 0; 
    } else {
        free(bytes);
        return RValue_makeBool(false);
    }

    int32_t len = dsStreamReadS32(&s);
    if (s.error || 0 > len) { 
        free(bytes); 
        return RValue_makeBool(false); 
    }

    int32_t* tempPri = (int32_t*) safeMalloc((size_t) len * sizeof(int32_t));
    RValue* tempVal = (RValue*) safeMalloc((size_t) len * sizeof(RValue));

    repeat(len, i) {
        RValue p = dsStreamReadValue(&s, version);
        if (s.error) {
            RValue_free(&p);
            free(tempPri);
            free(tempVal);
            free(bytes);
            return RValue_makeBool(false);
        }
        tempPri[i] = RValue_toInt32(p);
        RValue_free(&p);
    }

    repeat(len, i) {
        RValue v = dsStreamReadValue(&s, version);
        if (s.error) {
            RValue_free(&v);
            repeat(i, j) RValue_free(&tempVal[j]); 
            free(tempPri);
            free(tempVal);
            free(bytes);
            return RValue_makeBool(false);
        }
        tempVal[i] = v;
    }

    repeat((int32_t) arrlen(q->items), i) {
        RValue_free(&q->items[i].item);
    }
    arrfree(q->items);
    q->items = nullptr;

    repeat(len, i) {
        DsPriorityItem pitem;
        pitem.depth = tempPri[i];
        pitem.item = tempVal[i];
        arrput(q->items, pitem);
    }

    free(tempPri);
    free(tempVal);
    free(bytes);
    return RValue_makeBool(true);
}

// ===[ ARRAY FUNCTIONS ]===

static RValue builtin_array_length_1d(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) GMLArray_length1D(args[0].array));
}

static RValue builtin_array_length_2d(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeReal(0.0);
    int32_t index = (int32_t) RValue_toReal(args[1]);
    if (index < 0 || index >= GMLArray_length1D(args[0].array)) return RValue_makeReal(0.0);
    RValue* slot = GMLArray_slot(args[0].array, index);
    if (slot == nullptr || slot->type != RVALUE_ARRAY || slot->array == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) GMLArray_length1D(slot->array));
}

static RValue builtin_array_height_2d(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) GMLArray_height2D(args[0].array));
}

// array_get(array, index) - return the value at the given index of row 0. Out-of-range or non-array input returns undefined.
static RValue builtin_array_get(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    int32_t index = (int32_t) RValue_toReal(args[1]);
    RValue* slot = GMLArray_slot(args[0].array, index);
    if (slot == nullptr) return RValue_makeUndefined();
    return RValue_makeIndependent(*slot);
}

// array_set(array, index, value) - write "value" into slot "index" of row 0, growing the array (padding with real 0) if needed.
// Mutates in place so the change is visible through every handle that shares the underlying GMLArray.
static RValue builtin_array_set(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    int32_t index = (int32_t) RValue_toReal(args[1]);
    if (0 > index) return RValue_makeUndefined();
    GMLArray* arr = args[0].array;
    int32_t oldLen = GMLArray_length1D(arr);
    if (index >= oldLen) {
        GMLArray_growTo(arr, index + 1);
        // GMLArray_growTo leaves the freshly added slots undefined; pad the gap with real 0 to match GML array_set semantics.
        for (int32_t i = oldLen; index > i; i++) {
            RValue* gap = GMLArray_slot(arr, i);
            if (gap != nullptr) { RValue_free(gap); *gap = RValue_makeReal(0.0); }
        }
    }
    RValue* slot = GMLArray_slot(arr, index);
    if (slot == nullptr) return RValue_makeUndefined();
    RValue_free(slot);
    *slot = RValue_makeIndependent(args[2]);
    return RValue_makeUndefined();
}

// array_push(array, values...) - append one or more values to the end of the array (row 0). BC17+ arrays are mutable references; mutate in place.
static RValue builtin_array_push(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    GMLArray* arr = args[0].array;
    int32_t startLen = GMLArray_length1D(arr);
    int32_t toPush = argCount - 1;
    if (toPush > 0) {
        GMLArray_growTo(arr, startLen + toPush);
        repeat(toPush, i) {
            RValue* slot = GMLArray_slot(arr, startLen + i);
            RValue val = args[1 + i];
            RValue_free(slot);
            *slot = RValue_makeIndependent(val);
        }
    }
    return RValue_makeUndefined();
}

// array_push(array) - pops a value from a array.
static RValue builtin_array_pop(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    GMLArray* arr = args[0].array;
    int32_t length = GMLArray_length1D(arr);
    // "If the array is empty, undefined is returned."
    if (length == 0) {
        return RValue_makeUndefined();
    }
    RValue* value = GMLArray_slot(arr, length - 1);
    // We are stealing the ownership, we don't need to increase the ref
    arr->rows[0].length--;
    return *value;
}

// array_insert(array, index, values...) - insert one or more values at "index", shifting the tail up. If "index" is past the end, fill the gap with real 0 (see the yyVariable.js for reference).
static RValue builtin_array_insert(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    GMLArray* arr = args[0].array;
    int32_t index = (int32_t) RValue_toReal(args[1]);
    if (0 > index) index = 0;
    int32_t toInsert = argCount - 2;
    int32_t oldLen = (arr->rowCount == 0) ? 0 : arr->rows[0].length;

    // Pad with real 0 if index is past the current end
    if (index > oldLen) {
        GMLArray_growTo(arr, index);
        GMLArrayRow* row = &arr->rows[0];
        for (int32_t i = oldLen; index > i; i++) {
            RValue_free(&row->data[i]);
            row->data[i] = RValue_makeReal(0.0);
        }
        oldLen = index;
    }

    if (0 >= toInsert) return RValue_makeUndefined();

    GMLArray_growTo(arr, oldLen + toInsert);
    GMLArrayRow* row = &arr->rows[0];

    // Shift tail up by toInsert
    int32_t tailLen = oldLen - index;
    if (tailLen > 0) memmove(&row->data[index + toInsert], &row->data[index], (size_t) tailLen * sizeof(RValue));

    // Write inserted values
    repeat(toInsert, i) {
        row->data[index + i] = RValue_makeIndependent(args[2 + i]);
    }
    return RValue_makeUndefined();
}

// array_resize(array, newSize) - resize row 0 to newSize. Growth fills with undefined, shrinking frees truncated entries.
static RValue builtin_array_resize(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    GMLArray* arr = args[0].array;
    int32_t newSize = (int32_t) RValue_toReal(args[1]);
    if (0 > newSize) newSize = 0;
    if (arr->rowCount == 0) {
        if (newSize == 0) return RValue_makeUndefined();
        GMLArray_growTo(arr, newSize);
        return RValue_makeUndefined();
    }
    GMLArrayRow* row = &arr->rows[0];
    if (newSize > row->length) {
        GMLArray_growTo(arr, newSize);
    } else if (row->length > newSize) {
        for (int32_t i = newSize; row->length > i; i++) RValue_free(&row->data[i]);
        row->length = newSize;
    }
    return RValue_makeUndefined();
}

// array_delete(array, pos, count) - remove `count` entries starting at `pos` from row 0, shifting the tail down.
static RValue builtin_array_delete(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    if (args[0].type != RVALUE_ARRAY || args[0].array == nullptr) return RValue_makeUndefined();
    GMLArray* arr = args[0].array;
    if (arr->rowCount == 0) return RValue_makeUndefined();
    GMLArrayRow* row = &arr->rows[0];
    int32_t pos = (int32_t) RValue_toReal(args[1]);
    int32_t count = (int32_t) RValue_toReal(args[2]);
    if (0 > pos) pos = 0;
    if (pos >= row->length || 0 >= count) return RValue_makeUndefined();
    if (count > row->length - pos) count = row->length - pos;
    repeat(count, i) RValue_free(&row->data[pos + i]);
    int32_t tailStart = pos + count;
    int32_t tailLen = row->length - tailStart;
    if (tailLen > 0) memmove(&row->data[pos], &row->data[tailStart], (size_t) tailLen * sizeof(RValue));
    row->length -= count;
    return RValue_makeUndefined();
}

// ===[ COLLISION FUNCTIONS]===

static RValue builtin_place_free(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeBool(true);

    Runner* runner = ctx->runner;
    Instance* caller = ctx->currentInstance;
    if (caller == nullptr) return RValue_makeBool(true);

    GMLReal testX = RValue_toReal(args[0]);
    GMLReal testY = RValue_toReal(args[1]);

    // Save current position and temporarily move to test position
    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    bool free = true;

    if (callerBBox.valid) {
        int32_t instanceCount = (int32_t) arrlen(runner->instances);
        repeat(instanceCount, i) {
            Instance* other = runner->instances[i];
            if (!other->active || !other->solid || other == caller) continue;

            InstanceBBox otherBBox = Collision_computeBBox(runner, other);
            if (!otherBBox.valid) continue;

            if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                free = false;
                break;
            }
        }
    }

    // Restore original position
    caller->x = savedX;
    caller->y = savedY;

    return RValue_makeBool(free);
}

// place_empty(x, y) - returns true if no instance overlaps at position (x, y), checking ALL instances (not just solid)
static bool placeEmptyAt(Runner* runner, Instance* caller, GMLReal testX, GMLReal testY) {
    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    bool empty = true;

    if (callerBBox.valid) {
        int32_t instanceCount = (int32_t) arrlen(runner->instances);
        repeat(instanceCount, i) {
            Instance* other = runner->instances[i];
            if (!other->active || other == caller) continue;

            InstanceBBox otherBBox = Collision_computeBBox(runner, other);
            if (!otherBBox.valid) continue;

            if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                empty = false;
                break;
            }
        }
    }

    caller->x = savedX;
    caller->y = savedY;
    return empty;
}

// placeFreeAt - returns true if no SOLID instance overlaps at position (x, y)
static bool placeFreeAt(Runner* runner, Instance* caller, GMLReal testX, GMLReal testY) {
    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    bool free = true;

    if (callerBBox.valid) {
        int32_t instanceCount = (int32_t) arrlen(runner->instances);
        repeat(instanceCount, i) {
            Instance* other = runner->instances[i];
            if (!other->active || !other->solid || other == caller) continue;

            InstanceBBox otherBBox = Collision_computeBBox(runner, other);
            if (!otherBBox.valid) continue;

            if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                free = false;
                break;
            }
        }
    }

    caller->x = savedX;
    caller->y = savedY;
    return free;
}

// noCollisionWithObject - returns true if no instance of the given object overlaps at position (x, y)
static bool noCollisionWithObject(Runner* runner, Instance* caller, GMLReal testX, GMLReal testY, int32_t objIndex) {
    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    bool free = true;

    if (callerBBox.valid) {
        int32_t snapBase = Runner_pushInstancesForTarget(runner, objIndex);
        int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
        for (int32_t i = snapBase; snapEnd > i; i++) {
            Instance* other = runner->instanceSnapshots[i];
            if (!other->active || other == caller) continue;

            InstanceBBox otherBBox = Collision_computeBBox(runner, other);
            if (!otherBBox.valid) continue;

            if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                free = false;
                break;
            }
        }
        Runner_popInstanceSnapshot(runner, snapBase);
    }

    caller->x = savedX;
    caller->y = savedY;
    return free;
}

// Tests whether a position is free for the given collision mode
// objIndex == INSTANCE_ALL with checkall=false: check solid only (place_free)
// objIndex == INSTANCE_ALL with checkall=true: check all instances (place_empty)
// objIndex == specific object/instance: check that specific target (instance_place == noone)
static bool mpTestFree(Runner* runner, Instance* inst, GMLReal x, GMLReal y, int32_t objIndex, bool checkall) {
    if (objIndex == INSTANCE_ALL) {
        if (checkall) {
            return placeEmptyAt(runner, inst, x, y);
        } else {
            return placeFreeAt(runner, inst, x, y);
        }
    } else {
        return noCollisionWithObject(runner, inst, x, y, objIndex);
    }
}

// place_empty(x, y) - returns true if no instance (solid or not) overlaps at position (x, y)
static RValue builtin_place_empty(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeBool(true);

    Runner* runner = ctx->runner;
    Instance* caller = ctx->currentInstance;
    if (caller == nullptr) return RValue_makeBool(true);

    GMLReal testX = RValue_toReal(args[0]);
    GMLReal testY = RValue_toReal(args[1]);
    return RValue_makeBool(placeEmptyAt(runner, caller, testX, testY));
}

// ===[ Motion Planning ]===

static RValue builtinMpLinearStepCommon(VMContext* ctx, GMLReal goalX, GMLReal goalY, GMLReal stepsize, int32_t objIndex, bool checkall) {
    Runner* runner = ctx->runner;
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeBool(false);

    // Check whether already at the correct position
    if (inst->x == (float) goalX && inst->y == (float) goalY) return RValue_makeBool(true);

    // Check whether close enough for a single step
    GMLReal dx = inst->x - goalX;
    GMLReal dy = inst->y - goalY;
    GMLReal dist = GMLReal_sqrt(dx * dx + dy * dy);

    GMLReal newX, newY;
    bool reached;
    if (dist <= stepsize) {
        newX = goalX;
        newY = goalY;
        reached = true;
    } else {
        newX = inst->x + stepsize * (goalX - inst->x) / dist;
        newY = inst->y + stepsize * (goalY - inst->y) / dist;
        reached = false;
    }

    // Check whether free
    if (!mpTestFree(runner, inst, newX, newY, objIndex, checkall)) return RValue_makeBool(reached);

    inst->direction = (float) (GMLReal_atan2(-(newY - inst->y), newX - inst->x) * (180.0 / M_PI));
    inst->x = (float) newX;
    inst->y = (float) newY;
    SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
    return RValue_makeBool(reached);
}

// mp_linear_step(x, y, stepsize, checkall)
static RValue builtin_mp_linear_step(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal goalX = RValue_toReal(args[0]);
    GMLReal goalY = RValue_toReal(args[1]);
    GMLReal stepsize = RValue_toReal(args[2]);
    bool checkall = RValue_toBool(args[3]);
    return builtinMpLinearStepCommon(ctx, goalX, goalY, stepsize, INSTANCE_ALL, checkall);
}

// mp_linear_step_object(x, y, stepsize, obj)
static RValue builtin_mp_linear_step_object(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal goalX = RValue_toReal(args[0]);
    GMLReal goalY = RValue_toReal(args[1]);
    GMLReal stepsize = RValue_toReal(args[2]);
    int32_t obj = RValue_toInt32(args[3]);
    return builtinMpLinearStepCommon(ctx, goalX, goalY, stepsize, obj, true);
}


// Computes the shortest angular difference between two directions (result 0-180)
static GMLReal mpDiffDir(GMLReal dir1, GMLReal dir2) {
    while (dir1 <= 0.0) dir1 += 360.0;
    while (dir1 >= 360.0) dir1 -= 360.0;
    while (dir2 < 0.0) dir2 += 360.0;
    while (dir2 >= 360.0) dir2 -= 360.0;
    GMLReal result = dir2 - dir1;
    if (result < 0.0) result = -result;
    if (result > 180.0) result = 360.0 - result;
    return result;
}

// Tries a step in the indicated direction; returns whether successful
// If successful, moves the instance and sets its direction
static bool mpTryDir(GMLReal dir, Runner* runner, Instance* inst, GMLReal speed, int32_t objIndex, bool checkall) {
    // See whether angle is acceptable
    if (mpDiffDir(dir, inst->direction) > runner->mpPotMaxrot) return false;

    GMLReal dirRad = dir * (M_PI / 180.0);
    GMLReal cosDir = GMLReal_cos(dirRad);
    GMLReal sinDir = GMLReal_sin(dirRad);

    // Check position a bit ahead
    GMLReal aheadX = inst->x + speed * runner->mpPotAhead * cosDir;
    GMLReal aheadY = inst->y - speed * runner->mpPotAhead * sinDir;
    if (!mpTestFree(runner, inst, aheadX, aheadY, objIndex, checkall)) return false;

    // Check next position
    GMLReal nextX = inst->x + speed * cosDir;
    GMLReal nextY = inst->y - speed * sinDir;
    if (!mpTestFree(runner, inst, nextX, nextY, objIndex, checkall)) return false;

    // OK, so set the position
    inst->direction = (float) dir;
    inst->x = (float) nextX;
    inst->y = (float) nextY;
    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
    return true;
}

static RValue builtinMpPotentialStepCommon(VMContext* ctx, GMLReal goalX, GMLReal goalY, GMLReal stepsize, int32_t objIndex, bool checkall) {
    Runner* runner = ctx->runner;
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeBool(false);

    // Check whether already at the correct position
    if (inst->x == (float) goalX && inst->y == (float) goalY) return RValue_makeBool(true);

    // Check whether close enough for a single step
    GMLReal dx = inst->x - goalX;
    GMLReal dy = inst->y - goalY;
    GMLReal dist = GMLReal_sqrt(dx * dx + dy * dy);
    if (stepsize >= dist) {
        if (mpTestFree(runner, inst, goalX, goalY, objIndex, checkall)) {
            GMLReal dir = GMLReal_atan2(-(goalY - inst->y), goalX - inst->x) * (180.0 / M_PI);
            inst->direction = (float) dir;
            inst->x = (float) goalX;
            inst->y = (float) goalY;
            SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
        }
        return RValue_makeBool(true);
    }

    // Try directions as much as possible towards the goal
    GMLReal goaldir = GMLReal_atan2(-(goalY - inst->y), goalX - inst->x) * (180.0 / M_PI);
    GMLReal curdir = 0.0;
    while (180.0 > curdir) {
        if (mpTryDir(goaldir - curdir, runner, inst, stepsize, objIndex, checkall)) return RValue_makeBool(false);
        if (mpTryDir(goaldir + curdir, runner, inst, stepsize, objIndex, checkall)) return RValue_makeBool(false);
        curdir += runner->mpPotStep;
    }

    // If we did not succeed, a local minima was reached
    // To avoid the instance getting stuck we rotate on the spot
    if (runner->mpPotOnSpot) {
        inst->direction = (float) (inst->direction + runner->mpPotMaxrot);
    }

    return RValue_makeBool(false);
}

// mp_potential_step(x, y, stepsize, checkall)
static RValue builtin_mp_potential_step(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal goalX = RValue_toReal(args[0]);
    GMLReal goalY = RValue_toReal(args[1]);
    GMLReal stepsize = RValue_toReal(args[2]);
    bool checkall = RValue_toBool(args[3]);
    return builtinMpPotentialStepCommon(ctx, goalX, goalY, stepsize, INSTANCE_ALL, checkall);
}

// mp_potential_step_object(x, y, stepsize, obj)
static RValue builtin_mp_potential_step_object(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal goalX = RValue_toReal(args[0]);
    GMLReal goalY = RValue_toReal(args[1]);
    GMLReal stepsize = RValue_toReal(args[2]);
    int32_t obj = RValue_toInt32(args[3]);
    return builtinMpPotentialStepCommon(ctx, goalX, goalY, stepsize, obj, true);
}

// mp_potential_settings(maxrot, rotstep, ahead, onspot)
static RValue builtin_mp_potential_settings(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal maxrot = RValue_toReal(args[0]);
    GMLReal rotstep = RValue_toReal(args[1]);
    GMLReal ahead = RValue_toReal(args[2]);
    bool onspot = RValue_toBool(args[3]);
    runner->mpPotMaxrot = (maxrot < 1.0) ? 1.0 : maxrot;
    runner->mpPotStep = (rotstep < 1.0) ? 1.0 : rotstep;
    runner->mpPotAhead = (ahead < 1.0) ? 1.0 : ahead;
    runner->mpPotOnSpot = onspot;
    return RValue_makeReal(0.0);
}

// ===[ Steam ]===

// Steam stubs
STUB_RETURN_ZERO(steam_initialised)
STUB_RETURN_ZERO(steam_stats_ready)
STUB_RETURN_ZERO(steam_file_exists)
STUB_RETURN_UNDEFINED(steam_file_write)
STUB_RETURN_UNDEFINED(steam_file_read)
STUB_RETURN_ZERO(steam_get_persona_name)

// ===[ Audio Built-in Functions ]===

// Helper to get the AudioSystem from VMContext (returns nullptr if no audio)
static AudioSystem* getAudioSystem(VMContext* ctx) {
    Runner* runner = ctx->runner;
    return runner->audioSystem;
}

static RValue builtin_audio_system_is_available(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    logSemiStubbedFunction(ctx, "audio_system_is_available");
    return RValue_makeBool(true);
}

static RValue builtin_audio_exists(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr || audio->vtable == nullptr || 1 > argCount) return RValue_makeBool(false);
    if (args[0].type == RVALUE_UNDEFINED) return RValue_makeBool(false);

    // Invalid sound index!
    int32_t soundIndex = RValue_toInt32(args[0]);
    if (0 > soundIndex) return RValue_makeBool(false);

    // Check if it is a valid soundIndex
    DataWin* dw = audio->audioGroups[0];
    if (dw->sond.count > (uint32_t) soundIndex)
        return RValue_makeBool(true);

    // If it isn't a valid soundIndex, then this is a sound instance handle
    // So let's check if the audio system is playing it!
    if (audio->vtable != nullptr && audio->vtable->isPlaying != nullptr && audio->vtable->isPlaying(audio, soundIndex))
        return RValue_makeBool(true);

    return RValue_makeBool(false);
}

static RValue builtin_audio_channel_num(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t count = RValue_toInt32(args[0]);
    audio->vtable->setChannelCount(audio, count);
    return RValue_makeUndefined();
}

// Old version of builtin_audio_play_sound, the GMS2 compatibility script sets the priority to 10 for... some reason
static RValue builtin_sound_play(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);

    // Do not attempt to play "undefined" sounds
    if (args[0].type == RVALUE_UNDEFINED)
        return RValue_makeReal(-1.0);

    int32_t soundIndex = RValue_toInt32(args[0]);
    int32_t instanceId = audio->vtable->playSound(audio, soundIndex, 10, false);
    return RValue_makeReal((GMLReal) instanceId);
}

// same as builtin_sound_play with loop enabled
static RValue builtin_sound_loop(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);

    // Do not attempt to play "undefined" sounds
    if (args[0].type == RVALUE_UNDEFINED)
        return RValue_makeReal(-1.0);

    int32_t soundIndex = RValue_toInt32(args[0]);
    int32_t instanceId = audio->vtable->playSound(audio, soundIndex, 10, true);
    return RValue_makeReal((GMLReal) instanceId);
}

static RValue builtin_sound_volume(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();

    int32_t soundIndex = RValue_toInt32(args[0]);
    float volume = (float) RValue_toReal(args[1]);

    // Set timeMs to 0 for immediate change
    audio->vtable->setSoundGain(audio, soundIndex, volume, 0);

    return RValue_makeUndefined();
}

static RValue builtin_audio_play_sound(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);

    // Do not attempt to play "undefined" sounds (matches GameMaker-HTML5 behavior, and fixes random sound effects on room transitions in DELTARUNE Chapter 2)
    if (args[0].type == RVALUE_UNDEFINED)
        return RValue_makeReal(-1.0);

    int32_t soundIndex = RValue_toInt32(args[0]);
    int32_t priority = RValue_toInt32(args[1]);
    bool loop = RValue_toBool(args[2]);
    int32_t instanceId = audio->vtable->playSound(audio, soundIndex, priority, loop);
    return RValue_makeReal((GMLReal) instanceId);
}

static RValue builtin_action_sound(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);

    // Do not attempt to play "undefined" sounds
    if (args[0].type == RVALUE_UNDEFINED)
        return RValue_makeReal(-1.0);

    int32_t soundIndex = RValue_toInt32(args[0]);
    bool loop = RValue_toBool(args[1]);
    int32_t instanceId = audio->vtable->playSound(audio, soundIndex, 10, loop);
    return RValue_makeReal((GMLReal) instanceId);
}

static RValue builtin_audio_stop_sound(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    audio->vtable->stopSound(audio, soundOrInstance);
    return RValue_makeUndefined();
}

static RValue builtin_audio_stop_all(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    audio->vtable->stopAll(audio);
    runner->lastMusicInstance = -1;
    return RValue_makeUndefined();
}

static RValue builtin_audio_is_playing(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeBool(false);
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    bool playing = audio->vtable->isPlaying(audio, soundOrInstance);
    return RValue_makeBool(playing);
}

static RValue builtin_audio_is_paused(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeBool(false);
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    bool playing = audio->vtable->isPlaying(audio, soundOrInstance);
    return RValue_makeBool(!playing);
}


// audio_sound_length(sound) - returns the length of a sound in seconds.
static RValue builtin_audio_sound_length(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(0.0);
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float length = audio->vtable->getSoundLength(audio, soundOrInstance);
    return RValue_makeReal((GMLReal) length);
}

static RValue builtin_audio_sound_gain(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float gain = (float) RValue_toReal(args[1]);
    uint32_t timeMs = (uint32_t) RValue_toInt32(args[2]);
    audio->vtable->setSoundGain(audio, soundOrInstance, gain, timeMs);
    return RValue_makeUndefined();
}

static RValue builtin_audio_sound_pitch(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float pitch = (float) RValue_toReal(args[1]);
    audio->vtable->setSoundPitch(audio, soundOrInstance, pitch);
    return RValue_makeUndefined();
}

static RValue builtin_audio_sound_get_gain(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(0.0);
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float gain = audio->vtable->getSoundGain(audio, soundOrInstance);
    return RValue_makeReal((GMLReal) gain);
}

static RValue builtin_audio_sound_get_pitch(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(1.0);
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float pitch = audio->vtable->getSoundPitch(audio, soundOrInstance);
    return RValue_makeReal((GMLReal) pitch);
}

static RValue builtin_audio_master_gain(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    float gain = (float) RValue_toReal(args[0]);
    audio->vtable->setMasterGain(audio, gain);
    return RValue_makeUndefined();
}

static RValue builtin_audio_group_load(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t groupIndex = RValue_toInt32(args[0]);
    audio->vtable->groupLoad(audio, groupIndex);
    return RValue_makeUndefined();
}

static RValue builtin_audio_group_is_loaded(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeBool(false);
    int32_t groupIndex = RValue_toInt32(args[0]);
    bool loaded = audio->vtable->groupIsLoaded(audio, groupIndex);
    return RValue_makeBool(loaded);
}

static RValue builtin_audio_play_music(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->dataWin->gen8.wadVersion >= 14) {
        fprintf(stderr, "VM: [%s] audio_play_music is no-op in WAD version 14+!\n", ctx->currentCodeName);
        return RValue_makeUndefined();
    }

    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);
    int32_t soundIndex = RValue_toInt32(args[0]);
    bool loop = RValue_toBool(args[1]);
    Runner* runner = ctx->runner;
    int32_t instanceId = audio->vtable->playSound(audio, soundIndex, 10, loop);
    runner->lastMusicInstance = instanceId;
    return RValue_makeReal((GMLReal) instanceId);
}

static RValue builtin_audio_stop_music(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->dataWin->gen8.wadVersion >= 14) {
        fprintf(stderr, "VM: [%s] audio_stop_music is no-op in WAD version 14+!\n", ctx->currentCodeName);
        return RValue_makeUndefined();
    }

    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    if (runner->lastMusicInstance >= 0) {
        audio->vtable->stopSound(audio, runner->lastMusicInstance);
        runner->lastMusicInstance = -1;
    }
    return RValue_makeUndefined();
}

static RValue builtin_audio_music_gain(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    if (runner->lastMusicInstance >= 0) {
        float gain = (float) RValue_toReal(args[0]);
        uint32_t timeMs = (uint32_t) RValue_toInt32(args[1]);
        audio->vtable->setSoundGain(audio, runner->lastMusicInstance, gain, timeMs);
    }
    return RValue_makeUndefined();
}

static RValue builtin_audio_music_is_playing(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    if (runner->lastMusicInstance >= 0) {
        return RValue_makeBool(audio->vtable->isPlaying(audio, runner->lastMusicInstance));
    }
    return RValue_makeBool(false);
}

static RValue builtin_audio_pause_sound(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    audio->vtable->pauseSound(audio, soundOrInstance);
    return RValue_makeUndefined();
}

static RValue builtin_audio_resume_sound(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    audio->vtable->resumeSound(audio, soundOrInstance);
    return RValue_makeUndefined();
}

static RValue builtin_audio_pause_all(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    audio->vtable->pauseAll(audio);
    return RValue_makeUndefined();
}

static RValue builtin_audio_resume_all(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    audio->vtable->resumeAll(audio);
    return RValue_makeUndefined();
}

static RValue builtin_audio_sound_get_track_position(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(0.0);
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float pos = audio->vtable->getTrackPosition(audio, soundOrInstance);
    return RValue_makeReal((GMLReal) pos);
}

static RValue builtin_audio_sound_set_track_position(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeUndefined();
    int32_t soundOrInstance = RValue_toInt32(args[0]);
    float pos = (float) RValue_toReal(args[1]);
    audio->vtable->setTrackPosition(audio, soundOrInstance, pos);
    return RValue_makeUndefined();
}

static RValue builtin_audio_create_stream(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);
    char* filename = RValue_toString(args[0]);
    int32_t streamIndex = audio->vtable->createStream(audio, filename);
    free(filename);
    return RValue_makeReal((GMLReal) streamIndex);
}

static RValue builtin_audio_destroy_stream(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    AudioSystem* audio = getAudioSystem(ctx);
    if (audio == nullptr) return RValue_makeReal(-1.0);
    int32_t streamIndex = RValue_toInt32(args[0]);
    bool success = audio->vtable->destroyStream(audio, streamIndex);
    return RValue_makeReal(success ? 1.0 : -1.0);
}

// Application surface
static RValue builtin_application_surface_enable(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == nullptr || argCount < 1) return RValue_makeUndefined();

    bool enable = RValue_toBool(args[0]);
    if (runner->appSurfaceEnabled) {
        runner->oldApplicationWidth = runner->applicationWidth;
        runner->oldApplicationHeight = runner->applicationHeight;
    }

    runner->appSurfaceEnabled = enable;
    runner->usingAppSurface = enable;

    if (!enable) {
        int32_t w = runner->applicationWidth;
        int32_t h = runner->applicationHeight;
        if (runner->getWindowSize != nullptr && runner->getWindowSize(&w, &h) && w > 0 && h > 0) {
            runner->applicationWidth = w;
            runner->applicationHeight = h;
        }
    } else {
        if (runner->oldApplicationWidth > 0 && runner->oldApplicationHeight > 0) {
            runner->applicationWidth = runner->oldApplicationWidth;
            runner->applicationHeight = runner->oldApplicationHeight;
        } else {
            runner->applicationWidth = (int32_t) ctx->dataWin->gen8.defaultWindowWidth;
            runner->applicationHeight = (int32_t) ctx->dataWin->gen8.defaultWindowHeight;
        }
    }

    return RValue_makeUndefined();
}

static RValue builtin_application_surface_draw_enable(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == nullptr || argCount < 1) return RValue_makeUndefined();
    runner->appSurfaceAutoDraw = RValue_toBool(args[0]);
    return RValue_makeUndefined();
}

// ===[ Gamepad Functions ]===
static RValue builtin_gamepad_get_device_count(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) RunnerGamepad_getDeviceCount(runner->gamepads));
}

static RValue builtin_gamepad_is_connected(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerGamepad_isConnected(runner->gamepads, device));
}

static RValue builtin_gamepad_button_check(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t device = RValue_toInt32(args[0]);
    int32_t button = RValue_toInt32(args[1]);
    bool result = RunnerGamepad_buttonCheck(runner->gamepads, device, button);
    return RValue_makeBool(result);
}

static RValue builtin_gamepad_button_check_pressed(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t device = RValue_toInt32(args[0]);
    int32_t button = RValue_toInt32(args[1]);
    return RValue_makeBool(RunnerGamepad_buttonCheckPressed(runner->gamepads, device, button));
}

static RValue builtin_gamepad_button_check_released(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t device = RValue_toInt32(args[0]);
    int32_t button = RValue_toInt32(args[1]);
    return RValue_makeBool(RunnerGamepad_buttonCheckReleased(runner->gamepads, device, button));
}

static RValue builtin_gamepad_button_value(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t device = RValue_toInt32(args[0]);
    int32_t button = RValue_toInt32(args[1]);
    return RValue_makeReal(RunnerGamepad_buttonValue(runner->gamepads, device, button));
}

static RValue builtin_gamepad_is_supported(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    return RValue_makeBool(true);
}

static RValue builtin_gamepad_axis_value(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t device = RValue_toInt32(args[0]);
    int32_t axis = RValue_toInt32(args[1]);
    return RValue_makeReal(RunnerGamepad_axisValue(runner->gamepads, device, axis));
}

static RValue builtin_gamepad_get_description(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeOwnedString(safeStrdup(""));
    int32_t device = RValue_toInt32(args[0]);
    const char* desc = RunnerGamepad_getDescription(runner->gamepads, device);
    return RValue_makeOwnedString(safeStrdup(desc));
}

static RValue builtin_gamepad_get_guid(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeOwnedString(safeStrdup("none"));
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeOwnedString(safeStrdup(RunnerGamepad_getGuid(runner->gamepads, device)));
}

static RValue builtin_gamepad_get_button_threshold(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.5);
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeReal(RunnerGamepad_getButtonThreshold(runner->gamepads, device));
}

static RValue builtin_gamepad_set_button_threshold(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeUndefined();
    int32_t device = RValue_toInt32(args[0]);
    float threshold = (float) RValue_toReal(args[1]);
    RunnerGamepad_setButtonThreshold(runner->gamepads, device, threshold);
    return RValue_makeUndefined();
}

static RValue builtin_gamepad_get_axis_deadzone(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.15);
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeReal(RunnerGamepad_getAxisDeadzone(runner->gamepads, device));
}

static RValue builtin_gamepad_set_axis_deadzone(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeUndefined();
    int32_t device = RValue_toInt32(args[0]);
    float deadzone = (float) RValue_toReal(args[1]);
    RunnerGamepad_setAxisDeadzone(runner->gamepads, device, deadzone);
    return RValue_makeUndefined();
}

static RValue builtin_gamepad_axis_count(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeReal(RunnerGamepad_getAxisCount(runner->gamepads, device));
}

static RValue builtin_gamepad_button_count(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeReal(RunnerGamepad_getButtonCount(runner->gamepads, device));
}

static RValue builtin_gamepad_hat_count(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t device = RValue_toInt32(args[0]);
    return RValue_makeReal(RunnerGamepad_getHatCount(runner->gamepads, device));
}

static RValue builtin_gamepad_hat_value(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t device = RValue_toInt32(args[0]);
    int32_t hat = RValue_toInt32(args[1]);
    return RValue_makeReal(RunnerGamepad_getHatValue(runner->gamepads, device, hat));
}

// ===[ INI Functions ]===

static void discardIniCache(Runner* runner) {
    if (runner->cachedIni != nullptr) {
        Ini_free(runner->cachedIni);
        runner->cachedIni = nullptr;
    }
    free(runner->cachedIniPath);
    runner->cachedIniPath = nullptr;
}

static RValue builtin_ini_open(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();

    Runner* runner = ctx->runner;
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");

    // If the same file is already open, do nothing
    if (runner->currentIni != nullptr && runner->currentIniPath != nullptr && strcmp(runner->currentIniPath, path) == 0) {
        return RValue_makeUndefined();
    }

    // Close any previously open INI (implicit close, no disk write)
    if (runner->currentIni != nullptr) {
        Ini_free(runner->currentIni);
        runner->currentIni = nullptr;
    }
    free(runner->currentIniPath);
    runner->currentIniPath = nullptr;

    // Check if we have a cached INI for this path
    if (runner->cachedIni != nullptr && runner->cachedIniPath != nullptr && strcmp(runner->cachedIniPath, path) == 0) {
        runner->currentIni = runner->cachedIni;
        runner->currentIniPath = runner->cachedIniPath;
        runner->cachedIni = nullptr;
        runner->cachedIniPath = nullptr;
        runner->currentIniDirty = false;
        return RValue_makeUndefined();
    }

    // Cache miss, discard the old cache and read from disk
    discardIniCache(runner);

    FileSystem* fs = runner->fileSystem;

    runner->currentIniPath = safeStrdup(path);

    char* content = fs->vtable->readFileText(fs, path);
    if (content != nullptr) {
        runner->currentIni = Ini_parse(content);
        free(content);
    } else {
        runner->currentIni = Ini_parse("");
    }

    runner->currentIniDirty = false;

    return RValue_makeUndefined();
}

// ini_open_from_string(content): opens a ini file from a string
static RValue builtin_ini_open_from_string(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();

    Runner* runner = ctx->runner;
    const char* content = (args[0].type == RVALUE_STRING ? args[0].string : "");

    // Implicit close of any open INI (no disk write)
    if (runner->currentIni != nullptr) {
        Ini_free(runner->currentIni);
        runner->currentIni = nullptr;
    }
    free(runner->currentIniPath);
    runner->currentIniPath = nullptr; // string-backed: no file path

    runner->currentIni = Ini_parse(content);
    runner->currentIniDirty = false;

    return RValue_makeUndefined();
}

static RValue builtin_ini_close(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->currentIni == nullptr) {
        free(runner->currentIniPath);
        runner->currentIniPath = nullptr;
        // No ini open = empty
        return RValue_makeOwnedString(safeStrdup(""));
    }

    // Serialize the current contents.
    char* serialized = Ini_serialize(runner->currentIni, INI_SERIALIZE_DEFAULT_INITIAL_CAPACITY);

    // Write back to disk only for file-backed INIs (ini_open).
    if (runner->currentIniDirty && runner->currentIniPath != nullptr) {
        FileSystem* fs = runner->fileSystem;
        fs->vtable->writeFileText(fs, runner->currentIniPath, serialized);
    }

    // Move to cache instead of freeing
    discardIniCache(runner);
    runner->cachedIni = runner->currentIni;
    runner->cachedIniPath = runner->currentIniPath;
    runner->currentIni = nullptr;
    runner->currentIniPath = nullptr;

    return RValue_makeOwnedString(serialized); // takes ownership of serialized
}

static RValue builtin_ini_read_string(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));

    if (runner->currentIni != nullptr) {
        const char* section = (args[0].type == RVALUE_STRING ? args[0].string : "");
        const char* key = (args[1].type == RVALUE_STRING ? args[1].string : "");

        const char* value = Ini_getString(runner->currentIni, section, key);
        if (value != nullptr) {
            return RValue_makeOwnedString(safeStrdup(value));
        }
    }

    // Return the default value (3rd arg)
    if (args[2].type == RVALUE_STRING && args[2].string != nullptr) {
        return RValue_makeOwnedString(safeStrdup(args[2].string));
    }
    char* str = RValue_toString(args[2]);
    return RValue_makeOwnedString(str);
}

static RValue builtin_ini_read_real(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (3 > argCount) return RValue_makeReal(0.0);

    if (runner->currentIni != nullptr) {
        const char* section = (args[0].type == RVALUE_STRING ? args[0].string : "");
        const char* key = (args[1].type == RVALUE_STRING ? args[1].string : "");

        const char* value = Ini_getString(runner->currentIni, section, key);
        if (value != nullptr) {
            return RValue_makeReal(atof(value));
        }
    }

    return RValue_makeReal(RValue_toReal(args[2]));
}

static RValue builtin_ini_write_string(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (3 > argCount || runner->currentIni == nullptr) return RValue_makeUndefined();

    const char* section = (args[0].type == RVALUE_STRING ? args[0].string : "");
    const char* key = (args[1].type == RVALUE_STRING ? args[1].string : "");
    const char* value = (args[2].type == RVALUE_STRING ? args[2].string : "");

    Ini_setString(runner->currentIni, section, key, value);
    runner->currentIniDirty = true;
    return RValue_makeUndefined();
}

static RValue builtin_ini_write_real(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (3 > argCount || runner->currentIni == nullptr) return RValue_makeUndefined();

    const char* section = (args[0].type == RVALUE_STRING ? args[0].string : "");
    const char* key = (args[1].type == RVALUE_STRING ? args[1].string : "");
    char* valueStr = RValue_toString(args[2]);

    Ini_setString(runner->currentIni, section, key, valueStr);
    runner->currentIniDirty = true;
    free(valueStr);
    return RValue_makeUndefined();
}

static RValue builtin_ini_section_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (1 > argCount || runner->currentIni == nullptr) return RValue_makeBool(false);

    const char* section = (args[0].type == RVALUE_STRING ? args[0].string : "");
    return RValue_makeBool(Ini_hasSection(runner->currentIni, section));
}

// ===[ Text File Functions ]===

static int32_t findFreeTextFileSlot(Runner* runner) {
    repeat(MAX_OPEN_TEXT_FILES, i) {
        if (!runner->openTextFiles[i].isOpen) return (int32_t) i;
    }
    return -1;
}

static RValue builtin_file_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;
    return RValue_makeBool(fs->vtable->fileExists(fs, path));
}

static RValue builtin_directory_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = (Runner*) ctx->runner;
    FileSystem* fs = runner->fileSystem;
    return RValue_makeBool(fs->vtable->directoryExists(fs, path));
}

static RValue builtin_directory_create(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = (Runner*) ctx->runner;
    FileSystem* fs = runner->fileSystem;
    fs->vtable->createDirectory(fs, path);
    return RValue_makeUndefined();
}

static RValue builtin_directory_destroy(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = (Runner*) ctx->runner;
    FileSystem* fs = runner->fileSystem;
    fs->vtable->deleteDirectory(fs, path);
    return RValue_makeUndefined();
}

static RValue builtin_file_text_open_read(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;

    int32_t slot = findFreeTextFileSlot(runner);
    if (0 > slot) {
        fprintf(stderr, "Warning: Too many open text files!\n");
        abort();
    }

    char* content = fs->vtable->readFileText(fs, path);
    if (content == nullptr) {
        // GML returns a valid handle even if the file doesn't exist; eof is immediately true
        content = safeStrdup("");
    }

    OpenTextFile file = {0};
    file.content = content;
    file.writeBuffer = nullptr;
    file.filePath = nullptr;
    file.readPos = 0;
    file.contentLen = (int32_t) strlen(content);
    file.isWriteMode = false;
    file.isOpen = true;
    runner->openTextFiles[slot] = file;

    return RValue_makeReal((GMLReal) slot);
}

static RValue builtin_file_text_open_write(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = ctx->runner;

    int32_t slot = findFreeTextFileSlot(runner);
    if (0 > slot) {
        fprintf(stderr, "Warning: Too many open text files!\n");
        abort();
    }

    OpenTextFile file = {0};
    file.content = nullptr;
    file.writeBuffer = safeStrdup("");
    file.filePath = safeStrdup(path);
    file.readPos = 0;
    file.contentLen = 0;
    file.isWriteMode = true;
    file.isOpen = true;
    runner->openTextFiles[slot] = file;

    return RValue_makeReal((GMLReal) slot);
}

static RValue builtin_file_text_close(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeUndefined();

    OpenTextFile* file = &runner->openTextFiles[handle];
    if (file->isWriteMode && file->writeBuffer != nullptr && file->filePath != nullptr) {
        FileSystem* fs = runner->fileSystem;
        fs->vtable->writeFileText(fs, file->filePath, file->writeBuffer);
    }

    free(file->content);
    free(file->writeBuffer);
    free(file->filePath);
    *file = (OpenTextFile) {0};
    return RValue_makeUndefined();
}

static RValue builtin_file_text_read_string(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeOwnedString(safeStrdup(""));

    OpenTextFile* file = &runner->openTextFiles[handle];
    if (file->readPos >= file->contentLen) return RValue_makeOwnedString(safeStrdup(""));

    // Read until newline, carriage return, or EOF (does NOT consume the newline)
    int32_t start = file->readPos;
    while (file->contentLen > file->readPos) {
        char c = file->content[file->readPos];
        if (TextUtils_isNewlineChar(c))
            break;
        file->readPos++;
    }

    int32_t len = file->readPos - start;
    char* result = safeMalloc((size_t) len + 1);
    memcpy(result, file->content + start, (size_t) len);
    result[len] = '\0';
    return RValue_makeOwnedString(result);
}

static RValue builtin_file_text_readln(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || MAX_OPEN_TEXT_FILES <= handle || !runner->openTextFiles[handle].isOpen) return RValue_makeOwnedString(safeStrdup(""));

    OpenTextFile* file = &runner->openTextFiles[handle];

    int size = 0;
    int readPos = file->readPos;

    // First we read everything to figure out what will be the size of the string
    // Skip past the current line (consume everything up to and including the newline)
    while (file->contentLen > readPos) {
        char c = file->content[readPos];
        readPos++;
        if (c == '\n')
            break;
        if (c == '\r') {
            // Handle \r\n
            if (file->contentLen > readPos && file->content[readPos] == '\n') {
                readPos++;
            }
            break;
        }
        size++;
    }

    // Now we copy it because we already know the size of the string!
    char* string = safeMalloc(size + 1); // +1 because the last one is null
    memcpy(string, file->content + file->readPos, size);
    string[size] = '\0';
    file->readPos = readPos;
    return RValue_makeOwnedString(string);
}

static RValue builtin_file_text_read_real(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeReal(0.0);

    OpenTextFile* file = &runner->openTextFiles[handle];
    if (file->readPos >= file->contentLen) return RValue_makeReal(0.0);

    // strtod will parse the number and advance past it
    char* endPtr = nullptr;
    GMLReal value = GMLReal_strtod(file->content + file->readPos, &endPtr);
    if (endPtr != nullptr) {
        file->readPos = (int32_t) (endPtr - file->content);
    }

    return RValue_makeReal(value);
}

static RValue builtin_file_text_write_string(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeUndefined();

    OpenTextFile* file = &runner->openTextFiles[handle];
    if (!file->isWriteMode) return RValue_makeUndefined();

    char* str = RValue_toString(args[1]);
    size_t oldLen = strlen(file->writeBuffer);
    size_t addLen = strlen(str);
    file->writeBuffer = safeRealloc(file->writeBuffer, oldLen + addLen + 1);
    memcpy(file->writeBuffer + oldLen, str, addLen);
    file->writeBuffer[oldLen + addLen] = '\0';
    free(str);

    return RValue_makeUndefined();
}

static RValue builtin_file_text_writeln(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeUndefined();

    OpenTextFile* file = &runner->openTextFiles[handle];
    if (!file->isWriteMode) return RValue_makeUndefined();

    size_t oldLen = strlen(file->writeBuffer);
    file->writeBuffer = safeRealloc(file->writeBuffer, oldLen + 2);
    file->writeBuffer[oldLen] = '\n';
    file->writeBuffer[oldLen + 1] = '\0';

    return RValue_makeUndefined();
}

static RValue builtin_file_text_write_real(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeUndefined();

    OpenTextFile* file = &runner->openTextFiles[handle];
    if (!file->isWriteMode) return RValue_makeUndefined();

    char* str = RValue_toString(args[1]);
    size_t oldLen = strlen(file->writeBuffer);
    size_t addLen = strlen(str);
    file->writeBuffer = safeRealloc(file->writeBuffer, oldLen + addLen + 1);
    memcpy(file->writeBuffer + oldLen, str, addLen);
    file->writeBuffer[oldLen + addLen] = '\0';
    free(str);

    return RValue_makeUndefined();
}

static RValue builtin_file_text_eof(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(true);
    Runner* runner = ctx->runner;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || handle >= MAX_OPEN_TEXT_FILES || !runner->openTextFiles[handle].isOpen) return RValue_makeBool(true);

    OpenTextFile* file = &runner->openTextFiles[handle];
    return RValue_makeBool(file->readPos >= file->contentLen);
}

static RValue builtin_file_delete(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;
    fs->vtable->deleteFile(fs, path);
    return RValue_makeUndefined();
}

// ===[ File Find Functions ]===

// Case-sensitive `*` / `?` wildcard match:
// "*" matches any run of characters (including empty), "?" matches exactly one character.
static bool matchWildcard(const char* pattern, const char* name) {
    const char* star = nullptr; // position in pattern just after the last '*' seen
    const char* starMatch = nullptr; // position in name where that '*' started matching
    while (*name != '\0') {
        if (*pattern == '?' || *pattern == *name) {
            pattern++;
            name++;
        } else if (*pattern == '*') {
            star = ++pattern; // remember the spot after the '*'
            starMatch = name; // and where it began consuming
        } else if (star != nullptr) {
            // Mismatch, but a previous '*' can absorb one more character: backtrack.
            pattern = star;
            name = ++starMatch;
        } else {
            return false;
        }
    }
    // Trailing '*'s in the pattern match the empty remainder.
    while (*pattern == '*') pattern++;
    return *pattern == '\0';
}

// Frees the active file_find_* session, if any.
static void closeFileFindSession(Runner* runner) {
    repeat(arrlen(runner->fileFindResults), i) {
        free(runner->fileFindResults[i]);
    }
    arrfree(runner->fileFindResults);
    runner->fileFindResults = nullptr;
    runner->fileFindPosition = 0;
}

static RValue builtin_file_find_first(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;

    // A new search always replaces any previous one.
    closeFileFindSession(runner);

    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    // TODO: File Attributes!
    const char* mask = (args[0].type == RVALUE_STRING ? args[0].string : "");

    // Split the mask into a directory part and a wildcard pattern at the last separator.
    char* maskCopy = safeStrdup(mask);
    for (int i = 0; maskCopy[i] != '\0'; i++) {
        if (maskCopy[i] == '\\') maskCopy[i] = '/';
    }
    char* lastSlash = strrchr(maskCopy, '/');
    const char* dir;
    const char* pattern;
    if (lastSlash != nullptr) {
        *lastSlash = '\0';
        dir = maskCopy;
        pattern = lastSlash + 1;
    } else {
        dir = "";
        pattern = maskCopy;
    }

    FileSystemDirEntry* entries = fs->vtable->listDirectory(fs, dir);
    repeat(arrlen(entries), i) {
        if (matchWildcard(pattern, entries[i].name)) {
            arrput(runner->fileFindResults, safeStrdup(entries[i].name));
        }
        free(entries[i].name);
    }
    arrfree(entries);
    free(maskCopy);

    if (0 >= arrlen(runner->fileFindResults)) return RValue_makeOwnedString(safeStrdup(""));
    runner->fileFindPosition = 0;
    return RValue_makeOwnedString(safeStrdup(runner->fileFindResults[0]));
}

static RValue builtin_file_find_next(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    runner->fileFindPosition++;
    if (runner->fileFindPosition >= (int32_t) arrlen(runner->fileFindResults)) {
        return RValue_makeOwnedString(safeStrdup(""));
    }
    return RValue_makeOwnedString(safeStrdup(runner->fileFindResults[runner->fileFindPosition]));
}

static RValue builtin_file_find_close(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    closeFileFindSession(ctx->runner);
    return RValue_makeUndefined();
}

// ===[ Binary File Functions ]===

static int32_t findFreeBinaryFileSlot(Runner* runner) {
    for (int32_t i = 1; MAX_OPEN_BINARY_FILES > i; i++) {
        if (!runner->openBinaryFiles[i].isOpen) return i;
    }
    return -1;
}

static OpenBinaryFile* getBinaryFile(Runner* runner, int32_t handle) {
    if (1 > handle || handle >= MAX_OPEN_BINARY_FILES) return nullptr;
    OpenBinaryFile* file = &runner->openBinaryFiles[handle];
    return file->isOpen ? file : nullptr;
}

static RValue builtin_file_bin_open(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-1.0);
    const char* path = (args[0].type == RVALUE_STRING ? args[0].string : "");
    int32_t mode = RValue_toInt32(args[1]);
    Runner* runner = ctx->runner;

    int32_t slot = findFreeBinaryFileSlot(runner);
    if (0 > slot) {
        fprintf(stderr, "Warning: Too many open binary files!\n");
        return RValue_makeReal(-1.0);
    }

    FileSystem* fs = runner->fileSystem;
    void* handle = fs->vtable->binaryOpen(fs, path, mode);
    if (handle == nullptr) return RValue_makeReal(-1.0);

    OpenBinaryFile file = {0};
    file.handle = handle;
    file.isOpen = true;
    runner->openBinaryFiles[slot] = file;
    return RValue_makeReal((GMLReal) slot);
}

static RValue builtin_file_bin_close(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeUndefined();
    runner->fileSystem->vtable->binaryClose(runner->fileSystem, file->handle);
    *file = (OpenBinaryFile) {0};
    return RValue_makeUndefined();
}

static RValue builtin_file_bin_position(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) runner->fileSystem->vtable->binaryTell(runner->fileSystem, file->handle));
}

static RValue builtin_file_bin_size(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) runner->fileSystem->vtable->binarySize(runner->fileSystem, file->handle));
}

static RValue builtin_file_bin_seek(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeUndefined();
    int32_t pos = RValue_toInt32(args[1]);
    if (0 > pos) pos = 0;
    runner->fileSystem->vtable->binarySeek(runner->fileSystem, file->handle, pos);
    return RValue_makeUndefined();
}

static RValue builtin_file_bin_read_byte(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeReal(0.0);
    uint8_t byte = 0;
    int32_t got = runner->fileSystem->vtable->binaryRead(runner->fileSystem, file->handle, &byte, 1);
    if (got != 1) return RValue_makeReal(0.0); // past EOF -> native returns 0
    return RValue_makeReal((GMLReal) byte);
}

static RValue builtin_file_bin_write_byte(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeUndefined();
    uint8_t byte = (uint8_t) (RValue_toInt32(args[1]) & 0xFF);
    runner->fileSystem->vtable->binaryWrite(runner->fileSystem, file->handle, &byte, 1);
    return RValue_makeUndefined();
}

static RValue builtin_file_bin_rewrite(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    OpenBinaryFile* file = getBinaryFile(runner, RValue_toInt32(args[0]));
    if (file == nullptr) return RValue_makeUndefined();
    runner->fileSystem->vtable->binaryRewrite(runner->fileSystem, file->handle);
    return RValue_makeUndefined();
}

// Keyboard functions
static RValue builtin_keyboard_check(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerKeyboard_check(runner->keyboard, key));
}

static RValue builtin_keyboard_check_pressed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerKeyboard_checkPressed(runner->keyboard, key));
}

static RValue builtin_keyboard_check_released(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerKeyboard_checkReleased(runner->keyboard, key));
}

static RValue builtin_keyboard_check_direct(VMContext* ctx, RValue* args, int32_t argCount) {
    // keyboard_check_direct is the same as keyboard_check for our purposes
    return builtin_keyboard_check(ctx, args, argCount);
}

static RValue builtin_keyboard_key_press(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    RunnerKeyboard_simulatePress(runner->keyboard, key);
    return RValue_makeUndefined();
}

static RValue builtin_keyboard_key_release(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    RunnerKeyboard_simulateRelease(runner->keyboard, key);
    return RValue_makeUndefined();
}

static RValue builtin_keyboard_clear(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    RunnerKeyboard_clear(runner->keyboard, key);
    return RValue_makeUndefined();
}

static RValue builtin_keyboard_set_map(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t fromKey = RValue_toInt32(args[0]);
    int32_t toKey = RValue_toInt32(args[1]);
    RunnerKeyboard_setMap(runner->keyboard, fromKey, toKey);
    return RValue_makeUndefined();
}

static RValue builtin_keyboard_get_map(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    int32_t fromKey = RValue_toInt32(args[0]);
    return RValue_makeReal((GMLReal) RunnerKeyboard_getMap(runner->keyboard, fromKey));
}

static RValue builtin_keyboard_unset_map(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    RunnerKeyboard_unsetMap(runner->keyboard);
    return RValue_makeUndefined();
}

// Mouse functions
static RValue builtinDeviceMouseCheckButton(VMContext* ctx, RValue* args, int32_t argCount) {   
    if (2 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;

    // We only support mouse 0 for now (device 0)
    int32_t device = RValue_toInt32(args[0]);
    if (device != 0) return RValue_makeBool(false);

    int32_t button = RValue_toInt32(args[1]);
    return RValue_makeBool(RunnerMouse_checkButton(runner->mouse, button));
}

static RValue builtinMouseCheckButton(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t button = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerMouse_checkButton(runner->mouse, button));
}

static RValue builtinMouseCheckButtonPressed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t button = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerMouse_checkButtonPressed(runner->mouse, button));
}

static RValue builtinMouseCheckButtonReleased(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t button = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerMouse_checkButtonReleased(runner->mouse, button));
}

static RValue builtinMouseClear(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = (Runner*) ctx->runner;
    int32_t button = RValue_toInt32(args[0]);
    RunnerMouse_clear(runner->mouse, button);
    return RValue_makeUndefined();
}

static RValue builtinMouseWheelUp(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    return RValue_makeBool(RunnerMouse_getWheelUp(runner->mouse));
}

static RValue builtinMouseWheelDown(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    return RValue_makeBool(RunnerMouse_getWheelDown(runner->mouse));
}

// ===[ Joystick Functions ]===
static RValue builtin_joystick_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t id = RValue_toInt32(args[0]) - 1;
    return RValue_makeBool(RunnerGamepad_isConnected(runner->gamepads, id));
}

static RValue builtin_joystick_xpos(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t id = RValue_toInt32(args[0]) - 1;
    return RValue_makeReal((GMLReal) RunnerGamepad_axisValue(runner->gamepads, id, GP_AXIS_LH));
}

static RValue builtin_joystick_ypos(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t id = RValue_toInt32(args[0]) - 1;
    return RValue_makeReal((GMLReal) RunnerGamepad_axisValue(runner->gamepads, id, GP_AXIS_LV));
}

static RValue builtin_joystick_direction(VMContext* ctx, RValue* args, int32_t argCount) {
    // Returns the joystick direction
    if (1 > argCount) return RValue_makeReal(101.0);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(101.0);
    int32_t id = RValue_toInt32(args[0]) - 1;
    float haxis = RunnerGamepad_axisValue(runner->gamepads, id, GP_AXIS_LH);
    float vaxis = RunnerGamepad_axisValue(runner->gamepads, id, GP_AXIS_LV);

    int32_t dir = 0;
    if (vaxis < -0.3f) {
        dir = 6;
    } else if (vaxis > 0.3f) {
        dir = 0;
    } else {
        dir = 3;
    }

    if (haxis < -0.3f) {
        dir += 1;
    } else if (haxis > 0.3f) {
        dir += 3;
    } else {
        dir += 2;
    }

    return RValue_makeReal(96 + dir);
}

static RValue builtin_joystick_pov(VMContext* ctx, RValue* args, int32_t argCount) {
    // Returns the D-pad/POV hat angle in degrees (0=up, 90=right, 180=down, 270=left),
    if (1 > argCount) return RValue_makeReal(-1.0);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(-1.0);
    int32_t id = RValue_toInt32(args[0]) - 1;
    RunnerGamepadState* gp = runner->gamepads;
    bool up    = RunnerGamepad_buttonCheck(gp, id, GP_PADU);
    bool down  = RunnerGamepad_buttonCheck(gp, id, GP_PADD);
    bool left  = RunnerGamepad_buttonCheck(gp, id, GP_PADL);
    bool right = RunnerGamepad_buttonCheck(gp, id, GP_PADR);
    if (!up && !down && !left && !right) return RValue_makeReal(-1.0);
    if (up    && right) return RValue_makeReal(45.0);
    if (right && down) return RValue_makeReal(135.0);
    if (down  && left) return RValue_makeReal(225.0);
    if (left  && up)   return RValue_makeReal(315.0);
    if (up)    return RValue_makeReal(0.0);
    if (right) return RValue_makeReal(90.0);
    if (down)  return RValue_makeReal(180.0);
    if (left)  return RValue_makeReal(270.0);
    return RValue_makeReal(-1.0);
}

static RValue builtin_joystick_check_button(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t id = RValue_toInt32(args[0]) - 1;
    int32_t button = RawToGPUndertale(RValue_toInt32(args[1])); //UNDERTALE HACK
    return RValue_makeBool(RunnerGamepad_buttonCheck(runner->gamepads, id, button));
}

static RValue builtin_joystick_has_pov(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeBool(false);
    int32_t id = RValue_toInt32(args[0]) - 1;
    return RValue_makeBool(RunnerGamepad_isConnected(runner->gamepads, id));
}

static RValue builtin_joystick_buttons(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t id = RValue_toInt32(args[0]) - 1;
    if (!RunnerGamepad_isConnected(runner->gamepads, id)) return RValue_makeReal(0.0);
    return RValue_makeReal(GP_BUTTON_COUNT);
}

static RValue builtin_joystick_name(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeOwnedString(safeStrdup(""));
    int32_t id = RValue_toInt32(args[0]) - 1;
    return RValue_makeOwnedString(safeStrdup(RunnerGamepad_getDescription(runner->gamepads, id)));
}

static RValue builtin_joystick_axes(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    if (runner == NULL || runner->gamepads == NULL) return RValue_makeReal(0.0);
    int32_t id = RValue_toInt32(args[0]) - 1;
    return RValue_makeReal(RunnerGamepad_getAxisCount(runner->gamepads, id));
}

// Window stubs
STUB_RETURN_ZERO(window_get_fullscreen)
STUB_RETURN_UNDEFINED(window_set_fullscreen)
static RValue builtin_window_get_width(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner != nullptr && runner->getWindowSize != nullptr) {
        int32_t w = 0;
        int32_t h = 0;
        if (runner->getWindowSize(&w, &h)) {
            return RValue_makeReal((GMLReal) w);
        }
    }
    return RValue_makeReal((GMLReal) ctx->dataWin->gen8.defaultWindowWidth);
}

static RValue builtin_window_get_height(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner != nullptr && runner->getWindowSize != nullptr) {
        int32_t w = 0;
        int32_t h = 0;
        if (runner->getWindowSize(&w, &h)) {
            return RValue_makeReal((GMLReal) h);
        }
    }
    return RValue_makeReal((GMLReal) ctx->dataWin->gen8.defaultWindowHeight);
}

static RValue builtin_window_set_size(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (argCount < 2) return RValue_makeUndefined();

    Runner* runner = ctx->runner;
    if (runner == nullptr) return RValue_makeUndefined();

    int32_t width = RValue_toInt32(args[0]);
    int32_t height = RValue_toInt32(args[1]);
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (runner->setWindowSize != nullptr) {
        runner->setWindowSize(width, height);
    }

    return RValue_makeUndefined();
}
STUB_RETURN_UNDEFINED(window_center)

static RValue builtin_window_set_caption(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    char* val = RValue_toString(args[0]);

    Runner* runner = ctx->runner;
    if (runner->setWindowTitle) {
        runner->setWindowTitle(val);
        printf("GL: Window title set to: %s\n", val);
    }

    free(val);
    return RValue_makeUndefined();
}

static RValue builtin_window_has_focus(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner != nullptr && runner->windowHasFocus) {
        return RValue_makeBool(runner->windowHasFocus());
    }

    return RValue_makeBool(true);
}

static RValue builtin_window_set_cursor(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount < 1) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    if (runner == nullptr) return RValue_makeUndefined();
    int32_t cursorType = RValue_toInt32(args[0]);
    runner->currentCursor = cursorType;
    if (runner->setCursor != nullptr) {
        runner->setCursor(cursorType);
    }
    return RValue_makeUndefined();
}

static RValue builtin_window_get_cursor(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner == nullptr) return RValue_makeReal(0);
    return RValue_makeReal(runner->currentCursor);
}

// ===[ Game State Functions ]===
static RValue builtin_game_restart(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    ctx->runner->pendingRoom = ROOM_RESTARTGAME;
    return RValue_makeUndefined();
}

static RValue builtin_game_end(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    runner->shouldExit = true;
    return RValue_makeUndefined();
}
STUB_RETURN_UNDEFINED(game_save)
STUB_RETURN_UNDEFINED(game_load)

static RValue builtin_instance_number(VMContext* ctx, MAYBE_UNUSED RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    int32_t objectIndex = RValue_toInt32(args[0]);
    int32_t count = 0;
    int32_t snapBase = Runner_pushInstancesOfObject(runner, objectIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        if (runner->instanceSnapshots[i]->active) count++;
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return RValue_makeReal((GMLReal) count);
}

static RValue builtin_instance_find(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(INSTANCE_NOONE);
    Runner* runner = ctx->runner;
    int32_t objectIndex = RValue_toInt32(args[0]);
    int32_t n = RValue_toInt32(args[1]);
    int32_t count = 0;
    int32_t resultId = INSTANCE_NOONE;
    int32_t snapBase = Runner_pushInstancesOfObject(runner, objectIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (!inst->active) continue;
        if (count == n) { resultId = inst->instanceId; break; }
        count++;
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return RValue_makeReal((GMLReal) resultId);
}

static RValue builtin_instance_nearest(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(INSTANCE_NOONE);
    Runner* runner = ctx->runner;
    GMLReal x = RValue_toReal(args[0]);
    GMLReal y = RValue_toReal(args[1]);
    GMLReal bestDistSq = 0.0;
    int32_t objectIndex = RValue_toInt32(args[2]);
    int32_t resultId = INSTANCE_NOONE;
    int32_t snapBase = Runner_pushInstancesOfObject(runner, objectIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (!inst->active) continue;

        GMLReal dx = inst->x - x;
        GMLReal dy = inst->y - y;
        GMLReal distSq = dx * dx + dy * dy;

        if (resultId == INSTANCE_NOONE || distSq < bestDistSq) {
            resultId = inst->instanceId;
            bestDistSq = distSq;
        }
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return RValue_makeReal((GMLReal) resultId);
}

static RValue builtin_instance_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool found = false;
    if (id >= 0 && runner->dataWin->objt.count > (uint32_t) id) {
        int32_t snapBase = Runner_pushInstancesOfObject(runner, id);
        int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
        for (int32_t i = snapBase; snapEnd > i; i++) {
            if (runner->instanceSnapshots[i]->active) { found = true; break; }
        }
        Runner_popInstanceSnapshot(runner, snapBase);
    } else {
        // Instance ID: search for a specific instance
        Instance* inst = hmget(runner->instancesById, id);
        found = (inst != nullptr && inst->active);
    }
    return RValue_makeBool(found);
}

static RValue builtin_instance_destroy(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    if (1 > argCount) {
        // No args: destroy the current instance
        if (ctx->currentInstance != nullptr) {
            Runner_destroyInstance(runner, ctx->currentInstance, true);
        }
        return RValue_makeUndefined();
    }
    bool runDestroyEvent = argCount >= 2 ? RValue_toBool(args[1]) : true;
    // 1 arg: find and destroy matching instances. Destroy events run user code that can spawn/destroy/instance_change other instances; iterate a snapshot of the bucket so those mutations don't corrupt our loop.
    int32_t id = RValue_toInt32(args[0]);
    if (id >= 0 && runner->dataWin->objt.count > (uint32_t) id) {
        int32_t snapBase = Runner_pushInstancesOfObject(runner, id);
        int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
        for (int32_t i = snapBase; snapEnd > i; i++) {
            Instance* inst = runner->instanceSnapshots[i];
            if (inst->active) Runner_destroyInstance(runner, inst, runDestroyEvent);
        }
        Runner_popInstanceSnapshot(runner, snapBase);
    } else {
        Instance* inst = hmget(runner->instancesById, id);
        if (inst != nullptr && inst->active) Runner_destroyInstance(runner, inst, runDestroyEvent);
    }
    return RValue_makeUndefined();
}

static RValue builtin_instance_create(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    GMLReal x = RValue_toReal(args[0]);
    GMLReal y = RValue_toReal(args[1]);
    int32_t objectIndex = RValue_toInt32(args[2]);
    if (0 > objectIndex || runner->dataWin->objt.count <= (uint32_t) objectIndex) {
        fprintf(stderr, "VM: instance_create: objectIndex %d out of range\n", objectIndex);
        return RValue_makeReal(0.0);
    }
    Instance* callerInst = ctx->currentInstance;
    Instance* inst = Runner_createInstance(runner, x, y, objectIndex);
    if (inst == nullptr) return RValue_makeReal(INSTANCE_NOONE);
    if (callerInst != nullptr && ctx->creatorVarID >= 0) {
        Instance_setSelfVar(inst, ctx->creatorVarID, RValue_makeReal((GMLReal) callerInst->instanceId));
    }
    return RValue_makeReal((GMLReal) inst->instanceId);
}

static RValue builtin_instance_copy(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    Instance* source = ctx->currentInstance;
    if (source == nullptr) {
        fprintf(stderr, "VM: instance_copy: no current instance\n");
        return RValue_makeReal(INSTANCE_NOONE);
    }
    bool performEvent = argCount > 0 ? RValue_toBool(args[0]) : false;
    Instance* inst = Runner_copyInstance(runner, source, performEvent);
    if (inst == nullptr) return RValue_makeReal(INSTANCE_NOONE);
    return RValue_makeReal((GMLReal) inst->instanceId);
}

static RValue builtin_instance_create_layer(VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeReal(INSTANCE_NOONE);
    Runner* runner = ctx->runner;
    GMLReal x = RValue_toReal(args[0]);
    GMLReal y = RValue_toReal(args[1]);
    int32_t layerId = resolveLayerIdArg(runner, args[2]);
    int32_t objectIndex = RValue_toInt32(args[3]);

    Instance* inst = Runner_createInstanceWithLayer(runner, x, y, objectIndex, layerId);
    if (inst == nullptr) return RValue_makeReal(INSTANCE_NOONE);

    Instance* callerInst = ctx->currentInstance;
    if (callerInst != nullptr && ctx->creatorVarID >= 0) {
        Instance_setSelfVar(inst, ctx->creatorVarID, RValue_makeReal((GMLReal) callerInst->instanceId));
    }

    return RValue_makeReal((GMLReal) inst->instanceId);
}

static RValue builtin_instance_create_depth(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    GMLReal x = RValue_toReal(args[0]);
    GMLReal y = RValue_toReal(args[1]);
    int32_t depth = RValue_toInt32(args[2]);
    int32_t objectIndex = RValue_toInt32(args[3]);
    if (0 > objectIndex || runner->dataWin->objt.count <= (uint32_t) objectIndex) {
        fprintf(stderr, "VM: instance_create: objectIndex %d out of range\n", objectIndex);
        return RValue_makeReal(0.0);
    }
    Instance* callerInst = ctx->currentInstance;
    Instance* inst = Runner_createInstanceWithDepth(runner, x, y, objectIndex, depth);
    if (inst == nullptr) return RValue_makeReal(INSTANCE_NOONE);
    if (callerInst != nullptr && ctx->creatorVarID >= 0) {
        Instance_setSelfVar(inst, ctx->creatorVarID, RValue_makeReal((GMLReal) callerInst->instanceId));
    }
    return RValue_makeReal((GMLReal) inst->instanceId);
}

static RValue builtin_instance_change(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();
    if (inst->objectIndex == STRUCT_OBJECT_INDEX) return RValue_makeUndefined();

    int32_t objectIndex = RValue_toInt32(args[0]);
    bool performEvents = RValue_toBool(args[1]);

    if (0 > objectIndex || (uint32_t) objectIndex >= runner->dataWin->objt.count) {
        fprintf(stderr, "VM: instance_change: objectIndex %d out of range\n", objectIndex);
        return RValue_makeUndefined();
    }

    // Fire destroy event on old object if requested
    if (performEvents) {
        Runner_executeEvent(runner, inst, EVENT_DESTROY, 0);
        Runner_executeEvent(runner, inst, EVENT_CLEANUP, 0);
    }

    // Move the instance between per-object lists before mutating objectIndex so the remove walks the old parent chain and the add walks the new one.
    Runner_removeInstanceFromObjectLists(runner, inst);
    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);

    // Change object index and copy properties from new object definition
    GameObject* newObjDef = &runner->dataWin->objt.objects[objectIndex];
    inst->objectIndex = objectIndex;
    Runner_addInstanceToObjectLists(runner, inst);
    inst->spriteIndex = newObjDef->spriteId;
    inst->visible = newObjDef->visible;
    inst->solid = newObjDef->solid;
    inst->persistent = newObjDef->persistent;
    inst->depth = newObjDef->depth;
    inst->maskIndex = newObjDef->textureMaskId;
    inst->imageIndex = 0.0;
    // The instance pointer is unchanged so this is just a depth shift, not a structural change.
    runner->drawableListSortDirty = true;

    // Fire create event on new object if requested
    if (performEvents) {
        Runner_executeEvent(runner, inst, EVENT_CREATE, 0);
    }

    return RValue_makeUndefined();
}

static RValue builtin_instance_deactivate_all(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    bool notme = RValue_toBool(args[0]);

    int instances = arrlen(ctx->runner->instances);
    repeat(instances, i) {
        Instance* instance = ctx->runner->instances[i];

        if (!notme || instance != ctx->currentInstance) {
            Runner_setActiveState(ctx->runner, instance, false);
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_instance_activate_all(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int instances = arrlen(ctx->runner->instances);
    repeat(instances, i) {
        Instance* instance = ctx->runner->instances[i];
        if (!instance->destroyed)
            Runner_setActiveState(ctx->runner, ctx->runner->instances[i], true);
    }
    return RValue_makeUndefined();
}

static RValue builtin_instance_activate_object(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t objIndex = RValue_toInt32(args[0]);

    // Per-object buckets retain inactive (deactivated) instances since we only remove on destroy-cleanup, so this still finds them. INSTANCE_ALL falls back to the full instances list.
    int32_t snapBase = Runner_pushInstancesForTarget(runner, objIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* instance = runner->instanceSnapshots[i];
        if (!instance->active && !instance->destroyed) Runner_setActiveState(ctx->runner, instance, true);
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return RValue_makeUndefined();
}

static RValue builtin_instance_deactivate_object(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t objIndex = RValue_toInt32(args[0]);

    int32_t snapBase = Runner_pushInstancesForTarget(runner, objIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* instance = runner->instanceSnapshots[i];
        if (instance->active && !instance->destroyed) Runner_setActiveState(ctx->runner, instance, false);
    }
    Runner_popInstanceSnapshot(runner, snapBase);
    return RValue_makeUndefined();
}

static RValue builtin_instance_activate_region(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    DataWin* dataWin = ctx->dataWin;
    GMLReal left = RValue_toReal(args[0]);
    GMLReal top = RValue_toReal(args[1]);
    GMLReal width = RValue_toReal(args[2]);
    GMLReal height = RValue_toReal(args[3]);
    bool wantInside = RValue_toBool(args[4]);

    GMLReal right = left + width - 1;
    GMLReal bottom = top + height - 1;

    // We don't use SpatialGrid here because inactive instances are NOT included in the SpatialGrid
    int instances = arrlen(runner->instances);
    repeat(instances, i) {
        Instance* inst = runner->instances[i];
        if (inst->active || inst->destroyed) continue;

        bool outside = false;
        Sprite* spr = Collision_getSprite(dataWin, inst);
        if (spr != nullptr) {
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (bbox.right < left || bbox.left > right || bbox.bottom < top || bbox.top > bottom) {
                outside = true;
            }
        } else {
            if (inst->x > right || left > inst->x || inst->y > bottom || top > inst->y) {
                outside = true;
            }
        }

        if (outside != wantInside) Runner_setActiveState(ctx->runner, inst, true);
    }
    return RValue_makeUndefined();
}

static RValue builtin_instance_deactivate_region(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    DataWin* dataWin = ctx->dataWin;
    GMLReal left = RValue_toReal(args[0]);
    GMLReal top = RValue_toReal(args[1]);
    GMLReal width = RValue_toReal(args[2]);
    GMLReal height = RValue_toReal(args[3]);
    bool wantInside = RValue_toBool(args[4]);
    bool notme = argCount > 5 ? RValue_toBool(args[5]) : false;
    Instance* self = ctx->currentInstance;

    GMLReal right = left + width - 1;
    GMLReal bottom = top + height - 1;

    // We don't use SpatialGrid here because sprite-less instances are NOT included in the SpatialGrid
    int instCount = arrlen(runner->instances);
    repeat(instCount, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active || inst->destroyed) continue;
        if (notme && inst == self) continue;
        bool outside = false;
        Sprite* spr = Collision_getSprite(dataWin, inst);
        if (spr != nullptr) {
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (bbox.right < left || bbox.left > right || bbox.bottom < top || bbox.top > bottom) outside = true;
        } else {
            if (inst->x > right || left > inst->x || inst->y > bottom || top > inst->y) outside = true;
        }
        if (outside != wantInside) Runner_setActiveState(ctx->runner, inst, false);
    }
    return RValue_makeUndefined();
}

static RValue builtin_event_inherited(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr || 0 > ctx->currentEventObjectIndex || 0 > ctx->currentEventType) {
        fprintf(stderr, "VM: event_inherited called with no event context (inst=%p, eventObjIdx=%d, eventType=%d)\n", (void*) inst, ctx->currentEventObjectIndex, ctx->currentEventType);
        return RValue_makeReal(0.0);
    }

    DataWin* dataWin = ctx->dataWin;
    int32_t ownerObjectIndex = ctx->currentEventObjectIndex;
    if ((uint32_t) ownerObjectIndex >= dataWin->objt.count) {
        fprintf(stderr, "VM: event_inherited ownerObjectIndex %d out of range\n", ownerObjectIndex);
        return RValue_makeReal(0.0);
    }

    int32_t parentObjectIndex = dataWin->objt.objects[ownerObjectIndex].parentId;
    if (ctx->traceEventInherited) {
        fprintf(stderr, "VM: [%s] event_inherited owner=%s(%d) parent=%s(%d) event=%s (instanceId=%d)\n", dataWin->objt.objects[inst->objectIndex].name, dataWin->objt.objects[ownerObjectIndex].name, ownerObjectIndex, (0 > parentObjectIndex) ? "none" : dataWin->objt.objects[parentObjectIndex].name, parentObjectIndex, Runner_getEventName(ctx->currentEventType, ctx->currentEventSubtype), inst->instanceId);
    }
    if (0 > parentObjectIndex) return RValue_makeReal(0.0);

    Runner_executeEventFromObject(runner, inst, parentObjectIndex, ctx->currentEventType, ctx->currentEventSubtype);
    return RValue_makeReal(0.0);
}

static RValue builtin_event_user(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeReal(0.0);

    int32_t subevent = RValue_toInt32(args[0]);
    if (0 > subevent || 15 < subevent) return RValue_makeReal(0.0);

    Runner_executeEvent(runner, inst, EVENT_OTHER, OTHER_USER0 + subevent);
    return RValue_makeReal(0.0);
}

static RValue builtin_event_perform(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeReal(0.0);

    int32_t eventType = RValue_toInt32(args[0]);
    int32_t eventSubtype = RValue_toInt32(args[1]);

    Runner_executeEvent(runner, inst, eventType, eventSubtype);
    return RValue_makeReal(0.0);
}

static RValue builtin_action_kill_object(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (ctx->currentInstance != nullptr) {
        Runner_destroyInstance(runner, ctx->currentInstance, true);
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_create_object(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t objectIndex = RValue_toInt32(args[0]);
    GMLReal x = RValue_toReal(args[1]);
    GMLReal y = RValue_toReal(args[2]);
    if (0 > objectIndex || runner->dataWin->objt.count <= (uint32_t) objectIndex) {
        fprintf(stderr, "VM: action_create_object: objectIndex %d out of range\n", objectIndex);
        return RValue_makeUndefined();
    }
    Instance* callerInst = ctx->currentInstance;
    if (ctx->actionRelativeFlag && callerInst != nullptr) {
        x += callerInst->x;
        y += callerInst->y;
    }
    Instance* inst = Runner_createInstance(runner, x, y, objectIndex);
    if (callerInst != nullptr && ctx->creatorVarID >= 0) {
        Instance_setSelfVar(inst, ctx->creatorVarID, RValue_makeReal((GMLReal) callerInst->instanceId));
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_relative(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    ctx->actionRelativeFlag = RValue_toInt32(args[0]) != 0;
    return RValue_makeUndefined();
}

// When the DnD "relative" checkbox is active and there is a calling instance, shifts (*x, *y) by that instance's position.
static void applyActionRelativeOffset(VMContext* ctx, float* x, float* y) {
    if (!ctx->actionRelativeFlag || ctx->currentInstance == nullptr) return;
    Instance* inst = ctx->currentInstance;
    *x += inst->x;
    *y += inst->y;
}

static RValue builtin_action_move(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    // action_move(direction_string, speed)
    // Direction string is 9 chars of '0'/'1' encoding a 3x3 direction grid:
    //   Pos: 0=UL(225) 1=U(270) 2=UR(315) 3=L(180) 4=STOP 5=R(0) 6=DL(135) 7=D(90) 8=DR(45)
    char* dirs = RValue_toString(args[0]);
    GMLReal spd = RValue_toReal(args[1]);

    static const GMLReal angles[] = {225, 270, 315, 180, -1, 0, 135, 90, 45};

    // Collect all enabled directions
    int candidates[9];
    int count = 0;
    for (int i = 0; 9 > i && dirs[i] != '\0'; i++) {
        if (dirs[i] == '1') {
            candidates[count++] = i;
        }
    }

    if (count == 0) {
        free(dirs);
        return RValue_makeUndefined();
    }

    // Pick one at random
    int pick = candidates[0 == count - 1 ? 0 : rand() % count];

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (4 == pick) {
            // STOP
            if (ctx->actionRelativeFlag) {
                inst->speed += (float) spd;
            } else {
                inst->speed = 0;
            }
        } else {
            GMLReal angle = angles[pick];
            if (ctx->actionRelativeFlag) {
                inst->direction += (float) angle;
                inst->speed += (float) spd;
            } else {
                inst->direction = (float) angle;
                inst->speed = (float) spd;
            }
        }
        Instance_computeComponentsFromSpeed(inst);
    }
    free(dirs);
    return RValue_makeUndefined();
}

static RValue builtin_action_move_to(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal ax = RValue_toReal(args[0]);
    GMLReal ay = RValue_toReal(args[1]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->x += (float) ax;
            inst->y += (float) ay;
        } else {
            inst->x = (float) ax;
            inst->y = (float) ay;
        }
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
    }
    return RValue_makeUndefined();
}

// action_move_start(): teleport the current instance back to its (xstart, ystart) spawn position.
static RValue builtin_action_move_start(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        inst->x = inst->xstart;
        inst->y = inst->ystart;
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
    }
    return RValue_makeUndefined();
}

// action_potential_step(x, y, stepsize, checkall): DnD wrapper around mp_potential_step that honors the "relative" checkbox by shifting (x, y) by the current instance's position.
static RValue builtin_action_potential_step(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal goalX = RValue_toReal(args[0]);
    GMLReal goalY = RValue_toReal(args[1]);
    GMLReal stepsize = RValue_toReal(args[2]);
    bool checkall = RValue_toBool(args[3]);
    if (ctx->actionRelativeFlag && ctx->currentInstance != nullptr) {
        goalX += ctx->currentInstance->x;
        goalY += ctx->currentInstance->y;
    }
    return builtinMpPotentialStepCommon(ctx, goalX, goalY, stepsize, INSTANCE_ALL, checkall);
}

// Tests whether the current instance can occupy (testX, testY) without colliding (useall=true checks all instances, false checks only solids).
static bool bounceTestFree(Runner* runner, Instance* inst, GMLReal testX, GMLReal testY, bool useall) {
    if (useall) {
        return placeEmptyAt(runner, inst, testX, testY);
    }
    return placeFreeAt(runner, inst, testX, testY);
}

static void moveBounceCommon(Runner* runner, Instance* inst, bool advanced, bool useall) {
    bool didBounce = false;
    if (!bounceTestFree(runner, inst, inst->x, inst->y, useall)) {
        inst->x = inst->xprevious;
        inst->y = inst->yprevious;
        SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
        didBounce = true;
    }

    GMLReal xx = inst->x;
    GMLReal yy = inst->y;

    if (advanced) {
        int32_t n = 18;
        GMLReal dir = 10.0 * GMLReal_round(inst->direction / 10.0);
        GMLReal ldir = dir;
        GMLReal rdir = dir;
        for (int32_t i = 1; 2 * n > i; i++) {
            ldir -= 180.0 / (GMLReal) n;
            GMLReal xn = xx + inst->speed * GMLReal_cos(ldir * (M_PI / 180.0));
            GMLReal yn = yy - inst->speed * GMLReal_sin(ldir * (M_PI / 180.0));
            if (bounceTestFree(runner, inst, xn, yn, useall)) {
                break;
            }
            didBounce = true;
        }
        for (int32_t i = 1; 2 * n > i; i++) {
            rdir += 180.0 / (GMLReal) n;
            GMLReal xn = xx + inst->speed * GMLReal_cos(rdir * (M_PI / 180.0));
            GMLReal yn = yy - inst->speed * GMLReal_sin(rdir * (M_PI / 180.0));
            if (bounceTestFree(runner, inst, xn, yn, useall)) {
                break;
            }
            didBounce = true;
        }
        if (didBounce) {
            inst->direction = (float) (180.0 + (ldir + rdir) - dir);
            Instance_computeComponentsFromSpeed(inst);
        }
    } else {
        bool canMoveH = bounceTestFree(runner, inst, inst->x + inst->hspeed, inst->y, useall);
        bool canMoveV = bounceTestFree(runner, inst, inst->x, inst->y + inst->vspeed, useall);
        bool canMoveDiagonally = bounceTestFree(runner, inst, inst->x + inst->hspeed, inst->y + inst->vspeed, useall);
        if (!canMoveH && !canMoveV) {
            inst->hspeed = -inst->hspeed;
            inst->vspeed = -inst->vspeed;
        } else if (canMoveH && canMoveV && !canMoveDiagonally) {
            inst->hspeed = -inst->hspeed;
            inst->vspeed = -inst->vspeed;
        } else if (!canMoveH) {
            inst->hspeed = -inst->hspeed;
        } else if (!canMoveV) {
            inst->vspeed = -inst->vspeed;
        }
        Instance_computeSpeedFromComponents(inst);
    }
}

// action_bounce(adv, against): DnD wrapper around move_bounce_solid / move_bounce_all.
// * adv (arg[0]): real, treated as bool via the native ">= 0.5" rule.
// * against (arg[1]): real menu pick: 0 = solid only, 1 = all instances (useall = (against == 1.0)).
static RValue builtin_action_bounce(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    bool advanced = RValue_toReal(args[0]) >= 0.5;
    bool useall = RValue_toReal(args[1]) == 1.0;
    moveBounceCommon(ctx->runner, ctx->currentInstance, advanced, useall);
    return RValue_makeUndefined();
}

static RValue builtin_move_bounce_solid(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || ctx->currentInstance == nullptr) return RValue_makeUndefined();
    bool advanced = RValue_toBool(args[0]);
    moveBounceCommon(ctx->runner, ctx->currentInstance, advanced, false);
    return RValue_makeUndefined();
}

static RValue builtin_move_bounce_all(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || ctx->currentInstance == nullptr) return RValue_makeUndefined();
    bool advanced = RValue_toBool(args[0]);
    moveBounceCommon(ctx->runner, ctx->currentInstance, advanced, true);
    return RValue_makeUndefined();
}

// Steps the current instance up to maxdist pixels in "dir" (degrees), stopping the unit before it would collide. useall=true tests all instances, false tests only solids.
static void moveContactCommon(Runner* runner, Instance* inst, GMLReal dir, GMLReal maxdist, bool useall) {
    int32_t steps = (maxdist <= 0.0) ? 1000 : (int32_t) GMLReal_bankersRound(maxdist);
    GMLReal rad = dir * (M_PI / 180.0);
    GMLReal dx = GMLReal_cos(rad);
    GMLReal dy = -GMLReal_sin(rad);
    if (!bounceTestFree(runner, inst, inst->x, inst->y, useall)) {
        return;
    }
    for (int32_t i = 1; steps >= i; i++) {
        GMLReal nx = inst->x + dx;
        GMLReal ny = inst->y + dy;
        if (!bounceTestFree(runner, inst, nx, ny, useall)) {
            return;
        }
        inst->x = (float) nx;
        inst->y = (float) ny;
        SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
    }
}

static RValue builtin_move_contact_solid(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    GMLReal dir = RValue_toReal(args[0]);
    GMLReal maxdist = RValue_toReal(args[1]);
    moveContactCommon(ctx->runner, ctx->currentInstance, dir, maxdist, false);
    return RValue_makeUndefined();
}

// action_move_contact(dir, maxdist, against): DnD wrapper around move_contact_solid / move_contact_all.
// * args[2] == 0: solid only
// * args[2] == 1: use all
static RValue builtin_action_move_contact(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    GMLReal dir = RValue_toReal(args[0]);
    GMLReal maxdist = RValue_toReal(args[1]);
    bool useall = RValue_toReal(args[2]) == 1.0;
    moveContactCommon(ctx->runner, ctx->currentInstance, dir, maxdist, useall);
    return RValue_makeUndefined();
}

// Moves the current instance up to maxdist pixels in "dir" (degrees) until it is no longer colliding (lands in a free spot). The inverse of moveContactCommon: if the current position is already free the instance is not moved. useall=true tests all instances, false tests only solids.
static void moveOutsideCommon(Runner* runner, Instance* inst, GMLReal dir, GMLReal maxdist, bool useall) {
    int32_t steps = (maxdist <= 0.0) ? 1000 : (int32_t) GMLReal_bankersRound(maxdist);
    GMLReal rad = dir * (M_PI / 180.0);
    GMLReal dx = GMLReal_cos(rad);
    GMLReal dy = -GMLReal_sin(rad);
    if (bounceTestFree(runner, inst, inst->x, inst->y, useall)) {
        return;
    }
    for (int32_t i = 1; steps >= i; i++) {
        inst->x = (float) (inst->x + dx);
        inst->y = (float) (inst->y + dy);
        SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
        if (bounceTestFree(runner, inst, inst->x, inst->y, useall)) {
            return;
        }
    }
}

static RValue builtin_move_outside_solid(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    GMLReal dir = RValue_toReal(args[0]);
    GMLReal maxdist = RValue_toReal(args[1]);
    moveOutsideCommon(ctx->runner, ctx->currentInstance, dir, maxdist, false);
    return RValue_makeUndefined();
}

static RValue builtin_move_outside_all(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    GMLReal dir = RValue_toReal(args[0]);
    GMLReal maxdist = RValue_toReal(args[1]);
    moveOutsideCommon(ctx->runner, ctx->currentInstance, dir, maxdist, true);
    return RValue_makeUndefined();
}

static RValue builtin_action_snap(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal hsnap = RValue_toReal(args[0]);
    GMLReal vsnap = RValue_toReal(args[1]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (hsnap > 0.0) {
            inst->x = (float) ((int32_t) GMLReal_round(inst->x / hsnap) * hsnap);
            SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
        }
        if (vsnap > 0.0) {
            inst->y = (float) ((int32_t) GMLReal_round(inst->y / vsnap) * vsnap);
            SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_friction(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal val = RValue_toReal(args[0]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->friction += (float) val;
        } else {
            inst->friction = (float) val;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_gravity(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal dir = RValue_toReal(args[0]);
    GMLReal grav = RValue_toReal(args[1]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->gravityDirection += (float) dir;
            inst->gravity += (float) grav;
        } else {
            inst->gravityDirection = (float) dir;
            inst->gravity = (float) grav;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_hspeed(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal val = RValue_toReal(args[0]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->hspeed += (float) val;
        } else {
            inst->hspeed = (float) val;
        }
        Instance_computeSpeedFromComponents(inst);
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_vspeed(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal val = RValue_toReal(args[0]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->vspeed += (float) val;
        } else {
            inst->vspeed = (float) val;
        }
        Instance_computeSpeedFromComponents(inst);
    }
    return RValue_makeUndefined();
}

// ===[ GML BUFFER SYSTEM ]===

static int32_t gmlBufferCreate(Runner* runner, int32_t size, int32_t type, int32_t alignment) {
    GmlBuffer buf = {0};
    buf.size = size > 0 ? size : 1;
    buf.data = safeCalloc((size_t) buf.size, 1);
    buf.position = 0;
    buf.usedSize = (type == GML_BUFFER_GROW) ? 0 : buf.size;
    buf.alignment = alignment > 0 ? alignment : 1;
    buf.type = type;
    buf.isValid = true;
    int32_t id = (int32_t) arrlen(runner->gmlBufferPool);
    arrput(runner->gmlBufferPool, buf);
    return id;
}

static GmlBuffer* gmlBufferGet(Runner* runner, int32_t id) {
    if (0 > id || id >= (int32_t) arrlen(runner->gmlBufferPool)) return nullptr;
    GmlBuffer* buf = &runner->gmlBufferPool[id];
    if (!buf->isValid) return nullptr;
    return buf;
}

// Aligns position up to the buffer's alignment boundary
static int32_t gmlBufferAlign(int32_t position, int32_t alignment) {
    if (1 >= alignment) return position;
    return ((position + alignment - 1) / alignment) * alignment;
}

// Ensures the grow buffer has at least newSize bytes allocated
static void gmlBufferEnsureSize(GmlBuffer* buf, int32_t newSize) {
    if (buf->type != GML_BUFFER_GROW || newSize <= buf->size) return;
    // Double or use newSize, whichever is larger
    int32_t newAlloc = buf->size * 2;
    if (newAlloc < newSize) newAlloc = newSize;
    buf->data = safeRealloc(buf->data, (size_t) newAlloc);
    memset(buf->data + buf->size, 0, (size_t) (newAlloc - buf->size));
    buf->size = newAlloc;
}

static RValue builtin_buffer_create(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t size = RValue_toInt32(args[0]);
    int32_t type = RValue_toInt32(args[1]);
    int32_t alignment = RValue_toInt32(args[2]);
    int32_t id = gmlBufferCreate(runner, size, type, alignment);
    return RValue_makeReal((GMLReal) id);
}

static RValue builtin_buffer_delete(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf != nullptr) {
        free(buf->data);
        buf->data = nullptr;
        buf->isValid = false;
    }
    return RValue_makeUndefined();
}

static RValue builtin_buffer_write(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t dataType = RValue_toInt32(args[1]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr) return RValue_makeUndefined();

    switch (dataType) {
        case GML_BUFTYPE_U8:
        case GML_BUFTYPE_BOOL: {
            uint8_t val = (uint8_t) RValue_toInt32(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 1);
            if (buf->size > buf->position) buf->data[buf->position] = val;
            buf->position += 1;
            break;
        }
        case GML_BUFTYPE_S8: {
            int8_t val = (int8_t) RValue_toInt32(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 1);
            if (buf->size > buf->position) buf->data[buf->position] = (uint8_t) val;
            buf->position += 1;
            break;
        }
        case GML_BUFTYPE_U16: {
            uint16_t val = (uint16_t) RValue_toInt32(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 2);
            if (buf->position + 2 <= buf->size) {
                BinaryUtils_writeUint16(buf->data + buf->position, val);
            }
            buf->position += 2;
            break;
        }
        case GML_BUFTYPE_S16: {
            int16_t val = (int16_t) RValue_toInt32(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 2);
            if (buf->position + 2 <= buf->size) {
                BinaryUtils_writeUint16(buf->data + buf->position, (uint16_t) val);
            }
            buf->position += 2;
            break;
        }
        case GML_BUFTYPE_U32:
        case GML_BUFTYPE_S32: {
            int32_t val = RValue_toInt32(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 4);
            if (buf->position + 4 <= buf->size) {
                BinaryUtils_writeUint32(buf->data + buf->position, (uint32_t) val);
            }
            buf->position += 4;
            break;
        }
        case GML_BUFTYPE_F32: {
            float val = (float) RValue_toReal(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 4);
            if (buf->position + 4 <= buf->size) {
                BinaryUtils_writeFloat32(buf->data + buf->position, val);
            }
            buf->position += 4;
            break;
        }
        case GML_BUFTYPE_F64: {
            double val = (double) RValue_toReal(args[2]);
            gmlBufferEnsureSize(buf, buf->position + 8);
            if (buf->position + 8 <= buf->size) {
                BinaryUtils_writeFloat64(buf->data + buf->position, val);
            }
            buf->position += 8;
            break;
        }
        case GML_BUFTYPE_STRING: {
            // Writes string bytes + null terminator
            char* str = RValue_toString(args[2]);
            int32_t len = (int32_t) strlen(str);
            int32_t writeLen = len + 1; // include null terminator
            gmlBufferEnsureSize(buf, buf->position + writeLen);
            if (buf->position + writeLen <= buf->size) {
                memcpy(buf->data + buf->position, str, (size_t) writeLen);
            }
            buf->position += writeLen;
            free(str);
            break;
        }
        case GML_BUFTYPE_TEXT: {
            // Writes string bytes WITHOUT null terminator
            char* str = RValue_toString(args[2]);
            int32_t len = (int32_t) strlen(str);
            gmlBufferEnsureSize(buf, buf->position + len);
            if (buf->position + len <= buf->size) {
                memcpy(buf->data + buf->position, str, (size_t) len);
            }
            buf->position += len;
            free(str);
            break;
        }
        default:
            fprintf(stderr, "buffer_write: unsupported data type %d\n", dataType);
            break;
    }

    buf->position = gmlBufferAlign(buf->position, buf->alignment);
    if (buf->type == GML_BUFFER_GROW && buf->position > buf->usedSize) {
        buf->usedSize = buf->position;
    }

    return RValue_makeUndefined();
}

static RValue builtin_buffer_read(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t dataType = RValue_toInt32(args[1]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr) return RValue_makeReal(0.0);

    RValue result = RValue_makeReal(0.0);

    switch (dataType) {
        case GML_BUFTYPE_U8:
        case GML_BUFTYPE_BOOL: {
            if (buf->size > buf->position) {
                result = RValue_makeReal((GMLReal) buf->data[buf->position]);
            }
            buf->position += 1;
            break;
        }
        case GML_BUFTYPE_S8: {
            if (buf->size > buf->position) {
                result = RValue_makeReal((GMLReal) (int8_t) buf->data[buf->position]);
            }
            buf->position += 1;
            break;
        }
        case GML_BUFTYPE_U16: {
            if (buf->position + 2 <= buf->size) {
                uint16_t val = BinaryUtils_readUint16(buf->data + buf->position);
                result = RValue_makeReal((GMLReal) val);
            }
            buf->position += 2;
            break;
        }
        case GML_BUFTYPE_S16: {
            if (buf->position + 2 <= buf->size) {
                result = RValue_makeReal((GMLReal) BinaryUtils_readInt16(buf->data + buf->position));
            }
            buf->position += 2;
            break;
        }
        case GML_BUFTYPE_U32: {
            if (buf->position + 4 <= buf->size) {
                uint32_t val = BinaryUtils_readUint32(buf->data + buf->position);
                result = RValue_makeReal((GMLReal) val);
            }
            buf->position += 4;
            break;
        }
        case GML_BUFTYPE_S32: {
            if (buf->position + 4 <= buf->size) {
                result = RValue_makeReal((GMLReal) BinaryUtils_readInt32(buf->data + buf->position));
            }
            buf->position += 4;
            break;
        }
        case GML_BUFTYPE_F32: {
            if (buf->position + 4 <= buf->size) {
                float val = BinaryUtils_readFloat32(buf->data + buf->position);
                result = RValue_makeReal((GMLReal) val);
            }
            buf->position += 4;
            break;
        }
        case GML_BUFTYPE_F64: {
            if (buf->position + 8 <= buf->size) {
                double val = BinaryUtils_readFloat64(buf->data + buf->position);
                result = RValue_makeReal((GMLReal) val);
            }
            buf->position += 8;
            break;
        }
        case GML_BUFTYPE_STRING: {
            // Read until null terminator or end of buffer
            int32_t start = buf->position;
            while (buf->size > buf->position && buf->data[buf->position] != '\0') {
                buf->position++;
            }
            int32_t len = buf->position - start;
            char* str = safeMalloc((size_t) len + 1);
            memcpy(str, buf->data + start, (size_t) len);
            str[len] = '\0';
            // Skip past the null terminator
            if (buf->size > buf->position) buf->position++;
            result = RValue_makeOwnedString(str);
            break;
        }
        case GML_BUFTYPE_TEXT: {
            // Read all remaining bytes as text (no null terminator delimiter)
            int32_t start = buf->position;
            int32_t len = buf->size - start;
            if (0 > len) len = 0;
            char* str = safeMalloc((size_t) len + 1);
            if (len > 0) memcpy(str, buf->data + start, (size_t) len);
            str[len] = '\0';
            buf->position = buf->size;
            result = RValue_makeOwnedString(str);
            break;
        }
        default:
            fprintf(stderr, "buffer_read: unsupported data type %d\n", dataType);
            break;
    }

    buf->position = gmlBufferAlign(buf->position, buf->alignment);
    return result;
}

static RValue builtin_buffer_seek(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t seekMode = RValue_toInt32(args[1]);
    int32_t offset = RValue_toInt32(args[2]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr) return RValue_makeUndefined();

    switch (seekMode) {
        case GML_BUFFER_SEEK_START:
            buf->position = offset;
            break;
        case GML_BUFFER_SEEK_RELATIVE:
            buf->position += offset;
            break;
        case GML_BUFFER_SEEK_END: {
            int32_t endPos = (buf->type == GML_BUFFER_GROW) ? buf->usedSize : buf->size;
            buf->position = endPos + offset;
            break;
        }
    }

    // Clamp position
    if (0 > buf->position) buf->position = 0;
    if (buf->position > buf->size) buf->position = buf->size;

    return RValue_makeUndefined();
}

static RValue builtin_buffer_tell(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) buf->position);
}

static RValue builtin_buffer_get_size(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ((buf->type == GML_BUFFER_GROW) ? buf->usedSize : buf->size));
}

static RValue builtin_buffer_load(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;
    char* filename = RValue_toString(args[0]);

    uint8_t* fileData = nullptr;
    int32_t fileSize = 0;
    bool ok = fs->vtable->readFileBinary(fs, filename, &fileData, &fileSize);
    free(filename);

    if (!ok) return RValue_makeReal(-1.0);

    // Create a fixed buffer with the loaded data
    int32_t id = gmlBufferCreate(runner, fileSize, GML_BUFFER_FIXED, 1);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    free(buf->data);
    buf->data = fileData;
    buf->size = fileSize;
    buf->usedSize = fileSize;
    return RValue_makeReal((GMLReal) id);
}

static RValue builtin_buffer_save(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;
    int32_t id = RValue_toInt32(args[0]);
    char* filename = RValue_toString(args[1]);
    GmlBuffer* buf = gmlBufferGet(runner, id);

    if (buf != nullptr) {
        int32_t saveSize = (buf->type == GML_BUFFER_GROW) ? buf->usedSize : buf->size;
        fs->vtable->writeFileBinary(fs, filename, buf->data, saveSize);
    }

    free(filename);
    return RValue_makeUndefined();
}

static RValue builtin_buffer_save_ext(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;
    int32_t id = RValue_toInt32(args[0]);
    char* filename = RValue_toString(args[1]);
    int32_t offset = RValue_toInt32(args[2]);
    int32_t size = RValue_toInt32(args[3]);
    GmlBuffer* buf = gmlBufferGet(runner, id);

    if (buf != nullptr) {
        int32_t maxBoundary = (buf->type == GML_BUFFER_GROW) ? buf->usedSize : buf->size;

        // These are checks that the original runner does
        if (0 > offset) offset = 0;
        if (0 > size) size = maxBoundary;
        if (offset + size > maxBoundary) size = maxBoundary - offset;

        if (maxBoundary > offset) {
            if (offset + size > maxBoundary) {
                size = maxBoundary - offset;
            }

            fs->vtable->writeFileBinary(fs, filename, buf->data + offset, size);
        }
    }

    free(filename);
    return RValue_makeUndefined();
}

// ===[ Async buffer save/load ]===

// Resolves the on-disk path for an async buffer op: "<group>/<filename>" when a non-empty group name is set, otherwise just "<filename>". Caller owns the returned string.
static char* gmlAsyncBufferResolvePath(const char* groupName, const char* filename) {
    if (groupName == nullptr || groupName[0] == '\0') return safeStrdup(filename);
    size_t length = strlen(groupName) + 1 + strlen(filename) + 1;
    char* path = safeMalloc(length);
    snprintf(path, length, "%s/%s", groupName, filename);
    return path;
}

// Performs a single async buffer op against the file system. Returns true on success.
static bool gmlAsyncBufferRunOp(Runner* runner, const AsyncBufferOp* op, const char* groupName) {
    FileSystem* fs = runner->fileSystem;
    GmlBuffer* buf = gmlBufferGet(runner, op->bufferId);
    if (buf == nullptr) return false;

    char* path = gmlAsyncBufferResolvePath(groupName, op->filename);
    bool ok = false;

    if (op->isSave) {
        int32_t maxBoundary = (buf->type == GML_BUFFER_GROW) ? buf->usedSize : buf->size;
        int32_t offset = op->offset < 0 ? 0 : op->offset;
        int32_t size = op->size;
        if (size < 0) size = maxBoundary - offset;
        if (offset > maxBoundary) size = 0;
        else if (offset + size > maxBoundary) size = maxBoundary - offset;
        if (size < 0) size = 0;
        ok = fs->vtable->writeFileBinary(fs, path, buf->data + offset, size);
    } else {
        uint8_t* fileData = nullptr;
        int32_t fileSize = 0;
        if (fs->vtable->readFileBinary(fs, path, &fileData, &fileSize)) {
            int32_t offset = op->offset < 0 ? 0 : op->offset;
            int32_t copySize = (op->size < 0 || op->size > fileSize) ? fileSize : op->size;
            // CopyMemoryToBuffer preserves the read/write cursor, so save and restore it around the copy.
            int32_t savedPosition = buf->position;
            gmlBufferEnsureSize(buf, offset + copySize);
            if (copySize > 0 && buf->size >= offset + copySize) {
                memcpy(buf->data + offset, fileData, (size_t) copySize);
            }
            if (buf->type == GML_BUFFER_GROW && offset + copySize > buf->usedSize) {
                buf->usedSize = offset + copySize;
            }
            buf->position = savedPosition;
            free(fileData);
            ok = true;
        }
    }

    free(path);
    return ok;
}

// Runs a batch of async buffer ops immediately, then queues a single completion that fires the "Async - Save/Load" event on a later frame. Returns the request id (also the async_load "id").
static int32_t gmlAsyncBufferKick(Runner* runner, AsyncBufferOp* ops, int32_t opCount, const char* groupName) {
    int32_t requestId = runner->asyncBufferNextRequestId++;
    bool allOk = true;
    repeat(opCount, i) {
        if (!gmlAsyncBufferRunOp(runner, &ops[i], groupName)) allOk = false;
    }
    AsyncSaveLoadCompletion completion = {0};
    completion.requestId = requestId;
    completion.status = allOk ? 1 : 0;
    arrput(runner->asyncSaveLoadQueue, completion);
    return requestId;
}

// Shared by buffer_load_async and buffer_save_async: builds the op and either accumulates it into the open group (returning -1) or kicks it immediately (returning the request id).
static RValue gmlBufferAsyncEnqueue(Runner* runner, RValue* args, bool isSave) {
    AsyncBufferOp op = {0};
    op.bufferId = RValue_toInt32(args[0]);
    op.filename = RValue_toString(args[1]); // owned
    op.offset = RValue_toInt32(args[2]);
    op.size = RValue_toInt32(args[3]);
    op.isSave = isSave;

    if (runner->asyncBufferGroupActive) {
        arrput(runner->asyncBufferGroupOps, op); // buffer_async_group_end frees op.filename
        return RValue_makeReal(-1.0); // native returns -1 while a group is open
    }

    // No open group: kick this single op right away.
    int32_t requestId = gmlAsyncBufferKick(runner, &op, 1, nullptr);
    free(op.filename);
    return RValue_makeReal((GMLReal) requestId);
}

static RValue builtin_buffer_load_async(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    return gmlBufferAsyncEnqueue(ctx->runner, args, false);
}

static RValue builtin_buffer_save_async(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    return gmlBufferAsyncEnqueue(ctx->runner, args, true);
}

static RValue builtin_buffer_async_group_begin(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->asyncBufferGroupActive) {
        fprintf(stderr, "buffer_async_group_begin: a buffer group is already open\n");
        return RValue_makeUndefined();
    }
    free(runner->asyncBufferGroupName);
    runner->asyncBufferGroupName = (argCount > 0) ? RValue_toString(args[0]) : safeStrdup("");
    runner->asyncBufferGroupActive = true;
    return RValue_makeUndefined();
}

static RValue builtin_buffer_async_group_end(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (!runner->asyncBufferGroupActive) {
        fprintf(stderr, "buffer_async_group_end: no matching buffer_async_group_begin\n");
        return RValue_makeReal(-1.0);
    }

    int32_t opCount = (int32_t) arrlen(runner->asyncBufferGroupOps);
    int32_t requestId = -1;
    if (opCount > 0) {
        requestId = gmlAsyncBufferKick(runner, runner->asyncBufferGroupOps, opCount, runner->asyncBufferGroupName);
    }

    repeat(opCount, i) {
        free(runner->asyncBufferGroupOps[i].filename);
    }
    arrfree(runner->asyncBufferGroupOps);
    runner->asyncBufferGroupOps = nullptr;

    free(runner->asyncBufferGroupName);
    runner->asyncBufferGroupName = nullptr;
    runner->asyncBufferGroupActive = false;

    return RValue_makeReal((GMLReal) requestId);
}

// filename_change_ext(fname, newext): changes the extension of fname to newext
// (see GameMaker-HTML5 Function_File.js for reference)
static RValue builtin_filename_change_ext(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    char* fname = RValue_toString(args[0]);
    char* newext = RValue_toString(args[1]); // includes the ., example: ".gmk"

    char *last = strrchr(fname, '.');

    if (last != nullptr && last != 0) {
        long index = last - fname;
        char* new_name = safeMalloc(index + strlen(newext) + 1);
        memcpy(new_name, fname, (size_t) index);
        memcpy(new_name + index, newext, (size_t) strlen(newext));
        new_name[index + strlen(newext)] = '\0';
        RValue result = RValue_makeOwnedString(new_name);

        free(fname);
        free(newext);

        return result;
    }

    free(newext);

    // If there isn't a dot, we return the original string as is
    return RValue_makeOwnedString(fname);
}

static RValue builtin_buffer_base64_encode(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (3 > argCount) return RValue_makeOwnedString(safeStrdup(""));

    int32_t id = RValue_toInt32(args[0]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr) return RValue_makeOwnedString(safeStrdup(""));

    int32_t offset = RValue_toInt32(args[1]);
    size_t size = RValue_toInt32(args[2]);

    int32_t maxBoundary = (buf->type == GML_BUFFER_GROW) ? buf->usedSize : buf->size;

    if (offset < 0 || offset >= maxBoundary || size <= 0) {
        return RValue_makeOwnedString(safeStrdup(""));
    }

    if (offset + size > (size_t)maxBoundary) {
        size = (size_t)(maxBoundary - offset);
    }

    char* out = safeMalloc(BASE64_ENCODE_OUT_SIZE(size));
    base64_encode((const unsigned char*) buf->data + offset, size, out);
    return RValue_makeOwnedString(out);
}

static RValue builtin_buffer_base64_decode(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (2 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* input = RValue_toString(args[1]);
    unsigned int inLen = (unsigned int) strlen(input);
    size_t outLen = BASE64_DECODE_OUT_SIZE(inLen);
    uint8_t* out = safeMalloc(outLen);
    base64_decode(input, inLen, out);
    free(input);
    int32_t id = gmlBufferCreate(runner, outLen, GML_BUFFER_GROW, 1);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    free(buf->data);
    buf->data = out;
    buf->size = outLen;
    buf->usedSize = outLen;
    return RValue_makeReal((GMLReal) id);
}

static RValue builtin_base64_encode(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* input = RValue_toString(args[0]);
    unsigned int inLen = (unsigned int) strlen(input);
    char* out = safeMalloc(BASE64_ENCODE_OUT_SIZE(inLen));
    base64_encode((const unsigned char*) input, inLen, out);
    free(input);
    return RValue_makeOwnedString(out);
}

static RValue builtin_base64_decode(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup(""));
    char* input = RValue_toString(args[0]);
    unsigned int inLen = (unsigned int) strlen(input);
    unsigned int outCap = BASE64_DECODE_OUT_SIZE(inLen);
    unsigned char* out = safeMalloc(outCap + 1);
    unsigned int outLen = base64_decode(input, inLen, out);
    out[outLen] = '\0';
    free(input);
    return RValue_makeOwnedString((char*) out);
}

// buffer_md5(buffer, offset, size) -> hex string (32 chars, lowercase). Uses the RFC 1321 reference impl in vendor/md5.
static RValue builtin_buffer_md5(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t offset = RValue_toInt32(args[1]);
    int32_t size = RValue_toInt32(args[2]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr || 0 > offset || 0 > size) return RValue_makeOwnedString(safeStrdup(""));
    if (offset + size > buf->size) {
        if (buf->size > offset) size = buf->size - offset; else size = 0;
    }

    MD5_CTX mctx;
    MD5Init(&mctx);
    if (size > 0) MD5Update(&mctx, buf->data + offset, (unsigned int) size);
    unsigned char digest[16];
    MD5Final(digest, &mctx);

    char* hex = safeMalloc(33);
    for (int32_t i = 0; 16 > i; i++) {
        sprintf(&hex[i * 2], "%02x", digest[i]);
    }
    hex[32] = '\0';
    return RValue_makeOwnedString(hex);
}

// buffer_sha1(buffer, offset, size) -> hex string (40 chars, lowercase). Uses Steve Reid's C implementation in vendor/sha1.
static RValue builtin_buffer_sha1(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    int32_t offset = RValue_toInt32(args[1]);
    int32_t size = RValue_toInt32(args[2]);
    GmlBuffer* buf = gmlBufferGet(runner, id);
    if (buf == nullptr || 0 > offset || 0 > size) return RValue_makeOwnedString(safeStrdup(""));
    if (offset + size > buf->size) {
        if (buf->size > offset) size = buf->size - offset; else size = 0;
    }

    SHA1_CTX sctx;
    SHA1Init(&sctx);
    if (size > 0) SHA1Update(&sctx, buf->data + offset, (unsigned int) size);
    unsigned char digest[20];
    SHA1Final(digest, &sctx);

    char* hex = safeMalloc(41);
    for (int32_t i = 0; 20 > i; i++) {
        sprintf(&hex[i * 2], "%02x", digest[i]);
    }
    hex[40] = '\0';
    return RValue_makeOwnedString(hex);
}

// buffer_get_surface(buffer, surface, offset) -> bool
// Reads RGBA8 pixels from the surface into the buffer at the given offset.
static RValue builtin_buffer_get_surface(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t bufId = RValue_toInt32(args[0]);
    int32_t surfaceId = RValue_toInt32(args[1]);
    int32_t offset = RValue_toInt32(args[2]);
    GmlBuffer* buf = gmlBufferGet(runner, bufId);
    if (buf == nullptr || runner->renderer == nullptr) return RValue_makeBool(false);
    if (runner->renderer->vtable->surfaceGetPixels == nullptr) return RValue_makeBool(false);
    if (!Renderer_surfaceExists(runner->renderer, surfaceId)) return RValue_makeBool(false);

    int32_t w = (int32_t) Renderer_getSurfaceWidth(runner->renderer, surfaceId);
    int32_t h = (int32_t) Renderer_getSurfaceHeight(runner->renderer, surfaceId);
    if (0 >= w || 0 >= h) return RValue_makeBool(false);
    int32_t bytes = w * h * 4;

    if (0 > offset) return RValue_makeBool(false);
    gmlBufferEnsureSize(buf, offset + bytes);
    if (offset + bytes > buf->size) return RValue_makeBool(false);

    bool ok = runner->renderer->vtable->surfaceGetPixels(runner->renderer, surfaceId, buf->data + offset);
    if (ok && buf->type == GML_BUFFER_GROW && offset + bytes > buf->usedSize) buf->usedSize = offset + bytes;
    return RValue_makeBool(ok);
}

// PSN stubs
STUB_RETURN_UNDEFINED(psn_init)
STUB_RETURN_UNDEFINED(psn_init_np_libs)
STUB_RETURN_ZERO(psn_default_user)
STUB_RETURN_ZERO(psn_get_leaderboard_score)

static RValue builtin_psn_setup_trophies(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    // Always tells the runner that trophies have been set up successfully
    return RValue_makeInt32(1);
}

// Draw functions
static RValue builtin_draw_sprite(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);

    // If subimg < 0, use the current instance's imageIndex
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSprite(runner->renderer, spriteIndex, subimg, x, y);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);
    float xscale = (float) RValue_toReal(args[4]);
    float yscale = (float) RValue_toReal(args[5]);
    float rot = (float) RValue_toReal(args[6]);
    uint32_t color = (uint32_t) RValue_toInt32(args[7]);
    float alpha = (float) RValue_toReal(args[8]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpriteExt(runner->renderer, spriteIndex, subimg, x, y, xscale, yscale, rot, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_tiled(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    float roomW = (float) runner->currentRoom->width;
    float roomH = (float) runner->currentRoom->height;
    Renderer_drawSpriteTiled(runner->renderer, spriteIndex, subimg, x, y, 1.0f, 1.0f, roomW, roomH, 0xFFFFFF, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_tiled_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);
    float xscale = (float) RValue_toReal(args[4]);
    float yscale = (float) RValue_toReal(args[5]);
    uint32_t color = (uint32_t) RValue_toInt32(args[6]);
    float alpha = (float) RValue_toReal(args[7]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    float roomW = (float) runner->currentRoom->width;
    float roomH = (float) runner->currentRoom->height;
    Renderer_drawSpriteTiled(runner->renderer, spriteIndex, subimg, x, y, xscale, yscale, roomW, roomH, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_stretched(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);
    float w = (float) RValue_toReal(args[4]);
    float h = (float) RValue_toReal(args[5]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpriteStretched(runner->renderer, spriteIndex, subimg, x, y, w, h, 0xFFFFFF, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_stretched_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);
    float w = (float) RValue_toReal(args[4]);
    float h = (float) RValue_toReal(args[5]);
    uint32_t color = (uint32_t) RValue_toInt32(args[6]);
    float alpha = (float) RValue_toReal(args[7]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpriteStretched(runner->renderer, spriteIndex, subimg, x, y, w, h, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_part(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    int32_t left = RValue_toInt32(args[2]);
    int32_t top = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    int32_t height = RValue_toInt32(args[5]);
    float x = (float) RValue_toReal(args[6]);
    float y = (float) RValue_toReal(args[7]);

    // If subimg < 0, use the current instance's imageIndex
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpritePart(runner->renderer, spriteIndex, subimg, left, top, width, height, x, y);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_part_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    int32_t left = RValue_toInt32(args[2]);
    int32_t top = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    int32_t height = RValue_toInt32(args[5]);
    float x = (float) RValue_toReal(args[6]);
    float y = (float) RValue_toReal(args[7]);
    float xscale = (float) RValue_toReal(args[8]);
    float yscale = (float) RValue_toReal(args[9]);
    uint32_t color = (uint32_t) RValue_toInt32(args[10]);
    float alpha = (float) RValue_toReal(args[11]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpritePartExt(runner->renderer, spriteIndex, subimg, left, top, width, height, x, y, xscale, yscale, 0.0f, 0.0f, 0.0f, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_sprite_general(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    logSemiStubbedFunction(ctx, "draw_sprite_general");
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    int32_t left = RValue_toInt32(args[2]);
    int32_t top = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    int32_t height = RValue_toInt32(args[5]);
    float x = (float) RValue_toReal(args[6]);
    float y = (float) RValue_toReal(args[7]);
    float xscale = (float) RValue_toReal(args[8]);
    float yscale = (float) RValue_toReal(args[9]);
    float rot = (float) RValue_toReal(args[10]);
    uint32_t c1 = (uint32_t) RValue_toInt32(args[11]);
    float alpha = (float) RValue_toReal(args[15]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpritePartExt(runner->renderer, spriteIndex, subimg, left, top, width, height, x, y, xscale, yscale, rot, x, y, c1, alpha);
    return RValue_makeUndefined();
}


static RValue builtin_draw_sprite_pos(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x1 = (float) RValue_toReal(args[2]);
    float y1 = (float) RValue_toReal(args[3]);
    float x2 = (float) RValue_toReal(args[4]);
    float y2 = (float) RValue_toReal(args[5]);
    float x3 = (float) RValue_toReal(args[6]);
    float y3 = (float) RValue_toReal(args[7]);
    float x4 = (float) RValue_toReal(args[8]);
    float y4 = (float) RValue_toReal(args[9]);
    float alpha = (float) RValue_toReal(args[10]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }

    Renderer_drawSpritePos(runner->renderer, spriteIndex, subimg, x1, y1, x2, y2, x3, y3, x4, y4, alpha);

    return RValue_makeUndefined();
}

static RValue builtin_draw_rectangle(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x1 = (float) RValue_toReal(args[0]);
    float y1 = (float) RValue_toReal(args[1]);
    float x2 = (float) RValue_toReal(args[2]);
    float y2 = (float) RValue_toReal(args[3]);
    bool outline = RValue_toBool(args[4]);
    runner->renderer->vtable->drawRectangle(runner->renderer, x1, y1, x2, y2, runner->renderer->drawColor, runner->renderer->drawAlpha, outline);
    return RValue_makeUndefined();
}

static RValue builtin_draw_rectangle_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x1 = (float) RValue_toReal(args[0]);
    float y1 = (float) RValue_toReal(args[1]);
    float x2 = (float) RValue_toReal(args[2]);
    float y2 = (float) RValue_toReal(args[3]);
    uint32_t color1 = (uint32_t) RValue_toInt32(args[4]);
    uint32_t color2 = (uint32_t) RValue_toInt32(args[5]);
    uint32_t color3 = (uint32_t) RValue_toInt32(args[6]);
    uint32_t color4 = (uint32_t) RValue_toInt32(args[7]);
    bool outline = RValue_toBool(args[8]);

    runner->renderer->vtable->drawRectangleColor(runner->renderer, x1, y1, x2, y2, color1, color2, color3, color4, runner->renderer->drawAlpha, outline);
    return RValue_makeUndefined();
}

static RValue builtin_draw_healthbar(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x1 = (float) RValue_toReal(args[0]);
    float y1 = (float) RValue_toReal(args[1]);
    float x2 = (float) RValue_toReal(args[2]);
    float y2 = (float) RValue_toReal(args[3]);
    float amount = (float) RValue_toReal(args[4]);

    amount = amount / (float)100; // 0 - 1;
    float healthbarX = (x1 * (1-amount) + x2 * amount);
    //float healthbarY = (y1 * (1-amount) + y2 * amount);

    uint32_t backCol = (uint32_t) RValue_toInt32(args[5]);
    uint32_t minCol = (uint32_t) RValue_toInt32(args[6]);
    uint32_t maxCol = (uint32_t) RValue_toInt32(args[7]);
    uint32_t intermediateColor = (uint32_t) Color_lerp((int32_t) minCol, (int32_t) maxCol, amount);

    bool showBack = RValue_toBool(args[9]);

    if (showBack) {
        runner->renderer->vtable->drawRectangle(runner->renderer, x1,y1,x2,y2,backCol, runner->renderer->drawAlpha, false);
    }

    runner->renderer->vtable->drawRectangle(runner->renderer,x1,y1,healthbarX,y2,intermediateColor, runner->renderer->drawAlpha, false);
    return RValue_makeUndefined();
}

static RValue builtin_draw_set_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawColor = (uint32_t) RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_clear(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        uint32_t color = (uint32_t) RValue_toInt32(args[0]);
        runner->renderer->vtable->clearScreen(runner->renderer, color, 1.0f);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_clear_alpha(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        uint32_t color = (uint32_t) RValue_toInt32(args[0]);
        float alpha = RValue_toReal(args[1]);
        runner->renderer->vtable->clearScreen(runner->renderer, color, alpha);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_set_alpha(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawAlpha = (float) RValue_toReal(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_set_font(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawFont = RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_set_halign(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawHalign = RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_set_valign(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawValign = RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_text(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    runner->renderer->vtable->drawText(runner->renderer, processedText.text, x, y, 1.0f, 1.0f, 0.0f, -1.0f);
    PreprocessedText_free(processedText);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_draw_text_transformed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    float angle = (float) RValue_toReal(args[5]);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    runner->renderer->vtable->drawText(runner->renderer, processedText.text, x, y, xscale, yscale, angle, -1.0f);
    PreprocessedText_free(processedText);
    free(str);
    return RValue_makeUndefined();
}

// Drives draw_text_ext / draw_text_ext_transformed by wrapping the (preprocessed) text and forwarding to drawText.
// Disable wrapping with 0 > "width", keep the font default line stride with 0 > "separation".
static void drawTextExtCommonColor(Runner* runner, const char* str, float x, float y, float xscale, float yscale, float angle, int32_t separation, int32_t width, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4) {
    int32_t fontIndex = runner->renderer->drawFont;
    if (0 > fontIndex || (uint32_t) fontIndex >= runner->dataWin->font.count) return;
    Font* font = &runner->dataWin->font.fonts[fontIndex];

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    PreprocessedText wrappedText = TextUtils_wrapText(font, processedText.text, width);
    if (c1 == c2 && c2 == c3 && c3 == c4 && c4 == runner->renderer->drawColor) {
        // using the ordinary drawText is safe
        runner->renderer->vtable->drawText(runner->renderer, wrappedText.text, x, y, xscale, yscale, angle, (float) separation);
    } else {
        runner->renderer->vtable->drawTextColor(runner->renderer, wrappedText.text, x, y, xscale, yscale, angle, c1, c2, c3, c4, 1.0f, (float) separation);
    }
    PreprocessedText_free(wrappedText);
    PreprocessedText_free(processedText);
}
static void drawTextExtCommon(Runner* runner, const char* str, float x, float y, float xscale, float yscale, float angle, int32_t separation, int32_t width) {
    uint32_t fill = runner->renderer->drawColor;
    drawTextExtCommonColor(runner, str, x, y, xscale, yscale, angle, separation, width, fill, fill, fill, fill);
}


static RValue builtin_draw_text_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    int32_t separation = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);

    drawTextExtCommon(runner, str, x, y, 1.0f, 1.0f, 0.0f, separation, width);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_draw_text_ext_transformed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    int32_t separation = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    float xscale = (float) RValue_toReal(args[5]);
    float yscale = (float) RValue_toReal(args[6]);
    float angle = (float) RValue_toReal(args[7]);

    drawTextExtCommon(runner, str, x, y, xscale, yscale, angle, separation, width);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_draw_text_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    int32_t c1 = (float) RValue_toInt32(args[3]);
    int32_t c2 = (float) RValue_toInt32(args[4]);
    int32_t c3 = (float) RValue_toInt32(args[5]);
    int32_t c4 = (float) RValue_toInt32(args[6]);
    float alpha = (float) RValue_toReal(args[7]);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    runner->renderer->vtable->drawTextColor(runner->renderer, processedText.text, x, y, 1.0f, 1.0f, 0.0f, c1, c2, c3, c4, alpha, -1.0f);
    PreprocessedText_free(processedText);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_draw_text_color_transformed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    float angle = (float) RValue_toReal(args[5]);
    int32_t c1 = (float) RValue_toInt32(args[6]);
    int32_t c2 = (float) RValue_toInt32(args[7]);
    int32_t c3 = (float) RValue_toInt32(args[8]);
    int32_t c4 = (float) RValue_toInt32(args[9]);
    float alpha = (float) RValue_toReal(args[10]);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    runner->renderer->vtable->drawTextColor(runner->renderer, processedText.text, x, y, xscale, yscale, angle, c1, c2, c3, c4, alpha, -1.0f);
    PreprocessedText_free(processedText);
    free(str);
    return RValue_makeUndefined();
}

// Drives draw_text_color_ext / draw_text_color_ext_transformed by wrapping the (preprocessed) text and forwarding to drawTextColor.
static void drawTextColorExtCommon(Runner* runner, const char* str, float x, float y, float xscale, float yscale, float angle, int32_t separation, int32_t width, int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha) {
    int32_t fontIndex = runner->renderer->drawFont;
    if (0 > fontIndex || runner->dataWin->font.count <= (uint32_t) fontIndex) return;
    Font* font = &runner->dataWin->font.fonts[fontIndex];

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    PreprocessedText wrappedText = TextUtils_wrapText(font, processedText.text, width);
    runner->renderer->vtable->drawTextColor(runner->renderer, wrappedText.text, x, y, xscale, yscale, angle, c1, c2, c3, c4, alpha, (float) separation);
    PreprocessedText_free(wrappedText);
    PreprocessedText_free(processedText);
}

static RValue builtin_draw_text_color_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    int32_t separation = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    int32_t c1 = (float) RValue_toInt32(args[5]);
    int32_t c2 = (float) RValue_toInt32(args[6]);
    int32_t c3 = (float) RValue_toInt32(args[7]);
    int32_t c4 = (float) RValue_toInt32(args[8]);
    float alpha = (float) RValue_toReal(args[9]);

    drawTextColorExtCommon(runner, str, x, y, 1.0f, 1.0f, 0.0f, separation, width, c1, c2, c3, c4, alpha);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_draw_text_color_ext_transformed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    int32_t separation = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    float xscale = (float) RValue_toReal(args[5]);
    float yscale = (float) RValue_toReal(args[6]);
    float angle = (float) RValue_toReal(args[7]);
    int32_t c1 = (float) RValue_toInt32(args[8]);
    int32_t c2 = (float) RValue_toInt32(args[9]);
    int32_t c3 = (float) RValue_toInt32(args[10]);
    int32_t c4 = (float) RValue_toInt32(args[11]);
    float alpha = (float) RValue_toReal(args[12]);

    drawTextColorExtCommon(runner, str, x, y, xscale, yscale, angle, separation, width, c1, c2, c3, c4, alpha);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_draw_background(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || 3 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, x, y, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFF, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_background_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || 8 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    float rot = (float) RValue_toReal(args[5]);
    uint32_t color = (uint32_t) RValue_toInt32(args[6]);
    float alpha = (float) RValue_toReal(args[7]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, x, y, 0.0f, 0.0f, xscale, yscale, rot, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_background_stretched(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || 5 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float w = (float) RValue_toReal(args[3]);
    float h = (float) RValue_toReal(args[4]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    TexturePageItem* tpag = &runner->dataWin->tpag.items[tpagIndex];
    float xscale = w / (float) tpag->boundingWidth;
    float yscale = h / (float) tpag->boundingHeight;

    runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, x, y, 0.0f, 0.0f, xscale, yscale, 0.0f, 0xFFFFFF, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_background_part_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || 11 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    int32_t left = RValue_toInt32(args[1]);
    int32_t top = RValue_toInt32(args[2]);
    int32_t width = RValue_toInt32(args[3]);
    int32_t height = RValue_toInt32(args[4]);
    float x = (float) RValue_toReal(args[5]);
    float y = (float) RValue_toReal(args[6]);
    float xscale = (float) RValue_toReal(args[7]);
    float yscale = (float) RValue_toReal(args[8]);
    uint32_t color = (uint32_t) RValue_toInt32(args[9]);
    float alpha = (float) RValue_toReal(args[10]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    runner->renderer->vtable->drawSpritePart(runner->renderer, tpagIndex, left, top, width, height, x, y, xscale, yscale, 0.0f, 0.0f, 0.0f, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_background_tiled(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || 3 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    float roomW = (float) runner->currentRoom->width;
    float roomH = (float) runner->currentRoom->height;
    runner->renderer->vtable->drawTiled(runner->renderer, tpagIndex, 0.0f, 0.0f, x, y, 1.0f, 1.0f, true, true, roomW, roomH, 0xFFFFFFu, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_draw_background_tiled_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || 7 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    uint32_t color = (uint32_t) RValue_toInt32(args[5]);
    float alpha = (float) RValue_toReal(args[6]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    float roomW = (float) runner->currentRoom->width;
    float roomH = (float) runner->currentRoom->height;
    runner->renderer->vtable->drawTiled(runner->renderer, tpagIndex, 0.0f, 0.0f, x, y, xscale, yscale, true, true, roomW, roomH, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_background_get_width(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t bgIndex = RValue_toInt32(args[0]);
    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(ctx->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->tpag.items[tpagIndex].boundingWidth);
}

static RValue builtin_background_get_height(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t bgIndex = RValue_toInt32(args[0]);
    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(ctx->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->tpag.items[tpagIndex].boundingHeight);
}

static RValue builtin_draw_self(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr && ctx->currentInstance != nullptr) {
        Renderer_drawSelf(runner->renderer, ctx->currentInstance);
    }
    return RValue_makeUndefined();
}

// draw_line(x1, y1, x2, y2)
static RValue builtin_draw_line(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        runner->renderer->vtable->drawLine(runner->renderer, x1, y1, x2, y2, 1.0f, runner->renderer->drawColor, runner->renderer->drawAlpha);
    }
    return RValue_makeUndefined();
}

// draw_line_colour(x1, y1, x2, y2, col1, col2)
static RValue builtin_draw_line_colour(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        float col1 = (float) RValue_toReal(args[4]);
        float col2 = (float) RValue_toReal(args[5]);
        runner->renderer->vtable->drawLineColor(runner->renderer, x1, y1, x2, y2, 1.0f, col1, col2, runner->renderer->drawAlpha);
    }
    return RValue_makeUndefined();
}

// draw_line_width(x1, y1, x2, y2, w)
static RValue builtin_draw_line_width(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        float w = (float) RValue_toReal(args[4]);
        runner->renderer->vtable->drawLine(runner->renderer, x1, y1, x2, y2, w, runner->renderer->drawColor, runner->renderer->drawAlpha);
    }
    return RValue_makeUndefined();
}

// draw_line_width_colour(x1, y1, x2, y2, w, col1, col2)
static RValue builtin_draw_line_width_colour(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        float w = (float) RValue_toReal(args[4]);
        uint32_t col1 = (uint32_t) RValue_toInt32(args[5]);
        uint32_t col2 = (uint32_t) RValue_toInt32(args[6]);
        runner->renderer->vtable->drawLineColor(runner->renderer, x1, y1, x2, y2, w, col1, col2, runner->renderer->drawAlpha);
    }
    return RValue_makeUndefined();
}

// draw_triangle(x1, y1, x2, y2, x3, y3, outline)
static RValue builtin_draw_triangle(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        float x3 = (float) RValue_toReal(args[4]);
        float y3 = (float) RValue_toReal(args[5]);
        bool outline = (float) RValue_toBool(args[6]);
        uint32_t color = runner->renderer->drawColor;
        runner->renderer->vtable->drawTriangle(runner->renderer, x1, y1, x2, y2, x3, y3, color, color, color, runner->renderer->drawAlpha, outline);
    }
    return RValue_makeUndefined();
}

// draw_triangle_color(x1, y1, x2, y2, x3, y3, col1, col2, col3, outline)
static RValue builtin_draw_triangle_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        float x3 = (float) RValue_toReal(args[4]);
        float y3 = (float) RValue_toReal(args[5]);
        uint32_t col1 = (uint32_t) RValue_toInt32(args[6]);
        uint32_t col2 = (uint32_t) RValue_toInt32(args[7]);
        uint32_t col3 = (uint32_t) RValue_toInt32(args[8]);
        bool outline = RValue_toBool(args[9]);
        runner->renderer->vtable->drawTriangle(runner->renderer, x1, y1, x2, y2, x3, y3, col1, col2, col3, runner->renderer->drawAlpha, outline);
    }
    return RValue_makeUndefined();
}

// draw_circle(x, y, r, outline)
static RValue builtin_draw_circle(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x = (float) RValue_toReal(args[0]);
        float y = (float) RValue_toReal(args[1]);
        float r = (float) RValue_toReal(args[2]);
        bool outline = RValue_toBool(args[3]);
        Renderer_drawCircle(runner->renderer, x, y, r, outline);
    }
    return RValue_makeUndefined();
}

// draw_circle_color(x, y, r, col1, col2, outline)
static RValue builtin_draw_circle_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x = (float) RValue_toReal(args[0]);
        float y = (float) RValue_toReal(args[1]);
        float r = (float) RValue_toReal(args[2]);
        uint32_t col1 = (uint32_t) RValue_toInt32(args[3]);
        uint32_t col2 = (uint32_t) RValue_toInt32(args[4]);
        bool outline = RValue_toBool(args[5]);
        Renderer_drawCircleColor(runner->renderer, x, y, r, col1, col2, outline);
    }
    return RValue_makeUndefined();
}

// draw_ellipse(x1, y1, x2, y2, outline)
static RValue builtin_draw_ellipse(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        bool outline = RValue_toBool(args[4]);
        uint32_t color = runner->renderer->drawColor;
        Renderer_drawEllipseColor(runner->renderer, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, (x2 - x1) * 0.5f, (y2 - y1) * 0.5f, color, color, outline);
    }
    return RValue_makeUndefined();
}

// draw_ellipse_color(x1, y1, x2, y2, col1, col2, outline)
static RValue builtin_draw_ellipse_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float x1 = (float) RValue_toReal(args[0]);
        float y1 = (float) RValue_toReal(args[1]);
        float x2 = (float) RValue_toReal(args[2]);
        float y2 = (float) RValue_toReal(args[3]);
        uint32_t col1 = (uint32_t) RValue_toInt32(args[4]);
        uint32_t col2 = (uint32_t) RValue_toInt32(args[5]);
        bool outline = RValue_toBool(args[6]);
        Renderer_drawEllipseColor(runner->renderer, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, (x2 - x1) * 0.5f, (y2 - y1) * 0.5f, col1, col2, outline);
    }
    return RValue_makeUndefined();
}

// draw_set_circle_precision(precision)
static RValue builtin_draw_set_circle_precision(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->circlePrecision = Renderer_normalizeCirclePrecision(RValue_toInt32(args[0]));
    }
    return RValue_makeUndefined();
}

// draw_get_circle_precision()
static RValue builtin_draw_get_circle_precision(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((GMLReal) runner->renderer->circlePrecision);
    }
    return RValue_makeReal(24.0);
}

static RValue builtin_draw_set_colour(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawColor = (uint32_t) RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_get_colour(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((GMLReal) runner->renderer->drawColor);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_draw_get_color(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((GMLReal) runner->renderer->drawColor);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_draw_get_alpha(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((GMLReal) runner->renderer->drawAlpha);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_draw_get_font(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeInt32(runner->renderer->drawFont);
    }
    return RValue_makeInt32(-1);
}

static RValue builtin_motion_add(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();
    
    GMLReal dir = RValue_toReal(args[0]);
    GMLReal spd = RValue_toReal(args[1]);
    GMLReal rad = dir * (M_PI / 180.0);
    
    inst->hspeed += (float)(GMLReal_cos(rad) * spd);
    inst->vspeed += (float)(-GMLReal_sin(rad) * spd);
    Instance_computeSpeedFromComponents(inst);
    
    return RValue_makeUndefined();
}

// merge_color(col1, col2, amount) - lerps between two colors
static RValue builtin_merge_color(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t col1 = RValue_toInt32(args[0]);
    int32_t col2 = RValue_toInt32(args[1]);
    float amount = (float) RValue_toReal(args[2]);
    return RValue_makeReal((GMLReal) Color_lerp(col1, col2, amount));
}

static RValue builtin_surface_create(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t width = (int32_t) RValue_toReal(args[0]);
    int32_t height = (int32_t) RValue_toReal(args[1]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        int32_t surfaceId = Renderer_createSurface(runner->renderer, width,height);
        return RValue_makeReal(surfaceId);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_surface_exists(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        bool exists = Renderer_surfaceExists(runner->renderer, surfaceId);
        if (exists == true) {
            return RValue_makeReal(1.0);
        }
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_surface_set_target(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);

    Runner* runner = ctx->runner;
    if (Runner_surfaceSetTarget(runner, surfaceId)) {
        return RValue_makeReal(1.0);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_surface_reset_target(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (Runner_surfaceResetTarget(runner)) {
        return RValue_makeReal(1.0);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_surface_get_target(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeReal((GMLReal) Runner_surfaceGetTarget(runner));
}

static RValue builtin_surface_resize(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    int32_t w = (int32_t) RValue_toReal(args[1]);
    int32_t h = (int32_t) RValue_toReal(args[2]);
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    // For the application_surface, the runner is the source of truth for the requested dimensions.
    if (surfaceId == runner->applicationSurfaceId) {
        if (w > 0) runner->applicationWidth = w;
        if (h > 0) runner->applicationHeight = h;
    }
    runner->renderer->vtable->surfaceResize(runner->renderer, surfaceId, w, h);
    return RValue_makeUndefined();
}

static RValue builtin_surface_copy_part(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t sourceID = (int32_t) RValue_toReal(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    int32_t destinationID = (int32_t) RValue_toReal(args[3]);
    float xs = (float) RValue_toReal(args[4]);
    float ys = (float) RValue_toReal(args[5]);
    float ws = (float) RValue_toReal(args[6]);
    float hs = (float) RValue_toReal(args[7]);
    //fprintf(stderr, "Set Surface Target Yes\n");
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->vtable->surfaceCopy(runner->renderer, sourceID, x, y, destinationID, xs, ys, ws, hs, true);
    }
    return RValue_makeUndefined();
}

static RValue builtin_surface_copy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t sourceID = (int32_t) RValue_toReal(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    int32_t destinationID = (int32_t) RValue_toReal(args[3]);
    //fprintf(stderr, "Set Surface Target Yes\n");
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->vtable->surfaceCopy(runner->renderer, sourceID, x, y, destinationID, 0.0, 0.0, 0.0, 0.0, false);
    }
    return RValue_makeUndefined();
}

static RValue builtin_surface_free(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);

    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->vtable->surfaceFree(runner->renderer, surfaceId);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_surface(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {

    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->vtable->drawSurface(runner->renderer, surfaceId, 0, 0, -1, -1, x, y, 1.0, 1.0, 0.0, 0xFFFFFFFF, runner->renderer->drawAlpha);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_surface_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {

    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    float rot = (float) RValue_toReal(args[5]);
    uint32_t color = (uint32_t) RValue_toInt32(args[6]);
    float alpha = (float) RValue_toReal(args[7]);


    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->vtable->drawSurface(runner->renderer, surfaceId, 0, 0, -1, -1, x, y, xscale, yscale, rot, color, alpha);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_surface_part(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {

    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);

    float left = (float) RValue_toReal(args[1]);
    float top = (float) RValue_toReal(args[2]);
    float w = (float) RValue_toReal(args[3]);
    float h = (float) RValue_toReal(args[4]);

    float x = (float) RValue_toReal(args[5]);
    float y = (float) RValue_toReal(args[6]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {

        runner->renderer->vtable->drawSurface(runner->renderer, surfaceId, (int32_t) left, (int32_t) top, (int32_t) w, (int32_t) h, x, y, 1.0, 1.0, 0.0, 0xFFFFFFFF, runner->renderer->drawAlpha);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_surface_part_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {

    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);

    float left = (float) RValue_toReal(args[1]);
    float top = (float) RValue_toReal(args[2]);
    float w = (float) RValue_toReal(args[3]);
    float h = (float) RValue_toReal(args[4]);

    float x = (float) RValue_toReal(args[5]);
    float y = (float) RValue_toReal(args[6]);

    float xscale = (float) RValue_toReal(args[7]);
    float yscale = (float) RValue_toReal(args[8]);
    uint32_t color = (uint32_t) RValue_toInt32(args[9]);
    float alpha = (float) RValue_toReal(args[10]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {

        runner->renderer->vtable->drawSurface(runner->renderer, surfaceId, (int32_t) left, (int32_t) top, (int32_t) w, (int32_t) h, x, y, xscale, yscale, 0.0, color, alpha);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_surface_stretched(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {

    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float width = (float) RValue_toReal(args[3]);
    float height = (float) RValue_toReal(args[4]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float surfW = Renderer_getSurfaceWidth(runner->renderer, surfaceId);
        float surfH = Renderer_getSurfaceHeight(runner->renderer, surfaceId);
        float xscale = surfW > 0.0f ? width  / surfW : 1.0f;
        float yscale = surfH > 0.0f ? height / surfH : 1.0f;
        runner->renderer->vtable->drawSurface(runner->renderer, surfaceId, 0, 0, -1, -1, x, y, xscale, yscale, 0.0, 0xFFFFFFFF, 1.0);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_surface_stretched_ext(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float width = (float) RValue_toReal(args[3]);
    float height = (float) RValue_toReal(args[4]);
    uint32_t color = (uint32_t) RValue_toInt32(args[5]);
    float alpha = (float) RValue_toReal(args[6]);
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        float surfW = Renderer_getSurfaceWidth(runner->renderer, surfaceId);
        float surfH = Renderer_getSurfaceHeight(runner->renderer, surfaceId);
        float xscale = surfW > 0.0f ? width  / surfW : 1.0f;
        float yscale = surfH > 0.0f ? height / surfH : 1.0f;
        runner->renderer->vtable->drawSurface(runner->renderer, surfaceId, 0, 0, -1, -1, x, y, xscale, yscale, 0.0, color, alpha);
    }
    return RValue_makeUndefined();
}

static RValue builtin_surface_get_width(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    Runner* runner = ctx->runner;
    if (runner != nullptr && surfaceId == runner->applicationSurfaceId) {
        if (runner->applicationWidth > 0) return RValue_makeReal((GMLReal) runner->applicationWidth);
        return RValue_makeReal((GMLReal) ctx->dataWin->gen8.defaultWindowWidth);
    }
    return RValue_makeReal(Renderer_getSurfaceWidth(runner->renderer, surfaceId));
}

static RValue builtin_surface_get_height(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    Runner* runner = ctx->runner;
    if (runner != nullptr && surfaceId == runner->applicationSurfaceId) {
        if (runner->applicationHeight > 0) return RValue_makeReal((GMLReal) runner->applicationHeight);
        return RValue_makeReal((GMLReal) ctx->dataWin->gen8.defaultWindowHeight);
    }
    return RValue_makeReal(Renderer_getSurfaceHeight(runner->renderer, surfaceId));
}

static RValue builtin_surface_get_texture(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    Runner* runner = ctx->runner;
    if (runner != nullptr && runner->renderer != nullptr && runner->renderer->vtable->surfaceGetTexture != nullptr) {
        return RValue_makeInt32((int32_t) runner->renderer->vtable->surfaceGetTexture(runner->renderer, surfaceId));
    }
    return RValue_makeInt32(-1);
}

// Sprite functions
static RValue builtin_sprite_add(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    logStubbedFunction(ctx, "sprite_add");
    // Return 1, so that a sprite_exists check passes
    return RValue_makeInt32(1);
}

static RValue builtin_sprite_exists(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (args[0].type == RVALUE_UNDEFINED) return RValue_makeBool(false);
    int32_t spriteIndex = RValue_toInt32(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeBool(false);
    return RValue_makeBool(true);
}

static RValue builtin_sprite_get_width(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].width);
}

static RValue builtin_sprite_get_height(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].height);
}

static RValue builtin_sprite_get_number(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].textureCount);
}

static RValue builtin_sprite_get_xoffset(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].originX);
}

static RValue builtin_sprite_get_yoffset(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].originY);
}

static RValue builtin_sprite_get_bbox_left(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].marginLeft);
}

static RValue builtin_sprite_get_bbox_right(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].marginRight);
}

static RValue builtin_sprite_get_bbox_top(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].marginTop);
}

static RValue builtin_sprite_get_bbox_bottom(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) ctx->dataWin->sprt.sprites[spriteIndex].marginBottom);
}

static RValue builtin_sprite_get_name(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeString("<undefined>");
    const char* name = ctx->dataWin->sprt.sprites[spriteIndex].name;
    return RValue_makeString(name != nullptr ? name : "<undefined>");
}

// sprite_set_offset(sprite_index, xoff, yoff)
static RValue builtin_sprite_set_offset(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    if (0 > spriteIndex || (uint32_t) spriteIndex >= ctx->dataWin->sprt.count) return RValue_makeReal(0.0);
    ctx->dataWin->sprt.sprites[spriteIndex].originX = (int32_t) RValue_toReal(args[1]);
    ctx->dataWin->sprt.sprites[spriteIndex].originY = (int32_t) RValue_toReal(args[2]);
    return RValue_makeReal(0.0);
}

// sprite_create_from_surface(surface_id, x, y, w, h, removeback, smooth, xorig, yorig)
static RValue builtin_sprite_create_from_surface(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || runner->renderer->vtable->createSpriteFromSurface == nullptr) return RValue_makeReal(-1);

    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    int32_t x = RValue_toInt32(args[1]);
    int32_t y = RValue_toInt32(args[2]);
    int32_t w = RValue_toInt32(args[3]);
    int32_t h = RValue_toInt32(args[4]);
    bool removeback = RValue_toBool(args[5]);
    bool smooth = RValue_toBool(args[6]);
    int32_t xorig = RValue_toInt32(args[7]);
    int32_t yorig = RValue_toInt32(args[8]);

    int32_t result = runner->renderer->vtable->createSpriteFromSurface(runner->renderer, surfaceId, x, y, w, h, removeback, smooth, xorig, yorig);
    return RValue_makeReal((GMLReal) result);
}

// sprite_delete(sprite_index)
static RValue builtin_sprite_delete(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr || runner->renderer->vtable->deleteSprite == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    runner->renderer->vtable->deleteSprite(runner->renderer, spriteIndex);
    return RValue_makeUndefined();
}

// Font/text measurement
static RValue builtin_string_width(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || renderer->dataWin->font.count <= (uint32_t) fontIndex) return RValue_makeReal(0.0);

    Font* font = &renderer->dataWin->font.fonts[fontIndex];
    char* str = RValue_toString(args[0]);

    PreprocessedText processed = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    int32_t textLen = (int32_t) strlen(processed.text);

    // Find the widest line
    float maxWidth = 0;
    int32_t lineStart = 0;
    while (textLen >= lineStart) {
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(processed.text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        float lineWidth = TextUtils_measureLineWidth(font, processed.text + lineStart, lineLen);
        if (lineWidth > maxWidth) maxWidth = lineWidth;

        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(processed.text, lineEnd, textLen);
        } else {
            break;
        }
    }

    PreprocessedText_free(processed);
    free(str);
    return RValue_makeReal((GMLReal) (maxWidth * font->scaleX));
}

static RValue builtin_string_height(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || renderer->dataWin->font.count <= (uint32_t) fontIndex) return RValue_makeReal(0.0);

    Font* font = &renderer->dataWin->font.fonts[fontIndex];
    char* str = RValue_toString(args[0]);

    PreprocessedText processed = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    int32_t textLen = (int32_t) strlen(processed.text);
    int32_t lineCount = TextUtils_countLines(processed.text, textLen);
    PreprocessedText_free(processed);
    free(str);

    // Match HTML5 runner: string_height = lines * TextHeight('M') = lines * max_glyph_height * scaleY.
    return RValue_makeReal((GMLReal) ((float) lineCount * TextUtils_lineStride(font) * font->scaleY));
}

STUB_RETURN_ZERO(string_width_ext)
STUB_RETURN_ZERO(string_height_ext)

// Color functions
static RValue builtin_make_color_rgb(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    int32_t r = RValue_toInt32(args[0]);
    int32_t g = RValue_toInt32(args[1]);
    int32_t b = RValue_toInt32(args[2]);
    return RValue_makeReal((GMLReal) (r | (g << 8) | (b << 16)));
}

static RValue builtin_make_colour_rgb(VMContext* ctx, RValue* args, int32_t argCount) {
    return builtin_make_color_rgb(ctx, args, argCount);
}

static RValue builtin_make_color_hsv(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);

    // GameMaker: Studio 1.x: Values are wrapped around 256 (example: -1 -> 255, 257 -> 1)
    // GameMaker: Studio 2.x+: Clamps values around [0, 255]
    // Hue, Saturation, Value
    GMLReal hRaw, sRaw, vRaw;
    if (DataWin_isVersionAtLeast(ctx->dataWin, 2, 0, 0, 0)) {
        hRaw = RValue_toReal(args[0]);
        sRaw = RValue_toReal(args[1]);
        vRaw = RValue_toReal(args[2]);
        if (0.0 > hRaw) hRaw = 0.0; else if (hRaw > 255.0) hRaw = 255.0;
        if (0.0 > sRaw) sRaw = 0.0; else if (sRaw > 255.0) sRaw = 255.0;
        if (0.0 > vRaw) vRaw = 0.0; else if (vRaw > 255.0) vRaw = 255.0;
    } else {
        hRaw = (GMLReal) (RValue_toInt32(args[0]) & 0xFF);
        sRaw = (GMLReal) (RValue_toInt32(args[1]) & 0xFF);
        vRaw = (GMLReal) (RValue_toInt32(args[2]) & 0xFF);
    }

    GMLReal s = sRaw / 255.0;
    GMLReal v = vRaw / 255.0;

    GMLReal r = v, g = v, b = v;
    if (s != 0.0) {
        // https://en.wikipedia.org/wiki/HSL_and_HSV#HSV_to_RGB_alternative
        GMLReal h = (hRaw * 360.0) / 255.0;
        GMLReal hSector = h / 60.0;
        if (h == 360.0) hSector = 0.0;
        int32_t i = (int32_t) hSector;
        GMLReal f = hSector - (GMLReal) i;
        GMLReal p = v * (1.0 - s);
        GMLReal q = v * (1.0 - s * f);
        GMLReal t = v * (1.0 - s * (1.0 - f));
        switch (i) {
            case 0:  r = v; g = t; b = p; break;
            case 1:  r = q; g = v; b = p; break;
            case 2:  r = p; g = v; b = t; break;
            case 3:  r = p; g = q; b = v; break;
            case 4:  r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }

    int32_t rOut = (int32_t) (r * 255.0 + 0.5);
    int32_t gOut = (int32_t) (g * 255.0 + 0.5);
    int32_t bOut = (int32_t) (b * 255.0 + 0.5);
    if (0 > rOut) rOut = 0; else if (rOut > 255) rOut = 255;
    if (0 > gOut) gOut = 0; else if (gOut > 255) gOut = 255;
    if (0 > bOut) bOut = 0; else if (bOut > 255) bOut = 255;

    return RValue_makeReal((GMLReal) (rOut | (gOut << 8) | (bOut << 16)));
}

static RValue builtin_make_colour_hsv(VMContext* ctx, RValue* args, int32_t argCount) {
    return builtin_make_color_hsv(ctx, args, argCount);
}

static RValue builtin_color_get_red(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) BGR_R(RValue_toInt32(args[0])));
}

static RValue builtin_color_get_green(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) BGR_G(RValue_toInt32(args[0])));
}

static RValue builtin_color_get_blue(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) BGR_B(RValue_toInt32(args[0])));
}

// Matches HTML5 Color_RGBtoHSV: returns h/s/v in [0,255] as floats (no rounding).
static void Color_RGBtoHSV(int32_t col, GMLReal* outH, GMLReal* outS, GMLReal* outV) {
    GMLReal r = (GMLReal) BGR_R(col) / 255.0;
    GMLReal g = (GMLReal) BGR_G(col) / 255.0;
    GMLReal b = (GMLReal) BGR_B(col) / 255.0;
    GMLReal m = r;
    if (g < m) m = g;
    if (b < m) m = b;
    GMLReal v = r;
    if (g > v) v = g;
    if (b > v) v = b;
    GMLReal d = v - m;

    GMLReal s = (v == 0.0) ? 0.0 : (d / v);
    GMLReal h;
    if (s == 0.0)        h = 0.0;
    else if (r == v)     h = 60.0  * (g - b) / d;
    else if (g == v)     h = 120.0 + 60.0 * (b - r) / d;
    else                 h = 240.0 + 60.0 * (r - g) / d;
    if (0.0 > h) h += 360.0;

    GMLReal hOut = (h * 255.0) / 360.0;
    GMLReal sOut = s * 255.0;
    GMLReal vOut = v * 255.0;
    if (0.0 > hOut) hOut = 0.0; else if (hOut > 255.0) hOut = 255.0;
    if (0.0 > sOut) sOut = 0.0; else if (sOut > 255.0) sOut = 255.0;
    if (0.0 > vOut) vOut = 0.0; else if (vOut > 255.0) vOut = 255.0;
    *outH = hOut;
    *outS = sOut;
    *outV = vOut;
}

static RValue builtin_color_get_hue(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal h, s, v;
    Color_RGBtoHSV(RValue_toInt32(args[0]), &h, &s, &v);
    return RValue_makeReal(h);
}

static RValue builtin_color_get_saturation(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal h, s, v;
    Color_RGBtoHSV(RValue_toInt32(args[0]), &h, &s, &v);
    return RValue_makeReal(s);
}

static RValue builtin_color_get_value(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GMLReal h, s, v;
    Color_RGBtoHSV(RValue_toInt32(args[0]), &h, &s, &v);
    return RValue_makeReal(v);
}

// Display stubs
STUB_RETURN_VALUE(display_get_width, 640.0)
STUB_RETURN_VALUE(display_get_height, 480.0)

static int32_t resolveGuiWidth(Runner* runner) {
    if (runner->guiWidth > 0) return runner->guiWidth;
    Room* room = runner->currentRoom;
    if (room != nullptr) {
        repeat(8, vi) {
            if (room->views[vi].enabled && room->views[vi].portWidth > 0) {
                return room->views[vi].portWidth;
            }
        }
        if (room->width > 0) return (int32_t) room->width;
    }
    return 320;
}

static int32_t resolveGuiHeight(Runner* runner) {
    if (runner->guiHeight > 0) return runner->guiHeight;
    Room* room = runner->currentRoom;
    if (room != nullptr) {
        repeat(8, vi) {
            if (room->views[vi].enabled && room->views[vi].portHeight > 0) {
                return room->views[vi].portHeight;
            }
        }
        if (room->height > 0) return (int32_t) room->height;
    }
    return 240;
}

static RValue builtin_display_get_gui_width(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeInt32(resolveGuiWidth(runner));
}

static RValue builtin_display_get_gui_height(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeInt32(resolveGuiHeight(runner));
}

static RValue builtinDeviceMouseX(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    // We only support mouse 0 for now (device 0)
    int32_t device = RValue_toInt32(args[0]);
    if (device != 0) return RValue_makeReal(0.0);
    GMLReal mouseRoomX, mouseRoomY;
    Runner_getMouseRoomPosition(runner, &mouseRoomX, &mouseRoomY);
    return RValue_makeReal(mouseRoomX);
}

static RValue builtinDeviceMouseY(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    // We only support mouse 0 for now (device 0)
    int32_t device = RValue_toInt32(args[0]);
    if (device != 0) return RValue_makeReal(0.0);
    GMLReal mouseRoomX, mouseRoomY;
    Runner_getMouseRoomPosition(runner, &mouseRoomX, &mouseRoomY);
    return RValue_makeReal(mouseRoomY);
}

static RValue builtinDeviceMouseXToGui(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    // We only support mouse 0 for now (device 0)
    int32_t device = RValue_toInt32(args[0]);
    if (device != 0) return RValue_makeReal(0.0);
    int32_t guiWidth = resolveGuiWidth(runner);
    return RValue_makeReal(runner->mouse->normalizedX * guiWidth);
}

static RValue builtinDeviceMouseYToGui(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    // We only support mouse 0 for now (device 0)
    int32_t device = RValue_toInt32(args[0]);
    if (device != 0) return RValue_makeReal(0.0);
    int32_t guiHeight = resolveGuiHeight(runner);
    return RValue_makeReal(runner->mouse->normalizedY * guiHeight);
}

static RValue builtin_display_set_gui_size(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t w = RValue_toInt32(args[0]);
    int32_t h = RValue_toInt32(args[1]);
    runner->guiWidth = w > 0 ? w : 0;
    runner->guiHeight = h > 0 ? h : 0;
    Runner_guiSizeChanged(runner);
    return RValue_makeUndefined();
}

static RValue builtin_display_set_gui_maximise(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    // GMS: display_set_gui_maximise(xscale, yscale, xoffset, yoffset). We don't support scaling yet; reset to auto (match view).
    Runner* runner = ctx->runner;
    runner->guiWidth = 0;
    runner->guiHeight = 0;
    Runner_guiSizeChanged(runner);
    return RValue_makeUndefined();
}

// place_meeting(x, y, obj) - returns true if the calling instance would collide with obj at position (x, y)
static RValue builtin_place_meeting(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeBool(false);

    Runner* runner = ctx->runner;
    Instance* caller = ctx->currentInstance;
    if (caller == nullptr) return RValue_makeBool(false);

    GMLReal testX = RValue_toReal(args[0]);
    GMLReal testY = RValue_toReal(args[1]);
    int32_t target = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[2]));
    if (target == INSTANCE_NOONE) return RValue_makeBool(false);

    // Save current position and temporarily move to test position
    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    bool found = false;

    SpatialGrid_syncGrid(runner, runner->spatialGrid);

    if (callerBBox.valid) {
        SpatialGridQuery query = SpatialGrid_prepareQuery(runner, callerBBox.left, callerBBox.top, callerBBox.right, callerBBox.bottom, target);

        for (int32_t gx = query.range.minGridX; query.range.maxGridX >= gx && !found; gx++) {
            for (int32_t gy = query.range.minGridY; query.range.maxGridY >= gy && !found; gy++) {
                Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
                int32_t cellLen = (int32_t) arrlen(cell);
                repeat(cellLen, ci) {
                    Instance* other = cell[ci];
                    if (!other->active || other == caller) continue;
                    if (other->lastCollisionQueryId == query.queryId) continue;
                    other->lastCollisionQueryId = query.queryId;

                    if (!query.matchAll && query.filterByObject && !VM_isObjectOrDescendant(runner->dataWin, other->objectIndex, target)) continue;
                    if (!query.matchAll && query.filterByInstanceId && other->instanceId != (uint32_t) target) continue;

                    InstanceBBox otherBBox = Collision_computeBBox(runner, other);
                    if (!otherBBox.valid) continue;

                    if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    // Restore original position
    caller->x = savedX;
    caller->y = savedY;

    return RValue_makeBool(found);
}

// GameMaker rounds the coordinates when in collision compatibility mode
static inline GMLReal compatRoundCoord(GMLReal v) { return GMLReal_bankersRound(v); }

// collision_line(x1, y1, x2, y2, obj, prec, notme)
static RValue builtin_collision_line(VMContext* ctx, RValue* args, int32_t argCount) {
    if (7 > argCount) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    GMLReal lx1 = RValue_toReal(args[0]);
    GMLReal ly1 = RValue_toReal(args[1]);
    GMLReal lx2 = RValue_toReal(args[2]);
    GMLReal ly2 = RValue_toReal(args[3]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[4]));
    int32_t prec = RValue_toInt32(args[5]);
    int32_t notme = RValue_toInt32(args[6]);

    if (runner->collisionCompatibilityMode) {
        lx1 = compatRoundCoord(lx1); ly1 = compatRoundCoord(ly1);
        lx2 = compatRoundCoord(lx2); ly2 = compatRoundCoord(ly2);
    }

    Instance* self = ctx->currentInstance;

    int32_t resultId = INSTANCE_NOONE;
    int32_t snapBase = Runner_pushInstancesForTarget(runner, targetObjIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t snapIdx = snapBase; snapEnd > snapIdx; snapIdx++) {
        Instance* inst = runner->instanceSnapshots[snapIdx];
        if (!inst->active) continue;
        if (notme && inst == self) continue;

        if (!Collision_lineOverlapsInstance(ctx->runner, inst, lx1, ly1, lx2, ly2)) continue;
        InstanceBBox bbox = Collision_computeBBox(ctx->runner, inst);

        // Normalize line left-to-right for clipping
        GMLReal xl = lx1, yl = ly1, xr = lx2, yr = ly2;
        if (xl > xr) { GMLReal tmp = xl; xl = xr; xr = tmp; tmp = yl; yl = yr; yr = tmp; }

        GMLReal dx = xr - xl;
        GMLReal dy = yr - yl;

        // Clip line to bbox horizontally
        if (GMLReal_fabs(dx) > 0.0001) {
            if (bbox.left > xl) {
                GMLReal t = (bbox.left - xl) / dx;
                xl = bbox.left;
                yl = yl + t * dy;
            }
            if (xr > bbox.right) {
                GMLReal t = (bbox.right - xl) / (xr - xl);
                yr = yl + t * (yr - yl);
                xr = bbox.right;
            }
        }

        // Y-bounds check after horizontal clipping
        GMLReal clippedTop    = GMLReal_fmin(yl, yr);
        GMLReal clippedBottom = GMLReal_fmax(yl, yr);
        if (bbox.top > clippedBottom || clippedTop >= bbox.bottom) continue;

        // Bbox-only mode: collision confirmed
        if (prec == 0) {
            resultId = inst->instanceId;
            break;
        }

        // Precise mode: walk line pixel-by-pixel within bbox
        Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
        if (spr == nullptr || spr->sepMasks != 1 || spr->masks == nullptr || spr->maskCount == 0) {
            // No precise mask available, treat as bbox hit
            resultId = inst->instanceId;
            break;
        }

        // Recompute dx/dy for the clipped segment
        GMLReal cdx = xr - xl;
        GMLReal cdy = yr - yl;
        bool found = false;

        if (GMLReal_fabs(cdy) >= GMLReal_fabs(cdx)) {
            // Vertical-major: normalize top-to-bottom
            GMLReal xt = xl, yt = yl, xb = xr, yb = yr;
            if (yt > yb) { GMLReal tmp = xt; xt = xb; xb = tmp; tmp = yt; yt = yb; yb = tmp; }
            GMLReal vdx = xb - xt;
            GMLReal vdy = yb - yt;

            int32_t startY = (int32_t) GMLReal_fmax(bbox.top, yt);
            int32_t endY   = (int32_t) GMLReal_fmin(bbox.bottom, yb);
            for (int32_t py = startY; endY >= py && !found; py++) {
                GMLReal px = (GMLReal_fabs(vdy) > 0.0001) ? xt + ((GMLReal) py - yt) * vdx / vdy : xt;
                if (Collision_pointInInstance(spr, inst, px + 0.5, (GMLReal) py + 0.5)) {
                    found = true;
                }
            }
        } else {
            // Horizontal-major
            int32_t startX = (int32_t) GMLReal_fmax(bbox.left, xl);
            int32_t endX   = (int32_t) GMLReal_fmin(bbox.right, xr);
            for (int32_t px = startX; endX >= px && !found; px++) {
                GMLReal py = (GMLReal_fabs(cdx) > 0.0001) ? yl + ((GMLReal) px - xl) * cdy / cdx : yl;
                if (Collision_pointInInstance(spr, inst, (GMLReal) px + 0.5, py + 0.5)) {
                    found = true;
                }
            }
        }

        if (!found) continue;
        resultId = inst->instanceId;
        break;
    }
    Runner_popInstanceSnapshot(runner, snapBase);

    return RValue_makeReal((GMLReal) resultId);
}

// rectangle_in_rectangle(px1, py1, px2, py2, x1, y1, x2, y2)
// Returns 0 if rectangle P is outside R, 1 if fully inside, 2 if partially overlapping.
// Matches GameMaker-HTML5 scripts/functions/Function_Collision.js.
static RValue builtin_rectangle_in_rectangle(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (8 > argCount) return RValue_makeReal(0.0);

    GMLReal px1 = RValue_toReal(args[0]);
    GMLReal py1 = RValue_toReal(args[1]);
    GMLReal px2 = RValue_toReal(args[2]);
    GMLReal py2 = RValue_toReal(args[3]);
    GMLReal x1  = RValue_toReal(args[4]);
    GMLReal y1  = RValue_toReal(args[5]);
    GMLReal x2  = RValue_toReal(args[6]);
    GMLReal y2  = RValue_toReal(args[7]);

    // Normalize so (1,1) is always top-left and (2,2) is bottom-right.
    if (px1 > px2) { GMLReal t = px1; px1 = px2; px2 = t; }
    if (py1 > py2) { GMLReal t = py1; py1 = py2; py2 = t; }
    if (x1  > x2)  { GMLReal t = x1;  x1  = x2;  x2  = t; }
    if (y1  > y2)  { GMLReal t = y1;  y1  = y2;  y2  = t; }

    // Count how many corners of P sit inside R.
    int32_t cornersIn = 0;
    if (px1 >= x1 && px1 <= x2 && py1 >= y1 && py1 <= y2) cornersIn |= 1;
    if (px2 >= x1 && px2 <= x2 && py1 >= y1 && py1 <= y2) cornersIn |= 2;
    if (px2 >= x1 && px2 <= x2 && py2 >= y1 && py2 <= y2) cornersIn |= 4;
    if (px1 >= x1 && px1 <= x2 && py2 >= y1 && py2 <= y2) cornersIn |= 8;

    if (cornersIn == 15) return RValue_makeReal(1.0);

    if (cornersIn == 0) {
        // No P corner is inside R. Check whether R's corners are inside P (R engulfs P partially)
        // or the rectangles cross axis-wise (T-intersection).
        int32_t rCornersIn = 0;
        if (x1 >= px1 && x1 <= px2 && y1 >= py1 && y1 <= py2) rCornersIn |= 1;
        if (x2 >= px1 && x2 <= px2 && y1 >= py1 && y1 <= py2) rCornersIn |= 2;
        if (x2 >= px1 && x2 <= px2 && y2 >= py1 && y2 <= py2) rCornersIn |= 4;
        if (x1 >= px1 && x1 <= px2 && y2 >= py1 && y2 <= py2) rCornersIn |= 8;
        if (rCornersIn != 0) return RValue_makeReal(2.0);

        // R crosses P horizontally (R's x-edges within P, P's y-edges within R).
        int32_t crossX = 0;
        if (x1 >= px1 && x1 <= px2 && py1 >= y1 && py1 <= y2) crossX |= 1;
        if (x2 >= px1 && x2 <= px2 && py1 >= y1 && py1 <= y2) crossX |= 2;
        if (x2 >= px1 && x2 <= px2 && py2 >= y1 && py2 <= y2) crossX |= 4;
        if (x1 >= px1 && x1 <= px2 && py2 >= y1 && py2 <= y2) crossX |= 8;
        if (crossX != 0) return RValue_makeReal(2.0);

        // R crosses P vertically (R's y-edges within P, P's x-edges within R).
        int32_t crossY = 0;
        if (px1 >= x1 && px1 <= x2 && y1 >= py1 && y1 <= py2) crossY |= 1;
        if (px2 >= x1 && px2 <= x2 && y1 >= py1 && y1 <= py2) crossY |= 2;
        if (px2 >= x1 && px2 <= x2 && y2 >= py1 && y2 <= py2) crossY |= 4;
        if (px1 >= x1 && px1 <= x2 && y2 >= py1 && y2 <= py2) crossY |= 8;
        if (crossY != 0) return RValue_makeReal(2.0);

        return RValue_makeReal(0.0);
    }

    // Some but not all of P's corners are inside R: partial overlap.
    return RValue_makeReal(2.0);
}

// collision_rectangle(x1, y1, x2, y2, obj, prec, notme)
static RValue builtin_collision_rectangle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (7 > argCount) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    GMLReal x1 = RValue_toReal(args[0]);
    GMLReal y1 = RValue_toReal(args[1]);
    GMLReal x2 = RValue_toReal(args[2]);
    GMLReal y2 = RValue_toReal(args[3]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[4]));
    int32_t prec = RValue_toInt32(args[5]);
    int32_t notme = RValue_toInt32(args[6]);

    if (runner->collisionCompatibilityMode) {
        x1 = compatRoundCoord(x1); y1 = compatRoundCoord(y1);
        x2 = compatRoundCoord(x2); y2 = compatRoundCoord(y2);
    }

    // Normalize rect
    if (x1 > x2) { GMLReal tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { GMLReal tmp = y1; y1 = y2; y2 = tmp; }

    Instance* self = ctx->currentInstance;

    int32_t resultId = INSTANCE_NOONE;
    int32_t snapBase = Runner_pushInstancesForTarget(runner, targetObjIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t snapIdx = snapBase; snapEnd > snapIdx; snapIdx++) {
        Instance* inst = runner->instanceSnapshots[snapIdx];
        if (!inst->active) continue;
        if (notme && inst == self) continue;

        if (!Collision_rectOverlapsInstance(ctx->runner, inst, x1, y1, x2, y2)) continue;

        InstanceBBox bbox = Collision_computeBBox(ctx->runner, inst);

        // Precise check if requested and sprite has precise masks
        if (prec != 0) {
            Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
            if (Collision_hasFrameMasks(spr)) {
                // Check if any pixel in the overlap region hits the mask
                GMLReal iLeft   = GMLReal_fmax(x1, bbox.left);
                GMLReal iRight  = GMLReal_fmin(x2, bbox.right);
                GMLReal iTop    = GMLReal_fmax(y1, bbox.top);
                GMLReal iBottom = GMLReal_fmin(y2, bbox.bottom);

                bool found = false;
                int32_t startX = (int32_t) GMLReal_floor(iLeft);
                int32_t endX   = (int32_t) GMLReal_ceil(iRight);
                int32_t startY = (int32_t) GMLReal_floor(iTop);
                int32_t endY   = (int32_t) GMLReal_ceil(iBottom);

                for (int32_t py = startY; endY > py && !found; py++) {
                    for (int32_t px = startX; endX > px && !found; px++) {
                        if (Collision_pointInInstance(spr, inst, (GMLReal) px + 0.5, (GMLReal) py + 0.5)) {
                            found = true;
                        }
                    }
                }
                if (!found) continue;
            }
        }

        resultId = inst->instanceId;
        break;
    }
    Runner_popInstanceSnapshot(runner, snapBase);

    return RValue_makeReal((GMLReal) resultId);
}

// collision_circle(x, y, radius, obj, prec, notme)
static RValue builtin_collision_circle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (6 > argCount) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    GMLReal cx = RValue_toReal(args[0]);
    GMLReal cy = RValue_toReal(args[1]);
    GMLReal radius = RValue_toReal(args[2]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[3]));
    int32_t prec = RValue_toInt32(args[4]);
    int32_t notme = RValue_toInt32(args[5]);

    if (targetObjIndex == INSTANCE_NOONE) return RValue_makeReal((GMLReal) INSTANCE_NOONE);
    if (0 > radius) radius = -radius;
    GMLReal radiusSq = radius * radius;

    Instance* self = ctx->currentInstance;
    if (runner->collisionCompatibilityMode) {
        GMLReal qx1r = compatRoundCoord(cx - radius);
        GMLReal qy1r = compatRoundCoord(cy - radius);
        GMLReal qx2r = compatRoundCoord(cx + radius);
        GMLReal qy2r = compatRoundCoord(cy + radius);
        cx = (qx1r + qx2r) * 0.5;
        cy = (qy1r + qy2r) * 0.5;
        // Genuine collision_circle has qx2-qx1 == qy2-qy1 == 2r; use the smaller in case of rounding asymmetry.
        GMLReal rx = (qx2r - qx1r) * 0.5;
        GMLReal ry = (qy2r - qy1r) * 0.5;
        radius = rx < ry ? rx : ry;
        radiusSq = radius * radius;
    }

    GMLReal qx1 = cx - radius;
    GMLReal qy1 = cy - radius;
    GMLReal qx2 = cx + radius;
    GMLReal qy2 = cy + radius;

    SpatialGrid_syncGrid(runner, runner->spatialGrid);
    SpatialGridQuery query = SpatialGrid_prepareQuery(runner, qx1, qy1, qx2, qy2, targetObjIndex);

    int32_t resultId = INSTANCE_NOONE;
    for (int32_t gx = query.range.minGridX; query.range.maxGridX >= gx && resultId == INSTANCE_NOONE; gx++) {
        for (int32_t gy = query.range.minGridY; query.range.maxGridY >= gy && resultId == INSTANCE_NOONE; gy++) {
            Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
            int32_t cellLen = (int32_t) arrlen(cell);
            repeat(cellLen, ci) {
                Instance* inst = cell[ci];
                if (!inst->active) continue;
                if (notme && inst == self) continue;
                if (inst->lastCollisionQueryId == query.queryId) continue;
                inst->lastCollisionQueryId = query.queryId;

                if (!query.matchAll && query.filterByObject && !VM_isObjectOrDescendant(ctx->dataWin, inst->objectIndex, targetObjIndex)) continue;
                if (!query.matchAll && query.filterByInstanceId && inst->instanceId != (uint32_t) targetObjIndex) continue;

                if (!Collision_circleOverlapsInstance(ctx->runner, inst, cx, cy, radius)) continue;

                if (prec != 0) {
                    Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
                    if (Collision_hasFrameMasks(spr)) {
                        InstanceBBox bbox = Collision_computeBBox(ctx->runner, inst);
                        GMLReal iLeft   = GMLReal_fmax(qx1, bbox.left);
                        GMLReal iRight  = GMLReal_fmin(qx2, bbox.right);
                        GMLReal iTop    = GMLReal_fmax(qy1, bbox.top);
                        GMLReal iBottom = GMLReal_fmin(qy2, bbox.bottom);

                        bool found = false;
                        int32_t startX = (int32_t) GMLReal_floor(iLeft);
                        int32_t endX   = (int32_t) GMLReal_ceil(iRight);
                        int32_t startY = (int32_t) GMLReal_floor(iTop);
                        int32_t endY   = (int32_t) GMLReal_ceil(iBottom);

                        for (int32_t py = startY; endY > py && !found; py++) {
                            for (int32_t px = startX; endX > px && !found; px++) {
                                GMLReal wpx = (GMLReal) px + 0.5;
                                GMLReal wpy = (GMLReal) py + 0.5;
                                GMLReal ddx = wpx - cx;
                                GMLReal ddy = wpy - cy;
                                if (ddx * ddx + ddy * ddy > radiusSq) continue;
                                if (Collision_pointInInstance(spr, inst, wpx, wpy)) {
                                    found = true;
                                }
                            }
                        }
                        if (!found) continue;
                    }
                }

                resultId = inst->instanceId;
                break;
            }
        }
    }

    return RValue_makeReal((GMLReal) resultId);
}

// collision_rectangle_list(x1, y1, x2, y2, obj, prec, notme, list, ordered) -> count
static RValue builtin_collision_rectangle_list(VMContext* ctx, RValue* args, int32_t argCount) {
    if (8 > argCount) return RValue_makeReal(0.0);

    Runner* runner = ctx->runner;
    GMLReal x1 = RValue_toReal(args[0]);
    GMLReal y1 = RValue_toReal(args[1]);
    GMLReal x2 = RValue_toReal(args[2]);
    GMLReal y2 = RValue_toReal(args[3]);
    int32_t target = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[4]));
    int32_t prec = RValue_toInt32(args[5]);
    int32_t notme = RValue_toInt32(args[6]);
    int32_t listId = RValue_toInt32(args[7]);
    // arg 8 (ordered) is currently ignored; instances are appended in iteration order

    if (target == INSTANCE_NOONE) return RValue_makeReal(0.0);
    DsList* list = dsListGet(runner, listId);
    if (list == nullptr) return RValue_makeReal(0.0);

    if (runner->collisionCompatibilityMode) {
        x1 = compatRoundCoord(x1); y1 = compatRoundCoord(y1);
        x2 = compatRoundCoord(x2); y2 = compatRoundCoord(y2);
    }

    if (x1 > x2) { GMLReal tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { GMLReal tmp = y1; y1 = y2; y2 = tmp; }

    Instance* self = ctx->currentInstance;
    int32_t count = 0;

    SpatialGrid_syncGrid(runner, runner->spatialGrid);
    SpatialGridQuery query = SpatialGrid_prepareQuery(runner, x1, y1, x2, y2, target);

    for (int32_t gx = query.range.minGridX; query.range.maxGridX >= gx; gx++) {
        for (int32_t gy = query.range.minGridY; query.range.maxGridY >= gy; gy++) {
            Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
            int32_t cellLen = (int32_t) arrlen(cell);
            repeat(cellLen, ci) {
                Instance* inst = cell[ci];
                if (!inst->active) continue;
                if (notme && inst == self) continue;
                if (inst->lastCollisionQueryId == query.queryId) continue;
                inst->lastCollisionQueryId = query.queryId;

                if (!query.matchAll && query.filterByObject && !VM_isObjectOrDescendant(ctx->dataWin, inst->objectIndex, target)) continue;
                if (!query.matchAll && query.filterByInstanceId && inst->instanceId != (uint32_t) target) continue;

                if (!Collision_rectOverlapsInstance(ctx->runner, inst, x1, y1, x2, y2)) continue;
                InstanceBBox bbox = Collision_computeBBox(ctx->runner, inst);

                if (prec != 0) {
                    Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
                    if (Collision_hasFrameMasks(spr)) {
                        GMLReal iLeft   = GMLReal_fmax(x1, bbox.left);
                        GMLReal iRight  = GMLReal_fmin(x2, bbox.right);
                        GMLReal iTop    = GMLReal_fmax(y1, bbox.top);
                        GMLReal iBottom = GMLReal_fmin(y2, bbox.bottom);

                        bool found = false;
                        int32_t startX = (int32_t) GMLReal_floor(iLeft);
                        int32_t endX   = (int32_t) GMLReal_ceil(iRight);
                        int32_t startY = (int32_t) GMLReal_floor(iTop);
                        int32_t endY   = (int32_t) GMLReal_ceil(iBottom);

                        for (int32_t py = startY; endY > py && !found; py++) {
                            for (int32_t px = startX; endX > px && !found; px++) {
                                if (Collision_pointInInstance(spr, inst, (GMLReal) px + 0.5, (GMLReal) py + 0.5)) {
                                    found = true;
                                }
                            }
                        }
                        if (!found) continue;
                    }
                }

                arrput(list->items, RValue_makeReal((GMLReal) inst->instanceId));
                count++;
            }
        }
    }

    return RValue_makeReal((GMLReal) count);
}

// collision_point(x, y, obj, prec, notme)
static RValue builtin_collision_point(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    GMLReal px = RValue_toReal(args[0]);
    GMLReal py = RValue_toReal(args[1]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[2]));
    int32_t prec = RValue_toInt32(args[3]);
    int32_t notme = RValue_toInt32(args[4]);

    if (runner->collisionCompatibilityMode) {
        px = compatRoundCoord(px); py = compatRoundCoord(py);
    }

    Instance* self = ctx->currentInstance;

    int32_t resultId = INSTANCE_NOONE;
    int32_t snapBase = Runner_pushInstancesForTarget(runner, targetObjIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t snapIdx = snapBase; snapEnd > snapIdx; snapIdx++) {
        Instance* inst = runner->instanceSnapshots[snapIdx];
        if (!inst->active) continue;
        if (notme && inst == self) continue;

        if (!Collision_pointInsideInstanceBox(ctx->runner, inst, px, py)) continue;

        if (prec != 0) {
            Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
            if (Collision_hasFrameMasks(spr)) {
                if (!Collision_pointInInstance(spr, inst, px, py)) continue;
            }
        }

        resultId = inst->instanceId;
        break;
    }
    Runner_popInstanceSnapshot(runner, snapBase);

    return RValue_makeReal((GMLReal) resultId);
}

// instance_place(x, y, obj) - returns colliding instance id at (x, y), or noone
static RValue builtin_instance_place(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    Instance* caller = ctx->currentInstance;
    if (caller == nullptr) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    GMLReal testX = RValue_toReal(args[0]);
    GMLReal testY = RValue_toReal(args[1]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[2]));
    if (targetObjIndex == INSTANCE_NOONE) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    int32_t resultId = INSTANCE_NOONE;

    SpatialGrid_syncGrid(runner, runner->spatialGrid);

    if (callerBBox.valid) {
        SpatialGridQuery query = SpatialGrid_prepareQuery(runner, callerBBox.left, callerBBox.top, callerBBox.right, callerBBox.bottom, targetObjIndex);

        for (int32_t gx = query.range.minGridX; query.range.maxGridX >= gx && resultId == INSTANCE_NOONE; gx++) {
            for (int32_t gy = query.range.minGridY; query.range.maxGridY >= gy && resultId == INSTANCE_NOONE; gy++) {
                Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
                int32_t cellLen = (int32_t) arrlen(cell);
                repeat(cellLen, ci) {
                    Instance* other = cell[ci];
                    if (!other->active || other == caller) continue;
                    if (other->lastCollisionQueryId == query.queryId) continue;
                    other->lastCollisionQueryId = query.queryId;

                    if (!query.matchAll && query.filterByObject && !VM_isObjectOrDescendant(runner->dataWin, other->objectIndex, targetObjIndex)) continue;
                    if (!query.matchAll && query.filterByInstanceId && other->instanceId != (uint32_t) targetObjIndex) continue;

                    InstanceBBox otherBBox = Collision_computeBBox(runner, other);
                    if (!otherBBox.valid) continue;

                    if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                        resultId = other->instanceId;
                        break;
                    }
                }
            }
        }
    }

    caller->x = savedX;
    caller->y = savedY;
    return RValue_makeReal((GMLReal) resultId);
}

// instance_place_list(x, y, obj, list, ordered) -> count of colliding instances, appended to ds_list
static RValue builtin_instance_place_list(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeReal(0.0);

    Runner* runner = ctx->runner;
    Instance* caller = ctx->currentInstance;
    if (caller == nullptr) return RValue_makeReal(0.0);

    GMLReal testX = RValue_toReal(args[0]);
    GMLReal testY = RValue_toReal(args[1]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[2]));
    int32_t listId = RValue_toInt32(args[3]);
    // arg 4 (ordered) is currently ignored; instances are appended in iteration order

    if (targetObjIndex == INSTANCE_NOONE) return RValue_makeReal(0.0);

    DsList* list = dsListGet(runner, listId);
    if (list == nullptr) return RValue_makeReal(0.0);

    GMLReal savedX = caller->x;
    GMLReal savedY = caller->y;
    caller->x = testX;
    caller->y = testY;

    InstanceBBox callerBBox = Collision_computeBBox(runner, caller);
    int32_t count = 0;

    SpatialGrid_syncGrid(runner, runner->spatialGrid);

    if (callerBBox.valid) {
        SpatialGridQuery query = SpatialGrid_prepareQuery(runner, callerBBox.left, callerBBox.top, callerBBox.right, callerBBox.bottom, targetObjIndex);

        for (int32_t gx = query.range.minGridX; query.range.maxGridX >= gx; gx++) {
            for (int32_t gy = query.range.minGridY; query.range.maxGridY >= gy; gy++) {
                Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
                int32_t cellLen = (int32_t) arrlen(cell);
                repeat(cellLen, ci) {
                    Instance* other = cell[ci];
                    if (!other->active || other == caller) continue;
                    if (other->lastCollisionQueryId == query.queryId) continue;
                    other->lastCollisionQueryId = query.queryId;

                    if (!query.matchAll && query.filterByObject && !VM_isObjectOrDescendant(runner->dataWin, other->objectIndex, targetObjIndex)) continue;
                    if (!query.matchAll && query.filterByInstanceId && other->instanceId != (uint32_t) targetObjIndex) continue;

                    InstanceBBox otherBBox = Collision_computeBBox(runner, other);
                    if (!otherBBox.valid) continue;

                    if (Collision_instancesOverlapPrecise(runner, caller, other, callerBBox, otherBBox)) {
                        arrput(list->items, RValue_makeReal((GMLReal) other->instanceId));
                        count++;
                    }
                }
            }
        }
    }

    caller->x = savedX;
    caller->y = savedY;
    return RValue_makeReal((GMLReal) count);
}

// instance_position(x, y, obj)
static RValue builtin_instance_position(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal((GMLReal) INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    GMLReal px = RValue_toReal(args[0]);
    GMLReal py = RValue_toReal(args[1]);
    int32_t targetObjIndex = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[2]));

    if (runner->collisionCompatibilityMode) {
        px = compatRoundCoord(px); py = compatRoundCoord(py);
    }

    int32_t resultId = INSTANCE_NOONE;
    int32_t snapBase = Runner_pushInstancesForTarget(runner, targetObjIndex);
    int32_t snapEnd  = (int32_t) arrlen(runner->instanceSnapshots);
    for (int32_t i = snapBase; snapEnd > i; i++) {
        Instance* inst = runner->instanceSnapshots[i];
        if (!inst->active) continue;

        if (!Collision_pointInsideInstanceBox(ctx->runner, inst, px, py)) continue;

        // GameMaker ALWAYS does precise collision checks here
        Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
        if (Collision_hasFrameMasks(spr) && !Collision_pointInInstance(spr, inst, px, py)) continue;

        resultId = inst->instanceId;
        break;
    }
    Runner_popInstanceSnapshot(runner, snapBase);

    return RValue_makeReal((GMLReal) resultId);
}

// position_meeting(x, y, obj) - returns true if point (x, y) is inside any instance of obj.
static RValue builtin_position_meeting(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeBool(false);

    Runner* runner = ctx->runner;
    GMLReal px = RValue_toReal(args[0]);
    GMLReal py = RValue_toReal(args[1]);
    int32_t target = VM_resolveInstanceTarget(ctx, RValue_toInt32(args[2]));
    if (target == INSTANCE_NOONE) return RValue_makeBool(false);

    if (runner->collisionCompatibilityMode) {
        px = compatRoundCoord(px); py = compatRoundCoord(py);
    }

    SpatialGrid_syncGrid(runner, runner->spatialGrid);
    SpatialGridQuery query = SpatialGrid_prepareQuery(runner, px, py, px, py, target);
    bool found = false;

    for (int32_t gx = query.range.minGridX; query.range.maxGridX >= gx && !found; gx++) {
        for (int32_t gy = query.range.minGridY; query.range.maxGridY >= gy && !found; gy++) {
            Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
            int32_t cellLen = (int32_t) arrlen(cell);
            repeat(cellLen, ci) {
                Instance* other = cell[ci];
                // Keep in mind that we DO NOT skip "self"
                if (!other->active) continue;
                if (other->lastCollisionQueryId == query.queryId) continue;
                other->lastCollisionQueryId = query.queryId;

                if (!query.matchAll && query.filterByObject && !VM_isObjectOrDescendant(runner->dataWin, other->objectIndex, target)) continue;
                if (!query.matchAll && query.filterByInstanceId && other->instanceId != (uint32_t) target) continue;

                if (!Collision_pointInsideInstanceBox(ctx->runner, other, px, py)) continue;

                // GameMaker ALWAYS does precise collision checks here
                Sprite* spr = Collision_getSprite(ctx->dataWin, other);
                if (Collision_hasFrameMasks(spr) && !Collision_pointInInstance(spr, other, px, py)) continue;

                found = true;
                break;
            }
        }
    }

    return RValue_makeBool(found);
}

// Misc stubs
STUB_RETURN_ZERO(get_timer)
static RValue builtin_action_set_alarm(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t steps = RValue_toInt32(args[0]);
    int32_t alarmIndex = RValue_toInt32(args[1]);

    if (0 > alarmIndex || alarmIndex >= GML_ALARM_COUNT) {
        return RValue_makeUndefined();
    }

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        Runner* runner = ctx->runner;

#ifdef ENABLE_VM_TRACING
        if (shgeti(ctx->alarmsToBeTraced, "*") != -1 || shgeti(ctx->alarmsToBeTraced, runner->dataWin->objt.objects[inst->objectIndex].name) != -1) {
            fprintf(stderr, "VM: [%s] Setting Alarm[%d] = %d (instanceId=%d)\n", runner->dataWin->objt.objects[inst->objectIndex].name, alarmIndex, steps, inst->instanceId);
        }
#endif

        inst->alarm[alarmIndex] = steps;
        if (steps > 0) inst->activeAlarmMask |= (uint16_t) (1u << alarmIndex);
        else inst->activeAlarmMask &= (uint16_t) ~(1u << alarmIndex);
    }

    return RValue_makeUndefined();
}

static RValue builtin_alarm_set(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t alarmIndex = RValue_toInt32(args[0]);
    int32_t value = RValue_toInt32(args[1]);

    if (0 > alarmIndex || alarmIndex >= GML_ALARM_COUNT) {
        return RValue_makeUndefined();
    }

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;

#ifdef ENABLE_VM_TRACING
        Runner* runner = ctx->runner;
        if (shgeti(ctx->alarmsToBeTraced, "*") != -1 || shgeti(ctx->alarmsToBeTraced, runner->dataWin->objt.objects[inst->objectIndex].name) != -1) {
            fprintf(stderr, "VM: [%s] Setting Alarm[%d] = %d (instanceId=%d)\n", runner->dataWin->objt.objects[inst->objectIndex].name, alarmIndex, value, inst->instanceId);
        }
#endif

        inst->alarm[alarmIndex] = value;
        if (value > 0) inst->activeAlarmMask |= (uint16_t) (1u << alarmIndex);
        else inst->activeAlarmMask &= (uint16_t) ~(1u << alarmIndex);
    }

    return RValue_makeUndefined();
}

static RValue builtin_alarm_get(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t alarmIndex = RValue_toInt32(args[0]);

    if (0 > alarmIndex || alarmIndex >= GML_ALARM_COUNT) {
        return RValue_makeReal(-1);
    }

    if (ctx->currentInstance != nullptr) {
        Instance* inst = ctx->currentInstance;
        return RValue_makeReal((GMLReal) inst->alarm[alarmIndex]);
    }

    return RValue_makeReal(-1);
}

#define LEGACY_DND_CMP_EQ 0
#define LEGACY_DND_CMP_LT 1
#define LEGACY_DND_CMP_GT 2
#define LEGACY_DND_CMP_LTE 3
#define LEGACY_DND_CMP_GTE 4

// action_if_variable(variable, value, op)
// Compares the variable against value using op (LEGACY_DND_CMP_*).
// String operands compare via strcmp; mismatched types compare unequal.
static RValue builtin_action_if_variable(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t op = RValue_toInt32(args[2]);
    GMLReal diff;

    bool aIsString = args[0].type == RVALUE_STRING;
    bool bIsString = args[1].type == RVALUE_STRING;
    if (aIsString != bIsString) {
        return RValue_makeBool(false);
    }
    if (aIsString) {
        const char* sa = args[0].string != nullptr ? args[0].string : "";
        const char* sb = args[1].string != nullptr ? args[1].string : "";
        diff = (GMLReal) strcmp(sa, sb);
    } else {
        diff = (GMLReal) (RValue_toReal(args[0]) - RValue_toReal(args[1]));
    }

    bool result;
    if (op == LEGACY_DND_CMP_LT) result = diff < 0.0;
    else if (op == LEGACY_DND_CMP_GT) result = diff > 0.0;
    else if (op == LEGACY_DND_CMP_LTE) result = diff <= 0.0;
    else if (op == LEGACY_DND_CMP_GTE) result = diff >= 0.0;
    else result = diff == 0.0f;
    return RValue_makeBool(result);
}

static RValue builtin_action_if(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(RValue_toBool(args[0]));
}

static RValue builtin_action_if_dice(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);

    int32_t probability = RValue_toInt32(args[0]);
    if (probability <= 1) {
        return RValue_makeBool(probability > 0);
    }

    return RValue_makeBool((rand() % probability) == 0);
}

static RValue builtin_action_set_score(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal val = RValue_toReal(args[0]);
    if (ctx->actionRelativeFlag) runner->score += val;
    else runner->score = val;
    return RValue_makeUndefined();
}

static RValue builtin_action_if_score(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal value = RValue_toReal(args[0]);
    int32_t op = RValue_toInt32(args[1]);
    bool result;
    if (op == LEGACY_DND_CMP_LT) result = runner->score < value;
    else if (op == LEGACY_DND_CMP_GT) result = runner->score > value;
    else result = runner->score == value;
    return RValue_makeBool(result);
}

// Shared implementation for action_draw_score and action_draw_life: draws "caption + value" at (x, y), respecting the relative flag.
// The value is integer-formatted to match the native runner's "%d" output.
static void drawLegacyDndCaptionedCounter(VMContext* ctx, RValue* args, int32_t intValue) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return;

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* caption = RValue_toString(args[2]);

    applyActionRelativeOffset(ctx, &x, &y);

    char numBuf[64];
    snprintf(numBuf, sizeof(numBuf), "%d", intValue);
    size_t captionLen = strlen(caption);
    size_t numLen = strlen(numBuf);
    char* combined = (char*) safeMalloc(captionLen + numLen + 1);
    memcpy(combined, caption, captionLen);
    memcpy(combined + captionLen, numBuf, numLen + 1);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, combined);
    runner->renderer->vtable->drawText(runner->renderer, processedText.text, x, y, 1.0f, 1.0f, 0.0f, -1.0f);
    PreprocessedText_free(processedText);
    free(combined);
    free(caption);
}

static RValue builtin_action_draw_score(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    drawLegacyDndCaptionedCounter(ctx, args, (int32_t) ctx->runner->score);
    return RValue_makeUndefined();
}

static RValue builtin_action_set_life(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal val = RValue_toReal(args[0]);
    Runner_setLives(runner, ctx->actionRelativeFlag ? runner->lives + val : val);
    return RValue_makeUndefined();
}

static RValue builtin_action_if_life(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal value = RValue_toReal(args[0]);
    int32_t op = RValue_toInt32(args[1]);
    bool result;
    if (op == LEGACY_DND_CMP_LT) result = runner->lives < value;
    else if (op == LEGACY_DND_CMP_GT) result = runner->lives > value;
    else result = runner->lives == value;
    return RValue_makeBool(result);
}

static RValue builtin_action_draw_life(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    drawLegacyDndCaptionedCounter(ctx, args, (int32_t) ctx->runner->lives);
    return RValue_makeUndefined();
}

static RValue builtin_action_draw_life_images(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    int32_t spriteIndex = RValue_toInt32(args[2]);

    if (0 > spriteIndex || runner->dataWin->sprt.count <= (uint32_t) spriteIndex) return RValue_makeUndefined();

    applyActionRelativeOffset(ctx, &x, &y);

    int32_t spriteWidth = (int32_t) runner->dataWin->sprt.sprites[spriteIndex].width;
    int32_t lives = (int32_t) runner->lives;
    for (int32_t i = 0; lives > i; i++) {
        Renderer_drawSprite(runner->renderer, spriteIndex, 0, x + (float) (i * spriteWidth), y);
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_health(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal val = RValue_toReal(args[0]);
    Runner_setHealth(runner, ctx->actionRelativeFlag ? runner->health + val : val);
    return RValue_makeUndefined();
}

static RValue builtin_action_if_health(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    GMLReal value = RValue_toReal(args[0]);
    int32_t op = RValue_toInt32(args[1]);
    bool result;
    if (op == LEGACY_DND_CMP_LT) result = runner->health < value;
    else if (op == LEGACY_DND_CMP_GT) result = runner->health > value;
    else result = runner->health == value;
    return RValue_makeBool(result);
}

// action_if_aligned(hsnap, vsnap) - Returns true if self.x is a multiple of hsnap AND self.y is a multiple of vsnap.
// A snap value <= 0 disables that axis check.
static RValue builtin_action_if_aligned(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* self = ctx->currentInstance;
    if (self == nullptr) return RValue_makeBool(false);

    GMLReal hsnap = RValue_toReal(args[0]);
    GMLReal vsnap = RValue_toReal(args[1]);

    if (hsnap > 0.0) {
        GMLReal q = self->x / hsnap;
        GMLReal rounded = (GMLReal) (long) (q + (q >= 0.0 ? 0.5 : -0.5));
        if (((self->x - hsnap * rounded) > 0.001) || (-0.001 > (self->x - hsnap * rounded))) return RValue_makeBool(false);
    }
    if (vsnap > 0.0) {
        GMLReal q = self->y / vsnap;
        GMLReal rounded = (GMLReal) (long) (q + (q >= 0.0 ? 0.5 : -0.5));
        if (((self->y - vsnap * rounded) > 0.001) || (-0.001 > (self->y - vsnap * rounded))) return RValue_makeBool(false);
    }
    return RValue_makeBool(true);
}

// action_if_collision(x, y, kind)
// * kind 0: "only solid": returns true if NOT place_free (there's a solid collision)
// * kind 1: "all": returns true if NOT place_empty (there's any collision)
// When relative flag is set, x/y are offsets from self.
static RValue builtin_action_if_collision(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* self = ctx->currentInstance;
    if (self == nullptr) return RValue_makeBool(false);

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    int32_t kind = RValue_toInt32(args[2]);
    applyActionRelativeOffset(ctx, &x, &y);

    RValue posArgs[2] = { RValue_makeReal((GMLReal) x), RValue_makeReal((GMLReal) y) };
    RValue inner = (kind == 0) ? builtin_place_free(ctx, posArgs, 2) : builtin_place_empty(ctx, posArgs, 2);
    return RValue_makeBool(!RValue_toBool(inner));
}

// action_if_empty(x, y, kind)
// * kind 0: returns place_free(x, y)
// * kind 1: returns place_empty(x, y)
static RValue builtin_action_if_empty(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* self = ctx->currentInstance;
    if (self == nullptr) return RValue_makeBool(true);

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    int32_t kind = RValue_toInt32(args[2]);
    applyActionRelativeOffset(ctx, &x, &y);

    RValue posArgs[2] = { RValue_makeReal((GMLReal) x), RValue_makeReal((GMLReal) y) };
    return (kind == 0) ? builtin_place_free(ctx, posArgs, 2) : builtin_place_empty(ctx, posArgs, 2);
}

// action_if_object(obj, x, y)
// Returns true if the self instance, moved to (x, y), would be touching an instance of obj.
// Equivalent to place_meeting(x, y, obj). Relative flag offsets x/y from self.
static RValue builtin_action_if_object(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* self = ctx->currentInstance;
    if (self == nullptr) return RValue_makeBool(false);

    int32_t obj = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    applyActionRelativeOffset(ctx, &x, &y);

    RValue meetArgs[3] = { RValue_makeReal((GMLReal) x), RValue_makeReal((GMLReal) y), RValue_makeReal((GMLReal) obj) };
    return builtin_place_meeting(ctx, meetArgs, 3);
}

// action_if_number(obj, number, op)
// * op 0: instance_number(obj) == number
// * op 1: instance_number(obj) < number
// * op 2: instance_number(obj) > number
static RValue builtin_action_if_number(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    GMLReal value = RValue_toReal(args[1]);
    int32_t op = RValue_toInt32(args[2]);

    RValue numArgs[1] = { args[0] };
    GMLReal count = RValue_toReal(builtin_instance_number(ctx, numArgs, 1));

    bool result;
    if (op == LEGACY_DND_CMP_LT) result = count < value;
    else if (op == LEGACY_DND_CMP_GT) result = count > value;
    else result = count == value;
    return RValue_makeBool(result);
}

// action_if_next_room()
// Returns true if there IS a room after the current one in room order.
static RValue builtin_action_if_next_room(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    uint32_t count = runner->dataWin->gen8.roomOrderCount;
    if (count == 0) return RValue_makeBool(false);
    int32_t lastRoom = runner->dataWin->gen8.roomOrder[count - 1];
    return RValue_makeBool(runner->currentRoomIndex != lastRoom);
}

// action_if_previous_room()
// Returns true if there IS a room before the current one in room order.
static RValue builtin_action_if_previous_room(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    uint32_t count = runner->dataWin->gen8.roomOrderCount;
    if (count == 0) return RValue_makeBool(false);
    int32_t firstRoom = runner->dataWin->gen8.roomOrder[0];
    return RValue_makeBool(runner->currentRoomIndex != firstRoom);
}

STUB_RETURN_FALSE(action_if_mouse)
STUB_RETURN_FALSE(action_if_question)

// DnD "back" / "bar" color preset in BGR format enum used by action_draw_health.
// Indices match the GMS 1.x DnD dropdowns:
// Stored as BGR (GameMaker color order) so they can be passed straight to the renderer.
static const uint32_t DND_PALETTE_BGR[17] = {
    0x000000, // 0 = no fill (the "no fill" part is handled on the handler itself)
    0x000000, // 1 = black
    0x808080, // 2 = gray
    0xC0C0C0, // 3 = silver
    0xFFFFFF, // 4 = white
    0x000080, // 5 = maroon
    0x008000, // 6 = green
    0x008080, // 7 = olive
    0x800000, // 8 = navy
    0x800080, // 9 = purple
    0x808000, // 10 = teal
    0x0000FF, // 11 = red
    0x00FF00, // 12 = lime
    0x00FFFF, // 13 = yellow
    0xFF0000, // 14 = blue
    0xFF00FF, // 15 = fuchsia
    0xFFFF00, // 16 = aqua
};

static RValue builtin_action_draw_health(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x1 = (float) RValue_toReal(args[0]);
    float y1 = (float) RValue_toReal(args[1]);
    float x2 = (float) RValue_toReal(args[2]);
    float y2 = (float) RValue_toReal(args[3]);
    int32_t backIdx = RValue_toInt32(args[4]);
    int32_t barIdx = RValue_toInt32(args[5]);

    applyActionRelativeOffset(ctx, &x1, &y1);
    applyActionRelativeOffset(ctx, &x2, &y2);

    if (0 > backIdx || backIdx >= 17) backIdx = 0;
    if (0 > barIdx || barIdx >= 17) barIdx = 12; // lime fallback

    // Optional back fill.
    if (backIdx > 0) {
        uint32_t backCol = DND_PALETTE_BGR[backIdx];
        runner->renderer->vtable->drawRectangle(runner->renderer, x1, y1, x2, y2, backCol, runner->renderer->drawAlpha, false);
    }

    GMLReal amount = runner->health;
    if (0.0 > amount) amount = 0.0;
    if (amount > 100.0) amount = 100.0;
    float fillFraction = (float) (amount / 100.0);
    if (fillFraction > 0.0f) {
        uint32_t barCol = DND_PALETTE_BGR[barIdx];
        float fillX = x1 + (x2 - x1) * fillFraction;
        runner->renderer->vtable->drawRectangle(runner->renderer, x1, y1, fillX, y2, barCol, runner->renderer->drawAlpha, false);
    }
    return RValue_makeUndefined();
}

// action_sprite_set(sprite_id, sub_img, image_speed) - sets the calling instance's sprite, image_index (only if >= 0), and image_speed.
static RValue builtin_action_sprite_set(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();
    int32_t spriteId = RValue_toInt32(args[0]);
    GMLReal subImg = RValue_toReal(args[1]);
    GMLReal speed = RValue_toReal(args[2]);
    inst->spriteIndex = spriteId;
    if (subImg >= 0.0) inst->imageIndex = (float) subImg;
    inst->imageSpeed = (float) speed;
    return RValue_makeUndefined();
}

// action_sprite_color(color, alpha) - sets the calling instance's image_blend and image_alpha.
static RValue builtin_action_sprite_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();
    inst->imageBlend = (uint32_t) RValue_toInt32(args[0]);
    inst->imageAlpha = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

// action_message(text) - shows a dialog.
static RValue builtin_action_message(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    char* text = RValue_toString(args[0]);
    fprintf(stderr, "VM: action_message: %s\n", text);
    free(text);
    return RValue_makeUndefined();
}

// action_another_room(room_id) - jumps to the given room.
static RValue builtin_action_another_room(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: action_another_room called but no runner!");
    runner->pendingRoom = RValue_toInt32(args[0]);
    return RValue_makeUndefined();
}

// action_current_room() -  restarts the current room.
static RValue builtin_action_current_room(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: action_current_room called but no runner!");
    runner->pendingRoom = runner->currentRoomIndex;
    return RValue_makeUndefined();
}

// action_next_room() - goes to the next room in the room order.
static RValue builtin_action_next_room(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: action_next_room called but no runner!");
    int32_t nextPos = runner->currentRoomOrderPosition + 1;
    if ((int32_t) runner->dataWin->gen8.roomOrderCount > nextPos) {
        runner->pendingRoom = runner->dataWin->gen8.roomOrder[nextPos];
    } else {
        fprintf(stderr, "VM: action_next_room - already at last room!\n");
    }
    return RValue_makeUndefined();
}

// action_reverse_xdir() - negates the calling instance's hspeed.
static RValue builtin_action_reverse_xdir(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();
    inst->hspeed = -inst->hspeed;
    Instance_computeSpeedFromComponents(inst);
    return RValue_makeUndefined();
}

// action_reverse_ydir() - negates the calling instance's vspeed.
static RValue builtin_action_reverse_ydir(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();
    inst->vspeed = -inst->vspeed;
    Instance_computeSpeedFromComponents(inst);
    return RValue_makeUndefined();
}

// action_color(color) - sets the current draw color.
static RValue builtin_action_color(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawColor = (uint32_t) RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

// action_font(font, halign) - sets the current draw font and horizontal alignment.
static RValue builtin_action_font(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawFont = RValue_toInt32(args[0]);
        runner->renderer->drawHalign = RValue_toInt32(args[1]);
    }
    return RValue_makeUndefined();
}

// action_draw_text(text, x, y) - draws text at (x, y), respecting the relative flag.
static RValue builtin_action_draw_text(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    char* str = RValue_toString(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);

    applyActionRelativeOffset(ctx, &x, &y);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    runner->renderer->vtable->drawText(runner->renderer, processedText.text, x, y, 1.0f, 1.0f, 0.0f, -1.0f);
    PreprocessedText_free(processedText);
    free(str);
    return RValue_makeUndefined();
}

// action_draw_sprite(sprite_id, x, y, subimg) - draws the sprite at (x, y) using subimg (or instance's image_index if subimg < 0), respecting the relative flag.
static RValue builtin_action_draw_sprite(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteId = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    int32_t subimg = RValue_toInt32(args[3]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }
    applyActionRelativeOffset(ctx, &x, &y);

    Renderer_drawSprite(runner->renderer, spriteId, subimg, x, y);
    return RValue_makeUndefined();
}

// action_draw_variable(value, x, y) - draws the value as text at (x, y), respecting the relative flag.
static RValue builtin_action_draw_variable(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    char* str = RValue_toString(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);

    applyActionRelativeOffset(ctx, &x, &y);

    PreprocessedText processedText = TextUtils_preprocessGmlTextIfNeeded(runner, str);
    runner->renderer->vtable->drawText(runner->renderer, processedText.text, x, y, 1.0f, 1.0f, 0.0f, -1.0f);
    PreprocessedText_free(processedText);
    free(str);
    return RValue_makeUndefined();
}

// ===[ Tile Layer Functions ]===

static TileLayerState* getOrCreateTileLayer(Runner* runner, int32_t depth) {
    ptrdiff_t idx = hmgeti(runner->tileLayerMap, depth);
    if (0 > idx) {
        TileLayerState defaultVal = {0};
        defaultVal.visible = true;
        hmput(runner->tileLayerMap, depth, defaultVal);
        idx = hmgeti(runner->tileLayerMap, depth);
    }
    return &runner->tileLayerMap[idx].value;
}

static RValue builtin_tile_layer_hide(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    TileLayerState* layer = getOrCreateTileLayer(runner, depth);
    layer->visible = false;
    return RValue_makeUndefined();
}

static RValue builtin_tile_layer_show(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    TileLayerState* layer = getOrCreateTileLayer(runner, depth);
    layer->visible = true;
    return RValue_makeUndefined();
}

static RValue builtin_tile_layer_shift(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    float dx = (float) RValue_toReal(args[1]);
    float dy = (float) RValue_toReal(args[2]);
    TileLayerState* layer = getOrCreateTileLayer(runner, depth);
    layer->offsetX += dx;
    layer->offsetY += dy;
    return RValue_makeUndefined();
}

// tile_add(background, left, top, width, height, x, y, depth) - creates a new tile in the current room and returns its id.
static RValue builtin_tile_add(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    if (room == nullptr) return RValue_makeReal(-1.0);

    int32_t backgroundIndex = RValue_toInt32(args[0]);
    if (0 > backgroundIndex || backgroundIndex >= (int32_t) ctx->dataWin->bgnd.count) {
        fprintf(stderr, "VM: tile_add: background does not exist (%d)\n", backgroundIndex);
        return RValue_makeReal(-1.0);
    }

    uint32_t newId = runner->nextInstanceId++;
    uint32_t newCount = room->tileCount + 1;
    room->tiles = safeRealloc(room->tiles, newCount * sizeof(RoomTile));
    RoomTile* tile = &room->tiles[room->tileCount];
    tile->x = RValue_toInt32(args[5]);
    tile->y = RValue_toInt32(args[6]);
    tile->useSpriteDefinition = false; // Will never be true because this function is only available for pre-GM:S 2 games
    tile->backgroundDefinition = backgroundIndex;
    tile->sourceX = RValue_toInt32(args[1]);
    tile->sourceY = RValue_toInt32(args[2]);
    tile->width = (uint32_t) RValue_toInt32(args[3]);
    tile->height = (uint32_t) RValue_toInt32(args[4]);
    tile->tileDepth = RValue_toInt32(args[7]);
    tile->instanceID = newId;
    tile->scaleX = 1.0f;
    tile->scaleY = 1.0f;
    tile->color = 0xFFFFFFFFu;
    tile->alpha = 1.0f;
    room->tileCount = newCount;

    runner->drawableListStructureDirty = true;
    return RValue_makeReal((GMLReal) newId);
}

// tile_exists(id) - returns true if a tile with that id exists in the current room.
static RValue builtin_tile_exists(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    if (room == nullptr) return RValue_makeBool(false);
    uint32_t id = (uint32_t) RValue_toInt32(args[0]);
    repeat(room->tileCount, i) {
        if (room->tiles[i].instanceID == id) return RValue_makeBool(true);
    }
    return RValue_makeBool(false);
}

// tile_layer_find(depth, x, y) - returns the id of the topmost tile at depth covering (x, y), or -1.
static RValue builtin_tile_layer_find(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    if (room == nullptr) return RValue_makeReal(-1.0);
    int32_t depth = RValue_toInt32(args[0]);
    GMLReal qx = RValue_toReal(args[1]);
    GMLReal qy = RValue_toReal(args[2]);
    // Walk in reverse so the most recently added tile (drawn on top) wins.
    for (int32_t i = (int32_t) room->tileCount - 1; i >= 0; i--) {
        RoomTile* tile = &room->tiles[i];
        if (tile->tileDepth != depth) continue;
        GMLReal w = (GMLReal) tile->width * (GMLReal) tile->scaleX;
        GMLReal h = (GMLReal) tile->height * (GMLReal) tile->scaleY;
        if (qx >= (GMLReal) tile->x && qx < (GMLReal) tile->x + w && qy >= (GMLReal) tile->y && qy < (GMLReal) tile->y + h) {
            return RValue_makeReal((GMLReal) tile->instanceID);
        }
    }
    return RValue_makeReal(-1.0);
}

// tile_layer_delete(depth) - removes every tile at the given depth from the current room.
static RValue builtin_tile_layer_delete(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    if (room == nullptr) return RValue_makeUndefined();
    int32_t depth = RValue_toInt32(args[0]);
    uint32_t write = 0;
    bool removedAny = false;
    repeat(room->tileCount, i) {
        if (room->tiles[i].tileDepth == depth) { removedAny = true; continue; }
        if (write != i) room->tiles[write] = room->tiles[i];
        write++;
    }
    room->tileCount = write;
    if (removedAny) runner->drawableListStructureDirty = true;
    return RValue_makeUndefined();
}

// tile_delete(id) - removes the tile with the given id from the current room.
static RValue builtin_tile_delete(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    if (room == nullptr) return RValue_makeUndefined();
    uint32_t id = (uint32_t) RValue_toInt32(args[0]);
    repeat(room->tileCount, i) {
        if (room->tiles[i].instanceID != id) continue;
        uint32_t tailLen = room->tileCount - i - 1;
        if (tailLen > 0) memmove(&room->tiles[i], &room->tiles[i + 1], tailLen * sizeof(RoomTile));
        room->tileCount--;
        runner->drawableListStructureDirty = true;
        return RValue_makeUndefined();
    }
    fprintf(stderr, "VM: tile_delete: tile does not exist (%u)\n", id);
    return RValue_makeUndefined();
}

// tile_set_alpha(id, alpha) - sets the alpha (0.0 to 1.0) of the tile with the given id.
static RValue builtin_tile_set_alpha(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    if (room == nullptr) return RValue_makeUndefined();
    uint32_t id = (uint32_t) RValue_toInt32(args[0]);
    float alpha = (float) RValue_toReal(args[1]);
    repeat(room->tileCount, i) {
        if (room->tiles[i].instanceID != id) continue;
        room->tiles[i].alpha = alpha;
        return RValue_makeUndefined();
    }
    fprintf(stderr, "VM: tile_set_alpha: tile does not exist (%u)\n", id);
    return RValue_makeUndefined();
}

// tile_get_ids_at_depth(depth) - returns a 1D array of tile ids whose tileDepth matches.
static RValue builtin_tile_get_ids_at_depth(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    Room* room = runner->currentRoom;
    int32_t depth = RValue_toInt32(args[0]);
    int32_t matchCount = 0;
    if (room != nullptr) {
        repeat(room->tileCount, i) {
            if (room->tiles[i].tileDepth == depth) matchCount++;
        }
    }
    GMLArray* out = GMLArray_create(matchCount > 0 ? matchCount : 1);
    if (matchCount > 0) {
        int32_t w = 0;
        repeat(room->tileCount, i) {
            RoomTile* tile = &room->tiles[i];
            if (tile->tileDepth != depth) continue;
            *GMLArray_slot(out, w++) = RValue_makeReal((GMLReal) tile->instanceID);
        }
    }
    return RValue_makeArray(out);
}

// ===[ Layer Functions ]===

static RValue builtin_layer_force_draw_depth(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    runner->forceDrawDepth = RValue_toBool(args[0]);
    runner->forcedDepth = RValue_toInt32(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_is_draw_depth_forced(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeBool(runner->forceDrawDepth);
}

static RValue builtin_layer_get_forced_depth(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    return RValue_makeReal((GMLReal) runner->forcedDepth);
}

// ===[ GMS2 Layer Runtime API ]===

// GMS layer functions accept either a numeric layer id or a layer name string.
// Returns the resolved runtime id, or -1 if no match.
static int32_t resolveLayerIdArg(Runner* runner, RValue arg) {
    if (arg.type == RVALUE_STRING) {
        const char* name = arg.string;
        if (name == nullptr) return -1;
        size_t runtimeLayerCount = arrlenu(runner->runtimeLayers);
        repeat(runtimeLayerCount, i) {
            RuntimeLayer* rl = &runner->runtimeLayers[i];
            if (rl->dynamic && strcmp(rl->dynamicName, name) == 0)
                return (int32_t) rl->id;
        }
        if (runner->currentRoom != nullptr) {
            repeat(runner->currentRoom->layerCount, i) {
                RoomLayer* layer = &runner->currentRoom->layers[i];
                if (layer->name != nullptr && strcmp(layer->name, name) == 0)
                    return (int32_t) layer->id;
            }
        }
        return -1;
    }
    return RValue_toInt32(arg);
}

static void instanceSetLayerActiveState(Runner* runner, int32_t layerId, bool isActive) {
    if (0 > layerId || runner->currentRoom == nullptr) return;

    repeat(runner->currentRoom->layerCount, layerIndex) {
        RoomLayer* layer = &runner->currentRoom->layers[layerIndex];

        if ((int32_t) layer->id != layerId)
            continue;

        if (layer->type != RoomLayerType_Instances || layer->instancesData == nullptr)
            break;

        RoomLayerInstancesData* layerData = layer->instancesData;

        repeat(layerData->instanceCount, instanceIndex) {
            Instance* inst = hmget(runner->instancesById, layerData->instanceIds[instanceIndex]);
            if (inst != nullptr && !inst->destroyed)
                Runner_setActiveState(runner, inst, isActive);
        }
        return;
    }
}

static RValue builtin_instance_activate_layer(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t layerId = resolveLayerIdArg(runner, args[0]);
    instanceSetLayerActiveState(runner, layerId, true);
    return RValue_makeUndefined();
}

static RValue builtin_instance_deactivate_layer(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t layerId = resolveLayerIdArg(runner, args[0]);
    instanceSetLayerActiveState(runner, layerId, false);
    return RValue_makeUndefined();
}

static RValue builtin_layer_get_id(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    char* name = RValue_toString(args[0]);
    if (name == nullptr) return RValue_makeReal(-1.0);
    int32_t result = -1;
    // Check dynamic layers first (they may shadow a parsed layer by name).
    size_t runtimeLayerCount = arrlenu(runner->runtimeLayers);
    repeat(runtimeLayerCount, i) {
        RuntimeLayer* runtimeLayer = &runner->runtimeLayers[i];
        if (runtimeLayer->dynamic && strcmp(runtimeLayer->dynamicName, name) == 0) {
            result = (int32_t) runtimeLayer->id;
            break;
        }
    }
    if (result == -1 && runner->currentRoom != nullptr) {
        repeat(runner->currentRoom->layerCount, i) {
            RoomLayer* layer = &runner->currentRoom->layers[i];
            if (layer->name != nullptr && strcmp(layer->name, name) == 0) {
                result = (int32_t) layer->id;
                break;
            }
        }
    }
    free(name);
    return RValue_makeReal((GMLReal) result);
}

static RValue builtin_layer_exists(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    return RValue_makeBool(Runner_findRuntimeLayerById(runner, id) != nullptr);
}

static RValue builtin_layer_get_name(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr && runtimeLayer->dynamic)
        return RValue_makeString(runtimeLayer->dynamicName);

    RoomLayer* roomLayer = Runner_findRoomLayerById(runner->currentRoom, id);
    if (roomLayer == nullptr || roomLayer->name == nullptr)
        return RValue_makeString("");

    return RValue_makeString(roomLayer->name);
}

static RValue builtin_layer_get_depth(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return RValue_makeUndefined();

    return RValue_makeReal((GMLReal) runtimeLayer->depth);
}

static RValue builtin_layer_depth(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    int32_t depth = RValue_toInt32(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr && runtimeLayer->depth != depth) {
        runtimeLayer->depth = depth;
        runner->drawableListSortDirty = true;
    }

    return RValue_makeUndefined();
}

static RValue builtin_layer_get_visible(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return RValue_makeBool(false);

    return RValue_makeBool(runtimeLayer->visible);
}

static RValue builtin_layer_set_visible(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    bool visible = RValue_toBool(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr)
        runtimeLayer->visible = visible;

    return RValue_makeUndefined();
}

static RValue builtin_layer_get_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return RValue_makeReal(0.0);

    return RValue_makeReal((GMLReal) runtimeLayer->xOffset);
}

static RValue builtin_layer_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    float x = (float) RValue_toReal(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr)
        runtimeLayer->xOffset = x;

    return RValue_makeUndefined();
}

static RValue builtin_layer_get_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return RValue_makeReal(0.0);

    return RValue_makeReal((GMLReal) runtimeLayer->yOffset);
}

static RValue builtin_layer_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    float y = (float) RValue_toReal(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr)
        runtimeLayer->yOffset = y;

    return RValue_makeUndefined();
}

static RValue builtin_layer_get_hspeed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return RValue_makeReal(0.0);

    return RValue_makeReal((GMLReal) runtimeLayer->hSpeed);
}

static RValue builtin_layer_hspeed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    float hs = (float) RValue_toReal(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr)
        runtimeLayer->hSpeed = hs;

    return RValue_makeUndefined();
}

static RValue builtin_layer_get_vspeed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return RValue_makeReal(0.0);

    return RValue_makeReal((GMLReal) runtimeLayer->vSpeed);
}

// Creates a new dynamic layer. Signatures: layer_create(depth) or layer_create(depth, name).
static RValue builtin_layer_create(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    char* name = nullptr;
    uint32_t id = Runner_getNextLayerId(runner);
    if (argCount > 1) {
        name = RValue_toString(args[1]);
    } else {
        // Technically could be smaller, but let's be safe
        char* generatedName = safeMalloc(16);
        snprintf(generatedName, 16, "_layer_%x", id);
        name = generatedName;
    }
    RuntimeLayer runtimeLayer = {0};
    runtimeLayer.id = id;
    runtimeLayer.depth = depth;
    runtimeLayer.visible = true;
    runtimeLayer.dynamic = true;
    runtimeLayer.dynamicName = name, // ownership transferred
    arrput(runner->runtimeLayers, runtimeLayer);
    runner->drawableListStructureDirty = true;
    return RValue_makeReal((GMLReal) id);
}

static RValue builtin_layer_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    size_t count = arrlenu(runner->runtimeLayers);
    repeat(count, i) {
        if ((int32_t) runner->runtimeLayers[i].id != id)
            continue;

        // Ignore if we are trying to delete a non-dynamic layer
        if (!runner->runtimeLayers[i].dynamic)
            return RValue_makeUndefined();

        Runner_freeRuntimeLayer(&runner->runtimeLayers[i]);
        arrdel(runner->runtimeLayers, i);
        runner->drawableListStructureDirty = true;
        break;
    }
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_create(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t layerId = resolveLayerIdArg(runner, args[0]);
    int32_t spriteIndex = RValue_toInt32(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, layerId);
    if (runtimeLayer == nullptr)
        return RValue_makeReal(-1.0);

    RuntimeBackgroundElement* bg = safeMalloc(sizeof(RuntimeBackgroundElement));
    bg->spriteIndex = spriteIndex;
    bg->visible = true;
    bg->htiled = false;
    bg->vtiled = false;
    bg->stretch = false;
    bg->xScale = 1.0f;
    bg->yScale = 1.0f;
    bg->blend = 0xFFFFFF;
    bg->alpha = 1.0f;
    bg->xOffset = 0.0f;
    bg->yOffset = 0.0f;
    bg->imageIndex = 0;
    RuntimeLayerElement el = {0};
    el.id = Runner_getNextLayerId(runner);
    el.type = RuntimeLayerElementType_Background;
    el.visible = true;
    el.alpha = 1.0f;
    el.blend = 0xFFFFFFu;
    el.backgroundElement = bg;
    arrput(runtimeLayer->elements, el);
    return RValue_makeReal((GMLReal) el.id);
}

static RValue builtin_layer_background_exists(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t layerId = resolveLayerIdArg(runner, args[0]);
    int32_t elementId = RValue_toInt32(args[1]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, layerId);
    if (runtimeLayer == nullptr)
        return RValue_makeBool(false);

    size_t count = arrlenu(runtimeLayer->elements);
    repeat(count, i) {
        if ((int32_t) runtimeLayer->elements[i].id == elementId && runtimeLayer->elements[i].type == RuntimeLayerElementType_Background) {
            return RValue_makeBool(true);
        }
    }
    return RValue_makeBool(false);
}

static RuntimeBackgroundElement* findBackgroundElement(Runner* runner, int32_t elementId) {
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, elementId, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Background)
        return nullptr;
    return el->backgroundElement;
}

// Resolves a background element id to a parsed Background layer's data.
static RoomLayerBackgroundData* findParsedBackgroundData(Runner* runner, int32_t elementId) {
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, elementId, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Background)
        return nullptr;
    return el->parsedBackgroundData;
}

// Resolves a tilemap element id to its tiles data + owning runtime layer.
static RoomLayerTilesData* findTilemapData(Runner* runner, int32_t elementId, RuntimeLayer** outLayer) {
    if (outLayer != nullptr) *outLayer = nullptr;
    RuntimeLayer* owner = nullptr;
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, elementId, &owner);
    if (el == nullptr || el->type != RuntimeLayerElementType_Tilemap)
        return nullptr;
    if (outLayer != nullptr) *outLayer = owner;
    return el->tilemapData;
}

static RValue builtin_layer_background_visible(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool visible = RValue_toBool(args[1]);
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, id);
    if (bg != nullptr)
        bg->visible = visible;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, id);
    if (parsed != nullptr)
        parsed->visible = visible;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_htiled(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool tiled = RValue_toBool(args[1]);
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, id);
    if (bg != nullptr)
        bg->htiled = tiled;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, id);
    if (parsed != nullptr)
        parsed->hTiled = tiled;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_vtiled(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool tiled = RValue_toBool(args[1]);
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, id);
    if (bg != nullptr)
        bg->vtiled = tiled;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, id);
    if (parsed != nullptr)
        parsed->vTiled = tiled;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_xscale(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, RValue_toInt32(args[0]));
    if (bg != nullptr)
        bg->xScale = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_yscale(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, RValue_toInt32(args[0]));
    if (bg != nullptr)
        bg->yScale = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_stretch(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool stretch = RValue_toBool(args[1]);
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, id);
    if (bg != nullptr)
        bg->stretch = stretch;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, id);
    if (parsed != nullptr)
        parsed->stretch = stretch;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_blend(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    uint32_t blend = (uint32_t) RValue_toInt32(args[1]) & 0x00FFFFFF;
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, id);
    if (bg != nullptr)
        bg->blend = blend;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, id);
    if (parsed != nullptr)
        parsed->color = (parsed->color & 0xFF000000u) | blend;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_alpha(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    float alpha = (float) RValue_toReal(args[1]);
    RuntimeBackgroundElement* bg = findBackgroundElement(runner, id);
    if (bg != nullptr)
        bg->alpha = alpha;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, id);
    if (parsed != nullptr) {
        float clamped = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        uint32_t alphaByte = (uint32_t) (clamped * 255.0f);
        parsed->color = (parsed->color & 0x00FFFFFFu) | (alphaByte << 24);
    }
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_sprite(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t elementId = RValue_toInt32(args[0]);
    int32_t spriteIndex = RValue_toInt32(args[1]);

    RuntimeBackgroundElement* bg = findBackgroundElement(runner, elementId);
    if (bg != nullptr)
        bg->spriteIndex = spriteIndex;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, elementId);
    if (parsed != nullptr)
        parsed->spriteIndex = spriteIndex;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_get_id(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t layerId = resolveLayerIdArg(runner, args[0]);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, layerId);
    if (runtimeLayer != nullptr) {
        size_t count = arrlenu(runtimeLayer->elements);
        repeat(count, i) {
            if (runtimeLayer->elements[i].type == RuntimeLayerElementType_Background) {
                return RValue_makeReal((GMLReal) runtimeLayer->elements[i].id);
            }
        }
    }

    return RValue_makeReal(-1.0);
}

static RValue builtin_layer_background_index(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t background_element_id = RValue_toInt32(args[0]);
    int32_t index = RValue_toInt32(args[1]);

    RuntimeBackgroundElement* bg = findBackgroundElement(runner, background_element_id);
    if (bg != nullptr)
        bg->imageIndex = index;
    RoomLayerBackgroundData* parsed = findParsedBackgroundData(runner, background_element_id);
    if (parsed != nullptr)
        parsed->imageIndex = index;
    return RValue_makeUndefined();
}

static RValue builtin_layer_tile_alpha(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, RValue_toInt32(args[0]), nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Tile || el->tileElement == nullptr)
        return RValue_makeUndefined();
    el->alpha = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RoomTile* findTileElement(Runner* runner, RValue idArg) {
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, RValue_toInt32(idArg), nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Tile)
        return nullptr;
    return el->tileElement;
}

static RValue builtin_layer_tile_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile != nullptr)
        tile->x = RValue_toInt32(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_tile_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile != nullptr)
        tile->y = RValue_toInt32(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_tile_get_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) tile->x);
}

static RValue builtin_layer_tile_get_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) tile->y);
}

static RValue builtin_layer_tile_get_xscale(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile == nullptr)
        return RValue_makeReal(1.0);
    return RValue_makeReal((GMLReal) tile->scaleX);
}

static RValue builtin_layer_tile_get_yscale(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile == nullptr)
        return RValue_makeReal(1.0);
    return RValue_makeReal((GMLReal) tile->scaleY);
}

static RValue builtin_layer_tile_get_region(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    RoomTile* tile = findTileElement(ctx->runner, args[0]);
    if (tile == nullptr)
        return RValue_makeReal(-1.0);
    RValue arr = RValue_makeArray(GMLArray_create(4));
    GMLArray_setOnArrayRef(&arr, 0, RValue_makeReal((GMLReal) tile->sourceX));
    GMLArray_setOnArrayRef(&arr, 1, RValue_makeReal((GMLReal) tile->sourceY));
    GMLArray_setOnArrayRef(&arr, 2, RValue_makeReal((GMLReal) tile->width));
    GMLArray_setOnArrayRef(&arr, 3, RValue_makeReal((GMLReal) tile->height));
    return arr;
}

#if IS_WAD17_OR_HIGHER_ENABLED
static RValue builtin_layer_get_all_elements(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);

    RValue arr = RValue_makeArray(GMLArray_create(0));
    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer == nullptr)
        return arr;

    size_t count = arrlenu(runtimeLayer->elements);
    repeat(count, elementIndex) {
        RuntimeLayerElement* el = &runtimeLayer->elements[elementIndex];
        if (el->type == RuntimeLayerElementType_Instance) {
            // Instances destroyed mid-frame are reaped lazily; don't report their elements.
            Instance* elInst = hmget(runner->instancesById, el->instanceId);
            if (elInst == nullptr || elInst->destroyed) continue;
        }
        GMLArray_addOnArrayRef(&arr, RValue_makeReal((GMLReal) el->id));
    }
    return arr;
}

static RValue builtin_layer_instance_get_instance(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, RValue_toInt32(args[0]), nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Instance)
        return RValue_makeReal((GMLReal) INSTANCE_NOONE);
    return RValue_makeReal((GMLReal) el->instanceId);
}
#endif

static RValue builtin_layer_get_element_type(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);

    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    // layerelementtype_undefined == 0 matches GML's return for unknown/missing elements.
    if (el == nullptr)
        return RValue_makeReal(0.0);

    return RValue_makeReal((GMLReal) el->type);
}

static RValue builtin_layer_tile_visible(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool visible = RValue_toBool(args[1]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Tile) return RValue_makeUndefined();
    el->visible = visible;
    return RValue_makeUndefined();
}

static RValue builtin_layer_sprite_get_sprite(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);

    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(-1.0);

    return RValue_makeReal((GMLReal) el->spriteElement->spriteIndex);
}

static RValue builtin_layer_sprite_get_angle(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) el->spriteElement->rotation);
}

static RValue builtin_layer_sprite_get_alpha(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal(el->alpha);
}

static RValue builtin_layer_sprite_get_blend(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) el->blend);
}

static RValue builtin_layer_sprite_get_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) el->spriteElement->x);
}

static RValue builtin_layer_sprite_get_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) el->spriteElement->y);
}

static RValue builtin_layer_sprite_get_xscale(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(1.0);
    return RValue_makeReal((GMLReal) el->spriteElement->scaleX);
}

static RValue builtin_layer_sprite_get_yscale(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(1.0);
    return RValue_makeReal((GMLReal) el->spriteElement->scaleY);
}

static RValue builtin_layer_sprite_get_speed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) el->spriteElement->animationSpeed);
}

static RValue builtin_layer_sprite_get_index(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr)
        return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) el->spriteElement->frameIndex);
}

static RValue builtin_layer_sprite_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);

    RuntimeLayer* owningLayer = nullptr;
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, &owningLayer);
    if (el == nullptr || owningLayer == nullptr || el->type != RuntimeLayerElementType_Sprite)
        return RValue_makeUndefined();

    if (el->spriteElement != nullptr) {
        free(el->spriteElement);
        el->spriteElement = nullptr;
    }

    // Remove the element from the owning layer's element array to keep lookup + iteration tidy.
    size_t count = arrlenu(owningLayer->elements);
    repeat(count, i) {
        if (&owningLayer->elements[i] == el) {
            arrdel(owningLayer->elements, i);
            break;
        }
    }

    return RValue_makeUndefined();
}

static RValue builtin_layer_background_destroy(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);

    RuntimeLayer* owningLayer = nullptr;
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, id, &owningLayer);
    if (el == nullptr || owningLayer == nullptr || el->type != RuntimeLayerElementType_Background)
        return RValue_makeUndefined();

    if (el->backgroundElement != nullptr) {
        free(el->backgroundElement);
        el->backgroundElement = nullptr;
    }
    // The renderer draws parsed Background layers from the parsed data, so destroying the element must hide it there.
    if (el->parsedBackgroundData != nullptr) {
        el->parsedBackgroundData->visible = false;
        el->parsedBackgroundData = nullptr;
    }

    // Remove the element from the owning layer's element array to keep lookup + iteration tidy.
    size_t count = arrlenu(owningLayer->elements);
    repeat(count, i) {
        if (&owningLayer->elements[i] == el) {
            arrdel(owningLayer->elements, i);
            break;
        }
    }

    return RValue_makeUndefined();
}


#if IS_WAD17_OR_HIGHER_ENABLED
static RValue builtin_layer_tilemap_get_id(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    Runner* runner = ctx->runner;
    int32_t layerId = resolveLayerIdArg(runner, args[0]);
    if (0 > layerId) return RValue_makeReal(-1.0);

    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, layerId);
    if (runtimeLayer != nullptr) {
        size_t count = arrlenu(runtimeLayer->elements);
        repeat(count, i) {
            if (runtimeLayer->elements[i].type == RuntimeLayerElementType_Tilemap) {
                return RValue_makeReal((GMLReal) runtimeLayer->elements[i].id);
            }
        }
    }

    return RValue_makeReal(-1.0);
}

static RValue builtin_draw_tilemap(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLReal x = RValue_toReal(args[1]);
    GMLReal y = RValue_toReal(args[2]);

    RoomLayerTilesData* tilesData = findTilemapData(runner, RValue_toInt32(args[0]), nullptr);
    if (tilesData != nullptr) {
        Runner_drawTileLayer(runner, tilesData, x, y);
    }

    return RValue_makeUndefined();
}

// tilemap_x / tilemap_y set the owning runtime layer's draw offset for the tile layer identified by the tilemap element id.
static RValue builtin_tilemap_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLReal x = RValue_toReal(args[1]);

    RuntimeLayer* runtimeLayer = nullptr;
    if (findTilemapData(runner, RValue_toInt32(args[0]), &runtimeLayer) == nullptr) return RValue_makeUndefined();
    if (runtimeLayer != nullptr) runtimeLayer->xOffset = (float) x;
    return RValue_makeUndefined();
}

static RValue builtin_tilemap_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    GMLReal y = RValue_toReal(args[1]);

    RuntimeLayer* runtimeLayer = nullptr;
    if (findTilemapData(runner, RValue_toInt32(args[0]), &runtimeLayer) == nullptr) return RValue_makeUndefined();
    if (runtimeLayer != nullptr) runtimeLayer->yOffset = (float) y;
    return RValue_makeUndefined();
}

static RValue builtin_tilemap_get_x(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    Runner* runner = ctx->runner;

    RuntimeLayer* runtimeLayer = nullptr;
    if (findTilemapData(runner, RValue_toInt32(args[0]), &runtimeLayer) == nullptr || runtimeLayer == nullptr)
        return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal) runtimeLayer->xOffset);
}

static RValue builtin_tilemap_get_y(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    Runner* runner = ctx->runner;

    RuntimeLayer* runtimeLayer = nullptr;
    if (findTilemapData(runner, RValue_toInt32(args[0]), &runtimeLayer) == nullptr || runtimeLayer == nullptr)
        return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal) runtimeLayer->yOffset);
}

// Maps a room-space pixel coordinate to a flat tileData cell index, applying the owning layer's draw offset.
// Returns -1 when the tilemap/tileset is invalid or the coordinate falls outside the tilemap.
static int32_t tilemapGetCellIndexAtPixel(DataWin* dw, RoomLayerTilesData* data, RuntimeLayer* runtimeLayer, GMLReal findX, GMLReal findY, Background** outTileset) {
    if (data == nullptr || data->tileData == nullptr) return -1;
    if (0 > data->backgroundIndex) return -1;

    if ((uint32_t) data->backgroundIndex >= dw->bgnd.count) return -1;

    Background* tileset = &dw->bgnd.backgrounds[data->backgroundIndex];
    if (outTileset != nullptr) *outTileset = tileset;
    uint32_t tileW = tileset->gms2TileWidth;
    uint32_t tileH = tileset->gms2TileHeight;
    if (tileW == 0 || tileH == 0) return -1;

    float offsetX = runtimeLayer->xOffset; // GameMaker-HTML5: m_x
    float offsetY = runtimeLayer->yOffset; // GameMaker-HTML5: m_y

    GMLReal x = findX - (GMLReal) offsetX;
    GMLReal y = findY - (GMLReal) offsetY;

    GMLReal mapPixelW = (GMLReal) (data->tilesX * tileW);
    GMLReal mapPixelH = (GMLReal) (data->tilesY * tileH);
    // ATTENTION!!!
    // The GM-HTML5 runner has a bug here
    // if(x>=tmpw)
    //    return -1;
    // if(y>tmph)
    //    return -1;
    // The ORIGINAL runner uses >= for both
    if (0.0 > x || 0.0 > y || x >= mapPixelW || y >= mapPixelH) return -1;

    int32_t indexX = (int32_t) GMLReal_floor(x / (GMLReal) tileW);
    int32_t indexY = (int32_t) GMLReal_floor(y / (GMLReal) tileH);
    if (0 > indexX) indexX = 0;
    if (0 > indexY) indexY = 0;
    if (indexX >= (int32_t) data->tilesX) indexX = (int32_t) data->tilesX - 1;
    if (indexY >= (int32_t) data->tilesY) indexY = (int32_t) data->tilesY - 1;

    return indexY * ((int32_t) data->tilesX) + indexX;
}

// tilemap_get_at_pixel(tilemapElementId, x, y): returns the raw tile cell value (index + mirror/flip/rotate bits) at the given room-space pixel coordinate, or -1 if the coordinate falls outside the tilemap.
// (see GameMaker-HTML5 Function_Layers.js)
static RValue builtin_tilemap_get_at_pixel(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(-1.0);
    Runner* runner = ctx->runner;
    RuntimeLayer* runtimeLayer = nullptr;
    RoomLayerTilesData* data = findTilemapData(runner, RValue_toInt32(args[0]), &runtimeLayer);
    requireNotNullMessage(runtimeLayer, "Missing Runtime Layer! Bug?");

    int32_t cellIndex = tilemapGetCellIndexAtPixel(ctx->dataWin, data, runtimeLayer, RValue_toReal(args[1]), RValue_toReal(args[2]), nullptr);
    if (0 > cellIndex) return RValue_makeReal(-1.0);

    uint32_t cell = data->tileData[cellIndex];
    return RValue_makeReal((GMLReal) cell);
}

// tilemap_set_at_pixel(tilemapElementId, tiledata, x, y): writes the raw tile cell value at the given room-space pixel coordinate. Returns whether the write happened.
// (see GameMaker-HTML5 Function_Layers.js)
static RValue builtin_tilemap_set_at_pixel(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (4 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    RuntimeLayer* runtimeLayer = nullptr;
    RoomLayerTilesData* data = findTilemapData(runner, RValue_toInt32(args[0]), &runtimeLayer);
    requireNotNullMessage(runtimeLayer, "Missing Runtime Layer! Bug?");

    Background* tileset = nullptr;
    int32_t cellIndex = tilemapGetCellIndexAtPixel(ctx->dataWin, data, runtimeLayer, RValue_toReal(args[2]), RValue_toReal(args[3]), &tileset);
    if (0 > cellIndex) return RValue_makeBool(false);

    uint32_t cell = (uint32_t) RValue_toInt32(args[1]);
    uint32_t tileIndex = cell & 0x7ffff;
    if (tileset->gms2TileCount != 0 && tileIndex >= tileset->gms2TileCount) {
        fprintf(stderr, "VM: [%s] tilemap_set_at_pixel() - tile index outside tile set count\n", ctx->currentCodeName);
        return RValue_makeBool(false);
    }

    data->tileData[cellIndex] = cell;
    return RValue_makeBool(true);
}

// tilemap_get_tileset(tilemapElementId): returns the BGND (tileset) index backing the tilemap, or -1.
// (see GameMaker-HTML5 Function_Layers.js tilemap_get_tileset)
static RValue builtin_tilemap_get_tileset(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    RoomLayerTilesData* data = findTilemapData(ctx->runner, RValue_toInt32(args[0]), nullptr);
    if (data == nullptr) return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal) data->backgroundIndex);
}

// tile_get_index(tiledata): extracts the tileset cell index from a raw tile cell value, masking off the mirror/flip/rotate bits.
// (see GameMaker-HTML5 Function_Layers.js: TileIndex_Mask = 0x7ffff, TileIndex_Shift = 0)
static RValue builtin_tile_get_index(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal) (RValue_toInt32(args[0]) & 0x7ffff));
}

// tile_set_empty(tiledata): clears the tileset cell index from a raw tile cell value, keeping the mirror/flip/rotate bits.
// (see GameMaker-HTML5 Function_Layers.js tile_set_empty)
static RValue builtin_tile_set_empty(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal) (RValue_toInt32(args[0]) & ~0x7ffff));
}

static RValue builtin_layer_get_all(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    size_t count = arrlenu(runner->runtimeLayers);
    RValue arr = RValue_makeArray(GMLArray_create((int32_t) count));
    repeat(count, layerIndex) {
        GMLArray_setOnArrayRef(&arr, layerIndex, RValue_makeReal((GMLReal) runner->runtimeLayers[layerIndex].id));
    }
    return arr;
}

static RValue builtin_layer_get_id_at_depth(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t targetDepth = RValue_toInt32(args[0]);
    RValue arr = RValue_makeArray(GMLArray_create(0));
    size_t count = arrlenu(runner->runtimeLayers);
    bool found = false;
    repeat(count, layerIndex) {
        if (runner->runtimeLayers[layerIndex].depth == targetDepth) {
            GMLArray_addOnArrayRef(&arr, RValue_makeReal((GMLReal) runner->runtimeLayers[layerIndex].id));
            found = true;
        }
    }
    // When no layer matches, return [-1] instead of an empty array.
    if (!found)
        GMLArray_setOnArrayRef(&arr, 0, RValue_makeReal(-1.0));
    return arr;
}
#endif

static RValue builtin_layer_vspeed(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    int32_t id = resolveLayerIdArg(runner, args[0]);
    float vs = (float) RValue_toReal(args[1]);
    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, id);
    if (runtimeLayer != nullptr) runtimeLayer->vSpeed = vs;
    return RValue_makeUndefined();
}

// ===[ Array Functions ]===

// @@NewGMLArray@@ - GMS2 internal function to create a new array literal (e.g. `[1, 2, 3]`).
// Allocates a fresh GMLArray populated with the argument values.
static RValue builtin_NewGMLArray(VMContext* ctx, RValue* args, int32_t argCount) {
    RValue arr = RValue_makeArray(GMLArray_create(argCount));
    repeat(argCount, i) {
        GMLArray_setOnArrayRef(&arr, i, args[i]);
    }
    return arr;
}

// array_create - GMS2 internal function to create a new array.
// Allocates a fresh GMLArray populated with the argument values.
static RValue builtin_array_create(VMContext* ctx, RValue* args, int32_t argCount) {
    RValue arr = RValue_makeArray(GMLArray_create(0));
    RValue fill = (argCount > 1) ? args[1] : RValue_makeUndefined();
    repeat(RValue_toReal(args[0]), i) {
        GMLArray_setOnArrayRef(&arr, i, fill);
    }
    return arr;
}

// @@This@@ - GMS2 internal function returning the current instance's ID.
// Emitted by the GMS2 compiler for expressions like `self` when used as a value.
static RValue builtin_This(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeInt32(INSTANCE_SELF);
    return RValue_makeInt32((int32_t) inst->instanceId);
}

// @@Global@@ - GMS2 internal function returning the "global" instance's ID.
static RValue builtin_Global(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeInt32(INSTANCE_GLOBAL);
}

// @@Other@@ - GMS2 internal function returning the "other" instance's ID.
// Falls back to the current instance when there is no other (matches GML semantics outside with/collision).
static RValue builtin_Other(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* other = ctx->otherInstance;
    if (other != nullptr) return RValue_makeInt32((int32_t) other->instanceId);
    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeInt32(INSTANCE_SELF);
    return RValue_makeInt32((int32_t) inst->instanceId);
}

#if IS_WAD17_OR_HIGHER_ENABLED
// @@NullObject@@ - GMS2 internal sentinel pushed before "method()" when the GML source is a struct literal or anonymous constructor: the bound self is "nothing yet", and @@NewGMLObject@@ rebinds to the fresh struct.
// We encode it as INSTANCE_NOONE so "method()" stores it as is (its -1 -> current remap does not fire).
static RValue builtin_NullObject(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeInt32(INSTANCE_NOONE);
}

// @@SetStatic@@() - GMS2.3+ internal function emitted at the top of constructor bodies.
// TODO: Semi-stub! The native runner does more things than that
static RValue builtin_SetStatic(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    logSemiStubbedFunction(ctx, "@@SetStatic@@");
    if (ctx->staticInitialized != nullptr) ctx->staticInitialized[ctx->currentCodeIndex] = true;
    return RValue_makeUndefined();
}

// @@NewGMLObject@@(methodRef, ...args) - GMS2 internal function that allocates a fresh struct instance, runs the constructor method against it, and returns the new instance ID.
// We reuse Instance (with objectIndex = STRUCT_OBJECT_INDEX) the same way globalScopeInstance is used for GLOB scripts, instead of introducing a separate struct type.
static RValue builtin_NewGMLObject(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "VM: @@NewGMLObject@@ called with no arguments\n");
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    int32_t codeIndex;
    if (args[0].type == RVALUE_METHOD && args[0].method != nullptr) {
        codeIndex = args[0].method->codeIndex;
    } else {
        // Raw funcIdx pushed via "Push.i <funcIdx>; Conv.i.v" (no method() wrapper used when no static binding is needed).
        // Resolve via FUNC chunk name -> codeIndexByName, matching builtin_method's lookup.
        int32_t rawArg = RValue_toInt32(args[0]);
        codeIndex = rawArg;
        if (rawArg >= 0 && (uint32_t) rawArg < ctx->dataWin->func.functionCount) {
            const char* funcName = ctx->dataWin->func.functions[rawArg].name;
            if (funcName != nullptr) {
                ptrdiff_t idx = shgeti(ctx->codeIndexByName, (char*) funcName);
                if (idx >= 0) codeIndex = ctx->codeIndexByName[idx].value;
            }
        }
    }
    if (0 > codeIndex || (uint32_t) codeIndex > ctx->dataWin->code.count) {
        fprintf(stderr, "VM: @@NewGMLObject@@ method has invalid codeIndex %d\n", codeIndex);
        return RValue_makeUndefined();
    }

    Instance* structInst = Runner_createStruct(runner);
    // Remember the constructor so member reads can fallback to its shared static struct.
    structInst->constructorCodeIndex = codeIndex;

    Instance* savedSelf = ctx->currentInstance;
    ctx->currentInstance = structInst;

    RValue* ctorArgs = (argCount > 1) ? &args[1] : nullptr;
    int32_t ctorArgCount = argCount - 1;
    RValue result = VM_callCodeIndex(ctx, codeIndex, ctorArgs, ctorArgCount);
    RValue_free(&result);

    ctx->currentInstance = savedSelf;
    return RValue_makeStructAndIncRef(structInst);
}

// @@CopyStatic@@(parentRef) - links the current constructor's static struct to a parent constructor's static struct so a child instance resolves fields declared "static" on the parent (constructor inheritance).
static RValue builtin_CopyStatic(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();

    VM_copyStatic(ctx, &args[0]);
    return RValue_makeUndefined();
}

// @@GetInstance@@(target) - takes an object index and returns the first active instance's ID.
static RValue builtin_GetInstance(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeInt32(INSTANCE_NOONE);

    Runner* runner = ctx->runner;
    int32_t target = RValue_toInt32(args[0]);

    if (target >= 0 && (uint32_t) target < ctx->dataWin->objt.count) {
        Instance** bucket = runner->instancesByObject[target];
        int32_t bucketCount = (int32_t) arrlen(bucket);
        for (int32_t i = 0; bucketCount > i; i++) {
            if (bucket[i]->active) return RValue_makeInt32((int32_t) bucket[i]->instanceId);
        }
    }
    return RValue_makeInt32(INSTANCE_NOONE);
}
#endif

// ===[ PATH FUNCTIONS ]===

// Resolves a path by index, or nullptr if the index is out of range.
static GamePath* getPath(Runner* runner, int32_t pathIdx) {
    if (0 > pathIdx) return nullptr;
    if ((uint32_t) pathIdx >= runner->dataWin->path.count) return nullptr;
    return &runner->dataWin->path.paths[pathIdx];
}

// path_add() - create a new empty path, return its index
static RValue builtin_path_add(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    PathChunk* pc = &runner->dataWin->path;
    uint32_t newIdx = pc->count;
    GamePath* paths = (GamePath*) realloc(pc->paths, (newIdx + 1) * sizeof(GamePath));
    if (paths == nullptr) return RValue_makeInt32(-1);
    pc->paths = paths;
    GamePath* p = &paths[newIdx];
    memset(p, 0, sizeof(GamePath));
    p->name = "";
    p->isSmooth = false;
    p->isClosed = false;
    p->precision = 4;
    p->pointCount = 0;
    p->points = nullptr;
    p->internalPointCount = 0;
    p->internalPoints = nullptr;
    p->length = 0.0;
    pc->count = newIdx + 1;
    return RValue_makeInt32((int32_t) newIdx);
}

// path_clear_points(path)
static RValue builtin_path_clear_points(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    GamePath* p = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (p == nullptr) return RValue_makeUndefined();
    free(p->points);
    p->points = nullptr;
    p->pointCount = 0;
    free(p->internalPoints);
    p->internalPoints = nullptr;
    p->internalPointCount = 0;
    p->length = 0.0;
    return RValue_makeUndefined();
}

// path_add_point(path, x, y, speed)
static RValue builtin_path_add_point(VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeUndefined();
    GamePath* p = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (p == nullptr) return RValue_makeUndefined();
    PathPoint* pts = (PathPoint*) realloc(p->points, (p->pointCount + 1) * sizeof(PathPoint));
    if (pts == nullptr) return RValue_makeUndefined();
    p->points = pts;
    pts[p->pointCount].x = (float) RValue_toReal(args[1]);
    pts[p->pointCount].y = (float) RValue_toReal(args[2]);
    pts[p->pointCount].speed = (float) RValue_toReal(args[3]);
    p->pointCount++;
    GamePath_computeInternal(p);
    return RValue_makeUndefined();
}

// path_exists(path)
static RValue builtin_path_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(getPath(ctx->runner, RValue_toInt32(args[0])) != nullptr);
}

// path_delete(path) - we don't reclaim the slot (would require remapping indices); zero it out
static RValue builtin_path_delete(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    GamePath* p = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (p == nullptr) return RValue_makeUndefined();
    free(p->points); p->points = nullptr; p->pointCount = 0;
    free(p->internalPoints); p->internalPoints = nullptr; p->internalPointCount = 0;
    p->length = 0.0;
    return RValue_makeUndefined();
}

// ===[ TIMELINE FUNCTIONS ]===

static Timeline* resolveTimeline(Runner* runner, RValue arg) {
    int32_t idx = RValue_toInt32(arg);
    if (0 > idx || (uint32_t) idx >= runner->dataWin->tmln.count) return nullptr;
    Timeline* tl = &runner->dataWin->tmln.timelines[idx];
    if (!tl->present) return nullptr;
    return tl;
}

// timeline_exists(ind)
static RValue builtin_timeline_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(resolveTimeline(ctx->runner, args[0]) != nullptr);
}

// timeline_get_name(ind)
static RValue builtin_timeline_get_name(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(safeStrdup("<undefined>"));
    Timeline* tl = resolveTimeline(ctx->runner, args[0]);
    if (tl == nullptr || tl->name == nullptr) return RValue_makeOwnedString(safeStrdup("<undefined>"));
    return RValue_makeOwnedString(safeStrdup(tl->name));
}

// timeline_max_moment(ind) - highest step number, or -1 if empty
static RValue builtin_timeline_max_moment(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    Timeline* tl = resolveTimeline(ctx->runner, args[0]);
    if (tl == nullptr || tl->momentCount == 0) return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal) tl->moments[tl->momentCount - 1].step);
}

// timeline_size(ind) - number of moments
static RValue builtin_timeline_size(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Timeline* tl = resolveTimeline(ctx->runner, args[0]);
    if (tl == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) tl->momentCount);
}

// action_timeline_set's "pausedKind" argument from the DnD editor. Values >= TIMELINE_ACTION_KIND_STOP map to "stop" (treated like pause).
#define TIMELINE_ACTION_KIND_PLAY 0
#define TIMELINE_ACTION_KIND_PAUSE 1
#define TIMELINE_ACTION_KIND_STOP 2

// action_timeline_set's "loop" argument: TIMELINE_ACTION_LOOP_YES = looping, anything else = no loop.
#define TIMELINE_ACTION_LOOP_YES 1

static RValue builtin_action_timeline_start(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance != nullptr) ctx->currentInstance->timelineRunning = true;
    return RValue_makeUndefined();
}

static RValue builtin_action_timeline_pause(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance != nullptr) ctx->currentInstance->timelineRunning = false;
    return RValue_makeUndefined();
}

static RValue builtin_action_timeline_stop(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance != nullptr) {
        ctx->currentInstance->timelinePosition = 0.0f;
        ctx->currentInstance->timelineRunning = false;
    }
    return RValue_makeUndefined();
}

static RValue builtin_action_set_timeline_position(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    float pos = (float) RValue_toReal(args[0]);
    if (ctx->actionRelativeFlag) pos += ctx->currentInstance->timelinePosition;
    ctx->currentInstance->timelinePosition = pos;
    return RValue_makeUndefined();
}

static RValue builtin_action_set_timeline_speed(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    float spd = (float) RValue_toReal(args[0]);
    if (ctx->actionRelativeFlag) spd += ctx->currentInstance->timelineSpeed;
    ctx->currentInstance->timelineSpeed = spd;
    return RValue_makeUndefined();
}

// action_set_timeline(index, position) - sets timeline index, marks as running, jumps to position
static RValue builtin_action_set_timeline(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    Instance* inst = ctx->currentInstance;
    inst->timelineIndex = RValue_toInt32(args[0]);
    inst->timelineRunning = true;
    inst->timelinePosition = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

// action_timeline_set(index, position, pausedKind, loop)
static RValue builtin_action_timeline_set(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->currentInstance == nullptr) return RValue_makeUndefined();
    Instance* inst = ctx->currentInstance;
    int32_t idx = RValue_toInt32(args[0]);
    float pos = (float) RValue_toReal(args[1]);
    int32_t pausedKind = RValue_toInt32(args[2]);
    int32_t loop = RValue_toInt32(args[3]);

    inst->timelineIndex = idx;
    inst->timelinePosition = pos;
    inst->timelineRunning = pausedKind == TIMELINE_ACTION_KIND_PLAY;
    inst->timelineLoop = loop == TIMELINE_ACTION_LOOP_YES;
    return RValue_makeUndefined();
}

// ===[ ANIMCURVE FUNCTIONS ]===

// Resolve the first argument of animcurve_get_channel / animcurve_get_channel_index, which can be either an animcurve asset id (int / assetref) or a curve struct/object reference.
// We don't support runtime-constructed curve objects, so we treat anything non-int as invalid.
static AnimCurve* resolveAnimCurveArg(Runner* runner, RValue arg) {
    int32_t idx = RValue_toInt32(arg);
    if (0 > idx || (uint32_t) idx >= runner->dataWin->acrv.count) return nullptr;
    AnimCurve* cur = &runner->dataWin->acrv.curves[idx];
    if (!cur->present) return nullptr;
    return cur;
}

// animcurve_get(index) - returns an asset reference to the animation curve
static RValue builtin_animcurve_get(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t idx = RValue_toInt32(args[0]);
    if (0 > idx || (uint32_t) idx >= runner->dataWin->acrv.count) return RValue_makeUndefined();
    return RValue_makeAssetRef(idx, ASSET_TYPE_ANIMCURVE);
}

// animcurve_get_channel(curve, name_or_index) - returns an integer handle that animcurve_channel_evaluate can resolve back to a channel
static RValue builtin_animcurve_get_channel(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    AnimCurve* cur = resolveAnimCurveArg(runner, args[0]);
    if (cur == nullptr) return RValue_makeUndefined();

    if (args[1].type == RVALUE_STRING) {
        const char* needle = args[1].string;
        if (needle == nullptr) return RValue_makeUndefined();
        repeat(cur->channelCount, c) {
            const char* name = cur->channels[c].name;
            if (name != nullptr && strcmp(name, needle) == 0) {
                return RValue_makeInt32(cur->channels[c].globalId);
            }
        }
        return RValue_makeUndefined();
    }

    int32_t channelIdx = RValue_toInt32(args[1]);
    if (0 > channelIdx || (uint32_t) channelIdx >= cur->channelCount) return RValue_makeUndefined();
    return RValue_makeInt32(cur->channels[channelIdx].globalId);
}

// animcurve_get_channel_index(curve, name) - returns the integer index of the named channel within the curve
static RValue builtin_animcurve_get_channel_index(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-1.0);
    Runner* runner = ctx->runner;
    AnimCurve* cur = resolveAnimCurveArg(runner, args[0]);
    if (cur == nullptr || args[1].type != RVALUE_STRING || args[1].string == nullptr) return RValue_makeReal(-1.0);
    const char* needle = args[1].string;
    repeat(cur->channelCount, c) {
        const char* name = cur->channels[c].name;
        if (name != nullptr && strcmp(name, needle) == 0) {
            return RValue_makeInt32((int32_t) c);
        }
    }
    return RValue_makeReal(-1.0);
}

// Catmull-Rom interpolation: 4 control points P0..P3, parameter t in [0,1] from P1 to P2.
static float animcurveCatmullRom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

static float animcurveChannelEvaluate(const AnimCurveChannel* ch, float x) {
    if (ch == nullptr || ch->pointCount == 0) return 0.0f;
    if (ch->pointCount == 1) return ch->points[0].value;

    // Clamp to the channel's x range
    float x0 = ch->points[0].x;
    float xn = ch->points[ch->pointCount - 1].x;
    if (x0 > x) return ch->points[0].value;
    if (x > xn) return ch->points[ch->pointCount - 1].value;

    // Binary search for the interval [i, i+1] containing x
    uint32_t lo = 0;
    uint32_t hi = ch->pointCount - 1;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        if (ch->points[mid].x <= x) lo = mid;
        else hi = mid;
    }

    const AnimCurvePoint* a = &ch->points[lo];
    const AnimCurvePoint* b = &ch->points[lo + 1];
    float span = b->x - a->x;
    if (0.0f >= span) return a->value;
    float t = (x - a->x) / span;

    if (ch->curveType == ANIMCURVE_TYPE_SMOOTH) {
        // Catmull-Rom on the value channel, using neighbor points clamped at the ends
        float p0 = (lo > 0) ? ch->points[lo - 1].value : a->value;
        float p1 = a->value;
        float p2 = b->value;
        float p3 = (lo + 2 < ch->pointCount) ? ch->points[lo + 2].value : b->value;
        return animcurveCatmullRom(p0, p1, p2, p3, t);
    }

    // Linear (also used as fallback for bezier; full bezier handle eval not yet implemented)
    return a->value + (b->value - a->value) * t;
}

// animcurve_channel_evaluate(channel_handle, posx)
static RValue builtin_animcurve_channel_evaluate(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    Runner* runner = ctx->runner;
    Acrv* a = &runner->dataWin->acrv;
    int32_t handle = RValue_toInt32(args[0]);
    if (0 > handle || (uint32_t) handle >= a->allChannelsCount) return RValue_makeReal(0.0);
    AnimCurveChannel* ch = a->allChannels[handle];
    float x = (float) RValue_toReal(args[1]);
    return RValue_makeReal((GMLReal) animcurveChannelEvaluate(ch, x));
}

// ===[ MP_GRID FUNCTIONS ]===

static MpGrid* mpGridGet(Runner* runner, int32_t id) {
    if (0 > id || (int32_t) arrlen(runner->mpGridPool) <= id) return nullptr;
    MpGrid* g = &runner->mpGridPool[id];
    if (!g->inUse) return nullptr;
    return g;
}

// mp_grid_create(left, top, hcells, vcells, cellwidth, cellheight)
static RValue builtin_mp_grid_create(VMContext* ctx, RValue* args, int32_t argCount) {
    if (6 > argCount) return RValue_makeInt32(-1);
    Runner* runner = ctx->runner;
    MpGrid g;
    g.inUse = true;
    g.left = RValue_toReal(args[0]);
    g.top = RValue_toReal(args[1]);
    g.hcells = RValue_toInt32(args[2]);
    g.vcells = RValue_toInt32(args[3]);
    g.cellWidth = RValue_toReal(args[4]);
    g.cellHeight = RValue_toReal(args[5]);
    if (g.hcells <= 0 || g.vcells <= 0) return RValue_makeInt32(-1);
    g.cells = (uint8_t*) calloc((size_t) g.hcells * (size_t) g.vcells, 1);
    int32_t id = (int32_t) arrlen(runner->mpGridPool);
    arrput(runner->mpGridPool, g);
    return RValue_makeInt32(id);
}

static RValue builtin_mp_grid_destroy(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    MpGrid* g = mpGridGet(runner, id);
    if (g == nullptr) return RValue_makeUndefined();
    free(g->cells);
    g->cells = nullptr;
    g->inUse = false;
    return RValue_makeUndefined();
}

static RValue builtin_mp_grid_clear_all(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    MpGrid* g = mpGridGet(runner, RValue_toInt32(args[0]));
    if (g == nullptr) return RValue_makeUndefined();
    memset(g->cells, 0, (size_t) g->hcells * (size_t) g->vcells);
    return RValue_makeUndefined();
}

static RValue builtin_mp_grid_add_cell(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    MpGrid* g = mpGridGet(runner, RValue_toInt32(args[0]));
    if (g == nullptr) return RValue_makeUndefined();
    int32_t cx = RValue_toInt32(args[1]);
    int32_t cy = RValue_toInt32(args[2]);
    if (cx < 0 || cy < 0 || cx >= g->hcells || cy >= g->vcells) return RValue_makeUndefined();
    g->cells[cx * g->vcells + cy] = 1;
    return RValue_makeUndefined();
}

static RValue builtin_mp_grid_clear_cell(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    MpGrid* g = mpGridGet(runner, RValue_toInt32(args[0]));
    if (g == nullptr) return RValue_makeUndefined();
    int32_t cx = RValue_toInt32(args[1]);
    int32_t cy = RValue_toInt32(args[2]);
    if (cx < 0 || cy < 0 || cx >= g->hcells || cy >= g->vcells) return RValue_makeUndefined();
    g->cells[cx * g->vcells + cy] = 0;
    return RValue_makeUndefined();
}

static RValue builtin_mp_grid_add_rectangle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    MpGrid* g = mpGridGet(runner, RValue_toInt32(args[0]));
    if (g == nullptr) return RValue_makeUndefined();
    int32_t x1 = RValue_toInt32(args[1]);
    int32_t y1 = RValue_toInt32(args[2]);
    int32_t x2 = RValue_toInt32(args[3]);
    int32_t y2 = RValue_toInt32(args[4]);
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= g->hcells) x2 = g->hcells - 1;
    if (y2 >= g->vcells) y2 = g->vcells - 1;
    for (int32_t cx = x1; x2 >= cx; cx++) {
        for (int32_t cy = y1; y2 >= cy; cy++) {
            g->cells[cx * g->vcells + cy] = 1;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_mp_grid_clear_rectangle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeUndefined();
    Runner* runner = ctx->runner;
    MpGrid* g = mpGridGet(runner, RValue_toInt32(args[0]));
    if (g == nullptr) return RValue_makeUndefined();
    int32_t x1 = RValue_toInt32(args[1]);
    int32_t y1 = RValue_toInt32(args[2]);
    int32_t x2 = RValue_toInt32(args[3]);
    int32_t y2 = RValue_toInt32(args[4]);
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= g->hcells) x2 = g->hcells - 1;
    if (y2 >= g->vcells) y2 = g->vcells - 1;
    for (int32_t cx = x1; x2 >= cx; cx++) {
        for (int32_t cy = y1; y2 >= cy; cy++) {
            g->cells[cx * g->vcells + cy] = 0;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_mp_grid_get_cell(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeInt32(0);
    Runner* runner = ctx->runner;
    MpGrid* g = mpGridGet(runner, RValue_toInt32(args[0]));
    if (g == nullptr) return RValue_makeInt32(0);
    int32_t cx = RValue_toInt32(args[1]);
    int32_t cy = RValue_toInt32(args[2]);
    if (cx < 0 || cy < 0 || cx >= g->hcells || cy >= g->vcells) return RValue_makeInt32(0);
    // Native returns -1 for blocked, 0 for clear
    return RValue_makeInt32(g->cells[cx * g->vcells + cy] ? -1 : 0);
}

static RValue builtin_mp_grid_draw(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeUndefined();
}

// mp_grid_path(id, path, xstart, ystart, xgoal, ygoal, allowDiagonals)
// BFS pathfinder: fills `path` with cell-center waypoints from start to goal.
// Returns true if a path was found.
static RValue builtin_mp_grid_path(VMContext* ctx, RValue* args, int32_t argCount) {
    if (7 > argCount) return RValue_makeBool(false);
    Runner* runner = ctx->runner;
    MpGrid* mp = mpGridGet(runner, RValue_toInt32(args[0]));
    if (mp == nullptr) return RValue_makeBool(false);
    GamePath* pPath = getPath(runner, RValue_toInt32(args[1]));
    if (pPath == nullptr) return RValue_makeBool(false);

    GMLReal xstart = RValue_toReal(args[2]);
    GMLReal ystart = RValue_toReal(args[3]);
    GMLReal xgoal  = RValue_toReal(args[4]);
    GMLReal ygoal  = RValue_toReal(args[5]);
    bool allowdiag = RValue_toBool(args[6]);

    // Find the start & goal cells & check them.
    int32_t cxs = (int32_t) GMLReal_floor((xstart - mp->left) / mp->cellWidth);
    int32_t cys = (int32_t) GMLReal_floor((ystart - mp->top)  / mp->cellHeight);
    int32_t cxg = (int32_t) GMLReal_floor((xgoal  - mp->left) / mp->cellWidth);
    int32_t cyg = (int32_t) GMLReal_floor((ygoal  - mp->top)  / mp->cellHeight);

    if (cxs < 0 || cxs >= mp->hcells || cys < 0 || cys >= mp->vcells) return RValue_makeBool(false);
    if (cxg < 0 || cxg >= mp->hcells || cyg < 0 || cyg >= mp->vcells) return RValue_makeBool(false);
    if (mp->cells[cxs * mp->vcells + cys]) return RValue_makeBool(false);
    if (mp->cells[cxg * mp->vcells + cyg]) return RValue_makeBool(false);

    // Start the search.
    int32_t total = mp->hcells * mp->vcells;
    int32_t* dist = (int32_t*) malloc(total * sizeof(int32_t));
    int32_t* qq   = (int32_t*) malloc(total * sizeof(int32_t));
    if (dist == nullptr || qq == nullptr) {
        free(dist); free(qq);
        return RValue_makeBool(false);
    }
    for (int32_t i = 0; total > i; i++) dist[i] = -1;

    int32_t startIdx = cxs * mp->vcells + cys;
    int32_t goalIdx  = cxg * mp->vcells + cyg;
    int32_t head = 0, tail = 0;
    dist[startIdx] = 1;
    qq[tail++] = startIdx;

    bool result = false;
    while (tail > head) {
        int32_t val = qq[head++];
        int32_t xx = val / mp->vcells;
        int32_t yy = val % mp->vcells;
        if (xx == cxg && yy == cyg) {
            result = true;
            break;
        }
        int32_t d = dist[val] + 1;

        bool f1 = (xx > 0) && (yy < mp->vcells - 1) && (dist[(xx - 1) * mp->vcells + (yy + 1)] == -1) && !mp->cells[(xx - 1) * mp->vcells + (yy + 1)];
        bool f2 = (yy < mp->vcells - 1) && (dist[xx * mp->vcells + (yy + 1)] == -1) && !mp->cells[xx * mp->vcells + (yy + 1)];
        bool f3 = (xx < mp->hcells - 1) && (yy < mp->vcells - 1) && (dist[(xx + 1) * mp->vcells + (yy + 1)] == -1) && !mp->cells[(xx + 1) * mp->vcells + (yy + 1)];
        bool f4 = (xx > 0) && (dist[(xx - 1) * mp->vcells + yy] == -1) && !mp->cells[(xx - 1) * mp->vcells + yy];
        bool f6 = (xx < mp->hcells - 1) && (dist[(xx + 1) * mp->vcells + yy] == -1) && !mp->cells[(xx + 1) * mp->vcells + yy];
        bool f7 = (xx > 0) && (yy > 0) && (dist[(xx - 1) * mp->vcells + (yy - 1)] == -1) && !mp->cells[(xx - 1) * mp->vcells + (yy - 1)];
        bool f8 = (yy > 0) && (dist[xx * mp->vcells + (yy - 1)] == -1) && !mp->cells[xx * mp->vcells + (yy - 1)];
        bool f9 = (xx < mp->hcells - 1) && (yy > 0) && (dist[(xx + 1) * mp->vcells + (yy - 1)] == -1) && !mp->cells[(xx + 1) * mp->vcells + (yy - 1)];

        // Handle horizontal & vertical moves.
        if (f4) {
            dist[(xx - 1) * mp->vcells + yy] = d;
            qq[tail++] = (xx - 1) * mp->vcells + yy;
        }
        if (f6) {
            dist[(xx + 1) * mp->vcells + yy] = d;
            qq[tail++] = (xx + 1) * mp->vcells + yy;
        }
        if (f8) {
            dist[xx * mp->vcells + (yy - 1)] = d;
            qq[tail++] = xx * mp->vcells + (yy - 1);
        }
        if (f2) {
            dist[xx * mp->vcells + (yy + 1)] = d;
            qq[tail++] = xx * mp->vcells + (yy + 1);
        }
        // Handle diagonal moves (require both cardinal neighbors clear, matching HTML5).
        if (allowdiag && f1 && f2 && f4) {
            dist[(xx - 1) * mp->vcells + (yy + 1)] = d;
            qq[tail++] = (xx - 1) * mp->vcells + (yy + 1);
        }
        if (allowdiag && f7 && f8 && f4) {
            dist[(xx - 1) * mp->vcells + (yy - 1)] = d;
            qq[tail++] = (xx - 1) * mp->vcells + (yy - 1);
        }
        if (allowdiag && f3 && f2 && f6) {
            dist[(xx + 1) * mp->vcells + (yy + 1)] = d;
            qq[tail++] = (xx + 1) * mp->vcells + (yy + 1);
        }
        if (allowdiag && f9 && f8 && f6) {
            dist[(xx + 1) * mp->vcells + (yy - 1)] = d;
            qq[tail++] = (xx + 1) * mp->vcells + (yy - 1);
        }
    }

    if (!result) {
        free(dist); free(qq);
        return RValue_makeBool(false);
    }

    // Compute the path from back to front. At each step, scan neighbors with dist == val-1 in the order LEFT, RIGHT, UP, DOWN, then diagonals
    int32_t chainCap = 16;
    int32_t chainLen = 0;
    int32_t* chain = (int32_t*) malloc(chainCap * sizeof(int32_t));
    {
        int32_t xx = cxg;
        int32_t yy = cyg;
        chain[chainLen++] = xx * mp->vcells + yy;
        while (xx != cxs || yy != cys) {
            if (chainLen >= chainCap) {
                chainCap *= 2;
                chain = (int32_t*) realloc(chain, chainCap * sizeof(int32_t));
            }
            int32_t val = dist[xx * mp->vcells + yy];
            bool f1 = (xx > 0) && (yy < mp->vcells - 1) && (dist[(xx - 1) * mp->vcells + (yy + 1)] == val - 1);
            bool f2 = (yy < mp->vcells - 1) && (dist[xx * mp->vcells + (yy + 1)] == val - 1);
            bool f3 = (xx < mp->hcells - 1) && (yy < mp->vcells - 1) && (dist[(xx + 1) * mp->vcells + (yy + 1)] == val - 1);
            bool f4 = (xx > 0) && (dist[(xx - 1) * mp->vcells + yy] == val - 1);
            bool f6 = (xx < mp->hcells - 1) && (dist[(xx + 1) * mp->vcells + yy] == val - 1);
            bool f7 = (xx > 0) && (yy > 0) && (dist[(xx - 1) * mp->vcells + (yy - 1)] == val - 1);
            bool f8 = (yy > 0) && (dist[xx * mp->vcells + (yy - 1)] == val - 1);
            bool f9 = (xx < mp->hcells - 1) && (yy > 0) && (dist[(xx + 1) * mp->vcells + (yy - 1)] == val - 1);

            // Four directions movement
            if (f4) { xx = xx - 1; } else if (f6) { xx = xx + 1; } else if (f8) { yy = yy - 1; } else if (f2) { yy = yy + 1; } else if (allowdiag && f1) {
                xx = xx - 1;
                yy = yy + 1;
            } else if (allowdiag && f3) {
                xx = xx + 1;
                yy = yy + 1;
            } else if (allowdiag && f7) {
                xx = xx - 1;
                yy = yy - 1;
            } else if (allowdiag && f9) {
                xx = xx + 1;
                yy = yy - 1;
            } else {
                // Should be unreachable: BFS reached goal, so a predecessor must exist.
                free(chain);
                free(dist);
                free(qq);
                return RValue_makeBool(false);
            }
            chain[chainLen++] = xx * mp->vcells + yy;
        }
    }

    // Build the output path.
    // We walk "chain" in reverse to emit start-first, with explicit overrides so the endpoints are exactly (xstart, ystart) / (xgoal, ygoal) instead of cell centers.
    free(pPath->points);
    pPath->points = nullptr;
    pPath->pointCount = 0;

    // When start cell == goal cell, chain has 1 node but the native runner and GameMaker-HTML5 still emit a 2-point path (start coord + goal coord).
    // Without this, the path length is 0, adaptPath early-returns before advancing pathPosition past 1.0, and the OTHER_END_OF_PATH event never fires.
    int32_t pointCount = (startIdx == goalIdx) ? 2 : chainLen;
    pPath->points = (PathPoint*) malloc(pointCount * sizeof(PathPoint));
    pPath->pointCount = (uint32_t) pointCount;
    for (int32_t i = 0; pointCount > i; i++) {
        float wx, wy;
        if (startIdx == goalIdx) {
            wx = (float) (i == 0 ? xstart : xgoal);
            wy = (float) (i == 0 ? ystart : ygoal);
        } else {
            int32_t idx = chain[chainLen - 1 - i];
            int32_t xx = idx / mp->vcells;
            int32_t yy = idx % mp->vcells;
            wx = (float) (mp->left + (xx + 0.5) * mp->cellWidth);
            wy = (float) (mp->top  + (yy + 0.5) * mp->cellHeight);
            if (i == 0)              { wx = (float) xstart; wy = (float) ystart; }
            if (i == chainLen - 1)   { wx = (float) xgoal;  wy = (float) ygoal;  }
        }
        pPath->points[i].x = wx;
        pPath->points[i].y = wy;
        pPath->points[i].speed = 100.0f;
    }
    free(chain);
    free(dist);
    free(qq);

    free(pPath->internalPoints);
    pPath->internalPoints = nullptr;
    pPath->internalPointCount = 0;
    pPath->length = 0.0f;
    GamePath_computeInternal(pPath);

    return RValue_makeBool(true);
}

// path_start(path, speed, endaction, absolute) - HTML5: Assign_Path (yyInstance.js:2695-2743)
static RValue builtin_path_start(VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeUndefined();

    Instance* inst = ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();

    Runner* runner = ctx->runner;
    int32_t pathIdx = RValue_toInt32(args[0]);
    GMLReal speed = RValue_toReal(args[1]);
    int32_t endAction = RValue_toInt32(args[2]);
    bool absolute = RValue_toBool(args[3]);

    // Validate path index
    inst->pathIndex = -1;
    GamePath* path = getPath(runner, pathIdx);
    if (path == nullptr) return RValue_makeUndefined();
    if (0.0 >= path->length) return RValue_makeUndefined();

    inst->pathIndex = pathIdx;
    inst->pathSpeed = (float) speed;

    if (inst->pathSpeed >= 0.0f) {
        inst->pathPosition = 0.0f;
    } else {
        inst->pathPosition = 1.0f;
    }

    inst->pathPositionPrevious = inst->pathPosition;
    inst->pathScale = 1.0f;
    inst->pathOrientation = 0.0f;
    inst->pathEndAction = endAction;

    if (absolute) {
        PathPositionResult startPos = GamePath_getPosition(path, inst->pathSpeed >= 0.0f ? 0.0f : 1.0f);
        inst->x = (float) startPos.x;
        inst->y = (float) startPos.y;
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, inst);

        PathPositionResult origin = GamePath_getPosition(path, 0.0f);
        inst->pathXStart = (float) origin.x;
        inst->pathYStart = (float) origin.y;
    } else {
        inst->pathXStart = inst->x;
        inst->pathYStart = inst->y;
    }

    return RValue_makeUndefined();
}

// path_get_length(path) - returns total length of the path in pixels
static RValue builtin_path_get_length(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) path->length);
}

// Resolves the n'th defining point of a path, or nullptr if the path or index is invalid.
static PathPoint* getPathPoint(Runner* runner, int32_t pathIdx, int32_t n) {
    if (0 > n) return nullptr;
    GamePath* path = getPath(runner, pathIdx);
    if (path == nullptr) return nullptr;
    if (path->points == nullptr) return nullptr;
    if ((uint32_t) n >= path->pointCount) return nullptr;
    return &path->points[n];
}

// path_get_point_x(path, n) - returns x of the n'th defining point (0-indexed)
static RValue builtin_path_get_point_x(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    PathPoint* point = getPathPoint(ctx->runner, RValue_toInt32(args[0]), RValue_toInt32(args[1]));
    if (point == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal(point->x);
}

// path_get_point_y(path, n) - returns y of the n'th defining point (0-indexed)
static RValue builtin_path_get_point_y(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    PathPoint* point = getPathPoint(ctx->runner, RValue_toInt32(args[0]), RValue_toInt32(args[1]));
    if (point == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal(point->y);
}

// path_get_x(path, pos) - x-coordinate at position pos (0..1) along the path
static RValue builtin_path_get_x(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-1.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeReal(-1.0);
    return RValue_makeReal(GamePath_getPosition(path, (float) RValue_toReal(args[1])).x);
}

// path_get_y(path, pos) - y-coordinate at position pos (0..1) along the path
static RValue builtin_path_get_y(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-1.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeReal(-1.0);
    return RValue_makeReal(GamePath_getPosition(path, (float) RValue_toReal(args[1])).y);
}

// path_get_speed(path, pos) - speed factor at position pos (0..1) along the path
static RValue builtin_path_get_speed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-1.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeReal(-1.0);
    return RValue_makeReal(GamePath_getPosition(path, (float) RValue_toReal(args[1])).speed);
}

// path_get_kind(path) - 0=straight, 1=smooth
static RValue builtin_path_get_kind(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal(path->isSmooth ? 1.0 : 0.0);
}

// path_get_closed(path) - whether the path is closed
static RValue builtin_path_get_closed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(true);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeBool(true);
    return RValue_makeBool(path->isClosed);
}

// path_get_precision(path) - smoothing precision
static RValue builtin_path_get_precision(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(8.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeReal(8.0);
    return RValue_makeReal((GMLReal) path->precision);
}

// path_get_number(path) - number of defining points
static RValue builtin_path_get_number(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr || path->points == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((GMLReal) path->pointCount);
}

// path_get_point_speed(path, n) - speed factor at the n'th defining point (0-indexed)
static RValue builtin_path_get_point_speed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    PathPoint* point = getPathPoint(ctx->runner, RValue_toInt32(args[0]), RValue_toInt32(args[1]));
    if (point == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal(point->speed);
}

// path_set_kind(path, kind) - 0=straight, 1=smooth; recomputes the path
static RValue builtin_path_set_kind(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeUndefined();
    int32_t kind = RValue_toInt32(args[1]);
    path->isSmooth = (kind == 1);
    GamePath_computeInternal(path);
    return RValue_makeUndefined();
}

// path_set_closed(path, closed) - recomputes the path
static RValue builtin_path_set_closed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeUndefined();
    path->isClosed = RValue_toBool(args[1]);
    GamePath_computeInternal(path);
    return RValue_makeUndefined();
}

// path_set_precision(path, prec) - clamped to 0..8; recomputes the path
static RValue builtin_path_set_precision(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    GamePath* path = getPath(ctx->runner, RValue_toInt32(args[0]));
    if (path == nullptr) return RValue_makeUndefined();
    int32_t prec = RValue_toInt32(args[1]);
    if (0 > prec) prec = 0;
    if (prec > 8) prec = 8;
    path->precision = (uint32_t) prec;
    GamePath_computeInternal(path);
    return RValue_makeUndefined();
}

// path_end() - HTML5: Assign_Path(-1,...)
static RValue builtin_path_end(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Instance* inst = ctx->currentInstance;
    if (inst != nullptr) {
        inst->pathIndex = -1;
    }
    return RValue_makeUndefined();
}

// string_hash_to_newline - converts # to \n in a string
static RValue builtin_string_hash_to_newline(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeString("");
    RValue original = args[0]; // This is a copy

    if (original.type != RVALUE_STRING) {
        // Fast path: If the argument is not a string, return a copy of it
        return RValue_makeOwnedString(RValue_toString(original));
    }

    if (original.string == nullptr) {
        // Fast path: If the argument is a string but has no value, return an empty string
        return RValue_makeString("");
    }

    PreprocessedText result = TextUtils_preprocessGmlText(original.string);
    if (!result.owning) {
        // No # found, steal the reference to avoid copying the string
        args[0].ownsReference = false;
        return original;
    }
    return RValue_makeOwnedString((char*) result.text);
}

// See GameMaker-HTML5's Function_File.js for reference.
// useFloatMarkers: GameMaker only started encoding NaN/Infinity as the special "@@nan$$"/"@@infinity$$" string tokens in GM 2023.2+
static void jsonEncodeReal(JsonWriter* writer, GMLReal value, bool useFloatMarkers) {
    double d = (double) value;

    if (isnan(d) || isinf(d)) {
        if (useFloatMarkers) {
            if (isnan(d)) {
                JsonWriter_string(writer, "@@nan$$");
            } else {
                JsonWriter_string(writer, d > 0 ? "@@infinity$$" : "@@-infinity$$");
            }
            return;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "%g", d);
        JsonWriter_rawValue(writer, buf);
        return;
    }

    // Integer-valued reals are emitted without a fractional part, matching json_encode
    const double INT_SAFE_BOUND = 9.2233720368547758e18; // largest double strictly < 2^63
    if (d >= -INT_SAFE_BOUND && d <= INT_SAFE_BOUND && d == (double) (int64_t) d) {
        JsonWriter_int(writer, (int64_t) d);
        return;
    }

    // Find the shortest precision that round-trips so we emit "0.1" instead of "0.10000000000000001"
    char buf[64];
    repeat(18, i) {
        int precision = i + 1;
        snprintf(buf, sizeof(buf), "%.*g", precision, d);
        if (strtod(buf, nullptr) == d) break;
    }
    JsonWriter_rawValue(writer, buf);
}

static void jsonEncodeValue(JsonWriter* writer, RValue val, bool useFloatMarkers) {
    switch (val.type) {
        case RVALUE_UNDEFINED:
            JsonWriter_null(writer);
            break;
        case RVALUE_REAL:
            jsonEncodeReal(writer, val.real, useFloatMarkers);
            break;
        case RVALUE_INT32:
            JsonWriter_int(writer, val.int32);
            break;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:
            JsonWriter_int(writer, val.int64);
            break;
#endif
        case RVALUE_BOOL:
            JsonWriter_bool(writer, val.int32 != 0);
            break;
        case RVALUE_STRING:
            JsonWriter_string(writer, val.string);
            break;
        case RVALUE_ARRAY: {
            JsonWriter_beginArray(writer);
            if (val.array != nullptr) {
                int32_t length = GMLArray_length1D(val.array);
                repeat(length, i) {
                    RValue* slot = GMLArray_slot(val.array, i);
                    jsonEncodeValue(writer, slot != nullptr ? *slot : RValue_makeUndefined(), useFloatMarkers);
                }
            }
            JsonWriter_endArray(writer);
            break;
        }
        default: {
            char* str = RValue_toString(val);
            JsonWriter_string(writer, str);
            free(str);
            break;
        }
    }
}

// json_encode(map [, prettify]): encodes a ds_map into a JSON object string.
static RValue builtin_json_encode(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        return RValue_makeOwnedString(safeStrdup("{}"));
    }

    Runner* runner = ctx->runner;
    int32_t mapIndex = RValue_toInt32(args[0]);
    // TODO: Implement prettify!
    bool prettify = argCount == 2 ? RValue_toBool(args[1]) : false;
    DsMapEntry** mapPtr = dsMapGet(runner, mapIndex);
    bool useFloatMarkers = DataWin_isVersionAtLeast(ctx->dataWin, 2023, 2, 0, 0);

    JsonWriter writer = JsonWriter_create();
    JsonWriter_beginObject(&writer);

    if (mapPtr != nullptr && *mapPtr != nullptr) {
        repeat(shlen(*mapPtr), i) {
            JsonWriter_key(&writer, (*mapPtr)[i].key);
            jsonEncodeValue(&writer, (*mapPtr)[i].value, useFloatMarkers);
        }
    }

    JsonWriter_endObject(&writer);

    char* result = JsonWriter_copyOutput(&writer);
    JsonWriter_free(&writer);
    return RValue_makeOwnedString(result);
}

// json_decode
static RValue builtin_json_decode(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "[json_decode] Expected at least 1 argument\n");
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    int32_t mapIndex = dsMapCreate(runner);
    DsMapEntry **mapPtr = dsMapGet(runner, mapIndex);
    const char* content = args[0].string;
    JsonValue* json = JsonReader_parse(content);
    // While the docs say "An invalid DS Map handle (-1) is returned in case the JSON could not be decoded.", when looking at the GameMaker-HTML5 source code it actually wraps in a "default" block when it fails to be parsed
    if (json == nullptr) {
        shput(*mapPtr, "default", RValue_makeIndependent(args[0]));
    } else {
        repeat(JsonReader_objectLength(json), i) {
            const char *key = safeStrdup(JsonReader_getObjectKey(json, i));
            RValue val = RValue_makeOwnedString(safeStrdup(JsonReader_getString(JsonReader_getObjectValue(json, i))));
            shput(*mapPtr, key, val);
        }

        JsonReader_free(json);
    }

    return RValue_makeReal(mapIndex);
}

static RValue builtin_object_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        return RValue_makeBool(false);
    }

    int32_t id = RValue_toInt32(args[0]);
    bool exists = id >= 0 && ctx->dataWin->objt.count > (uint32_t) id;
    return RValue_makeBool(exists);
}

static RValue builtin_object_get_persistent(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);

    int32_t id = RValue_toInt32(args[0]);
    if (0 > id || (uint32_t) id >= ctx->dataWin->objt.count) {
        return RValue_makeBool(false);
    }

    return RValue_makeBool(ctx->dataWin->objt.objects[id].persistent);
}

static RValue builtin_object_get_solid(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);

    int32_t id = RValue_toInt32(args[0]);
    if (0 > id || (uint32_t) id >= ctx->dataWin->objt.count) {
        return RValue_makeBool(false);
    }

    return RValue_makeBool(ctx->dataWin->objt.objects[id].solid);
}

static RValue builtin_object_get_sprite(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "[object_get_sprite] Expected at least 1 argument\n");
        return RValue_makeUndefined();
    }

    int32_t id = RValue_toInt32(args[0]);

    return RValue_makeReal(ctx->dataWin->objt.objects[id].spriteId);
}

static RValue builtin_object_get_visible(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);

    int32_t id = RValue_toInt32(args[0]);
    if (0 > id || (uint32_t) id >= ctx->dataWin->objt.count) {
        return RValue_makeBool(false);
    }

    return RValue_makeBool(ctx->dataWin->objt.objects[id].visible);
}

static RValue builtin_object_get_depth(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);

    int32_t id = RValue_toInt32(args[0]);
    if (0 > id || (uint32_t) id >= ctx->dataWin->objt.count) {
        return RValue_makeReal(0.0);
    }

    return RValue_makeReal(ctx->dataWin->objt.objects[id].depth);
}

static RValue builtin_object_get_name(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeString("");

    int32_t id = RValue_toInt32(args[0]);
    if (0 > id || (uint32_t) id >= ctx->dataWin->objt.count) {
        return RValue_makeString("");
    }

    return RValue_makeString(ctx->dataWin->objt.objects[id].name);
}

static RValue builtin_object_get_parent(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);

    int32_t id = RValue_toInt32(args[0]);
    if (0 > id || (uint32_t) id >= ctx->dataWin->objt.count) {
        return RValue_makeReal(-1.0);
    }

    return RValue_makeReal(ctx->dataWin->objt.objects[id].parentId);
}

static RValue builtin_object_set_depth(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t id = RValue_toInt32(args[0]);
    GMLReal depth = RValue_toReal(args[1]);
    if (0 <= id && (uint32_t) id < ctx->dataWin->objt.count) {
        ctx->dataWin->objt.objects[id].depth = depth;
    }
    return RValue_makeUndefined();
}

static RValue builtin_object_set_parent(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t id = RValue_toInt32(args[0]);
    int32_t parentId = RValue_toInt32(args[1]);
    if (0 <= id && (uint32_t) id < ctx->dataWin->objt.count) {
        ctx->dataWin->objt.objects[id].parentId = parentId;
    }
    return RValue_makeUndefined();
}

static RValue builtin_object_set_persistent(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t id = RValue_toInt32(args[0]);
    bool persistent = RValue_toBool(args[1]);
    if (0 <= id && (uint32_t) id < ctx->dataWin->objt.count) {
        ctx->dataWin->objt.objects[id].persistent = persistent;
    }
    return RValue_makeUndefined();
}

static RValue builtin_object_set_solid(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t id = RValue_toInt32(args[0]);
    bool solid = RValue_toBool(args[1]);
    if (0 <= id && (uint32_t) id < ctx->dataWin->objt.count) {
        ctx->dataWin->objt.objects[id].solid = solid;
    }
    return RValue_makeUndefined();
}

static RValue builtin_object_set_sprite(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t id = RValue_toInt32(args[0]);
    int32_t spriteIndex = RValue_toReal(args[1]);
    if (0 <= id && (uint32_t) id < ctx->dataWin->objt.count) {
        ctx->dataWin->objt.objects[id].spriteId = spriteIndex;
    }
    return RValue_makeUndefined();
}

static RValue builtin_object_set_visible(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    int32_t id = RValue_toInt32(args[0]);
    bool visible = RValue_toBool(args[1]);
    if (0 <= id && (uint32_t) id < ctx->dataWin->objt.count) {
        ctx->dataWin->objt.objects[id].visible = visible;
    }
    return RValue_makeUndefined();
}

static RValue builtin_object_is_ancestor(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    
    int32_t id = RValue_toInt32(args[0]);
    int32_t ancestorId = RValue_toInt32(args[1]);

    int32_t parentId = ctx->dataWin->objt.objects[id].parentId;
    if (parentId == -1) return RValue_makeBool(false);

    while (parentId != -1) {
        if (parentId == ancestorId) return RValue_makeBool(true);
        parentId = ctx->dataWin->objt.objects[parentId].parentId;
    }
    return RValue_makeBool(false);
}

// Shared implementation for font_add_sprite and font_add_sprite_ext
static RValue fontAddSpriteImpl(VMContext* ctx, int32_t spriteIndex, uint16_t* charCodes, uint32_t charCount, bool proportional, int32_t sep) {
    DataWin* dw = ctx->dataWin;

    if (0 > spriteIndex || (uint32_t) spriteIndex >= dw->sprt.count) {
        fprintf(stderr, "[font_add_sprite] Invalid sprite index %d\n", spriteIndex);
        return RValue_makeReal(-1.0);
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];

    if (charCount == 0 || sprite->textureCount == 0) {
        return RValue_makeReal(-1.0);
    }

    // Limit glyph count to sprite frame count
    uint32_t glyphCount = charCount;
    if (glyphCount > sprite->textureCount) glyphCount = sprite->textureCount;

    // GM 2023.2 changed sprite-font glyph placement to subtract the source sprite's origin.
    // (See GameMaker-HTML5's commit a7c5b909209d5a28602fedfe2031965386a99921)
    bool spriteFontSubtractsOrigin = DataWin_isVersionAtLeast(dw, 2023, 2, 0, 0);

    // Compute emSize (max bounding height across all frames) and biggestShift
    uint32_t maxHeight = 0;
    int32_t biggestShift = 0;
    repeat(glyphCount, i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (0 > tpagIdx) continue;
        TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
        if (tpag->boundingHeight > maxHeight) maxHeight = tpag->boundingHeight;
        int32_t width = proportional ? (int32_t) tpag->sourceWidth : (int32_t) tpag->boundingWidth;
        if (width > biggestShift) biggestShift = width;
    }

    // Check if space (0x20) is in the string map
    bool hasSpace = false;
    repeat(glyphCount, i) {
        if (charCodes[i] == 0x20) { hasSpace = true; break; }
    }

    // Allocate glyphs (+ 1 for synthetic space if needed)
    uint32_t totalGlyphs = hasSpace ? glyphCount : glyphCount + 1;
    FontGlyph* glyphs = safeMalloc(totalGlyphs * sizeof(FontGlyph));

    repeat(glyphCount, i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        FontGlyph* glyph = &glyphs[i];
        glyph->character = charCodes[i];
        glyph->kerningCount = 0;
        glyph->kerning = nullptr;

        if (0 > tpagIdx) {
            glyph->sourceX = 0;
            glyph->sourceY = 0;
            glyph->sourceWidth = 0;
            glyph->sourceHeight = 0;
            glyph->shift = (int16_t) sep;
            glyph->offset = 0;
            continue;
        }

        TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
        glyph->sourceX = 0; // not used for sprite fonts (TPAG resolved per glyph)
        glyph->sourceY = 0;
        glyph->sourceWidth = tpag->sourceWidth;
        glyph->sourceHeight = tpag->sourceHeight;

        int32_t advanceWidth = proportional ? (int32_t) tpag->sourceWidth : (int32_t) tpag->boundingWidth;
        glyph->shift = (int16_t) (advanceWidth + sep);

        // Horizontal offset: proportional fonts have none. Non-proportional uses the cell offset targetX, minus the sprite origin only on GM 2023.2+ (pre-2023.2 the origin cancels).
        int32_t xOff = (int32_t) tpag->targetX - (spriteFontSubtractsOrigin ? sprite->originX : 0);
        glyph->offset = proportional ? 0 : (int16_t) xOff;
    }

    // Add synthetic space glyph if space is not in the string map
    if (!hasSpace) {
        FontGlyph* spaceGlyph = &glyphs[glyphCount];
        spaceGlyph->character = 0x20;
        spaceGlyph->sourceX = 0;
        spaceGlyph->sourceY = 0;
        spaceGlyph->sourceWidth = 0;
        spaceGlyph->sourceHeight = 0;
        spaceGlyph->shift = (int16_t) (biggestShift + sep);
        spaceGlyph->offset = 0;
        spaceGlyph->kerningCount = 0;
        spaceGlyph->kerning = nullptr;
    }

    // Grow the font array and create the new font
    uint32_t newFontIndex = dw->font.count;
    dw->font.count++;
    dw->font.fonts = safeRealloc(dw->font.fonts, dw->font.count * sizeof(Font));

    Font* font = &dw->font.fonts[newFontIndex];
    font->name = "sprite_font";
    font->displayName = "sprite_font";
    font->emSize = (maxHeight > 0) ? maxHeight : sprite->height;
    font->bold = false;
    font->italic = false;
    font->rangeStart = 0;
    font->charset = 0;
    font->antiAliasing = 0;
    font->rangeEnd = 0;
    font->tpagIndex = -1; // not used for sprite fonts
    font->scaleX = 1.0f;
    font->scaleY = 1.0f;
    font->ascenderOffset = 0;
    font->glyphCount = totalGlyphs;
    font->glyphs = glyphs;
    // Line height = full frame bounding height (used for line stride and fa_middle/fa_bottom
    // valign offsets), matching the native runner's sprite-font TextHeight (= sprite height).
    font->maxGlyphHeight = maxHeight;
    font->isSpriteFont = true;
    font->spriteIndex = spriteIndex;
    // Precompute the per-glyph Y origin adjustment (the X half is baked into glyph->offset above).
    font->spriteOriginYAdjust = spriteFontSubtractsOrigin ? (int16_t) sprite->originY : 0;
    Font_buildGlyphLUT(font);

    return RValue_makeReal((GMLReal) newFontIndex);
}

static RValue builtin_font_get_name(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "[font_get_name] Expected 1 argument, got 0");
        return RValue_makeUndefined();
    }

    int32_t fontIndex = RValue_toInt32(args[0]);
    if (0 > fontIndex || (uint32_t) fontIndex >= ctx->dataWin->font.count) return RValue_makeUndefined();
    return RValue_makeString(ctx->dataWin->font.fonts[fontIndex].name);
}

// font_get_info(font): returns a struct with the font information.
static RValue builtin_font_get_info(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t fontIndex = RValue_toInt32(args[0]);
    if (0 > fontIndex || (uint32_t) fontIndex >= ctx->dataWin->font.count) return RValue_makeUndefined();
    Font* font = &ctx->dataWin->font.fonts[fontIndex];

    Instance* ret = Runner_createStruct(ctx->runner);
    VM_structSetAndFreeVal(ctx, ret, "spriteIndex", RValue_makeInt32(font->isSpriteFont ? font->spriteIndex : -1), -1);
    VM_structSetAndFreeVal(ctx, ret, "size", RValue_makeInt32((int32_t) font->emSize), -1);
    VM_structSetAndFreeVal(ctx, ret, "ascender", RValue_makeInt32((int32_t) font->ascender), -1);
    VM_structSetAndFreeVal(ctx, ret, "ascenderOffset", RValue_makeInt32(font->ascenderOffset), -1);
    VM_structSetAndFreeVal(ctx, ret, "sdfSpread", RValue_makeInt32((int32_t) font->sdfSpread), -1);
    VM_structSetAndFreeVal(ctx, ret, "sdfEnabled", RValue_makeBool(font->sdfSpread != 0), -1);
    VM_structSetAndFreeVal(ctx, ret, "freetype", RValue_makeBool(false), -1);
    VM_structSetAndFreeVal(ctx, ret, "name", RValue_makeString(font->name ? font->name : ""), -1);
    VM_structSetAndFreeVal(ctx, ret, "bold", RValue_makeBool(font->bold), -1);
    VM_structSetAndFreeVal(ctx, ret, "italic", RValue_makeBool(font->italic), -1);
    VM_structSetAndFreeVal(ctx, ret, "texture", RValue_makeInt32(-1), -1);
    // glyphs: char -> { char: glyphIndexInSprite }, matching GameMaker-HTML5's SpriteMapDictionary.
    Instance* glyphs = Runner_createStruct(ctx->runner);
    if (font->isSpriteFont) {
        repeat(font->glyphCount, glyphIndex) {
            uint16_t cp = font->glyphs[glyphIndex].character;
            char key[8];
            int32_t klen = TextUtils_utf8EncodeCodepoint(cp, key);
            key[klen] = '\0';
            Instance* glyphEntry = Runner_createStruct(ctx->runner);
            VM_structSetAndFreeVal(ctx, glyphEntry, "char", RValue_makeInt32((int32_t) glyphIndex), -1);
            RValue glyphEntryRValue = RValue_makeStructAndIncRef(glyphEntry);
            VM_structSetAndFreeVal(ctx, glyphs, key, glyphEntryRValue, -1);
        }
    }
    RValue glyphsVal = RValue_makeStructAndIncRef(glyphs);
    VM_structSetAndFreeVal(ctx, ret, "glyphs", glyphsVal, -1);

    return RValue_makeStructAndIncRef(ret);
}

// font_add_sprite_ext(sprite, string_map, prop, sep)
static RValue builtin_font_add_sprite_ext(VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) {
        fprintf(stderr, "[font_add_sprite_ext] Expected 4 arguments, got %d\n", argCount);
        return RValue_makeReal(-1.0);
    }

    int32_t spriteIndex = RValue_toInt32(args[0]);
    char* stringMap = RValue_toString(args[1]);
    bool proportional = RValue_toBool(args[2]);
    int32_t sep = RValue_toInt32(args[3]);

    // Decode the string map to get character codes (UTF-8 -> codepoints)
    int32_t mapLen = (int32_t) strlen(stringMap);
    int32_t mapPos = 0;
    uint32_t charCount = 0;
    uint16_t charCodes[1024];
    while (mapLen > mapPos && 1024 > charCount) {
        charCodes[charCount++] = TextUtils_decodeUtf8(stringMap, mapLen, &mapPos);
    }
    free(stringMap);

    return fontAddSpriteImpl(ctx, spriteIndex, charCodes, charCount, proportional, sep);
}

// font_add_sprite(sprite, first, prop, sep)
static RValue builtin_font_add_sprite(VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) {
        fprintf(stderr, "[font_add_sprite] Expected 4 arguments, got %d\n", argCount);
        return RValue_makeReal(-1.0);
    }

    DataWin* dw = ctx->dataWin;
    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t first = RValue_toInt32(args[1]);
    bool proportional = RValue_toBool(args[2]);
    int32_t sep = RValue_toInt32(args[3]);

    // Build sequential character codes: first, first+1, first+2, ...
    uint32_t frameCount = 0;
    if (spriteIndex >= 0 && dw->sprt.count > (uint32_t) spriteIndex) {
        frameCount = dw->sprt.sprites[spriteIndex].textureCount;
    }
    if (frameCount > 1024) frameCount = 1024;

    uint16_t charCodes[1024];
    repeat(frameCount, i) {
        charCodes[i] = (uint16_t) (first + (int32_t) i);
    }

    return fontAddSpriteImpl(ctx, spriteIndex, charCodes, frameCount, proportional, sep);
}

static RValue builtin_asset_get_index(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "[asset_get_index] Expected at least 1 argument\n");
        return RValue_makeUndefined();
    }

    char* name = RValue_toString(args[0]);

    int32_t value = shget(ctx->runner->assetsByName, name);
    free(name);
    return RValue_makeReal(value);
}

static RValue builtin_gpu_set_blendmode(VMContext* ctx, RValue* args, int32_t argCount) {
    int mode = RValue_toReal(args[0]);
    ctx->runner->renderer->vtable->gpuSetBlendMode(ctx->runner->renderer, mode);
    return RValue_makeUndefined();
}

static RValue builtin_gpu_set_blendmode_ext(VMContext* ctx, RValue* args, int32_t argCount) {
    int sfactor = RValue_toReal(args[0]);
    int dfactor = RValue_toReal(args[1]);
    ctx->runner->renderer->vtable->gpuSetBlendModeExt(ctx->runner->renderer, sfactor, dfactor);
    return RValue_makeUndefined();
}

static RValue builtin_gpu_set_blendenable(VMContext* ctx, RValue* args, int32_t argCount) {
    bool enable = RValue_toBool(args[0]);
    ctx->runner->renderer->vtable->gpuSetBlendEnable(ctx->runner->renderer, enable);
    return RValue_makeUndefined();
}

static RValue builtin_gpu_get_blendenable(VMContext* ctx, RValue* args, int32_t argCount) {
    return RValue_makeBool(ctx->runner->renderer->vtable->gpuGetBlendEnable(ctx->runner->renderer));
}

static RValue builtin_gpu_set_alphatestenable(VMContext* ctx, RValue* args, int32_t argCount) {
    bool enable = RValue_toBool(args[0]);
    ctx->runner->renderer->vtable->gpuSetAlphaTestEnable(ctx->runner->renderer, enable);
    return RValue_makeUndefined();
}

static RValue builtin_gpu_set_alphatestref(VMContext* ctx, RValue* args, int32_t argCount) {
    ctx->runner->renderer->vtable->gpuSetAlphaTestRef(ctx->runner->renderer, RValue_toInt32(args[0]));
    return RValue_makeUndefined();
}

static RValue builtin_gpu_set_fog(VMContext* ctx, RValue* args, int32_t argCount) {
    bool enable;
    int32_t color;
    if (argCount == 1 && args[0].type == RVALUE_ARRAY && args[0].array != nullptr && GMLArray_length1D(args[0].array) >= 2) {
        GMLArray* arr = args[0].array;
        enable = RValue_toBool(*GMLArray_slot(arr, 0));
        color = RValue_toInt32(*GMLArray_slot(arr, 1));
    } else if (argCount >= 2) {
        enable = RValue_toBool(args[0]);
        color = RValue_toInt32(args[1]);
    } else {
        return RValue_makeUndefined();
    }
    if (ctx->runner->renderer->vtable->gpuSetFog != nullptr) {
        ctx->runner->renderer->vtable->gpuSetFog(ctx->runner->renderer, enable, (uint32_t) color);
    }
    return RValue_makeUndefined();
}

static RValue builtin_gpu_set_colorwriteenable(VMContext* ctx, RValue* args, int32_t argCount) {
    bool r, g, b, a;
    if (argCount == 1 && args[0].type == RVALUE_ARRAY && args[0].array != nullptr && GMLArray_length1D(args[0].array) >= 4) {
        GMLArray* arr = args[0].array;
        r = RValue_toBool(*GMLArray_slot(arr, 0));
        g = RValue_toBool(*GMLArray_slot(arr, 1));
        b = RValue_toBool(*GMLArray_slot(arr, 2));
        a = RValue_toBool(*GMLArray_slot(arr, 3));
    } else if (argCount >= 4) {
        r = RValue_toBool(args[0]);
        g = RValue_toBool(args[1]);
        b = RValue_toBool(args[2]);
        a = RValue_toBool(args[3]);
    } else {
        return RValue_makeUndefined();
    }
    ctx->runner->renderer->vtable->gpuSetColorWriteEnable(ctx->runner->renderer, r, g, b, a);
    return RValue_makeUndefined();
}

static RValue builtin_gpu_get_colorwriteenable(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    bool r, g, b, a;
    ctx->runner->renderer->vtable->gpuGetColorWriteEnable(ctx->runner->renderer, &r, &g, &b, &a);
    GMLArray* out = GMLArray_create(4);
    *GMLArray_slot(out, 0) = RValue_makeReal(r ? 1.0 : 0.0);
    *GMLArray_slot(out, 1) = RValue_makeReal(g ? 1.0 : 0.0);
    *GMLArray_slot(out, 2) = RValue_makeReal(b ? 1.0 : 0.0);
    *GMLArray_slot(out, 3) = RValue_makeReal(a ? 1.0 : 0.0);
    return RValue_makeArray(out);
}

static RValue builtin_game_change(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();

    char* workingDirectory = RValue_toString(args[0]);
    char* launchParameters = RValue_toString(args[1]);

    // I really doubt that a game calls game_change twice in a row, but...
    if (ctx->runner->pendingWorkingDirectory != nullptr) {
        free(ctx->runner->pendingWorkingDirectory);
    }

    if (ctx->runner->pendingLaunchParameters != nullptr) {
        free(ctx->runner->pendingLaunchParameters);
    }

    ctx->runner->pendingWorkingDirectory = workingDirectory;
    ctx->runner->pendingLaunchParameters = launchParameters;

    return RValue_makeUndefined();
}

static RValue builtin_parameter_count(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeReal((int32_t) arrlen(ctx->runner->gameArgs));
}

static RValue builtin_parameter_string(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeString("");
    int32_t index = RValue_toInt32(args[0]);
    if (0 > index || index >= (int32_t) arrlen(ctx->runner->gameArgs)) return RValue_makeString("");
    return RValue_makeString(ctx->runner->gameArgs[index]);
}

static RValue builtin_shader_set(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t ShaderID = (int32_t) RValue_toReal(args[0]);
    //fprintf(stderr, "Set Shader ID %u\n", ShaderID);
    //gpuSetShader
    ctx->runner->renderer->vtable->gpuSetShader(ctx->runner->renderer, ShaderID);
    return RValue_makeUndefined();
}

static RValue builtin_shader_reset(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    ctx->runner->renderer->vtable->gpuResetShader(ctx->runner->renderer);
    return RValue_makeUndefined();
}

static RValue builtin_shader_current(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (ctx->runner->renderer != nullptr) {
        return RValue_makeReal(ctx->runner->renderer->currentShader);
    }
    return RValue_makeReal(-1);
}

static RValue builtin_shader_is_compiled(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t ShaderID = (int32_t) RValue_toReal(args[0]);
    return RValue_makeBool(ctx->runner->renderer->vtable->shaderIsCompiled(ctx->runner->renderer, ShaderID));
}

static RValue builtin_shader_get_name(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t shaderIndex = (int32_t) RValue_toReal(args[0]);
    Shader* shdr = &ctx->dataWin->shdr.shaders[shaderIndex];
    if (0 > shaderIndex || (uint32_t) shaderIndex >= ctx->dataWin->shdr.count) return RValue_makeString("<undefined>");
    const char* name = shdr->name;
    return RValue_makeString(name != nullptr ? name : "<undefined>");
}

static RValue builtin_shaders_are_supported(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeBool(ctx->runner->renderer->vtable->shadersSupported(ctx->runner->renderer));
}

static RValue builtin_shader_get_uniform(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t ShaderID = (int32_t) RValue_toReal(args[0]);
    char* uniform = RValue_toString(args[1]);
    return RValue_makeInt32(ctx->runner->renderer->vtable->shaderGetUniform(ctx->runner->renderer, ShaderID, uniform));
}

static RValue builtin_shader_get_sampler_index(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t ShaderID = (int32_t) RValue_toReal(args[0]);
    char* uniform = RValue_toString(args[1]);
    return RValue_makeInt32(ctx->runner->renderer->vtable->shaderGetSamplerIndex(ctx->runner->renderer, ShaderID, uniform));
}

static RValue builtin_texture_set_stage(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t TextureSlot = (int32_t) RValue_toReal(args[0]);
    int32_t texID = (int32_t) RValue_toInt32(args[1]);
    ctx->runner->renderer->vtable->textureSetStage(ctx->runner->renderer, TextureSlot, texID);
    return RValue_makeUndefined();
}

static RValue builtin_shader_set_uniformF(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t handle = (int32_t) RValue_toReal(args[0]);
    float value1, value2, value3, value4;
    //fprintf(stderr, "Set ARG Count %u\n", argCount);
    if (argCount == 2) {
        value1 = (float) RValue_toReal(args[1]);
        ctx->runner->renderer->vtable->shaderSetUniformF(ctx->runner->renderer, handle, 1, value1, 0.0, 0.0, 0.0);
    } else if (argCount == 3) {
        value1 = (float) RValue_toReal(args[1]);
        value2 = (float) RValue_toReal(args[2]);
        ctx->runner->renderer->vtable->shaderSetUniformF(ctx->runner->renderer, handle, 2, value1, value2, 0.0, 0.0);
    } else if (argCount == 4) {
        value1 = (float) RValue_toReal(args[1]);
        value2 = (float) RValue_toReal(args[2]);
        value3 = (float) RValue_toReal(args[3]);
        ctx->runner->renderer->vtable->shaderSetUniformF(ctx->runner->renderer, handle, 3, value1, value2, value3, 0.0);
    } else if (argCount == 5) {
        value1 = (float) RValue_toReal(args[1]);
        value2 = (float) RValue_toReal(args[2]);
        value3 = (float) RValue_toReal(args[3]);
        value4 = (float) RValue_toReal(args[4]);
        //fprintf(stderr, "Value4  %.8f\n", value4);
        ctx->runner->renderer->vtable->shaderSetUniformF(ctx->runner->renderer, handle, 4, value1, value2, value3, value4);
    }
    return RValue_makeUndefined();
}

static RValue builtin_shader_set_uniform_f_array(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (argCount < 2) return RValue_makeUndefined();
    
    int32_t handle = (int32_t) RValue_toReal(args[0]);
    if (args[1].type != RVALUE_ARRAY || args[1].array == nullptr) {
        return RValue_makeUndefined();
    }
    
    GMLArray* arr = args[1].array;
    uint32_t count = GMLArray_length1D(arr);
    if (count == 0) return RValue_makeUndefined();
    
    float* values = safeMalloc(count * sizeof(float));
    for (uint32_t i = 0; i < count; i++) {
        values[i] = (float) RValue_toReal(*GMLArray_slot(arr, i));
    }
    
    if (ctx->runner->renderer->vtable->shaderSetUniformFArray != nullptr) {
        ctx->runner->renderer->vtable->shaderSetUniformFArray(ctx->runner->renderer, handle, values, count);
    }
    
    free(values);
    return RValue_makeUndefined();
}

static RValue builtin_shader_set_uniformI(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t handle = (int32_t) RValue_toReal(args[0]);
    int32_t value1 = 0, value2 = 0, value3 = 0, value4 = 0;
    int32_t count = argCount - 1;
    if (count >= 1) value1 = (int32_t) RValue_toReal(args[1]);
    if (count >= 2) value2 = (int32_t) RValue_toReal(args[2]);
    if (count >= 3) value3 = (int32_t) RValue_toReal(args[3]);
    if (count >= 4) value4 = (int32_t) RValue_toReal(args[4]);
    ctx->runner->renderer->vtable->shaderSetUniformI(ctx->runner->renderer, handle, count, value1, value2, value3, value4);
    return RValue_makeUndefined();
}

static RValue builtin_sprite_get_uvs(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }  
    int32_t TpagIndex = Renderer_resolveTPAGIndex(ctx->dataWin, spriteIndex, subimg);
    //I think default texture page size is 2048x2048?
    float DivW = 0.00048828125; //1.0/2048.0
    float DivH = 0.00048828125; //1.0/2048.0

    DivW = ctx->runner->renderer->vtable->textureGetTexelWidth(ctx->runner->renderer, ctx->runner->renderer->vtable->spriteGetTexture(ctx->runner->renderer, TpagIndex));
    DivH = ctx->runner->renderer->vtable->textureGetTexelHeight(ctx->runner->renderer, ctx->runner->renderer->vtable->spriteGetTexture(ctx->runner->renderer, TpagIndex));
    
    float left = (float) ctx->dataWin->tpag.items[TpagIndex].sourceX * DivW;
    float top = (float) ctx->dataWin->tpag.items[TpagIndex].sourceY * DivH;
    float right = (float)  left + (ctx->dataWin->tpag.items[TpagIndex].sourceWidth * DivW);
    float bottom = (float) top + (ctx->dataWin->tpag.items[TpagIndex].sourceHeight * DivH);
    float trimmedLeft = (float) ctx->dataWin->tpag.items[TpagIndex].targetX;
    float trimmedTop = (float) ctx->dataWin->tpag.items[TpagIndex].targetY;
    float NormWidthS = (float) ctx->dataWin->tpag.items[TpagIndex].sourceWidth / (float) ctx->dataWin->tpag.items[TpagIndex].boundingWidth;
    float NormHeightS = (float) ctx->dataWin->tpag.items[TpagIndex].sourceHeight / (float) ctx->dataWin->tpag.items[TpagIndex].boundingHeight;


    GMLArray* out = GMLArray_create(8);
    *GMLArray_slot(out, 0) = RValue_makeReal(left);
    *GMLArray_slot(out, 1) = RValue_makeReal(top);
    *GMLArray_slot(out, 2) = RValue_makeReal(right);
    *GMLArray_slot(out, 3) = RValue_makeReal(bottom);
    *GMLArray_slot(out, 4) = RValue_makeReal(trimmedLeft);
    *GMLArray_slot(out, 5) = RValue_makeReal(trimmedTop);
    *GMLArray_slot(out, 6) = RValue_makeReal(NormWidthS);
    *GMLArray_slot(out, 7) = RValue_makeReal(NormHeightS);
    return RValue_makeArray(out);
}

static RValue builtin_sprite_get_texture(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {

    int32_t spriteIndex = (int32_t) RValue_toReal(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ctx->currentInstance->imageIndex;
    }  
    int32_t TpagIndex = Renderer_resolveTPAGIndex(ctx->dataWin, spriteIndex, subimg);

    return RValue_makeInt32(ctx->runner->renderer->vtable->spriteGetTexture(ctx->runner->renderer, TpagIndex));
}

static RValue builtin_font_get_uvs(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    int32_t fontIndex = (int32_t) RValue_toReal(args[0]);

    //if (0 > fontIndex || ctx->dataWin->font.count <= (uint32_t) fontIndex) return;

    Font* font = &ctx->runner->dataWin->font.fonts[fontIndex];
    

    int32_t TpagIndex = font->tpagIndex;
    //I think default texture page size is 2048x2048?
    float DivW = 0.00048828125; //1.0/2048.0
    float DivH = 0.00048828125; //1.0/2048.0
    
    DivW = ctx->runner->renderer->vtable->textureGetTexelWidth(ctx->runner->renderer, ctx->runner->renderer->vtable->spriteGetTexture(ctx->runner->renderer, TpagIndex));
    DivH = ctx->runner->renderer->vtable->textureGetTexelHeight(ctx->runner->renderer, ctx->runner->renderer->vtable->spriteGetTexture(ctx->runner->renderer, TpagIndex));
    
    float left = (float) ctx->dataWin->tpag.items[TpagIndex].sourceX * DivW;
    float top = (float) ctx->dataWin->tpag.items[TpagIndex].sourceY * DivH;
    float right = (float)  left + (ctx->dataWin->tpag.items[TpagIndex].sourceWidth * DivW);
    float bottom = (float) top + (ctx->dataWin->tpag.items[TpagIndex].sourceHeight * DivH);

    GMLArray* out = GMLArray_create(4);
    *GMLArray_slot(out, 0) = RValue_makeReal(left);
    *GMLArray_slot(out, 1) = RValue_makeReal(top);
    *GMLArray_slot(out, 2) = RValue_makeReal(right);
    *GMLArray_slot(out, 3) = RValue_makeReal(bottom);
    return RValue_makeArray(out);
}

static RValue builtin_font_get_texture(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    //see if it works
    int32_t fontIndex = (int32_t) RValue_toReal(args[0]);
    Font* font = &ctx->runner->dataWin->font.fonts[fontIndex];
    int32_t TpagIndex = font->tpagIndex;

    return RValue_makeInt32(ctx->runner->renderer->vtable->spriteGetTexture(ctx->runner->renderer, TpagIndex));
}

static RValue builtin_texture_get_texel_width(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {

    uint32_t texID = (uint32_t) RValue_toReal(args[0]);
    return RValue_makeReal(ctx->runner->renderer->vtable->textureGetTexelWidth(ctx->runner->renderer, texID));
}

static RValue builtin_texture_get_texel_height(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {

    uint32_t texID = (uint32_t) RValue_toReal(args[0]);
    return RValue_makeReal(ctx->runner->renderer->vtable->textureGetTexelHeight(ctx->runner->renderer, texID));
}

static RValue builtin_texture_get_uvs(VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    uint32_t texID = (uint32_t) RValue_toReal(args[0]);
    // Default to the full page (0,0,1,1) if the renderer can't resolve the handle.
    float uvs[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ctx->runner->renderer->vtable->textureGetUVs(ctx->runner->renderer, texID, uvs);
    GMLArray* out = GMLArray_create(4);
    *GMLArray_slot(out, 0) = RValue_makeReal(uvs[0]);
    *GMLArray_slot(out, 1) = RValue_makeReal(uvs[1]);
    *GMLArray_slot(out, 2) = RValue_makeReal(uvs[2]);
    *GMLArray_slot(out, 3) = RValue_makeReal(uvs[3]);
    return RValue_makeArray(out);
}

// ===[ REGISTRATION ]===

void VMBuiltins_registerAll(VMContext* ctx) {
    requireMessage(!ctx->registeredBuiltinFunctions, "Attempting to register all VMBuiltins, but it was already registered!");
    ctx->registeredBuiltinFunctions = true;

    const bool isGMS2 = DataWin_isVersionAtLeast(ctx->dataWin, 2, 0, 0, 0);

    // Core output
    VM_registerBuiltin(ctx, "show_debug_message", builtin_show_debug_message);

    // String functions
    VM_registerBuiltin(ctx, "string_length", builtin_string_length);
    VM_registerBuiltin(ctx, "string_letters", builtin_string_letters);
    VM_registerBuiltin(ctx, "string_digits", builtin_string_digits);
    VM_registerBuiltin(ctx, "string_lettersdigits", builtin_string_lettersdigits);
    VM_registerBuiltin(ctx, "string_byte_length", builtin_string_byte_length);
    VM_registerBuiltin(ctx, "string", builtin_string);
    VM_registerBuiltin(ctx, "string_upper", builtin_string_upper);
    VM_registerBuiltin(ctx, "string_lower", builtin_string_lower);
    VM_registerBuiltin(ctx, "string_copy", builtin_string_copy);
    VM_registerBuiltin(ctx, "string_pos", builtin_string_pos);
    VM_registerBuiltin(ctx, "string_char_at", builtin_string_char_at);
    VM_registerBuiltin(ctx, "string_ord_at", builtin_string_ord_at);
    VM_registerBuiltin(ctx, "string_split", builtin_string_split);
    VM_registerBuiltin(ctx, "string_delete", builtin_string_delete);
    VM_registerBuiltin(ctx, "string_insert", builtin_string_insert);
    VM_registerBuiltin(ctx, "string_replace", builtin_string_replace);
    VM_registerBuiltin(ctx, "string_replace_all", builtin_string_replace_all);
    VM_registerBuiltin(ctx, "string_repeat", builtin_string_repeat);
    VM_registerBuiltin(ctx, "string_format", builtin_string_format);
    VM_registerBuiltin(ctx, "string_count", builtin_string_count);
    VM_registerBuiltin(ctx, "ord", builtin_ord);
    VM_registerBuiltin(ctx, "chr", builtin_chr);

    // Type functions
    VM_registerBuiltin(ctx, "real", builtin_real);
    VM_registerBuiltin(ctx, "is_string", builtin_is_string);
    VM_registerBuiltin(ctx, "is_real", builtin_is_real);
    VM_registerBuiltin(ctx, "is_nan", builtin_is_nan);
    VM_registerBuiltin(ctx, "is_infinity", builtin_is_infinity);
    VM_registerBuiltin(ctx, "is_numeric", builtin_is_real);
    VM_registerBuiltin(ctx, "is_bool", builtin_is_bool);
    VM_registerBuiltin(ctx, "is_array", builtin_is_array);
    VM_registerBuiltin(ctx, "is_struct", builtin_is_struct);
    VM_registerBuiltin(ctx, "is_int32", builtin_is_int32);
    VM_registerBuiltin(ctx, "is_int64", builtin_is_int64);
    VM_registerBuiltin(ctx, "is_undefined", builtin_is_undefined);
#if IS_WAD17_OR_HIGHER_ENABLED
    VM_registerBuiltin(ctx, "is_method", builtin_is_method);
    VM_registerBuiltin(ctx, "is_callable", builtin_is_callable);
#endif

    // Math functions
    VM_registerBuiltin(ctx, "floor", builtin_floor);
    VM_registerBuiltin(ctx, "ceil", builtin_ceil);
    VM_registerBuiltin(ctx, "round", builtin_round);
    VM_registerBuiltin(ctx, "abs", builtin_abs);
    VM_registerBuiltin(ctx, "frac", builtin_frac);
    VM_registerBuiltin(ctx, "sign", builtin_sign);
    VM_registerBuiltin(ctx, "max", builtin_max);
    VM_registerBuiltin(ctx, "min", builtin_min);
    VM_registerBuiltin(ctx, "median", builtin_median);
    VM_registerBuiltin(ctx, "power", builtin_power);
    VM_registerBuiltin(ctx, "sqrt", builtin_sqrt);
    VM_registerBuiltin(ctx, "log2", builtin_log2);
    VM_registerBuiltin(ctx, "sqr", builtin_sqr);
    VM_registerBuiltin(ctx, "sin", builtin_sin);
    VM_registerBuiltin(ctx, "arccos", builtin_arccos);
    VM_registerBuiltin(ctx, "arcsin", builtin_arcsin);
    VM_registerBuiltin(ctx, "arctan", builtin_arctan);
    VM_registerBuiltin(ctx, "cos", builtin_cos);
    VM_registerBuiltin(ctx, "dsin", builtin_dsin);
    VM_registerBuiltin(ctx, "dcos", builtin_dcos);
    VM_registerBuiltin(ctx, "darctan", builtin_darctan);
    VM_registerBuiltin(ctx, "darctan2", builtin_darctan2);
    VM_registerBuiltin(ctx, "degtorad", builtin_degtorad);
    VM_registerBuiltin(ctx, "radtodeg", builtin_radtodeg);
    VM_registerBuiltin(ctx, "clamp", builtin_clamp);
    VM_registerBuiltin(ctx, "lerp", builtin_lerp);
    VM_registerBuiltin(ctx, "point_distance", builtin_point_distance);
    VM_registerBuiltin(ctx, "point_in_rectangle", builtin_point_in_rectangle);
    VM_registerBuiltin(ctx, "point_in_circle", builtin_point_in_circle);
    VM_registerBuiltin(ctx, "point_direction", builtin_point_direction);
    VM_registerBuiltin(ctx, "angle_difference", builtin_angle_difference);
    VM_registerBuiltin(ctx, "distance_to_point", builtin_distance_to_point);
    VM_registerBuiltin(ctx, "distance_to_object", builtin_distance_to_object);
    VM_registerBuiltin(ctx, "move_towards_point", builtin_move_towards_point);
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "action_move_point", builtin_move_towards_point);
    }
    VM_registerBuiltin(ctx, "move_snap", builtin_move_snap);
    VM_registerBuiltin(ctx, "move_contact_solid", builtin_move_contact_solid);
    VM_registerBuiltin(ctx, "move_outside_solid", builtin_move_outside_solid);
    VM_registerBuiltin(ctx, "move_outside_all", builtin_move_outside_all);
    VM_registerBuiltin(ctx, "move_bounce_solid", builtin_move_bounce_solid);
    VM_registerBuiltin(ctx, "move_bounce_all", builtin_move_bounce_all);    
    VM_registerBuiltin(ctx, "lengthdir_x", builtin_lengthdir_x);
    VM_registerBuiltin(ctx, "lengthdir_y", builtin_lengthdir_y);

    // Matrix/linear algebra
    VM_registerBuiltin(ctx, "matrix_build_identity", builtin_matrix_build_identity);
    VM_registerBuiltin(ctx, "matrix_inverse", builtin_matrix_inverse);
    VM_registerBuiltin(ctx, "matrix_multiply", builtin_matrix_multiply);
    VM_registerBuiltin(ctx, "matrix_build_lookat", builtin_matrix_build_lookat);
    VM_registerBuiltin(ctx, "matrix_build_projection_ortho", builtin_matrix_build_projection_ortho);
    VM_registerBuiltin(ctx, "matrix_build_projection_perspective_fov", builtin_matrix_build_projection_perspective_fov);

    // Random
    VM_registerBuiltin(ctx, "random", builtin_random);
    VM_registerBuiltin(ctx, "random_range", builtin_random_range);
    VM_registerBuiltin(ctx, "irandom", builtin_irandom);
    VM_registerBuiltin(ctx, "irandom_range", builtin_irandom_range);
    VM_registerBuiltin(ctx, "choose", builtin_choose);
    VM_registerBuiltin(ctx, "randomize", builtin_randomize);
    VM_registerBuiltin(ctx, "randomise", builtin_randomize);

    // Room
    VM_registerBuiltin(ctx, "game_get_speed", builtin_game_get_speed);
    VM_registerBuiltin(ctx, "room_exists", builtin_room_exists);
    VM_registerBuiltin(ctx, "room_get_name", builtin_room_get_name);
    VM_registerBuiltin(ctx, "room_get_info", builtin_room_get_info);
    VM_registerBuiltin(ctx, "room_goto_next", builtin_room_goto_next);
    VM_registerBuiltin(ctx, "room_goto_previous", builtin_room_goto_previous);
    VM_registerBuiltin(ctx, "room_goto", builtin_room_goto);
    VM_registerBuiltin(ctx, "room_restart", builtin_room_restart);
    VM_registerBuiltin(ctx, "room_next", builtin_room_next);
    VM_registerBuiltin(ctx, "room_previous", builtin_room_previous);
    VM_registerBuiltin(ctx, "room_set_persistent", builtin_room_set_persistent);

    // GMS2 camera compatibility
    VM_registerBuiltin(ctx, "view_get_camera", builtin_view_get_camera);
    VM_registerBuiltin(ctx, "view_get_visible", builtin_view_get_visible);
    VM_registerBuiltin(ctx, "view_get_xport", builtin_view_get_xport);
    VM_registerBuiltin(ctx, "view_get_yport", builtin_view_get_yport);
    VM_registerBuiltin(ctx, "view_get_wport", builtin_view_get_wport);
    VM_registerBuiltin(ctx, "view_get_hport", builtin_view_get_hport);
    VM_registerBuiltin(ctx, "view_get_surface_id", builtin_view_get_surface_id);
    VM_registerBuiltin(ctx, "view_set_visible", builtin_view_set_visible);
    VM_registerBuiltin(ctx, "view_set_xport", builtin_view_set_xport);
    VM_registerBuiltin(ctx, "view_set_yport", builtin_view_set_yport);
    VM_registerBuiltin(ctx, "view_set_wport", builtin_view_set_wport);
    VM_registerBuiltin(ctx, "view_set_hport", builtin_view_set_hport);
    VM_registerBuiltin(ctx, "view_set_surface_id", builtin_view_set_surface_id);
    VM_registerBuiltin(ctx, "camera_get_view_x", builtin_camera_get_view_x);
    VM_registerBuiltin(ctx, "camera_get_view_y", builtin_camera_get_view_y);
    VM_registerBuiltin(ctx, "camera_get_view_width", builtin_camera_get_view_width);
    VM_registerBuiltin(ctx, "camera_get_view_height", builtin_camera_get_view_height);
    VM_registerBuiltin(ctx, "camera_set_view_pos", builtin_camera_set_view_pos);
    VM_registerBuiltin(ctx, "camera_set_view_mat", builtin_camera_set_view_mat);
    VM_registerBuiltin(ctx, "camera_set_proj_mat", builtin_camera_set_proj_mat);
    VM_registerBuiltin(ctx, "camera_get_view_target", builtin_camera_get_view_target);
    VM_registerBuiltin(ctx, "camera_set_view_target", builtin_camera_set_view_target);
    VM_registerBuiltin(ctx, "camera_get_view_border_x", builtin_camera_get_view_border_x);
    VM_registerBuiltin(ctx, "camera_get_view_border_y", builtin_camera_get_view_border_y);
    VM_registerBuiltin(ctx, "camera_set_view_border", builtin_camera_set_view_border);
    VM_registerBuiltin(ctx, "camera_set_view_size", builtin_camera_set_view_size);
    VM_registerBuiltin(ctx, "camera_set_view_speed", builtin_camera_set_view_speed);
    VM_registerBuiltin(ctx, "camera_set_view_angle", builtin_camera_set_view_angle);
    VM_registerBuiltin(ctx, "camera_get_view_angle", builtin_camera_get_view_angle);
    VM_registerBuiltin(ctx, "camera_get_view_speed_x", builtin_camera_get_view_speed_x);
    VM_registerBuiltin(ctx, "camera_get_view_speed_y", builtin_camera_get_view_speed_y);
    VM_registerBuiltin(ctx, "camera_create", builtin_camera_create);
    VM_registerBuiltin(ctx, "camera_create_view", builtin_camera_create_view);
    VM_registerBuiltin(ctx, "camera_destroy", builtin_camera_destroy);
    VM_registerBuiltin(ctx, "view_set_camera", builtin_view_set_camera);
    VM_registerBuiltin(ctx, "camera_get_active", builtin_camera_get_active);
    VM_registerBuiltin(ctx, "camera_get_default", builtin_camera_get_default);
    VM_registerBuiltin(ctx, "camera_apply", builtin_camera_apply);

    // Variables
    VM_registerBuiltin(ctx, "variable_global_exists", builtin_variable_global_exists);
    VM_registerBuiltin(ctx, "variable_global_get", builtin_variable_global_get);
    VM_registerBuiltin(ctx, "variable_global_set", builtin_variable_global_set);
    VM_registerBuiltin(ctx, "variable_instance_set", builtin_variable_instance_set);
    VM_registerBuiltin(ctx, "variable_instance_get", builtin_variable_instance_get);
    VM_registerBuiltin(ctx, "variable_instance_exists", builtin_variable_instance_exists);
    VM_registerBuiltin(ctx, "variable_struct_set", builtin_variable_struct_set);
    VM_registerBuiltin(ctx, "variable_struct_get", builtin_variable_struct_get);
    VM_registerBuiltin(ctx, "variable_struct_exists", builtin_variable_struct_exists);

    // Script
    VM_registerBuiltin(ctx, "script_execute", builtin_script_execute);
#if IS_WAD17_OR_HIGHER_ENABLED
    VM_registerBuiltin(ctx, "method", builtin_method);
#endif

    // OS
    VM_registerBuiltin(ctx, "os_get_language", builtin_os_get_language);
    VM_registerBuiltin(ctx, "os_get_region", builtin_os_get_region);
    VM_registerBuiltin(ctx, "os_is_paused", builtin_os_is_paused);

    // Xbox One
    VM_registerBuiltin(ctx, "xboxone_show_account_picker", builtin_xboxone_show_account_picker);
    VM_registerBuiltin(ctx, "xboxone_user_is_signed_in", builtin_xboxone_user_is_signed_in);
    VM_registerBuiltin(ctx, "xboxone_is_suspending", builtin_xboxone_is_suspending);
    VM_registerBuiltin(ctx, "xboxone_is_constrained", builtin_xboxone_is_constrained);
    VM_registerBuiltin(ctx, "xboxone_suspend", builtin_xboxone_suspend);
    VM_registerBuiltin(ctx, "xboxone_set_savedata_user", builtin_xboxone_set_savedata_user);
    VM_registerBuiltin(ctx, "xboxone_stats_add_user", builtin_xboxone_stats_add_user);
    VM_registerBuiltin(ctx, "xboxone_achievements_set_progress", builtin_xboxone_achievements_set_progress);
    VM_registerBuiltin(ctx, "environment_get_variable", builtin_environment_get_variable);

    // ds_map
    VM_registerBuiltin(ctx, "ds_map_create", builtin_ds_map_create);
    VM_registerBuiltin(ctx, "ds_map_add", builtin_ds_map_add);
    VM_registerBuiltin(ctx, "ds_map_set", builtin_ds_map_set);
    VM_registerBuiltin(ctx, "ds_map_set_pre", builtin_ds_map_set_pre);
    VM_registerBuiltin(ctx, "ds_map_set_post", builtin_ds_map_set_post);
    VM_registerBuiltin(ctx, "ds_map_replace", builtin_ds_map_replace);
    VM_registerBuiltin(ctx, "ds_map_find_value", builtin_ds_map_find_value);
    VM_registerBuiltin(ctx, "ds_map_exists", builtin_ds_map_exists);
    VM_registerBuiltin(ctx, "ds_map_find_first", builtin_ds_map_find_first);
    VM_registerBuiltin(ctx, "ds_map_find_next", builtin_ds_map_find_next);
    VM_registerBuiltin(ctx, "ds_map_size", builtin_ds_map_size);
    VM_registerBuiltin(ctx, "ds_map_destroy", builtin_ds_map_destroy);

    // ds_list
    VM_registerBuiltin(ctx, "ds_list_create", builtin_ds_list_create);
    VM_registerBuiltin(ctx, "ds_list_destroy", builtin_ds_list_destroy);
    VM_registerBuiltin(ctx, "ds_list_add", builtin_ds_list_add);
    VM_registerBuiltin(ctx, "ds_list_delete", builtin_ds_list_delete);
    VM_registerBuiltin(ctx, "ds_list_empty", builtin_ds_list_empty);
    VM_registerBuiltin(ctx, "ds_list_size", builtin_ds_list_size);
    VM_registerBuiltin(ctx, "ds_list_find_index", builtin_ds_list_find_index);
    VM_registerBuiltin(ctx, "ds_list_find_value", builtin_ds_list_find_value);
    VM_registerBuiltin(ctx, "ds_list_shuffle", builtin_ds_list_shuffle);
    VM_registerBuiltin(ctx, "ds_list_clear", builtin_ds_list_clear);
    VM_registerBuiltin(ctx, "ds_list_write", builtin_ds_list_write);
    VM_registerBuiltin(ctx, "ds_list_read", builtin_ds_list_read);

    // ds_stack
    VM_registerBuiltin(ctx, "ds_stack_create", builtin_ds_stack_create);
    VM_registerBuiltin(ctx, "ds_stack_destroy", builtin_ds_stack_destroy);
    VM_registerBuiltin(ctx, "ds_stack_clear", builtin_ds_stack_clear);
    VM_registerBuiltin(ctx, "ds_stack_copy", builtin_ds_stack_copy);
    VM_registerBuiltin(ctx, "ds_stack_size", builtin_ds_stack_size);
    VM_registerBuiltin(ctx, "ds_stack_empty", builtin_ds_stack_empty);
    VM_registerBuiltin(ctx, "ds_stack_push", builtin_ds_stack_push);
    VM_registerBuiltin(ctx, "ds_stack_pop", builtin_ds_stack_pop);
    VM_registerBuiltin(ctx, "ds_stack_top", builtin_ds_stack_top);
    VM_registerBuiltin(ctx, "ds_stack_write", builtin_ds_stack_write);
    VM_registerBuiltin(ctx, "ds_stack_read", builtin_ds_stack_read);

    // ds_queue
    VM_registerBuiltin(ctx, "ds_queue_create", builtin_ds_queue_create);
    VM_registerBuiltin(ctx, "ds_queue_destroy", builtin_ds_queue_destroy);
    VM_registerBuiltin(ctx, "ds_queue_clear", builtin_ds_queue_clear);
    VM_registerBuiltin(ctx, "ds_queue_copy", builtin_ds_queue_copy);
    VM_registerBuiltin(ctx, "ds_queue_size", builtin_ds_queue_size);
    VM_registerBuiltin(ctx, "ds_queue_empty", builtin_ds_queue_empty);
    VM_registerBuiltin(ctx, "ds_queue_enqueue", builtin_ds_queue_enqueue);
    VM_registerBuiltin(ctx, "ds_queue_dequeue", builtin_ds_queue_dequeue);
    VM_registerBuiltin(ctx, "ds_queue_head", builtin_ds_queue_head);
    VM_registerBuiltin(ctx, "ds_queue_tail", builtin_ds_queue_tail);
    VM_registerBuiltin(ctx, "ds_queue_write", builtin_ds_queue_write);
    VM_registerBuiltin(ctx, "ds_queue_read", builtin_ds_queue_read);

    // ds_priority
    VM_registerBuiltin(ctx, "ds_priority_clear", builtin_ds_priority_clear);
    VM_registerBuiltin(ctx, "ds_priority_copy", builtin_ds_priority_copy);
    VM_registerBuiltin(ctx, "ds_priority_destroy", builtin_ds_priority_destroy);
    VM_registerBuiltin(ctx, "ds_priority_size", builtin_ds_priority_size);
    VM_registerBuiltin(ctx, "ds_priority_empty", builtin_ds_priority_empty);
    VM_registerBuiltin(ctx, "ds_priority_add", builtin_ds_priority_add);
    VM_registerBuiltin(ctx, "ds_priority_delete_value", builtin_ds_priority_delete_value);
    VM_registerBuiltin(ctx, "ds_priority_change_priority", builtin_ds_priority_change_priority);
    VM_registerBuiltin(ctx, "ds_priority_find_priority", builtin_ds_priority_find_priority);
    VM_registerBuiltin(ctx, "ds_priority_delete_min", builtin_ds_priority_delete_min);
    VM_registerBuiltin(ctx, "ds_priority_delete_max", builtin_ds_priority_delete_max);
    VM_registerBuiltin(ctx, "ds_priority_find_min", builtin_ds_priority_find_min);
    VM_registerBuiltin(ctx, "ds_priority_find_max", builtin_ds_priority_find_max);
    VM_registerBuiltin(ctx, "ds_priority_write", builtin_ds_priority_write);
    VM_registerBuiltin(ctx, "ds_priority_read", builtin_ds_priority_read);

    // Array

    VM_registerBuiltin(ctx, "array_length_1d", builtin_array_length_1d);
    VM_registerBuiltin(ctx, "array_length_2d", builtin_array_length_2d);
    VM_registerBuiltin(ctx, "array_length", builtin_array_length_1d); // GM:S 2 alias for array_length_1d
    VM_registerBuiltin(ctx, "array_height_2d", builtin_array_height_2d);
    VM_registerBuiltin(ctx, "array_get", builtin_array_get);
    VM_registerBuiltin(ctx, "array_set", builtin_array_set);
    VM_registerBuiltin(ctx, "array_push", builtin_array_push);
    VM_registerBuiltin(ctx, "array_pop", builtin_array_pop);
    VM_registerBuiltin(ctx, "array_resize", builtin_array_resize);
    VM_registerBuiltin(ctx, "array_delete", builtin_array_delete);
    VM_registerBuiltin(ctx, "array_insert", builtin_array_insert);
    VM_registerBuiltin(ctx, "array_create", builtin_array_create);

    // Steam stubs
    VM_registerBuiltin(ctx, "steam_initialised", builtin_steam_initialised);
    VM_registerBuiltin(ctx, "steam_stats_ready", builtin_steam_stats_ready);
    VM_registerBuiltin(ctx, "steam_file_exists", builtin_steam_file_exists);
    VM_registerBuiltin(ctx, "steam_file_write", builtin_steam_file_write);
    VM_registerBuiltin(ctx, "steam_file_read", builtin_steam_file_read);
    VM_registerBuiltin(ctx, "steam_get_persona_name", builtin_steam_get_persona_name);

    // Audio
    VM_registerBuiltin(ctx, "audio_system_is_available", builtin_audio_system_is_available);
    VM_registerBuiltin(ctx, "audio_exists", builtin_audio_exists);
    VM_registerBuiltin(ctx, "audio_channel_num", builtin_audio_channel_num);
    VM_registerBuiltin(ctx, "audio_play_sound", builtin_audio_play_sound);
    VM_registerBuiltin(ctx, "audio_stop_sound", builtin_audio_stop_sound);
    VM_registerBuiltin(ctx, "audio_stop_all", builtin_audio_stop_all);
    VM_registerBuiltin(ctx, "audio_is_playing", builtin_audio_is_playing);
    VM_registerBuiltin(ctx, "audio_is_paused", builtin_audio_is_paused);
    VM_registerBuiltin(ctx, "audio_sound_length", builtin_audio_sound_length);
    VM_registerBuiltin(ctx, "audio_sound_gain", builtin_audio_sound_gain);
    VM_registerBuiltin(ctx, "audio_sound_pitch", builtin_audio_sound_pitch);
    VM_registerBuiltin(ctx, "audio_sound_get_gain", builtin_audio_sound_get_gain);
    VM_registerBuiltin(ctx, "audio_sound_get_pitch", builtin_audio_sound_get_pitch);
    VM_registerBuiltin(ctx, "audio_master_gain", builtin_audio_master_gain);
    VM_registerBuiltin(ctx, "audio_group_load", builtin_audio_group_load);
    VM_registerBuiltin(ctx, "audio_group_is_loaded", builtin_audio_group_is_loaded);
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "audio_play_music", builtin_audio_play_music);
        VM_registerBuiltin(ctx, "audio_stop_music", builtin_audio_stop_music);
        VM_registerBuiltin(ctx, "audio_music_gain", builtin_audio_music_gain);
        VM_registerBuiltin(ctx, "audio_music_is_playing", builtin_audio_music_is_playing);
    }
    VM_registerBuiltin(ctx, "audio_pause_sound", builtin_audio_pause_sound);
    VM_registerBuiltin(ctx, "audio_resume_sound", builtin_audio_resume_sound);
    VM_registerBuiltin(ctx, "audio_pause_all", builtin_audio_pause_all);
    VM_registerBuiltin(ctx, "audio_resume_all", builtin_audio_resume_all);
    VM_registerBuiltin(ctx, "audio_sound_get_track_position", builtin_audio_sound_get_track_position);
    VM_registerBuiltin(ctx, "audio_sound_set_track_position", builtin_audio_sound_set_track_position);
    VM_registerBuiltin(ctx, "audio_create_stream", builtin_audio_create_stream);
    VM_registerBuiltin(ctx, "audio_destroy_stream", builtin_audio_destroy_stream);
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "action_sound", builtin_action_sound);
        VM_registerBuiltin(ctx, "action_end_sound", builtin_audio_stop_sound);
        VM_registerBuiltin(ctx, "action_if_sound", builtin_audio_is_playing);
        VM_registerBuiltin(ctx, "sound_play", builtin_sound_play);
        VM_registerBuiltin(ctx, "sound_loop", builtin_sound_loop);
        VM_registerBuiltin(ctx, "sound_volume", builtin_sound_volume);
        VM_registerBuiltin(ctx, "sound_exists", builtin_audio_exists); // Replaced with audio_exists in GMS2
        VM_registerBuiltin(ctx, "sound_fade", builtin_audio_sound_gain);
        VM_registerBuiltin(ctx, "sound_global_volume", builtin_audio_master_gain);
        VM_registerBuiltin(ctx, "sound_isplaying", builtin_audio_is_playing);
        VM_registerBuiltin(ctx, "sound_stop", builtin_audio_stop_sound);
        VM_registerBuiltin(ctx, "sound_stop_all", builtin_audio_stop_all);
    }
    // Application surface
    VM_registerBuiltin(ctx, "application_surface_enable", builtin_application_surface_enable);
    VM_registerBuiltin(ctx, "application_surface_draw_enable", builtin_application_surface_draw_enable);

    // Gamepad
    VM_registerBuiltin(ctx, "gamepad_get_device_count", builtin_gamepad_get_device_count);
    VM_registerBuiltin(ctx, "gamepad_is_connected", builtin_gamepad_is_connected);
    VM_registerBuiltin(ctx, "gamepad_button_check", builtin_gamepad_button_check);
    VM_registerBuiltin(ctx, "gamepad_button_check_pressed", builtin_gamepad_button_check_pressed);
    VM_registerBuiltin(ctx, "gamepad_button_check_released", builtin_gamepad_button_check_released);
    VM_registerBuiltin(ctx, "gamepad_axis_value", builtin_gamepad_axis_value);
    VM_registerBuiltin(ctx, "gamepad_get_description", builtin_gamepad_get_description);
    VM_registerBuiltin(ctx, "gamepad_button_value", builtin_gamepad_button_value);
    VM_registerBuiltin(ctx, "gamepad_is_supported", builtin_gamepad_is_supported);
    VM_registerBuiltin(ctx, "gamepad_get_guid", builtin_gamepad_get_guid);
    VM_registerBuiltin(ctx, "gamepad_get_button_threshold", builtin_gamepad_get_button_threshold);
    VM_registerBuiltin(ctx, "gamepad_set_button_threshold", builtin_gamepad_set_button_threshold);
    VM_registerBuiltin(ctx, "gamepad_get_axis_deadzone", builtin_gamepad_get_axis_deadzone);
    VM_registerBuiltin(ctx, "gamepad_set_axis_deadzone", builtin_gamepad_set_axis_deadzone);
    VM_registerBuiltin(ctx, "gamepad_axis_count", builtin_gamepad_axis_count);
    VM_registerBuiltin(ctx, "gamepad_button_count", builtin_gamepad_button_count);
    VM_registerBuiltin(ctx, "gamepad_hat_count", builtin_gamepad_hat_count);
    VM_registerBuiltin(ctx, "gamepad_hat_value", builtin_gamepad_hat_value);

    // INI
    VM_registerBuiltin(ctx, "ini_open", builtin_ini_open);
    VM_registerBuiltin(ctx, "ini_open_from_string", builtin_ini_open_from_string);
    VM_registerBuiltin(ctx, "ini_close", builtin_ini_close);
    VM_registerBuiltin(ctx, "ini_write_real", builtin_ini_write_real);
    VM_registerBuiltin(ctx, "ini_write_string", builtin_ini_write_string);
    VM_registerBuiltin(ctx, "ini_read_string", builtin_ini_read_string);
    VM_registerBuiltin(ctx, "ini_read_real", builtin_ini_read_real);
    VM_registerBuiltin(ctx, "ini_section_exists", builtin_ini_section_exists);

    // Directory
    VM_registerBuiltin(ctx, "directory_exists", builtin_directory_exists);
    VM_registerBuiltin(ctx, "directory_create", builtin_directory_create);
    VM_registerBuiltin(ctx, "directory_destroy", builtin_directory_destroy);

    // File
    VM_registerBuiltin(ctx, "file_exists", builtin_file_exists);
    VM_registerBuiltin(ctx, "file_text_open_write", builtin_file_text_open_write);
    VM_registerBuiltin(ctx, "file_text_open_read", builtin_file_text_open_read);
    VM_registerBuiltin(ctx, "file_text_close", builtin_file_text_close);
    VM_registerBuiltin(ctx, "file_text_write_string", builtin_file_text_write_string);
    VM_registerBuiltin(ctx, "file_text_writeln", builtin_file_text_writeln);
    VM_registerBuiltin(ctx, "file_text_write_real", builtin_file_text_write_real);
    VM_registerBuiltin(ctx, "file_text_eof", builtin_file_text_eof);
    VM_registerBuiltin(ctx, "file_delete", builtin_file_delete);
    VM_registerBuiltin(ctx, "file_find_first", builtin_file_find_first);
    VM_registerBuiltin(ctx, "file_find_next", builtin_file_find_next);
    VM_registerBuiltin(ctx, "file_find_close", builtin_file_find_close);
    VM_registerBuiltin(ctx, "file_text_read_string", builtin_file_text_read_string);
    VM_registerBuiltin(ctx, "file_text_read_real", builtin_file_text_read_real);
    VM_registerBuiltin(ctx, "file_text_readln", builtin_file_text_readln);
    VM_registerBuiltin(ctx, "file_bin_open", builtin_file_bin_open);
    VM_registerBuiltin(ctx, "file_bin_close", builtin_file_bin_close);
    VM_registerBuiltin(ctx, "file_bin_position", builtin_file_bin_position);
    VM_registerBuiltin(ctx, "file_bin_size", builtin_file_bin_size);
    VM_registerBuiltin(ctx, "file_bin_seek", builtin_file_bin_seek);
    VM_registerBuiltin(ctx, "file_bin_read_byte", builtin_file_bin_read_byte);
    VM_registerBuiltin(ctx, "file_bin_write_byte", builtin_file_bin_write_byte);
    VM_registerBuiltin(ctx, "file_bin_rewrite", builtin_file_bin_rewrite);

    // Keyboard
    VM_registerBuiltin(ctx, "keyboard_check", builtin_keyboard_check);
    VM_registerBuiltin(ctx, "keyboard_check_pressed", builtin_keyboard_check_pressed);
    VM_registerBuiltin(ctx, "keyboard_check_released", builtin_keyboard_check_released);
    VM_registerBuiltin(ctx, "keyboard_check_direct", builtin_keyboard_check_direct);
    VM_registerBuiltin(ctx, "keyboard_key_press", builtin_keyboard_key_press);
    VM_registerBuiltin(ctx, "keyboard_key_release", builtin_keyboard_key_release);
    VM_registerBuiltin(ctx, "keyboard_clear", builtin_keyboard_clear);
    VM_registerBuiltin(ctx, "keyboard_set_map", builtin_keyboard_set_map);
    VM_registerBuiltin(ctx, "keyboard_get_map", builtin_keyboard_get_map);
    VM_registerBuiltin(ctx, "keyboard_unset_map", builtin_keyboard_unset_map);

    // Mouse
    VM_registerBuiltin(ctx, "mouse_check_button", builtinMouseCheckButton);
    VM_registerBuiltin(ctx, "mouse_check_button_pressed", builtinMouseCheckButtonPressed);
    VM_registerBuiltin(ctx, "mouse_check_button_released", builtinMouseCheckButtonReleased);
    VM_registerBuiltin(ctx, "mouse_clear", builtinMouseClear);
    VM_registerBuiltin(ctx, "mouse_wheel_up", builtinMouseWheelUp);
    VM_registerBuiltin(ctx, "mouse_wheel_down", builtinMouseWheelDown);

    // Joystick
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "joystick_exists", builtin_joystick_exists);
        VM_registerBuiltin(ctx, "joystick_name", builtin_joystick_name);
        VM_registerBuiltin(ctx, "joystick_axes", builtin_joystick_axes);
        VM_registerBuiltin(ctx, "joystick_xpos", builtin_joystick_xpos);
        VM_registerBuiltin(ctx, "joystick_ypos", builtin_joystick_ypos);
        VM_registerBuiltin(ctx, "joystick_direction", builtin_joystick_direction);
        VM_registerBuiltin(ctx, "joystick_pov", builtin_joystick_pov);
        VM_registerBuiltin(ctx, "joystick_check_button", builtin_joystick_check_button);
        VM_registerBuiltin(ctx, "joystick_has_pov", builtin_joystick_has_pov);
        VM_registerBuiltin(ctx, "joystick_buttons", builtin_joystick_buttons);
    }

    // Window
    VM_registerBuiltin(ctx, "window_get_fullscreen", builtin_window_get_fullscreen);
    VM_registerBuiltin(ctx, "window_set_fullscreen", builtin_window_set_fullscreen);
    VM_registerBuiltin(ctx, "window_set_caption", builtin_window_set_caption);
    VM_registerBuiltin(ctx, "window_get_width", builtin_window_get_width);
    VM_registerBuiltin(ctx, "window_get_height", builtin_window_get_height);
    VM_registerBuiltin(ctx, "window_set_size", builtin_window_set_size);
    VM_registerBuiltin(ctx, "window_center", builtin_window_center);
    VM_registerBuiltin(ctx, "window_has_focus", builtin_window_has_focus);
    VM_registerBuiltin(ctx, "window_set_cursor", builtin_window_set_cursor);
    VM_registerBuiltin(ctx, "window_get_cursor", builtin_window_get_cursor);

    // Game
    VM_registerBuiltin(ctx, "game_restart", builtin_game_restart);
    VM_registerBuiltin(ctx, "game_end", builtin_game_end);
    VM_registerBuiltin(ctx, "game_save", builtin_game_save);
    VM_registerBuiltin(ctx, "game_load", builtin_game_load);

    // Instance
    VM_registerBuiltin(ctx, "instance_exists", builtin_instance_exists);
    VM_registerBuiltin(ctx, "instance_number", builtin_instance_number);
    VM_registerBuiltin(ctx, "instance_find", builtin_instance_find);
    VM_registerBuiltin(ctx, "instance_nearest", builtin_instance_nearest);
    VM_registerBuiltin(ctx, "instance_destroy", builtin_instance_destroy);
    if(!isGMS2) {
        VM_registerBuiltin(ctx, "instance_create", builtin_instance_create);
    }
    else {
        VM_registerBuiltin(ctx, "instance_create_depth", builtin_instance_create_depth);
        VM_registerBuiltin(ctx, "instance_create_layer", builtin_instance_create_layer);
    }
    VM_registerBuiltin(ctx, "instance_copy", builtin_instance_copy);
    VM_registerBuiltin(ctx, "instance_change", builtin_instance_change);
    VM_registerBuiltin(ctx, "instance_deactivate_all", builtin_instance_deactivate_all);
    VM_registerBuiltin(ctx, "instance_activate_all", builtin_instance_activate_all);
    VM_registerBuiltin(ctx, "instance_activate_object", builtin_instance_activate_object);
    VM_registerBuiltin(ctx, "instance_deactivate_object", builtin_instance_deactivate_object);
    VM_registerBuiltin(ctx, "instance_activate_region", builtin_instance_activate_region);
    VM_registerBuiltin(ctx, "instance_deactivate_region", builtin_instance_deactivate_region);
    VM_registerBuiltin(ctx, "instance_activate_layer", builtin_instance_activate_layer);
    VM_registerBuiltin(ctx, "instance_deactivate_layer", builtin_instance_deactivate_layer);
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "action_kill_object", builtin_action_kill_object);
        VM_registerBuiltin(ctx, "action_create_object", builtin_action_create_object);
        VM_registerBuiltin(ctx, "action_set_relative", builtin_action_set_relative);
        VM_registerBuiltin(ctx, "action_move", builtin_action_move);
        VM_registerBuiltin(ctx, "action_move_to", builtin_action_move_to);
        VM_registerBuiltin(ctx, "action_move_start", builtin_action_move_start);
        VM_registerBuiltin(ctx, "action_potential_step", builtin_action_potential_step);
        VM_registerBuiltin(ctx, "action_bounce", builtin_action_bounce);
        VM_registerBuiltin(ctx, "action_move_contact", builtin_action_move_contact);
        VM_registerBuiltin(ctx, "action_snap", builtin_action_snap);
        VM_registerBuiltin(ctx, "action_set_friction", builtin_action_set_friction);
        VM_registerBuiltin(ctx, "action_set_gravity", builtin_action_set_gravity);
        VM_registerBuiltin(ctx, "action_set_hspeed", builtin_action_set_hspeed);
        VM_registerBuiltin(ctx, "action_set_vspeed", builtin_action_set_vspeed);
        VM_registerBuiltin(ctx, "action_inherited", builtin_event_inherited);
        VM_registerBuiltin(ctx, "action_timeline_start", builtin_action_timeline_start);
        VM_registerBuiltin(ctx, "action_timeline_pause", builtin_action_timeline_pause);
        VM_registerBuiltin(ctx, "action_timeline_stop", builtin_action_timeline_stop);
        VM_registerBuiltin(ctx, "action_set_timeline_position", builtin_action_set_timeline_position);
        VM_registerBuiltin(ctx, "action_set_timeline_speed", builtin_action_set_timeline_speed);
        VM_registerBuiltin(ctx, "action_set_timeline", builtin_action_set_timeline);
        VM_registerBuiltin(ctx, "action_timeline_set", builtin_action_timeline_set);
    }
    VM_registerBuiltin(ctx, "event_inherited", builtin_event_inherited);
    VM_registerBuiltin(ctx, "event_user", builtin_event_user);
    VM_registerBuiltin(ctx, "event_perform", builtin_event_perform);

    // Buffer
    VM_registerBuiltin(ctx, "buffer_create", builtin_buffer_create);
    VM_registerBuiltin(ctx, "buffer_delete", builtin_buffer_delete);
    VM_registerBuiltin(ctx, "buffer_write", builtin_buffer_write);
    VM_registerBuiltin(ctx, "buffer_read", builtin_buffer_read);
    VM_registerBuiltin(ctx, "buffer_seek", builtin_buffer_seek);
    VM_registerBuiltin(ctx, "buffer_tell", builtin_buffer_tell);
    VM_registerBuiltin(ctx, "buffer_get_size", builtin_buffer_get_size);
    VM_registerBuiltin(ctx, "buffer_load", builtin_buffer_load);
    VM_registerBuiltin(ctx, "buffer_save", builtin_buffer_save);
    VM_registerBuiltin(ctx, "buffer_save_ext", builtin_buffer_save_ext);
    VM_registerBuiltin(ctx, "buffer_load_async", builtin_buffer_load_async);
    VM_registerBuiltin(ctx, "buffer_save_async", builtin_buffer_save_async);
    VM_registerBuiltin(ctx, "buffer_async_group_begin", builtin_buffer_async_group_begin);
    VM_registerBuiltin(ctx, "buffer_async_group_end", builtin_buffer_async_group_end);
    VM_registerBuiltin(ctx, "filename_change_ext", builtin_filename_change_ext);
    VM_registerBuiltin(ctx, "buffer_base64_encode", builtin_buffer_base64_encode);
    VM_registerBuiltin(ctx, "buffer_base64_decode", builtin_buffer_base64_decode);
    VM_registerBuiltin(ctx, "base64_encode", builtin_base64_encode);
    VM_registerBuiltin(ctx, "base64_decode", builtin_base64_decode);
    VM_registerBuiltin(ctx, "buffer_md5", builtin_buffer_md5);
    VM_registerBuiltin(ctx, "buffer_sha1", builtin_buffer_sha1);
    VM_registerBuiltin(ctx, "buffer_get_surface", builtin_buffer_get_surface);

    // PSN
    VM_registerBuiltin(ctx, "psn_init", builtin_psn_init);
    VM_registerBuiltin(ctx, "psn_init_np_libs", builtin_psn_init_np_libs);
    VM_registerBuiltin(ctx, "psn_default_user", builtin_psn_default_user);
    VM_registerBuiltin(ctx, "psn_get_leaderboard_score", builtin_psn_get_leaderboard_score);
    VM_registerBuiltin(ctx, "psn_setup_trophies", builtin_psn_setup_trophies);

    // Draw
    VM_registerBuiltin(ctx, "draw_sprite", builtin_draw_sprite);
    VM_registerBuiltin(ctx, "draw_sprite_ext", builtin_draw_sprite_ext);
    VM_registerBuiltin(ctx, "draw_sprite_tiled", builtin_draw_sprite_tiled);
    VM_registerBuiltin(ctx, "draw_sprite_tiled_ext", builtin_draw_sprite_tiled_ext);
    VM_registerBuiltin(ctx, "draw_sprite_stretched", builtin_draw_sprite_stretched);
    VM_registerBuiltin(ctx, "draw_sprite_stretched_ext", builtin_draw_sprite_stretched_ext);
    VM_registerBuiltin(ctx, "draw_sprite_part", builtin_draw_sprite_part);
    VM_registerBuiltin(ctx, "draw_sprite_part_ext", builtin_draw_sprite_part_ext);
    VM_registerBuiltin(ctx, "draw_sprite_general", builtin_draw_sprite_general);
    VM_registerBuiltin(ctx, "draw_sprite_pos", builtin_draw_sprite_pos);
    VM_registerBuiltin(ctx, "draw_rectangle", builtin_draw_rectangle);
    VM_registerBuiltin(ctx, "draw_rectangle_color", builtin_draw_rectangle_color);
    VM_registerBuiltin(ctx, "draw_rectangle_colour", builtin_draw_rectangle_color);
    VM_registerBuiltin(ctx, "draw_healthbar", builtin_draw_healthbar);
    VM_registerBuiltin(ctx, "draw_set_color", builtin_draw_set_color);
    VM_registerBuiltin(ctx, "draw_set_alpha", builtin_draw_set_alpha);
    VM_registerBuiltin(ctx, "draw_clear", builtin_draw_clear);
    VM_registerBuiltin(ctx, "draw_clear_alpha", builtin_draw_clear_alpha);
    VM_registerBuiltin(ctx, "draw_set_font", builtin_draw_set_font);
    VM_registerBuiltin(ctx, "draw_set_halign", builtin_draw_set_halign);
    VM_registerBuiltin(ctx, "draw_set_valign", builtin_draw_set_valign);
    VM_registerBuiltin(ctx, "draw_text", builtin_draw_text);
    VM_registerBuiltin(ctx, "draw_text_transformed", builtin_draw_text_transformed);
    VM_registerBuiltin(ctx, "draw_text_ext", builtin_draw_text_ext);
    VM_registerBuiltin(ctx, "draw_text_ext_color", builtin_draw_text_color_ext);
    VM_registerBuiltin(ctx, "draw_text_ext_transformed", builtin_draw_text_ext_transformed);
    VM_registerBuiltin(ctx, "draw_text_color", builtin_draw_text_color);
    VM_registerBuiltin(ctx, "draw_text_transformed_color", builtin_draw_text_color_transformed);
    VM_registerBuiltin(ctx, "draw_text_ext_transformed_color", builtin_draw_text_color_ext_transformed);
    VM_registerBuiltin(ctx, "draw_text_colour", builtin_draw_text_color);
    VM_registerBuiltin(ctx, "draw_text_ext_colour", builtin_draw_text_color_ext);
    VM_registerBuiltin(ctx, "draw_text_transformed_colour", builtin_draw_text_color_transformed);
    VM_registerBuiltin(ctx, "draw_text_ext_transformed_colour", builtin_draw_text_color_ext_transformed);
    VM_registerBuiltin(ctx, "draw_surface", builtin_draw_surface);
    VM_registerBuiltin(ctx, "draw_surface_ext", builtin_draw_surface_ext);
    VM_registerBuiltin(ctx, "draw_surface_part", builtin_draw_surface_part);
    VM_registerBuiltin(ctx, "draw_surface_part_ext", builtin_draw_surface_part_ext);
    VM_registerBuiltin(ctx, "draw_surface_stretched", builtin_draw_surface_stretched);
    VM_registerBuiltin(ctx, "draw_surface_stretched_ext", builtin_draw_surface_stretched_ext);
    if(!isGMS2) {
        VM_registerBuiltin(ctx, "draw_background", builtin_draw_background);
        VM_registerBuiltin(ctx, "draw_background_ext", builtin_draw_background_ext);
        VM_registerBuiltin(ctx, "draw_background_stretched", builtin_draw_background_stretched);
        VM_registerBuiltin(ctx, "draw_background_part_ext", builtin_draw_background_part_ext);
        VM_registerBuiltin(ctx, "draw_background_tiled", builtin_draw_background_tiled);
        VM_registerBuiltin(ctx, "draw_background_tiled_ext", builtin_draw_background_tiled_ext);
        VM_registerBuiltin(ctx, "background_get_width", builtin_background_get_width);
        VM_registerBuiltin(ctx, "background_get_height", builtin_background_get_height);
        VM_registerBuiltin(ctx, "background_delete", builtin_sprite_delete);
        VM_registerBuiltin(ctx, "background_exists", builtin_sprite_exists);
        VM_registerBuiltin(ctx, "background_get_name", builtin_sprite_get_name);
        VM_registerBuiltin(ctx, "background_name", builtin_sprite_get_name);
    }
    VM_registerBuiltin(ctx, "draw_self", builtin_draw_self);
    VM_registerBuiltin(ctx, "draw_line", builtin_draw_line);
    VM_registerBuiltin(ctx, "draw_line_colour", builtin_draw_line_colour);
    VM_registerBuiltin(ctx, "draw_line_color", builtin_draw_line_colour); // alt-spelling (used in Undertale)
    VM_registerBuiltin(ctx, "draw_line_width", builtin_draw_line_width);
    VM_registerBuiltin(ctx, "draw_line_width_colour", builtin_draw_line_width_colour);
    VM_registerBuiltin(ctx, "draw_line_width_color", builtin_draw_line_width_colour);
    VM_registerBuiltin(ctx, "draw_triangle", builtin_draw_triangle);
    VM_registerBuiltin(ctx, "draw_triangle_colour", builtin_draw_triangle_color);
    VM_registerBuiltin(ctx, "draw_triangle_color", builtin_draw_triangle_color);
    VM_registerBuiltin(ctx, "draw_circle", builtin_draw_circle);
    VM_registerBuiltin(ctx, "draw_circle_colour", builtin_draw_circle_color);
    VM_registerBuiltin(ctx, "draw_circle_color", builtin_draw_circle_color);
    VM_registerBuiltin(ctx, "draw_ellipse", builtin_draw_ellipse);
    VM_registerBuiltin(ctx, "draw_ellipse_colour", builtin_draw_ellipse_color);
    VM_registerBuiltin(ctx, "draw_ellipse_color", builtin_draw_ellipse_color);
    VM_registerBuiltin(ctx, "draw_set_circle_precision", builtin_draw_set_circle_precision);
    VM_registerBuiltin(ctx, "draw_get_circle_precision", builtin_draw_get_circle_precision);
    VM_registerBuiltin(ctx, "draw_set_colour", builtin_draw_set_colour);
    VM_registerBuiltin(ctx, "draw_get_colour", builtin_draw_get_colour);
    VM_registerBuiltin(ctx, "draw_get_color", builtin_draw_get_color);
    VM_registerBuiltin(ctx, "draw_get_alpha", builtin_draw_get_alpha);
    VM_registerBuiltin(ctx, "draw_get_font", builtin_draw_get_font);

    // Motion
    VM_registerBuiltin(ctx, "motion_add", builtin_motion_add);

    // Color
    VM_registerBuiltin(ctx, "merge_color", builtin_merge_color);
    VM_registerBuiltin(ctx, "merge_colour", builtin_merge_color);

    // Surface
    VM_registerBuiltin(ctx, "surface_create", builtin_surface_create);
    VM_registerBuiltin(ctx, "surface_free", builtin_surface_free);
    VM_registerBuiltin(ctx, "surface_set_target", builtin_surface_set_target);
    VM_registerBuiltin(ctx, "surface_reset_target", builtin_surface_reset_target);
    VM_registerBuiltin(ctx, "surface_get_target", builtin_surface_get_target);
    VM_registerBuiltin(ctx, "surface_exists", builtin_surface_exists);
    VM_registerBuiltin(ctx, "surface_get_width", builtin_surface_get_width);
    VM_registerBuiltin(ctx, "surface_get_height", builtin_surface_get_height);
    VM_registerBuiltin(ctx, "surface_get_texture", builtin_surface_get_texture);
    VM_registerBuiltin(ctx, "surface_resize", builtin_surface_resize);
    VM_registerBuiltin(ctx, "surface_copy", builtin_surface_copy);
    VM_registerBuiltin(ctx, "surface_copy_part", builtin_surface_copy_part);

    // Sprite info
    VM_registerBuiltin(ctx, "sprite_add", builtin_sprite_add);
    VM_registerBuiltin(ctx, "sprite_exists", builtin_sprite_exists);
    VM_registerBuiltin(ctx, "sprite_get_width", builtin_sprite_get_width);
    VM_registerBuiltin(ctx, "sprite_get_height", builtin_sprite_get_height);
    VM_registerBuiltin(ctx, "sprite_get_number", builtin_sprite_get_number);
    VM_registerBuiltin(ctx, "sprite_get_xoffset", builtin_sprite_get_xoffset);
    VM_registerBuiltin(ctx, "sprite_get_yoffset", builtin_sprite_get_yoffset);
    VM_registerBuiltin(ctx, "sprite_get_name", builtin_sprite_get_name);
    VM_registerBuiltin(ctx, "sprite_get_bbox_left", builtin_sprite_get_bbox_left);
    VM_registerBuiltin(ctx, "sprite_get_bbox_right", builtin_sprite_get_bbox_right);
    VM_registerBuiltin(ctx, "sprite_get_bbox_top", builtin_sprite_get_bbox_top);
    VM_registerBuiltin(ctx, "sprite_get_bbox_bottom", builtin_sprite_get_bbox_bottom);
    VM_registerBuiltin(ctx, "sprite_set_offset", builtin_sprite_set_offset);
    VM_registerBuiltin(ctx, "sprite_create_from_surface", builtin_sprite_create_from_surface);
    VM_registerBuiltin(ctx, "sprite_delete", builtin_sprite_delete);

    // Text measurement
    VM_registerBuiltin(ctx, "string_width", builtin_string_width);
    VM_registerBuiltin(ctx, "string_height", builtin_string_height);
    VM_registerBuiltin(ctx, "string_width_ext", builtin_string_width_ext);
    VM_registerBuiltin(ctx, "string_height_ext", builtin_string_height_ext);

    // Color
    VM_registerBuiltin(ctx, "make_color_rgb", builtin_make_color_rgb);
    VM_registerBuiltin(ctx, "make_colour_rgb", builtin_make_colour_rgb);
    VM_registerBuiltin(ctx, "make_color_hsv", builtin_make_color_hsv);
    VM_registerBuiltin(ctx, "make_colour_hsv", builtin_make_colour_hsv);
    VM_registerBuiltin(ctx, "color_get_red", builtin_color_get_red);
    VM_registerBuiltin(ctx, "colour_get_red", builtin_color_get_red);
    VM_registerBuiltin(ctx, "color_get_green", builtin_color_get_green);
    VM_registerBuiltin(ctx, "colour_get_green", builtin_color_get_green);
    VM_registerBuiltin(ctx, "color_get_blue", builtin_color_get_blue);
    VM_registerBuiltin(ctx, "colour_get_blue", builtin_color_get_blue);
    VM_registerBuiltin(ctx, "color_get_hue", builtin_color_get_hue);
    VM_registerBuiltin(ctx, "colour_get_hue", builtin_color_get_hue);
    VM_registerBuiltin(ctx, "color_get_saturation", builtin_color_get_saturation);
    VM_registerBuiltin(ctx, "colour_get_saturation", builtin_color_get_saturation);
    VM_registerBuiltin(ctx, "color_get_value", builtin_color_get_value);
    VM_registerBuiltin(ctx, "colour_get_value", builtin_color_get_value);

    // Display
    VM_registerBuiltin(ctx, "display_get_width", builtin_display_get_width);
    VM_registerBuiltin(ctx, "display_get_height", builtin_display_get_height);
    VM_registerBuiltin(ctx, "display_get_gui_width", builtin_display_get_gui_width);
    VM_registerBuiltin(ctx, "display_get_gui_height", builtin_display_get_gui_height);
    VM_registerBuiltin(ctx, "display_set_gui_size", builtin_display_set_gui_size);
    VM_registerBuiltin(ctx, "display_set_gui_maximise", builtin_display_set_gui_maximise);
    VM_registerBuiltin(ctx, "display_set_gui_maximize", builtin_display_set_gui_maximise);

    // Devices
    VM_registerBuiltin(ctx, "device_mouse_check_button", builtinDeviceMouseCheckButton);
    VM_registerBuiltin(ctx, "device_mouse_x", builtinDeviceMouseX);
    VM_registerBuiltin(ctx, "device_mouse_y", builtinDeviceMouseY);
    VM_registerBuiltin(ctx, "device_mouse_x_to_gui", builtinDeviceMouseXToGui);
    VM_registerBuiltin(ctx, "device_mouse_y_to_gui", builtinDeviceMouseYToGui);

    // Collision
    VM_registerBuiltin(ctx, "place_meeting", builtin_place_meeting);
    VM_registerBuiltin(ctx, "collision_rectangle", builtin_collision_rectangle);
    VM_registerBuiltin(ctx, "collision_rectangle_list", builtin_collision_rectangle_list);
    VM_registerBuiltin(ctx, "rectangle_in_rectangle", builtin_rectangle_in_rectangle);
    VM_registerBuiltin(ctx, "collision_line", builtin_collision_line);
    VM_registerBuiltin(ctx, "collision_point", builtin_collision_point);
    VM_registerBuiltin(ctx, "collision_circle", builtin_collision_circle);
    VM_registerBuiltin(ctx, "instance_place", builtin_instance_place);
    VM_registerBuiltin(ctx, "instance_place_list", builtin_instance_place_list);
    VM_registerBuiltin(ctx, "instance_position", builtin_instance_position);
    VM_registerBuiltin(ctx, "position_meeting", builtin_position_meeting);
    VM_registerBuiltin(ctx, "place_free", builtin_place_free);
    VM_registerBuiltin(ctx, "place_empty", builtin_place_empty);

    // Motion planning
    VM_registerBuiltin(ctx, "mp_linear_step", builtin_mp_linear_step);
    VM_registerBuiltin(ctx, "mp_linear_step_object", builtin_mp_linear_step_object);
    VM_registerBuiltin(ctx, "mp_potential_step", builtin_mp_potential_step);
    VM_registerBuiltin(ctx, "mp_potential_step_object", builtin_mp_potential_step_object);
    VM_registerBuiltin(ctx, "mp_potential_settings", builtin_mp_potential_settings);

    // Tile layers (GM:S 1.x)
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "tile_layer_hide", builtin_tile_layer_hide);
        VM_registerBuiltin(ctx, "tile_layer_show", builtin_tile_layer_show);
        VM_registerBuiltin(ctx, "tile_layer_shift", builtin_tile_layer_shift);
        VM_registerBuiltin(ctx, "tile_add", builtin_tile_add);
        VM_registerBuiltin(ctx, "tile_exists", builtin_tile_exists);
        VM_registerBuiltin(ctx, "tile_layer_find", builtin_tile_layer_find);
        VM_registerBuiltin(ctx, "tile_layer_delete", builtin_tile_layer_delete);
        VM_registerBuiltin(ctx, "tile_delete", builtin_tile_delete);
        VM_registerBuiltin(ctx, "tile_get_ids_at_depth", builtin_tile_get_ids_at_depth);
        VM_registerBuiltin(ctx, "tile_set_alpha", builtin_tile_set_alpha);
        VM_registerBuiltin(ctx, "tile_set_visible", builtin_layer_tile_visible);
    }

    // Layer
    VM_registerBuiltin(ctx, "layer_force_draw_depth", builtin_layer_force_draw_depth);
    VM_registerBuiltin(ctx, "layer_is_draw_depth_forced", builtin_layer_is_draw_depth_forced);
    VM_registerBuiltin(ctx, "layer_get_forced_depth", builtin_layer_get_forced_depth);
    VM_registerBuiltin(ctx, "layer_get_id", builtin_layer_get_id);
    VM_registerBuiltin(ctx, "layer_exists", builtin_layer_exists);
    VM_registerBuiltin(ctx, "layer_get_name", builtin_layer_get_name);
    VM_registerBuiltin(ctx, "layer_get_depth", builtin_layer_get_depth);
    VM_registerBuiltin(ctx, "layer_depth", builtin_layer_depth);
    VM_registerBuiltin(ctx, "layer_get_visible", builtin_layer_get_visible);
    VM_registerBuiltin(ctx, "layer_set_visible", builtin_layer_set_visible);
    VM_registerBuiltin(ctx, "layer_get_x", builtin_layer_get_x);
    VM_registerBuiltin(ctx, "layer_x", builtin_layer_x);
    VM_registerBuiltin(ctx, "layer_get_y", builtin_layer_get_y);
    VM_registerBuiltin(ctx, "layer_y", builtin_layer_y);
    VM_registerBuiltin(ctx, "layer_get_hspeed", builtin_layer_get_hspeed);
    VM_registerBuiltin(ctx, "layer_hspeed", builtin_layer_hspeed);
    VM_registerBuiltin(ctx, "layer_get_vspeed", builtin_layer_get_vspeed);
    VM_registerBuiltin(ctx, "layer_vspeed", builtin_layer_vspeed);
#if IS_WAD17_OR_HIGHER_ENABLED
    VM_registerBuiltin(ctx, "layer_get_all", builtin_layer_get_all);
    VM_registerBuiltin(ctx, "layer_get_all_elements", builtin_layer_get_all_elements);
    VM_registerBuiltin(ctx, "layer_instance_get_instance", builtin_layer_instance_get_instance);
#endif
    VM_registerBuiltin(ctx, "layer_get_element_type", builtin_layer_get_element_type);
    VM_registerBuiltin(ctx, "layer_sprite_get_sprite", builtin_layer_sprite_get_sprite);
    VM_registerBuiltin(ctx, "layer_sprite_get_x", builtin_layer_sprite_get_x);
    VM_registerBuiltin(ctx, "layer_sprite_get_y", builtin_layer_sprite_get_y);
    VM_registerBuiltin(ctx, "layer_sprite_get_xscale", builtin_layer_sprite_get_xscale);
    VM_registerBuiltin(ctx, "layer_sprite_get_yscale", builtin_layer_sprite_get_yscale);
    VM_registerBuiltin(ctx, "layer_sprite_get_speed", builtin_layer_sprite_get_speed);
    VM_registerBuiltin(ctx, "layer_sprite_get_index", builtin_layer_sprite_get_index);
    VM_registerBuiltin(ctx, "layer_sprite_get_angle", builtin_layer_sprite_get_angle);
    VM_registerBuiltin(ctx, "layer_sprite_get_alpha", builtin_layer_sprite_get_alpha);
    VM_registerBuiltin(ctx, "layer_sprite_get_blend", builtin_layer_sprite_get_blend);
    VM_registerBuiltin(ctx, "layer_sprite_destroy", builtin_layer_sprite_destroy);
    VM_registerBuiltin(ctx, "layer_tile_visible", builtin_layer_tile_visible);
#if IS_WAD17_OR_HIGHER_ENABLED
    VM_registerBuiltin(ctx, "layer_get_id_at_depth", builtin_layer_get_id_at_depth);
    VM_registerBuiltin(ctx, "layer_tilemap_get_id", builtin_layer_tilemap_get_id);
    VM_registerBuiltin(ctx, "draw_tilemap", builtin_draw_tilemap);
    VM_registerBuiltin(ctx, "tilemap_x", builtin_tilemap_x);
    VM_registerBuiltin(ctx, "tilemap_y", builtin_tilemap_y);
    VM_registerBuiltin(ctx, "tilemap_get_x", builtin_tilemap_get_x);
    VM_registerBuiltin(ctx, "tilemap_get_y", builtin_tilemap_get_y);
    VM_registerBuiltin(ctx, "tilemap_get_at_pixel", builtin_tilemap_get_at_pixel);
    VM_registerBuiltin(ctx, "tilemap_get_tileset", builtin_tilemap_get_tileset);
    VM_registerBuiltin(ctx, "tile_get_index", builtin_tile_get_index);
    VM_registerBuiltin(ctx, "tile_set_empty", builtin_tile_set_empty);
    VM_registerBuiltin(ctx, "tilemap_set_at_pixel", builtin_tilemap_set_at_pixel);
#endif
    VM_registerBuiltin(ctx, "layer_create", builtin_layer_create);
    VM_registerBuiltin(ctx, "layer_destroy", builtin_layer_destroy);
    VM_registerBuiltin(ctx, "layer_background_create", builtin_layer_background_create);
    VM_registerBuiltin(ctx, "layer_background_exists", builtin_layer_background_exists);
    VM_registerBuiltin(ctx, "layer_background_visible", builtin_layer_background_visible);
    VM_registerBuiltin(ctx, "layer_background_htiled", builtin_layer_background_htiled);
    VM_registerBuiltin(ctx, "layer_background_vtiled", builtin_layer_background_vtiled);
    VM_registerBuiltin(ctx, "layer_background_xscale", builtin_layer_background_xscale);
    VM_registerBuiltin(ctx, "layer_background_yscale", builtin_layer_background_yscale);
    VM_registerBuiltin(ctx, "layer_background_stretch", builtin_layer_background_stretch);
    VM_registerBuiltin(ctx, "layer_background_blend", builtin_layer_background_blend);
    VM_registerBuiltin(ctx, "layer_background_alpha", builtin_layer_background_alpha);
    VM_registerBuiltin(ctx, "layer_background_sprite", builtin_layer_background_sprite);
    VM_registerBuiltin(ctx, "layer_background_change", builtin_layer_background_sprite);
    VM_registerBuiltin(ctx, "layer_background_get_id", builtin_layer_background_get_id);
    VM_registerBuiltin(ctx, "layer_background_index", builtin_layer_background_index);
    VM_registerBuiltin(ctx, "layer_tile_alpha", builtin_layer_tile_alpha);
    VM_registerBuiltin(ctx, "layer_tile_x", builtin_layer_tile_x);
    VM_registerBuiltin(ctx, "layer_tile_y", builtin_layer_tile_y);
    VM_registerBuiltin(ctx, "layer_tile_get_x", builtin_layer_tile_get_x);
    VM_registerBuiltin(ctx, "layer_tile_get_y", builtin_layer_tile_get_y);
    VM_registerBuiltin(ctx, "layer_tile_get_xscale", builtin_layer_tile_get_xscale);
    VM_registerBuiltin(ctx, "layer_tile_get_yscale", builtin_layer_tile_get_yscale);
    VM_registerBuiltin(ctx, "layer_tile_get_region", builtin_layer_tile_get_region);
    VM_registerBuiltin(ctx, "layer_background_destroy", builtin_layer_background_destroy);

    // GMS2 internal
    VM_registerBuiltin(ctx, "@@NewGMLArray@@", builtin_NewGMLArray);
    VM_registerBuiltin(ctx, "@@This@@", builtin_This);
    VM_registerBuiltin(ctx, "@@Other@@", builtin_Other);
    VM_registerBuiltin(ctx, "@@Global@@", builtin_Global);
#if IS_WAD17_OR_HIGHER_ENABLED
    VM_registerBuiltin(ctx, "@@NullObject@@", builtin_NullObject);
    VM_registerBuiltin(ctx, "@@NewGMLObject@@", builtin_NewGMLObject);
    VM_registerBuiltin(ctx, "@@CopyStatic@@", builtin_CopyStatic);
    VM_registerBuiltin(ctx, "@@SetStatic@@", builtin_SetStatic);
    VM_registerBuiltin(ctx, "@@GetInstance@@", builtin_GetInstance);
#endif

    // Path
    VM_registerBuiltin(ctx, "path_start", builtin_path_start);
    VM_registerBuiltin(ctx, "path_end", builtin_path_end);
    VM_registerBuiltin(ctx, "path_get_length", builtin_path_get_length);
    VM_registerBuiltin(ctx, "path_get_point_x", builtin_path_get_point_x);
    VM_registerBuiltin(ctx, "path_get_point_y", builtin_path_get_point_y);
    VM_registerBuiltin(ctx, "path_get_point_speed", builtin_path_get_point_speed);
    VM_registerBuiltin(ctx, "path_get_x", builtin_path_get_x);
    VM_registerBuiltin(ctx, "path_get_y", builtin_path_get_y);
    VM_registerBuiltin(ctx, "path_get_speed", builtin_path_get_speed);
    VM_registerBuiltin(ctx, "path_get_kind", builtin_path_get_kind);
    VM_registerBuiltin(ctx, "path_get_closed", builtin_path_get_closed);
    VM_registerBuiltin(ctx, "path_get_precision", builtin_path_get_precision);
    VM_registerBuiltin(ctx, "path_get_number", builtin_path_get_number);
    VM_registerBuiltin(ctx, "path_set_kind", builtin_path_set_kind);
    VM_registerBuiltin(ctx, "path_set_closed", builtin_path_set_closed);
    VM_registerBuiltin(ctx, "path_set_precision", builtin_path_set_precision);
    VM_registerBuiltin(ctx, "path_add", builtin_path_add);
    VM_registerBuiltin(ctx, "path_clear_points", builtin_path_clear_points);
    VM_registerBuiltin(ctx, "path_add_point", builtin_path_add_point);
    VM_registerBuiltin(ctx, "path_exists", builtin_path_exists);
    VM_registerBuiltin(ctx, "path_delete", builtin_path_delete);

    // Timeline
    VM_registerBuiltin(ctx, "timeline_exists", builtin_timeline_exists);
    VM_registerBuiltin(ctx, "timeline_get_name", builtin_timeline_get_name);
    VM_registerBuiltin(ctx, "timeline_max_moment", builtin_timeline_max_moment);
    VM_registerBuiltin(ctx, "timeline_size", builtin_timeline_size);

    // Animation curves
    VM_registerBuiltin(ctx, "animcurve_get", builtin_animcurve_get);
    VM_registerBuiltin(ctx, "animcurve_get_channel", builtin_animcurve_get_channel);
    VM_registerBuiltin(ctx, "animcurve_get_channel_index", builtin_animcurve_get_channel_index);
    VM_registerBuiltin(ctx, "animcurve_channel_evaluate", builtin_animcurve_channel_evaluate);

    // Motion planning grid
    VM_registerBuiltin(ctx, "mp_grid_create", builtin_mp_grid_create);
    VM_registerBuiltin(ctx, "mp_grid_destroy", builtin_mp_grid_destroy);
    VM_registerBuiltin(ctx, "mp_grid_clear_all", builtin_mp_grid_clear_all);
    VM_registerBuiltin(ctx, "mp_grid_add_cell", builtin_mp_grid_add_cell);
    VM_registerBuiltin(ctx, "mp_grid_clear_cell", builtin_mp_grid_clear_cell);
    VM_registerBuiltin(ctx, "mp_grid_add_rectangle", builtin_mp_grid_add_rectangle);
    VM_registerBuiltin(ctx, "mp_grid_clear_rectangle", builtin_mp_grid_clear_rectangle);
    VM_registerBuiltin(ctx, "mp_grid_get_cell", builtin_mp_grid_get_cell);
    VM_registerBuiltin(ctx, "mp_grid_draw", builtin_mp_grid_draw);
    VM_registerBuiltin(ctx, "mp_grid_path", builtin_mp_grid_path);

    // Misc
    VM_registerBuiltin(ctx, "get_timer", builtin_get_timer);
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "action_if_variable", builtin_action_if_variable);
        VM_registerBuiltin(ctx, "action_if", builtin_action_if);
        VM_registerBuiltin(ctx, "action_if_dice", builtin_action_if_dice);
        VM_registerBuiltin(ctx, "action_set_alarm", builtin_action_set_alarm);
        VM_registerBuiltin(ctx, "action_set_score", builtin_action_set_score);
        VM_registerBuiltin(ctx, "action_if_score", builtin_action_if_score);
        VM_registerBuiltin(ctx, "action_draw_score", builtin_action_draw_score);
        VM_registerBuiltin(ctx, "action_set_life", builtin_action_set_life);
        VM_registerBuiltin(ctx, "action_if_life", builtin_action_if_life);
        VM_registerBuiltin(ctx, "action_draw_life", builtin_action_draw_life);
        VM_registerBuiltin(ctx, "action_draw_life_images", builtin_action_draw_life_images);
        VM_registerBuiltin(ctx, "action_set_health", builtin_action_set_health);
        VM_registerBuiltin(ctx, "action_if_health", builtin_action_if_health);
        VM_registerBuiltin(ctx, "action_if_aligned", builtin_action_if_aligned);
        VM_registerBuiltin(ctx, "action_if_collision", builtin_action_if_collision);
        VM_registerBuiltin(ctx, "action_if_empty", builtin_action_if_empty);
        VM_registerBuiltin(ctx, "action_if_object", builtin_action_if_object);
        VM_registerBuiltin(ctx, "action_if_number", builtin_action_if_number);
        VM_registerBuiltin(ctx, "action_if_next_room", builtin_action_if_next_room);
        VM_registerBuiltin(ctx, "action_if_previous_room", builtin_action_if_previous_room);
        VM_registerBuiltin(ctx, "action_if_mouse", builtin_action_if_mouse);
        VM_registerBuiltin(ctx, "action_if_question", builtin_action_if_question);
        VM_registerBuiltin(ctx, "action_draw_health", builtin_action_draw_health);
        VM_registerBuiltin(ctx, "action_sprite_set", builtin_action_sprite_set);
        VM_registerBuiltin(ctx, "action_sprite_color", builtin_action_sprite_color);
        VM_registerBuiltin(ctx, "action_sprite_colour", builtin_action_sprite_color);
        VM_registerBuiltin(ctx, "action_message", builtin_action_message);
        VM_registerBuiltin(ctx, "action_another_room", builtin_action_another_room);
        VM_registerBuiltin(ctx, "action_current_room", builtin_action_current_room);
        VM_registerBuiltin(ctx, "action_next_room", builtin_action_next_room);
        VM_registerBuiltin(ctx, "action_reverse_xdir", builtin_action_reverse_xdir);
        VM_registerBuiltin(ctx, "action_reverse_ydir", builtin_action_reverse_ydir);
        VM_registerBuiltin(ctx, "action_color", builtin_action_color);
        VM_registerBuiltin(ctx, "action_colour", builtin_action_color);
        VM_registerBuiltin(ctx, "action_font", builtin_action_font);
        VM_registerBuiltin(ctx, "action_draw_text", builtin_action_draw_text);
        VM_registerBuiltin(ctx, "action_draw_sprite", builtin_action_draw_sprite);
        VM_registerBuiltin(ctx, "action_draw_variable", builtin_action_draw_variable);
        VM_registerBuiltin(ctx, "action_change_object", builtin_instance_change);
        VM_registerBuiltin(ctx, "action_end_game", builtin_game_end);
        VM_registerBuiltin(ctx, "action_execute_script", builtin_script_execute); //It its right? i think
        VM_registerBuiltin(ctx, "action_load_game", builtin_game_load);
        VM_registerBuiltin(ctx, "action_path", builtin_path_start);
        VM_registerBuiltin(ctx, "action_path_end", builtin_path_end);
        VM_registerBuiltin(ctx, "action_previous_room", builtin_room_goto_previous);
        VM_registerBuiltin(ctx, "action_restart_game", builtin_game_restart);
        VM_registerBuiltin(ctx, "action_save_game", builtin_game_save);
    }
    VM_registerBuiltin(ctx, "alarm_set", builtin_alarm_set);
    VM_registerBuiltin(ctx, "alarm_get", builtin_alarm_get);
    VM_registerBuiltin(ctx, "string_hash_to_newline", builtin_string_hash_to_newline);
    VM_registerBuiltin(ctx, "json_decode", builtin_json_decode);
    VM_registerBuiltin(ctx, "json_encode", builtin_json_encode);
    VM_registerBuiltin(ctx, "font_add_sprite", builtin_font_add_sprite);
    VM_registerBuiltin(ctx, "font_add_sprite_ext", builtin_font_add_sprite_ext);
    VM_registerBuiltin(ctx, "font_get_name", builtin_font_get_name);
    VM_registerBuiltin(ctx, "font_get_info", builtin_font_get_info);
    VM_registerBuiltin(ctx, "object_exists", builtin_object_exists);
    VM_registerBuiltin(ctx, "object_get_name", builtin_object_get_name);
    VM_registerBuiltin(ctx, "object_name", builtin_object_get_name); // alias for pre-GMS 2.3
    VM_registerBuiltin(ctx, "object_get_parent", builtin_object_get_parent);
    VM_registerBuiltin(ctx, "object_get_persistent", builtin_object_get_persistent);
    VM_registerBuiltin(ctx, "object_get_solid", builtin_object_get_solid);
    VM_registerBuiltin(ctx, "object_get_sprite", builtin_object_get_sprite);
    VM_registerBuiltin(ctx, "object_set_parent", builtin_object_set_parent);
    VM_registerBuiltin(ctx, "object_set_persistent", builtin_object_set_persistent);
    VM_registerBuiltin(ctx, "object_set_solid", builtin_object_set_solid);
    VM_registerBuiltin(ctx, "object_set_sprite", builtin_object_set_sprite);
    VM_registerBuiltin(ctx, "object_set_visible", builtin_object_set_visible);
    if (!isGMS2) {
        VM_registerBuiltin(ctx, "object_get_depth", builtin_object_get_depth);
        VM_registerBuiltin(ctx, "object_set_depth", builtin_object_set_depth);
    }
    VM_registerBuiltin(ctx, "object_is_ancestor", builtin_object_is_ancestor);
    VM_registerBuiltin(ctx, "asset_get_index", builtin_asset_get_index);
    VM_registerBuiltin(ctx,"gpu_set_blendmode", builtin_gpu_set_blendmode);
    VM_registerBuiltin(ctx,"gpu_set_blendmode_ext", builtin_gpu_set_blendmode_ext);
    VM_registerBuiltin(ctx,"gpu_set_blendenable", builtin_gpu_set_blendenable);
    VM_registerBuiltin(ctx,"gpu_get_blendenable", builtin_gpu_get_blendenable);
    VM_registerBuiltin(ctx,"gpu_set_alphatestenable", builtin_gpu_set_alphatestenable);
    VM_registerBuiltin(ctx,"gpu_set_alphatestref", builtin_gpu_set_alphatestref);
    VM_registerBuiltin(ctx,"gpu_set_colorwriteenable", builtin_gpu_set_colorwriteenable);
    VM_registerBuiltin(ctx,"gpu_get_colorwriteenable", builtin_gpu_get_colorwriteenable);
    VM_registerBuiltin(ctx,"gpu_set_fog", builtin_gpu_set_fog);
    if (!isGMS2) {
        VM_registerBuiltin(ctx,"draw_set_blend_mode", builtin_gpu_set_blendmode);
        VM_registerBuiltin(ctx,"draw_set_blend_mode_ext", builtin_gpu_set_blendmode_ext);
        VM_registerBuiltin(ctx,"d3d_set_fog", builtin_gpu_set_fog);
        VM_registerBuiltin(ctx, "draw_enable_alphablend", builtin_gpu_set_blendenable);
        VM_registerBuiltin(ctx, "draw_set_alpha_test", builtin_gpu_set_alphatestenable);
        VM_registerBuiltin(ctx, "draw_set_alpha_test_ref_value", builtin_gpu_set_alphatestref);
        VM_registerBuiltin(ctx, "draw_set_color_write_enable", builtin_gpu_set_colorwriteenable);
        VM_registerBuiltin(ctx, "draw_set_colour_write_enable", builtin_gpu_set_colorwriteenable);
    }
    VM_registerBuiltin(ctx, "game_change", builtin_game_change);
    VM_registerBuiltin(ctx, "parameter_count", builtin_parameter_count);
    VM_registerBuiltin(ctx, "parameter_string", builtin_parameter_string);
    VM_registerBuiltin(ctx, "shader_set", builtin_shader_set);
    VM_registerBuiltin(ctx, "shader_reset", builtin_shader_reset);
    VM_registerBuiltin(ctx, "shader_current", builtin_shader_current);
    VM_registerBuiltin(ctx, "shader_is_compiled", builtin_shader_is_compiled); 
    VM_registerBuiltin(ctx, "shader_get_name", builtin_shader_get_name);
    VM_registerBuiltin(ctx, "shaders_are_supported", builtin_shaders_are_supported);  
    VM_registerBuiltin(ctx, "shader_get_uniform", builtin_shader_get_uniform);
    VM_registerBuiltin(ctx, "shader_get_sampler_index", builtin_shader_get_sampler_index);
    VM_registerBuiltin(ctx, "shader_set_uniform_f", builtin_shader_set_uniformF);
    VM_registerBuiltin(ctx, "shader_set_uniform_f_array", builtin_shader_set_uniform_f_array);
    VM_registerBuiltin(ctx, "shader_set_uniform_i", builtin_shader_set_uniformI);
    VM_registerBuiltin(ctx, "sprite_get_uvs", builtin_sprite_get_uvs);
    VM_registerBuiltin(ctx, "sprite_get_texture", builtin_sprite_get_texture);
    VM_registerBuiltin(ctx, "font_get_uvs", builtin_font_get_uvs);   
    VM_registerBuiltin(ctx, "texture_get_texel_width", builtin_texture_get_texel_width);
    VM_registerBuiltin(ctx, "texture_get_texel_height", builtin_texture_get_texel_height);
    VM_registerBuiltin(ctx, "texture_get_uvs", builtin_texture_get_uvs);
    VM_registerBuiltin(ctx, "texture_set_stage", builtin_texture_set_stage);
}
