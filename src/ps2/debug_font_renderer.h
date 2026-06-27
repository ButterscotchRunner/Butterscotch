#ifndef _BS_DEBUG_FONT_RENDERER_H_
#define _BS_DEBUG_FONT_RENDERER_H_

#include <common.h>
#include <gsKit.h>
#include <stdint.h>

typedef struct {
    GSGLOBAL* gsGlobal;
    GSTEXTURE tex; // Configured once; points at the cached VRAM atlas + CLUT
    uint8_t align; // GSKIT_FALIGN_LEFT/CENTER/RIGHT
    float spacing; // xadvance multiplier
    uint64_t outlineColor;
    float outlineRadius;
} DebugFontRenderer;

// Allocates VRAM for the atlas + CLUT, uploads the pre-baked pixel data,
// and returns a ready-to-use renderer. Call once at boot, before any text drawing.
DebugFontRenderer* DebugFontRenderer_create(GSGLOBAL* gsGlobal);
void DebugFontRenderer_destroy(DebugFontRenderer* r);
void DebugFontRenderer_printScaled(DebugFontRenderer* r, float x, float y, int z, float scale, uint64_t color, const char* text);

#endif /* _BS_DEBUG_FONT_RENDERER_H_ */
