#include "ps2_overlay.h"

#include <malloc.h>
#include <stdio.h>
#include <unistd.h>

#include "ps2_utils.h"
#include "debug_font.h"

// ===[ Loading Screen ]===

static const int PROFILER_WINDOW_FRAMES = 60;
static bool gPS2OverlayInitialized = false;
static PS2Overlay gOverlay = { 0 };

static inline void overlayPrint(float x, float y, int z, float scale, uint64_t color, const char* text) {
    DebugFontRenderer_printScaled(gOverlay.font, x, y, z, scale, color, text);
}

// Draws chunk item counts in the top-left corner (if any stats have been recorded)
static void drawChunkStats(LoadingScreenState* loadingState) {
    if (!loadingState || loadingState->statCount == 0)
        return;

    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);
    gOverlay.font->align = GSKIT_FALIGN_LEFT;
    float statsY = 10.0f;
    float statsScale = 0.35f;
    float statsLineHeight = 14.0f;
    char statLine[32];

    repeat(loadingState->statCount, i) {
        snprintf(statLine, sizeof(statLine), "%d %s", loadingState->stats[i].count, loadingState->stats[i].label);
        overlayPrint(10.0f, statsY, 1, statsScale, gray, statLine);
        statsY += statsLineHeight;
    }
}

// Draws a simple status screen with "Butterscotch" title, optional game name, and a status message (no progress bar)
// gameName can be nullptr if the game name is not yet known
// Begins a status screen: clears, draws title + optional game name, leaves center align active.
static void beginStatusScreen(GSGLOBAL* gs, const char* gameName) {
    gsKit_clear(gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));

    u64 title = GS_SETREG_RGBAQ(0xE8 >> 1, 0xA5 >> 1, 0x52 >> 1, 0x80, 0x00);
    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);

    gOverlay.font->align = GSKIT_FALIGN_CENTER;
    overlayPrint(320.0f, 180.0f, 1, 0.8f, title, "Butterscotch");
    if (gameName) {
        overlayPrint(320.0f, 210.0f, 1, 0.5f, gray, gameName);
    }
}

// Draws the bottom-left credits text (shared between status screen and loading screen)
static void drawCreditsText(void) {
    u64 darkGray = GS_SETREG_RGBAQ(0x70, 0x70, 0x70, 0x80, 0x00);
    float creditsScale = 0.4f;
    float lineHeight = 26.0f * creditsScale;
    float creditsY = 448.0f - 10.0f - lineHeight * 2.0f;

    char versionText[128];
    snprintf(versionText, sizeof(versionText), "Butterscotch (%s) [%s]", BUTTERSCOTCH_COMMIT_HASH, BUTTERSCOTCH_COMMIT_DATE);
    overlayPrint(10.0f, creditsY, 1, creditsScale, darkGray, versionText);
    overlayPrint(10.0f, creditsY + lineHeight, 1, creditsScale, darkGray, "Created by MrPowerGamerBR (https://mrpowergamerbr.com/)");
}

// Ends a status screen: draws credits, resets align, flips
static void endStatusScreen(GSGLOBAL* gs) {
    gOverlay.font->align = GSKIT_FALIGN_LEFT;
    drawCreditsText();
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

    gOverlay.font = DebugFontRenderer_create(gsGlobal);
    gOverlay.font->spacing = 0.95f;
    // Soft black halo/outline so debug text stays legible over any background.
    gOverlay.font->outlineColor = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x10, 0x00);
    gOverlay.font->outlineRadius = 1.0f;

    gPS2OverlayInitialized = true;
}

void PS2Overlay_deinit() {
    if (!gPS2OverlayInitialized) return;

    DebugFontRenderer_destroy(gOverlay.font);

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

    const char* gameName = dataWin->gen8.displayName ? dataWin->gen8.displayName : "Unknown Game";
    beginStatusScreen(gs, gameName);

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

    // Percentage text centered on the bar
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", (int) (progress * 100));
    overlayPrint(320.0f, barY + 2.0f, 1, 0.4f, white, percentText);

    // Chunk name text below the bar
    char statusText[32];
    snprintf(statusText, sizeof(statusText), "Loading %.4s... (%d/%d)", chunkName, chunkIndex + 1, totalChunks);
    overlayPrint(320.0f, barY + barH + 10.0f, 1, 0.5f, white, statusText);

    // Memory usage below the status text
    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);
    void* heapTop = sbrk(0);
    int32_t usedBytes = (int32_t) (uintptr_t) heapTop;
    char memText[48];
    snprintf(memText, sizeof(memText), "Memory: %.1f/%.1f MB", (double) (usedBytes / (1024.0f * 1024.0f)), (double) (gOverlay.memorySize / (1024.0f * 1024.0f)));
    overlayPrint(320.0f, barY + barH + 30.0f, 1, 0.4f, gray, memText);

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

    drawChunkStats(state);

    endStatusScreen(gs);
}

void PS2Overlay_drawStatusScreen(const char* gameName, const char* statusText, bool includeChunkStats) {
    if (!gPS2OverlayInitialized) return;

    beginStatusScreen(gOverlay.gsGlobal, gameName);
    u64 gray = GS_SETREG_RGBAQ(0xAA, 0xAA, 0xAA, 0x80, 0x00);
    overlayPrint(320.0f, 300.0f, 1, 0.5f, gray, statusText);
    if (includeChunkStats) {
        drawChunkStats(&gOverlay.loadingState);
    }
    endStatusScreen(gOverlay.gsGlobal);
}

void PS2Overlay_drawDebugOverlay(const Renderer* renderer, const Runner* runner, float tick, float step, float draw, float audio, bool speedCapRemoved) {
    if (!gPS2OverlayInitialized) return;
    if (gOverlay.state == STATS_DISABLED) return;

    u64 debugColor = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
    char debugText[512];
    uint32_t vramFreeBytes = GS_VRAM_SIZE - gOverlay.gsGlobal->CurrentPointer;

    // Count atlases loaded in VRAM and EE RAM cache + bucket by size.
    const GsRenderer* gsRenderer = (GsRenderer*) renderer;
    uint32_t vramAtlasCount = 0;
    uint32_t eeramAtlasCount = 0;

    // Bucket distinct (width, height) pairs.
    typedef struct { uint16_t width; uint16_t height; uint16_t total; uint16_t resident; } AtlasSizeBucket;
    AtlasSizeBucket sizeBuckets[8] = { 0 };
    uint32_t sizeBucketCount = 0;

    repeat(gsRenderer->atlasCount, ai) {
        bool resident = gsRenderer->atlasToChunk[ai] >= 0;
        if (resident) vramAtlasCount++;
        if (gsRenderer->eeCacheEntries[ai].atlasId >= 0) eeramAtlasCount++;

        uint16_t aw = gsRenderer->atlasWidth[ai];
        uint16_t ah = gsRenderer->atlasHeight[ai];
        bool found = false;
        repeat(sizeBucketCount, bi) {
            if (sizeBuckets[bi].width == aw && sizeBuckets[bi].height == ah) {
                sizeBuckets[bi].total++;
                if (resident) sizeBuckets[bi].resident++;
                found = true;
                break;
            }
        }
        if (!found && sizeof(sizeBuckets) / sizeof(sizeBuckets[0]) > sizeBucketCount) {
            sizeBuckets[sizeBucketCount].width = aw;
            sizeBuckets[sizeBucketCount].height = ah;
            sizeBuckets[sizeBucketCount].total = 1;
            sizeBuckets[sizeBucketCount].resident = resident ? 1 : 0;
            sizeBucketCount++;
        }
    }

    // Sort buckets by (width, height) ascending so output is stable across frames
    repeat(sizeBucketCount, bi) {
        repeat(sizeBucketCount - bi - 1, bj) {
            uint32_t a = ((uint32_t) sizeBuckets[bj].width << 16) | sizeBuckets[bj].height;
            uint32_t b = ((uint32_t) sizeBuckets[bj + 1].width << 16) | sizeBuckets[bj + 1].height;
            if (b < a) {
                AtlasSizeBucket tmp = sizeBuckets[bj];
                sizeBuckets[bj] = sizeBuckets[bj + 1];
                sizeBuckets[bj + 1] = tmp;
            }
        }
    }

    char atlasSizeText[160];
    atlasSizeText[0] = '\0';
    int atlasSizeOffset = 0;
    repeat(sizeBucketCount, bi) {
        int written = snprintf(atlasSizeText + atlasSizeOffset, sizeof(atlasSizeText) - atlasSizeOffset, "\n  %ux%u: %u/%u", sizeBuckets[bi].width, sizeBuckets[bi].height, sizeBuckets[bi].resident, sizeBuckets[bi].total);
        if (written <= 0 || sizeof(atlasSizeText) - atlasSizeOffset <= (size_t) written)
            break;
        atlasSizeOffset += written;
    }

    int freeBytes = gOverlay.heapCeiling - mallinfo().uordblks;

    const char* roomName = runner->currentRoom != nullptr && runner->currentRoom->name != nullptr ? runner->currentRoom->name : "?";

    const char* thrashIndicator = "";
    if (gsRenderer->chunksNeededThisFrame > gsRenderer->chunkCount) {
        thrashIndicator = gsRenderer->diskLoadsThisFrame > 0 ? " [RAM+DISK THRASHING]" : " [RAM THRASHING]";
    } else if (gsRenderer->diskLoadsThisFrame > 0) {
        thrashIndicator = " [DISK LOAD]";
    }

    int pinned = 0;

    repeat(gsRenderer->chunkCount, i) {
        if (gsRenderer->chunks[i].snapshotIdx != -1 || gsRenderer->chunks[i].surfaceIdx != -1)
            pinned++;
    }

    snprintf(debugText, sizeof(debugText), "Room: %s\nTick: %.2fms\nStep: %.2fms\nDraw: %.2fms\nAudio: %.2fms\nFree: %d bytes\nVRAM Free: %lu bytes\nRoom Speed: %u%s\nAtlas: (%u, %u, %u) [%u/%u (%u pinned)]%s%s\nInstances: %d\nStructs: %d", roomName, (double) tick, (double) step, (double) draw, (double) audio, freeBytes, (unsigned long) vramFreeBytes, runner->currentRoom->speed, speedCapRemoved ? " [UNCAPPED]" : "", vramAtlasCount, eeramAtlasCount, gsRenderer->atlasCount, gsRenderer->chunksNeededThisFrame, gsRenderer->chunkCount, pinned, thrashIndicator, atlasSizeText, (int) arrlen(runner->instances), (int) arrlen(runner->structInstances));
    overlayPrint(10.0f, 10.0f, 10, 0.6f, debugColor, debugText);

    if (gOverlay.state == STATS_ENABLED_WITH_PROFILER) {
        float profilerY = 10.0f + (15.6f * (float) (10 + sizeBucketCount)) + 6.0f;

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
        overlayPrint(10.0f, profilerY, 10, 0.35f, debugColor, profilerDisplay);
#else
        overlayPrint(10.0f, profilerY, 10, 0.35f, debugColor, "Butterscotch GML Profiler is disabled on this build :(");
#endif
    }
}
