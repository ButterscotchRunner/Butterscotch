#include "ps3_overlay.h"

#include "debug_font.h"
#include "profiler.h"
#include "utils.h"
#include "stb_ds.h"

#include <ps3gl.h>
#include "stdio_compat.h"
#include <stdlib.h>
#include <string.h>

#define OVERLAY_LINE_HEIGHT_SCALE 0.80f
#define PROFILER_WINDOW_FRAMES 60

typedef struct {
    bool initialized;
    DebugOverlayState state;
    GLuint fontTexture;
    int profilerFramesInWindow;
#ifdef ENABLE_VM_GML_PROFILER
    char profilerOverlayText[4096];
#endif
} PS3Overlay;

static PS3Overlay gOverlay = { 0 };

void PS3Overlay_init(void) {
    if (gOverlay.initialized) return;

    // Convert the 8bpp atlas into RGBA
    uint8_t* rgba = (uint8_t*) malloc((size_t) (DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H * 4));
    if (rgba == nullptr) {
        fprintf(stderr, "PS3Overlay: failed to allocate %d bytes for the font atlas\n", DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H * 4);
        return;
    }

    repeat(DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H, i) {
        uint8_t a = debugFontPixels[i];
        rgba[i * 4 + 0] = 0xFF;
        rgba[i * 4 + 1] = 0xFF;
        rgba[i * 4 + 2] = 0xFF;
        rgba[i * 4 + 3] = a;
    }

    glGenTextures(1, &gOverlay.fontTexture);
    glBindTexture(GL_TEXTURE_2D, gOverlay.fontTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DEBUGFONT_ATLAS_W, DEBUGFONT_ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(rgba);

    gOverlay.state = STATS_ENABLED;
    gOverlay.profilerFramesInWindow = 0;
    gOverlay.initialized = true;
}

void PS3Overlay_deinit(void) {
    if (!gOverlay.initialized) return;
    glDeleteTextures(1, &gOverlay.fontTexture);
    memset(&gOverlay, 0, sizeof(gOverlay));
}

DebugOverlayState PS3Overlay_getDebugOverlayState(void) {
    if (!gOverlay.initialized) return STATS_DISABLED;
    return gOverlay.state;
}

void PS3Overlay_toggleDebugOverlay(MAYBE_UNUSED Runner* runner) {
    if (!gOverlay.initialized) return;
    gOverlay.state = (gOverlay.state + 1) % STATS_MAX;

#ifdef ENABLE_VM_GML_PROFILER
    Profiler_setEnabled(&runner->vmContext->profiler, gOverlay.state == STATS_ENABLED_WITH_PROFILER);
    gOverlay.profilerFramesInWindow = 0;
    gOverlay.profilerOverlayText[0] = '\0';
#endif
}

static const DebugFontGlyphEntry* lookupGlyph(uint8_t c) {
    if (DEBUGFONT_FIRST_CP > c || c > DEBUGFONT_LAST_CP) return nullptr;
    return &debugFontGlyphs[c - DEBUGFONT_FIRST_CP];
}

// Draws a string at (x, y) using the bound font texture.
// Assumes the caller has set up an ortho projection in pixel space, bound the font texture, and configured glColor4ub.
static void drawText(float x, float y, float scale, const char* text) {
    if (text == nullptr) return;
    int32_t len = (int32_t) strlen(text);

    float cursorY = y;
    int32_t lineStart = 0;

    for (int32_t i = 0; len >= i; i++) {
        if (i == len || text[i] == '\n') {
            int32_t lineLen = i - lineStart;
            float pen = x;

            glBegin(GL_QUADS);
            repeat((uint32_t) lineLen, j) {
                const DebugFontGlyphEntry* g = lookupGlyph((uint8_t) text[lineStart + (int32_t) j]);
                if (g == nullptr) continue;

                if (g->w > 0 && g->h > 0) {
                    float qx0 = pen + (float) g->xoffset * scale;
                    float qy0 = cursorY + (float) g->yoffset * scale;
                    float qx1 = qx0 + (float) g->w * scale;
                    float qy1 = qy0 + (float) g->h * scale;

                    float u0 = (float) g->x / (float) DEBUGFONT_ATLAS_W;
                    float v0 = (float) g->y / (float) DEBUGFONT_ATLAS_H;
                    float u1 = ((float) g->x + (float) g->w) / (float) DEBUGFONT_ATLAS_W;
                    float v1 = ((float) g->y + (float) g->h) / (float) DEBUGFONT_ATLAS_H;

                    glTexCoord2f(u0, v0); glVertex2f(qx0, qy0);
                    glTexCoord2f(u1, v0); glVertex2f(qx1, qy0);
                    glTexCoord2f(u1, v1); glVertex2f(qx1, qy1);
                    glTexCoord2f(u0, v1); glVertex2f(qx0, qy1);
                }

                pen += (float) g->xadvance * scale;
            }
            glEnd();

            cursorY += (float) DEBUGFONT_LINE_HEIGHT * scale * OVERLAY_LINE_HEIGHT_SCALE;
            lineStart = i + 1;
        }
    }
}

void PS3Overlay_drawDebugOverlay(const Runner* runner, float tickMs, float stepMs, float drawMs, float audioMs, int fbWidth, int fbHeight) {
    if (!gOverlay.initialized) return;
    if (gOverlay.state == STATS_DISABLED) return;

    const char* roomName = runner->currentRoom != nullptr && runner->currentRoom->name != nullptr ? runner->currentRoom->name : "?";

    char debugText[384];
    snprintf(debugText, sizeof(debugText),
        "Room: %s\nTick: %.2fms\nStep: %.2fms\nDraw: %.2fms\nAudio: %.2fms\nRoom Speed: %u\nInstances: %d\nStructs: %d",
        roomName, (double) tickMs, (double) stepMs, (double) drawMs, (double) audioMs,
        runner->currentRoom != nullptr ? runner->currentRoom->speed : 0,
        (int) arrlen(runner->instances), (int) arrlen(runner->structInstances)
    );

    // Save the renderer's projection/modelview so we can set up our own pixel-space ortho without disturbing it.
    // TODO: Replace with glOrtho!
    float fbW = (float) fbWidth;
    float fbH = (float) fbHeight;
    float orthoMatrix[16] = {
        2.0f / fbW,  0.0f,         0.0f,  0.0f,
        0.0f,        -2.0f / fbH,  0.0f,  0.0f,
        0.0f,        0.0f,         -1.0f, 0.0f,
        -1.0f,       1.0f,         0.0f,  1.0f,
    };

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(orthoMatrix);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Update the view port to fill the entire screen instead of using letterboxing.
    glViewport(0, 0, fbWidth, fbHeight);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, gOverlay.fontTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4ub(0xFF, 0xFF, 0xFF, 0xFF);

    drawText(10.0f, 10.0f, 0.8f, debugText);

    if (gOverlay.state == STATS_ENABLED_WITH_PROFILER) {
        // Match PS2 layout: profiler block sits below the fixed-height status block. Scale matches the main text size.
        float profilerY = 10.0f + ((float) DEBUGFONT_LINE_HEIGHT * 0.8f * OVERLAY_LINE_HEIGHT_SCALE * 9.0f) + 6.0f;

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
        drawText(10.0f, profilerY, 0.45f, profilerDisplay);
#else
        drawText(10.0f, profilerY, 0.45f, "Butterscotch GML Profiler is disabled on this build :(");
#endif
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}
