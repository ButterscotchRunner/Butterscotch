#ifndef _BS_DEFAULT_FONT_RENDERER_H_
#define _BS_DEFAULT_FONT_RENDERER_H_

#include <common.h>
#include <gsKit.h>
#include <stdint.h>

typedef struct {
    GSGLOBAL* gsGlobal;
    GSTEXTURE tex;
    uint8_t align;
    float spacing;
    uint64_t outlineColor;
    float outlineRadius;
} DefaultFontRenderer;

DefaultFontRenderer* DefaultFontRenderer_create(GSGLOBAL* gsGlobal);
void DefaultFontRenderer_destroy(DefaultFontRenderer* renderer);
void DefaultFontRenderer_printScaled(DefaultFontRenderer* renderer, float x, float y, int z, float scale, uint64_t color, const char* text);

#endif
