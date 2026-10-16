#ifndef _BS_JETBRAINS_MONO_SEMIBOLD_FONT_H_
#define _BS_JETBRAINS_MONO_SEMIBOLD_FONT_H_

#include <stdint.h>

#define JBMSB_ATLAS_W 512
#define JBMSB_ATLAS_H 512
#define JBMSB_FIRST_CP 32
#define JBMSB_LAST_CP 126
#define JBMSB_GLYPH_COUNT 95
#define JBMSB_FONT_SCALE 1.1f

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
    int16_t xoffset;
    int16_t yoffset;
    int16_t xadvance;
} JetBrainsMonoBoldGlyph;

extern const uint8_t jetbrainsMonoBoldPixels[JBMSB_ATLAS_W * JBMSB_ATLAS_H];
extern const JetBrainsMonoBoldGlyph jetbrainsMonoBoldGlyphs[JBMSB_GLYPH_COUNT];

#endif
