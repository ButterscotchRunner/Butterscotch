#include "default_font_renderer.h"

#include "default_font/jetbrains_mono_extrabold_font.h"
#include "utils.h"

#include <malloc.h>
#include <stdlib.h>
#include "string_compat.h"

#define DEFAULT_FONT_LINE_HEIGHT_SCALE 0.80f
#define DEFAULT_FONT_SCALE (JBMEB_FONT_SCALE / 2.0f)
#define DEFAULT_FONT_CLUT_ENTRIES 16
#define DEFAULT_FONT_PIXELS_4BPP_BYTES (JBMEB_ATLAS_W * JBMEB_ATLAS_H / 2)

// Allocate VRAM and upload one of the debug font tables via a DMA-aligned bounce buffer.
// Returns the assigned VRAM address. Aborts on allocation failure.
static uint32_t uploadTable(GSGLOBAL* gsGlobal, const void* srcData, size_t srcBytes, int width, int height, uint32_t psm, uint32_t tbw, uint8_t clutFlag, const char* what) {
    uint32_t vramSize = gsKit_texture_size(width, height, psm);
    uint32_t vramAddr = gsKit_vram_alloc(gsGlobal, vramSize, GSKIT_ALLOC_USERBUFFER);
    if (vramAddr == GSKIT_ALLOC_ERROR) {
        logError("DefaultFontRenderer: Failed to allocate VRAM for %s\n", what);
        abort();
    }

    // The const source lives in .rodata; gsKit_texture_send wants a writable,
    // 128-byte-aligned EE pointer for DMA, so bounce through a temp buffer.
    uint8_t* buf = (uint8_t*) memalign(128, srcBytes);
    if (buf == nullptr) {
        logError("DefaultFontRenderer: Failed to allocate upload buffer for %s\n", what);
        abort();
    }
    memcpy(buf, srcData, srcBytes);
    gsKit_texture_send((u32*) buf, width, height, vramAddr, psm, tbw, clutFlag);
    free(buf);

    return vramAddr;
}

DefaultFontRenderer* DefaultFontRenderer_create(GSGLOBAL* gsGlobal) {
    DefaultFontRenderer* renderer = (DefaultFontRenderer*) calloc(1, sizeof(DefaultFontRenderer));
    if (renderer == nullptr) {
        logError("DefaultFontRenderer: Failed to allocate renderer\n");
        abort();
    }
    renderer->gsGlobal = gsGlobal;
    renderer->align = GSKIT_FALIGN_LEFT;
    renderer->spacing = 1.0f;

    uint32_t clut[DEFAULT_FONT_CLUT_ENTRIES];
    repeat(DEFAULT_FONT_CLUT_ENTRIES, i) {
        uint8_t alpha = (uint8_t) ((i * 255) / (DEFAULT_FONT_CLUT_ENTRIES - 1));
        clut[i] = ((uint32_t) alpha << 24) | 0x00FFFFFFu;
    }
    // CLUT first (16 entries laid out 8x2 CT32, gsKit convention for 4bpp palettes).
    uint32_t clutVram = uploadTable(gsGlobal, clut, sizeof(clut), 8, 2, GS_PSM_CT32, 1, GS_CLUT_PALLETE, "font CLUT");

    uint8_t* packedPixels = (uint8_t*) malloc(DEFAULT_FONT_PIXELS_4BPP_BYTES);
    if (packedPixels == nullptr) {
        logError("DefaultFontRenderer: Failed to allocate packed font atlas\n");
        abort();
    }
    repeat(DEFAULT_FONT_PIXELS_4BPP_BYTES, i) {
        uint8_t first = (uint8_t) (JetBrainsMonoExtraBoldPixels[i * 2] >> 4);
        uint8_t second = (uint8_t) (JetBrainsMonoExtraBoldPixels[i * 2 + 1] >> 4);
        packedPixels[i] = (uint8_t) (first | (uint8_t) (second << 4));
    }
    // Atlas: 256x256 PSMT4 (32 KB).
    uint32_t atlasVram = uploadTable(gsGlobal, packedPixels, DEFAULT_FONT_PIXELS_4BPP_BYTES, JBMEB_ATLAS_W, JBMEB_ATLAS_H, GS_PSM_T4, JBMEB_ATLAS_W / 64, GS_CLUT_TEXTURE, "font atlas");
    free(packedPixels);

    GSTEXTURE* texture = &renderer->tex;
    memset(texture, 0, sizeof(GSTEXTURE));
    texture->Width = JBMEB_ATLAS_W;
    texture->Height = JBMEB_ATLAS_H;
    texture->PSM = GS_PSM_T4;
    texture->ClutPSM = GS_PSM_CT32;
    texture->TBW = JBMEB_ATLAS_W / 64;
    texture->Vram = atlasVram;
    texture->VramClut = clutVram;
    texture->Filter = GS_FILTER_LINEAR;
    texture->ClutStorageMode = GS_CLUT_STORAGE_CSM1;

    logInfo("DefaultFontRenderer: uploaded - CLUT 0x%08lX, atlas 0x%08lX\n", (unsigned long) clutVram, (unsigned long) atlasVram);
    return renderer;
}

void DefaultFontRenderer_destroy(DefaultFontRenderer* renderer) {
    free(renderer);
}

static const JetBrainsMonoExtraBoldGlyph* lookupGlyph(uint8_t character) {
    if (JBMEB_FIRST_CP > character || character > JBMEB_LAST_CP) return nullptr;
    return &JetBrainsMonoExtraBoldGlyphs[character - JBMEB_FIRST_CP];
}

static float measureLineWidth(const char* text, int32_t length, float spacing) {
    float width = 0.0f;
    repeat(length, i) {
        const JetBrainsMonoExtraBoldGlyph* glyph = lookupGlyph((uint8_t) text[i]);
        if (glyph != nullptr) width += (float) glyph->xadvance * spacing;
    }
    return width;
}

// Single render pass for an entire (possibly multi-line) string at a given offset and color.
// Caller is responsible for any alpha-blend state setup.
static void drawPass(DefaultFontRenderer* renderer, float x, float y, int z, float scale, uint64_t color, const char* text, int32_t length) {
    float cursorY = y;
    int32_t lineStart = 0;
    float fontScale = scale * DEFAULT_FONT_SCALE;

    for (int32_t i = 0; length >= i; i++) {
        if (i == length || text[i] == '\n') {
            int32_t lineLength = i - lineStart;
            float startX = x;
            if (renderer->align != GSKIT_FALIGN_LEFT) {
                float lineWidth = measureLineWidth(text + lineStart, lineLength, renderer->spacing) * fontScale;
                if (renderer->align == GSKIT_FALIGN_CENTER) startX = x - lineWidth * 0.5f;
                else if (renderer->align == GSKIT_FALIGN_RIGHT) startX = x - lineWidth;
            }

            float pen = startX;
            repeat((uint32_t) lineLength, j) {
                const JetBrainsMonoExtraBoldGlyph* glyph = lookupGlyph((uint8_t) text[lineStart + (int32_t) j]);
                if (glyph == nullptr) continue;

                if (glyph->w > 0 && glyph->h > 0) {
                    float qx0 = pen + (float) glyph->xoffset * fontScale;
                    float qy0 = cursorY + (float) glyph->yoffset * fontScale;
                    float qx1 = qx0 + (float) glyph->w * fontScale;
                    float qy1 = qy0 + (float) glyph->h * fontScale;
                    float u0 = (float) glyph->x;
                    float v0 = (float) glyph->y;
                    float u1 = u0 + (float) glyph->w;
                    float v1 = v0 + (float) glyph->h;
                    gsKit_prim_sprite_texture(renderer->gsGlobal, &renderer->tex, qx0, qy0, u0, v0, qx1, qy1, u1, v1, z, color);
                }
                pen += (float) glyph->xadvance * renderer->spacing * fontScale;
            }

            cursorY += JBMEB_FONT_SCALE * 24.0f * scale * DEFAULT_FONT_LINE_HEIGHT_SCALE;
            lineStart = i + 1;
        }
    }
}

void DefaultFontRenderer_printScaled(DefaultFontRenderer* renderer, float x, float y, int z, float scale, uint64_t color, const char* text) {
    if (text == nullptr)
        return;

    int32_t length = (int32_t) strlen(text);
    GSGLOBAL* gs = renderer->gsGlobal;
    // Force alpha blending because our font has transparent background.
    uint8_t savedAlphaEnable = gs->PrimAlphaEnable;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    // Outline pre-pass: 8 neighboring blits whose AA edges overlap into a soft halo.
    // Alpha=0 in the high byte of outlineColor disables; this is the default.
    uint8_t outlineAlpha = (uint8_t) ((renderer->outlineColor >> 24) & 0xFF);
    if (outlineAlpha != 0) {
        float radius = renderer->outlineRadius;
        static const float OFFSETS[8][2] = {
            {-1, -1}, { 0, -1}, { 1, -1},
            {-1,  0},           { 1,  0},
            {-1,  1}, { 0,  1}, { 1,  1},
        };
        
        repeat(8, i) {
            drawPass(renderer, x + OFFSETS[i][0] * radius, y + OFFSETS[i][1] * radius, z, scale, renderer->outlineColor, text, length);
        }
    }

    // Foreground pass.
    drawPass(renderer, x, y, z, scale, color, text, length);

    gs->PrimAlphaEnable = savedAlphaEnable;
}
