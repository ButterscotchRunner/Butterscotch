#include "sdl_renderer.h"

#include "common.h"
#include "data_win.h"
#include "image_decoder.h"
#include "log.h"
#include "runner.h"
#include "text_utils.h"
#include "utils.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#define SDL_PIXEL_FORMAT_BGRA32 SDL_PIXELFORMAT_ARGB8888

typedef struct {
    Renderer base;

    SDL_Window* window;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* framebufferTex;
    uint32_t* framebuffer;
    int32_t framebufferW;
    int32_t framebufferH;

    SDL_Surface** pageSurfaces;
    int32_t* pageWidths;
    int32_t* pageHeights;
    uint32_t pageCount;

    int32_t *surfaceWidths;
    int32_t *surfaceHeights;
    bool *surfaceExistsFlag;
    uint32_t surfaceCount;
    uint32_t surfaceCapacity;

    bool blendEnable;
    int32_t blendMode;
    BlendFactors blendFactors;
    bool alphaTestEnable;
    uint8_t alphaTestRef;
    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;
    bool fogEnable;
    uint32_t fogColor;
} SDLRenderer;

static void sdlLogStub(const char* fnName) {
    logInfo("SDL: stubbed %s\n", fnName);
}

static void sdlEnsureSurfaceCapacity(SDLRenderer* sdl, uint32_t needed) {
    if (needed <= sdl->surfaceCapacity) return;
    uint32_t newCap = sdl->surfaceCapacity ? sdl->surfaceCapacity * 2 : 16;
    while (newCap < needed) newCap *= 2;

    sdl->surfaceWidths = (int32_t*)safeRealloc(sdl->surfaceWidths, newCap * sizeof(int32_t));
    sdl->surfaceHeights = (int32_t*)safeRealloc(sdl->surfaceHeights, newCap * sizeof(int32_t));
    sdl->surfaceExistsFlag = (bool*)safeRealloc(sdl->surfaceExistsFlag, newCap * sizeof(bool));

    for (uint32_t i = sdl->surfaceCapacity; i < newCap; i++) {
        sdl->surfaceWidths[i] = 0;
        sdl->surfaceHeights[i] = 0;
        sdl->surfaceExistsFlag[i] = false;
    }

    sdl->surfaceCapacity = newCap;
    if (needed > sdl->surfaceCount) sdl->surfaceCount = needed;
}

static void sdlEnsureFrameBuffer(SDLRenderer* sdl, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    if (sdl->framebuffer != NULL && sdl->framebufferW == width && sdl->framebufferH == height) return;

    free(sdl->framebuffer);
    sdl->framebuffer = (uint32_t*)safeCalloc((size_t)width * (size_t)height, sizeof(uint32_t));
    sdl->framebufferW = width;
    sdl->framebufferH = height;
}

static bool sdlLoadTexturePage(SDLRenderer* sdl, uint32_t pageId) {
    if (pageId >= sdl->pageCount) return false;
    if (sdl->pageSurfaces[pageId] != NULL) return true;

    DataWin* dw = sdl->base.dataWin;
    if (dw == NULL || dw->txtr.textures == NULL) return false;
    Texture* txtr = &dw->txtr.textures[pageId];
    DataWin_loadTxtrIfNeeded(dw, pageId);

    int32_t w = 0;
    int32_t h = 0;
    uint8_t* rgba = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0), &w, &h);
    if (rgba == NULL) {
        logWarn("SDL: Failed to decode TXTR page %u\n", pageId);
        return false;
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surf == NULL) {
        free(rgba);
        return false;
    }

    if (surf->pitch != (int)(w * 4)) {
        SDL_LockSurface(surf);
    }
    memcpy(surf->pixels, rgba, (size_t) w * (size_t) h * 4);
    if (surf->pitch == (int)(w * 4)) {
        /* no-op */
    } else {
        SDL_UnlockSurface(surf);
    }

    free(rgba);
    sdl->pageSurfaces[pageId] = surf;
    sdl->pageWidths[pageId] = w;
    sdl->pageHeights[pageId] = h;
    return true;
}

static inline uint8_t sdlClampByte(float value) {
    if (value < 0.0f) return 0;
    if (value > 255.0f) return 255;
    return (uint8_t) value;
}

static inline uint8_t sdlAlphaBlendComponent(uint8_t dst, uint8_t src, uint8_t a) {
    return (uint8_t)((src * a + dst * (255 - a)) / 255);
}

static inline uint32_t sdlAlphaBlendPixel(uint32_t dst, uint32_t src, uint8_t alpha) {
    if (alpha >= 255) return src;
    uint8_t sr = (uint8_t)((src >> 16) & 0xFF);
    uint8_t sg = (uint8_t)((src >> 8) & 0xFF);
    uint8_t sb = (uint8_t)(src & 0xFF);
    uint8_t sa = (uint8_t)((src >> 24) & 0xFF);
    uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
    uint8_t dg = (uint8_t)((dst >> 8) & 0xFF);
    uint8_t db = (uint8_t)(dst & 0xFF);
    uint8_t da = (uint8_t)((dst >> 24) & 0xFF);
    uint8_t outA = (uint8_t)((sa * alpha + da * (255 - alpha)) / 255);
    uint8_t outR = (uint8_t)((sr * alpha + dr * (255 - alpha)) / 255);
    uint8_t outG = (uint8_t)((sg * alpha + dg * (255 - alpha)) / 255);
    uint8_t outB = (uint8_t)((sb * alpha + db * (255 - alpha)) / 255);
    return ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
}

static void sdlBlitSurfaceToFramebuffer(SDLRenderer* sdl, SDL_Surface* src, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH, float xscale, float yscale, uint32_t color, float alpha) {
    if (src == NULL || sdl->framebuffer == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    uint8_t colR = (uint8_t)BGR_R(color);
    uint8_t colG = (uint8_t)BGR_G(color);
    uint8_t colB = (uint8_t)BGR_B(color);
    uint8_t colA = sdlClampByte(alpha * 255.0f);
    bool flipX = xscale < 0.0f;
    bool flipY = yscale < 0.0f;

    int32_t xMin = dstX;
    int32_t xMax = dstX + dstW;
    int32_t yMin = dstY;
    int32_t yMax = dstY + dstH;

    if (xMin < 0) xMin = 0;
    if (yMin < 0) yMin = 0;
    if (xMax > sdl->framebufferW) xMax = sdl->framebufferW;
    if (yMax > sdl->framebufferH) yMax = sdl->framebufferH;

    uint8_t* srcPixels = (uint8_t*)src->pixels;
    for (int32_t y = yMin; y < yMax; ++y) {
        int32_t relY = y - dstY;
        int32_t sampleY = (int32_t)((float)relY / (float)dstH * (float)srcH);
        int32_t srcYIndex = flipY ? srcY + srcH - 1 - sampleY : srcY + sampleY;
        if (srcYIndex < 0) srcYIndex = 0;
        if (srcYIndex >= src->h) continue;

        for (int32_t x = xMin; x < xMax; ++x) {
            int32_t relX = x - dstX;
            int32_t sampleX = (int32_t)((float)relX / (float)dstW * (float)srcW);
            int32_t srcXIndex = flipX ? srcX + srcW - 1 - sampleX : srcX + sampleX;
            if (srcXIndex < 0) srcXIndex = 0;
            if (srcXIndex >= src->w) continue;

            int32_t srcIndex = (srcYIndex * src->w + srcXIndex) * 4;
            uint8_t r = srcPixels[srcIndex + 0];
            uint8_t g = srcPixels[srcIndex + 1];
            uint8_t b = srcPixels[srcIndex + 2];
            uint8_t a = srcPixels[srcIndex + 3];
            uint32_t srcPixel = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            if (colA != 255 || colR != 255 || colG != 255 || colB != 255) {
                uint8_t mulR = (uint8_t)((r * colR + 127) / 255);
                uint8_t mulG = (uint8_t)((g * colG + 127) / 255);
                uint8_t mulB = (uint8_t)((b * colB + 127) / 255);
                srcPixel = ((uint32_t)a << 24) | ((uint32_t)mulR << 16) | ((uint32_t)mulG << 8) | (uint32_t)mulB;
            }

            if (alpha < 1.0f) {
                int32_t dstIndex = y * sdl->framebufferW + x;
                uint32_t dstColor = sdl->framebuffer[dstIndex];
                uint8_t outA = (uint8_t)((a * colA + 127) / 255);
                uint8_t outR = (uint8_t)((((srcPixel >> 16) & 0xFF) * outA + ((dstColor >> 16) & 0xFF) * (255 - outA)) / 255);
                uint8_t outG = (uint8_t)((((srcPixel >> 8) & 0xFF) * outA + ((dstColor >> 8) & 0xFF) * (255 - outA)) / 255);
                uint8_t outB = (uint8_t)((((srcPixel) & 0xFF) * outA + ((dstColor) & 0xFF) * (255 - outA)) / 255);
                sdl->framebuffer[dstIndex] = ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
            } else {
                sdl->framebuffer[y * sdl->framebufferW + x] = srcPixel;
            }
        }
    }
}

static void sdlFillRect(SDLRenderer* sdl, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, float alpha) {
    if (sdl->framebuffer == NULL) return;
    int32_t xMin = (x0 < x1) ? x0 : x1;
    int32_t xMax = (x0 < x1) ? x1 : x0;
    int32_t yMin = (y0 < y1) ? y0 : y1;
    int32_t yMax = (y0 < y1) ? y1 : y0;
    if (xMin < 0) xMin = 0;
    if (yMin < 0) yMin = 0;
    if (xMax > sdl->framebufferW) xMax = sdl->framebufferW;
    if (yMax > sdl->framebufferH) yMax = sdl->framebufferH;

    uint32_t outColor = color;
    uint8_t a = (uint8_t)(alpha * 255.0f);
    if (a < 255) {
        for (int32_t y = yMin; y < yMax; ++y) {
            for (int32_t x = xMin; x < xMax; ++x) {
                uint32_t dst = sdl->framebuffer[y * sdl->framebufferW + x];
                uint8_t r = (uint8_t)((((color >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * (255 - a)) / 255);
                uint8_t g = (uint8_t)((((color >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * (255 - a)) / 255);
                uint8_t b = (uint8_t)(((color & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255);
                sdl->framebuffer[y * sdl->framebufferW + x] = ((uint32_t)0xFF << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        return;
    }

    for (int32_t y = yMin; y < yMax; ++y) {
        for (int32_t x = xMin; x < xMax; ++x) {
            sdl->framebuffer[y * sdl->framebufferW + x] = outColor;
        }
    }
}

static SDLRenderer* g_currentSDLRenderer = NULL;

Renderer* SDLRenderer_getCurrent(void) {
    return g_currentSDLRenderer != NULL ? (Renderer*)g_currentSDLRenderer : NULL;
}

void SDLRenderer_presentCurrentFrame(SDL_Window* window) {
    SDLRenderer* sdl = g_currentSDLRenderer;
    if (sdl == NULL || sdl->framebuffer == NULL || window == NULL) return;

    SDL_Surface* windowSurface = SDL_GetWindowSurface(window);
    if (windowSurface == NULL) return;

    SDL_FillRect(windowSurface, NULL, SDL_MapRGB(windowSurface->format, 0, 0, 0));

    SDL_Surface* frameSurface = SDL_CreateRGBSurfaceFrom(
        sdl->framebuffer,
        sdl->framebufferW,
        sdl->framebufferH,
        32,
        sdl->framebufferW * 4,
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000
    );
    if (frameSurface == NULL) return;

    SDL_BlitScaled(frameSurface, NULL, windowSurface, NULL);
    SDL_FreeSurface(frameSurface);
    SDL_UpdateWindowSurface(window);
}

static void sdlInit(Renderer* renderer, DataWin* dataWin) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    renderer->dataWin = dataWin;
    Matrix4f world;
    Matrix4f_identity(&world);
    renderer->gmlMatrices[MATRIX_WORLD] = world;

    sdl->pageCount = dataWin != NULL ? dataWin->txtr.count : 0;
    sdl->pageSurfaces = dataWin != NULL ? (SDL_Surface**)safeCalloc(sdl->pageCount ? sdl->pageCount : 1, sizeof(SDL_Surface*)) : NULL;
    sdl->pageWidths = dataWin != NULL ? (int32_t*)safeCalloc(sdl->pageCount ? sdl->pageCount : 1, sizeof(int32_t)) : NULL;
    sdl->pageHeights = dataWin != NULL ? (int32_t*)safeCalloc(sdl->pageCount ? sdl->pageCount : 1, sizeof(int32_t)) : NULL;

    sdl->framebufferW = 0;
    sdl->framebufferH = 0;
    sdl->window = NULL;
    sdl->sdlRenderer = NULL;
    sdl->framebufferTex = NULL;
    sdl->framebuffer = NULL;

    sdl->blendEnable = true;
    sdl->blendMode = bm_normal;
    sdl->blendFactors.src = bm_src_alpha;
    sdl->blendFactors.dst = bm_inv_src_alpha;
    sdl->blendFactors.srcAlpha = bm_src_alpha;
    sdl->blendFactors.dstAlpha = bm_inv_src_alpha;
    sdl->alphaTestEnable = false;
    sdl->alphaTestRef = 0;
    sdl->colorWriteR = sdl->colorWriteG = sdl->colorWriteB = sdl->colorWriteA = true;
    sdl->fogEnable = false;
    sdl->fogColor = 0;

    logInfo("SDL renderer initialized\n");
}

static void sdlDestroy(Renderer* renderer) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;

    if (sdl->framebufferTex) { SDL_DestroyTexture(sdl->framebufferTex); sdl->framebufferTex = NULL; }
    if (sdl->sdlRenderer) { SDL_DestroyRenderer(sdl->sdlRenderer); sdl->sdlRenderer = NULL; }
    if (sdl->window) { SDL_DestroyWindow(sdl->window); sdl->window = NULL; }

    if (sdl->pageSurfaces != NULL) {
        for (uint32_t i = 0; i < sdl->pageCount; ++i) {
            if (sdl->pageSurfaces[i] != NULL) {
                SDL_FreeSurface(sdl->pageSurfaces[i]);
            }
        }
        free(sdl->pageSurfaces);
    }
    free(sdl->pageWidths);
    free(sdl->pageHeights);
    free(sdl->framebuffer);
    free(sdl->surfaceWidths);
    free(sdl->surfaceHeights);
    free(sdl->surfaceExistsFlag);
    free(sdl);
}

static void sdlBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, MAYBE_UNUSED int32_t windowW, MAYBE_UNUSED int32_t windowH) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdlEnsureFrameBuffer(sdl, gameW, gameH);
    if (sdl->framebuffer != NULL) {
        memset(sdl->framebuffer, 0, (size_t)sdl->framebufferW * (size_t)sdl->framebufferH * sizeof(uint32_t));
    }
}
static void sdlEndFrameInit(Renderer* renderer) {
    (void)renderer;
}
static void sdlEndFrameEnd(Renderer* renderer) {
    (void)renderer;
}
static void sdlBeginView(Renderer* renderer, MAYBE_UNUSED int32_t viewX, MAYBE_UNUSED int32_t viewY, MAYBE_UNUSED int32_t viewW, MAYBE_UNUSED int32_t viewH, MAYBE_UNUSED int32_t portX, MAYBE_UNUSED int32_t portY, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH, MAYBE_UNUSED float viewAngle) {
    (void)renderer;
}
static void sdlEndView(Renderer* renderer) {
    (void)renderer;
}
static void sdlApplyProjection(Renderer* renderer, MAYBE_UNUSED const Matrix4f* viewMatrix, MAYBE_UNUSED const Matrix4f* projectionMatrix) {
    (void)renderer;
}
static void sdlBeginGUI(Renderer* renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH, MAYBE_UNUSED int32_t portX, MAYBE_UNUSED int32_t portY, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH, MAYBE_UNUSED int32_t targetSurfaceId) {
    (void)renderer;
}
static void sdlSetGuiProjection(Renderer* renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH, MAYBE_UNUSED bool renderingToUserSurface) {
    (void)renderer;
}
static void sdlEndGUI(Renderer* renderer) {
    (void)renderer;
}

static void sdlDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, MAYBE_UNUSED float angleDeg, uint32_t color, float alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;
    if (dw == NULL || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || pageId >= (int32_t)sdl->pageCount) return;
    if (!sdlLoadTexturePage(sdl, (uint32_t)pageId)) return;

    SDL_Surface* pageSurf = sdl->pageSurfaces[pageId];
    int32_t srcX = tpag->sourceX;
    int32_t srcY = tpag->sourceY;
    int32_t srcW = tpag->sourceWidth;
    int32_t srcH = tpag->sourceHeight;
    int32_t dstX = (int32_t)floorf(x - originX * xscale + tpag->targetX * xscale);
    int32_t dstY = (int32_t)floorf(y - originY * yscale + tpag->targetY * yscale);
    int32_t dstW = (int32_t)floorf((float)tpag->targetWidth * fabsf(xscale));
    int32_t dstH = (int32_t)floorf((float)tpag->targetHeight * fabsf(yscale));

    if (dstW <= 0 || dstH <= 0) return;
    sdlBlitSurfaceToFramebuffer(sdl, pageSurf, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, xscale, yscale, color, alpha);
}

static void sdlDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED float pivotX, MAYBE_UNUSED float pivotY, uint32_t color, float alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;
    if (dw == NULL || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || pageId >= (int32_t)sdl->pageCount) return;
    if (!sdlLoadTexturePage(sdl, (uint32_t)pageId)) return;

    int32_t dstX = (int32_t)floorf(x);
    int32_t dstY = (int32_t)floorf(y);
    int32_t dstW = (int32_t)floorf((float)srcW * fabsf(xscale));
    int32_t dstH = (int32_t)floorf((float)srcH * fabsf(yscale));

    if (dstW <= 0 || dstH <= 0) return;
    sdlBlitSurfaceToFramebuffer(sdl, sdl->pageSurfaces[pageId], tpag->sourceX + srcOffX, tpag->sourceY + srcOffY, srcW, srcH, dstX, dstY, dstW, dstH, xscale, yscale, color, alpha);
}

static void sdlDrawSpritePartColor(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED float pivotX, MAYBE_UNUSED float pivotY, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha) {
    (void)color2; (void)color3; (void)color4;
    sdlDrawSpritePart(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY, color1, alpha);
}
static void sdlDrawSpritePos(Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1, MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED float x3, MAYBE_UNUSED float y3, MAYBE_UNUSED float x4, MAYBE_UNUSED float y4, MAYBE_UNUSED float alpha) {
    (void)renderer;
    sdlLogStub("sdlDrawSpritePos");
}
static void sdlDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, MAYBE_UNUSED bool outline) {
    sdlFillRect((SDLRenderer*)renderer, (int32_t)floorf(x1), (int32_t)floorf(y1), (int32_t)floorf(x2), (int32_t)floorf(y2), color, alpha);
}
static void sdlDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha, MAYBE_UNUSED bool outline) {
    sdlDrawRectangle(renderer, x1, y1, x2, y2, color1, alpha, false);
}
static void sdlDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, MAYBE_UNUSED float width, uint32_t color, float alpha) { sdlFillRect((SDLRenderer*)renderer, (int32_t)floorf(x1), (int32_t)floorf(y1), (int32_t)floorf(x2), (int32_t)floorf(y2), color, alpha); }
static void sdlDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (sdl->framebuffer == NULL) return;

    if (outline) {
        sdlDrawLine(renderer, x1, y1, x2, y2, 1.0f, color1, alpha);
        sdlDrawLine(renderer, x2, y2, x3, y3, 1.0f, color2, alpha);
        sdlDrawLine(renderer, x3, y3, x1, y1, 1.0f, color3, alpha);
        return;
    }

    float xMin = fminf(x1, fminf(x2, x3));
    float xMax = fmaxf(x1, fmaxf(x2, x3));
    float yMin = fminf(y1, fminf(y2, y3));
    float yMax = fmaxf(y1, fmaxf(y2, y3));

    int32_t minX = (int32_t)floorf(xMin);
    int32_t maxX = (int32_t)ceilf(xMax);
    int32_t minY = (int32_t)floorf(yMin);
    int32_t maxY = (int32_t)ceilf(yMax);

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > sdl->framebufferW) maxX = sdl->framebufferW;
    if (maxY > sdl->framebufferH) maxY = sdl->framebufferH;

    float denom = ((y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3));
    if (denom == 0.0f) return;

    float aR = (float)BGR_R(color1);
    float aG = (float)BGR_G(color1);
    float aB = (float)BGR_B(color1);
    float bR = (float)BGR_R(color2);
    float bG = (float)BGR_G(color2);
    float bB = (float)BGR_B(color2);
    float cR = (float)BGR_R(color3);
    float cG = (float)BGR_G(color3);
    float cB = (float)BGR_B(color3);
    float aAlpha = alpha;

    for (int32_t y = minY; y < maxY; ++y) {
        for (int32_t x = minX; x < maxX; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;

            float w1 = ((y2 - y3) * (px - x3) + (x3 - x2) * (py - y3)) / denom;
            float w2 = ((y3 - y1) * (px - x3) + (x1 - x3) * (py - y3)) / denom;
            float w3 = 1.0f - w1 - w2;

            if (w1 < 0.0f || w2 < 0.0f || w3 < 0.0f) {
                continue;
            }

            float r = w1 * aR + w2 * bR + w3 * cR;
            float g = w1 * aG + w2 * bG + w3 * cG;
            float b = w1 * aB + w2 * bB + w3 * cB;

            uint32_t dstColor = sdl->framebuffer[y * sdl->framebufferW + x];
            uint8_t dr = (uint8_t)((dstColor >> 16) & 0xFF);
            uint8_t dg = (uint8_t)((dstColor >> 8) & 0xFF);
            uint8_t db = (uint8_t)(dstColor & 0xFF);
            uint8_t da = (uint8_t)((dstColor >> 24) & 0xFF);

            uint8_t srcA = (uint8_t)(aAlpha * 255.0f);
            uint8_t srcR = (uint8_t)r;
            uint8_t srcG = (uint8_t)g;
            uint8_t srcB = (uint8_t)b;

            uint8_t outA = (uint8_t)((srcA * 255 + da * (255 - srcA)) / 255);
            uint8_t outR = (uint8_t)((srcR * srcA + dr * (255 - srcA)) / 255);
            uint8_t outG = (uint8_t)((srcG * srcA + dg * (255 - srcA)) / 255);
            uint8_t outB = (uint8_t)((srcB * srcA + db * (255 - srcA)) / 255);
            sdl->framebuffer[y * sdl->framebufferW + x] = ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
        }
    }
}
static void sdlDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, MAYBE_UNUSED float width, uint32_t color1, uint32_t color2, float alpha) { sdlDrawLine(renderer, x1, y1, x2, y2, 1.0f, color1, alpha); (void)color2; }
typedef struct {
    Font* font;
    TexturePageItem* fontTpag;
    SDL_Surface* pageSurface;
    Sprite* spriteFontSprite;
} SDLFontState;

static bool sdlResolveFontState(SDLRenderer* sdl, DataWin* dw, Font* font, SDLFontState* state) {
    memset(state, 0, sizeof(*state));
    state->font = font;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = font->tpagIndex;
        if (0 > fontTpagIndex) return false;

        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId) return false;
        if (!sdlLoadTexturePage(sdl, (uint32_t) pageId)) return false;

        state->pageSurface = sdl->pageSurfaces[pageId];
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }

    return true;
}

static bool sdlResolveGlyph(SDLRenderer* sdl, DataWin* dw, SDLFontState* state, FontGlyph* glyph, float cursorX, float cursorY,
    SDL_Surface** outSurface, int32_t* outSrcX, int32_t* outSrcY, int32_t* outSrcW, int32_t* outSrcH,
    float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;

    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex || glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIndex = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIndex) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIndex];
        int16_t pageId = glyphTpag->texturePageId;
        if (0 > pageId) return false;
        if (!sdlLoadTexturePage(sdl, (uint32_t) pageId)) return false;

        *outSurface = sdl->pageSurfaces[pageId];
        *outSrcX = glyphTpag->sourceX;
        *outSrcY = glyphTpag->sourceY;
        *outSrcW = glyphTpag->sourceWidth;
        *outSrcH = glyphTpag->sourceHeight;
        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) (int32_t) glyphTpag->targetY - (float) font->spriteOriginYAdjust;
        return true;
    }

    if (state->fontTpag == nullptr || state->pageSurface == nullptr) return false;

    *outSurface = state->pageSurface;
    *outSrcX = state->fontTpag->sourceX + glyph->sourceX;
    *outSrcY = state->fontTpag->sourceY + glyph->sourceY;
    *outSrcW = glyph->sourceWidth;
    *outSrcH = glyph->sourceHeight;
    *outLocalX0 = cursorX + (float) glyph->offset;
    *outLocalY0 = cursorY;
    return true;
}

static void sdlDrawTextInternal(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation, uint32_t color, float alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;

    if (text == nullptr || text[0] == '\0') return;
    if (0 > renderer->drawFont || dw == nullptr || dw->font.count <= (uint32_t) renderer->drawFont) return;

    Font* font = &dw->font.fonts[renderer->drawFont];
    SDLFontState fontState;
    if (!sdlResolveFontState(sdl, dw, font, &fontState)) return;

    int32_t textLen = (int32_t) strlen(text);
    if (textLen == 0) return;

    (void) angleDeg;

    int32_t lineCount = TextUtils_countLines(text, textLen);
    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    float valignOffset = 0.0f;
    if (renderer->drawValign == 1) valignOffset = -((float) lineCount * lineStride) / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -((float) lineCount * lineStride);

    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    while (textLen >= lineStart) {
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }

        int32_t lineLen = lineEnd - lineStart;
        const char* line = text + lineStart;

        float lineWidth = TextUtils_measureLineWidth(font, line, lineLen);
        float halignOffset = 0.0f;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(line, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(line, lineLen, &pos);

            if (glyph != nullptr && glyph->sourceWidth > 0 && glyph->sourceHeight > 0) {
                SDL_Surface* glyphSurface = NULL;
                int32_t srcX = 0, srcY = 0, srcW = 0, srcH = 0;
                float localX0 = 0.0f, localY0 = 0.0f;
                if (sdlResolveGlyph(sdl, dw, &fontState, glyph, cursorX, cursorY,
                    &glyphSurface, &srcX, &srcY, &srcW, &srcH, &localX0, &localY0)) {
                    float drawScaleX = xscale * font->scaleX;
                    float drawScaleY = yscale * font->scaleY;
                    int32_t dstX = (int32_t) floorf(x + localX0 * drawScaleX);
                    int32_t dstY = (int32_t) floorf(y + localY0 * drawScaleY);
                    int32_t dstW = (int32_t) floorf((float) srcW * fabsf(drawScaleX));
                    int32_t dstH = (int32_t) floorf((float) srcH * fabsf(drawScaleY));
                    if (dstW > 0 && dstH > 0) {
                        sdlBlitSurfaceToFramebuffer(sdl, glyphSurface, srcX, srcY, srcW, srcH,
                            dstX, dstY, dstW, dstH, drawScaleX, drawScaleY, color, alpha);
                    }
                }
            }

            cursorX += (float) glyph->shift;
            if (glyph != nullptr && hasNext) {
                cursorX += TextUtils_getKerningOffset(glyph, nextCh);
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            break;
        }
    }
}

static void sdlDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation) {
    sdlDrawTextInternal(renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation, renderer->drawColor, renderer->drawAlpha);
}
static void sdlDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha, float lineSeparation) {
    (void)c2; (void)c3; (void)c4;
    sdlDrawTextInternal(renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation, c1, alpha);
}
static void sdlFlush(Renderer* renderer) {
    (void)renderer;
}
static void sdlClearScreen(Renderer* renderer, uint32_t color, MAYBE_UNUSED float alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (sdl->framebuffer == NULL) return;
    uint32_t value = color;
    for (int32_t i = 0; i < sdl->framebufferW * sdl->framebufferH; ++i) {
        sdl->framebuffer[i] = value;
    }
}

static int32_t sdlCreateSpriteFromSurface(Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED int32_t x, MAYBE_UNUSED int32_t y, MAYBE_UNUSED int32_t w, MAYBE_UNUSED int32_t h, MAYBE_UNUSED bool removeback, MAYBE_UNUSED bool smooth, MAYBE_UNUSED int32_t xorig, MAYBE_UNUSED int32_t yorig) {
    (void)renderer;
    sdlLogStub("sdlCreateSpriteFromSurface");
    return -1;
}
static void sdlDeleteSprite(Renderer* renderer, MAYBE_UNUSED int32_t spriteIndex) {
    (void)renderer;
    sdlLogStub("sdlDeleteSprite");
}

static BlendFactors sdlGpuGetBlendFactors(Renderer* renderer) { return ((SDLRenderer*)renderer)->blendFactors; }
static int32_t sdlGpuGetBlendMode(Renderer* renderer) { return ((SDLRenderer*)renderer)->blendMode; }
static void sdlGpuSetBlendMode(Renderer* renderer, int32_t mode) { ((SDLRenderer*)renderer)->blendMode = mode; }
static void sdlGpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->blendFactors.src = sfactor;
    sdl->blendFactors.dst = dfactor;
    sdl->blendFactors.srcAlpha = sfactor_alpha;
    sdl->blendFactors.dstAlpha = dfactor_alpha;
}
static void sdlGpuSetBlendEnable(Renderer* renderer, bool enable) { ((SDLRenderer*)renderer)->blendEnable = enable; }
static void sdlGpuSetAlphaTestEnable(Renderer* renderer, bool enable) { ((SDLRenderer*)renderer)->alphaTestEnable = enable; }
static bool sdlGpuGetAlphaTestEnable(Renderer* renderer) { return ((SDLRenderer*)renderer)->alphaTestEnable; }
static void sdlGpuSetAlphaTestRef(Renderer* renderer, uint8_t ref) { ((SDLRenderer*)renderer)->alphaTestRef = ref; }
static void sdlGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->colorWriteR = red;
    sdl->colorWriteG = green;
    sdl->colorWriteB = blue;
    sdl->colorWriteA = alpha;
}
static void sdlGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (red) *red = sdl->colorWriteR;
    if (green) *green = sdl->colorWriteG;
    if (blue) *blue = sdl->colorWriteB;
    if (alpha) *alpha = sdl->colorWriteA;
}
static bool sdlGpuGetBlendEnable(Renderer* renderer) { return ((SDLRenderer*)renderer)->blendEnable; }
static void sdlGpuSetFog(Renderer* renderer, bool enable, uint32_t color) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->fogEnable = enable;
    sdl->fogColor = color;
}
static void sdlDrawTile(Renderer* renderer, MAYBE_UNUSED RoomTile* tile, MAYBE_UNUSED float offsetX, MAYBE_UNUSED float offsetY) {
    (void)renderer;
    sdlLogStub("sdlDrawTile");
}
static void sdlDrawSpriteTiled(Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED float originX, MAYBE_UNUSED float originY, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED bool tileX, MAYBE_UNUSED bool tileY, MAYBE_UNUSED float roomW, MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    (void)renderer;
    sdlLogStub("sdlDrawSpriteTiled");
}

static int32_t sdlCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    for (uint32_t i = 0; i < sdl->surfaceCount; i++) {
        if (!sdl->surfaceExistsFlag[i]) {
            sdl->surfaceWidths[i] = width;
            sdl->surfaceHeights[i] = height;
            sdl->surfaceExistsFlag[i] = true;
            return (int32_t)i;
        }
    }
    uint32_t id = sdl->surfaceCount;
    sdlEnsureSurfaceCapacity(sdl, id + 1);
    sdl->surfaceWidths[id] = width;
    sdl->surfaceHeights[id] = height;
    sdl->surfaceExistsFlag[id] = true;
    return (int32_t)id;
}
static bool sdlSurfaceExists(Renderer* renderer, int32_t surfaceID) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return false;
    return sdl->surfaceExistsFlag[surfaceID];
}
static bool sdlSetRenderTarget(MAYBE_UNUSED Renderer* renderer, int32_t surfaceID, MAYBE_UNUSED bool implicitApplicationSurface) {
    if (surfaceID == APPLICATION_SURFACE_ID || surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) return true;
    return sdlSurfaceExists(renderer, surfaceID);
}
static int32_t sdlEnsureApplicationSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t width, MAYBE_UNUSED int32_t height) { return APPLICATION_SURFACE_ID; }
static float sdlGetSurfaceWidth(Renderer* renderer, int32_t surfaceID) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return 0.0f;
    return sdl->surfaceExistsFlag[surfaceID] ? (float)sdl->surfaceWidths[surfaceID] : 0.0f;
}
static float sdlGetSurfaceHeight(Renderer* renderer, int32_t surfaceID) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return 0.0f;
    return sdl->surfaceExistsFlag[surfaceID] ? (float)sdl->surfaceHeights[surfaceID] : 0.0f;
}
static void sdlDrawSurface(Renderer* renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, uint32_t color, float alpha) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (!sdlSurfaceExists(renderer, surfaceID) || sdl->framebuffer == NULL) return;
    if (srcWidth <= 0 || srcHeight <= 0) return;
    int32_t dstW = (int32_t)floorf((float)srcWidth * fabsf(xscale));
    int32_t dstH = (int32_t)floorf((float)srcHeight * fabsf(yscale));
    if (dstW <= 0 || dstH <= 0) return;
    sdlBlitSurfaceToFramebuffer(sdl, sdl->pageSurfaces[surfaceID], srcLeft, srcTop, srcWidth, srcHeight, (int32_t)floorf(x), (int32_t)floorf(y), dstW, dstH, xscale, yscale, color, alpha);
}
static void sdlDrawSurfaceColor(Renderer* renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha) {
    sdlDrawSurface(renderer, surfaceID, srcLeft, srcTop, srcWidth, srcHeight, x, y, xscale, yscale, angleDeg, color1, alpha);
}
static void sdlDrawSurfaceTiled(Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float roomW, MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    (void)renderer;
    sdlLogStub("sdlDrawSurfaceTiled");
}
static void sdlSurfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return;
    if (!sdl->surfaceExistsFlag[surfaceID]) return;
    sdl->surfaceWidths[surfaceID] = width;
    sdl->surfaceHeights[surfaceID] = height;
}
static void sdlSurfaceFree(Renderer* renderer, int32_t surfaceID) {
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return;
    sdl->surfaceExistsFlag[surfaceID] = false;
    sdl->surfaceWidths[surfaceID] = 0;
    sdl->surfaceHeights[surfaceID] = 0;
}
static void sdlSurfaceCopy(Renderer* renderer, MAYBE_UNUSED int32_t destSurfaceID, MAYBE_UNUSED int32_t destX, MAYBE_UNUSED int32_t destY, MAYBE_UNUSED int32_t srcSurfaceID, MAYBE_UNUSED int32_t srcX, MAYBE_UNUSED int32_t srcY, MAYBE_UNUSED int32_t srcW, MAYBE_UNUSED int32_t srcH, MAYBE_UNUSED bool part) {
    (void)renderer;
    sdlLogStub("sdlSurfaceCopy");
}
static bool sdlSurfaceGetPixels(Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED uint8_t* outRGBA) {
    (void)renderer;
    sdlLogStub("sdlSurfaceGetPixels");
    return false;
}
static void sdlDrawTiledPart(Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED int32_t srcX, MAYBE_UNUSED int32_t srcY, MAYBE_UNUSED int32_t srcW, MAYBE_UNUSED int32_t srcH, MAYBE_UNUSED float dstX, MAYBE_UNUSED float dstY, MAYBE_UNUSED float dstW, MAYBE_UNUSED float dstH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    (void)renderer;
    sdlLogStub("sdlDrawTiledPart");
}

static void sdlPrimitiveBegin(Renderer* renderer, int32_t primitiveType) {
    (void)renderer;
    (void)primitiveType;
    sdlLogStub("sdlPrimitiveBegin");
}
static void sdlPrimitiveBeginTexture(Renderer* renderer, int32_t primitiveType, int32_t texture) {
    (void)renderer;
    (void)primitiveType;
    (void)texture;
    sdlLogStub("sdlPrimitiveBeginTexture");
}
static void sdlPrimitiveEnd(Renderer* renderer) {
    (void)renderer;
    sdlLogStub("sdlPrimitiveEnd");
}
static void sdlDrawVertex(Renderer* renderer, float x, float y, float z, uint32_t color, float alpha, float u, float v) {
    (void)renderer;
    (void)x; (void)y; (void)z; (void)color; (void)alpha; (void)u; (void)v;
    sdlLogStub("sdlDrawVertex");
}
static void sdlDrawVertexBuffer(Renderer* renderer, VertexBuffer* buffer, int32_t primitive, int32_t texture, int32_t offset, int32_t count) {
    (void)renderer; (void)buffer; (void)primitive; (void)texture; (void)offset; (void)count;
    sdlLogStub("sdlDrawVertexBuffer");
}

static void sdlGpuSetShader(Renderer* renderer, int32_t shaderIndex) { renderer->currentShader = shaderIndex; }
static void sdlGpuResetShader(Renderer* renderer) { renderer->currentShader = -1; }
static int32_t sdlShaderGetUniform(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) { return -1; }
static int32_t sdlShaderGetSamplerIndex(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) { return -1; }
static void sdlShaderSetUniformF(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED float value1, MAYBE_UNUSED float value2, MAYBE_UNUSED float value3, MAYBE_UNUSED float value4) {}
static void sdlShaderSetUniformFArray(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED float* values, MAYBE_UNUSED uint32_t count) {}
static void sdlShaderSetUniformI(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t value1, MAYBE_UNUSED int32_t value2, MAYBE_UNUSED int32_t value3, MAYBE_UNUSED int32_t value4) {}
static uint32_t sdlSpriteGetTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex) { return 0; }
static uint32_t sdlSurfaceGetTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) { return 0; }
static float sdlTextureGetTexelWidth(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) { return 1.0f; }
static float sdlTextureGetTexelHeight(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) { return 1.0f; }
static bool sdlTextureGetUVs(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID, MAYBE_UNUSED float* outUVs) { return false; }
static void sdlTextureSetStage(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texID) {}
static bool sdlShaderIsCompiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shader) { return false; }
static bool sdlShadersSupported(void) { return false; }
static void sdlSetMatrix(Renderer* renderer, int32_t matrixType, Matrix4f matrix) {
    if (matrixType >= 0 && matrixType < MATRICES_MAX) {
        renderer->gmlMatrices[matrixType] = matrix;
    }
}

static RendererVtable sdlVtable;

void SDLRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color) {
    sdlClearScreen(renderer, color, 1.0f);
}

Renderer* SDLRenderer_create(void) {
    SDLRenderer* sdl = (SDLRenderer*)safeCalloc(1, sizeof(SDLRenderer));
    g_currentSDLRenderer = sdl;
    sdl->base.vtable = &sdlVtable;

    sdlVtable.init = sdlInit;
    sdlVtable.destroy = sdlDestroy;
    sdlVtable.beginFrame = sdlBeginFrame;
    sdlVtable.endFrameInit = sdlEndFrameInit;
    sdlVtable.endFrameEnd = sdlEndFrameEnd;
    sdlVtable.beginView = sdlBeginView;
    sdlVtable.endView = sdlEndView;
    sdlVtable.applyProjection = sdlApplyProjection;
    sdlVtable.beginGUI = sdlBeginGUI;
    sdlVtable.setGuiProjection = sdlSetGuiProjection;
    sdlVtable.endGUI = sdlEndGUI;
    sdlVtable.drawSprite = sdlDrawSprite;
    sdlVtable.drawSpritePart = sdlDrawSpritePart;
    sdlVtable.drawSpritePartColor = sdlDrawSpritePartColor;
    sdlVtable.drawSpritePos = sdlDrawSpritePos;
    sdlVtable.drawRectangle = sdlDrawRectangle;
    sdlVtable.drawRectangleColor = sdlDrawRectangleColor;
    sdlVtable.drawLine = sdlDrawLine;
    sdlVtable.drawTriangle = sdlDrawTriangle;
    sdlVtable.drawLineColor = sdlDrawLineColor;
    sdlVtable.drawText = sdlDrawText;
    sdlVtable.drawTextColor = sdlDrawTextColor;
    sdlVtable.primitiveBegin = sdlPrimitiveBegin;
    sdlVtable.primitiveBeginTexture = sdlPrimitiveBeginTexture;
    sdlVtable.primitiveEnd = sdlPrimitiveEnd;
    sdlVtable.drawVertex = sdlDrawVertex;
    sdlVtable.drawVertexBuffer = sdlDrawVertexBuffer;
    sdlVtable.flush = sdlFlush;
    sdlVtable.clearScreen = sdlClearScreen;
    sdlVtable.createSpriteFromSurface = sdlCreateSpriteFromSurface;
    sdlVtable.deleteSprite = sdlDeleteSprite;
    sdlVtable.gpuGetBlendFactors = sdlGpuGetBlendFactors;
    sdlVtable.gpuGetBlendMode = sdlGpuGetBlendMode;
    sdlVtable.gpuSetBlendMode = sdlGpuSetBlendMode;
    sdlVtable.gpuSetBlendModeExt = sdlGpuSetBlendModeExt;
    sdlVtable.gpuSetBlendEnable = sdlGpuSetBlendEnable;
    sdlVtable.gpuSetAlphaTestEnable = sdlGpuSetAlphaTestEnable;
    sdlVtable.gpuGetAlphaTestEnable = sdlGpuGetAlphaTestEnable;
    sdlVtable.gpuSetAlphaTestRef = sdlGpuSetAlphaTestRef;
    sdlVtable.gpuSetColorWriteEnable = sdlGpuSetColorWriteEnable;
    sdlVtable.gpuGetColorWriteEnable = sdlGpuGetColorWriteEnable;
    sdlVtable.gpuGetBlendEnable = sdlGpuGetBlendEnable;
    sdlVtable.gpuSetFog = sdlGpuSetFog;
    sdlVtable.drawTile = sdlDrawTile;
    sdlVtable.drawSpriteTiled = sdlDrawSpriteTiled;
    sdlVtable.createSurface = sdlCreateSurface;
    sdlVtable.surfaceExists = sdlSurfaceExists;
    sdlVtable.setRenderTarget = sdlSetRenderTarget;
    sdlVtable.ensureApplicationSurface = sdlEnsureApplicationSurface;
    sdlVtable.getSurfaceWidth = sdlGetSurfaceWidth;
    sdlVtable.getSurfaceHeight = sdlGetSurfaceHeight;
    sdlVtable.drawSurface = sdlDrawSurface;
    sdlVtable.drawSurfaceColor = sdlDrawSurfaceColor;
    sdlVtable.drawSurfaceTiled = sdlDrawSurfaceTiled;
    sdlVtable.surfaceResize = sdlSurfaceResize;
    sdlVtable.surfaceFree = sdlSurfaceFree;
    sdlVtable.surfaceCopy = sdlSurfaceCopy;
    sdlVtable.surfaceGetPixels = sdlSurfaceGetPixels;
    sdlVtable.drawTiledPart = sdlDrawTiledPart;
    sdlVtable.gpuSetShader = sdlGpuSetShader;
    sdlVtable.gpuResetShader = sdlGpuResetShader;
    sdlVtable.shaderGetUniform = sdlShaderGetUniform;
    sdlVtable.shaderGetSamplerIndex = sdlShaderGetSamplerIndex;
    sdlVtable.shaderSetUniformF = sdlShaderSetUniformF;
    sdlVtable.shaderSetUniformFArray = sdlShaderSetUniformFArray;
    sdlVtable.shaderSetUniformI = sdlShaderSetUniformI;
    sdlVtable.spriteGetTexture = sdlSpriteGetTexture;
    sdlVtable.surfaceGetTexture = sdlSurfaceGetTexture;
    sdlVtable.textureGetTexelWidth = sdlTextureGetTexelWidth;
    sdlVtable.textureGetTexelHeight = sdlTextureGetTexelHeight;
    sdlVtable.textureGetUVs = sdlTextureGetUVs;
    sdlVtable.textureSetStage = sdlTextureSetStage;
    sdlVtable.shaderIsCompiled = sdlShaderIsCompiled;
    sdlVtable.shadersSupported = sdlShadersSupported;
    sdlVtable.setMatrix = sdlSetMatrix;

    sdl->base.drawColor = 0xFFFFFF;
    sdl->base.drawAlpha = 1.0f;
    sdl->base.drawFont = -1;
    sdl->base.drawHalign = 0;
    sdl->base.drawValign = 0;
    sdl->base.circlePrecision = 24;
    sdl->base.currentShader = -1;
    Matrix4f_identity(&sdl->base.gmlMatrices[MATRIX_WORLD]);
    sdl->blendEnable = true;
    sdl->blendMode = bm_normal;
    sdl->blendFactors.src = bm_src_alpha;
    sdl->blendFactors.dst = bm_inv_src_alpha;
    sdl->blendFactors.srcAlpha = bm_src_alpha;
    sdl->blendFactors.dstAlpha = bm_inv_src_alpha;
    sdl->alphaTestEnable = false;
    sdl->alphaTestRef = 0;
    sdl->colorWriteR = sdl->colorWriteG = sdl->colorWriteB = sdl->colorWriteA = true;
    sdl->fogEnable = false;
    sdl->fogColor = 0;

    return (Renderer*)sdl;
}
