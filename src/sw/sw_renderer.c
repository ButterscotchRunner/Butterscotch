#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <assert.h>
#include "defines.h"
#include "sw_renderer.h"
#include "text_utils.h"
#include "pixel_convert.h"
#include "image/image_decoder.h"

#define UNIMP() do { fprintf(stderr, "NYI %s\n", __func__); } while (0)
//#define UNIMP() do { } while (0)
#define UNIMP2() do { } while (0)

#ifndef M_PI
#define M_PI 3.1415926535897932384626
#endif

#define TEXTURE_LRU_LENGTH 64

#define SURFACE_MAX_COUNT 64

typedef struct
{
    uintpixel_t* buffer;
    uint16_t width, height;
}
SWTexture;

typedef struct
{
    Renderer base;
    
    // Window Properties
    uint16_t width;
    uint16_t height;
    // Framebuffer
    uintpixel_t* fb;
    uint16_t fbPitch; // in sizeof(uintpixel_t) units, NOT in bytes!
    
    bool drawingToSurface;
    uintpixel_t* mainFb;
    uint16_t mainWidth;
    uint16_t mainHeight;
    uint16_t mainPitch;
    int lastViewX, lastViewY, lastViewW, lastViewH;
    int lastPortX, lastPortY, lastPortW, lastPortH;
    int lastGameW, lastGameH, lastMaxX, lastMaxY;
    float lastScaleX, lastScaleY;
    
    SWTexture** textures;
    SWTexture** surfaces;
    uint32_t* textureIndexLRU;
    uint32_t textureIndexLRUHead;
    uint32_t textureIndexLRUTail;
    size_t textureCount;
    size_t surfaceCount;
    size_t totalTextureCount;
    size_t originalTPagCount;
    size_t originalSpriteCount;
    
    bool viewActive;
    int viewX, viewY, viewW, viewH;
    int portX, portY, portW, portH;
    int gameW, gameH, maxX, maxY;

    int offsetX, offsetY;
    float scaleX, scaleY;
    float defaultScaleX, defaultScaleY;
}
SWRenderer;

void Runner_setNextFrame(uintpixel_t* framebuffer, int width, int height);

FORCE_INLINE int swrMin(int a, int b) { return a < b ? a : b; }
FORCE_INLINE int swrMax(int a, int b) { return a > b ? a : b; }
FORCE_INLINE int swrAbs(int x) { return x < 0 ? -x : x; }

FORCE_INLINE bool opaque(uintpixel_t color)
{
#if PIXEL_SIZE == 8
    return (color != PXL_TRANSPARENT);
#else
    return (color & TRANSPARENT_MASK) != 0;
#endif
}

FORCE_INLINE uintpixel_t tint(uintpixel_t tintColor, uintpixel_t color)
{
#if PIXEL_SIZE == 32
    Pixel32ARGB x, y;
    
    if ((tintColor & 0xFFFFFF) == 0xFFFFFF)
        return color;
    
    x.l = color;
    y.l = tintColor;
    
    x.p.b = (int)x.p.b * y.p.b / 255;
    x.p.g = (int)x.p.g * y.p.g / 255;
    x.p.r = (int)x.p.r * y.p.r / 255;
    return x.l;
#elif PIXEL_SIZE == 16
    if ((tintColor & 0x7FFF) == 0x7FFF)
        return color;
    
    int tcb = tintColor & 0x1F;
    int tcg = (tintColor >> 5) & 0x1F;
    int tcr = (tintColor >> 10) & 0x1F;
    
    int cb = color & 0x1F;
    int cg = (color >> 5) & 0x1F;
    int cr = (color >> 10) & 0x1F;
    int ca = color & 0x8000;
    
    cb = (cb * tcb) / 32;
    cg = (cg * tcg) / 32;
    cr = (cr * tcr) / 32;
    return ca | cb | (cg << 5) | (cr << 10);
#elif PIXEL_SIZE == 8
    // fast but hacky
    if (tintColor == 0xFF || tintColor == PXL_TRANSPARENT)
        return color;
    
    return color & tintColor;
#endif
}

// NOTE: alpha is between 0 and 256, NOT between 0 and 255!
FORCE_INLINE void alphaBlend(uintpixel_t* dcolor, uintpixel_t scolor, int alpha)
{
#if PIXEL_SIZE == 32 || PIXEL_SIZE == 16
    // it's so insignificant here nobody will notice if we just don't...
    if (alpha < 3)
        return;
    
    // it's so significant here we might as well fill in the whole color
    if (alpha > 253)
    {
        *dcolor = scolor;
        return;
    }
    
    int inval = 256 - alpha;
#endif

#if PIXEL_SIZE == 32
    Pixel32ARGB dc, sc;
    dc.l = *dcolor;
    sc.l = scolor;
    
    dc.p.r = (dc.p.r * inval + sc.p.r * alpha) >> 8;
    dc.p.g = (dc.p.g * inval + sc.p.g * alpha) >> 8;
    dc.p.b = (dc.p.b * inval + sc.p.b * alpha) >> 8;
    dc.p.a = 0xFF;
    
    *dcolor = dc.l;
#elif PIXEL_SIZE == 16
    int scb = scolor & 0x1F;
    int scg = (scolor >> 5) & 0x1F;
    int scr = (scolor >> 10) & 0x1F;

    uintpixel_t _dcolor = *dcolor;
    int dcb = _dcolor & 0x1F;
    int dcg = (_dcolor >> 5) & 0x1F;
    int dcr = (_dcolor >> 10) & 0x1F;
    
    dcr = (dcr * inval + scr * alpha) >> 8;
    dcg = (dcg * inval + scg * alpha) >> 8;
    dcb = (dcb * inval + scb * alpha) >> 8;
    
    *dcolor = 0x8000 | dcb | (dcg << 5) | (dcr << 10);
#else
    if (alpha < 240) {
        static int alphaApproximationThingy = 0;
        alphaApproximationThingy += 1339;
        if (alphaApproximationThingy > 601000)
            alphaApproximationThingy = 0;
        
        //gotta love that RNG
        if ((alphaApproximationThingy & 0xFF) >= alpha)
            return;
    }
    
    *dcolor = scolor;
#endif
}

FORCE_INLINE int swrIntAlpha(float alphaf)
{
    return (int)(alphaf * 256);
}

FORCE_INLINE bool swrMustRotate(float angleDeg)
{
    int angleDegInt = (int)(angleDeg * 4);
    angleDegInt %= 360*4;
    
    if (angleDegInt > 180*4)
        angleDegInt -= 360*4;
    
    return swrAbs(angleDegInt) >= 1; // 0.25 degrees
}

FORCE_INLINE bool swrMustRotateTolerant(float angleDeg)
{
    int angleDegInt = (int)(angleDeg * 16);
    angleDegInt %= 360*16;
    
    if (angleDegInt > 180*16)
        angleDegInt -= 360*16;
    
    return swrAbs(angleDegInt) >= 1; // 1/16 of a degree
}

FORCE_INLINE int swrSgn(float x)
{
    if (x < 0) return -1;
    return 1;
}

FORCE_INLINE int swrFloor(float x)
{
    int i = (int) x;
    return i - (x < (float) i);
}

FORCE_INLINE int swrCeiling(float x)
{
    int i = (int) x;
    return i + (x > (float) i);
}

static SWTexture* swrCreateTexture(const uint8_t* srcBuffer, int width, int height)
{
    SWTexture* txt = (SWTexture*) safeMalloc(sizeof(SWTexture));
    txt->buffer = (uintpixel_t*) safeMalloc(width * height * sizeof(uintpixel_t));
    
    const uint32_t* rgbaSrc = (const uint32_t*) srcBuffer;

    size_t sz = width * height;
    
    if (srcBuffer)
    {
        for (size_t i = 0; i < sz; i++)
            txt->buffer[i] = swrConvertPixelTexture(rgbaSrc[i]);
    }
    
    txt->width = (uint16_t) width;
    txt->height = (uint16_t) height;
    
    return txt;
}

static void swrFreeTexture(SWTexture* texture)
{
    free(texture->buffer);
    free(texture);
}

static bool swrAddTextureIndexToLRU(SWRenderer* swr, int textureIndex)
{
    uint32_t newIndex = (swr->textureIndexLRUHead + 1) % TEXTURE_LRU_LENGTH;
    if (newIndex == swr->textureIndexLRUTail) {
        // about to collide with tail from the other side -- nope.
        return false;
    }
    
    swr->textureIndexLRU[swr->textureIndexLRUHead] = textureIndex;
    swr->textureIndexLRUHead = newIndex;
    return true;
}

static int swrTailTextureIndexLRU(SWRenderer* swr, bool remove)
{
    if (swr->textureIndexLRUHead == swr->textureIndexLRUTail)
        return -1;
    
    uint32_t textureIndex = swr->textureIndexLRU[swr->textureIndexLRUTail];
    
    if (remove)
        swr->textureIndexLRUTail = (swr->textureIndexLRUTail + 1) % TEXTURE_LRU_LENGTH;
    
    return textureIndex;
}

static void swrEvictTextureFromCache(SWRenderer* swr, int textureIndex)
{
    SWTexture* texture = swr->textures[textureIndex];
    swr->textures[textureIndex] = NULL;
    
    swrFreeTexture(texture);
}

// Lazily decodes and uploads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to decode.
static bool swrEnsureTextureIsLoaded(SWRenderer* swr, uint32_t pageId)
{
    if (swr->textures[pageId])
        return true;

    DataWin* dw = swr->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];

    int w, h;
    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    
    uint8_t* pixels = NULL;
    
    do
    {
        if (!txtr->blobData) {
            DataWin_loadTxtrIfNeeded(dw, pageId);
        }
        
        if (txtr->blobData) {
            pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
            if (pixels) {
                free(txtr->blobData);
                txtr->blobData = NULL;
                break;
            }
            
            fprintf(stderr, "swr: Failed to decode TXTR page %u.  This is likely because we're out of memory, so evicting a texture.\n", pageId);
        } else {
            fprintf(stderr, "swr: Failed to load TXTR page %u.  This is likely because we're out of memory, so evicting a texture.\n", pageId);
        }
        
        int tail = swrTailTextureIndexLRU(swr, true);
        if (tail == -1) {
            fprintf(stderr, "swr: Looks like we can't fit this texture in memory at all. Bummer.\n");
            break;
        }
        
        swrEvictTextureFromCache(swr, tail);
        fprintf(stderr, "swr: Evicted texture %d, trying again.\n", tail);
    }
    while (!pixels);
    
    if (pixels == nullptr) {
        fprintf(stderr, "swr: Failed to decode TXTR page %u.\n", pageId);
        return false;
    }

    swr->textures[pageId] = swrCreateTexture(pixels, w, h);
    free(pixels);
    
    fprintf(stderr, "SWR: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
    
    // add it to the LRU
    do
    {
        bool added = swrAddTextureIndexToLRU(swr, pageId);
        if (added)
            break;
        
        int tail = swrTailTextureIndexLRU(swr, true);
        if (tail == -1) {
            fprintf(stderr, "swr: Come on now.\n");
            assert(tail != -1);
        }
        
        swrEvictTextureFromCache(swr, tail);
    }
    while (true);
    
    return true;
}

static void SWRenderer_init(Renderer* renderer, DataWin* dataWin)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    renderer->dataWin = dataWin;
    
    //allocate texture buffer
    swr->textureCount = dataWin->txtr.count;
    swr->surfaceCount = SURFACE_MAX_COUNT;
    swr->totalTextureCount = swr->textureCount + swr->surfaceCount;
    swr->textures = (SWTexture**) safeCalloc(swr->totalTextureCount, sizeof(SWTexture*));
    swr->surfaces = (SWTexture**) safeCalloc(swr->surfaceCount, sizeof(SWTexture*));
    
    //allocate texture LRU cache to allow for dynamic unloading of textures
    swr->textureIndexLRU = (uint32_t*) safeCalloc(TEXTURE_LRU_LENGTH, sizeof(uint32_t));
    swr->textureIndexLRUHead = 0;
    swr->textureIndexLRUTail = 0;
    
    //HACK: this isn't good, really.  This should seriously be refactored.
    //expand datawin's tpag items list to include our surface count.
    swr->originalTPagCount = dataWin->tpag.count;
    dataWin->tpag.items = (TexturePageItem*) safeRealloc(dataWin->tpag.items, sizeof(TexturePageItem) * (dataWin->tpag.count + swr->surfaceCount));
    dataWin->tpag.count += swr->surfaceCount;
    
    swr->originalSpriteCount = dataWin->sprt.count;
    
    for (size_t i = swr->originalTPagCount; i < dataWin->tpag.count; i++)
    {
        memset(&dataWin->tpag.items[i], 0, sizeof(TexturePageItem));
        dataWin->tpag.items[i].texturePageId = -1;
    }
    
    fprintf(stderr, "SWRenderer initialized.\n");
}

static void SWRenderer_destroy(Renderer* renderer)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    // TODO: why didn't I implement this.
    (void) swr;
    
    fprintf(stderr, "SWRenderer destroyed.\n");
}

static void SWRenderer_beginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    swr->gameW = gameW;
    swr->gameH = gameH;
    swr->drawingToSurface = false;

    if (swr->width != windowW || swr->height != windowH)
    {
        //allocate frame buffer
        free(swr->fb);
        swr->fb = (uintpixel_t*) safeMalloc(windowW * windowH * sizeof(uintpixel_t));
        swr->fbPitch = windowW;
        swr->width = windowW;
        swr->height = windowH;
    }
}

// This used to be just one, "endFrame". Not sure what the difference is.
static void SWRenderer_endFrameInit(Renderer* renderer)
{
    (void) renderer;
    
    //this is kinda useless to do twice isn't it?
}

static void SWRenderer_endFrameEnd(Renderer* renderer)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    assert(!swr->drawingToSurface);
    Runner_setNextFrame(swr->fb, swr->width, swr->height);
}

static void SWRenderer_beginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH,
                                 int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle)
{
    (void)renderer; (void)viewX; (void)viewY; (void)viewW; (void)viewH;
    (void)portX; (void)portY; (void)portW; (void)portH; (void)viewAngle;
    UNIMP2();
    
    SWRenderer* swr = (SWRenderer*) renderer;
    
    float xratio, yratio;
    
    float portviewX = (float) portW / viewW;
    float portviewY = (float) portH / viewH;
    
    int offsetX = 0, offsetY = 0;
    
    if (swr->drawingToSurface) {
        UNIMP();
        xratio = 1.0f;
        yratio = 1.0f;
        portX = (int)(portX * xratio);
        portY = (int)(portY * yratio);
    }
    else {
        float scaleX = (float) swr->width  / swr->gameW;
        float scaleY = (float) swr->height / swr->gameH;
        float scale  = (scaleX < scaleY) ? scaleX : scaleY;

        int32_t scaledW = (int32_t)(swr->gameW * scale);
        int32_t scaledH = (int32_t)(swr->gameH * scale);
        
        offsetX = (swr->width  - scaledW) / 2;
        offsetY = (swr->height - scaledH) / 2;

        xratio = scale;
        yratio = scale;

        portX = (int)(portX * xratio) + offsetX;
        portY = (int)(portY * yratio) + offsetY;
    }
    
    swr->scaleX = xratio * portviewX;
    swr->scaleY = yratio * portviewY;
    swr->offsetX = offsetX;
    swr->offsetY = offsetY;
    swr->defaultScaleX = xratio;
    swr->defaultScaleY = yratio;
    
    portW = (int)(portW * xratio);
    portH = (int)(portH * yratio);
    
    swr->viewActive = true;
    swr->viewX = viewX;
    swr->viewY = viewY;
    swr->viewW = viewW;
    swr->viewH = viewH;
    swr->portX = portX;
    swr->portY = portY;
    swr->portW = portW;
    swr->portH = portH;
    swr->maxX = portX + portW;
    swr->maxY = portY + portH;
}

static void SWRenderer_endView(Renderer* renderer)
{
    (void)renderer;
    UNIMP2();
    
    SWRenderer* swr = (SWRenderer*) renderer;
    swr->viewActive = false;
    
    swr->viewX = 0;
    swr->viewY = 0;
    swr->portX = swr->offsetX;
    swr->portY = swr->offsetY;
    swr->portW = swr->viewW = swr->width;
    swr->portH = swr->viewH = swr->height;
    swr->maxX = swr->portX + swr->portW;
    swr->maxY = swr->portY + swr->portH;
    swr->scaleX = swr->defaultScaleX;
    swr->scaleY = swr->defaultScaleY;
}

static bool swrSwitchToSurface(Renderer* renderer, int32_t targetSurfaceId, bool restoreOldView)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER)
    {
        if (!swr->drawingToSurface)
            return true;
        
        // restore the original framebuffer
        fprintf(stderr, "back to original framebuffer\n");
        swr->drawingToSurface = false;
        swr->fb = swr->mainFb;
        swr->width = swr->mainWidth;
        swr->height = swr->mainHeight;
        swr->fbPitch = swr->mainPitch;
        
        if (restoreOldView) {
            // restore the old transform, if needed
            swr->viewX = swr->lastViewX;
            swr->viewY = swr->lastViewY;
            swr->viewW = swr->lastViewW;
            swr->viewH = swr->lastViewH;
            swr->portX = swr->lastPortX;
            swr->portY = swr->lastPortY;
            swr->portW = swr->lastPortW;
            swr->portH = swr->lastPortH;
            swr->gameW = swr->lastGameW;
            swr->gameH = swr->lastGameH;
            swr->maxX = swr->lastMaxX;
            swr->maxY = swr->lastMaxY;
            swr->scaleX = swr->lastScaleX;
            swr->scaleY = swr->lastScaleY;
        }
        return true;
    }
    
    if (targetSurfaceId < 0 || (size_t) targetSurfaceId >= swr->surfaceCount || swr->surfaces[targetSurfaceId] == NULL) {
        fprintf(stderr, "swr: Invalid surface id %d\n", targetSurfaceId);
        return false;
    }
    
    if (!swr->drawingToSurface)
    {
        // back up the original framebuffer
        swr->drawingToSurface = true;
        swr->mainFb = swr->fb;
        swr->mainWidth = swr->width;
        swr->mainHeight = swr->height;
        swr->mainPitch = swr->fbPitch;
        
        // and the old transform
        swr->lastViewX = swr->viewX;
        swr->lastViewY = swr->viewY;
        swr->lastViewW = swr->viewW;
        swr->lastViewH = swr->viewH;
        swr->lastPortX = swr->portX;
        swr->lastPortY = swr->portY;
        swr->lastPortW = swr->portW;
        swr->lastPortH = swr->portH;
        swr->lastGameW = swr->gameW;
        swr->lastGameH = swr->gameH;
        swr->lastMaxX = swr->maxX;
        swr->lastMaxY = swr->maxY;
        swr->lastScaleX = swr->scaleX;
        swr->lastScaleY = swr->scaleY;
    }
    
    SWTexture* surface = swr->surfaces[targetSurfaceId];
    swr->fb = surface->buffer;
    swr->width = surface->width;
    swr->height = surface->height;
    swr->fbPitch = surface->width;
    swr->drawingToSurface = true;
    
    swr->viewX = swr->portX = 0;
    swr->viewY = swr->portY = 0;
    swr->maxX = swr->viewW = swr->portW = surface->width;
    swr->maxY = swr->viewH = swr->portH = surface->height;
    swr->scaleX = swr->scaleY = 1.0f;
    
    fprintf(stderr, "switching to surface %p, fb %p, %dx%d\n", surface, swr->fb, swr->width, swr->height);
    
    return true;
}

static void SWRenderer_beginGUI(Renderer* renderer, int32_t guiW, int32_t guiH,
                                int32_t portX, int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId)
{
    swrSwitchToSurface(renderer, targetSurfaceId, false);
    
    (void)guiW; (void)guiH;
    (void)portX; (void)portY; (void)portW; (void)portH;
    (void)targetSurfaceId;
    UNIMP2();
}

static void SWRenderer_setGuiProjection(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, bool renderingToUserSurface)
{
    (void) renderer;
    (void) guiW; (void) guiH;
    (void) portW; (void) portH;
    (void) renderingToUserSurface;
    UNIMP();
}

static void SWRenderer_endGUI(Renderer* renderer)
{
    (void)renderer;
    UNIMP2();
}

// TODO[MrPowerGamerBR]: This is supposed to be refactored, not to modify data.win structs directly.
static int32_t swrFindSurfaceTextureSlot(SWRenderer* swr)
{
    // NOTE: dynamic textures are not enrolled into the eviction cache for
    // hopefully obvious reasons ...
    for (size_t i = swr->textureCount; i != swr->totalTextureCount; i++)
    {
        if (swr->textures[i] == NULL) {
            return (int32_t) i;
        }
    }
    
    return -1;
}

static int32_t swrFindSurfaceTPagSlot(SWRenderer* swr)
{
    DataWin* dw = swr->base.dataWin;
    for (size_t i = swr->originalTPagCount; i != dw->tpag.count; i++)
    {
        if (dw->tpag.items[i].texturePageId == -1) {
            return (int32_t) i;
        }
    }
    
    return -1;
}

static void swrTransformPosIfNeeded(SWRenderer* swr, float* dx, float* dy)
{
    if (dx) {
        *dx -= swr->viewX;
        *dx *= swr->scaleX;
        *dx += swr->portX;
    }
    if (dy) {
        *dy -= swr->viewY;
        *dy *= swr->scaleY;
        *dy += swr->portY;
    }
}

static void swrTransformSizeIfNeeded(SWRenderer* swr, float* dx, float* dy)
{
    if (dx) *dx *= swr->scaleX;
    if (dy) *dy *= swr->scaleY;
}

static void swrTransformPosIntIfNeeded(SWRenderer* swr, int32_t* dx, int32_t* dy)
{
    if (dx) {
        *dx -= swr->viewX;
        *dx = (int)(*dx * swr->scaleX);
        *dx += swr->portX;
    }
    if (dy) {
        *dy -= swr->viewY;
        *dy = (int)(*dy * swr->scaleY);
        *dy += swr->portY;
    }
}

static void swrTransformSizeIntIfNeeded(SWRenderer* swr, int32_t* dx, int32_t* dy)
{
    if (dx) *dx = (int)(*dx * swr->scaleX);
    if (dy) *dy = (int)(*dy * swr->scaleY);
}

static void swrReverseTransformSizeIntIfNeeded(SWRenderer* swr, int32_t* dx, int32_t* dy)
{
    if (dx) *dx = (int)(*dx / swr->scaleX);
    if (dy) *dy = (int)(*dy / swr->scaleY);
}

FORCE_INLINE void swrPlotPixel(Renderer* renderer, int x, int y, uintpixel_t color, int alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (x < swr->portX || y < swr->portY) return;
    if (x >= swr->maxX || y >= swr->maxY) return;
    
    alphaBlend(&swr->fb[y * swr->fbPitch + x], color, alpha);
}

static void swrDrawHLineInt(Renderer* renderer, int dx, int dy, int dw, uintpixel_t color, UNUSED uintpixel_t color2, int alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    if (dy < swr->portY) return;
    if (dy >= swr->maxY) return;
    if (dx < swr->portX) { dw += dx - swr->portX; dx = swr->portX; }
    if (dx + dw >= swr->maxX) dw = swr->maxX - dx;
    if (dw <= 0) return;
    
#if PIXEL_SIZE == 32
    if (color == color2)
#endif
    {
        uintpixel_t *line = &swr->fb[dy * swr->fbPitch + dx];
        for (int i = 0; i < dw; i++)
            alphaBlend(&line[i], color, alpha);
    }
#if PIXEL_SIZE == 32
    else
    {
        Pixel32ARGB clr1, clr2;
        clr1.l = color;
        clr2.l = color2;
        
        uint32_t rinit = clr1.p.r << 20;
        uint32_t ginit = clr1.p.g << 20;
        uint32_t binit = clr1.p.b << 20;
        int32_t rstep = ((int)clr2.p.r - clr1.p.r) << 20;
        int32_t gstep = ((int)clr2.p.g - clr1.p.g) << 20;
        int32_t bstep = ((int)clr2.p.b - clr1.p.b) << 20;
        rstep /= dw;
        gstep /= dw;
        bstep /= dw;
        
        uintpixel_t *line = &swr->fb[dy * swr->fbPitch + dx];
        for (int i = 0; i < dw; i++)
        {
            Pixel32ARGB resultPixel;
            resultPixel.p.r = rinit >> 20;
            resultPixel.p.g = ginit >> 20;
            resultPixel.p.b = binit >> 20;
            rinit += rstep;
            ginit += gstep;
            binit += bstep;
            alphaBlend(&line[i], resultPixel.l, alpha);
        }
    }
#endif
}

static void swrDrawHLine(Renderer* renderer, float dx, float dy, float dw, uintpixel_t color, uintpixel_t color2, float alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    float thickness = 1;

    swrTransformPosIfNeeded(swr, &dx, &dy);
    swrTransformSizeIfNeeded(swr, &dw, &thickness);

    // TODO: use thickness
    swrDrawHLineInt(renderer, swrFloor(dx), swrFloor(dy), swrCeiling(dw), color, color2, swrIntAlpha(alpha));
}

static void swrDrawVLineInt(Renderer* renderer, int dx, int dy, int dh, uintpixel_t color, UNUSED uintpixel_t color2, int alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    if (dx < swr->portX) return;
    if (dx >= swr->maxX) return;
    if (dy < swr->portY) { dh += dy - swr->portY; dy = swr->portY; }
    if (dy + dh >= swr->maxY) dh = swr->maxY - dy;
    if (dh <= 0) return;
    
#if PIXEL_SIZE == 32
    if (color == color2)
#endif
    {
        for (int i = 0; i < dh; i++)
        {
            uintpixel_t *line = &swr->fb[(dy + i) * swr->fbPitch + dx];
            alphaBlend(&line[0], color, alpha);
        }
    }
#if PIXEL_SIZE == 32
    else
    {
        Pixel32ARGB clr1, clr2;
        clr1.l = color;
        clr2.l = color2;
        
        uint32_t rinit = clr1.p.r << 20;
        uint32_t ginit = clr1.p.g << 20;
        uint32_t binit = clr1.p.b << 20;
        int32_t rstep = ((int)clr2.p.r - clr1.p.r) << 20;
        int32_t gstep = ((int)clr2.p.g - clr1.p.g) << 20;
        int32_t bstep = ((int)clr2.p.b - clr1.p.b) << 20;
        rstep /= dh;
        gstep /= dh;
        bstep /= dh;
        
        for (int i = 0; i < dh; i++)
        {
            uintpixel_t *line = &swr->fb[(dy + i) * swr->fbPitch + dx];
            Pixel32ARGB resultPixel;
            resultPixel.p.r = rinit >> 20;
            resultPixel.p.g = ginit >> 20;
            resultPixel.p.b = binit >> 20;
            rinit += rstep;
            ginit += gstep;
            binit += bstep;
            alphaBlend(&line[0], resultPixel.l, alpha);
        }
    }
#endif
}

static void swrDrawVLine(Renderer* renderer, float dx, float dy, float dh, uintpixel_t color, uintpixel_t color2, float alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    float thickness = 1;

    swrTransformPosIfNeeded(swr, &dx, &dy);
    swrTransformSizeIfNeeded(swr, &thickness, &dh);
    
    // TODO: use thickness
    swrDrawVLineInt(renderer, swrFloor(dx), swrFloor(dy), swrCeiling(dh), color, color2, swrIntAlpha(alpha));
}

static void swrDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color, float alpha)
{
    swrDrawHLine(renderer, x1, y1, (x2 - x1) + 1, color, color, alpha);
    swrDrawHLine(renderer, x1, y2, (x2 - x1) + 1, color, color, alpha);
    swrDrawVLine(renderer, x1, y1, (y2 - y1) + 1, color, color, alpha);
    swrDrawVLine(renderer, x2, y1, (y2 - y1) + 1, color, color, alpha);
}

static void swrDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, uintpixel_t color4, float alpha)
{
    swrDrawHLine(renderer, x1, y1, (x2 - x1) + 1, color1, color2, alpha);
    swrDrawHLine(renderer, x1, y2, (x2 - x1) + 1, color3, color4, alpha);
    swrDrawVLine(renderer, x1, y1, (y2 - y1) + 1, color1, color3, alpha);
    swrDrawVLine(renderer, x2, y1, (y2 - y1) + 1, color2, color4, alpha);
}

static void swrDrawLineInt(Renderer* renderer, int x1, int y1, int x2, int y2, MAYBE_UNUSED int width, uintpixel_t color1, uintpixel_t color2, int alpha)
{
    if (x1 == x2)
    {
        swrDrawVLineInt(renderer, x1, swrMin(y1, y2), swrAbs(y1 - y2), color1, color2, alpha);
        return;
    }
    if (y1 == y2)
    {
        swrDrawHLineInt(renderer, swrMin(x1, x2), y1, swrAbs(x1 - x2), color1, color2, alpha);
        return;
    }
    
    int dx = x2 - x1, dy = y2 - y1;
    int dx1 = swrAbs(dx), dy1 = swrAbs(dy), xe, ye, x, y;
    int px = 2 * dy1 - dx1, py = 2 * dx1 - dy1;
    
    uintpixel_t color = color1;
#if PIXEL_SIZE == 32
    Pixel32ARGB clr1, clr2;
    clr1.l = color1;
    clr2.l = color2;
    
    uint32_t rinit = clr1.p.r << 20;
    uint32_t ginit = clr1.p.g << 20;
    uint32_t binit = clr1.p.b << 20;
    int32_t rstep = ((int)clr2.p.r - clr1.p.r) << 20;
    int32_t gstep = ((int)clr2.p.g - clr1.p.g) << 20;
    int32_t bstep = ((int)clr2.p.b - clr1.p.b) << 20;
#endif
    
    if (dy1 <= dx1)
    {
        if (dx >= 0)
        {
            x = x1, y = y1, xe = x2;
        }
        else
        {
            x = x2, y = y2, xe = x1;
        }
        
#if PIXEL_SIZE == 32
        if (dx1 > 0) {
            rstep /= dx1;
            gstep /= dx1;
            bstep /= dx1;
        } else {
            rstep = gstep = bstep = 0;
        }
#endif
        
        swrPlotPixel(renderer, x, y, color, alpha);
        
        for (int i = 0; x < xe; i++)
        {
            x++;
            if (px < 0)
            {
                px += 2 * dy1;
            }
            else
            {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) y++; else y--;
                px += 2 * (dy1 - dx1);
            }
            
#if PIXEL_SIZE == 32
            Pixel32ARGB resultPixel;
            resultPixel.p.r = rinit >> 20;
            resultPixel.p.g = ginit >> 20;
            resultPixel.p.b = binit >> 20;
            rinit += rstep;
            ginit += gstep;
            binit += bstep;
            color = resultPixel.l;
#endif
            
            swrPlotPixel(renderer, x, y, color, alpha);
        }
    }
    else
    {
        if (dy >= 0)
        {
            x = x1, y = y1, ye = y2;
        }
        else
        {
            x = x2, y = y2, ye = y1;
        }
        
#if PIXEL_SIZE == 32
        if (dy1 > 0) {
            rstep /= dy1;
            gstep /= dy1;
            bstep /= dy1;
        } else {
            rstep = gstep = bstep = 0;
        }
#endif
        
        swrPlotPixel(renderer, x, y, color, alpha);
        
        for (int i = 0; y < ye; i++)
        {
            y++;
            if (py <= 0)
            {
                py += 2 * dx1;
            }
            else
            {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) x++; else x--;
                py += 2 * (dx1 - dy1);
            }
            
#if PIXEL_SIZE == 32
            Pixel32ARGB resultPixel;
            resultPixel.p.r = rinit >> 20;
            resultPixel.p.g = ginit >> 20;
            resultPixel.p.b = binit >> 20;
            rinit += rstep;
            ginit += gstep;
            binit += bstep;
            color = resultPixel.l;
#endif
            
            swrPlotPixel(renderer, x, y, color, alpha);
        }
    }
}

static void swrDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uintpixel_t color, uintpixel_t color2, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);
    swrTransformSizeIfNeeded(swr, &width, NULL);
    swrDrawLineInt(renderer, swrFloor(x1), swrFloor(y1), swrCeiling(x2), swrCeiling(y2), swrCeiling(width), color, color2, swrIntAlpha(alpha));
}

static void swrDrawSpriteInternal(
    Renderer* renderer, int dx, int dy, int dw, int dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uintpixel_t tintColor, int alpha
)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    bool flipX = false, flipY = false;
    if (dw < 0) { dx += dw; dw = -dw; flipX = true; }
    if (dh < 0) { dy += dh; dh = -dh; flipY = true; }
    
    //basic out of bound checks
    if (dw == 0 || dh == 0) return;
    if (sw == 0) sw = 1;
    if (sh == 0) sh = 1;
    if (dx + dw <= swr->portX) return;
    if (dy + dh <= swr->portY) return;
    if (dx >= swr->maxX) return;
    if (dy >= swr->maxY) return;
    
    int odw = dw, odh = dh;
    int osw = sw, osh = sh;
    
    int minx = swr->portX, miny = swr->portY, maxx = swr->portX + swr->portW, maxy = swr->portY + swr->portH;
    
    //out of bounds adjustment checks
    int diffxl = 0, diffyl = 0, diffxu = 0, diffyu = 0;
    if (dx < minx) { diffxl = minx - dx; dx = minx; dw -= diffxl; }
    if (dy < miny) { diffyl = miny - dy; dy = miny; dh -= diffyl; }
    if (dx + dw > maxx) { diffxu = dx + dw - maxx; dw -= diffxu; }
    if (dy + dh > maxy) { diffyu = dy + dh - maxy; dh -= diffyu; }
    
    if (diffxl != 0 || diffyl != 0 || diffxu != 0 || diffyu != 0)
    {
        //adjust source coordinates too
        diffxl = (int)((long)diffxl * osw / odw);
        diffyl = (int)((long)diffyl * osh / odh);
        diffxu = (int)((long)(diffxu + 1) * osw / odw);
        diffyu = (int)((long)(diffyu + 1) * osh / odh);
        sx += flipX ? diffxu : diffxl;
        sy += flipY ? diffyu : diffyl;
        sw -= diffxl + diffxu;
        sh -= diffyl + diffyu;
        if (sw <= 0 || sh <= 0) return;
    }
    
    //clip the source coords into bounds too
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw >= texture->width)  { sw = texture->width  - sx; }
    if (sy + sh >= texture->height) { sh = texture->height - sy; }
    if (sw <= 0 || sh <= 0) return;
    
    //okay, now we can finally get on with rendering
    
    int ixs = 0, oxs = 1, iys = 0, oys = 1;
    if (flipX) ixs = dw - 1, oxs = -1;
    if (flipY) iys = dh - 1, oys = -1;
    
    // tweak these if stuff doesn't look right
    typedef int32_t fixedp_t;
    const int fp_prec = 14;

    fixedp_t ystep = (sh == dh) ? (1 << fp_prec) : ((fixedp_t) osh << fp_prec) / odh;
    fixedp_t xstep = (sw == dw) ? (1 << fp_prec) : ((fixedp_t) osw << fp_prec) / odw;
    fixedp_t oxs2 = oxs * xstep;
    fixedp_t oys2 = oys * ystep;
    fixedp_t ixs2 = ixs * xstep;
    fixedp_t iys2 = iys * ystep;
    
    if (sw == dw)
    {
        fixedp_t ys2 = (fixedp_t) iys2;
        for (int y = 0, ys = iys; y < dh; y++, ys += oys, ys2 += oys2)
        {
            uintpixel_t* dstline;
            const uintpixel_t* srcline;
            dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            if (dh == sh)
                srcline = &texture->buffer[(sy + ys) * texture->width + sx];
            else
                srcline = &texture->buffer[(sy + (int)(ys2 >> fp_prec)) * texture->width + sx];
            
            for (int x = 0, xs = ixs; x < dw; x++, xs += oxs)
            {
                uintpixel_t pixel = srcline[xs];
                if (opaque(pixel))
                    alphaBlend(&dstline[x], tint(tintColor, pixel), alpha);
            }
        }
    }
    else
    {
        fixedp_t ys2 = iys2;
        for (int y = 0, ys = iys; y < dh; y++, ys += oys, ys2 += oys2)
        {
            uintpixel_t* dstline;
            const uintpixel_t* srcline;
            dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            if (dh == sh)
                srcline = &texture->buffer[(sy + ys) * texture->width + sx];
            else
                srcline = &texture->buffer[(sy + (int)(ys2 >> fp_prec)) * texture->width + sx];
            
            fixedp_t xs2 = ixs2;
            for (int x = 0, xs = ixs; x < dw; x++, xs += oxs, xs2 += oxs2)
            {
                uintpixel_t pixel = srcline[(int)(xs2 >> fp_prec)];
                if (opaque(pixel))
                    alphaBlend(&dstline[x], tint(tintColor, pixel), alpha);
            }
        }
    }
}

static void swrDrawSprite(
    Renderer* renderer, float dx, float dy, float dw, float dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uint32_t tintColor, float alpha
)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    swrTransformPosIfNeeded(swr, &dx, &dy);
    swrTransformSizeIfNeeded(swr, &dw, &dh);
    
    swrDrawSpriteInternal(
        renderer,
        swrFloor(dx),
        swrFloor(dy),
        swrCeiling(dw),
        swrCeiling(dh),
        texture,
        sx, sy,
        sw, sh,
        swrConvertPixel(tintColor),
        swrIntAlpha(alpha)
    );
}

static void swrDrawSpriteRotatedInternal(
    Renderer* renderer, int dx, int dy, int dw, int dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uintpixel_t tintColor, int alpha,
    float angleDeg,
    float pivotX,
    float pivotY
)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    float angleRad = -angleDeg * M_PI / 180.0f;
    
    bool flipX = false, flipY = false;
    if (dw < 0) { dw = -dw; dx -= dw; pivotX = dw - pivotX; flipX = true; }
    if (dh < 0) { dh = -dh; dy -= dh; pivotY = dh - pivotY; flipY = true; }
    
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);
    
    float cnrx[4], cnry[4];
    cnrx[0] = cnrx[3] = dx;
    cnry[0] = cnry[1] = dy;
    cnrx[1] = cnrx[2] = dx + dw;
    cnry[2] = cnry[3] = dy + dh;
    
    float pxa = pivotX + dx;
    float pya = pivotY + dy;
    
    float minXf = FLT_MAX, minYf = FLT_MAX, maxXf = -FLT_MAX, maxYf = -FLT_MAX;
    for (int i = 0; i < 4; i++)
    {
        float cxi = cnrx[i] - pxa;
        float cyi = cnry[i] - pya;
        float rx = cosA * cxi - sinA * cyi + pxa;
        float ry = sinA * cxi + cosA * cyi + pya;
        if (minXf > rx) minXf = rx;
        if (maxXf < rx) maxXf = rx;
        if (minYf > ry) minYf = ry;
        if (maxYf < ry) maxYf = ry;
    }

    // minX, minY, maxX, maxY now represent an AABB of pixels we should loop over
    int minX = swrFloor(minXf);
    int minY = swrFloor(minYf);
    int maxX = swrCeiling(maxXf);
    int maxY = swrCeiling(maxYf);
    
    // basic out-of-bound checks
    if (maxX < swr->portX) return;
    if (maxY < swr->portY) return;
    if (minX >= swr->maxX) return;
    if (minY >= swr->maxY) return;
    
    // however, we'll need to clip it against out of bounds first
    int minXc = minX, minYc = minY, maxXc = maxX, maxYc = maxY;
    int minx = swr->portX, miny = swr->portY, maxx = swr->portX + swr->portW, maxy = swr->portY + swr->portH;
    
    if (minXc < minx) minXc = minx;
    if (minYc < miny) minYc = miny;
    if (maxXc >= maxx) maxXc = maxx;
    if (maxYc >= maxy) maxYc = maxy;
    
    // some final clip checks
    if (minXc >= maxXc || minYc >= maxYc) return;
    
    int sox = flipX ? sw - 1 : 0;
    int soy = flipY ? sh - 1 : 0;
    int six = flipX ? -1 : 1;
    int siy = flipY ? -1 : 1;
    
    float sw_dw = (float) sw / dw;
    float sh_dh = (float) sh / dh;
    
    for (int cy = minYc; cy < maxYc; cy++)
    {
        uintpixel_t *dstline = &swr->fb[cy * swr->fbPitch];
        for (int cx = minXc; cx < maxXc; cx++)
        {
            // we need to determine the texture-space coordinate of cx/cy
            float ox = (float) cx + 0.5f - pxa;
            float oy = (float) cy + 0.5f - pya;
            
            // "undo" the rotation
            float lx =  cosA * ox + sinA * oy;
            float ly = -sinA * ox + cosA * oy;
            
            // turn it into a texture-local coordinate
            lx += pxa - dx;
            ly += pya - dy;
            
            if (lx < 0 || ly < 0 || lx >= (float) dw || ly >= (float) dh) continue;
            
            lx = lx * sw_dw;
            ly = ly * sh_dh;
            
            int tx = (int)(sox + lx * six);
            int ty = (int)(soy + ly * siy);
            
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx >= sw) tx = sw - 1;
            if (ty >= sh) ty = sh - 1;
            
            tx += sx;
            ty += sy;
            
            uintpixel_t src = texture->buffer[ty * texture->width + tx];
            
            if (opaque(src))
                alphaBlend(&dstline[cx], tint(tintColor, src), alpha);
        }
    }
}

static void swrDrawSpriteRotated(
    Renderer* renderer, float dx, float dy, float dw, float dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uint32_t tintColor, float alpha,
    float angleDeg,
    float pivotX,
    float pivotY
)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    swrTransformPosIfNeeded(swr, &dx, &dy);
    swrTransformSizeIfNeeded(swr, &pivotX, &pivotY);
    swrTransformSizeIfNeeded(swr, &dw, &dh);
    
    swrDrawSpriteRotatedInternal(
        renderer,
        swrFloor(dx),
        swrFloor(dy),
        swrCeiling(dw),
        swrCeiling(dh),
        texture,
        sx, sy,
        sw, sh,
        swrConvertPixel(tintColor),
        swrIntAlpha(alpha),
        angleDeg,
        pivotX,
        pivotY
    );
}

static void SWRenderer_drawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y,
                                  float originX, float originY, float xscale, float yscale,
                                  float angleDeg, uint32_t color, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;

    if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) {
        fprintf(stderr, "%s: tpagIndex of %d is invalid\n", __func__, tpagIndex);
        return;
    }

    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) {
        fprintf(stderr, "%s: tpagIndex of %d is invalid, as pageId of %d is invalid\n", __func__, tpagIndex, pageId);
        return;
    }
    if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) {
        fprintf(stderr, "%s: could not ensure texture is loaded, tpagIndex: %d, pageId: %d\n", __func__, tpagIndex, pageId);
        return;
    }
    
    int sx = tpag->sourceX;
    int sy = tpag->sourceY;
    int sw = tpag->sourceWidth;
    int sh = tpag->sourceHeight;
    
    float dx = (float)(tpag->targetX - originX);
    float dy = (float)(tpag->targetY - originY);
    int dw = (int)(xscale * tpag->targetWidth);
    int dh = (int)(yscale * tpag->targetHeight);
    dx *= xscale;
    dy *= yscale;
    dx += x;
    dy += y;

    SWTexture* texture = swr->textures[pageId];
    
    if (UNLIKELY(swrMustRotate(angleDeg)))
    {
        float pivotX = (x - dx) * swrSgn(xscale);
        float pivotY = (y - dy) * swrSgn(yscale);
        
        if (tpag->targetWidth != tpag->sourceWidth)
            pivotX *= (float)tpag->targetWidth / tpag->sourceWidth;
        if (tpag->targetHeight != tpag->sourceHeight)
            pivotY *= (float)tpag->targetHeight/ tpag->sourceHeight;
        
        swrDrawSpriteRotated(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, pivotX, pivotY);
    }
    else
    {
        swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
    }
}

static void SWRenderer_drawSpritePart(Renderer* renderer, int32_t tpagIndex,
                                      int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH,
                                      float x, float y, float xscale, float yscale, float angleDeg,
                                      float pivotX, float pivotY, uint32_t color, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;
    
    if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) return;
    
    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) return;
    if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return;
    
    int sx = tpag->sourceX + srcOffX;
    int sy = tpag->sourceY + srcOffY;
    int sw = srcW;
    int sh = srcH;
    
    float dx = x;
    float dy = y;
    int dw = swrCeiling(xscale * sw);
    int dh = swrCeiling(yscale * sh);
    
    if (tpag->sourceWidth != tpag->targetWidth) {
        sx = sx * tpag->sourceWidth / tpag->targetWidth;
        sw = sw * tpag->sourceWidth / tpag->targetWidth;
    }
    if (tpag->sourceHeight != tpag->targetHeight) {
        sy = sy * tpag->sourceHeight / tpag->targetHeight;
        sh = sh * tpag->sourceHeight / tpag->targetHeight;
    }
    
    SWTexture* texture = swr->textures[pageId];
    
    if (UNLIKELY(swrMustRotate(angleDeg)))
    {
        swrDrawSpriteRotated(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, pivotX * dw, pivotY * dh);
    }
    else
    {
        swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
    }
}

static void SWRenderer_drawSpritePos(Renderer* renderer, int32_t tpagIndex,
                                     float x1, float y1, float x2, float y2,
                                     float x3, float y3, float x4, float y4, float alpha)
{
    (void)renderer; (void)tpagIndex;
    (void)x1; (void)y1; (void)x2; (void)y2;
    (void)x3; (void)y3; (void)x4; (void)y4; (void)alpha;
    UNIMP();
}

static void SWRenderer_drawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2,
                                     uint32_t color, float alpha, bool outline)
{
    uintpixel_t pxcolor = swrConvertPixel(color);
    
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (outline)
    {
        swrDrawRectangle(renderer, x1, y1, x2, y2, pxcolor, alpha);
    }
    else
    {
        swrTransformPosIfNeeded(swr, &x1, &y1);
        swrTransformPosIfNeeded(swr, &x2, &y2);

        int alphaInt = swrIntAlpha(alpha);
        int x1i = swrFloor(x1), x2i = swrCeiling(x2), y1i = swrFloor(y1), y2i = swrCeiling(y2);
        int xd = x2i - x1i;
        int yd = y2i - y1i;
        if (xd <= 0 || yd <= 0) return;
        
        for (int y = 0; y <= yd; y++) {
            swrDrawHLineInt(renderer, x1i, y1i + y, xd, pxcolor, pxcolor, alphaInt);
        }
    }
}

static void SWRenderer_drawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2,
                                          uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4,
                                          float alpha, bool outline)
{
    uintpixel_t pxcolor1 = swrConvertPixel(color1);
    uintpixel_t pxcolor2 = swrConvertPixel(color2);
    uintpixel_t pxcolor3 = swrConvertPixel(color3);
    uintpixel_t pxcolor4 = swrConvertPixel(color4);
    
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (outline)
    {
        swrDrawRectangleColor(renderer, x1, y1, x2, y2, pxcolor1, pxcolor2, pxcolor3, pxcolor4, alpha);
    }
    else
    {
        swrTransformPosIfNeeded(swr, &x1, &y1);
        swrTransformPosIfNeeded(swr, &x2, &y2);

        int alphaInt = swrIntAlpha(alpha);
        int x1i = swrFloor(x1), x2i = swrCeiling(x2), y1i = swrFloor(y1), y2i = swrCeiling(y2);
        int xd = x2i - x1i;
        int yd = y2i - y1i;
        if (xd <= 0 || yd <= 0) return;
        
        // TODO: blending vertically
        for (int y = 0; y <= yd; y++) {
            swrDrawHLineInt(renderer, x1i, y1i + y, xd, pxcolor1, pxcolor2, alphaInt);
        }
    }
}

static void SWRenderer_drawLine(Renderer* renderer, float x1, float y1, float x2, float y2,
                                float width, uint32_t color, float alpha)
{
    (void)renderer; (void)x1; (void)y1; (void)x2; (void)y2;
    (void)width; (void)color; (void)alpha;
    
    uintpixel_t colorCvt = swrConvertPixel(color);
    swrDrawLine(renderer, x1, y1, x2, y2, width, colorCvt, colorCvt, alpha);
}

static void swrDrawTriangleInternal(SWRenderer* swr, int xup, int yup, int xleft, int yleft, int xright, int yright, uint32_t color1, uint32_t color2, uint32_t color3, int alpha)
{
    // TODO: update this
    (void) color2;
    (void) color3;
    
    // Figure out the maximum Y extent of the triangle.
    // (Note that we know yup is the minimum.)
    int xmid, ymid, xmid2 = xup, xmax, ymax;
    if (yleft < yright) {
        xmax = xright, ymax = yright;
        xmid = xleft, ymid = yleft;
        if (yright != yup)
            xmid2 = xup + (xright - xup) * (ymid - yup) / (yright - yup);
    } else {
        xmax = xleft, ymax = yleft;
        xmid = xright, ymid = yright;
        if (yleft != yup)
            xmid2 = xup + (xleft - xup) * (ymid - yup) / (yleft - yup);
    }
    
    for (int y = yup; y < ymax; y++)
    {
        if (y < 0) continue;
        if (y >= swr->height) break;
        
        int x1 = xup, x2 = xup;
        if (y <= ymid)
        {
            // Lines: between up and mid, and between up and max
            if (ymid != yup)
                x1 = xup + (xmid - xup) * (y - yup) / (ymid - yup);
            
            if (ymid != yup)
                x2 = xup + (xmid2 - xup) * (y - yup) / (ymid - yup);
        }
        else
        {
            // Lines: between mid and max, and between up and max
            if (ymax != yup)
                x1 = xup + (xmax - xup) * (y - yup) / (ymax - yup);
            
            if (ymax != ymid)
                x2 = xmid + (xmax - xmid) * (y - ymid) / (ymax - ymid);
        }
        
        if (x1 >= x2) {
            int tmp = x1;
            x1 = x2;
            x2 = tmp;
        }
        
        if (x1 < swr->portX) x1 = swr->portX;
        if (x1 >= swr->maxX) continue;
        if (x2 < swr->portX) continue;
        if (x2 >= swr->maxX) x2 = swr->maxX - 1;
        if (x1 > x2) continue;
        
        uintpixel_t* line = &swr->fb[y * swr->width];
        for (int x = x1; x < x2; x++) {
            alphaBlend(&line[x], color1, alpha);
        }
    }
}

static void swrDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha)
{
    float xup, yup, xleft, yleft, xright, yright;
    uint32_t colorup, colorleft, colorright;
    
    SWRenderer* swr = (SWRenderer*) renderer;
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);
    swrTransformPosIfNeeded(swr, &x3, &y3);
    
    //which vertex is higher?
    xup = x1, yup = y1; colorup = color1;
    xleft = x2, yleft = y2; colorleft = color2;
    xright = x3, yright = y3; colorright = color3;
    if (yup > y2) {
        xup = x2, yup = y2, colorup = color2;
        xleft = x1, yleft = y1, colorleft = color1;
        //xright = x3, yright = y3;
    }
    if (yup > y3) {
        xup = x3, yup = y3, colorup = color3;
        xleft = x1, yleft = y1, colorleft = color1;
        xright = x2, yright = y2, colorright = color2;
    }
    
    if (xleft > xright) {
        float tmp = xleft;
        xleft = xright;
        xright = tmp;
        tmp = yleft;
        yleft = yright;
        yright = tmp;
        uint32_t tmp2 = colorleft;
        colorleft = colorright;
        colorright = tmp2;
    }
    
    swrDrawTriangleInternal(
        swr,
        swrFloor(xup), swrFloor(yup),
        swrFloor(xleft), swrCeiling(yleft),
        swrFloor(xright), swrCeiling(yright),
        swrConvertPixel(colorup),
        swrConvertPixel(colorleft),
        swrConvertPixel(colorright),
        swrIntAlpha(alpha)
    );
}

static void SWRenderer_drawTriangle(Renderer* renderer,
                                    float x1, float y1, float x2, float y2, float x3, float y3,
                                    uint32_t color1, uint32_t color2, uint32_t color3,
                                    float alpha, bool outline)
{
    if (outline)
    {
        uintpixel_t color1cvt = swrConvertPixel(color1);
        uintpixel_t color2cvt = swrConvertPixel(color2);
        uintpixel_t color3cvt = swrConvertPixel(color3);
        swrDrawLine(renderer, x1, y1, x2, y2, 1, color1cvt, color2cvt, renderer->drawAlpha);
        swrDrawLine(renderer, x1, y1, x3, y3, 1, color1cvt, color3cvt, renderer->drawAlpha);
        swrDrawLine(renderer, x2, y2, x3, y3, 1, color3cvt, color3cvt, renderer->drawAlpha);
    }
    else
    {
        swrDrawTriangle(renderer, x1, y1, x2, y2, x3, y3, color1, color2, color3, alpha);
    }
}

static void SWRenderer_drawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2,
                                     float width, uint32_t color1, uint32_t color2, float alpha)
{
    swrDrawLine(renderer, x1, y1, x2, y2, width, swrConvertPixel(color1), swrConvertPixel(color2), alpha);
}

typedef struct
{
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (NULL for sprite fonts)
    int fontTpagIndex;
    int fontPageId;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (NULL for regular fonts)
}
SwrFontState;

static bool swrResolveFontState(SWRenderer* swr, DataWin* dw, Font* font, SwrFontState* state)
{
    state->font = font;
    state->fontTpag = NULL;
    state->fontTpagIndex = 0;
    state->spriteFontSprite = NULL;
    
    if (font->isSpriteFont)
    {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    else
    {
        state->fontTpagIndex = font->tpagIndex;
        if (state->fontTpagIndex < 0) return false;
        
        state->fontTpag = &dw->tpag.items[state->fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= swr->totalTextureCount) return false;
        if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return false;
        
        state->fontPageId = pageId;
    }
    
    return true;
}

static bool swrResolveGlyph(
    SWRenderer* swr, DataWin* dw, SwrFontState* state, FontGlyph* glyph, float cursorX, float cursorY,
    int* tpagIndex, int* pageId, int* sx, int* sy, int* sw, int* sh, float* dx, float* dy
)
{
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != NULL)
    {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIdx = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= swr->totalTextureCount) return false;
        if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pid)) return false;

        *tpagIndex = tpagIdx;
        *pageId = glyphTpag->texturePageId;
        
        *sx = glyphTpag->sourceX;
        *sy = glyphTpag->sourceY;
        *sw = glyphTpag->sourceWidth;
        *sh = glyphTpag->sourceHeight;
        
        *dx = cursorX + glyph->offset;
        *dy = cursorY + glyphTpag->targetY - sprite->originY;
    }
    else
    {
        *tpagIndex = state->fontTpagIndex;
        *pageId = state->fontPageId;
        
        *sx = state->fontTpag->sourceX + glyph->sourceX;
        *sy = state->fontTpag->sourceY + glyph->sourceY;
        *sw = glyph->sourceWidth;
        *sh = glyph->sourceHeight;
        
        *dx = cursorX + glyph->offset;
        *dy = cursorY;
    }
    
    return true;
}

static void swrDrawText(SWRenderer* swr, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t color, float alpha, float lineSeparation)
{
    Renderer* renderer = &swr->base;
    DataWin* dwin = renderer->dataWin;
    
    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dwin->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dwin->font.fonts[fontIndex];
    
    SwrFontState fontState;
    memset(&fontState, 0, sizeof fontState); // silence warning treated as error
    
    if (!swrResolveFontState(swr, dwin, font, &fontState)) return;
    
    // TODO: do we need to mirror the way the text scrolls too?!
    float cosA = 1.0f, sinA = 0.0f, angleRad = 0.0f;
    bool mustRotate = swrMustRotateTolerant(angleDeg);
    if (UNLIKELY(mustRotate))
    {
        angleRad = -angleDeg * M_PI / 180.0f;
        cosA = cosf(angleRad);
        sinA = sinf(angleRad);
    }
    
    int textLen = (int) strlen(text);
    int lineCount = TextUtils_countLines(text, textLen);
    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;
    
    xscale *= font->scaleX;
    yscale *= font->scaleY;

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
                    int fontTpagIndex = 0, pageId = 0;
                    int sx, sy, sw, sh, dw, dh;
                    float dx, dy;
                    if (swrResolveGlyph(swr, dwin, &fontState, glyph, cursorX, cursorY,
                            &fontTpagIndex, &pageId, &sx, &sy, &sw, &sh, &dx, &dy))
                    {
                        dx *= xscale; dx += x;
                        dy *= xscale; dy += y;
                        dw = swrCeiling(xscale * glyph->sourceWidth);
                        dh = swrCeiling(yscale * glyph->sourceHeight);
                        
                        // TODO: at 640x480, for some reason, without this fixup the
                        // letters in the "Name the fallen human." screen don't shake
                        dx = roundf(dx * 2) / 2;
                        dy = roundf(dy * 2) / 2;
                        
                        SWTexture* texture = swr->textures[pageId];
                        
                        if (UNLIKELY(mustRotate))
                        {
                            dx -= x;
                            dy -= y;
                            float ndx = cosA * dx - sinA * dy;
                            float ndy = sinA * dx + cosA * dy;
                            ndx += x;
                            ndy += y;
                            swrDrawSpriteRotated(renderer, ndx, ndy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, 0.0f, 0.0f);
                        }
                        else
                        {
                            swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
                        }
                        
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

static void SWRenderer_drawText(Renderer* renderer, const char* text, float x, float y,
                                float xscale, float yscale, float angleDeg, float lineSeparation)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    swrDrawText(swr, text, x, y, xscale, yscale, angleDeg, renderer->drawColor, renderer->drawAlpha, lineSeparation);
}

static void SWRenderer_drawTextColor(Renderer* renderer, const char* text, float x, float y,
                                     float xscale, float yscale, float angleDeg,
                                     int32_t c1, int32_t c2, int32_t c3, int32_t c4, MAYBE_UNUSED float alpha,
                                     float lineSeparation)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    // TODO: allow c2, c3, c4
    (void) c2;
    (void) c3;
    (void) c4;
    
    swrDrawText(swr, text, x, y, xscale, yscale, angleDeg, c1, renderer->drawAlpha, lineSeparation);
}

static void SWRenderer_drawSpriteTiled(Renderer* renderer, int32_t tpagIndex,
                                       float originX, float originY, float x, float y,
                                       float xscale, float yscale, bool tileX, bool tileY,
                                       float roomW, float roomH, uint32_t color, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;

    if (0 > tpagIndex || dwin->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) return;
    if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return;

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
    
    int sx = tpag->sourceX;
    int sy = tpag->sourceY;
    int sw = tpag->sourceWidth;
    int sh = tpag->sourceHeight;

    int localX0 = tpag->targetX - originX;
    int localY0 = tpag->targetY - originY;
    int localX1 = localX0 + tpag->sourceWidth;
    int localY1 = localY0 + tpag->sourceHeight;
    int sx0 = xscale * localX0;
    int sy0 = yscale * localY0;
    int sx1 = xscale * localX1;
    int sy1 = yscale * localY1;

    for (int dy = startY; endY > dy; dy += tileH) {
        int cy = dy + (int)(originY * ayScale);
        int vy0 = cy + sy0;
        int vy1 = cy + sy1;
        int dh = vy1 - vy0;

        for (int dx = startX; endX > dx; dx += tileW) {
            int cx = dx + (int)(originX * axScale);
            int vx0 = cx + sx0;
            int vx1 = cx + sx1;
            int dw = vx1 - vx0;

            swrDrawSprite(renderer, vx0, vy0, dw, dh, swr->textures[pageId], sx, sy, sw, sh, color, alpha);
        }
    }
}

static void SWRenderer_drawSurfaceTiled(Renderer* renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;

    if (0 > surfaceID || swr->surfaceCount <= (size_t) surfaceID) return;

    SWTexture* surface = swr->surfaces[surfaceID];
    if (!surface) return;

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) surface->width * axScale;
    float tileH = (float) surface->height * ayScale;
    if (0 >= tileW || 0 >= tileH) return;
    
    float originX = 0, originY = 0;

    float startX, endX, startY, endY;
    startX = fmodf(x - originX * axScale, tileW);
    if (startX > 0) startX -= tileW;
    endX = roomW;
    startY = fmodf(y - originY * ayScale, tileH);
    if (startY > 0) startY -= tileH;
    endY = roomH;
    
    int sx = 0, sy = 0;
    int sw = surface->width;
    int sh = surface->height;

    int localX0 = -originX;
    int localY0 = -originY;
    int localX1 = localX0 + surface->width;
    int localY1 = localY0 + surface->height;
    int sx0 = xscale * localX0;
    int sy0 = yscale * localY0;
    int sx1 = xscale * localX1;
    int sy1 = yscale * localY1;

    for (int dy = startY; endY > dy; dy += tileH) {
        int cy = dy + (int)(originY * ayScale);
        int vy0 = cy + sy0;
        int vy1 = cy + sy1;
        int dh = vy1 - vy0;

        for (int dx = startX; endX > dx; dx += tileW) {
            int cx = dx + (int)(originX * axScale);
            int vx0 = cx + sx0;
            int vx1 = cx + sx1;
            int dw = vx1 - vx0;

            swrDrawSprite(renderer, vx0, vy0, dw, dh, surface, sx, sy, sw, sh, color, alpha);
        }
    }
}

static void SWRenderer_flush(Renderer* renderer)
{
    (void)renderer;
    UNIMP();
}

static void SWRenderer_clearScreen(Renderer* renderer, uint32_t color, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    color = swrConvertPixel(color);
#ifdef TRANSPARENT_MASK
    if (alpha >= 0.95f) {
        color |= TRANSPARENT_MASK;
    }
    else if (alpha < 0.01f) {
        color &= ~TRANSPARENT_MASK;
    }
    else {
    #if PIXEL_SIZE == 32
        int alphai = (int)(255.0f * alpha);
        if (alphai < 0) alphai = 0;
        if (alphai > 255) alphai = 255;
        color |= (alphai << 24);
    #elif PIXEL_SIZE == 16
        color |= (alpha > 0.5f);
    #endif
    }
#endif
    
    for (int y = 0; y < swr->height; y++) {
        uintpixel_t* line = &swr->fb[y * swr->fbPitch];
        for (int x = 0; x < swr->width; x++) {
            line[x] = color;
        }
    }
}

static void SWRenderer_gpuSetBlendMode(Renderer* renderer, int32_t mode)
{
    UNIMP();
    (void)renderer; (void)mode;
}

static void SWRenderer_gpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha)
{
    UNIMP();
    (void)renderer; (void)sfactor; (void)dfactor; (void)sfactor_alpha; (void)dfactor_alpha;
}

static void SWRenderer_gpuSetBlendEnable(Renderer* renderer, bool enable)
{
    UNIMP();
    (void)renderer; (void)enable;
}

static void SWRenderer_gpuSetAlphaTestEnable(Renderer* renderer, bool enable)
{
    UNIMP();
    (void)renderer; (void)enable;
}

static void SWRenderer_gpuSetAlphaTestRef(Renderer* renderer, uint8_t ref)
{
    UNIMP();
    (void)renderer; (void)ref;
}

static void SWRenderer_gpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha)
{
    UNIMP();
    (void)renderer; (void)red; (void)green; (void)blue; (void)alpha;
}

static void SWRenderer_gpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha)
{
    UNIMP();
    *red = false;
    *green = false;
    *blue = false;
    *alpha = false;
    (void)renderer; (void)red; (void)green; (void)blue; (void)alpha;
}

static bool SWRenderer_gpuGetBlendEnable(Renderer* renderer)
{
    UNIMP();
    (void)renderer;
    return false;
}

static void SWRenderer_gpuSetFog(Renderer* renderer, bool enable, uint32_t color)
{
    UNIMP();
    (void)renderer; (void)enable; (void)color;
}

static int32_t SWRenderer_createSurface(Renderer* renderer, int32_t width, int32_t height)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    int32_t slot = -1;
    for (size_t i = 0; i < swr->surfaceCount; i++)
    {
        if (swr->surfaces[i] == NULL) {
            slot = (int32_t) i;
            break;
        }
    }
    
    if (slot < 0) {
        fprintf(stderr, "swr: Could not create surface, too many exist at once.\n");
        return slot;
    }
    
    swr->surfaces[slot] = swrCreateTexture(NULL, width, height);
    return slot;
}

static bool SWRenderer_surfaceExists(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount)
        return false;
    
    return swr->surfaces[surfaceID] != NULL;
}

static bool SWRenderer_setRenderTarget(Renderer* renderer, int32_t surfaceID, bool implicitApplicationSurface)
{
    return swrSwitchToSurface(renderer, surfaceID, implicitApplicationSurface);
}

static float SWRenderer_getSurfaceWidth(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (surfaceID == APPLICATION_SURFACE_ID)
        return (float)(int)((swr->drawingToSurface ? swr->mainWidth : swr->width) / swr->scaleX);
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL)
        return 0.0f;
    
    return (float) swr->surfaces[surfaceID]->width;
}

static float SWRenderer_getSurfaceHeight(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (surfaceID == APPLICATION_SURFACE_ID) 
        return (float)(int)((swr->drawingToSurface ? swr->mainHeight : swr->height) / swr->scaleY);
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL)
        return 0.0f;
    
    return (float) swr->surfaces[surfaceID]->height;
}

static void SWRenderer_drawSurface(Renderer* renderer, int32_t surfaceID,
                                   int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight,
                                   float x, float y, float xscale, float yscale, float angleDeg,
                                   uint32_t color, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    SWTexture* surface, localSurface;
    if (surfaceID == APPLICATION_SURFACE_ID) {
        localSurface.buffer = swr->drawingToSurface ? swr->mainFb : swr->fb;
        localSurface.width = swr->drawingToSurface ? swr->mainWidth : swr->width;
        localSurface.height = swr->drawingToSurface ? swr->mainHeight : swr->height;
        surface = &localSurface;
    } else {
        if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL) {
            fprintf(stderr, "swr: Invalid surface id %d for drawSurface\n", surfaceID);
            return;
        }
        
        surface = swr->surfaces[surfaceID];
    }
    
    int sx = srcLeft;
    int sy = srcTop;
    int sw = srcWidth;
    int sh = srcHeight;
    
    int tw = (int)(srcWidth * swr->scaleX);
    int th = (int)(srcHeight * swr->scaleY);
    
    float dx = x;
    float dy = y;
    int dw = (int)(xscale * tw);
    int dh = (int)(yscale * th);

    if (UNLIKELY(swrMustRotate(angleDeg)))
    {
        float pivotX = (x - dx) * swrSgn(xscale);
        float pivotY = (y - dy) * swrSgn(yscale);
        
        if (tw != sw)
            pivotX *= (float)tw / sw;
        if (th != sh)
            pivotY *= (float)th / sh;
        
        swrDrawSpriteRotated(renderer, dx, dy, dw, dh, surface, sx, sy, sw, sh, color, alpha, angleDeg, pivotX, pivotY);
    }
    else
    {
        swrDrawSprite(renderer, dx, dy, dw, dh, surface, sx, sy, sw, sh, color, alpha);
    }
}

static void SWRenderer_surfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (surfaceID == APPLICATION_SURFACE_ID) {
        fprintf(stderr, "swr: Don't support resizing the application window with this.  There must be another way!\n");
        return;
    }
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL) {
        fprintf(stderr, "swr: Cannot resize surface id %d, it's invalid\n", surfaceID);
        return;
    }
    
    swrFreeTexture(swr->surfaces[surfaceID]);
    swr->surfaces[surfaceID] = swrCreateTexture(NULL, width, height);
}

static void SWRenderer_surfaceFree(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (surfaceID == APPLICATION_SURFACE_ID) {
        fprintf(stderr, "swr: Don't support resizing the application window with this.  There must be another way!\n");
        return;
    }
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL) {
        fprintf(stderr, "swr: Cannot resize surface id %d, it's invalid\n", surfaceID);
        return;
    }
    
    swrFreeTexture(swr->surfaces[surfaceID]);
    swr->surfaces[surfaceID] = NULL;
}

static void SWRenderer_surfaceCopy(Renderer* renderer,
                                   int32_t DestSurfaceID, int32_t DestX, int32_t DestY,
                                   int32_t SrcSurfaceID, int32_t SrcX, int32_t SrcY,
                                   int32_t SrcW, int32_t SrcH, bool part)
{
    UNIMP();
    (void)renderer;
    (void)DestSurfaceID; (void)DestX; (void)DestY;
    (void)SrcSurfaceID; (void)SrcX; (void)SrcY;
    (void)SrcW; (void)SrcH; (void)part;
}

static bool SWRenderer_surfaceGetPixels(Renderer* renderer, int32_t surfaceID, uint8_t* outRGBA)
{
    UNIMP();
    (void)renderer; (void)surfaceID; (void)outRGBA;
    return false;
}

static int32_t SWRenderer_createSpriteFromSurface(Renderer* renderer, int32_t surfaceID,
                                                   int32_t x, int32_t y, int32_t w, int32_t h,
                                                   bool removeback, bool smooth,
                                                   int32_t xorig, int32_t yorig)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    swrTransformPosIntIfNeeded(swr, &x, &y);
    swrTransformSizeIntIfNeeded(swr, &w, &h);
    swrTransformSizeIntIfNeeded(swr, &xorig, &yorig);
    
    (void) removeback;
    (void) smooth;
    
    if (surfaceID != -1) {
        fprintf(stderr, "%s: Surfaces other than application_surface aren't supported yet!\n", __func__);
        return 0;
    }

    int32_t texturePageId = swrFindSurfaceTextureSlot(swr);
    int32_t tpagIndex = swrFindSurfaceTPagSlot(swr);
    if (texturePageId == -1 || tpagIndex == -1) {
        fprintf(stderr, "%s: Surface overflow!!\n", __func__);
        return 0;
    }
    
    SWTexture* tex = swrCreateTexture(NULL, w, h);
    
    // grab the pixels.
    for (int iy = 0; iy < h; iy++)
    {
        uintpixel_t* dstline = &tex->buffer[iy * tex->width];
        if ((iy + y) < 0 || (iy + y) >= swr->height)
        {
            for (int ix = 0; ix < w; ix++)
                dstline[ix] = 0;
            
            continue;
        }
        
        uintpixel_t* srcline = &swr->fb[(iy + y) * swr->width + x];
        
        int ix = 0, sx = x;
        // left edge
        for (; sx < 0 && ix < w; sx++, ix++)
            dstline[ix] = 0;
        
        // in-bounds
        for (; sx < swr->width && ix < w; sx++, ix++)
#if PIXEL_SIZE == 8
            dstline[ix] = srcline[ix];
#else
            dstline[ix] = srcline[ix] | TRANSPARENT_MASK;
#endif

        // right edge
        for (; ix < w; ix++)
            dstline[ix] = 0;
    }
    
    int32_t spriteW = w;
    int32_t spriteH = h;
    
    int32_t targetW = spriteW;
    int32_t targetH = spriteH;
    swrReverseTransformSizeIntIfNeeded(swr, &targetW, &targetH);

    swr->textures[texturePageId] = tex;

    // TODO[MrPowerGamerBR]: This is supposed to be refactored, not to modify data.win structs directly.
    DataWin* dw = swr->base.dataWin;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t) spriteW;
    tpag->sourceHeight = (uint16_t) spriteH;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t) (spriteW / swr->scaleX);
    tpag->targetHeight = (uint16_t) (spriteH / swr->scaleY);
    tpag->boundingWidth = (uint16_t) spriteW;
    tpag->boundingHeight = (uint16_t) spriteH;
    tpag->texturePageId = texturePageId;
    
    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, swr->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    // name was set by DataWin_allocSpriteSlot ("__newsprite<N>"); don't overwrite it here
    sprite->width = (uint32_t) tpag->targetWidth;
    sprite->height = (uint32_t) tpag->targetHeight;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = (int32_t*) safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t) tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = nullptr;

    fprintf(stderr, "%s: Allocated surface sprite with ID %d\n", __func__, spriteIndex);
    return spriteIndex;
}

static void SWRenderer_deleteSprite(Renderer* renderer, int32_t spriteIndex)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    DataWin* dw = renderer->dataWin;
    if (0 > spriteIndex || dw->sprt.count <= (uint32_t) spriteIndex) return;

    // Refuse to delete original data.win sprites
    if (swr->originalSpriteCount > (uint32_t) spriteIndex) {
        fprintf(stderr, "%s: Cannot delete sprite with index %d, it's invalid.\n", __func__, spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return; // already deleted

    for (uint32_t i = 0; i < sprite->textureCount; i++)
    {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t) tpagIdx >= swr->originalTPagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && swr->totalTextureCount > (uint32_t) pageId) {
                swrFreeTexture(swr->textures[pageId]);
                swr->textures[pageId] = NULL;
            }
            // Mark TPAG slot as free for reuse
            tpag->texturePageId = -1;
        }
    }
    
    free(sprite->tpagIndices);
    sprite->tpagIndices = NULL;
    
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;

    fprintf(stderr, "SWR: Deleted sprite %d\n", spriteIndex);
}

static void SWRenderer_drawTiledPart(Renderer* renderer, int32_t tpagIndex,
                                     int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH,
                                     float dstX, float dstY, float dstW, float dstH,
                                     uint32_t color, float alpha)
{
    UNIMP();
    (void)renderer; (void)tpagIndex;
    (void)srcX; (void)srcY; (void)srcW; (void)srcH;
    (void)dstX; (void)dstY; (void)dstW; (void)dstH;
    (void)color; (void)alpha;
}

static int32_t SWRenderer_ensureApplicationSurface(Renderer* renderer, int32_t width, int32_t height)
{
    // We don't evict surfaces, and especially not the primary framebuffer,
    // but if we did, this is where we would restore it.
    (void) renderer;
    (void) width;
    (void) height;
    
    return APPLICATION_SURFACE_ID;
}

static RendererVtable swrVtable;

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    uintpixel_t pxcolor = swrConvertPixel(color);
    
    size_t fbSize = swr->fbPitch;
    fbSize *= swr->height;
    for (size_t i = 0; i < fbSize; i++)
    {
        swr->fb[i] = pxcolor;
    }
}

static uint32_t SWRenderer_spriteGetTexture(Renderer* renderer, int32_t tpagIndex)
{
    (void) renderer;

    return (uint32_t) tpagIndex + 1;
}

static uint32_t SWRenderer_surfaceGetTexture(Renderer* renderer, int32_t surfaceID)
{
    (void) renderer;
    (void) surfaceID;

    return (uint32_t) -1;
}

static float SWRenderer_textureGetTexelWidth(Renderer* renderer, uint32_t texID)
{
    (void) renderer;
    (void) texID;
    
    return 1.0f;
}

static float SWRenderer_textureGetTexelHeight(Renderer* renderer, uint32_t texID)
{
    (void) renderer;
    (void) texID;
    
    return 1.0f;
}

static bool SWRenderer_textureGetUVs(Renderer* renderer, uint32_t texID, float* outUVs)
{
    (void) renderer;
    (void) texID;
    (void) outUVs;
    
    return false;
}

static void SWRenderer_textureSetStage(Renderer* renderer, int32_t slot, uint32_t texID)
{
    (void) renderer;
    (void) slot;
    (void) texID;
}

static bool SWRenderer_shaderIsCompiled(Renderer* renderer, int32_t shader)
{
    (void) renderer;
    (void) shader;
    
    return false;
}

static bool SWRenderer_shadersSupported(void)
{
    return false;
}

static void SWRenderer_gpuSetShader(Renderer* renderer, int32_t shaderIndex)
{
    (void) renderer;
    (void) shaderIndex;
}

static void SWRenderer_gpuResetShader(Renderer* renderer)
{
    (void) renderer;
}

static int32_t SWRenderer_shaderGetUniform(Renderer* renderer, int32_t shaderIndex, char* uniform)
{
    (void) renderer;
    (void) shaderIndex;
    (void) uniform;
    
    return 0;
}

static int32_t SWRenderer_shaderGetSamplerIndex(Renderer* renderer, int32_t shaderIndex, char* uniform)
{
    (void) renderer;
    (void) shaderIndex;
    (void) uniform;
    
    return 0;
}

static void SWRenderer_shaderSetUniformF(Renderer* renderer, int32_t handle, int32_t count, float value1, float value2, float value3, float value4)
{
    (void) renderer;
    (void) handle;
    (void) count;
    (void) value1;
    (void) value2;
    (void) value3;
    (void) value4;
}

static void SWRenderer_shaderSetUniformI(Renderer* renderer, int32_t handle, int32_t count, int32_t value1, int32_t value2, int32_t value3, int32_t value4)
{
    (void) renderer;
    (void) handle;
    (void) count;
    (void) value1;
    (void) value2;
    (void) value3;
    (void) value4;
}

Renderer* SWRenderer_create(void)
{
    SWRenderer* swr = (SWRenderer*) safeCalloc(1, sizeof(SWRenderer));
    swr->base.vtable = &swrVtable;
    swrVtable.init                     = SWRenderer_init;
    swrVtable.destroy                  = SWRenderer_destroy;
    swrVtable.beginFrame               = SWRenderer_beginFrame;
    swrVtable.endFrameInit             = SWRenderer_endFrameInit;
    swrVtable.endFrameEnd              = SWRenderer_endFrameEnd;
    swrVtable.beginView                = SWRenderer_beginView;
    swrVtable.endView                  = SWRenderer_endView;
    swrVtable.beginGUI                 = SWRenderer_beginGUI;
    swrVtable.setGuiProjection         = SWRenderer_setGuiProjection;
    swrVtable.endGUI                   = SWRenderer_endGUI;
    swrVtable.drawSprite               = SWRenderer_drawSprite;
    swrVtable.drawSpritePart           = SWRenderer_drawSpritePart;
    swrVtable.drawSpritePos            = SWRenderer_drawSpritePos;
    swrVtable.drawRectangle            = SWRenderer_drawRectangle;
    swrVtable.drawRectangleColor       = SWRenderer_drawRectangleColor;
    swrVtable.drawLine                 = SWRenderer_drawLine;
    swrVtable.drawTriangle             = SWRenderer_drawTriangle;
    swrVtable.drawLineColor            = SWRenderer_drawLineColor;
    swrVtable.drawText                 = SWRenderer_drawText;
    swrVtable.drawTextColor            = SWRenderer_drawTextColor;
    swrVtable.flush                    = SWRenderer_flush;
    swrVtable.clearScreen              = SWRenderer_clearScreen;
    swrVtable.createSpriteFromSurface  = SWRenderer_createSpriteFromSurface;
    swrVtable.deleteSprite             = SWRenderer_deleteSprite;
    swrVtable.gpuSetBlendMode          = SWRenderer_gpuSetBlendMode;
    swrVtable.gpuSetBlendModeExt       = SWRenderer_gpuSetBlendModeExt;
    swrVtable.gpuSetBlendEnable        = SWRenderer_gpuSetBlendEnable;
    swrVtable.gpuSetAlphaTestEnable    = SWRenderer_gpuSetAlphaTestEnable;
    swrVtable.gpuSetAlphaTestRef       = SWRenderer_gpuSetAlphaTestRef;
    swrVtable.gpuSetColorWriteEnable   = SWRenderer_gpuSetColorWriteEnable;
    swrVtable.gpuGetColorWriteEnable   = SWRenderer_gpuGetColorWriteEnable;
    swrVtable.gpuGetBlendEnable        = SWRenderer_gpuGetBlendEnable;
    swrVtable.gpuSetFog                = SWRenderer_gpuSetFog;
    swrVtable.drawSpriteTiled          = SWRenderer_drawSpriteTiled;
    swrVtable.drawSurfaceTiled         = SWRenderer_drawSurfaceTiled;
    swrVtable.createSurface            = SWRenderer_createSurface;
    swrVtable.surfaceExists            = SWRenderer_surfaceExists;
    swrVtable.setRenderTarget          = SWRenderer_setRenderTarget;
    swrVtable.ensureApplicationSurface = SWRenderer_ensureApplicationSurface;
    swrVtable.getSurfaceWidth          = SWRenderer_getSurfaceWidth;
    swrVtable.getSurfaceHeight         = SWRenderer_getSurfaceHeight;
    swrVtable.drawSurface              = SWRenderer_drawSurface;
    swrVtable.surfaceResize            = SWRenderer_surfaceResize;
    swrVtable.surfaceFree              = SWRenderer_surfaceFree;
    swrVtable.surfaceCopy              = SWRenderer_surfaceCopy;
    swrVtable.surfaceGetPixels         = SWRenderer_surfaceGetPixels;
    swrVtable.drawTiledPart            = SWRenderer_drawTiledPart;
    swrVtable.spriteGetTexture         = SWRenderer_spriteGetTexture;
    swrVtable.surfaceGetTexture        = SWRenderer_surfaceGetTexture;
    swrVtable.textureGetTexelWidth     = SWRenderer_textureGetTexelWidth;
    swrVtable.textureGetTexelHeight    = SWRenderer_textureGetTexelHeight;
    swrVtable.textureGetUVs            = SWRenderer_textureGetUVs;
    swrVtable.textureSetStage          = SWRenderer_textureSetStage;
    swrVtable.gpuSetShader             = SWRenderer_gpuSetShader;
    swrVtable.gpuResetShader           = SWRenderer_gpuResetShader;
    swrVtable.shaderGetUniform         = SWRenderer_shaderGetUniform;
    swrVtable.shaderGetSamplerIndex    = SWRenderer_shaderGetSamplerIndex;
    swrVtable.shaderSetUniformF        = SWRenderer_shaderSetUniformF;
    swrVtable.shaderSetUniformI        = SWRenderer_shaderSetUniformI;
    swrVtable.shaderIsCompiled         = SWRenderer_shaderIsCompiled;
    swrVtable.shadersSupported         = SWRenderer_shadersSupported;
    
    swrVtable.drawTile                 = NULL;
    
    swr->base.drawColor = 0xFFFFFF;
    swr->base.drawAlpha = 1.0f;
    swr->base.drawFont = -1;
    swr->base.drawHalign = 0;
    swr->base.drawValign = 0;
    swr->base.circlePrecision = 24;

    return (Renderer*) swr;
}
