#include "gl_common.h"

#include "stdio_compat.h"
#include <stdlib.h>
#include "string_compat.h"
#include <math.h>

#include "runner.h"
#include "utils.h"
#include "renderer.h" // for bm_* constants
#include "matrix_math.h"
#include "debug_font.h"

// ===[ Letterbox blit ]===

void GLCommon_computeLetterbox(int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, int32_t* outStartX, int32_t* outStartY, int32_t* outEndX, int32_t* outEndY) {
    int32_t effW, effH;
    if ((gameW * windowH) / gameH < windowW) {
        effW = (gameW * windowH) / gameH;
        effH = windowH;
    } else {
        effW = windowW;
        effH = (gameH * windowW) / gameW;
    }
    int32_t startX = (windowW - effW) / 2;
    int32_t startY = (windowH - effH) / 2;
    *outStartX = startX;
    *outStartY = startY;
    *outEndX = startX + effW;
    *outEndY = startY + effH;
}

void GLCommon_beginLetterboxBlit(GLuint fbo, GLuint hostFbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hostFbo);
}

void GLCommon_endLetterboxBlit(int32_t fboWidth, int32_t fboHeight, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, GLuint hostFbo) {
    int32_t sx, sy, ex, ey;
    glClearColor(0.0, 0.0, 0.0, 1.0); //please remove if it breaks something like borders, it was just my quick-fix for the color to not be randomly changed
    GLCommon_computeLetterbox(gameW, gameH, windowW, windowH, &sx, &sy, &ex, &ey);
    glBlitFramebuffer(0, 0, fboWidth, fboHeight, sx, ey, ex, sy, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, hostFbo);
}

// ===[ Surface arrays ]===

uint32_t GLCommon_findOrAllocateSurfaceSlot(GLuint** surfaces, GLuint** surfaceTexture, int32_t** surfaceWidth, int32_t** surfaceHeight, uint32_t* count) {
    repeat(*count, i) {
        if ((*surfaces)[i] == 0)
            return i;
    }

    uint32_t newIndex = *count;
    (*count)++;
    *surfaces = (GLuint *)safeRealloc(*surfaces, *count * sizeof(GLuint));
    *surfaceTexture = (GLuint *)safeRealloc(*surfaceTexture, *count * sizeof(GLuint));
    *surfaceWidth = (int32_t *)safeRealloc(*surfaceWidth,   *count * sizeof(int32_t));
    *surfaceHeight = (int32_t *)safeRealloc(*surfaceHeight,  *count * sizeof(int32_t));
    (*surfaces)[newIndex]       = 0;
    (*surfaceTexture)[newIndex] = 0;
    (*surfaceWidth)[newIndex]   = 0;
    (*surfaceHeight)[newIndex]  = 0;
    return newIndex;
}

// Resolves a surface ID to its FBO handle and dimensions. Returns false for out-of-range or freed surfaces.
static bool resolveSurfaceFBO(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t id, GLuint* outFbo, int32_t* outW, int32_t* outH) {
    if (0 > id || (uint32_t) id >= count) return false;
    if (surfaces[id] == 0) return false;
    *outFbo = surfaces[id];
    *outW = surfaceWidth[id];
    *outH = surfaceHeight[id];
    return true;
}

void GLCommon_surfaceBlit(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t dstId, int32_t dstX, int32_t dstY, int32_t srcId, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLuint srcFbo, dstFbo;
    int32_t srcFboW, srcFboH;
    MAYBE_UNUSED int32_t dstFboW, dstFboH;

    if (!resolveSurfaceFBO(surfaces, surfaceWidth, surfaceHeight, count, srcId, &srcFbo, &srcFboW, &srcFboH))
        return;

    if (!resolveSurfaceFBO(surfaces, surfaceWidth, surfaceHeight, count, dstId, &dstFbo, &dstFboW, &dstFboH))
        return;

    int originalFramebufferBinding;

    // Yes, in OpenGL you need to use _BINDING to query things
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &originalFramebufferBinding);

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    if (scissorWasEnabled) glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);

    if (part) {
        // GL Y is bottom-up; convert GML's top-down (srcX, srcY) source rect.
        int32_t srcY0 = srcY;
        glBlitFramebuffer(srcX, srcY0, srcX + srcW, srcY0 + srcH, dstX, dstY, dstX + srcW, dstY + srcH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
        glBlitFramebuffer(0, 0, srcFboW, srcFboH, dstX, dstY, dstX + srcFboW, dstY + srcFboH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, originalFramebufferBinding);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
}

bool GLCommon_surfaceGetPixels(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t surfaceId, uint8_t* outRGBA) {
    if (0 > surfaceId || (uint32_t) surfaceId >= count)
        return false;

    if (surfaces[surfaceId] == 0)
        return false;

    int32_t w = surfaceWidth[surfaceId];
    int32_t h = surfaceHeight[surfaceId];
    if (0 >= w || 0 >= h)
        return false;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevPackAlign = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlign);

    glBindFramebuffer(GL_FRAMEBUFFER, surfaces[surfaceId]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    uint8_t* tmp = (uint8_t *)safeMalloc((size_t) w * (size_t) h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);

    // OpenGL reads bottom-up; native expects y=0 at the top
    int32_t rowBytes = w * 4;
    for (int32_t py = 0; h > py; py++) {
        memcpy(outRGBA + (size_t) py * (size_t) rowBytes, tmp + (size_t) (h - 1 - py) * (size_t) rowBytes, (size_t) rowBytes);
    }
    free(tmp);

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlign);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevFbo);
    return true;
}

#ifndef PLATFORM_PS3

// ===[ GL version queries ]===

GLVer GLCommon_getGLVersion(void) {
    GLVer v = {0, 0, false};
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (!ver) return v;
    if (strstr(ver, "OpenGL ES")) v.isGLES = true;
    const char* p = ver;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (*p) {
        v.major = *p - '0';
        ++p;
        if (*p == '.') ++p;
        v.minor = *p - '0';
    }
    return v;
}

#endif

// ===[ Blend mode translation ]===

GLenum GLCommon_blendFactorToGL(int factor) {
    switch (factor) {
        case bm_zero:           return GL_ZERO;
        default:
        case bm_one:            return GL_ONE;
        case bm_src_color:      return GL_SRC_COLOR;
        case bm_inv_src_color:  return GL_ONE_MINUS_SRC_COLOR;
        case bm_src_alpha:      return GL_SRC_ALPHA;
        case bm_inv_src_alpha:  return GL_ONE_MINUS_SRC_ALPHA;
        case bm_dest_alpha:     return GL_DST_ALPHA;
        case bm_inv_dest_alpha: return GL_ONE_MINUS_DST_ALPHA;
        case bm_dest_color:     return GL_DST_COLOR;
        case bm_inv_dest_color: return GL_ONE_MINUS_DST_COLOR;
        case bm_src_alpha_sat:  return GL_SRC_ALPHA_SATURATE;
    }
}

GLenum GLCommon_blendModeToEquation(int mode) {
    switch (mode) {
        default:
        case bm_normal:           return GL_FUNC_ADD;
        case bm_add:              return GL_FUNC_ADD;
        case bm_subtract:         return GL_FUNC_ADD;
        case bm_reverse_subtract: return GL_FUNC_REVERSE_SUBTRACT;
        case bm_min:              return GL_MIN;
        case bm_max:              return GL_FUNC_ADD;
    }
}

GLenum GLCommon_blendModeToSFactor(int mode) {
    switch (mode) {
        default:
        case bm_normal:           return GL_SRC_ALPHA;
        case bm_add:              return GL_SRC_ALPHA;
        case bm_subtract:         return GL_ZERO;
        case bm_reverse_subtract: return GL_SRC_ALPHA;
        case bm_min:              return GL_ONE;
        case bm_max:              return GL_SRC_ALPHA;
    }
}

GLenum GLCommon_blendModeToDFactor(int mode) {
    switch (mode) {
        default:
        case bm_normal:           return GL_ONE_MINUS_SRC_ALPHA;
        case bm_add:              return GL_ONE;
        case bm_subtract:         return GL_ONE_MINUS_SRC_COLOR;
        case bm_reverse_subtract: return GL_ONE;
        case bm_min:              return GL_ONE;
        case bm_max:              return GL_ONE_MINUS_SRC_COLOR;
    }
}

// ===[ Debug font helpers ]===

void GLCommon_ensureDebugFontTexture(GLuint* outTexture) {
    if (*outTexture != 0) return;
    glGenTextures(1, outTexture);
    glBindTexture(GL_TEXTURE_2D, *outTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uint8_t* rgba = (uint8_t*) safeMalloc((size_t) DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H * 4);
    for (int i = 0; i < DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H; i++) {
        uint8_t v = debugFontPixels[i];
        rgba[i * 4 + 0] = 0xFF;
        rgba[i * 4 + 1] = 0xFF;
        rgba[i * 4 + 2] = 0xFF;
        rgba[i * 4 + 3] = v;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DEBUGFONT_ATLAS_W, DEBUGFONT_ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
}

void GLCommon_drawDebugFontText(GLuint texture, const char* text, float x, float y, float xscale, float yscale, float angleDeg, uint8_t cr, uint8_t cg, uint8_t cb, float alpha, void* user, DebugFontDrawFn drawFn) {
    if (text == NULL) return;
    int32_t len = (int32_t) strlen(text);
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);
    float cursorY = 0;
    int32_t lineStart = 0;

    for (int32_t i = 0; len >= i; i++) {
        if (i == len || text[i] == '\n') {
            int32_t lineLen = i - lineStart;
            float pen = 0;
            for (int32_t j = 0; j < lineLen; j++) {
                const DebugFontGlyphEntry* glyph;
                uint8_t c = (uint8_t) text[lineStart + j];
                if (DEBUGFONT_FIRST_CP > c || c > DEBUGFONT_LAST_CP) glyph = NULL;
                else glyph = &debugFontGlyphs[c - DEBUGFONT_FIRST_CP];
                if (glyph == NULL) continue;
                if (glyph->w > 0 && glyph->h > 0) {
                    float localX0 = pen + (float) glyph->xoffset;
                    float localY0 = cursorY + (float) glyph->yoffset;
                    float localX1 = localX0 + (float) glyph->w;
                    float localY1 = localY0 + (float) glyph->h;
                    float u0 = (float) glyph->x / (float) DEBUGFONT_ATLAS_W;
                    float v0 = (float) glyph->y / (float) DEBUGFONT_ATLAS_H;
                    float u1 = (float) (glyph->x + glyph->w) / (float) DEBUGFONT_ATLAS_W;
                    float v1 = (float) (glyph->y + glyph->h) / (float) DEBUGFONT_ATLAS_H;
                    float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
                    Matrix4f_transformPoint(&transform, localX0, localY0, &sx0, &sy0);
                    Matrix4f_transformPoint(&transform, localX1, localY0, &sx1, &sy1);
                    Matrix4f_transformPoint(&transform, localX1, localY1, &sx2, &sy2);
                    Matrix4f_transformPoint(&transform, localX0, localY1, &sx3, &sy3);
                    drawFn(user, texture, sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3, u0, v0, u1, v1, cr, cg, cb, alpha);
                }
                pen += (float) glyph->xadvance;
            }
            cursorY += (float) DEBUGFONT_LINE_HEIGHT;
            lineStart = i + 1;
        }
    }
}


