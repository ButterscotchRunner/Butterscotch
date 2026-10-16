#ifndef _BS_JETBRAINS_MONO_MEDIUM_FONT_H_
#define _BS_JETBRAINS_MONO_MEDIUM_FONT_H_

#include <stdint.h>

#define JBMMED_ATLAS_W 512
#define JBMMED_ATLAS_H 512
#define JBMMED_FIRST_CP 32
#define JBMMED_LAST_CP 126
#define JBMMED_GLYPH_COUNT 95

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
    int16_t xoffset;
    int16_t yoffset;
    int16_t xadvance;
} JetBrainsMonoMediumGlyph;

extern const uint8_t jetbrainsMonoMediumPixels[JBMMED_ATLAS_W * JBMMED_ATLAS_H];
extern const JetBrainsMonoMediumGlyph jetbrainsMonoMediumGlyphs[JBMMED_GLYPH_COUNT];

#endif
