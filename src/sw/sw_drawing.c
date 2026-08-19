#include <stdlib.h>
#include "sw_renderer_private.h"

bool swrSwitchToSurface(Renderer* renderer, int32_t targetSurfaceId, bool restoreOldView)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (swr->drawingToSurface) {
        swrCommitShadowWritesToSurfaceIfNeeded(swr, swr->surfaces[swr->currentSurfaceIndex]);
    }
    
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
        swr->blendMode = bm_normal;
        swr->currentSurfaceIndex = -1;
        swr->writeMask = WRITE_MASK_ALL;
        
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
        swr->blendMode = bm_normal;
        
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
    
    SWTexture* surface = swr->surfaces[targetSurfaceId]->texture;
    swr->fb = surface->buffer;
    swr->width = surface->width;
    swr->height = surface->height;
    swr->fbPitch = surface->width;
    swr->drawingToSurface = true;
    swr->blendMode = bm_normal;
    swr->currentSurfaceIndex = targetSurfaceId;
    swr->writeMask = WRITE_MASK_ALL;
    
    swr->viewX = swr->portX = 0;
    swr->viewY = swr->portY = 0;
    swr->maxX = swr->viewW = swr->portW = surface->width;
    swr->maxY = swr->viewH = swr->portH = surface->height;
    swr->scaleX = swr->scaleY = 1.0f;
    
    fprintf(stderr, "switching to surface %p, fb %p, %dx%d\n", surface, swr->fb, swr->width, swr->height);
    
    return true;
}
