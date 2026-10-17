#include "default_font/default_font.h"

#include <string.h>

#include "common.h"
#include "default_font/jetbrains_mono_extrabold_font.h"

static void DefaultFont_fillGlyphs(Font* font) {
    font->glyphCount = JBMEB_GLYPH_COUNT;
    font->glyphs = (FontGlyph *)safeMalloc((size_t) font->glyphCount * sizeof(FontGlyph));
    font->maxGlyphHeight = 0;

    repeat(font->glyphCount, i) {
        const JetBrainsMonoExtraBoldGlyph* glyphInfo = &JetBrainsMonoExtraBoldGlyphs[i];
        FontGlyph* glyph = &font->glyphs[i];

        glyph->character = (uint16_t) (JBMEB_FIRST_CP + i);
        glyph->sourceX = glyphInfo->x;
        glyph->sourceY = glyphInfo->y;
        glyph->sourceWidth = glyphInfo->w;
        glyph->sourceHeight = glyphInfo->h;
        glyph->shift = glyphInfo->xadvance * 0.9f;
        glyph->offset = glyphInfo->xoffset;
        glyph->kerningCount = 0;
        glyph->kerning = nullptr;

        if ((uint16_t) glyph->sourceHeight > font->maxGlyphHeight) {
            font->maxGlyphHeight = glyph->sourceHeight;
        }
    }

    Font_buildGlyphLUT(font);
}

int32_t DefaultFont_ensureLoaded(DataWin* dataWin) {
    if (dataWin == nullptr) return -1;

    repeat(dataWin->font.count, i) {
        Font* font = &dataWin->font.fonts[i];
        if (font->present && font->name != nullptr && strcmp(font->name, "JetBrainsMonoExtraBold") == 0) {
            return (int32_t) i;
        }
    }

    const uint32_t newIndex = dataWin->font.count;
    if (dataWin->font.fonts == nullptr) {
        dataWin->font.fonts = (Font *)safeMalloc(sizeof(Font));
    } else {
        dataWin->font.fonts = (Font *)safeRealloc(dataWin->font.fonts, (size_t) (dataWin->font.count + 1u) * sizeof(Font));
    }

    memset(&dataWin->font.fonts[newIndex], 0, sizeof(Font));
    Font* font = &dataWin->font.fonts[newIndex];

    font->present = true;
    font->name = "JetBrainsMonoExtraBold";
    font->displayName = "JetBrainsMonoExtraBold";
    font->emSize = 12.0f * JBMEB_FONT_SCALE;
    font->bold = false;
    font->italic = false;
    font->rangeStart = JBMEB_FIRST_CP;
    font->charset = 0;
    font->antiAliasing = 1;
    font->rangeEnd = JBMEB_LAST_CP;
    font->tpagIndex = -1;
    font->scaleX = JBMEB_FONT_SCALE * 0.5f;
    font->scaleY = JBMEB_FONT_SCALE * 0.5f;
    font->ascenderOffset = 0;
    font->ascender = 0;
    font->sdfSpread = 0;
    font->lineHeight = 0;
    font->hasAscender = false;
    font->hasSDFSpread = false;
    font->hasLineHeight = false;
    font->isSpriteFont = false;
    font->spriteIndex = -1;
    font->spriteOriginYAdjust = 0;

    DefaultFont_fillGlyphs(font);
    dataWin->font.count = newIndex + 1u;
    return (int32_t)newIndex;
}
