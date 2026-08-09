// This is auto generated!
// Font: "JetBrains Mono ExtraBold"
#ifndef _BS_DEBUG_FONT_H_
#define _BS_DEBUG_FONT_H_

#include <stdint.h>

#define DEBUGFONT_ATLAS_W 256
#define DEBUGFONT_ATLAS_H 256
#define DEBUGFONT_LINE_HEIGHT 43
#define DEBUGFONT_BASELINE 33
#define DEBUGFONT_FIRST_CP 32
#define DEBUGFONT_LAST_CP 126
#define DEBUGFONT_GLYPH_COUNT 95

typedef struct {
    uint16_t x, y; // atlas position
    uint16_t w, h; // glyph size in atlas
    int16_t  xoffset; // pen-relative draw offset
    int16_t  yoffset; // from top of line
    int16_t  xadvance; // pen advance after drawing
} DebugFontGlyphEntry;

extern const uint8_t debugFontPixels[DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H];
extern const DebugFontGlyphEntry debugFontGlyphs[DEBUGFONT_GLYPH_COUNT];

#endif /* _BS_DEBUG_FONT_H_ */
