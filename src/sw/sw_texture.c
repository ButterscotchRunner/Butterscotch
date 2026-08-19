#include <stdlib.h>
#include "sw_renderer_private.h"

SWTexture* swrCreateTextureEx(const void* srcBuffer, int width, int height, bool convert)
{
    SWTexture* txt = (SWTexture*) safeMalloc(sizeof(SWTexture));
    txt->buffer = (uintpixel_t*) safeMalloc(width * height * sizeof(uintpixel_t));
    
    size_t sz = width * height;
    
    if (srcBuffer)
    {
        if (convert) {
            const uint32_t* rgbaSrc = (const uint32_t*) srcBuffer;
            for (size_t i = 0; i < sz; i++)
                txt->buffer[i] = swrConvertPixelTexture(rgbaSrc[i]);
        }
        else {
            const uintpixel_t* rgbaSrc = (const uintpixel_t*) srcBuffer;
            for (size_t i = 0; i < sz; i++)
                txt->buffer[i] = rgbaSrc[i];
        }
    }
    else
    {
        memset(txt->buffer, 0, width * height * sizeof(uintpixel_t));
    }
    
    txt->width = (uint16_t) width;
    txt->height = (uint16_t) height;
    
    return txt;
}

SWTexture* swrCreateTexture(const uint8_t* srcBuffer, int width, int height)
{
    return swrCreateTextureEx(srcBuffer, width, height, true);
}

SWTexture* swrCopyTexture(SWTexture* texture)
{
    return swrCreateTextureEx(texture->buffer, texture->width, texture->height, false);
}

void swrFreeTexture(SWTexture* texture)
{
    if (UNLIKELY(!texture))
        return;
    
    free(texture->buffer);
    free(texture);
}
