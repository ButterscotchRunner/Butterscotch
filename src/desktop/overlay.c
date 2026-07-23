#include "overlay.h"

#include "profiler.h"
#include "utils.h"

#include "stb_ds.h"
#include "stdio_compat.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DEBUGFONT_LINE_HEIGHT 43
#define OVERLAY_FONT_SCALE 0.5f
#define PROFILER_SCALE 0.4f
#define PROFILER_WINDOW_FRAMES 60

typedef struct {
    bool initialized;
    OverlayState state;
    int profilerFramesInWindow;
#ifdef ENABLE_VM_GML_PROFILER
    char profilerOverlayText[4096];
#endif
} Overlay;

static Overlay gOverlay = { 0 };

void Overlay_init(OverlayState initialState) {
    if (gOverlay.initialized) return;
    gOverlay.state = initialState;
    gOverlay.profilerFramesInWindow = 0;
    gOverlay.initialized = true;
}

void Overlay_deinit(void) {
    memset(&gOverlay, 0, sizeof(gOverlay));
}

OverlayState Overlay_getState(void) {
    if (!gOverlay.initialized) return OVERLAY_STATS_DISABLED;
    return gOverlay.state;
}

void Overlay_toggle(Runner* runner) {
    if (!gOverlay.initialized) return;
    gOverlay.state = (OverlayState)((gOverlay.state + 1) % OVERLAY_STATS_MAX);

#ifdef ENABLE_VM_GML_PROFILER
    Profiler_setEnabled(&runner->vmContext->profiler, gOverlay.state == OVERLAY_STATS_ENABLED_WITH_PROFILER);
    gOverlay.profilerFramesInWindow = 0;
    gOverlay.profilerOverlayText[0] = '\0';
#endif
}

static void drawOverlayText(Renderer* renderer, float x, float y, float scale, uint32_t color, float alpha, const char* text) {
    if (text == NULL) return;
    uint32_t prevColor = renderer->drawColor;
    float prevAlpha = renderer->drawAlpha;
    int32_t prevFont = renderer->drawFont;
    renderer->drawColor = color;
    renderer->drawAlpha = alpha;
    renderer->drawFont = -1;
    if (renderer->vtable->drawDebugText) renderer->vtable->drawDebugText(renderer, text, x, y, scale, scale, 0.0f, -1.0f);
    renderer->drawColor = prevColor;
    renderer->drawAlpha = prevAlpha;
    renderer->drawFont = prevFont;
}

void Overlay_draw(Runner* runner, uint32_t fps, int32_t fbWidth, int32_t fbHeight, size_t memBytes) {
    if (!gOverlay.initialized) return;
    if (gOverlay.state == OVERLAY_STATS_DISABLED) return;

    Renderer* renderer = runner->renderer;

    const char* roomName = runner->currentRoom != NULL && runner->currentRoom->name != NULL ? runner->currentRoom->name : "?";

    char debugText[512];
    char memLine[64];
    if (memBytes > 0) {
        snprintf(memLine, sizeof(memLine), "\nMemory: %.2f MB", (double) memBytes / (1024.0 * 1024.0));
    } else {
        memLine[0] = '\0';
    }
    snprintf(debugText, sizeof(debugText),
        "Room: %s\nFPS: %u\nInstances: %d\nStructs: %d%s",
        roomName, fps,
        (int) arrlen(runner->instances), (int) arrlen(runner->structInstances),
        memLine
    );

    bool blendWas = renderer->vtable->gpuGetBlendEnable(renderer);
    renderer->vtable->gpuSetBlendEnable(renderer, true);
    renderer->vtable->gpuSetBlendMode(renderer, bm_normal);

    renderer->vtable->beginGUI(renderer, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight, RENDER_TARGET_HOST_FRAMEBUFFER);

    drawOverlayText(renderer, 10.0f, 10.0f, OVERLAY_FONT_SCALE, 0xFFFFFF, 1.0f, debugText);

    if (gOverlay.state == OVERLAY_STATS_ENABLED_WITH_PROFILER) {
        float profilerY = 10.0f + ((float) DEBUGFONT_LINE_HEIGHT * OVERLAY_FONT_SCALE * 5.0f);

#ifdef ENABLE_VM_GML_PROFILER
        gOverlay.profilerFramesInWindow++;
        if (gOverlay.profilerFramesInWindow >= PROFILER_WINDOW_FRAMES) {
            char* profilerReport = Profiler_createReport(runner->vmContext->profiler, 25, gOverlay.profilerFramesInWindow);
            if (profilerReport != NULL) {
                snprintf(gOverlay.profilerOverlayText, sizeof(gOverlay.profilerOverlayText), "%s", profilerReport);
                free(profilerReport);
            }
            Profiler_reset(runner->vmContext->profiler);
            gOverlay.profilerFramesInWindow = 0;
        }
        const char* profilerDisplay = gOverlay.profilerOverlayText[0] != '\0' ? gOverlay.profilerOverlayText : "GML Profiler (collecting...)";
        drawOverlayText(renderer, 10.0f, profilerY, PROFILER_SCALE, 0xFFFFFF, 1.0f, profilerDisplay);
#else
        drawOverlayText(renderer, 10.0f, profilerY, PROFILER_SCALE, 0xFFFFFF, 1.0f, "Butterscotch GML Profiler is disabled on this build :(");
#endif
    }

    renderer->vtable->flush(renderer);
    renderer->vtable->endGUI(renderer);

    renderer->vtable->gpuSetBlendMode(renderer, bm_normal);
    renderer->vtable->gpuSetBlendEnable(renderer, blendWas);
}
