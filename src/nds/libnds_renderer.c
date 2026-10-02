#include "libnds_renderer.h"

#include "common.h"
#include "renderer.h"
#include "runner.h"
#include "data_win.h"
#include "utils.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include "nds_image.h"

static u16* framebuffer = NULL;   // VRAM
static u16* backbuffer  = NULL;   // main RAM

#define DS_SCREEN_WIDTH 256
#define DS_SCREEN_HEIGHT 192

// Helpers
static void libndsEnsureSurfaceCapacity(LibNDSRenderer *libnds, uint32_t needed) {
    if (needed <= libnds->surfaceCapacity) return;
    uint32_t newCap = libnds->surfaceCapacity ? libnds->surfaceCapacity * 2 : 16;
    while (newCap < needed) newCap *= 2;
    libnds->surfaceWidths = (int32_t *)safeRealloc(libnds->surfaceWidths, newCap * sizeof(int32_t));
    libnds->surfaceHeights = (int32_t *)safeRealloc(libnds->surfaceHeights, newCap * sizeof(int32_t));
    libnds->surfaceExistsFlag = (bool *)safeRealloc(libnds->surfaceExistsFlag, newCap * sizeof(bool));
    for (uint32_t i = libnds->surfaceCapacity; i < newCap; i++) {
        libnds->surfaceWidths[i] = 0;
        libnds->surfaceHeights[i] = 0;
        libnds->surfaceExistsFlag[i] = false;
    }
    libnds->surfaceCapacity = newCap;
    if (needed > libnds->surfaceCount) libnds->surfaceCount = needed;
}

// ===[ Vtable stubs ]===

static void libndsInit(Renderer *renderer, DataWin *dataWin) {
    renderer->dataWin = dataWin;
    Matrix4f world;
    Matrix4f_identity(&world);
    renderer->gmlMatrices[MATRIX_WORLD] = world;

    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    framebuffer = (u16*)bgGetGfxPtr(bg);
    backbuffer  = (u16*)malloc(256 * 192 * sizeof(u16));
    if (!backbuffer)
        exit(1);

    logInfo("LibNDS renderer initialized\n");
}

static void libndsDestroy(Renderer *renderer) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    free(libnds->surfaceWidths);
    free(libnds->surfaceHeights);
    free(libnds->surfaceExistsFlag);
    free(libnds);
}

static void libndsBeginFrame(Renderer *renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH){
    dmaFillHalfWords(RGB15(3, 3, 3) | BIT(15), backbuffer, DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT * sizeof(u16));
}

static void libndsEndFrameInit(Renderer *renderer){}
static void libndsEndFrameEnd(Renderer *renderer){
    DC_FlushRange(backbuffer, DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT * sizeof(u16));
    dmaCopyHalfWords(3, backbuffer, framebuffer, DS_SCREEN_WIDTH * DS_SCREEN_HEIGHT * sizeof(u16));
    swiWaitForVBlank();
}
static void libndsBeginView(Renderer *renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle){
    LibNDSRenderer* lbds = (LibNDSRenderer*)renderer;

    //Set current viewport size
    lbds->viewX = viewX;
    lbds->viewY = viewY;
}

static void libndsEndView(Renderer *renderer) {}
static void libndsApplyProjection(Renderer *renderer, const Matrix4f *viewMatrix, const Matrix4f *projectionMatrix) {}
static void libndsBeginGUI(Renderer *renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId) {}
static void libndsSetGuiProjection(Renderer *renderer, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, bool renderingToUserSurface) {}
static void libndsEndGUI(Renderer *renderer) {}

//Get a texture pages png data and load the pixel data
static const u16* GetPixelData(LibNDSRenderer *lbds, int32_t texturePageId, int *outW, int *outH){
    //Load into cache array if not there already
    if (!lbds->texPixels[texturePageId]){
        //Create path
        char path[64];
        snprintf(path, sizeof(path), "nitro:/pg_%d.png", texturePageId);
        
        uint16_t* PixelData;
        int w = 0;
        int h = 0;
        nds_load_png_5551(path, &PixelData, &w, &h); //Load pixel data

        //Add to cache
        lbds->texPixels[texturePageId] = PixelData;
        lbds->texW[texturePageId] = w;
        lbds->texH[texturePageId] = h;
    }

    //Return array entry
    *outW = lbds->texW[texturePageId]; //Set the outW set in arg to the textures width
    *outH = lbds->texH[texturePageId]; //Set the outH set in arg to the textures height
    return lbds->texPixels[texturePageId]; //Return pixel data
}

static void libndsDrawSprite(Renderer *renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    LibNDSRenderer* lbds = (LibNDSRenderer*)renderer;
    DataWin* dw = renderer->dataWin;

    //The sprite requested is out of the texture page bounds
    if (0 > tpagIndex || (uint32_t) tpagIndex >= dw->tpag.count)
        return;

    //Get which texture page index its at
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];

    // Get crop region from atlas entry (falls back to full bounding box if unmapped)
    float cropX = 0.0f, cropY = 0.0f;
    float cropW = (float) tpag->boundingWidth;
    float cropH = (float) tpag->boundingHeight;
    if (lbds->atlasTPAGCount > (uint32_t) tpagIndex) {
        AtlasTPAGEntry* entry = &lbds->atlasTPAGEntries[tpagIndex];

        //The texture was invalid(?)
        if (entry->atlasId != 0xFFFF) {
            cropX = (float) entry->cropX;
            cropY = (float) entry->cropY;
            cropW = (float) entry->cropW;
            cropH = (float) entry->cropH;
        }
    }

    
    // Compute 4 screen-space corners (tristrip Z-pattern: top-left, top-right, bottom-left, bottom-right)
    // sx0/sy0 = top-left, sx1/sy1 = top-right, sx2/sy2 = bottom-left, sx3/sy3 = bottom-right
    float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
    bool hasRotation = angleDeg != 0.0f;

    /*
    I'll worry about rotation later...
    if (hasRotation) {
        // Rotated: compute 4 transformed corners via matrix, same approach as the GLFW renderer
        // Position the cropped region within the original bounding box
        float localX0 = cropX - originX;
        float localY0 = cropY - originY;
        float localX1 = cropX + cropW - originX;
        float localY1 = cropY + cropH - originY;

        // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
        // Negate angle because Y-down coordinate system
        float angleRad = -angleDeg * ((float) M_PI / 180.0f);
        Matrix4f transform;
        Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

        float gx0, gy0, gx1, gy1, gx2, gy2, gx3, gy3;
        Matrix4f_transformPoint(&transform, localX0, localY0, &gx0, &gy0); // top-left
        Matrix4f_transformPoint(&transform, localX1, localY0, &gx1, &gy1); // top-right
        Matrix4f_transformPoint(&transform, localX0, localY1, &gx2, &gy2); // bottom-left
        Matrix4f_transformPoint(&transform, localX1, localY1, &gx3, &gy3); // bottom-right

        // Apply view offset and scale
        sx0 = (gx0 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
        sy0 = (gy0 - (float) gs->viewY) * gs->scaleY + gs->offsetY;
        sx1 = (gx1 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
        sy1 = (gy1 - (float) gs->viewY) * gs->scaleY + gs->offsetY;
        sx2 = (gx2 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
        sy2 = (gy2 - (float) gs->viewY) * gs->scaleY + gs->offsetY;
        sx3 = (gx3 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
        sy3 = (gy3 - (float) gs->viewY) * gs->scaleY + gs->offsetY;
    } else {*/
        // Axis-aligned: simple rect math
        // Position the cropped region within the original bounding box
        float gameX1 = x + (cropX - originX) * xscale;
        float gameY1 = y + (cropY - originY) * yscale;
        float gameX2 = x + (cropX + cropW - originX) * xscale;
        float gameY2 = y + (cropY + cropH - originY) * yscale;

        // Apply view offset and scale
        sx0 = (gameX1 - (float) lbds->viewX) * lbds->scaleX + lbds->offsetX;
        sy0 = (gameY1 - (float) lbds->viewY) * lbds->scaleY + lbds->offsetY;
        sx1 = (gameX2 - (float) lbds->viewX) * lbds->scaleX + lbds->offsetX;
        sy1 = (gameY1 - (float) lbds->viewY) * lbds->scaleY + lbds->offsetY;
        sx2 = (gameX1 - (float) lbds->viewX) * lbds->scaleX + lbds->offsetX;
        sy2 = (gameY2 - (float) lbds->viewY) * lbds->scaleY + lbds->offsetY;
        sx3 = (gameX2 - (float) lbds->viewX) * lbds->scaleX + lbds->offsetX;
        sy3 = (gameY2 - (float) lbds->viewY) * lbds->scaleY + lbds->offsetY;
    //}

    

    //Cull off screen sprites
    float minSX = fminf(fminf(sx0, sx1), fminf(sx2, sx3));
    float maxSX = fmaxf(fmaxf(sx0, sx1), fmaxf(sx2, sx3));
    float minSY = fminf(fminf(sy0, sy1), fminf(sy2, sy3));
    float maxSY = fmaxf(fmaxf(sy0, sy1), fmaxf(sy2, sy3));
    if (maxSX < 0.0f || minSX > DS_SCREEN_WIDTH || maxSY < 0.0f || minSY > DS_SCREEN_HEIGHT)
        return;
    
    
    
    //Get texture page pixel data
    int texW = 0;
    int texH = 0;
    const u16* TexturePagePixels = GetPixelData(lbds, tpag->texturePageId, &texW, &texH);

    //Draw all pixle data from GetPixelData on screen
	for (int sy = sy0; sy < sy2; sy++)
	{
        //Don't wrap on the y
        if (!(sy >= 0 && sy < DS_SCREEN_HEIGHT))
            continue;

        //logInfo("hi\n");
		const u16* src = TexturePagePixels + (tpag->sourceY + (sy - (int)sy0)) * texW + tpag->sourceX;
		u16* dst = backbuffer + sy * DS_SCREEN_WIDTH;

		for (int sx = sx0; sx < sx1; sx++)
		{
            //Don't wrap on the x
            if (!(sx >= 0 && sx < DS_SCREEN_WIDTH))
                continue;

            u16 c = src[sx - (int)sx0];
            if (c & BIT(15))   // opaque
                dst[sx] = c;
		}
	}
    
    //logInfo("X: %.2f, Y: %.2f\n", x, y);
}

static void libndsDrawSpritePart(Renderer *renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {}
static void libndsDrawSpritePartColor(Renderer *renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha) {}
static void libndsDrawSpritePos(Renderer *renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {}
static void libndsDrawRectangle(Renderer *renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {}
static void libndsDrawRectangleColor(Renderer *renderer, float x1, float y1, float x2, float y2, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha, bool outline) {}
static void libndsDrawLine(Renderer *renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {}
static void libndsDrawLineColor(Renderer *renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {}
static void libndsDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline) {}
static void libndsDrawText(Renderer *renderer, const char *text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation) {}
static void libndsDrawTextColor(Renderer *renderer, const char *text, float x, float y, float xscale, float yscale, float angleDeg, int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha, float lineSeparation) {}
static void libndsFlush(Renderer *renderer) {}
static void libndsClearScreen(Renderer *renderer, uint32_t color, float alpha) {}

static int32_t libndsCreateSpriteFromSurface(Renderer *renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    return -1;
}
static void libndsDeleteSprite(Renderer *renderer, int32_t spriteIndex) {}

static BlendFactors libndsGpuGetBlendFactors(Renderer *renderer) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    return libnds->blendFactors;
}
static int32_t libndsGpuGetBlendMode(Renderer *renderer) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    return libnds->blendMode;
}
static void libndsGpuSetBlendMode(Renderer *renderer, int32_t mode) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    libnds->blendMode = mode;
}
static void libndsGpuSetBlendModeExt(Renderer *renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    libnds->blendFactors.src = sfactor;
    libnds->blendFactors.dst = dfactor;
    libnds->blendFactors.srcAlpha = sfactor_alpha;
    libnds->blendFactors.dstAlpha = dfactor_alpha;
}
static void libndsGpuSetBlendEnable(Renderer *renderer, bool enable) {
    ((LibNDSRenderer *)renderer)->blendEnable = enable;
}
static void libndsGpuSetAlphaTestEnable(Renderer *renderer, bool enable) {
    ((LibNDSRenderer *)renderer)->alphaTestEnable = enable;
}
static bool libndsGpuGetAlphaTestEnable(Renderer *renderer) {
    return ((LibNDSRenderer *)renderer)->alphaTestEnable;
}
static void libndsGpuSetAlphaTestRef(Renderer *renderer, uint8_t ref) {
    ((LibNDSRenderer *)renderer)->alphaTestRef = ref;
}
static void libndsGpuSetColorWriteEnable(Renderer *renderer, bool red, bool green, bool blue, bool alpha) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    libnds->colorWriteR = red;
    libnds->colorWriteG = green;
    libnds->colorWriteB = blue;
    libnds->colorWriteA = alpha;
}
static void libndsGpuGetColorWriteEnable(Renderer *renderer, bool *red, bool *green, bool *blue, bool *alpha) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    if (red) *red = libnds->colorWriteR;
    if (green) *green = libnds->colorWriteG;
    if (blue) *blue = libnds->colorWriteB;
    if (alpha) *alpha = libnds->colorWriteA;
}
static bool libndsGpuGetBlendEnable(Renderer *renderer) {
    return ((LibNDSRenderer *)renderer)->blendEnable;
}
static void libndsGpuSetFog(Renderer *renderer, bool enable, uint32_t color) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    libnds->fogEnable = enable;
    libnds->fogColor = color;
}
static void libndsDrawTile(Renderer *renderer, RoomTile *tile, float offsetX, float offsetY) {}
static void libndsDrawSpriteTiled(Renderer *renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {}

static int32_t libndsCreateSurface(Renderer *renderer, int32_t width, int32_t height) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    uint32_t id = libnds->surfaceCount;
    libndsEnsureSurfaceCapacity(libnds, id + 1);
    libnds->surfaceWidths[id] = width;
    libnds->surfaceHeights[id] = height;
    libnds->surfaceExistsFlag[id] = true;
    return (int32_t)id;
}
static bool libndsSurfaceExists(Renderer *renderer, int32_t surfaceID) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= libnds->surfaceCount) return false;
    return libnds->surfaceExistsFlag[surfaceID];
}
static bool libndsSetRenderTarget(Renderer *renderer, int32_t surfaceID, bool implicitApplicationSurface) {
    if (surfaceID == APPLICATION_SURFACE_ID || surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) return true;
    return libndsSurfaceExists(renderer, surfaceID);
}
static int32_t libndsEnsureApplicationSurface(Renderer *renderer, int32_t width, int32_t height) {
    return APPLICATION_SURFACE_ID;
}
static float libndsGetSurfaceWidth(Renderer *renderer, int32_t surfaceID) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= libnds->surfaceCount) return 0.0f;
    if (!libnds->surfaceExistsFlag[surfaceID]) return 0.0f;
    return (float)libnds->surfaceWidths[surfaceID];
}
static float libndsGetSurfaceHeight(Renderer *renderer, int32_t surfaceID) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= libnds->surfaceCount) return 0.0f;
    if (!libnds->surfaceExistsFlag[surfaceID]) return 0.0f;
    return (float)libnds->surfaceHeights[surfaceID];
}
static void libndsDrawSurface(Renderer *renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {}
static void libndsDrawSurfaceColor(Renderer *renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha) {}
static void libndsDrawSurfaceTiled(Renderer *renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha) {}
static void libndsSurfaceResize(Renderer *renderer, int32_t surfaceID, int32_t width, int32_t height) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= libnds->surfaceCount) return;
    if (!libnds->surfaceExistsFlag[surfaceID]) return;
    libnds->surfaceWidths[surfaceID] = width;
    libnds->surfaceHeights[surfaceID] = height;
}
static void libndsSurfaceFree(Renderer *renderer, int32_t surfaceID) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= libnds->surfaceCount) return;
    libnds->surfaceExistsFlag[surfaceID] = false;
    libnds->surfaceWidths[surfaceID] = 0;
    libnds->surfaceHeights[surfaceID] = 0;
}
static void libndsSurfaceCopy(Renderer *renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {}
static bool libndsSurfaceGetPixels(Renderer *renderer, int32_t surfaceID, uint8_t *outRGBA) {
    return false;
}
static void libndsDrawTiledPart(Renderer *renderer, int32_t tpagIndex, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, float dstX, float dstY, float dstW, float dstH, uint32_t color, float alpha) {}

static void libndsGpuSetShader(Renderer *renderer, int32_t shaderIndex) {
    renderer->currentShader = shaderIndex;
}
static void libndsGpuResetShader(Renderer *renderer) {
    renderer->currentShader = -1;
}
static int32_t libndsShaderGetUniform(Renderer *renderer, int32_t shaderIndex, char *uniform) {
    return -1;
}
static int32_t libndsShaderGetSamplerIndex(Renderer *renderer, int32_t shaderIndex, char *uniform) {
    return -1;
}
static void libndsShaderSetUniformF(Renderer *renderer, int32_t handle, int32_t count, float v1, float v2, float v3, float v4) {}
static void libndsShaderSetUniformFArray(Renderer *renderer, int32_t handle, float *values, uint32_t count) {}
static void libndsShaderSetUniformI(Renderer *renderer, int32_t handle, int32_t count, int32_t v1, int32_t v2, int32_t v3, int32_t v4) {}
static uint32_t libndsSpriteGetTexture(Renderer *renderer, int32_t tpagIndex) {
    return 0;
}
static uint32_t libndsSurfaceGetTexture(Renderer *renderer, int32_t surfaceID) {
    return 0;
}
static float libndsTextureGetTexelWidth(Renderer *renderer, uint32_t texID) {
    return 1.0f;
}
static float libndsTextureGetTexelHeight(Renderer *renderer, uint32_t texID) {
    return 1.0f;
}
static bool libndsTextureGetUVs(Renderer *renderer, uint32_t texID, float *outUVs) {
    return false;
}
static void libndsTextureSetStage(Renderer *renderer, int32_t slot, uint32_t texID) {}
static bool libndsShaderIsCompiled(Renderer *renderer, int32_t shader) {
    return false;
}
static bool libndsShadersSupported(void) {
    return false;
}
static void libndsSetMatrix(Renderer *renderer, int32_t matrixType, Matrix4f matrix) {
    if (matrixType >= 0 && matrixType < MATRICES_MAX) renderer->gmlMatrices[matrixType] = matrix;
}
static void libndsGpuSetTexFilter(Renderer *renderer, bool enable) {}

static RendererVtable libndsVtable;

Renderer* libndsRenderer_create(void) {
    LibNDSRenderer *libnds = (LibNDSRenderer *)safeCalloc(1, sizeof(LibNDSRenderer));
    libnds->base.vtable = &libndsVtable;
    libndsVtable.init = libndsInit;
    libndsVtable.destroy = libndsDestroy;
    libndsVtable.beginFrame = libndsBeginFrame;
    libndsVtable.endFrameInit = libndsEndFrameInit;
    libndsVtable.endFrameEnd = libndsEndFrameEnd;
    libndsVtable.beginView = libndsBeginView;
    libndsVtable.endView = libndsEndView;
    libndsVtable.applyProjection = libndsApplyProjection;
    libndsVtable.beginGUI = libndsBeginGUI;
    libndsVtable.setGuiProjection = libndsSetGuiProjection;
    libndsVtable.endGUI = libndsEndGUI;
    libndsVtable.drawSprite = libndsDrawSprite;
    libndsVtable.drawSpritePart = libndsDrawSpritePart;
    libndsVtable.drawSpritePartColor = libndsDrawSpritePartColor;
    libndsVtable.drawSpritePos = libndsDrawSpritePos;
    libndsVtable.drawRectangle = libndsDrawRectangle;
    libndsVtable.drawRectangleColor = libndsDrawRectangleColor;
    libndsVtable.drawLine = libndsDrawLine;
    libndsVtable.drawTriangle = libndsDrawTriangle;
    libndsVtable.drawLineColor = libndsDrawLineColor;
    libndsVtable.drawText = libndsDrawText;
    libndsVtable.drawTextColor = libndsDrawTextColor;
    libndsVtable.flush = libndsFlush;
    libndsVtable.clearScreen = libndsClearScreen;
    libndsVtable.createSpriteFromSurface = libndsCreateSpriteFromSurface;
    libndsVtable.deleteSprite = libndsDeleteSprite;
    libndsVtable.gpuGetBlendFactors = libndsGpuGetBlendFactors;
    libndsVtable.gpuGetBlendMode = libndsGpuGetBlendMode;
    libndsVtable.gpuSetBlendMode = libndsGpuSetBlendMode;
    libndsVtable.gpuSetBlendModeExt = libndsGpuSetBlendModeExt;
    libndsVtable.gpuSetBlendEnable = libndsGpuSetBlendEnable;
    libndsVtable.gpuSetAlphaTestEnable = libndsGpuSetAlphaTestEnable;
    libndsVtable.gpuGetAlphaTestEnable = libndsGpuGetAlphaTestEnable;
    libndsVtable.gpuSetAlphaTestRef = libndsGpuSetAlphaTestRef;
    libndsVtable.gpuSetColorWriteEnable = libndsGpuSetColorWriteEnable;
    libndsVtable.gpuGetColorWriteEnable = libndsGpuGetColorWriteEnable;
    libndsVtable.gpuGetBlendEnable = libndsGpuGetBlendEnable;
    libndsVtable.gpuSetTexFilter = libndsGpuSetTexFilter;
    libndsVtable.gpuSetFog = libndsGpuSetFog;
    libndsVtable.drawTile = libndsDrawTile;
    libndsVtable.drawSpriteTiled = libndsDrawSpriteTiled;
    libndsVtable.createSurface = libndsCreateSurface;
    libndsVtable.surfaceExists = libndsSurfaceExists;
    libndsVtable.setRenderTarget = libndsSetRenderTarget;
    libndsVtable.ensureApplicationSurface = libndsEnsureApplicationSurface;
    libndsVtable.getSurfaceWidth = libndsGetSurfaceWidth;
    libndsVtable.getSurfaceHeight = libndsGetSurfaceHeight;
    libndsVtable.drawSurface = libndsDrawSurface;
    libndsVtable.drawSurfaceColor = libndsDrawSurfaceColor;
    libndsVtable.drawSurfaceTiled = libndsDrawSurfaceTiled;
    libndsVtable.surfaceResize = libndsSurfaceResize;
    libndsVtable.surfaceFree = libndsSurfaceFree;
    libndsVtable.surfaceCopy = libndsSurfaceCopy;
    libndsVtable.surfaceGetPixels = libndsSurfaceGetPixels;
    libndsVtable.drawTiledPart = libndsDrawTiledPart;
    libndsVtable.gpuSetShader = libndsGpuSetShader;
    libndsVtable.gpuResetShader = libndsGpuResetShader;
    libndsVtable.shaderGetUniform = libndsShaderGetUniform;
    libndsVtable.shaderGetSamplerIndex = libndsShaderGetSamplerIndex;
    libndsVtable.shaderSetUniformF = libndsShaderSetUniformF;
    libndsVtable.shaderSetUniformFArray = libndsShaderSetUniformFArray;
    libndsVtable.shaderSetUniformI = libndsShaderSetUniformI;
    libndsVtable.spriteGetTexture = libndsSpriteGetTexture;
    libndsVtable.surfaceGetTexture = libndsSurfaceGetTexture;
    libndsVtable.textureGetTexelWidth = libndsTextureGetTexelWidth;
    libndsVtable.textureGetTexelHeight = libndsTextureGetTexelHeight;
    libndsVtable.textureGetUVs = libndsTextureGetUVs;
    libndsVtable.textureSetStage = libndsTextureSetStage;
    libndsVtable.shaderIsCompiled = libndsShaderIsCompiled;
    libndsVtable.shadersSupported = libndsShadersSupported;
    libndsVtable.setMatrix = libndsSetMatrix;
    libnds->base.drawColor = 0xFFFFFF;
    libnds->base.drawAlpha = 1.0f;
    libnds->base.drawFont = -1;
    libnds->base.drawHalign = 0;
    libnds->base.drawValign = 0;
    libnds->base.circlePrecision = 24;
    libnds->base.currentShader = -1;
    Matrix4f_identity(&libnds->base.gmlMatrices[MATRIX_WORLD]);
    libnds->blendEnable = true;
    libnds->blendMode = bm_normal;
    libnds->blendFactors.src = bm_src_alpha;
    libnds->blendFactors.dst = bm_inv_src_alpha;
    libnds->blendFactors.srcAlpha = bm_src_alpha;
    libnds->blendFactors.dstAlpha = bm_inv_src_alpha;
    libnds->alphaTestEnable = false;
    libnds->alphaTestRef = 0;
    libnds->colorWriteR = libnds->colorWriteG = libnds->colorWriteB = libnds->colorWriteA = true;
    libnds->fogEnable = false;
    libnds->fogColor = 0;
    libnds->base.currentShader = -1;
    libnds->scaleX = 1.0f;
    libnds->scaleY = 1.0f;
    return (Renderer *)libnds;
}