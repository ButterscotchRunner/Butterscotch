#ifndef _BS_JETBRAINS_MONO_EXTRABOLD_FONT_H_
#define _BS_JETBRAINS_MONO_EXTRABOLD_FONT_H_

#include <stdint.h>

#define DEFAULTFONT_ATLAS_W 512
#define DEFAULTFONT_ATLAS_H 512
#define DEFAULTFONT_FIRST_CP 32
#define DEFAULTFONT_LAST_CP 126
#define DEFAULTFONT_GLYPH_COUNT 95
#define DEFAULTFONT_FONT_SCALE 1.1f

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
    int16_t xoffset;
    int16_t yoffset;
    int16_t xadvance;
} JetBrainsMonoExtraBoldGlyph;

extern const uint8_t JetBrainsMonoExtraBoldPixels[DEFAULTFONT_ATLAS_W * DEFAULTFONT_ATLAS_H];
extern const JetBrainsMonoExtraBoldGlyph JetBrainsMonoExtraBoldGlyphs[DEFAULTFONT_GLYPH_COUNT];

#endif
