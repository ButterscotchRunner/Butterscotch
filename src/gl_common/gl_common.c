#include "gl_common.h"
#include "gl_wrappers.h"

#include "stdio_compat.h"
#include <stdlib.h>
#include "string_compat.h"

#include "runner.h"
#include "utils.h"
#include "renderer.h" // for bm_* constants

#ifdef PLATFORM_PS3
#include "ps3_textures.h"
#elif PLATFORM_VITA
#include "vita_textures.h"
#endif

void GLCommon_beginFrame(GLRenderer* gl,  int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    gl->gameW = gameW;
    gl->gameH = gameH;
    gl->windowW = windowW;
    gl->windowH = windowH;

    // Bind the application surface
    int32_t appId = gl->base.runner->applicationSurfaceId;
    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[appId]);
    glViewport(0, 0, gameW, gameH);
    gl->base.CPortX = 0;
    gl->base.CPortY = 0;
    gl->base.CPortW = gameW;
    gl->base.CPortH = gameH;
}

void GLCommon_init(Renderer* renderer) {   
    GLRenderer* gl = (GLRenderer*) renderer; 
    DataWin* dataWin = renderer->dataWin;

    Matrix4f world;
    Matrix4f_identity(&world);
    renderer->gmlMatrices[MATRIX_WORLD] = world;

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(PLATFORM_VITA) && !defined(__SWITCH__) && !defined(PLATFORM_PS3)
    gl_init_wrappers();
#endif

    gl->alphaTestEnable = false;
    gl->alphaTestRef = 0.0f;
    gl->colorWriteR = true;
    gl->colorWriteG = true;
    gl->colorWriteB = true;
    gl->colorWriteA = true;

    // Prepare texture slots for lazy loading (PNG decode deferred to first use)
#ifdef PLATFORM_PS3
    // TXTR is empty on PS3; page count comes from TEXTURES.BIN.
    gl->textureCount = PS3Textures_getPageCount();
#elif defined(PLATFORM_VITA)
    if (VitaTextures_Active())
        gl->textureCount = VitaTextures_GetPageCount();
    else
        gl->textureCount = dataWin->txtr.count;
#else
    gl->textureCount = dataWin->txtr.count;
#endif

    gl->glTextures = (GLuint *)safeMalloc(gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeMalloc(gl->textureCount * sizeof(bool));

    glGenTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->textureCount > i; i++) {
        gl->textureWidths[i] = 0;
        gl->textureHeights[i] = 0;
        gl->textureLoaded[i] = false;
    }

    GlPrimitive_reset(&gl->currentPrimitive);

    // Create 1x1 white pixel texture for primitive drawing (rectangles, lines, etc.)
    glGenTextures(1, &gl->whiteTexture);
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); //I believe the old way this was done was wrong
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Save original counts so we know which slots are from data.win vs dynamic
    gl->originalTexturePageCount = gl->textureCount;
    gl->originalTpagCount = dataWin->tpag.count;
    gl->originalSpriteCount = dataWin->sprt.count;

    gl->surfaces = nullptr;
    gl->surfaceTexture = nullptr;
    gl->surfaceWidth = nullptr;
    gl->surfaceHeight = nullptr;
    gl->surfaceCount = 0;
}

void GLCommon_destroy(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*)renderer;
    GlPrimitive_reset(&gl->currentPrimitive);
    
    GLCommon_deleteDebugFontTexture(&gl->debugUI);

    glDeleteTextures(1, &gl->whiteTexture);
    glDeleteTextures((GLsizei) gl->textureCount, gl->glTextures);
    gl->textureCount = 0;
    
    for (uint32_t i = 0; gl->surfaceCount > i; i++) {
        if (gl->surfaceTexture[i] != 0) glDeleteTextures(1, &gl->surfaceTexture[i]);
        if (gl->surfaces[i] != 0) glDeleteFramebuffers(1, &gl->surfaces[i]);
    }
    gl->surfaceCount = 0;

    free(gl->surfaces);
    free(gl->surfaceTexture);
    free(gl->surfaceWidth);
    free(gl->surfaceHeight);

    free(gl->glTextures);
    free(gl->textureWidths);
    free(gl->textureHeights);
    free(gl->textureLoaded);

#ifndef PLATFORM_VITA
    free(gl->vertexData);
#endif

    free(gl);
}

void GLCommon_applyViewport(GLRenderer* gl, int32_t portX, int32_t portY, int32_t portW, int32_t portH) {
    glViewport(portX, portY, portW, portH);

    gl->base.CPortX = portX;
    gl->base.CPortY = portY;
    gl->base.CPortW = portW;
    gl->base.CPortH = portH;

    glEnable(GL_SCISSOR_TEST);
    glScissor(portX, portY, portW, portH);
}

void GLCommon_beginView(
    Renderer* renderer,
    int32_t portX, int32_t portY, int32_t portW, int32_t portH,
    GLuint activeTexture, GLApplyProjectionFunc glApplyProjection
) {
    GLRenderer* gl = (GLRenderer*) renderer;
    GLCommon_applyViewport(gl, portX, portY, portW, portH);
    int32_t viewCurrent = 0;
    if (gl->base.runner->viewsEnabled) {
        viewCurrent = gl->base.runner->viewCurrent;
    }
    RuntimeView* view = &gl->base.runner->views[viewCurrent];
    gl->base.cameraCurrent = view->cameraId;
    GMLCamera* camera = Runner_getCameraById(gl->base.runner, gl->base.cameraCurrent);
    glApplyProjection(renderer, &camera->viewMatrix,&camera->projectionMatrix);
    glActiveTexture(activeTexture);
}

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

#ifdef PLATFORM_PS3
    ey = windowH - ey;
    sy = windowH - sy;
#endif

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

// Primitive

void GlPrimitive_reset(GlPrimitive* primitive) {
    primitive->type = PRIMITIVE_NONE;
    primitive->vertexCount = 0;
    primitive->textureId = 0;
    primitive->hasTexture = false;
}

static void _primitiveBeginEx(GlPrimitive* primitive, int32_t type, GLuint textureId, GLuint fallbackTexture) {
    primitive->type = type;
    primitive->vertexCount = 0;
    primitive->hasTexture = (textureId != 0);
    primitive->textureId = primitive->hasTexture
        ? textureId
        : fallbackTexture;
}

void GLCommon_primitiveBegin(GlPrimitive* primitive, int32_t type, int32_t textureId) {
    _primitiveBeginEx(primitive, type, textureId, 0);
}

void GLCommon_primitiveBeginTexture(GLRenderer* gl, int32_t primitiveType, GLuint resolvedTexture) {
    _primitiveBeginEx(&gl->currentPrimitive, primitiveType, resolvedTexture, gl->whiteTexture);
}

bool GLCommon_primitivePrepare(
    GlPrimitive* primitive, GLuint whiteTexture,
    GLenum* mode, GLuint* textureId
) {
    if (primitive->vertexCount <= 0)
        return false;

    switch (primitive->type) {
        case PRIMITIVE_POINTS: *mode = GL_POINTS; break;
        case PRIMITIVE_LINES: *mode = GL_LINES; break;
        case PRIMITIVE_LINE_STRIP: *mode = GL_LINE_STRIP; break;
        case PRIMITIVE_TRIANGLES: *mode = GL_TRIANGLES; break;
        case PRIMITIVE_TRIANGLE_STRIP: *mode = GL_TRIANGLE_STRIP; break;
        case PRIMITIVE_TRIANGLE_FAN: *mode = GL_TRIANGLE_FAN; break;
        default: return false;
    }

    *textureId = primitive->hasTexture
        ? primitive->textureId
        : whiteTexture;

    return true;
}

void GLCommon_drawVertex(
    GLRenderer* gl,
    float x, float y, float z,
    uint32_t color, float alpha,
    float u, float v
) {
    int32_t vertexCount = gl->currentPrimitive.vertexCount;
    GlVertex* vertex = &gl->vertexData[vertexCount];

    vertex->x = x;
    vertex->y = y;
    vertex->z = z;

    vertex->u = u;
    vertex->v = v;

    vertex->r = (uint8_t)BGR_R(color);
    vertex->g = (uint8_t)BGR_G(color);
    vertex->b = (uint8_t)BGR_B(color);
    vertex->a = floatToUnormByte(alpha);

    gl->currentPrimitive.vertexCount++;
}

// ===[ Debug UI font (drawTextUI) ]===

void GLCommon_initDebugUIFont(GLDebugUIFont* ui) {
    if (ui->initialized) return;
    ui->initialized = true;

    ui->font.name = "DebugUI";
    ui->font.displayName = "DebugUI";
    ui->font.scaleX = 1.0f;
    ui->font.scaleY = 1.0f;
    ui->font.ascenderOffset = 0;
    ui->font.maxGlyphHeight = DEBUGFONT_LINE_HEIGHT;
    ui->font.emSize = (float) DEBUGFONT_LINE_HEIGHT;
    ui->font.isSpriteFont = false;
    ui->font.tpagIndex = -1;

    repeat(DEBUGFONT_GLYPH_COUNT, i) {
        const DebugFontGlyphEntry* e = &debugFontGlyphs[i];
        FontGlyph* g = &ui->glyphs[i];
        g->character = (uint16_t) (DEBUGFONT_FIRST_CP + i);
        g->sourceX = e->x;
        g->sourceY = e->y;
        g->sourceWidth = e->w;
        g->sourceHeight = e->h;
        g->shift = e->xadvance;
        g->offset = e->xoffset;
        g->kerningCount = 0;
        g->kerning = nullptr;
    }
    ui->font.glyphs = ui->glyphs;
    ui->font.glyphCount = DEBUGFONT_GLYPH_COUNT;
    Font_buildGlyphLUT(&ui->font);
}

bool GLCommon_ensureDebugFontTexture(GLDebugUIFont* ui) {
    if (ui->texture != 0) return true;

    glGenTextures(1, &ui->texture);
    if (ui->texture == 0) return false;

    size_t pixelCount = (size_t) DEBUGFONT_ATLAS_W * (size_t) DEBUGFONT_ATLAS_H;
    uint8_t* rgba = (uint8_t *)safeMalloc(pixelCount * 4);
    if (rgba == nullptr) return false;
    repeat(pixelCount, i) {
        rgba[i * 4 + 0] = 0xFF;
        rgba[i * 4 + 1] = 0xFF;
        rgba[i * 4 + 2] = 0xFF;
        rgba[i * 4 + 3] = debugFontPixels[i];
    }

    glBindTexture(GL_TEXTURE_2D, ui->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DEBUGFONT_ATLAS_W, DEBUGFONT_ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(rgba);
    return true;
}

void GLCommon_deleteDebugFontTexture(GLDebugUIFont* ui) {
    if (ui->texture != 0) {
        glDeleteTextures(1, &ui->texture);
        ui->texture = 0;
    }
}
