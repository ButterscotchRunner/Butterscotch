#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include "text_utils.h"
#include "sw_renderer_private.h"

#define SWR_DEBUG_FONT_LINE_HEIGHT  16
#define SWR_DEBUG_FONT_CHAR_WIDTH   8

// ==== Internal structures ====

typedef struct
{
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (NULL for sprite fonts)
    int fontTpagIndex;
    int fontPageId;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (NULL for regular fonts)
}
SwrFontState;

// ==== Internal functions ====

FORCE_INLINE void swrPlotPixel_(Renderer* renderer, int x, int y, uintpixel_t color, int blendmode, int srcalpha, int dstalpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (x < swr->portX || y < swr->portY) return;
    if (x >= swr->maxX || y >= swr->maxY) return;
    
    alphaBlend(&swr->fb[y * swr->fbPitch + x], color, blendmode, srcalpha, dstalpha);
}

static bool swrResolveFontState(SWRenderer* swr, DataWin* dw, Font* font, SwrFontState* state)
{
    state->font = font;
    state->fontTpag = NULL;
    state->fontTpagIndex = 0;
    state->spriteFontSprite = NULL;
    
    if (font->isSpriteFont)
    {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    else
    {
        state->fontTpagIndex = font->tpagIndex;
        if (state->fontTpagIndex < 0) return false;
        
        state->fontTpag = &dw->tpag.items[state->fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= swr->totalTextureCount) return false;
        if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return false;
        
        state->fontPageId = pageId;
    }
    
    return true;
}

static bool swrResolveGlyph(
    SWRenderer* swr, DataWin* dw, SwrFontState* state, FontGlyph* glyph, float cursorX, float cursorY,
    int* tpagIndex, int* pageId, int* sx, int* sy, int* sw, int* sh, float* dx, float* dy
)
{
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != NULL)
    {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIdx = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= swr->totalTextureCount) return false;
        if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pid)) return false;

        *tpagIndex = tpagIdx;
        *pageId = glyphTpag->texturePageId;
        
        *sx = glyphTpag->sourceX;
        *sy = glyphTpag->sourceY;
        *sw = glyphTpag->sourceWidth;
        *sh = glyphTpag->sourceHeight;
        
        *dx = cursorX + glyph->offset;
        *dy = cursorY + glyphTpag->targetY - sprite->originY;
    }
    else
    {
        *tpagIndex = state->fontTpagIndex;
        *pageId = state->fontPageId;
        
        *sx = state->fontTpag->sourceX + glyph->sourceX;
        *sy = state->fontTpag->sourceY + glyph->sourceY;
        *sw = glyph->sourceWidth;
        *sh = glyph->sourceHeight;
        
        *dx = cursorX + glyph->offset;
        *dy = cursorY;
    }
    
    return true;
}

#ifdef SW_ENABLE_DEBUG_FONT

extern const uint8_t swrDebugFont1bpp[];

static void swrDrawDebugFontChar(SWRenderer* swr, char chr, int ax, int ay, uint32_t color, float alphaf)
{
    if (chr == ' ')
        return;
    if (chr < '!' || chr > '~')
        chr = '?';
    
    int alpha = swrIntAlpha(alphaf);
    int srcalpha = swrCalcSrcAlpha(swr, alpha);
    int dstalpha = swrCalcDstAlpha(swr, alpha);
    int blendmode = swr->blendMode;
    
    uintpixel_t actualColor = swrConvertPixel(color);
    for (int y = 0; y < SWR_DEBUG_FONT_LINE_HEIGHT; y++)
    {
        for (int x = 0; x < SWR_DEBUG_FONT_CHAR_WIDTH; x++)
        {
            uint8_t row = swrDebugFont1bpp[chr * SWR_DEBUG_FONT_LINE_HEIGHT + y];
            
            if (row & (1 << (SWR_DEBUG_FONT_CHAR_WIDTH - 1 - x)))
                swrPlotPixel_(&swr->base, ax + x, ay + y, actualColor, blendmode, srcalpha, dstalpha);
        }
    }
}

#endif

// ==== Exposed interface ====

void swrDrawText(SWRenderer* swr, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t color, float alpha, float lineSeparation)
{
    Renderer* renderer = &swr->base;
    DataWin* dwin = renderer->dataWin;
    
    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dwin->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dwin->font.fonts[fontIndex];
    
    SwrFontState fontState;
    memset(&fontState, 0, sizeof fontState); // silence warning treated as error
    
    if (!swrResolveFontState(swr, dwin, font, &fontState)) return;
    
    // TODO: do we need to mirror the way the text scrolls too?!
    float cosA = 1.0f, sinA = 0.0f, angleRad = 0.0f;
    bool mustRotate = swrMustRotateSensitive(angleDeg);
    if (UNLIKELY(mustRotate))
    {
        angleRad = -angleDeg * M_PI / 180.0f;
        cosA = cosf(angleRad);
        sinA = sinf(angleRad);
    }
    
    int textLen = (int) strlen(text);
    int lineCount = TextUtils_countLines(text, textLen);
    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;
    
    xscale *= font->scaleX;
    yscale *= font->scaleY;

    // Iterate through lines. HTML5 subtracts ascenderOffset from the per-line y offset
    // (see yyFont.GR_Text_Draw), shifting glyphs up so the baseline aligns with the drawn y.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    int fontTpagIndex = 0, pageId = 0;
                    int sx, sy, sw, sh, dw, dh;
                    float dx, dy;
                    if (swrResolveGlyph(swr, dwin, &fontState, glyph, cursorX, cursorY,
                            &fontTpagIndex, &pageId, &sx, &sy, &sw, &sh, &dx, &dy))
                    {
                        dx *= xscale; dx += x;
                        dy *= xscale; dy += y;
                        dw = swrCeiling(xscale * glyph->sourceWidth);
                        dh = swrCeiling(yscale * glyph->sourceHeight);
                        
                        // TODO: at 640x480, for some reason, without this fixup the
                        // letters in the "Name the fallen human." screen don't shake
                        dx = roundf(dx * 2) / 2;
                        dy = roundf(dy * 2) / 2;
                        
                        SWTexture* texture = swr->textures[pageId];
                        
                        if (UNLIKELY(mustRotate))
                        {
                            dx -= x;
                            dy -= y;
                            float ndx = cosA * dx - sinA * dy;
                            float ndy = sinA * dx + cosA * dy;
                            ndx += x;
                            ndy += y;
                            swrDrawSpriteRotated(renderer, ndx, ndy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, 0.0f, 0.0f);
                        }
                        else
                        {
                            swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
                        }
                        
                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    cursorX += TextUtils_getKerningOffset(glyph, nextCh);
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

#ifdef SW_ENABLE_DEBUG_FONT

void swrDrawDebugText(SWRenderer* swr, const char* text, int x, int y, uint32_t color, float alpha)
{
    int px = 0;
    int py = 0;
    
    while (*text)
    {
        char chr = *text;
        text++;
        
        if (chr == '\n') {
            px = 0;
            py += SWR_DEBUG_FONT_LINE_HEIGHT;
            continue;
        }
        
        swrDrawDebugFontChar(swr, chr, x + px, y + py, color, alpha);
        px += SWR_DEBUG_FONT_CHAR_WIDTH;
    }
}

#else

void swrDrawDebugText(SWRenderer* swr, const char* text, int x, int y, uint32_t color, float alpha)
{
    (void) swr;
    (void) text;
    (void) x;
    (void) y;
    (void) color;
    (void) alpha;
}

#endif
