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

// #define SDL_TRACE_ENABLED

#ifdef SDL_TRACE_ENABLED
#define SDL_TRACE_CALL() logInfo("SDL: trace %s\n", __func__)
#else
#define SDL_TRACE_CALL() ((void)0)
#endif

#define SDL_PIXEL_FORMAT_BGRA32 SDL_PIXELFORMAT_ARGB8888

typedef struct {
    Renderer base;

    SDL_Window* window;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* framebufferTex;
    uint32_t* framebuffer;
    int32_t framebufferW;
    int32_t framebufferH;

    int32_t viewX;
    int32_t viewY;
    int32_t viewW;
    int32_t viewH;
    int32_t portX;
    int32_t portY;
    int32_t portW;
    int32_t portH;
    bool hasView;

    SDL_Surface** pageSurfaces;
    int32_t* pageWidths;
    int32_t* pageHeights;
    uint32_t pageCount;
    uint32_t originalTexturePageCount;

    SDL_Surface** surfaceSurfaces;
    int32_t *surfaceWidths;
    int32_t *surfaceHeights;
    bool *surfaceExistsFlag;
    uint32_t surfaceCount;
    uint32_t surfaceCapacity;
    uint32_t originalTpagCount;
    uint32_t originalSpriteCount;

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
    SDL_TRACE_CALL();
    logInfo("SDL: stubbed %s\n", fnName);
}

static void sdlEnsureSurfaceCapacity(SDLRenderer* sdl, uint32_t needed) {
    SDL_TRACE_CALL();
    if (needed <= sdl->surfaceCapacity) return;
    uint32_t newCap = sdl->surfaceCapacity ? sdl->surfaceCapacity * 2 : 16;
    while (newCap < needed) newCap *= 2;

    sdl->surfaceSurfaces = (SDL_Surface**)safeRealloc(sdl->surfaceSurfaces, newCap * sizeof(SDL_Surface*));
    sdl->surfaceWidths = (int32_t*)safeRealloc(sdl->surfaceWidths, newCap * sizeof(int32_t));
    sdl->surfaceHeights = (int32_t*)safeRealloc(sdl->surfaceHeights, newCap * sizeof(int32_t));
    sdl->surfaceExistsFlag = (bool*)safeRealloc(sdl->surfaceExistsFlag, newCap * sizeof(bool));

    for (uint32_t i = sdl->surfaceCapacity; i < newCap; i++) {
        sdl->surfaceSurfaces[i] = NULL;
        sdl->surfaceWidths[i] = 0;
        sdl->surfaceHeights[i] = 0;
        sdl->surfaceExistsFlag[i] = false;
    }

    sdl->surfaceCapacity = newCap;
    if (needed > sdl->surfaceCount) sdl->surfaceCount = needed;
}

static uint32_t sdlFindOrAllocTexturePageSlot(SDLRenderer* sdl) {
    SDL_TRACE_CALL();
    for (uint32_t i = sdl->originalTexturePageCount; i < sdl->pageCount; ++i) {
        if (sdl->pageSurfaces[i] == NULL) return i;
    }

    uint32_t newPageId = sdl->pageCount;
    sdl->pageCount++;
    sdl->pageSurfaces = (SDL_Surface**)safeRealloc(sdl->pageSurfaces, sdl->pageCount * sizeof(SDL_Surface*));
    sdl->pageWidths = (int32_t*)safeRealloc(sdl->pageWidths, sdl->pageCount * sizeof(int32_t));
    sdl->pageHeights = (int32_t*)safeRealloc(sdl->pageHeights, sdl->pageCount * sizeof(int32_t));

    sdl->pageSurfaces[newPageId] = NULL;
    sdl->pageWidths[newPageId] = 0;
    sdl->pageHeights[newPageId] = 0;
    return newPageId;
}

static uint32_t sdlFindOrAllocTpagSlot(SDLRenderer* sdl, DataWin* dw) {
    SDL_TRACE_CALL();
    for (uint32_t i = sdl->originalTpagCount; i < dw->tpag.count; ++i) {
        if (dw->tpag.items[i].texturePageId == -1) return i;
    }

    uint32_t newIndex = dw->tpag.count;
    dw->tpag.count++;
    dw->tpag.items = (TexturePageItem*)safeRealloc(dw->tpag.items, dw->tpag.count * sizeof(TexturePageItem));
    memset(&dw->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dw->tpag.items[newIndex].texturePageId = -1;
    return newIndex;
}

static void sdlEnsureFrameBuffer(SDLRenderer* sdl, int32_t width, int32_t height) {
    SDL_TRACE_CALL();
    if (width <= 0 || height <= 0) return;
    if (sdl->framebuffer != NULL && sdl->framebufferW == width && sdl->framebufferH == height) return;

    free(sdl->framebuffer);
    sdl->framebuffer = (uint32_t*)safeCalloc((size_t)width * (size_t)height, sizeof(uint32_t));
    sdl->framebufferW = width;
    sdl->framebufferH = height;
}

static bool sdlLoadTexturePage(SDLRenderer* sdl, uint32_t pageId) {
    SDL_TRACE_CALL();
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

    SDL_LockSurface(surf);
    uint8_t* dstPixels = (uint8_t*)surf->pixels;
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            const int32_t srcIndex = (y * w + x) * 4;
            const int32_t dstIndex = (y * w + x) * 4;
            dstPixels[dstIndex + 0] = rgba[srcIndex + 2]; // B
            dstPixels[dstIndex + 1] = rgba[srcIndex + 1]; // G
            dstPixels[dstIndex + 2] = rgba[srcIndex + 0]; // R
            dstPixels[dstIndex + 3] = rgba[srcIndex + 3]; // A
        }
    }
    SDL_UnlockSurface(surf);

    free(rgba);
    sdl->pageSurfaces[pageId] = surf;
    sdl->pageWidths[pageId] = w;
    sdl->pageHeights[pageId] = h;
    return true;
}

static inline uint8_t sdlClampByte(float value) {
    // SDL_TRACE_CALL();
    if (value < 0.0f) return 0;
    if (value > 255.0f) return 255;
    return (uint8_t) value;
}

static inline uint8_t sdlAlphaBlendComponent(uint8_t dst, uint8_t src, uint8_t alpha) {
    SDL_TRACE_CALL();
    return (uint8_t)((src * alpha + dst * (255 - alpha)) / 255);
}

static inline uint32_t sdlAlphaBlendPixel(uint32_t dst, uint32_t src, uint8_t alpha) {
    // SDL_TRACE_CALL();
    if (alpha >= 255) return src;
    uint8_t sr = (uint8_t)((src >> 16) & 0xFF);
    uint8_t sg = (uint8_t)((src >> 8) & 0xFF);
    uint8_t sb = (uint8_t)(src & 0xFF);
    uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
    uint8_t dg = (uint8_t)((dst >> 8) & 0xFF);
    uint8_t db = (uint8_t)(dst & 0xFF);
    uint8_t da = (uint8_t)((dst >> 24) & 0xFF);
    uint8_t outA = (uint8_t)(alpha + ((da * (255 - alpha)) / 255));
    uint8_t outR = (uint8_t)((sr * alpha + dr * (255 - alpha)) / 255);
    uint8_t outG = (uint8_t)((sg * alpha + dg * (255 - alpha)) / 255);
    uint8_t outB = (uint8_t)((sb * alpha + db * (255 - alpha)) / 255);
    return ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
}

static void sdlBlitSurfaceToFramebuffer(SDLRenderer* sdl, SDL_Surface* src, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH, float xscale, float yscale, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    if (src == NULL || sdl->framebuffer == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    if (sdl->hasView && sdl->viewW > 0 && sdl->viewH > 0 && sdl->portW > 0 && sdl->portH > 0) {
        float scaleX = (float)sdl->portW / (float)sdl->viewW;
        float scaleY = (float)sdl->portH / (float)sdl->viewH;
        dstX = (int32_t)lroundf((float)(dstX - sdl->viewX) * scaleX + (float)sdl->portX);
        dstY = (int32_t)lroundf((float)(dstY - sdl->viewY) * scaleY + (float)sdl->portY);
        dstW = (int32_t)lroundf((float)dstW * scaleX);
        dstH = (int32_t)lroundf((float)dstH * scaleY);
    }

    uint8_t colR = 0, colG = 0, colB = 0;
    Renderer_unpackColorToRGB(color, &colR, &colG, &colB);
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
            uint8_t b = srcPixels[srcIndex + 0];
            uint8_t g = srcPixels[srcIndex + 1];
            uint8_t r = srcPixels[srcIndex + 2];
            uint8_t a = srcPixels[srcIndex + 3];
            uint32_t srcPixel = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            if (colA != 255 || colR != 255 || colG != 255 || colB != 255) {
                uint8_t mulR = (uint8_t)((r * colR + 127) / 255);
                uint8_t mulG = (uint8_t)((g * colG + 127) / 255);
                uint8_t mulB = (uint8_t)((b * colB + 127) / 255);
                srcPixel = ((uint32_t)a << 24) | ((uint32_t)mulR << 16) | ((uint32_t)mulG << 8) | (uint32_t)mulB;
            }

            if (alpha < 1.0f || a < 255) {
                int32_t dstIndex = y * sdl->framebufferW + x;
                uint32_t dstColor = sdl->framebuffer[dstIndex];
                uint8_t srcAlpha = sdlClampByte((float)a * alpha);
                if (srcAlpha == 0) {
                    sdl->framebuffer[dstIndex] = dstColor;
                } else {
                    sdl->framebuffer[dstIndex] = sdlAlphaBlendPixel(dstColor, srcPixel, srcAlpha);
                }
            } else {
                sdl->framebuffer[y * sdl->framebufferW + x] = srcPixel;
            }
        }
    }
}

static void sdlDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha);
static void sdlDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline);

static void sdlFillRect(SDLRenderer* sdl, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    if (sdl->framebuffer == NULL) return;
    int32_t xMin = (x0 < x1) ? x0 : x1;
    int32_t xMax = (x0 < x1) ? x1 : x0;
    int32_t yMin = (y0 < y1) ? y0 : y1;
    int32_t yMax = (y0 < y1) ? y1 : y0;

    if (sdl->hasView && sdl->viewW > 0 && sdl->viewH > 0 && sdl->portW > 0 && sdl->portH > 0) {
        float scaleX = (float)sdl->portW / (float)sdl->viewW;
        float scaleY = (float)sdl->portH / (float)sdl->viewH;
        xMin = (int32_t)lroundf((float)(xMin - sdl->viewX) * scaleX + (float)sdl->portX);
        xMax = (int32_t)lroundf((float)(xMax - sdl->viewX) * scaleX + (float)sdl->portX);
        yMin = (int32_t)lroundf((float)(yMin - sdl->viewY) * scaleY + (float)sdl->portY);
        yMax = (int32_t)lroundf((float)(yMax - sdl->viewY) * scaleY + (float)sdl->portY);
    }

    if (xMin < 0) xMin = 0;
    if (yMin < 0) yMin = 0;
    if (xMax > sdl->framebufferW) xMax = sdl->framebufferW;
    if (yMax > sdl->framebufferH) yMax = sdl->framebufferH;

    uint8_t rectR = 0, rectG = 0, rectB = 0;
    Renderer_unpackColorToRGB(color, &rectR, &rectG, &rectB);
    uint32_t outColor = ((uint32_t)0xFFu << 24) | ((uint32_t)rectR << 16) | ((uint32_t)rectG << 8) | (uint32_t)rectB;
    uint8_t a = sdlClampByte(alpha * 255.0f);
    if (a < 255) {
        for (int32_t y = yMin; y < yMax; ++y) {
            for (int32_t x = xMin; x < xMax; ++x) {
                uint32_t dst = sdl->framebuffer[y * sdl->framebufferW + x];
                sdl->framebuffer[y * sdl->framebufferW + x] = sdlAlphaBlendPixel(dst, outColor, a);
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

typedef struct {
    bool active;
    int32_t primitiveType;
    int32_t texture;
    float x[256];
    float y[256];
    float z[256];
    float u[256];
    float v[256];
    uint32_t color[256];
    float alpha[256];
    uint32_t count;
} SDLPrimitiveState;

static SDLPrimitiveState g_sdlPrimitiveState = {0};

static void sdlPrimitiveFlush(Renderer* renderer) {
    SDL_TRACE_CALL();
    if (!g_sdlPrimitiveState.active || g_sdlPrimitiveState.count == 0) return;

    uint32_t count = g_sdlPrimitiveState.count;
    int32_t primitiveType = g_sdlPrimitiveState.primitiveType;

    for (uint32_t i = 0; i + 1 < count; ++i) {
        if (primitiveType == PRIMITIVE_LINES || primitiveType == PRIMITIVE_LINE_STRIP) {
            uint32_t j = (primitiveType == PRIMITIVE_LINE_STRIP && i + 1 == count) ? i : i + 1;
            sdlDrawLine(renderer, g_sdlPrimitiveState.x[i], g_sdlPrimitiveState.y[i], g_sdlPrimitiveState.x[j], g_sdlPrimitiveState.y[j], 1.0f, g_sdlPrimitiveState.color[i], g_sdlPrimitiveState.alpha[i]);
            if (primitiveType == PRIMITIVE_LINES) ++i;
        } else if (primitiveType == PRIMITIVE_TRIANGLES) {
            if (i + 2 < count) {
                float x1 = g_sdlPrimitiveState.x[i], y1 = g_sdlPrimitiveState.y[i];
                float x2 = g_sdlPrimitiveState.x[i + 1], y2 = g_sdlPrimitiveState.y[i + 1];
                float x3 = g_sdlPrimitiveState.x[i + 2], y3 = g_sdlPrimitiveState.y[i + 2];
                sdlDrawTriangle(renderer, x1, y1, x2, y2, x3, y3,
                    g_sdlPrimitiveState.color[i], g_sdlPrimitiveState.color[i + 1], g_sdlPrimitiveState.color[i + 2],
                    g_sdlPrimitiveState.alpha[i], false);
                i += 2;
            }
        } else if (primitiveType == PRIMITIVE_TRIANGLE_STRIP) {
            if (i + 2 < count) {
                float x1 = g_sdlPrimitiveState.x[i], y1 = g_sdlPrimitiveState.y[i];
                float x2 = g_sdlPrimitiveState.x[i + 1], y2 = g_sdlPrimitiveState.y[i + 1];
                float x3 = g_sdlPrimitiveState.x[i + 2], y3 = g_sdlPrimitiveState.y[i + 2];
                sdlDrawTriangle(renderer, x1, y1, x2, y2, x3, y3,
                    g_sdlPrimitiveState.color[i], g_sdlPrimitiveState.color[i + 1], g_sdlPrimitiveState.color[i + 2],
                    g_sdlPrimitiveState.alpha[i], false);
            }
        } else if (primitiveType == PRIMITIVE_TRIANGLE_FAN) {
            if (i + 2 < count) {
                float x1 = g_sdlPrimitiveState.x[0], y1 = g_sdlPrimitiveState.y[0];
                float x2 = g_sdlPrimitiveState.x[i + 1], y2 = g_sdlPrimitiveState.y[i + 1];
                float x3 = g_sdlPrimitiveState.x[i + 2], y3 = g_sdlPrimitiveState.y[i + 2];
                sdlDrawTriangle(renderer, x1, y1, x2, y2, x3, y3,
                    g_sdlPrimitiveState.color[0], g_sdlPrimitiveState.color[i + 1], g_sdlPrimitiveState.color[i + 2],
                    g_sdlPrimitiveState.alpha[0], false);
            }
        }
    }

    g_sdlPrimitiveState.count = 0;
    g_sdlPrimitiveState.active = false;
    g_sdlPrimitiveState.primitiveType = PRIMITIVE_NONE;
    g_sdlPrimitiveState.texture = 0;
}

static SDLRenderer* g_currentSDLRenderer = NULL;

Renderer* SDLRenderer_getCurrent(void) {
    SDL_TRACE_CALL();
    return g_currentSDLRenderer != NULL ? (Renderer*)g_currentSDLRenderer : NULL;
}

void SDLRenderer_presentCurrentFrame(SDL_Window* window) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = g_currentSDLRenderer;
    if (sdl == NULL || sdl->framebuffer == NULL || window == NULL) return;

    SDL_Surface* windowSurface = SDL_GetWindowSurface(window);
    if (windowSurface == NULL) return;

    int32_t windowW = windowSurface->w;
    int32_t windowH = windowSurface->h;
    if (windowW <= 0 || windowH <= 0) {
        SDL_GetWindowSize(window, &windowW, &windowH);
    }
    if (windowW <= 0 || windowH <= 0) return;

    int32_t effW = windowW;
    int32_t effH = windowH;
    int32_t startX = 0;
    int32_t startY = 0;

    if ((sdl->framebufferW * windowH) / sdl->framebufferH < windowW) {
        effW = (sdl->framebufferW * windowH) / sdl->framebufferH;
        effH = windowH;
    } else {
        effW = windowW;
        effH = (sdl->framebufferH * windowW) / sdl->framebufferW;
    }
    startX = (windowW - effW) / 2;
    startY = (windowH - effH) / 2;

    SDL_FillRect(windowSurface, NULL, SDL_MapRGB(windowSurface->format, 0, 0, 0));

    SDL_Surface* frameSurface = SDL_CreateRGBSurfaceWithFormatFrom(
        sdl->framebuffer,
        sdl->framebufferW,
        sdl->framebufferH,
        32,
        sdl->framebufferW * 4,
        SDL_PIXELFORMAT_ARGB8888
    );
    if (frameSurface == NULL) return;

    SDL_Rect dstRect = { startX, startY, effW, effH };
    SDL_BlitScaled(frameSurface, NULL, windowSurface, &dstRect);
    SDL_FreeSurface(frameSurface);
    SDL_UpdateWindowSurface(window);
}

static void sdlInit(Renderer* renderer, DataWin* dataWin) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    renderer->dataWin = dataWin;
    Matrix4f world;
    Matrix4f_identity(&world);
    renderer->gmlMatrices[MATRIX_WORLD] = world;

    sdl->originalTexturePageCount = dataWin != NULL ? dataWin->txtr.count : 0;
    sdl->pageCount = sdl->originalTexturePageCount;
    sdl->pageSurfaces = dataWin != NULL ? (SDL_Surface**)safeCalloc(sdl->pageCount ? sdl->pageCount : 1, sizeof(SDL_Surface*)) : NULL;
    sdl->pageWidths = dataWin != NULL ? (int32_t*)safeCalloc(sdl->pageCount ? sdl->pageCount : 1, sizeof(int32_t)) : NULL;
    sdl->pageHeights = dataWin != NULL ? (int32_t*)safeCalloc(sdl->pageCount ? sdl->pageCount : 1, sizeof(int32_t)) : NULL;
    sdl->originalTpagCount = dataWin != NULL ? dataWin->tpag.count : 0;
    sdl->originalSpriteCount = dataWin != NULL ? dataWin->sprt.count : 0;

    sdl->surfaceSurfaces = NULL;
    sdl->framebufferW = 0;
    sdl->framebufferH = 0;
    sdl->viewX = 0;
    sdl->viewY = 0;
    sdl->viewW = 0;
    sdl->viewH = 0;
    sdl->portX = 0;
    sdl->portY = 0;
    sdl->portW = 0;
    sdl->portH = 0;
    sdl->hasView = false;
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
    SDL_TRACE_CALL();
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
    if (sdl->surfaceSurfaces != NULL) {
        for (uint32_t i = 0; i < sdl->surfaceCount; ++i) {
            if (sdl->surfaceSurfaces[i] != NULL) {
                SDL_FreeSurface(sdl->surfaceSurfaces[i]);
            }
        }
        free(sdl->surfaceSurfaces);
    }
    free(sdl->surfaceWidths);
    free(sdl->surfaceHeights);
    free(sdl->surfaceExistsFlag);
    free(sdl);
}

static void sdlBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, MAYBE_UNUSED int32_t windowW, MAYBE_UNUSED int32_t windowH) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->hasView = false;
    sdlEnsureFrameBuffer(sdl, gameW, gameH);
    if (sdl->framebuffer != NULL) {
        uint32_t clearColor = 0xFF000000u;
        for (int32_t i = 0; i < sdl->framebufferW * sdl->framebufferH; ++i) {
            sdl->framebuffer[i] = clearColor;
        }
    }
}
static void sdlEndFrameInit(Renderer* renderer) {
    SDL_TRACE_CALL();
    (void)renderer;
}
static void sdlEndFrameEnd(Renderer* renderer) {
    SDL_TRACE_CALL();
    (void)renderer;
}
static void sdlBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, MAYBE_UNUSED int32_t viewW, MAYBE_UNUSED int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, MAYBE_UNUSED float viewAngle) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->viewX = viewX;
    sdl->viewY = viewY;
    sdl->viewW = viewW;
    sdl->viewH = viewH;
    sdl->portX = portX;
    sdl->portY = portY;
    sdl->portW = portW;
    sdl->portH = portH;
    sdl->hasView = true;
}
static void sdlEndView(Renderer* renderer) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->hasView = false;
}
static void sdlApplyProjection(Renderer* renderer, MAYBE_UNUSED const Matrix4f* viewMatrix, MAYBE_UNUSED const Matrix4f* projectionMatrix) {
    SDL_TRACE_CALL();
    (void)renderer;
}
static void sdlBeginGUI(Renderer* renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH, MAYBE_UNUSED int32_t portX, MAYBE_UNUSED int32_t portY, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH, MAYBE_UNUSED int32_t targetSurfaceId) {
    SDL_TRACE_CALL();
    (void)renderer;
}
static void sdlSetGuiProjection(Renderer* renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH, MAYBE_UNUSED bool renderingToUserSurface) {
    SDL_TRACE_CALL();
    (void)renderer;
}
static void sdlEndGUI(Renderer* renderer) {
    SDL_TRACE_CALL();
    (void)renderer;
}

static void sdlDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, MAYBE_UNUSED float angleDeg, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;
    if (dw == NULL || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || pageId >= (int32_t)sdl->pageCount) return;
    if (!sdlLoadTexturePage(sdl, (uint32_t)pageId)) return;

    int32_t dstX = 0, dstY = 0, dstW = 0, dstH = 0;
    Renderer_computeSpriteDrawRect(tpag, x, y, originX, originY, xscale, yscale, &dstX, &dstY, &dstW, &dstH);

    if (dstW <= 0 || dstH <= 0) return;
    sdlBlitSurfaceToFramebuffer(sdl, sdl->pageSurfaces[pageId], tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight, dstX, dstY, dstW, dstH, xscale, yscale, color, alpha);
}

static void sdlDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED float pivotX, MAYBE_UNUSED float pivotY, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;
    if (dw == NULL || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || pageId >= (int32_t)sdl->pageCount) return;
    if (!sdlLoadTexturePage(sdl, (uint32_t)pageId)) return;

    float cx0 = 0.0f, cy0 = 0.0f, cx1 = 0.0f, cy1 = 0.0f, cx2 = 0.0f, cy2 = 0.0f, cx3 = 0.0f, cy3 = 0.0f;
    Renderer_computeSpritePartQuad(srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY, &cx0, &cy0, &cx1, &cy1, &cx2, &cy2, &cx3, &cy3);

    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    Renderer_computeQuadBounds(cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3, &minX, &minY, &maxX, &maxY);
    int32_t dstX = (int32_t)floorf(minX);
    int32_t dstY = (int32_t)floorf(minY);
    int32_t dstW = (int32_t)ceilf(maxX - minX);
    int32_t dstH = (int32_t)ceilf(maxY - minY);

    if (dstW <= 0 || dstH <= 0) return;
    sdlBlitSurfaceToFramebuffer(sdl, sdl->pageSurfaces[pageId], tpag->sourceX + srcOffX, tpag->sourceY + srcOffY, srcW, srcH, dstX, dstY, dstW, dstH, xscale, yscale, color, alpha);
}

static void sdlDrawSpritePartColor(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED float pivotX, MAYBE_UNUSED float pivotY, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha) {
    SDL_TRACE_CALL();
    (void)color2; (void)color3; (void)color4;
    sdlDrawSpritePart(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY, color1, alpha);
}
static void sdlDrawSpritePos(Renderer* renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;
    if (dw == NULL || 0 > tpagIndex || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || (uint32_t)pageId >= sdl->pageCount) return;
    if (!sdlLoadTexturePage(sdl, (uint32_t)pageId)) return;

    SDL_Surface* pageSurf = sdl->pageSurfaces[pageId];
    if (pageSurf == NULL) return;

    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    Renderer_computeQuadBounds(x1, y1, x2, y2, x3, y3, x4, y4, &minX, &minY, &maxX, &maxY);
    int32_t xMin = (int32_t)floorf(minX);
    int32_t xMax = (int32_t)ceilf(maxX);
    int32_t yMin = (int32_t)floorf(minY);
    int32_t yMax = (int32_t)ceilf(maxY);

    if (xMin < 0) xMin = 0;
    if (yMin < 0) yMin = 0;
    if (xMax > sdl->framebufferW) xMax = sdl->framebufferW;
    if (yMax > sdl->framebufferH) yMax = sdl->framebufferH;

    float denom = (x2 - x4) * (y3 - y1) - (x3 - x1) * (y2 - y4);
    if (denom == 0.0f) {
        denom = 1.0f;
    }

    for (int32_t y = yMin; y < yMax; ++y) {
        for (int32_t x = xMin; x < xMax; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;

            float a = ((x2 - x4) * (py - y4) - (y2 - y4) * (px - x4)) / denom;
            float b = ((x3 - x1) * (py - y1) - (y3 - y1) * (px - x1)) / denom;
            float c = 1.0f - a - b;

            if (a < 0.0f || b < 0.0f || c < 0.0f || a > 1.0f || b > 1.0f || c > 1.0f) {
                continue;
            }

            int32_t srcX = tpag->sourceX + (int32_t)floorf((float)tpag->sourceWidth * a);
            int32_t srcY = tpag->sourceY + (int32_t)floorf((float)tpag->sourceHeight * b);
            if (srcX < 0) srcX = 0;
            if (srcY < 0) srcY = 0;
            if (srcX >= pageSurf->w) continue;
            if (srcY >= pageSurf->h) continue;

            int32_t srcIndex = (srcY * pageSurf->w + srcX) * 4;
            uint8_t* srcBytes = (uint8_t*)pageSurf->pixels + srcIndex;
            uint8_t bChannel = srcBytes[0];
            uint8_t g = srcBytes[1];
            uint8_t r = srcBytes[2];
            uint8_t a8 = srcBytes[3];

            uint32_t dstColor = sdl->framebuffer[y * sdl->framebufferW + x];
            uint8_t dr = (uint8_t)((dstColor >> 16) & 0xFF);
            uint8_t dg = (uint8_t)((dstColor >> 8) & 0xFF);
            uint8_t db = (uint8_t)(dstColor & 0xFF);
            uint8_t da = (uint8_t)((dstColor >> 24) & 0xFF);

            uint8_t outA = (uint8_t)((a8 * alpha + da * (255 - a8 * alpha / 255)) / 255);
            uint8_t outR = (uint8_t)((r * a8 + dr * (255 - a8)) / 255);
            uint8_t outG = (uint8_t)((g * a8 + dg * (255 - a8)) / 255);
            uint8_t outB = (uint8_t)((bChannel * a8 + db * (255 - a8)) / 255);

            sdl->framebuffer[y * sdl->framebufferW + x] = ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
        }
    }
}
static void sdlDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, MAYBE_UNUSED bool outline) {
    SDL_TRACE_CALL();
    sdlFillRect((SDLRenderer*)renderer, (int32_t)floorf(x1), (int32_t)floorf(y1), (int32_t)floorf(x2), (int32_t)floorf(y2), color, alpha);
}
static void sdlDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha, MAYBE_UNUSED bool outline) {
    SDL_TRACE_CALL();
    sdlDrawRectangle(renderer, x1, y1, x2, y2, color1, alpha, false);
}
static void sdlDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, MAYBE_UNUSED float width, uint32_t color, float alpha) { SDL_TRACE_CALL(); sdlFillRect((SDLRenderer*)renderer, (int32_t)floorf(x1), (int32_t)floorf(y1), (int32_t)floorf(x2), (int32_t)floorf(y2), color, alpha); }
static void sdlDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (sdl->framebuffer == NULL) return;

    if (outline) {
        sdlDrawLine(renderer, x1, y1, x2, y2, 1.0f, color1, alpha);
        sdlDrawLine(renderer, x2, y2, x3, y3, 1.0f, color2, alpha);
        sdlDrawLine(renderer, x3, y3, x1, y1, 1.0f, color3, alpha);
        return;
    }

    float xMin = 0.0f, yMin = 0.0f, xMax = 0.0f, yMax = 0.0f;
    Renderer_computeQuadBounds(x1, y1, x2, y2, x3, y3, x3, y3, &xMin, &yMin, &xMax, &yMax);

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
static void sdlDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, MAYBE_UNUSED float width, uint32_t color1, uint32_t color2, float alpha) { SDL_TRACE_CALL(); sdlDrawLine(renderer, x1, y1, x2, y2, 1.0f, color1, alpha); (void)color2; }
typedef struct {
    Font* font;
    TexturePageItem* fontTpag;
    SDL_Surface* pageSurface;
    Sprite* spriteFontSprite;
} SDLFontState;

static bool sdlResolveFontState(SDLRenderer* sdl, DataWin* dw, Font* font, SDLFontState* state) {
    SDL_TRACE_CALL();
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
    SDL_TRACE_CALL();
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
    SDL_TRACE_CALL();
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
    SDL_TRACE_CALL();
    sdlDrawTextInternal(renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation, renderer->drawColor, renderer->drawAlpha);
}
static void sdlDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha, float lineSeparation) {
    SDL_TRACE_CALL();
    (void)c2; (void)c3; (void)c4;
    sdlDrawTextInternal(renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation, c1, alpha);
}
static void sdlFlush(Renderer* renderer) {
    SDL_TRACE_CALL();
    (void)renderer;
}
static void sdlClearScreen(Renderer* renderer, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (sdl->framebuffer == NULL) return;

    uint8_t alphaByte = sdlClampByte(alpha * 255.0f);
    uint32_t value = ((uint32_t)alphaByte << 24) | (uint32_t)color;

    for (int32_t i = 0; i < sdl->framebufferW * sdl->framebufferH; ++i) {
        sdl->framebuffer[i] = value;
    }
}

static int32_t sdlCreateSpriteFromSurface(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;

    if (dw == NULL) return -1;
    if (0 >= w || 0 >= h) return -1;
    if (0 > surfaceID || (uint32_t) surfaceID >= sdl->surfaceCount) return -1;
    if (!sdl->surfaceExistsFlag[surfaceID] || sdl->surfaceSurfaces[surfaceID] == NULL) return -1;

    SDL_Surface* srcSurf = sdl->surfaceSurfaces[surfaceID];
    int32_t srcX = x;
    int32_t srcY = y;
    int32_t srcW = w;
    int32_t srcH = h;

    if (srcX < 0) { srcW += srcX; srcX = 0; }
    if (srcY < 0) { srcH += srcY; srcY = 0; }
    if (srcX + srcW > srcSurf->w) srcW = srcSurf->w - srcX;
    if (srcY + srcH > srcSurf->h) srcH = srcSurf->h - srcY;
    if (srcW <= 0 || srcH <= 0) return -1;

    SDL_Surface* spriteSurf = SDL_CreateRGBSurfaceWithFormat(0, srcW, srcH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (spriteSurf == NULL) return -1;

    SDL_Rect srcRect = { srcX, srcY, srcW, srcH };
    SDL_Rect dstRect = { 0, 0, srcW, srcH };
    if (SDL_BlitSurface(srcSurf, &srcRect, spriteSurf, &dstRect) != 0) {
        SDL_FreeSurface(spriteSurf);
        return -1;
    }

    uint32_t pageId = sdlFindOrAllocTexturePageSlot(sdl);
    SDL_FreeSurface(sdl->pageSurfaces[pageId]);
    sdl->pageSurfaces[pageId] = spriteSurf;
    sdl->pageWidths[pageId] = srcW;
    sdl->pageHeights[pageId] = srcH;

    uint32_t tpagIndex = sdlFindOrAllocTpagSlot(sdl, dw);
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t)srcW;
    tpag->sourceHeight = (uint16_t)srcH;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t)srcW;
    tpag->targetHeight = (uint16_t)srcH;
    tpag->boundingWidth = (uint16_t)srcW;
    tpag->boundingHeight = (uint16_t)srcH;
    tpag->texturePageId = (int16_t)pageId;

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, sdl->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    sprite->width = (uint32_t)srcW;
    sprite->height = (uint32_t)srcH;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = (int32_t*)safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t)tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = NULL;

    (void)removeback;
    (void)smooth;
    logInfo("SDL: Created dynamic sprite %u (%dx%d) from surface %d at (%d,%d)\n", spriteIndex, srcW, srcH, surfaceID, x, y);
    return (int32_t)spriteIndex;
}
static void sdlDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    DataWin* dw = renderer->dataWin;

    if (dw == NULL || 0 > spriteIndex || dw->sprt.count <= (uint32_t)spriteIndex) return;
    if (sdl->originalSpriteCount > (uint32_t)spriteIndex) {
        logWarn("SDL: Cannot delete data.win sprite %d\n", spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return;

    for (uint32_t i = 0; i < sprite->textureCount; ++i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t)tpagIdx >= sdl->originalTpagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && (uint32_t)pageId < sdl->pageCount && sdl->pageSurfaces[pageId] != NULL) {
                SDL_FreeSurface(sdl->pageSurfaces[pageId]);
                sdl->pageSurfaces[pageId] = NULL;
                sdl->pageWidths[pageId] = 0;
                sdl->pageHeights[pageId] = 0;
            }
            tpag->texturePageId = -1;
        }
    }

    free(sprite->tpagIndices);
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;
}

static BlendFactors sdlGpuGetBlendFactors(Renderer* renderer) { SDL_TRACE_CALL(); return ((SDLRenderer*)renderer)->blendFactors; }
static int32_t sdlGpuGetBlendMode(Renderer* renderer) { SDL_TRACE_CALL(); return ((SDLRenderer*)renderer)->blendMode; }
static void sdlGpuSetBlendMode(Renderer* renderer, int32_t mode) { SDL_TRACE_CALL(); ((SDLRenderer*)renderer)->blendMode = mode; }
static void sdlGpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->blendFactors.src = sfactor;
    sdl->blendFactors.dst = dfactor;
    sdl->blendFactors.srcAlpha = sfactor_alpha;
    sdl->blendFactors.dstAlpha = dfactor_alpha;
}
static void sdlGpuSetBlendEnable(Renderer* renderer, bool enable) { SDL_TRACE_CALL(); ((SDLRenderer*)renderer)->blendEnable = enable; }
static void sdlGpuSetAlphaTestEnable(Renderer* renderer, bool enable) { SDL_TRACE_CALL(); ((SDLRenderer*)renderer)->alphaTestEnable = enable; }
static bool sdlGpuGetAlphaTestEnable(Renderer* renderer) { SDL_TRACE_CALL(); return ((SDLRenderer*)renderer)->alphaTestEnable; }
static void sdlGpuSetAlphaTestRef(Renderer* renderer, uint8_t ref) { SDL_TRACE_CALL(); ((SDLRenderer*)renderer)->alphaTestRef = ref; }
static void sdlGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->colorWriteR = red;
    sdl->colorWriteG = green;
    sdl->colorWriteB = blue;
    sdl->colorWriteA = alpha;
}
static void sdlGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (red) *red = sdl->colorWriteR;
    if (green) *green = sdl->colorWriteG;
    if (blue) *blue = sdl->colorWriteB;
    if (alpha) *alpha = sdl->colorWriteA;
}
static bool sdlGpuGetBlendEnable(Renderer* renderer) { SDL_TRACE_CALL(); return ((SDLRenderer*)renderer)->blendEnable; }
static void sdlGpuSetFog(Renderer* renderer, bool enable, uint32_t color) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    sdl->fogEnable = enable;
    sdl->fogColor = color;
}
static void sdlDrawTile(Renderer* renderer, RoomTile* tile, float offsetX, float offsetY) {
    SDL_TRACE_CALL();
    if (tile == NULL || renderer == NULL || renderer->dataWin == NULL) return;

    int32_t tpagIndex = Renderer_resolveObjectTPAGIndex(renderer->dataWin, tile);
    if (0 > tpagIndex) return;

    TexturePageItem* tpag = &renderer->dataWin->tpag.items[tpagIndex];

    int32_t srcX = tile->sourceX;
    int32_t srcY = tile->sourceY;
    int32_t srcW = (int32_t) tile->width;
    int32_t srcH = (int32_t) tile->height;
    float drawX = (float) tile->x + offsetX;
    float drawY = (float) tile->y + offsetY;

    int32_t clippedSrcX = 0;
    int32_t clippedSrcY = 0;
    int32_t clippedSrcW = 0;
    int32_t clippedSrcH = 0;
    if (!Renderer_computeTileAtlasClipping(tpag, srcX, srcY, srcW, srcH, tile->scaleX, tile->scaleY, &drawX, &drawY, &clippedSrcX, &clippedSrcY, &clippedSrcW, &clippedSrcH)) {
        return;
    }

    int32_t atlasOffX = clippedSrcX - tpag->targetX;
    int32_t atlasOffY = clippedSrcY - tpag->targetY;
    uint32_t bgr = tile->color & 0x00FFFFFF;

    renderer->vtable->drawSpritePart(renderer, tpagIndex, atlasOffX, atlasOffY, clippedSrcW, clippedSrcH, drawX, drawY, tile->scaleX, tile->scaleY, 0.0f, 0.0f, 0.0f, bgr, tile->alpha);
}
static void sdlDrawSpriteTiled(Renderer* renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    if (renderer == NULL || renderer->dataWin == NULL || 0 > tpagIndex || (uint32_t)tpagIndex >= renderer->dataWin->tpag.count) return;

    TexturePageItem* tpag = &renderer->dataWin->tpag.items[tpagIndex];
    if (tpag->boundingWidth <= 0 || tpag->boundingHeight <= 0) return;

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float)tpag->boundingWidth * axScale;
    float tileH = (float)tpag->boundingHeight * ayScale;
    if (tileW <= 0.0f || tileH <= 0.0f) return;

    float startX = 0.0f, startY = 0.0f, endX = 0.0f, endY = 0.0f;
    int32_t tilesX = 0, tilesY = 0;
    Renderer_computeTiledGrid(x, y, originX, originY, xscale, yscale, tileW, tileH, tileX, tileY, roomW, roomH, &startX, &startY, &endX, &endY, &tilesX, &tilesY);
    if (startX >= endX || startY >= endY || tilesX <= 0 || tilesY <= 0) return;

    float quadOffX0 = 0.0f, quadOffY0 = 0.0f, quadW = 0.0f, quadH = 0.0f;
    Renderer_computeTiledQuadOffsets(tpag, originX, originY, xscale, yscale, &quadOffX0, &quadOffY0, &quadW, &quadH);

    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || (uint32_t)pageId >= renderer->dataWin->tpag.count) return;
    if (!sdlLoadTexturePage((SDLRenderer*)renderer, (uint32_t)pageId)) return;
    SDL_Surface* pageSurf = ((SDLRenderer*)renderer)->pageSurfaces[pageId];
    if (pageSurf == NULL) return;

    for (int32_t row = 0; row < tilesY; ++row) {
        float dy = startY + (float) row * tileH;
        if (dy >= endY) break;
        for (int32_t col = 0; col < tilesX; ++col) {
            float dx = startX + (float) col * tileW;
            if (dx >= endX) break;

            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
            Renderer_computeTiledCellBounds(startX, startY, tileW, tileH, quadOffX0, quadOffY0, quadW, quadH, col, row, &minX, &minY, &maxX, &maxY);

            int32_t dstX = (int32_t)floorf(minX);
            int32_t dstY = (int32_t)floorf(minY);
            int32_t dstW = (int32_t)ceilf(maxX - minX);
            int32_t dstH = (int32_t)ceilf(maxY - minY);
            if (dstW <= 0 || dstH <= 0) continue;

            sdlBlitSurfaceToFramebuffer((SDLRenderer*)renderer, pageSurf,
                tpag->sourceX, tpag->sourceY,
                tpag->sourceWidth, tpag->sourceHeight,
                dstX, dstY, dstW, dstH,
                xscale, yscale, color, alpha);
        }
    }
}

static int32_t sdlCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    for (uint32_t i = 0; i < sdl->surfaceCount; i++) {
        if (!sdl->surfaceExistsFlag[i]) {
            sdl->surfaceWidths[i] = width;
            sdl->surfaceHeights[i] = height;
            sdl->surfaceSurfaces[i] = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
            sdl->surfaceExistsFlag[i] = sdl->surfaceSurfaces[i] != NULL;
            if (!sdl->surfaceExistsFlag[i]) {
                sdl->surfaceWidths[i] = 0;
                sdl->surfaceHeights[i] = 0;
            }
            return (int32_t)i;
        }
    }
    uint32_t id = sdl->surfaceCount;
    sdlEnsureSurfaceCapacity(sdl, id + 1);
    sdl->surfaceWidths[id] = width;
    sdl->surfaceHeights[id] = height;
    sdl->surfaceSurfaces[id] = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    sdl->surfaceExistsFlag[id] = sdl->surfaceSurfaces[id] != NULL;
    if (!sdl->surfaceExistsFlag[id]) {
        sdl->surfaceWidths[id] = 0;
        sdl->surfaceHeights[id] = 0;
    }
    return (int32_t)id;
}
static bool sdlSurfaceExists(Renderer* renderer, int32_t surfaceID) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return false;
    return sdl->surfaceExistsFlag[surfaceID];
}
static bool sdlSetRenderTarget(MAYBE_UNUSED Renderer* renderer, int32_t surfaceID, MAYBE_UNUSED bool implicitApplicationSurface) {
    SDL_TRACE_CALL();
    if (surfaceID == APPLICATION_SURFACE_ID || surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) return true;
    return sdlSurfaceExists(renderer, surfaceID);
}
static int32_t sdlEnsureApplicationSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t width, MAYBE_UNUSED int32_t height) { SDL_TRACE_CALL(); return APPLICATION_SURFACE_ID; }
static float sdlGetSurfaceWidth(Renderer* renderer, int32_t surfaceID) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return 0.0f;
    return sdl->surfaceExistsFlag[surfaceID] ? (float)sdl->surfaceWidths[surfaceID] : 0.0f;
}
static float sdlGetSurfaceHeight(Renderer* renderer, int32_t surfaceID) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return 0.0f;
    return sdl->surfaceExistsFlag[surfaceID] ? (float)sdl->surfaceHeights[surfaceID] : 0.0f;
}
static void sdlDrawSurface(Renderer* renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (!sdlSurfaceExists(renderer, surfaceID) || sdl->framebuffer == NULL) return;
    if (srcWidth <= 0 || srcHeight <= 0) return;
    if (sdl->surfaceSurfaces[surfaceID] == NULL) return;
    int32_t dstW = (int32_t)floorf((float)srcWidth * fabsf(xscale));
    int32_t dstH = (int32_t)floorf((float)srcHeight * fabsf(yscale));
    if (dstW <= 0 || dstH <= 0) return;
    sdlBlitSurfaceToFramebuffer(sdl, sdl->surfaceSurfaces[surfaceID], srcLeft, srcTop, srcWidth, srcHeight, (int32_t)floorf(x), (int32_t)floorf(y), dstW, dstH, xscale, yscale, color, alpha);
}
static void sdlDrawSurfaceColor(Renderer* renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha) {
    SDL_TRACE_CALL();
    sdlDrawSurface(renderer, surfaceID, srcLeft, srcTop, srcWidth, srcHeight, x, y, xscale, yscale, angleDeg, color1, alpha);
}
static void sdlDrawSurfaceTiled(Renderer* renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (!sdlSurfaceExists(renderer, surfaceID) || sdl->framebuffer == NULL) return;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount || sdl->surfaceSurfaces[surfaceID] == NULL) return;

    SDL_Surface* surf = sdl->surfaceSurfaces[surfaceID];
    float tileW = (float)surf->w * fabsf(xscale);
    float tileH = (float)surf->h * fabsf(yscale);
    if (tileW <= 0.0f || tileH <= 0.0f) return;

    float startX = 0.0f, startY = 0.0f, endX = 0.0f, endY = 0.0f;
    int32_t tilesX = 0, tilesY = 0;
    Renderer_computeSurfaceTileGrid(x, y, xscale, yscale, (float)surf->w, (float)surf->h, true, true, roomW, roomH, &startX, &startY, &endX, &endY, &tilesX, &tilesY);
    if (startX >= endX || startY >= endY || tilesX <= 0 || tilesY <= 0) return;

    float quadW = (float)surf->w * xscale;
    float quadH = (float)surf->h * yscale;
    for (int32_t row = 0; row < tilesY; ++row) {
        float dy = startY + (float) row * tileH;
        if (dy >= endY) break;
        for (int32_t col = 0; col < tilesX; ++col) {
            float dx = startX + (float) col * tileW;
            if (dx >= endX) break;

            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
            Renderer_computeTiledCellBounds(startX, startY, tileW, tileH, 0.0f, 0.0f, quadW, quadH, col, row, &minX, &minY, &maxX, &maxY);

            int32_t dstX = (int32_t)floorf(minX);
            int32_t dstY = (int32_t)floorf(minY);
            int32_t dstW = (int32_t)ceilf(maxX - minX);
            int32_t dstH = (int32_t)ceilf(maxY - minY);
            if (dstW <= 0 || dstH <= 0) continue;

            renderer->vtable->drawSurface(renderer, surfaceID, 0, 0, surf->w, surf->h, dstX, dstY, xscale, yscale, 0.0f, color, alpha);
        }
    }
}
static void sdlSurfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return;
    if (!sdl->surfaceExistsFlag[surfaceID]) return;
    if (sdl->surfaceSurfaces[surfaceID] != NULL) {
        SDL_FreeSurface(sdl->surfaceSurfaces[surfaceID]);
    }
    sdl->surfaceSurfaces[surfaceID] = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    sdl->surfaceWidths[surfaceID] = width;
    sdl->surfaceHeights[surfaceID] = height;
    sdl->surfaceExistsFlag[surfaceID] = sdl->surfaceSurfaces[surfaceID] != NULL;
}
static void sdlSurfaceFree(Renderer* renderer, int32_t surfaceID) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return;
    if (sdl->surfaceSurfaces[surfaceID] != NULL) {
        SDL_FreeSurface(sdl->surfaceSurfaces[surfaceID]);
        sdl->surfaceSurfaces[surfaceID] = NULL;
    }
    sdl->surfaceExistsFlag[surfaceID] = false;
    sdl->surfaceWidths[surfaceID] = 0;
    sdl->surfaceHeights[surfaceID] = 0;
}
static void sdlSurfaceCopy(Renderer* renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (!sdlSurfaceExists(renderer, destSurfaceID) || !sdlSurfaceExists(renderer, srcSurfaceID)) return;
    SDL_Surface* dst = sdl->surfaceSurfaces[destSurfaceID];
    SDL_Surface* src = sdl->surfaceSurfaces[srcSurfaceID];
    if (dst == NULL || src == NULL) return;

    if (!part) {
        srcX = 0; srcY = 0; srcW = src->w; srcH = src->h;
    }
    if (srcW <= 0 || srcH <= 0) return;

    SDL_Rect srcRect = { srcX, srcY, srcW, srcH };
    SDL_Rect dstRect = { destX, destY, srcW, srcH };
    SDL_BlitSurface(src, &srcRect, dst, &dstRect);
}
static bool sdlSurfaceGetPixels(Renderer* renderer, int32_t surfaceID, uint8_t* outRGBA) {
    SDL_TRACE_CALL();
    SDLRenderer* sdl = (SDLRenderer*)renderer;
    if (outRGBA == NULL || surfaceID < 0 || (uint32_t)surfaceID >= sdl->surfaceCount) return false;
    SDL_Surface* surf = sdl->surfaceSurfaces[surfaceID];
    if (surf == NULL || !sdl->surfaceExistsFlag[surfaceID]) return false;

    SDL_LockSurface(surf);
    uint8_t* pixels = (uint8_t*)surf->pixels;
    int32_t bytesPerPixel = surf->format != NULL ? surf->format->BytesPerPixel : 4;
    for (int32_t y = 0; y < surf->h; ++y) {
        for (int32_t x = 0; x < surf->w; ++x) {
            int32_t idx = (y * surf->w + x) * bytesPerPixel;
            uint32_t pixel = 0;
            switch (bytesPerPixel) {
                case 4: pixel = ((uint32_t)pixels[idx + 0]) | ((uint32_t)pixels[idx + 1] << 8) | ((uint32_t)pixels[idx + 2] << 16) | ((uint32_t)pixels[idx + 3] << 24); break;
                case 3: pixel = ((uint32_t)pixels[idx + 0]) | ((uint32_t)pixels[idx + 1] << 8) | ((uint32_t)pixels[idx + 2] << 16); break;
                default: pixel = 0; break;
            }
            uint8_t a = (uint8_t)((pixel >> 24) & 0xFF);
            uint8_t r = (uint8_t)((pixel >> 16) & 0xFF);
            uint8_t g = (uint8_t)((pixel >> 8) & 0xFF);
            uint8_t b = (uint8_t)(pixel & 0xFF);
            int32_t outIndex = (y * surf->w + x) * 4;
            outRGBA[outIndex + 0] = r;
            outRGBA[outIndex + 1] = g;
            outRGBA[outIndex + 2] = b;
            outRGBA[outIndex + 3] = a;
        }
    }
    SDL_UnlockSurface(surf);
    return true;
}
static void sdlDrawTiledPart(Renderer* renderer, int32_t tpagIndex, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, float dstX, float dstY, float dstW, float dstH, uint32_t color, float alpha) {
    SDL_TRACE_CALL();
    if (renderer == NULL || renderer->dataWin == NULL || 0 > tpagIndex || (uint32_t)tpagIndex >= renderer->dataWin->tpag.count) return;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0.0f || dstH <= 0.0f) return;

    TexturePageItem* tpag = &renderer->dataWin->tpag.items[tpagIndex];
    int32_t pageId = tpag->texturePageId;
    if (pageId < 0 || pageId >= (int32_t)((SDLRenderer*)renderer)->pageCount) return;
    if (!sdlLoadTexturePage((SDLRenderer*)renderer, (uint32_t)pageId)) return;

    int32_t cols = (int32_t)ceilf(dstW / (float)srcW);
    int32_t rows = (int32_t)ceilf(dstH / (float)srcH);
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t col = 0; col < cols; ++col) {
            float x = dstX + (float)col * (float)srcW;
            float y = dstY + (float)row * (float)srcH;
            int32_t drawW = srcW;
            int32_t drawH = srcH;
            if (x + drawW > dstX + dstW) drawW = (int32_t)floorf((dstX + dstW) - x);
            if (y + drawH > dstY + dstH) drawH = (int32_t)floorf((dstY + dstH) - y);
            if (drawW <= 0 || drawH <= 0) continue;
            renderer->vtable->drawSpritePart(renderer, tpagIndex, srcX, srcY, drawW, drawH, x, y, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, color, alpha);
        }
    }
}

static void sdlPrimitiveBegin(Renderer* renderer, int32_t primitiveType) {
    SDL_TRACE_CALL();
    (void)renderer;
    g_sdlPrimitiveState.active = true;
    g_sdlPrimitiveState.primitiveType = primitiveType;
    g_sdlPrimitiveState.texture = 0;
    g_sdlPrimitiveState.count = 0;
}
static void sdlPrimitiveBeginTexture(Renderer* renderer, int32_t primitiveType, int32_t texture) {
    SDL_TRACE_CALL();
    sdlPrimitiveBegin(renderer, primitiveType);
    g_sdlPrimitiveState.texture = texture;
}
static void sdlPrimitiveEnd(Renderer* renderer) {
    SDL_TRACE_CALL();
    sdlPrimitiveFlush(renderer);
}
static void sdlDrawVertex(Renderer* renderer, float x, float y, float z, uint32_t color, float alpha, float u, float v) {
    SDL_TRACE_CALL();
    if (!g_sdlPrimitiveState.active) return;
    if (g_sdlPrimitiveState.count >= 256) {
        sdlPrimitiveFlush(renderer);
    }
    uint32_t idx = g_sdlPrimitiveState.count++;
    g_sdlPrimitiveState.x[idx] = x;
    g_sdlPrimitiveState.y[idx] = y;
    g_sdlPrimitiveState.z[idx] = z;
    g_sdlPrimitiveState.u[idx] = u;
    g_sdlPrimitiveState.v[idx] = v;
    g_sdlPrimitiveState.color[idx] = color;
    g_sdlPrimitiveState.alpha[idx] = alpha;
}
static void sdlDrawVertexBuffer(Renderer* renderer, VertexBuffer* buffer, int32_t primitive, int32_t texture, int32_t offset, int32_t count) {
    SDL_TRACE_CALL();
    if (buffer == NULL || buffer->data == NULL || count <= 0) return;
    size_t stride = buffer->vertexSize ? buffer->vertexSize : buffer->format != NULL ? buffer->format->stride : 0;
    if (stride == 0) return;
    sdlPrimitiveBeginTexture(renderer, primitive, texture);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* base = buffer->data + (size_t)(offset + i) * stride;
        float x = 0.0f, y = 0.0f, z = 0.0f, u = 0.0f, v = 0.0f;
        uint32_t color = 0xFFFFFF;
        float alpha = 1.0f;

        if (buffer->format != NULL) {
            for (int j = 0; j < buffer->format->numElements; ++j) {
                VertexElement* e = &buffer->format->elements[j];
                uint8_t* ptr = base + e->offset;
                switch (e->usage) {
                    case VERTEX_USAGE_POSITION: {
                        float* f = (float*)ptr;
                        x = f[0];
                        y = f[1];
                        if (e->type == VERTEX_TYPE_FLOAT3) z = f[2];
                        break;
                    }
                    case VERTEX_USAGE_COLOR: {
                        uint8_t* b = ptr;
                        color = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
                        break;
                    }
                    case VERTEX_USAGE_TEXCOORD: {
                        float* f = (float*)ptr;
                        u = f[0];
                        v = f[1];
                        break;
                    }
                    default: break;
                }
            }
        }
        sdlDrawVertex(renderer, x, y, z, color, alpha, u, v);
    }
    sdlPrimitiveEnd(renderer);
}

static void sdlGpuSetShader(Renderer* renderer, int32_t shaderIndex) { SDL_TRACE_CALL(); renderer->currentShader = shaderIndex; }
static void sdlGpuResetShader(Renderer* renderer) { SDL_TRACE_CALL(); renderer->currentShader = -1; }
static int32_t sdlShaderGetUniform(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) { SDL_TRACE_CALL(); return -1; }
static int32_t sdlShaderGetSamplerIndex(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) { SDL_TRACE_CALL(); return -1; }
static void sdlShaderSetUniformF(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED float value1, MAYBE_UNUSED float value2, MAYBE_UNUSED float value3, MAYBE_UNUSED float value4) { SDL_TRACE_CALL(); }
static void sdlShaderSetUniformFArray(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED float* values, MAYBE_UNUSED uint32_t count) { SDL_TRACE_CALL(); }
static void sdlShaderSetUniformI(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t value1, MAYBE_UNUSED int32_t value2, MAYBE_UNUSED int32_t value3, MAYBE_UNUSED int32_t value4) { SDL_TRACE_CALL(); }
static uint32_t sdlSpriteGetTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex) { SDL_TRACE_CALL(); return 0; }
static uint32_t sdlSurfaceGetTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) { SDL_TRACE_CALL(); return 0; }
static float sdlTextureGetTexelWidth(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) { SDL_TRACE_CALL(); return 1.0f; }
static float sdlTextureGetTexelHeight(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) { SDL_TRACE_CALL(); return 1.0f; }
static bool sdlTextureGetUVs(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID, MAYBE_UNUSED float* outUVs) { SDL_TRACE_CALL(); return false; }
static void sdlTextureSetStage(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texID) { SDL_TRACE_CALL(); }
static bool sdlShaderIsCompiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shader) { SDL_TRACE_CALL(); return false; }
static bool sdlShadersSupported(void) { SDL_TRACE_CALL(); return false; }
static void sdlSetMatrix(Renderer* renderer, int32_t matrixType, Matrix4f matrix) {
    SDL_TRACE_CALL();
    if (matrixType >= 0 && matrixType < MATRICES_MAX) {
        renderer->gmlMatrices[matrixType] = matrix;
    }
}

static RendererVtable sdlVtable;

void SDLRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color) {
    SDL_TRACE_CALL();
    sdlClearScreen(renderer, color, 1.0f);
}

Renderer* SDLRenderer_create(void) {
    SDL_TRACE_CALL();
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
