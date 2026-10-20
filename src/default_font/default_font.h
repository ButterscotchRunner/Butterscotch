#ifndef _BS_DEFAULT_FONT_H_
#define _BS_DEFAULT_FONT_H_

#include "data_win.h"
#include "default_font/jetbrains_mono_extrabold_font.h"

#define DEFAULT_FONT_UI_SCALE (JBMEB_FONT_SCALE / 2.0f)
#define DEFAULT_FONT_UI_LINE_HEIGHT (JBMEB_FONT_SCALE * 24.0f)
#define DEFAULT_FONT_OVERLAY_LINE_HEIGHT (DEFAULT_FONT_UI_LINE_HEIGHT * 0.80f)
#define DEFAULT_FONT_RGBA_BYTES (JBMEB_ATLAS_W * JBMEB_ATLAS_H * 4)

typedef enum {
    DEFAULT_FONT_GAME,
    DEFAULT_FONT_UI
} DefaultFontUsage;

// Initializes a fresh font using caller-owned storage for JBMEB_GLYPH_COUNT glyphs.
void DefaultFont_initialize(Font* font, FontGlyph* glyphs, DefaultFontUsage usage);
int32_t DefaultFont_ensureLoaded(DataWin* dataWin);
bool DefaultFont_isDefault(const Font* font);
const JetBrainsMonoExtraBoldGlyph* DefaultFont_lookupGlyph(uint16_t character);
float DefaultFont_glyphYOffset(const Font* font, uint16_t character);

// Writes DEFAULT_FONT_RGBA_BYTES bytes to caller-owned storage.
void DefaultFont_writeRGBA(uint8_t* rgba);

#endif
