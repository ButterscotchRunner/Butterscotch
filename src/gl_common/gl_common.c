#include "gl_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runner.h"
#include "utils.h"
#include "renderer.h" // for bm_* constants

// ===[ Surface stack ]===

void GLCommon_initStack(int32_t* stack) {
    repeat(MAX_SURFACES, i) {
        stack[i] = -1;
    }
}

int32_t GLCommon_findStackSlot(int32_t* stack) {
    repeat(MAX_SURFACES, i) {
        if (stack[i] == -1)
            return i;
    }
    return -1;
}

int32_t GLCommon_findStackTop(int32_t* stack) {
    for (int32_t i = MAX_SURFACES - 1; i >= 0; i--) {
        if (stack[i] != -1) return i;
    }
    return -1;
}

void GLCommon_popStack(int32_t* stack) {
    for (int32_t i = MAX_SURFACES - 1; i >= 0; i--) {
        if (stack[i] != -1) {
            stack[i] = -1;
            return;
        }
    }
}

// ===[ Main FBO ]===

void GLCommon_resizeMainFBO(GLuint* fboTexture, GLuint fbo, int32_t* fboWidth, int32_t* fboHeight, int32_t width, int32_t height) {
    if (*fboTexture != 0)
        glDeleteTextures(1, fboTexture);

    glGenTextures(1, fboTexture);
    glBindTexture(GL_TEXTURE_2D, *fboTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *fboTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "GL: Main FBO incomplete (status=0x%X)\n", status);
    }

    *fboWidth = width;
    *fboHeight = height;
    fprintf(stderr, "GL: FBO resized to %dx%d\n", width, height);
}

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

void GLCommon_letterboxBlit(GLuint fbo, int32_t fboWidth, int32_t fboHeight, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    int32_t sx, sy, ex, ey;
    GLCommon_computeLetterbox(gameW, gameH, windowW, windowH, &sx, &sy, &ex, &ey);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, fboWidth, fboHeight, sx, sy, ex, ey, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ===[ Surface arrays ]===

uint32_t GLCommon_findOrAllocateSurfaceSlot(GLuint** surfaces, GLuint** surfaceTexture, int32_t** surfaceWidth, int32_t** surfaceHeight, uint32_t* count) {
    repeat(*count, i) {
        if ((*surfaces)[i] == 0)
            return i;
    }

    uint32_t newIndex = *count;
    (*count)++;
    *surfaces = safeRealloc(*surfaces,       *count * sizeof(GLuint));
    *surfaceTexture = safeRealloc(*surfaceTexture, *count * sizeof(GLuint));
    *surfaceWidth = safeRealloc(*surfaceWidth,   *count * sizeof(int32_t));
    *surfaceHeight = safeRealloc(*surfaceHeight,  *count * sizeof(int32_t));
    (*surfaces)[newIndex]       = 0;
    (*surfaceTexture)[newIndex] = 0;
    (*surfaceWidth)[newIndex]   = 0;
    (*surfaceHeight)[newIndex]  = 0;
    return newIndex;
}

// Resolves a surface ID to its FBO handle and dimensions. id == -1 picks the
// main FBO. Returns false for out-of-range or freed surfaces.
static bool resolveSurfaceFBO(GLuint mainFbo, int32_t mainFboWidth, int32_t mainFboHeight, GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t id, GLuint* outFbo, int32_t* outW, int32_t* outH) {
    if (id == -1) {
        *outFbo = mainFbo;
        *outW = mainFboWidth;
        *outH = mainFboHeight;
        return true;
    }
    if (0 > id || (uint32_t) id >= count) return false;
    if (surfaces[id] == 0) return false;
    *outFbo = surfaces[id];
    *outW = surfaceWidth[id];
    *outH = surfaceHeight[id];
    return true;
}

void GLCommon_surfaceBlit(GLuint mainFbo, int32_t mainFboWidth, int32_t mainFboHeight, GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t dstId, int32_t dstX, int32_t dstY, int32_t srcId, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLuint srcFbo, dstFbo;
    int32_t srcFboW, srcFboH;
    MAYBE_UNUSED int32_t dstFboW, dstFboH;

    if (!resolveSurfaceFBO(mainFbo, mainFboWidth, mainFboHeight, surfaces, surfaceWidth, surfaceHeight, count, srcId, &srcFbo, &srcFboW, &srcFboH))
        return;

    if (!resolveSurfaceFBO(mainFbo, mainFboWidth, mainFboHeight, surfaces, surfaceWidth, surfaceHeight, count, dstId, &dstFbo, &dstFboW, &dstFboH))
        return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);

    if (part) {
        // GL Y is bottom-up; convert GML's top-down (srcX, srcY) source rect.
        int32_t srcY0 = srcFboH - srcY - srcH;
        glBlitFramebuffer(srcX, srcY0, srcX + srcW, srcY0 + srcH, dstX, dstY, dstX + srcW, dstY + srcH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
        glBlitFramebuffer(0, 0, srcFboW, srcFboH, dstX, dstY, dstX + srcFboW, dstY + srcFboH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
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

    uint8_t* tmp = safeMalloc((size_t) w * (size_t) h * 4);
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

// ===[ Blend mode translation ]===

GLenum GLCommon_blendFactorToGL(int factor) {
    switch (factor) {
        case bm_zero:           return GL_ZERO;
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
        default:                return GL_ONE;
    }
}

GLenum GLCommon_blendModeToEquation(int mode) {
    switch (mode) {
        case bm_normal:           return GL_FUNC_ADD;
        case bm_add:              return GL_FUNC_ADD;
        case bm_subtract:         return GL_FUNC_ADD;
        case bm_reverse_subtract: return GL_FUNC_REVERSE_SUBTRACT;
        case bm_min:              return GL_MIN;
        case bm_max:              return GL_FUNC_ADD;
        default:                  return GL_FUNC_ADD;
    }
}

GLenum GLCommon_blendModeToSFactor(int mode) {
    switch (mode) {
        case bm_normal:           return GL_SRC_ALPHA;
        case bm_add:              return GL_SRC_ALPHA;
        case bm_subtract:         return GL_ZERO;
        case bm_reverse_subtract: return GL_SRC_ALPHA;
        case bm_min:              return GL_ONE;
        case bm_max:              return GL_SRC_ALPHA;
        default:                  return GLCommon_blendFactorToGL(mode);
    }
}

GLenum GLCommon_blendModeToDFactor(int mode) {
    switch (mode) {
        case bm_normal:           return GL_ONE_MINUS_SRC_ALPHA;
        case bm_add:              return GL_ONE;
        case bm_subtract:         return GL_ONE_MINUS_SRC_COLOR;
        case bm_reverse_subtract: return GL_ONE;
        case bm_min:              return GL_ONE;
        case bm_max:              return GL_ONE_MINUS_SRC_COLOR;
        default:                  return GLCommon_blendFactorToGL(mode);
    }
}
