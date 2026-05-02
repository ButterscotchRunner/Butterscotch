#pragma once

#include <gsKit.h>
#include <gsToolkit.h>

#include "ps2/gs_renderer.h"
#include "runner.h"
#include "profiler.h"

// Maximum number of chunk stats we track (24 chunks in data.win, but only some have interesting counts)
#define MAX_CHUNK_STATS 24

typedef struct {
    char label[16];
    uint32_t count;
} ChunkStat;

typedef struct {
    ChunkStat stats[MAX_CHUNK_STATS];
    int statCount;
} LoadingScreenState;

typedef enum {
    STATS_ENABLED = 0,
    STATS_ENABLED_WITH_PROFILER = 1,
    STATS_DISABLED = 2,
    STATS_MAX
} DebugOverlayState;

typedef struct {
    DebugOverlayState state;
    GSGLOBAL* gsGlobal;
    GSFONTM* gsFontm;
    int memorySize;
    int heapCeiling;
    int profilerFramesInWindow;
    LoadingScreenState loadingState;
#ifdef ENABLE_VM_GML_PROFILER
    char profilerOverlayText[4096];
#endif
} PS2Overlay;

// Note that the caller is expected to own the `gsGlobal` object.
void PS2Overlay_init(GSGLOBAL* gsGlobal, int memorySize, int heapCeiling);
void PS2Overlay_deinit();

DebugOverlayState PS2Overlay_getDebugOverlayState();
void PS2Overlay_setDebugOverlayState(DebugOverlayState state, Runner* runner);
void PS2Overlay_toggleDebugOverlay(Runner* runner);

PS2Overlay* PS2Overlay_getCallbackData();
void PS2Overlay_statusScreenCallback(const char* chunkName, int chunkIndex, int totalChunks, DataWin* dataWin, void* userData);

void PS2Overlay_drawStatusScreen(const char* gameName, const char* statusText, bool includeChunkStats);
void PS2Overlay_drawDebugOverlay(const Renderer* renderer, const Runner* runner, float tick, float step, float draw, float audio, bool speedCapRemoved);
