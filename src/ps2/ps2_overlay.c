#include "ps2/ps2_overlay.h"

#include <malloc.h>
#include <stdio.h>
#include <unistd.h>

#include "ps2/ps2_utils.h"

// ===[ Loading Screen ]===

static const int PROFILER_WINDOW_FRAMES = 60;
static bool gPS2OverlayInitialized = false;
static PS2Overlay gOverlay = { 0 };

// Draws chunk item counts in the top-left corner (if any stats have been recorded)
static void drawChunkStats(GSGLOBAL* gs, GSFONTM* fontm, LoadingScreenState* loadingState) {
    if (!loadingState || loadingState->statCount == 0)
        return;

    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);
    fontm->Align = GSKIT_FALIGN_LEFT;
    float statsY = 10.0f;
    float statsScale = 0.35f;
    float statsLineHeight = 14.0f;
    char statLine[32];

    repeat(loadingState->statCount, i) {
        snprintf(statLine, sizeof(statLine), "%d %s", loadingState->stats[i].count, loadingState->stats[i].label);
        gsKit_fontm_print_scaled(gs, fontm, 10.0f, statsY, 1, statsScale, gray, statLine);
        statsY += statsLineHeight;
    }
}

// Draws a simple status screen with "Butterscotch" title, optional game name, and a status message (no progress bar)
// gameName can be nullptr if the game name is not yet known
// Begins a status screen: clears, draws title + optional game name, leaves center align active
static void beginStatusScreen(GSGLOBAL* gs, GSFONTM* fontm, const char* gameName) {
    gsKit_clear(gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));

    u64 title = GS_SETREG_RGBAQ(0x5E, 0x54, 0x92, 0x80, 0x00);
    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);

    fontm->Align = GSKIT_FALIGN_CENTER;
    gsKit_fontm_print_scaled(gs, fontm, 320.0f, 180.0f, 1, 0.8f, title, "Butterscotch");
    if (gameName) {
        gsKit_fontm_print_scaled(gs, fontm, 320.0f, 210.0f, 1, 0.5f, gray, gameName);
    }
}

// Draws the bottom-left credits text (shared between status screen and loading screen)
static void drawCreditsText(GSGLOBAL* gs, GSFONTM* fontm) {
    u64 darkGray = GS_SETREG_RGBAQ(0x70, 0x70, 0x70, 0x80, 0x00);
    float creditsScale = 0.4f;
    float lineHeight = 26.0f * creditsScale;
    float creditsY = 448.0f - 10.0f - lineHeight * 2.0f;

    char versionText[128];
    snprintf(versionText, sizeof(versionText), "Butterscotch (%s) [%s]", BUTTERSCOTCH_COMMIT_HASH, BUTTERSCOTCH_COMMIT_DATE);
    gsKit_fontm_print_scaled(gs, fontm, 10.0f, creditsY, 1, creditsScale, darkGray, versionText);
    gsKit_fontm_print_scaled(gs, fontm, 10.0f, creditsY + lineHeight, 1, creditsScale, darkGray, "Created by MrPowerGamerBR (https://mrpowergamerbr.com/)");
}

// Ends a status screen: draws credits, resets align, flips
static void endStatusScreen(GSGLOBAL* gs, GSFONTM* fontm) {
    fontm->Align = GSKIT_FALIGN_LEFT;
    drawCreditsText(gs, fontm);
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
}

void PS2Overlay_init(GSGLOBAL* gsGlobal, int memorySize, int heapCeiling) {
    if (gPS2OverlayInitialized) return;

    gOverlay.gsGlobal = gsGlobal;
    gOverlay.memorySize = memorySize;
    gOverlay.heapCeiling = heapCeiling;
    gOverlay.state = STATS_DISABLED;
    gOverlay.profilerFramesInWindow = 0;

    gOverlay.gsFontm = gsKit_init_fontm();
    gsKit_fontm_upload(gOverlay.gsGlobal, gOverlay.gsFontm);
    gOverlay.gsFontm->Spacing = 0.95f;

    gPS2OverlayInitialized = true;
}

void PS2Overlay_deinit() {
    if (!gPS2OverlayInitialized) return;

    gsKit_free_fontm(gOverlay.gsGlobal, gOverlay.gsFontm);

    memset(&gOverlay, 0, sizeof(gOverlay));
    gPS2OverlayInitialized = false;
}

DebugOverlayState PS2Overlay_getDebugOverlayState() {
    if (!gPS2OverlayInitialized) return STATS_DISABLED;
    return gOverlay.state;
}

void PS2Overlay_setDebugOverlayState(DebugOverlayState state, Runner* runner) {
    if (!gPS2OverlayInitialized) return;
    gOverlay.state = state;

#ifdef ENABLE_VM_GML_PROFILER
    Profiler_setEnabled(&runner->vmContext->profiler, PS2Overlay_getDebugOverlayState() == STATS_ENABLED_WITH_PROFILER);
    gOverlay.profilerFramesInWindow = 0;
    gOverlay.profilerOverlayText[0] = '\0';
#endif
}

void PS2Overlay_toggleDebugOverlay(Runner* runner) {
    if (!gPS2OverlayInitialized) return;
    gOverlay.state = (gOverlay.state + 1) % STATS_MAX;

#ifdef ENABLE_VM_GML_PROFILER
    Profiler_setEnabled(&runner->vmContext->profiler, PS2Overlay_getDebugOverlayState() == STATS_ENABLED_WITH_PROFILER);
    gOverlay.profilerFramesInWindow = 0;
    gOverlay.profilerOverlayText[0] = '\0';
#endif
}

PS2Overlay* PS2Overlay_getCallbackData() {
    return &gOverlay;
}

void PS2Overlay_statusScreenCallback(const char* chunkName, int chunkIndex, int totalChunks, DataWin* dataWin, void* userData) {
    if (!gPS2OverlayInitialized) return;

    PS2Overlay* data = (PS2Overlay*) userData;
    LoadingScreenState* state = &data->loadingState;
    GSGLOBAL* gs = data->gsGlobal;
    GSFONTM* fontm = data->gsFontm;

    const char* gameName = dataWin->gen8.displayName ? dataWin->gen8.displayName : "Unknown Game";
    beginStatusScreen(gs, fontm, gameName);

    // Loading bar
    u64 white = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
    u64 barBg = GS_SETREG_RGBAQ(0x40, 0x40, 0x40, 0x80, 0x00);
    u64 barFg = GS_SETREG_RGBAQ(0xFF, 0xCC, 0x00, 0x80, 0x00); // Butterscotch yellow

    float barX = 120.0f;
    float barY = 300.0f;
    float barW = 400.0f;
    float barH = 20.0f;
    float progress = (float) (chunkIndex + 1) / (float) totalChunks;

    // Bar background (dark gray)
    gsKit_prim_sprite(gs, barX, barY, barX + barW, barY + barH, 1, barBg);

    // Bar fill (butterscotch yellow)
    float fillW = barW * progress;
    if (fillW > 1.0f) {
        gsKit_prim_sprite(gs, barX, barY, barX + fillW, barY + barH, 1, barFg);
    }

    // Enable alpha blending so the font text doesn't have a black box behind it
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    // Percentage text centered on the bar
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", (int) (progress * 100));
    gsKit_fontm_print_scaled(gs, fontm, 320.0f, barY + 4.5f, 1, 0.4f, white, percentText);

    // Chunk name text below the bar
    char statusText[32];
    snprintf(statusText, sizeof(statusText), "Loading %.4s... (%d/%d)", chunkName, chunkIndex + 1, totalChunks);
    gsKit_fontm_print_scaled(gs, fontm, 320.0f, barY + barH + 10.0f, 1, 0.5f, white, statusText);

    // Memory usage below the status text
    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);
    void* heapTop = sbrk(0);
    int32_t usedBytes = (int32_t) (uintptr_t) heapTop;
    char memText[48];
    snprintf(memText, sizeof(memText), "Memory: %.1f/%.1f MB", (double) (usedBytes / (1024.0f * 1024.0f)), (double) (gOverlay.memorySize / (1024.0f * 1024.0f)));
    gsKit_fontm_print_scaled(gs, fontm, 320.0f, barY + barH + 30.0f, 1, 0.4f, gray, memText);

    // Record item counts for already-parsed chunks (callback fires before parsing, so we scan all counts each time and add any newly non-zero ones in the order they appear)
    typedef struct { uint32_t* countPtr; const char* label; } CountSource;
    CountSource sources[] = {
        { &dataWin->sond.count, "sounds" },
        { &dataWin->sprt.count, "sprites" },
        { &dataWin->bgnd.count, "backgrounds" },
        { &dataWin->font.count, "fonts" },
        { &dataWin->objt.count, "objects" },
        { &dataWin->room.count, "rooms" },
        { &dataWin->code.count, "code entries" },
        { &dataWin->txtr.count, "textures" },
    };

    // sizeof(sources) = size of the ENTIRE array
    // So, if we divide the size of the ENTIRE array by the size of a SINGLE entry, we get the number of entries
    int arrayLength = sizeof(sources) / sizeof(CountSource);

    repeat(arrayLength, i) {
        if (*sources[i].countPtr == 0)
            continue;

        // Check if we already recorded this label
        bool found = false;
        forEach(CountSource, stat, sources, state->statCount) {
            if (strcmp(stat->label, sources[i].label) == 0) {
                found = true;
                break;
            }
        }

        if (!found && MAX_CHUNK_STATS > state->statCount) {
            ChunkStat* stat = &state->stats[state->statCount++];
            snprintf(stat->label, sizeof(stat->label), "%s", sources[i].label);
            stat->count = *sources[i].countPtr;
        }
    }

    drawChunkStats(gs, fontm, state);

    gs->PrimAlphaEnable = GS_SETTING_OFF;

    endStatusScreen(gs, fontm);
}

void PS2Overlay_drawStatusScreen(const char* gameName, const char* statusText, bool includeChunkStats) {
    if (!gPS2OverlayInitialized) return;

    beginStatusScreen(gOverlay.gsGlobal, gOverlay.gsFontm, gameName);
    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);
    gsKit_fontm_print_scaled(gOverlay.gsGlobal, gOverlay.gsFontm, 320.0f, 300.0f, 1, 0.5f, gray, statusText);
    if (includeChunkStats) {
        drawChunkStats(gOverlay.gsGlobal, gOverlay.gsFontm, &gOverlay.loadingState);
    }
    endStatusScreen(gOverlay.gsGlobal, gOverlay.gsFontm);
}

void PS2Overlay_drawDebugOverlay(const Renderer* renderer, const Runner* runner, float tick, float step, float draw, float audio, bool speedCapRemoved) {
    if (!gPS2OverlayInitialized) return;
    if (gOverlay.state == STATS_DISABLED) return;

    u64 debugColor = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
    char debugText[512];
    uint32_t vramFreeBytes = GS_VRAM_SIZE - gOverlay.gsGlobal->CurrentPointer;

    // Count atlases loaded in VRAM and EE RAM cache
    const GsRenderer* gsRenderer = (GsRenderer*) renderer;
    uint32_t vramAtlasCount = 0;
    uint32_t eeramAtlasCount = 0;
    repeat(gsRenderer->atlasCount, ai) {
        if (gsRenderer->atlasToChunk[ai] >= 0) vramAtlasCount++;
        if (gsRenderer->eeCacheEntries[ai].atlasId >= 0) eeramAtlasCount++;
    }

    int freeBytes = gOverlay.heapCeiling - mallinfo().uordblks;

    const char* roomName = runner->currentRoom != nullptr && runner->currentRoom->name != nullptr ? runner->currentRoom->name : "?";

    const char* thrashIndicator = "";
    if (gsRenderer->chunksNeededThisFrame > gsRenderer->chunkCount) {
        thrashIndicator = gsRenderer->diskLoadsThisFrame > 0 ? " [RAM+DISK THRASHING]" : " [RAM THRASHING]";
    } else if (gsRenderer->diskLoadsThisFrame > 0) {
        thrashIndicator = " [DISK LOAD]";
    }

    snprintf(debugText, sizeof(debugText), "Room: %s\nTick: %.2fms\nStep: %.2fms\nDraw: %.2fms\nAudio: %.2fms\nFree: %d bytes\nVRAM Free: %lu bytes\nRoom Speed: %u%s\nAtlas: (%u, %u, %u) [%u/%u]%s\nInstances: %d\nStructs: %d", roomName, (double) tick, (double) step, (double) draw, (double) audio, freeBytes, (unsigned long) vramFreeBytes, runner->currentRoom->speed, speedCapRemoved ? " [UNCAPPED]" : "", vramAtlasCount, eeramAtlasCount, gsRenderer->atlasCount, gsRenderer->chunksNeededThisFrame, gsRenderer->chunkCount, thrashIndicator, (int) arrlen(runner->instances), (int) arrlen(runner->structInstances));
    gsKit_fontm_print_scaled(gOverlay.gsGlobal, gOverlay.gsFontm, 10.0f, 10.0f, 10, 0.6f, debugColor, debugText);

    if (gOverlay.state == STATS_ENABLED_WITH_PROFILER) {
        float profilerY = 10.0f + (15.6f * 10.0f) + 6.0f;

#ifdef ENABLE_VM_GML_PROFILER
        gOverlay.profilerFramesInWindow++;
        if (gOverlay.profilerFramesInWindow >= PROFILER_WINDOW_FRAMES) {
            char* profilerReport = Profiler_createReport(runner->vmContext->profiler, 25, gOverlay.profilerFramesInWindow);
            if (profilerReport != nullptr) {
                snprintf(gOverlay.profilerOverlayText, sizeof(gOverlay.profilerOverlayText), "%s", profilerReport);
                free(profilerReport);
            }
            Profiler_reset(runner->vmContext->profiler);
            gOverlay.profilerFramesInWindow = 0;
        }
        const char* profilerDisplay = gOverlay.profilerOverlayText[0] != '\0' ? gOverlay.profilerOverlayText : "GML Profiler (collecting...)";
        gsKit_fontm_print_scaled(gOverlay.gsGlobal, gOverlay.gsFontm, 10.0f, profilerY, 10, 0.35f, debugColor, profilerDisplay);
#else
        gsKit_fontm_print_scaled(gOverlay.gsGlobal, gOverlay.gsFontm, 10.0f, profilerY, 10, 0.35f, debugColor, "Butterscotch GML Profiler is disabled on this build :(");
#endif
    }
}
