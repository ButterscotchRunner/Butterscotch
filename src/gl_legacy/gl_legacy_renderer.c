#include "gl_legacy_renderer.h"
#include "matrix_math.h"
#include "text_utils.h"


#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#include "ps3_textures.h"
extern GLuint gPalettedProgram;
extern GLint  gPalettedUPaletteVLoc;
// Activate the paletted shader for a sprite draw. The caller has already bound the index texture (via glBindTexture on TEXUNIT0).
// Sets unit 1 to the CLUT atlas and pushes uPaletteV for the TPAG's row.
#define PS3_PALETTED_BEGIN(tpagIndex) do {                                                  \
    float _v = PS3Textures_getTpagPaletteV(tpagIndex);                                      \
    if (0.0f > _v) break;                                                                   \
    glActiveTexture(GL_TEXTURE1);                                                           \
    glBindTexture(GL_TEXTURE_2D, PS3Textures_getClutTexture());                             \
    glEnable(GL_TEXTURE_2D);                                                                \
    glActiveTexture(GL_TEXTURE0);                                                           \
    glUseProgram(gPalettedProgram);                                                        \
    if (gPalettedUPaletteVLoc >= 0) glUniform1f(gPalettedUPaletteVLoc, _v);               \
} while (0)
#define PS3_PALETTED_END() do {                                                             \
    glUseProgram(0);                                                                        \
    glActiveTexture(GL_TEXTURE1);                                                           \
    glDisable(GL_TEXTURE_2D);                                                               \
    glActiveTexture(GL_TEXTURE0);                                                           \
} while (0)
#else
#include <glad/glad.h>
#define PS3_PALETTED_BEGIN(tpagIndex) ((void)0)
#define PS3_PALETTED_END()            ((void)0)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stb_image.h"
#include "stb_ds.h"
#include "utils.h"
#include "image_decoder.h"
#ifndef PLATFORM_PS3
#include "gl_common.h"
#endif

// ===[ Helpers ]===

#ifdef PLATFORM_PS3
static void glApplyViewport(GLLegacyRenderer* gl, int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t effW, effH;
    if ((gl->gameW * gl->windowH) / gl->gameH < gl->windowW) {
        effW = (gl->gameW * gl->windowH) / gl->gameH;
        effH = gl->windowH;
    } else {
        effW = gl->windowW;
        effH = (gl->gameH * gl->windowW) / gl->gameW;
    }
    float scale = (float)effW / (float)gl->gameW;
    int32_t offsetX = (gl->windowW - effW) / 2;
    int32_t offsetY = (gl->windowH - effH) / 2;

    int32_t vpX = offsetX + (int32_t)(x * scale);
    int32_t vpY = offsetY + (int32_t)((gl->gameH - y - h) * scale);
    int32_t vpW = (int32_t)(w * scale);
    int32_t vpH = (int32_t)(h * scale);

    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
}
#else
static void glApplyViewport(GLLegacyRenderer* gl, int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t glY = gl->gameH - y - h;
    glViewport(x, glY, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, glY, w, h);

    gl->base.CPortX = x;
    gl->base.CPortY = glY;
    gl->base.CPortW = w;
    gl->base.CPortH = h;
}
#endif

// ===[ Vtable Implementations ]===

static void glInit(Renderer* renderer, DataWin* dataWin) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    renderer->dataWin = dataWin;

    // Prepare texture slots for lazy loading (PNG decode deferred to first use)
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

#ifdef PLATFORM_PS3
    // TXTR is empty on PS3; page count comes from TEXTURES.BIN.
    gl->textureCount = PS3Textures_getPageCount();
#else
    gl->textureCount = dataWin->txtr.count;
#endif
    gl->glTextures = safeMalloc(gl->textureCount * sizeof(GLuint));
    gl->textureWidths = safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureHeights = safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = safeMalloc(gl->textureCount * sizeof(bool));

    glGenTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->textureCount > i; i++) {
        gl->textureWidths[i] = 0;
        gl->textureHeights[i] = 0;
        gl->textureLoaded[i] = false;
    }

    // Create 1x1 white pixel texture for primitive drawing (rectangles, lines, etc.)
    glGenTextures(1, &gl->whiteTexture);
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Save original counts so we know which slots are from data.win vs dynamic
    gl->originalTexturePageCount = gl->textureCount;
    gl->originalTpagCount = dataWin->tpag.count;
    gl->originalSpriteCount = dataWin->sprt.count;

#ifndef PLATFORM_PS3
    // Create FBO
    glGenFramebuffers(1, &gl->fbo);
    gl->fboTexture = 0;
    gl->fboWidth = 0;
    gl->fboHeight = 0;

    gl->surfaces = nullptr;
    gl->surfaceTexture = nullptr;
    gl->surfaceWidth = nullptr;
    gl->surfaceHeight = nullptr;
    gl->surfaceCount = 0;
#endif

    fprintf(stderr, "GL: Renderer initialized (%u texture pages)\n", gl->textureCount);
}

static void glDestroy(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glDeleteTextures(1, &gl->whiteTexture);

    glDeleteTextures((GLsizei) gl->textureCount, gl->glTextures);

#ifndef PLATFORM_PS3
    if (gl->fboTexture != 0) glDeleteTextures(1, &gl->fboTexture);
    if (gl->fbo != 0) glDeleteFramebuffers(1, &gl->fbo);
    for (uint32_t i = 0; gl->surfaceCount > i; i++) {
        if (gl->surfaceTexture[i] != 0) glDeleteTextures(1, &gl->surfaceTexture[i]);
        if (gl->surfaces[i] != 0) glDeleteFramebuffers(1, &gl->surfaces[i]);
    }
    free(gl->surfaces);
    free(gl->surfaceTexture);
    free(gl->surfaceWidth);
    free(gl->surfaceHeight);
#endif

    free(gl->glTextures);
    free(gl->textureWidths);
    free(gl->textureHeights);
    free(gl);
}

static void glBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    gl->windowW = windowW;
    gl->windowH = windowH;
    gl->gameW = gameW;
    gl->gameH = gameH;

#ifndef PLATFORM_PS3
    if (gameW != gl->fboWidth || gameH != gl->fboHeight) {
        GLCommon_resizeMainFBO(&gl->fboTexture, gl->fbo, &gl->fboWidth, &gl->fboHeight, gameW, gameH);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
    glViewport(0, 0, gameW, gameH);
    gl->base.CPortX = 0;
    gl->base.CPortY = 0;
    gl->base.CPortW = gameW;
    gl->base.CPortH = gameH;
#else
    glApplyViewport(gl, 0, 0, gameW, gameH);
#endif
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void glBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glBindTexture(GL_TEXTURE_2D, 0);

    // Set viewport and scissor to the port rectangle within the FBO
    // FBO uses game resolution, port coordinates are in game space
    // OpenGL viewport Y is bottom-up, game Y is top-down
    glApplyViewport(gl, portX, portY, portW, portH);

    // Build orthographic projection (Y-down for GML coordinate system)
    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, (float) viewX, (float) (viewX + viewW), (float) (viewY + viewH), (float) viewY, -1.0f, 1.0f);

    if (viewAngle != 0.0f) {
        // GML view_angle: rotate camera by this angle (degrees, counter-clockwise)
        // To rotate the camera, we rotate the world in the opposite direction around the view center
        float cx = (float) viewX + (float) viewW / 2.0f;
        float cy = (float) viewY + (float) viewH / 2.0f;
        Matrix4f rot;
        Matrix4f_identity(&rot);
        Matrix4f_translate(&rot, cx, cy, 0.0f);
        float angleRad = viewAngle * (float) M_PI / 180.0f;
        Matrix4f_rotateZ(&rot, -angleRad);
        Matrix4f_translate(&rot, -cx, -cy, 0.0f);
        Matrix4f result;
        Matrix4f_multiply(&result, &projection, &rot);
        projection = result;
    }

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glActiveTexture(GL_TEXTURE0);

    renderer->PreviousViewMatrix = projection;
}

static void glEndView(MAYBE_UNUSED Renderer* renderer) {
    glDisable(GL_SCISSOR_TEST);
}

static void glBeginGUI(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glBindTexture(GL_TEXTURE_2D, 0);

    glApplyViewport(gl, portX, portY, portW, portH);

    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, 0.0f, (float) guiW, (float) guiH, 0.0f, -1.0f, 1.0f);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glActiveTexture(GL_TEXTURE0);
}

static void glEndGUI(MAYBE_UNUSED Renderer* renderer) {
    glDisable(GL_SCISSOR_TEST);
}

static void glEndFrame(Renderer* renderer) {
    MAYBE_UNUSED GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
#ifndef PLATFORM_PS3
    GLCommon_letterboxBlit(gl->fbo, gl->fboWidth, gl->fboHeight, gl->gameW, gl->gameH, gl->windowW, gl->windowH);
#endif
}

static void glRendererFlush(MAYBE_UNUSED Renderer* renderer) {}

static void glClearScreen(MAYBE_UNUSED Renderer* renderer, uint32_t color, float alpha) {
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // GML draw_clear ignores the active scissor and clears the whole target. Disable scissor for the clear and restore it after.
    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    if (scissorWasEnabled) glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
}

// Lazily decodes and uploads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to decode.
static bool ensureTextureLoaded(GLLegacyRenderer* gl, uint32_t pageId) {
    if (gl->textureLoaded[pageId]) return (gl->textureWidths[pageId] != 0);

    gl->textureLoaded[pageId] = true;

    int w, h;
#ifdef PLATFORM_PS3
    // We'll load the textures on demand.
    uint8_t* pixels;
    if (!PS3Textures_loadPage(pageId, &w, &h, &pixels)) {
        fprintf(stderr, "GL: PS3 page %u has no pixels\n", pageId);
        return false;
    }
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    // Nearest is mandatory for index textures, bilinear would interpolate palette indices into nonsense colors.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);
#else
    DataWin* dw = gl->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];

    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    uint8_t* pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
    if (pixels == nullptr) {
        fprintf(stderr, "GL: Failed to decode TXTR page %u\n", pageId);
        return false;
    }

    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);
#endif
    fprintf(stderr, "GL: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
    return true;
}

static void glDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);

    // Compute normalized UVs from TPAG source rect
    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Compute local quad corners (relative to origin, with target offset)
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->sourceWidth;
    float localY1 = localY0 + (float) tpag->sourceHeight;

    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    glBegin(GL_QUADS);
        // Vertex 0: top-left
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v0);
        glVertex2f(x0, y0);

        // Vertex 1: top-right
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v0);
        glVertex2f(x1, y1);

        // Vertex 2: bottom-right
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v1);
        glVertex2f(x2, y2);

        // Vertex 3: bottom-left
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v1);
        glVertex2f(x3, y3);
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawTiled(Renderer* renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) tpag->boundingWidth * axScale;
    float tileH = (float) tpag->boundingHeight * ayScale;
    if (0 >= tileW || 0 >= tileH) return;

    float startX, endX, startY, endY;
    if (tileX) {
        startX = fmodf(x - originX * axScale, tileW);
        if (startX > 0) startX -= tileW;
        endX = roomW;
    } else {
        startX = x - originX * axScale;
        endX = startX + tileW;
    }
    if (tileY) {
        startY = fmodf(y - originY * ayScale, tileH);
        if (startY > 0) startY -= tileH;
        endY = roomH;
    } else {
        startY = y - originY * ayScale;
        endY = startY + tileH;
    }

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->sourceWidth;
    float localY1 = localY0 + (float) tpag->sourceHeight;
    float sx0 = xscale * localX0;
    float sy0 = yscale * localY0;
    float sx1 = xscale * localX1;
    float sy1 = yscale * localY1;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Emit the entire tile grid in a single glBegin -> glEnd
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);
    glBegin(GL_QUADS);
    glColor4f(r, g, b, alpha);
    for (float dy = startY; endY > dy; dy += tileH) {
        float cy = dy + originY * ayScale;
        float vy0 = cy + sy0;
        float vy1 = cy + sy1;
        for (float dx = startX; endX > dx; dx += tileW) {
            float cx = dx + originX * axScale;
            float vx0 = cx + sx0;
            float vx1 = cx + sx1;

            glTexCoord2f(u0, v0); glVertex2f(vx0, vy0);
            glTexCoord2f(u1, v0); glVertex2f(vx1, vy0);
            glTexCoord2f(u1, v1); glVertex2f(vx1, vy1);
            glTexCoord2f(u0, v1); glVertex2f(vx0, vy1);
        }
    }
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawSpritePos(Renderer* renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u0, v0);
        glVertex2f(x1, y1);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u1, v0);
        glVertex2f(x2, y2);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u1, v1);
        glVertex2f(x3, y3);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u0, v1);
        glVertex2f(x4, y4);
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    glBindTexture(GL_TEXTURE_2D, texId);

    // Compute UVs for the sub-region within the atlas
    float u0 = (float) (tpag->sourceX + srcOffX) / (float) texW;
    float v0 = (float) (tpag->sourceY + srcOffY) / (float) texH;
    float u1 = (float) (tpag->sourceX + srcOffX + srcW) / (float) texW;
    float v1 = (float) (tpag->sourceY + srcOffY + srcH) / (float) texH;

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Quad corners (no origin offset - draw_sprite_part ignores sprite origin)
    float cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3;
    if (angleDeg == 0.0f) {
        cx0 = x;                         cy0 = y;
        cx1 = x + (float) srcW * xscale; cy1 = y;
        cx2 = x + (float) srcW * xscale; cy2 = y + (float) srcH * yscale;
        cx3 = x;                         cy3 = y + (float) srcH * yscale;
    } else {
        float angleRad = -angleDeg * ((float) M_PI / 180.0f);
        float cosA = cosf(angleRad);
        float sinA = sinf(angleRad);
        float qx0 = x,                         qy0 = y;
        float qx1 = x + (float) srcW * xscale, qy1 = y;
        float qx2 = x + (float) srcW * xscale, qy2 = y + (float) srcH * yscale;
        float qx3 = x,                         qy3 = y + (float) srcH * yscale;
        float dx, dy;
        dx = qx0 - pivotX; dy = qy0 - pivotY; cx0 = cosA * dx - sinA * dy + pivotX; cy0 = sinA * dx + cosA * dy + pivotY;
        dx = qx1 - pivotX; dy = qy1 - pivotY; cx1 = cosA * dx - sinA * dy + pivotX; cy1 = sinA * dx + cosA * dy + pivotY;
        dx = qx2 - pivotX; dy = qy2 - pivotY; cx2 = cosA * dx - sinA * dy + pivotX; cy2 = sinA * dx + cosA * dy + pivotY;
        dx = qx3 - pivotX; dy = qy3 - pivotY; cx3 = cosA * dx - sinA * dy + pivotX; cy3 = sinA * dx + cosA * dy + pivotY;
    }

    PS3_PALETTED_BEGIN(tpagIndex);
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v0); glVertex2f(cx0, cy0);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v0); glVertex2f(cx1, cy1);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v1); glVertex2f(cx2, cy2);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v1); glVertex2f(cx3, cy3);
    glEnd();
    PS3_PALETTED_END();
}

// Emits a single colored quad into the batch using the white pixel texture
static void emitColoredQuad(GLLegacyRenderer* gl, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    glBegin(GL_QUADS);
        // All UVs point to (0.5, 0.5) center of the 1x1 white texture
        // Vertex 0: top-left
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x0, y0);

        // Vertex 1: top-right
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1, y0);

        // Vertex 2: bottom-right
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1, y1);

        // Vertex 3: bottom-left
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x0, y1);
    glEnd();
}

static void glDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        emitColoredQuad(gl, x1, y1, x2 + 1, y1 + 1, r, g, b, alpha); // top
        emitColoredQuad(gl, x1, y2, x2 + 1, y2 + 1, r, g, b, alpha); // bottom
        emitColoredQuad(gl, x1, y1 + 1, x1 + 1, y2, r, g, b, alpha); // left
        emitColoredQuad(gl, x2, y1 + 1, x2 + 1, y2, r, g, b, alpha); // right
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects
        emitColoredQuad(gl, x1, y1, x2 + 1, y2 + 1, r, g, b, alpha);
    }
}

static void glDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha, bool outline) {
    // Stub! Please implement me later. :3
    glDrawRectangle(renderer, x1, y1, x2, y2, color1, alpha, outline);
}

// ===[ Line Drawing ]===

static void glDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    // Vertex 0: start + perpendicular
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 + px, y1 + py);

        // Vertex 1: start - perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 - px, y1 - py);

        // Vertex 2: end - perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 - px, y2 - py);

        // Vertex 3: end + perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 + px, y2 + py);
    glEnd();
}

static void glDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    // Emit quad with per-vertex colors (color1 at start, color2 at end)
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    glBegin(GL_QUADS);
        // Vertex 0: start + perpendicular (color1)
        glColor4f(r1, g1, b1, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 + px, y1 + py); 

        // Vertex 1: start - perpendicular (color1)
        glColor4f(r1, g1, b1, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 - px, y1 - py); 

        // Vertex 2: end - perpendicular (color2)
        glColor4f(r2, g2, b2, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 - px, y2 - py); 

        // Vertex 3: end + perpendicular (color2)
        glColor4f(r2, g2, b2, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 + px, y2 + py); 
    glEnd();
}

static void glDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, bool outline)
{
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if(outline)
    {
        glDrawLine(renderer, x1, y1, x2, y2, 1, renderer->drawColor, 1.0);
        glDrawLine(renderer, x2, y2, x3, y3, 1, renderer->drawColor, 1.0);
        glDrawLine(renderer, x3, y3, x1, y1, 1, renderer->drawColor, 1.0);
    } else {
        float r = (float) BGR_R(renderer->drawColor) / 255.0f;
        float g = (float) BGR_G(renderer->drawColor) / 255.0f;
        float b = (float) BGR_B(renderer->drawColor) / 255.0f;

        glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

        glBegin(GL_TRIANGLES);
            glColor4f(r, g, b, renderer->drawAlpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1 , y1); 

            glColor4f(r, g, b, renderer->drawAlpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2, y2); 

            glColor4f(r, g, b, renderer->drawAlpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x3, y3); 
        glEnd();
    }
}

// ===[ Text Drawing ]===

// Resolved font state shared between glDrawText and glDrawTextColor
typedef struct {
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (nullptr for sprite fonts)
    int32_t fontTpagIndex;     // TPAG index for regular fonts (-1 for sprite fonts)
    GLuint texId;
    int32_t texW, texH;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (nullptr for regular fonts)
} GlFontState;

// Resolves font texture state
// Returns false if the font can't be drawn
static bool glResolveFontState(GLLegacyRenderer* gl, DataWin* dw, Font* font, GlFontState* state) {
    state->font = font;
    state->fontTpag = nullptr;
    state->fontTpagIndex = -1;
    state->texId = 0;
    state->texW = 0;
    state->texH = 0;
    state->spriteFontSprite = nullptr;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = font->tpagIndex;
        if (0 > fontTpagIndex) return false;

        state->fontTpagIndex = fontTpagIndex;
        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= gl->textureCount) return false;
        if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return false;

        state->texId = gl->glTextures[pageId];
        state->texW = gl->textureWidths[pageId];
        state->texH = gl->textureHeights[pageId];
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    return true;
}

// Resolves UV coordinates, texture ID, and local position for a single glyph
// Returns false if the glyph can't be drawn
static bool glResolveGlyph(GLLegacyRenderer* gl, DataWin* dw, GlFontState* state, FontGlyph* glyph, float cursorX, float cursorY, GLuint* outTexId, int32_t* outTpagIdx, float* outU0, float* outV0, float* outU1, float* outV1, float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIdx = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= gl->textureCount) return false;
        if (!ensureTextureLoaded(gl, (uint32_t) pid)) return false;

        *outTexId = gl->glTextures[pid];
        *outTpagIdx = tpagIdx;
        int32_t tw = gl->textureWidths[pid];
        int32_t th = gl->textureHeights[pid];

        *outU0 = (float) glyphTpag->sourceX / (float) tw;
        *outV0 = (float) glyphTpag->sourceY / (float) th;
        *outU1 = (float) (glyphTpag->sourceX + glyphTpag->sourceWidth) / (float) tw;
        *outV1 = (float) (glyphTpag->sourceY + glyphTpag->sourceHeight) / (float) th;

        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) ((int32_t) glyphTpag->targetY - sprite->originY);
    } else {
        *outTexId = state->texId;
        *outTpagIdx = state->fontTpagIndex;
        *outU0 = (float) (state->fontTpag->sourceX + glyph->sourceX) / (float) state->texW;
        *outV0 = (float) (state->fontTpag->sourceY + glyph->sourceY) / (float) state->texH;
        *outU1 = (float) (state->fontTpag->sourceX + glyph->sourceX + glyph->sourceWidth) / (float) state->texW;
        *outV1 = (float) (state->fontTpag->sourceY + glyph->sourceY + glyph->sourceHeight) / (float) state->texH;

        *outLocalX0 = cursorX + glyph->offset;
        *outLocalY0 = cursorY;
    }
    return true;
}

static void glDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    uint32_t color = renderer->drawColor;
    float alpha = renderer->drawAlpha;
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    int32_t textLen = (int32_t) strlen(text);

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    // Per-line vertical stride. HTML5 runner's default `linesep` is `max_glyph_height * scaleY`.
    // We apply scaleY via the transform matrix below, so keep the stride in pre-scale (local) coords.
    float lineStride = TextUtils_lineStride(font);

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from the per-line y offset
    // (see yyFont.GR_Text_Draw), shifting glyphs up so the baseline aligns with the drawn y.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;
                    int32_t glyphTpagIdx;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &glyphTpagIdx, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        glBindTexture(GL_TEXTURE_2D, glyphTexId);
                        PS3_PALETTED_BEGIN(glyphTpagIdx);

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        glBegin(GL_QUADS);
                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u0, v0);
                            glVertex2f(px0, py0);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u1, v0);
                            glVertex2f(px1, py1);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u1, v1);
                            glVertex2f(px2, py2);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u0, v1);
                            glVertex2f(px3, py3);
                        glEnd();
                        PS3_PALETTED_END();

                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    cursorX += TextUtils_getKerningOffset(glyph, nextCh);
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

static void glDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    int32_t textLen = (int32_t) strlen(text);
    if(textLen == 0) return;

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float lineStride = TextUtils_lineStride(font);

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from per-line y offset.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;
        // Pixel-position cursor for the gradient
        float gradientX = 0.0f;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                float advance = (float) glyph->shift;
                float leftFrac  = (lineWidth > 0.0f) ? (gradientX           / lineWidth) : 0.0f;
                float rightFrac = (lineWidth > 0.0f) ? ((gradientX + advance) / lineWidth) : 1.0f;
                int32_t c1 = Color_lerp(_c1, _c2, leftFrac);
                int32_t c2 = Color_lerp(_c1, _c2, rightFrac);
                int32_t c3 = Color_lerp(_c4, _c3, rightFrac);
                int32_t c4 = Color_lerp(_c4, _c3, leftFrac);

                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;
                    int32_t glyphTpagIdx;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &glyphTpagIdx, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        glBindTexture(GL_TEXTURE_2D, glyphTexId);
                        PS3_PALETTED_BEGIN(glyphTpagIdx);

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        glBegin(GL_QUADS);
                            glColor4ub(BGR_R(c1), BGR_G(c1), BGR_B(c1), alpha * 255);
                            glTexCoord2f(u0, v0);
                            glVertex2f(px0, py0);

                            glColor4ub(BGR_R(c2), BGR_G(c2), BGR_B(c2), alpha * 255);
                            glTexCoord2f(u1, v0);
                            glVertex2f(px1, py1);

                            glColor4ub(BGR_R(c3), BGR_G(c3), BGR_B(c3), alpha * 255);
                            glTexCoord2f(u1, v1);
                            glVertex2f(px2, py2);

                            glColor4ub(BGR_R(c4), BGR_G(c4), BGR_B(c4), alpha * 255);
                            glTexCoord2f(u0, v1);
                            glVertex2f(px3, py3);
                        glEnd();
                        PS3_PALETTED_END();

                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                gradientX   += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    float kern = TextUtils_getKerningOffset(glyph, nextCh);
                    cursorX += kern;
                    gradientX   += kern;
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

// ===[ Dynamic Sprite Creation/Deletion ]===

// Finds a free dynamic texture page slot (glTextures[i] == 0), or appends a new one.
static uint32_t findOrAllocTexturePageSlot(GLLegacyRenderer* gl) {
    // Scan dynamic range for a reusable slot
    for (uint32_t i = gl->originalTexturePageCount; gl->textureCount > i; i++) {
        if (gl->glTextures[i] == 0) return i;
    }
    // No free slot found, grow the arrays
    uint32_t newPageId = gl->textureCount;
    gl->textureCount++;
    gl->glTextures = safeRealloc(gl->glTextures, gl->textureCount * sizeof(GLuint));
    gl->textureWidths = safeRealloc(gl->textureWidths, gl->textureCount * sizeof(int32_t));
    gl->textureHeights = safeRealloc(gl->textureHeights, gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = safeRealloc(gl->textureLoaded, gl->textureCount * sizeof(bool));
    gl->glTextures[newPageId] = 0;
    gl->textureWidths[newPageId] = 0;
    gl->textureHeights[newPageId] = 0;
    gl->textureLoaded[newPageId] = false;
    return newPageId;
}

// Finds a free dynamic TPAG slot (texturePageId == -1), or appends a new one.
static uint32_t findOrAllocTpagSlot(DataWin* dw, uint32_t originalTpagCount) {
    for (uint32_t i = originalTpagCount; dw->tpag.count > i; i++) {
        if (dw->tpag.items[i].texturePageId == -1) return i;
    }
    uint32_t newIndex = dw->tpag.count;
    dw->tpag.count++;
    dw->tpag.items = safeRealloc(dw->tpag.items, dw->tpag.count * sizeof(TexturePageItem));
    memset(&dw->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dw->tpag.items[newIndex].texturePageId = -1;
    return newIndex;
}

static int32_t glCreateSpriteFromSurface(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 >= w || 0 >= h) return -1;

    // Because we don't support surfaces for now, games may attempt to read from surfaces that aren't the main surface could cause the game to crash with a buffer overruns (example: the PS3 renderer)
    // So, if we are trying to read from ANYWHERE that isn't the main surface, reject the read
    if (surfaceID != 0)
        return -1;

    uint8_t* pixels = safeMalloc((size_t) w * (size_t) h * 4);
    if (pixels == nullptr)
        return -1;

#ifdef PLATFORM_PS3
    if (0 > x || 0 > y || (uint32_t) (x + w) > display_width || (uint32_t) (y + h) > display_height) {
        free(pixels);
        return -1;
    }
    // Ensure that the draw calls were executed
    waitFinish();
    const uint8_t* src = (const uint8_t*) color_buffer[curr_fb];
    size_t srcRowBytes = color_pitch;
    size_t dstRowBytes = (size_t) w * 4;
    repeat(h, row) {
        const uint8_t* srcLine = src + ((size_t) (y + row)) * srcRowBytes + (size_t) (x * 4);
        uint8_t* dstLine = pixels + (size_t) row * dstRowBytes;
        repeat(w, px) {
            // Swizzle from ARGB to RGBA
            uint8_t a = srcLine[px * 4 + 0];
            uint8_t r = srcLine[px * 4 + 1];
            uint8_t g = srcLine[px * 4 + 2];
            uint8_t b = srcLine[px * 4 + 3];
            dstLine[px * 4 + 0] = r;
            dstLine[px * 4 + 1] = g;
            dstLine[px * 4 + 2] = b;
            dstLine[px * 4 + 3] = a;
        }
    }
    // We don't need to flip vertically because the PlayStation 3 framebuffer is already top-down
#else
    // OpenGL Y is bottom-up, GML Y is top-down, so flip the Y coordinate
    int32_t glY = gl->gameH - y - h;
    glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Flip vertically (OpenGL reads bottom-to-top)
    size_t rowBytes = (size_t) w * 4;
    uint8_t* rowTemp = safeMalloc(rowBytes);
    repeat(h / 2, row) {
        uint8_t* top = pixels + row * rowBytes;
        uint8_t* bot = pixels + (h - 1 - row) * rowBytes;
        memcpy(rowTemp, top, rowBytes);
        memcpy(top, bot, rowBytes);
        memcpy(bot, rowTemp, rowBytes);
    }
    free(rowTemp);
#endif

    // Create a new GL texture from the captured pixels
    GLuint newTexId;
    glGenTextures(1, &newTexId);
    glBindTexture(GL_TEXTURE_2D, newTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);

    // Find or allocate slots for texture page, TPAG, and sprite
    uint32_t pageId = findOrAllocTexturePageSlot(gl);
    gl->glTextures[pageId] = newTexId;
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;
    gl->textureLoaded[pageId] = true;

    uint32_t tpagIndex = findOrAllocTpagSlot(dw, gl->originalTpagCount);
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t) w;
    tpag->sourceHeight = (uint16_t) h;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t) w;
    tpag->targetHeight = (uint16_t) h;
    tpag->boundingWidth = (uint16_t) w;
    tpag->boundingHeight = (uint16_t) h;
    tpag->texturePageId = (int16_t) pageId;

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, gl->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    // name was set by DataWin_allocSpriteSlot ("__newsprite<N>"); don't overwrite it here
    sprite->width = (uint32_t) w;
    sprite->height = (uint32_t) h;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t) tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = nullptr;

    fprintf(stderr, "GL: Created dynamic sprite %u (%dx%d) from surface at (%d,%d)\n", spriteIndex, w, h, x, y);
    return (int32_t) spriteIndex;
}

static void glDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > spriteIndex || dw->sprt.count <= (uint32_t) spriteIndex) return;

    // Refuse to delete original data.win sprites
    if (gl->originalSpriteCount > (uint32_t) spriteIndex) {
        fprintf(stderr, "GL: Cannot delete data.win sprite %d\n", spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return; // already deleted

    // Clean up GL texture and TPAG entries owned by this sprite.
    // Slots with index >= originalTpagCount are dynamically allocated and ours to free.
    repeat(sprite->textureCount, i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t) tpagIdx >= gl->originalTpagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && gl->textureCount > (uint32_t) pageId) {
                glDeleteTextures(1, &gl->glTextures[pageId]);
                gl->glTextures[pageId] = 0;
            }
            // Mark TPAG slot as free for reuse
            tpag->texturePageId = -1;
        }
    }

    // Clear the sprite entry so it won't be drawn and can be reused. Preserve `name` across the memset: the slot is still in sprt.count and must keep a valid string for asset_get_index / name lookups.
    free(sprite->tpagIndices);
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;

    fprintf(stderr, "GL: Deleted sprite %d\n", spriteIndex);
}

static void glGpuSetBlendMode(MAYBE_UNUSED Renderer* renderer, int32_t mode) {
    glBlendEquation(GLCommon_blendModeToEquation(mode));
    glBlendFunc(GLCommon_blendModeToSFactor(mode), GLCommon_blendModeToDFactor(mode));
}

static void glGpuSetBlendModeExt(MAYBE_UNUSED Renderer* renderer, int32_t sfactor, int32_t dfactor) {
    glBlendFunc(GLCommon_blendFactorToGL(sfactor), GLCommon_blendFactorToGL(dfactor));
}

static void glGpuSetBlendEnable(Renderer* renderer, bool enable) {
    enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
}

static bool glGpuGetBlendEnable(Renderer* renderer) {
    
    return glIsEnabled(GL_BLEND);
}

static void glGpuSetAlphaTestEnable(Renderer* renderer, bool enable) {
    enable ? glEnable(GL_ALPHA_TEST) : glDisable(GL_ALPHA_TEST);
}

static void glGpuSetAlphaTestRef(Renderer* renderer, uint8_t ref) {
    glAlphaFunc(GL_GREATER, ref/255.0f);
}

static void glGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    gl->colorWriteR = red;
    gl->colorWriteG = green;
    gl->colorWriteB = blue;
    gl->colorWriteA = alpha;
    glColorMask(red, green, blue, alpha);
}

static void glGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    *red = gl->colorWriteR;
    *green = gl->colorWriteG;
    *blue = gl->colorWriteB;
    *alpha = gl->colorWriteA;
}

// ===[ Surfaces ]===

#ifndef PLATFORM_PS3

static int32_t glLegacyCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    uint32_t surfaceIndex = GLCommon_findOrAllocateSurfaceSlot(&gl->surfaces, &gl->surfaceTexture, &gl->surfaceWidth, &gl->surfaceHeight, &gl->surfaceCount);

    glGenFramebuffers(1, &gl->surfaces[surfaceIndex]);
    glGenTextures(1, &gl->surfaceTexture[surfaceIndex]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceIndex]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex], 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "GL: Surface FBO incomplete (status=0x%X)\n", status);
    }

    // Rebind whatever was the current render target before this call.
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);

    gl->surfaceWidth[surfaceIndex] = width;
    gl->surfaceHeight[surfaceIndex] = height;

    fprintf(stderr, "GL: Created surface %u with size (%dx%d)\n", surfaceIndex, width, height);
    return (int32_t) surfaceIndex;
}

static bool glLegacySurfaceExists(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    return gl->surfaces[surfaceId] != 0;
}

static float glLegacyGetSurfaceWidth(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceWidth[surfaceId];
}

static float glLegacyGetSurfaceHeight(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceHeight[surfaceId];
}

static void glLegacySurfaceResize(Renderer* renderer, int32_t surfaceId, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return;
    if (gl->surfaces[surfaceId] == 0) return;
    if (gl->surfaceWidth[surfaceId] == width && gl->surfaceHeight[surfaceId] == height) return;

    if (gl->surfaceTexture[surfaceId] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceId]);

    glGenTextures(1, &gl->surfaceTexture[surfaceId]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceId], 0);
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);

    gl->surfaceWidth[surfaceId] = width;
    gl->surfaceHeight[surfaceId] = height;
    fprintf(stderr, "GL: Resized Surface %u to (%dx%d)\n", surfaceId, width, height);
}

static void glLegacySurfaceFree(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return;
    if (gl->surfaceTexture[surfaceId] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceId]);
    if (gl->surfaces[surfaceId] != 0) glDeleteFramebuffers(1, &gl->surfaces[surfaceId]);
    gl->surfaces[surfaceId] = 0;
    gl->surfaceTexture[surfaceId] = 0;
    gl->surfaceWidth[surfaceId] = 0;
    gl->surfaceHeight[surfaceId] = 0;
    fprintf(stderr, "GL: Freed Surface %d\n", surfaceId);
}

// Binds the given surface (or APPLICATION_SURFACE_ID for the main FBO) and sets a matching ortho projection.
static bool glLegacySetRenderTarget(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    if (surfaceId == APPLICATION_SURFACE_ID) {
        glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
        glViewport(gl->base.CPortX, gl->base.CPortY, gl->base.CPortW, gl->base.CPortH);
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(renderer->PreviousViewMatrix.m);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glEnable(GL_SCISSOR_TEST);
        return true;
    }

    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;

    int32_t w = gl->surfaceWidth[surfaceId];
    int32_t h = gl->surfaceHeight[surfaceId];

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, 0.0f, (float) w, (float) h, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    return true;
}

// Resolves a surfaceID (or APPLICATION_SURFACE_ID for the main FBO) to a GL texture and its size.
static bool resolveSurfaceTexture(GLLegacyRenderer* gl, int32_t surfaceId, GLuint* outTexId, int32_t* outW, int32_t* outH) {
    if (surfaceId == APPLICATION_SURFACE_ID) {
        *outTexId = gl->fboTexture;
        *outW = gl->fboWidth;
        *outH = gl->fboHeight;
        return *outTexId != 0;
    }
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;
    *outTexId = gl->surfaceTexture[surfaceId];
    *outW = gl->surfaceWidth[surfaceId];
    *outH = gl->surfaceHeight[surfaceId];
    return true;
}

// Surface textures are stored with Y=0 at the bottom (OpenGL convention), but GML treats
// surfaces top-down, so we sample with V flipped (v0=1, v1=0) when drawing them.
static void glLegacyDrawSurface(Renderer* renderer, int32_t surfaceId, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSurfaceTexture(gl, surfaceId, &texId, &texW, &texH)) return;

    if (0 > srcWidth) { srcLeft = 0; srcTop = 0; srcWidth = texW; srcHeight = texH; }

    // top-down GML coords -> flipped V for our bottom-up texture
    float u0 = (float) srcLeft / (float) texW;
    float u1 = (float) (srcLeft + srcWidth) / (float) texW;
    float v0 = 1.0f - (float) srcTop / (float) texH;
    float v1 = 1.0f - (float) (srcTop + srcHeight) / (float) texH;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, 0.0f,             0.0f,             &x0, &y0);
    Matrix4f_transformPoint(&transform, (float) srcWidth, 0.0f,             &x1, &y1);
    Matrix4f_transformPoint(&transform, (float) srcWidth, (float) srcHeight, &x2, &y2);
    Matrix4f_transformPoint(&transform, 0.0f,             (float) srcHeight, &x3, &y3);

    glBindTexture(GL_TEXTURE_2D, texId);
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha); glTexCoord2f(u0, v0); glVertex2f(x0, y0);
        glColor4f(r, g, b, alpha); glTexCoord2f(u1, v0); glVertex2f(x1, y1);
        glColor4f(r, g, b, alpha); glTexCoord2f(u1, v1); glVertex2f(x2, y2);
        glColor4f(r, g, b, alpha); glTexCoord2f(u0, v1); glVertex2f(x3, y3);
    glEnd();
}

static void glLegacySurfaceCopy(Renderer* renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLCommon_surfaceBlit(gl->fbo, gl->fboWidth, gl->fboHeight, gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, destSurfaceID, destX, destY, srcSurfaceID, srcX, srcY, srcW, srcH, part);
}

static bool glLegacySurfaceGetPixels(Renderer* renderer, int32_t surfaceId, uint8_t* outRGBA) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    return GLCommon_surfaceGetPixels(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, surfaceId, outRGBA);
}

#else

// TODO: Add support for surfaces in ps3gl!

static int32_t glLegacyCreateSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t width, MAYBE_UNUSED int32_t height) { return -1; }
static bool glLegacySurfaceExists(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) { return false; }
static bool glLegacySetRenderTarget(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) { return false; }
static float glLegacyGetSurfaceWidth(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) { return 0.0f; }
static float glLegacyGetSurfaceHeight(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) { return 0.0f; }
static void glLegacyDrawSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED int32_t srcLeft, MAYBE_UNUSED int32_t srcTop, MAYBE_UNUSED int32_t srcWidth, MAYBE_UNUSED int32_t srcHeight, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {}
static void glLegacySurfaceResize(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED int32_t width, MAYBE_UNUSED int32_t height) {}
static void glLegacySurfaceFree(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) {}
static void glLegacySurfaceCopy(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t DestSurfaceID, MAYBE_UNUSED int32_t DestX, MAYBE_UNUSED int32_t DestY, MAYBE_UNUSED int32_t SrcSurfaceID, MAYBE_UNUSED int32_t SrcX, MAYBE_UNUSED int32_t SrcY, MAYBE_UNUSED int32_t SrcW, MAYBE_UNUSED int32_t SrcH, MAYBE_UNUSED bool part) {}
static bool glLegacySurfaceGetPixels(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED uint8_t* outRGBA) { return false; }

#endif


// ===[ Vtable ]===

static RendererVtable glVtable = {
    .init = glInit,
    .destroy = glDestroy,
    .beginFrame = glBeginFrame,
    .endFrame = glEndFrame,
    .beginView = glBeginView,
    .endView = glEndView,
    .beginGUI = glBeginGUI,
    .endGUI = glEndGUI,
    .drawSprite = glDrawSprite,
    .drawSpritePos = glDrawSpritePos,
    .drawSpritePart = glDrawSpritePart,
    .drawRectangle = glDrawRectangle,
    .drawRectangleColor = glDrawRectangleColor,
    .drawLine = glDrawLine,
    .drawLineColor = glDrawLineColor,
    .drawTriangle = glDrawTriangle,
    .drawText = glDrawText,
    .drawTextColor = glDrawTextColor,
    .flush = glRendererFlush,
    .clearScreen = glClearScreen,
    .createSpriteFromSurface = glCreateSpriteFromSurface,
    .deleteSprite = glDeleteSprite,
    .gpuSetBlendMode = glGpuSetBlendMode,
    .gpuSetBlendModeExt = glGpuSetBlendModeExt,
    .gpuSetBlendEnable = glGpuSetBlendEnable,
    .gpuSetAlphaTestEnable = glGpuSetAlphaTestEnable,
    .gpuSetAlphaTestRef = glGpuSetAlphaTestRef,
    .gpuSetColorWriteEnable = glGpuSetColorWriteEnable,
    .gpuGetColorWriteEnable = glGpuGetColorWriteEnable,
    .gpuGetBlendEnable = glGpuGetBlendEnable,
    .drawTile = nullptr,
    .drawTiled = glDrawTiled,
    .createSurface = glLegacyCreateSurface,
    .surfaceExists = glLegacySurfaceExists,
    .setRenderTarget = glLegacySetRenderTarget,
    .getSurfaceWidth = glLegacyGetSurfaceWidth,
    .getSurfaceHeight = glLegacyGetSurfaceHeight,
    .drawSurface = glLegacyDrawSurface,
    .surfaceResize = glLegacySurfaceResize,
    .surfaceFree = glLegacySurfaceFree,
    .surfaceCopy = glLegacySurfaceCopy,
    .surfaceGetPixels = glLegacySurfaceGetPixels,
};

// ===[ Public API ]===

Renderer* GLLegacyRenderer_create(void) {
    GLLegacyRenderer* gl = safeCalloc(1, sizeof(GLLegacyRenderer));
    gl->base.vtable = &glVtable;
    gl->base.drawColor = 0xFFFFFF; // white (BGR)
    gl->base.drawAlpha = 1.0f;
    gl->base.drawFont = -1;
    gl->base.drawHalign = 0;
    gl->base.drawValign = 0;
    gl->base.circlePrecision = 24;
    gl->colorWriteR = true;
    gl->colorWriteG = true;
    gl->colorWriteB = true;
    gl->colorWriteA = true;
    return (Renderer*) gl;
}
