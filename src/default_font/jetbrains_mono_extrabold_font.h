#ifndef _BS_JETBRAINS_MONO_EXTRABOLD_FONT_H_
#define _BS_JETBRAINS_MONO_EXTRABOLD_FONT_H_

#include <stdint.h>

#define JBMEB_ATLAS_W 512
#define JBMEB_ATLAS_H 512
#define JBMEB_FIRST_CP 32
#define JBMEB_LAST_CP 126
#define JBMEB_GLYPH_COUNT 95
#define JBMEB_FONT_SCALE 1.1f

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
    int16_t xoffset;
    int16_t yoffset;
    int16_t xadvance;
} JetBrainsMonoExtraBoldGlyph;

extern const uint8_t JetBrainsMonoExtraBoldPixels[JBMEB_ATLAS_W * JBMEB_ATLAS_H];
extern const JetBrainsMonoExtraBoldGlyph JetBrainsMonoExtraBoldGlyphs[JBMEB_GLYPH_COUNT];

#endif
