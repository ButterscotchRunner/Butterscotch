#include "default_font/default_font.h"

#include <string.h>

#include "common.h"

static const char DEFAULT_FONT_NAME[] = "JetBrainsMonoExtraBold";

void DefaultFont_initialize(Font* font, FontGlyph* glyphs, DefaultFontUsage usage) {
    memset(font, 0, sizeof(Font));
    font->present = true;
    font->name = DEFAULT_FONT_NAME;
    font->displayName = DEFAULT_FONT_NAME;
    font->emSize = usage == DEFAULT_FONT_UI ? 12.0f : 12.0f * JBMEB_FONT_SCALE;
    font->rangeStart = JBMEB_FIRST_CP;
    font->rangeEnd = JBMEB_LAST_CP;
    font->antiAliasing = 1;
    font->tpagIndex = -1;
    font->scaleX = usage == DEFAULT_FONT_UI ? DEFAULT_FONT_UI_SCALE : JBMEB_FONT_SCALE / 4.0f;
    font->scaleY = font->scaleX;
    font->spriteIndex = -1;
    font->glyphs = glyphs;
    font->glyphCount = JBMEB_GLYPH_COUNT;
    float advanceScale = usage == DEFAULT_FONT_UI ? 1.0f : 0.88f;

    repeat(font->glyphCount, i) {
        const JetBrainsMonoExtraBoldGlyph* glyphInfo = &JetBrainsMonoExtraBoldGlyphs[i];
        FontGlyph* glyph = &font->glyphs[i];

        glyph->character = (uint16_t) (JBMEB_FIRST_CP + i);
        glyph->sourceX = glyphInfo->x;
        glyph->sourceY = glyphInfo->y;
        glyph->sourceWidth = glyphInfo->w;
        glyph->sourceHeight = glyphInfo->h;
        glyph->shift = glyphInfo->xadvance * advanceScale;
        glyph->offset = glyphInfo->xoffset;
        glyph->kerningCount = 0;
        glyph->kerning = nullptr;

        if ((uint16_t) glyph->sourceHeight > font->maxGlyphHeight) {
            font->maxGlyphHeight = glyph->sourceHeight;
        }
    }

    Font_buildGlyphLUT(font);
}

bool DefaultFont_isDefault(const Font* font) {
    return font != nullptr && font->name != nullptr && strcmp(font->name, DEFAULT_FONT_NAME) == 0;
}

const JetBrainsMonoExtraBoldGlyph* DefaultFont_lookupGlyph(uint16_t character) {
    if (character < JBMEB_FIRST_CP || character > JBMEB_LAST_CP) return nullptr;
    return &JetBrainsMonoExtraBoldGlyphs[character - JBMEB_FIRST_CP];
}

float DefaultFont_glyphYOffset(const Font* font, uint16_t character) {
    if (!DefaultFont_isDefault(font)) return 0.0f;
    const JetBrainsMonoExtraBoldGlyph* glyph = DefaultFont_lookupGlyph(character);
    return glyph != nullptr ? (float) glyph->yoffset : 0.0f;
}

void DefaultFont_writeRGBA(uint8_t* rgba) {
    repeat(JBMEB_ATLAS_W * JBMEB_ATLAS_H, i) {
        rgba[i * 4 + 0] = 0xFF;
        rgba[i * 4 + 1] = 0xFF;
        rgba[i * 4 + 2] = 0xFF;
        rgba[i * 4 + 3] = JetBrainsMonoExtraBoldPixels[i];
    }
}

int32_t DefaultFont_ensureLoaded(DataWin* dataWin) {
    if (dataWin == nullptr) return -1;

    repeat(dataWin->font.count, i) {
        Font* font = &dataWin->font.fonts[i];
        if (font->present && DefaultFont_isDefault(font)) {
            return (int32_t) i;
        }
    }

    const uint32_t newIndex = dataWin->font.count;
    if (dataWin->font.fonts == nullptr) {
        dataWin->font.fonts = (Font *)safeMalloc(sizeof(Font));
    } else {
        dataWin->font.fonts = (Font *)safeRealloc(dataWin->font.fonts, (size_t) (dataWin->font.count + 1u) * sizeof(Font));
    }

    Font* font = &dataWin->font.fonts[newIndex];
    FontGlyph* glyphs = (FontGlyph *)safeMalloc((size_t) JBMEB_GLYPH_COUNT * sizeof(FontGlyph));
    DefaultFont_initialize(font, glyphs, DEFAULT_FONT_GAME);
    dataWin->font.count = newIndex + 1u;
    return (int32_t)newIndex;
}
