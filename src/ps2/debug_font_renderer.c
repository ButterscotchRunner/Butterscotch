#include "debug_font_renderer.h"

#include "debug_font.h"
#include "debug_font_4bpp.h"

#include "utils.h"

#include <gsKit.h>
#include <gsFontM.h>
#include <malloc.h>
#include "stdio_compat.h"
#include <stdlib.h>
#include <string.h>

#define DEBUGFONT_LINE_HEIGHT_SCALE 0.80f

// Allocate VRAM and upload one of the debug font tables via a DMA-aligned bounce buffer.
// Returns the assigned VRAM address. Aborts on allocation failure.
static uint32_t uploadTable(GSGLOBAL* gsGlobal, const void* srcData, size_t srcBytes, int width, int height, uint32_t psm, uint32_t tbw, uint8_t clutFlag, const char* what) {
    uint32_t vramSize = gsKit_texture_size(width, height, psm);
    uint32_t vramAddr = gsKit_vram_alloc(gsGlobal, vramSize, GSKIT_ALLOC_USERBUFFER);
    if (vramAddr == GSKIT_ALLOC_ERROR) {
        fprintf(stderr, "DebugFontRenderer: Failed to allocate VRAM for %s\n", what);
        abort();
    }

    // The const source lives in .rodata; gsKit_texture_send wants a writable,
    // 128-byte-aligned EE pointer for DMA, so bounce through a temp buffer.
    uint8_t* buf = (uint8_t*) memalign(128, srcBytes);
    memcpy(buf, srcData, srcBytes);
    gsKit_texture_send((u32*) buf, width, height, vramAddr, psm, tbw, clutFlag);
    free(buf);

    return vramAddr;
}

DebugFontRenderer* DebugFontRenderer_create(GSGLOBAL* gsGlobal) {
    DebugFontRenderer* r = (DebugFontRenderer*) calloc(1, sizeof(DebugFontRenderer));
    r->gsGlobal = gsGlobal;
    r->align = GSKIT_FALIGN_LEFT;
    r->spacing = 1.0f;

    // CLUT first (16 entries laid out 8x2 CT32, gsKit convention for 4bpp palettes).
    uint32_t clutVram = uploadTable(gsGlobal, debugFontClutPs2, sizeof(debugFontClutPs2), 8, 2, GS_PSM_CT32, 1, GS_CLUT_PALLETE, "DebugFont CLUT");

    // Atlas: 256x256 PSMT4 (32 KB).
    uint32_t atlasVram = uploadTable(gsGlobal, debugFontPixels4bpp, DEBUGFONT_PIXELS_4BPP_BYTES, DEBUGFONT_ATLAS_W, DEBUGFONT_ATLAS_H, GS_PSM_T4, DEBUGFONT_ATLAS_W / 64, GS_CLUT_TEXTURE, "DebugFont atlas");

    GSTEXTURE* tex = &r->tex;
    memset(tex, 0, sizeof(GSTEXTURE));
    tex->Width = DEBUGFONT_ATLAS_W;
    tex->Height = DEBUGFONT_ATLAS_H;
    tex->PSM = GS_PSM_T4;
    tex->ClutPSM = GS_PSM_CT32;
    tex->TBW = DEBUGFONT_ATLAS_W / 64;
    tex->Vram = atlasVram;
    tex->VramClut = clutVram;
    tex->Filter = GS_FILTER_LINEAR;
    tex->ClutStorageMode = GS_CLUT_STORAGE_CSM1;

    fprintf(stderr, "DebugFontRenderer: uploaded - CLUT 0x%08lX, atlas 0x%08lX\n", (unsigned long) clutVram, (unsigned long) atlasVram);
    return r;
}

void DebugFontRenderer_destroy(DebugFontRenderer* r) {
    free(r);
}

static const DebugFontGlyphEntry* lookupGlyph(uint8_t c) {
    if (DEBUGFONT_FIRST_CP > c || c > DEBUGFONT_LAST_CP) return nullptr;
    return &debugFontGlyphs[c - DEBUGFONT_FIRST_CP];
}

static float measureLineWidth(const char* s, int32_t len, float spacing) {
    float w = 0.0f;
    repeat(len, i) {
        const DebugFontGlyphEntry* g = lookupGlyph((uint8_t) s[i]);
        if (g != nullptr) w += (float) g->xadvance * spacing;
    }
    return w;
}

// Single render pass for an entire (possibly multi-line) string at a given offset and color.
// Caller is responsible for any alpha-blend state setup.
static void drawPass(DebugFontRenderer* r, float x, float y, int z, float scale, uint64_t color, const char* text, int32_t len) {
    float cursorY = y;
    int32_t lineStart = 0;

    for (int32_t i = 0; len >= i; i++) {
        if (i == len || text[i] == '\n') {
            int32_t lineLen = i - lineStart;

            float startX = x;
            if (r->align != GSKIT_FALIGN_LEFT) {
                float lineW = measureLineWidth(text + lineStart, lineLen, r->spacing) * scale;
                if (r->align == GSKIT_FALIGN_CENTER) startX = x - lineW * 0.5f;
                else if (r->align == GSKIT_FALIGN_RIGHT) startX = x - lineW;
            }

            float pen = startX;
            repeat((uint32_t) lineLen, j) {
                const DebugFontGlyphEntry* g = lookupGlyph((uint8_t) text[lineStart + (int32_t) j]);
                if (g == nullptr) continue;

                if (g->w > 0 && g->h > 0) {
                    float qx0 = pen + (float) g->xoffset * scale;
                    float qy0 = cursorY + (float) g->yoffset * scale;
                    float qx1 = qx0 + (float) g->w * scale;
                    float qy1 = qy0 + (float) g->h * scale;

                    float u0 = (float) g->x;
                    float v0 = (float) g->y;
                    float u1 = u0 + (float) g->w;
                    float v1 = v0 + (float) g->h;

                    gsKit_prim_sprite_texture(r->gsGlobal, &r->tex, qx0, qy0, u0, v0, qx1, qy1, u1, v1, z, color);
                }

                pen += (float) g->xadvance * r->spacing * scale;
            }

            cursorY += (float) ((DEBUGFONT_LINE_HEIGHT) * scale) * DEBUGFONT_LINE_HEIGHT_SCALE;
            lineStart = i + 1;
        }
    }
}

void DebugFontRenderer_printScaled(DebugFontRenderer* r, float x, float y, int z, float scale, uint64_t color, const char* text) {
    if (text == nullptr)
        return;

    int32_t len = (int32_t) strlen(text);

    GSGLOBAL* gs = r->gsGlobal;
    // Force alpha blending because our font has transparent background.
    uint8_t savedAlphaEnable = gs->PrimAlphaEnable;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    // Outline pre-pass: 8 neighboring blits whose AA edges overlap into a soft halo.
    // Alpha=0 in the high byte of outlineColor disables; this is the default.
    uint8_t outlineAlpha = (uint8_t) ((r->outlineColor >> 24) & 0xFF);
    if (outlineAlpha != 0) {
        float ro = r->outlineRadius;
        static const float OFFSETS[8][2] = {
            {-1, -1}, { 0, -1}, { 1, -1},
            {-1,  0},           { 1,  0},
            {-1,  1}, { 0,  1}, { 1,  1},
        };

        repeat(8, i) {
            drawPass(r, x + OFFSETS[i][0] * ro, y + OFFSETS[i][1] * ro, z, scale, r->outlineColor, text, len);
        }
    }

    // Foreground pass.
    drawPass(r, x, y, z, scale, color, text, len);

    gs->PrimAlphaEnable = savedAlphaEnable;
}
