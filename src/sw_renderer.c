#ifdef ENABLE_SW_RENDERER
#include "sw_renderer.h"

#include "common.h"
#include "renderer.h"
#include "vm_builtins.h"
#include "runner.h"
#include "data_win.h"
#include "utils.h"
#include "log.h"

#include "platformdefs.h"
#include "image_decoder.h"
#include "text_utils.h"
#include "debug_font/debug_font.h"
#include <math.h>

#include <stdlib.h>
#include <string.h>

// ===[ SWRenderer Struct ]===

typedef struct {
    int width, height;
    uint32_t* pixels;
} SWImage;

typedef struct {
    float x, y, u, v;
    float color[4];
} SWVertex;

typedef struct {
    Renderer base; // Must be first field for struct embedding

    // Render targets
    int32_t* surfaceWidths;
    int32_t* surfaceHeights;
    bool* surfaceExistsFlag;
    uint32_t surfaceCount;
    uint32_t surfaceCapacity;

    uint32_t** surfacePixels;
    SWImage frame;
    SWImage* pages;
    SWImage debugAtlas;
    Font debugFont;
    FontGlyph debugGlyphs[DEBUGFONT_GLYPH_COUNT];
    int32_t target, application;
    Matrix4f viewProjection;
    SWVertex* vertices;
    uint32_t vertexCount, vertexCapacity;
    int32_t primitive;
    uint32_t primitiveTexture;

    // GPU state
    bool blendEnable;
    int32_t blendMode;
    BlendFactors blendFactors;
    bool alphaTestEnable;
    uint8_t alphaTestRef;
    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;
    bool fogEnable;
    uint32_t fogColor;
} SWRenderer;

static int32_t swCreateSurface(Renderer* renderer, int32_t width, int32_t height);
static void swSurfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height);
static bool swSetRenderTarget(Renderer* renderer, int32_t surfaceID, bool implicitApplicationSurface);
void Runner_setNextFrame(uint32_t* framebuffer, int width, int height);

static bool swValidSize(int width, int height) { return width > 0 && height > 0 && width <= 8192 && height <= 8192; }

static uint32_t swPackColor(uint32_t bgr, float alpha) {
    uint32_t a = (uint32_t)(fminf(1.0f, fmaxf(0.0f, alpha)) * 255.0f + 0.5f);
    return (a << 24) | ((bgr & 255) << 16) | (bgr & 0xFF00) | ((bgr >> 16) & 255);
}

static SWImage swSurface(SWRenderer* sw, int32_t id) {
    if (id < 0) return sw->frame;
    if ((uint32_t)id >= sw->surfaceCount || !sw->surfaceExistsFlag[id]) return (SWImage){0};
    return (SWImage){sw->surfaceWidths[id], sw->surfaceHeights[id], sw->surfacePixels[id]};
}

static SWImage* swPage(SWRenderer* sw, uint32_t id) {
    DataWin* dw = sw->base.dataWin;
    if (id >= dw->txtr.count) return NULL;
    SWImage* image = &sw->pages[id];
    if (image->pixels) return image;
    DataWin_loadTxtrIfNeeded(dw, id);
    Texture* texture = &dw->txtr.textures[id];
    uint8_t* external = NULL;
    int32_t size = 0;
    if (texture->externalPath && sw->base.runner)
        sw->base.runner->fileSystem->vtable->readFileBinary(sw->base.runner->fileSystem, texture->externalPath,
                                                            &external, &size);
    uint8_t* rgba =
        ImageDecoder_decodeToRgba(external ? external : texture->blobData, external ? (size_t)size : texture->blobSize,
                                  DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0), &image->width, &image->height);
    free(external);
    if (!rgba) return NULL;
    size_t count = (size_t)image->width * image->height;
    image->pixels = (uint32_t*)rgba;
    for (size_t i = 0; i < count; i++) {
        uint8_t* p = rgba + i * 4;
        uint32_t value = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        image->pixels[i] = value;
    }
    return image;
}

static SWImage swTexture(SWRenderer* sw, uint32_t handle) {
    if (!handle) return (SWImage){0};
    if (handle & 0x80000000U) return swSurface(sw, (int32_t)((handle & 0x7FFFFFFF) - 1));
    SWImage* page = swPage(sw, handle - 1);
    return page ? *page : (SWImage){0};
}

static float swFactor(int factor, const float* src, const float* dst, int channel) {
    switch (factor) {
    case bm_zero:
        return 0;
    case bm_one:
        return 1;
    case bm_src_color:
        return src[channel] / 255;
    case bm_inv_src_color:
        return 1 - src[channel] / 255;
    case bm_src_alpha:
        return src[3] / 255;
    case bm_inv_src_alpha:
        return 1 - src[3] / 255;
    case bm_dest_color:
        return dst[channel] / 255;
    case bm_inv_dest_color:
        return 1 - dst[channel] / 255;
    case bm_dest_alpha:
        return dst[3] / 255;
    case bm_inv_dest_alpha:
        return 1 - dst[3] / 255;
    case bm_src_alpha_sat:
        return channel == 3 ? 1 : fminf(src[3], 255 - dst[3]) / 255;
    default:
        return 1;
    }
}

static void swPixel(SWRenderer* sw, SWImage target, int x, int y, float* color) {
    Renderer* r = &sw->base;
    if (!target.pixels || x < 0 || y < 0 || x >= target.width || y >= target.height || x < r->CPortX || y < r->CPortY ||
        x >= r->CPortX + r->CPortW || y >= r->CPortY + r->CPortH)
        return;
    if (sw->alphaTestEnable && color[3] <= sw->alphaTestRef) return;
    uint32_t* pixel = &target.pixels[(size_t)y * target.width + x];
    float dest[4] = {(*pixel >> 16) & 255, (*pixel >> 8) & 255, *pixel & 255, *pixel >> 24};
    bool write[4] = {sw->colorWriteR, sw->colorWriteG, sw->colorWriteB, sw->colorWriteA};
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        float value = color[i];
        if (sw->blendEnable) {
            int sf = i == 3 ? sw->blendFactors.srcAlpha : sw->blendFactors.src;
            int df = i == 3 ? sw->blendFactors.dstAlpha : sw->blendFactors.dst;
            float s = color[i] * swFactor(sf, color, dest, i);
            float d = dest[i] * swFactor(df, color, dest, i);
            value = sw->blendMode == bm_reverse_subtract ? d - s
                    : sw->blendMode == bm_min ? fminf(s, d) : s + d;
        }
        if (!write[i]) value = dest[i];
        uint32_t byte = (uint32_t)(fminf(255, fmaxf(0, value)) + 0.5f);
        result |= byte << (i == 0 ? 16 : i == 1 ? 8 : i == 2 ? 0 : 24);
    }
    *pixel = result;
}

static SWVertex swVertex(SWRenderer* sw, float x, float y, float z, uint32_t color, float alpha, float u, float v) {
    const float* m = sw->base.gmlMatrices[MATRIX_WORLD].m;
    float world[4] = {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
                      m[2] * x + m[6] * y + m[10] * z + m[14], m[3] * x + m[7] * y + m[11] * z + m[15]};
    const float* p = sw->viewProjection.m;
    float cx = p[0] * world[0] + p[4] * world[1] + p[8] * world[2] + p[12] * world[3];
    float cy = p[1] * world[0] + p[5] * world[1] + p[9] * world[2] + p[13] * world[3];
    float cw = p[3] * world[0] + p[7] * world[1] + p[11] * world[2] + p[15] * world[3];
    if (fabsf(cw) < 0.000001f) cw = 1;
    SWVertex vertex = {sw->base.CPortX + (cx / cw + 1) * sw->base.CPortW * 0.5f,
                       sw->base.CPortY + (1 - cy / cw) * sw->base.CPortH * 0.5f,
                       u,
                       v,
                       {(color & 255), ((color >> 8) & 255), ((color >> 16) & 255), fminf(1, fmaxf(0, alpha)) * 255}};
    return vertex;
}

static float swEdge(SWVertex a, SWVertex b, float x, float y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

static bool swTopLeft(SWVertex a, SWVertex b) { return b.y < a.y || (b.y == a.y && b.x > a.x); }

static void swTriangle(SWRenderer* sw, SWImage texture, SWVertex a, SWVertex b, SWVertex c) {
    float area = swEdge(a, b, c.x, c.y);
    if (!isfinite(area) || fabsf(area) < 0.00001f) return;
    if (area < 0) {
        SWVertex temp = b;
        b = c;
        c = temp;
        area = -area;
    }
    SWImage target = swSurface(sw, sw->target);
    if (!target.pixels) return;
    int left = (int)fmaxf(sw->base.CPortX, fmaxf(0, floorf(fminf(a.x, fminf(b.x, c.x)))));
    int right = (int)fminf(target.width, fminf(sw->base.CPortX + sw->base.CPortW, ceilf(fmaxf(a.x, fmaxf(b.x, c.x)))));
    int top = (int)fmaxf(sw->base.CPortY, fmaxf(0, floorf(fminf(a.y, fminf(b.y, c.y)))));
    int bottom =
        (int)fminf(target.height, fminf(sw->base.CPortY + sw->base.CPortH, ceilf(fmaxf(a.y, fmaxf(b.y, c.y)))));
    bool e0 = swTopLeft(b, c), e1 = swTopLeft(c, a), e2 = swTopLeft(a, b);
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++) {
            float w0 = swEdge(b, c, x + 0.5f, y + 0.5f);
            float w1 = swEdge(c, a, x + 0.5f, y + 0.5f);
            float w2 = swEdge(a, b, x + 0.5f, y + 0.5f);
            if (w0 < 0 || (w0 == 0 && !e0) || w1 < 0 || (w1 == 0 && !e1) || w2 < 0 || (w2 == 0 && !e2)) continue;
            w0 /= area;
            w1 /= area;
            w2 /= area;
            float color[4];
            for (int k = 0; k < 4; k++)
                color[k] = a.color[k] * w0 + b.color[k] * w1 + c.color[k] * w2;
            if (texture.pixels) {
                float u = a.u * w0 + b.u * w1 + c.u * w2;
                float v = a.v * w0 + b.v * w1 + c.v * w2;
                int tx = (int)fminf(texture.width - 1, fmaxf(0, floorf(u * texture.width)));
                int ty = (int)fminf(texture.height - 1, fmaxf(0, floorf(v * texture.height)));
                uint32_t sample = texture.pixels[(size_t)ty * texture.width + tx];
                color[0] *= ((sample >> 16) & 255) / 255.0f;
                color[1] *= ((sample >> 8) & 255) / 255.0f;
                color[2] *= (sample & 255) / 255.0f;
                color[3] *= (sample >> 24) / 255.0f;
            }
            swPixel(sw, target, x, y, color);
        }
}

static void swLine(SWRenderer* sw, SWVertex a, SWVertex b, float width) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.0001f) {
        swPixel(sw, swSurface(sw, sw->target), (int)a.x, (int)a.y, a.color);
        return;
    }
    float ox = -dy / length * fmaxf(1, width) * 0.5f, oy = dx / length * fmaxf(1, width) * 0.5f;
    SWVertex c = b, d = a;
    a.x += ox;
    a.y += oy;
    b.x += ox;
    b.y += oy;
    c.x -= ox;
    c.y -= oy;
    d.x -= ox;
    d.y -= oy;
    swTriangle(sw, (SWImage){0}, a, b, c);
    swTriangle(sw, (SWImage){0}, a, c, d);
}

static void swQuad(SWRenderer* sw, SWImage image, float x, float y, float left, float top, float width, float height,
                   float angle, float u0, float v0, float u1, float v1, uint32_t c1, uint32_t c2, uint32_t c3,
                   uint32_t c4, float alpha) {
    float rad = -angle * 0.017453292519943295f, cs = cosf(rad), sn = sinf(rad);
    float px[4] = {left, left + width, left + width, left}, py[4] = {top, top, top + height, top + height};
    float u[4] = {u0, u1, u1, u0}, v[4] = {v0, v0, v1, v1};
    uint32_t colors[4] = {c1, c2, c3, c4};
    SWVertex q[4];
    for (int i = 0; i < 4; i++)
        q[i] = swVertex(sw, x + px[i] * cs - py[i] * sn, y + px[i] * sn + py[i] * cs, 0, colors[i], alpha, u[i], v[i]);
    // Most 2D draws are axis-aligned, uniformly tinted rectangles.
    if (q[0].y == q[1].y && q[1].x == q[2].x && q[2].y == q[3].y && q[3].x == q[0].x &&
        q[1].x > q[0].x && q[3].y > q[0].y && c1 == c2 && c1 == c3 && c1 == c4 &&
        sw->colorWriteR && sw->colorWriteG && sw->colorWriteB && sw->colorWriteA &&
        (!sw->blendEnable || (sw->blendMode == bm_normal && sw->blendFactors.src == bm_src_alpha &&
                             sw->blendFactors.dst == bm_inv_src_alpha))) {
        SWImage target = swSurface(sw, sw->target);
        int l = (int)fmaxf(0, fmaxf(sw->base.CPortX, ceilf(q[0].x - 0.5f)));
        int t = (int)fmaxf(0, fmaxf(sw->base.CPortY, ceilf(q[0].y - 0.5f)));
        int r = (int)fminf(target.width, fminf(sw->base.CPortX + sw->base.CPortW, ceilf(q[1].x - 0.5f)));
        int b = (int)fminf(target.height, fminf(sw->base.CPortY + sw->base.CPortH, ceilf(q[3].y - 0.5f)));
        uint32_t tint = swPackColor(c1, alpha);
        float du = (u1 - u0) / (q[1].x - q[0].x), dv = (v1 - v0) / (q[3].y - q[0].y);
        for (int yy = t; yy < b; yy++) {
            int ty = image.pixels ? (int)fminf(image.height - 1, fmaxf(0, floorf((v0 + (yy + 0.5f - q[0].y) * dv) * image.height))) : 0;
            for (int xx = l; xx < r; xx++) {
                uint32_t s = tint;
                if (image.pixels) {
                    int tx = (int)fminf(image.width - 1, fmaxf(0, floorf((u0 + (xx + 0.5f - q[0].x) * du) * image.width)));
                    uint32_t texel = image.pixels[(size_t)ty * image.width + tx];
                    s = 0;
                    for (int k = 0; k < 32; k += 8)
                        s |= (((texel >> k) & 255) * ((tint >> k) & 255) / 255) << k;
                }
                uint32_t a = s >> 24;
                if (sw->alphaTestEnable && a <= sw->alphaTestRef) continue;
                uint32_t* dst = &target.pixels[(size_t)yy * target.width + xx];
                if (!sw->blendEnable || a == 255) { *dst = s; continue; }
                if (!a) continue;
                uint32_t result = 0;
                for (int k = 0; k < 32; k += 8)
                    result |= (((((s >> k) & 255) * a + ((*dst >> k) & 255) * (255 - a)) + 127) / 255) << k;
                *dst = result;
            }
        }
        return;
    }
    swTriangle(sw, image, q[0], q[1], q[2]);
    swTriangle(sw, image, q[0], q[2], q[3]);
}

static TexturePageItem* swTpag(SWRenderer* sw, int32_t id) {
    return id >= 0 && (uint32_t)id < sw->base.dataWin->tpag.count ? &sw->base.dataWin->tpag.items[id] : NULL;
}

static void swSpritePart(SWRenderer* sw, int32_t id, float sx, float sy, float w, float h, float x, float y, float xs,
                         float ys, float angle, float ox, float oy, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4,
                         float alpha) {
    TexturePageItem* p = swTpag(sw, id);
    if (!p || p->texturePageId < 0) return;
    SWImage* page = swPage(sw, p->texturePageId);
    if (!page || w <= 0 || h <= 0) return;
    // Clip to this sprite's atlas region so transparent margins don't sample neighboring sprites.
    float left = fmaxf(0, sx), top = fmaxf(0, sy);
    float right = fminf(p->sourceWidth, sx + w), bottom = fminf(p->sourceHeight, sy + h);
    if (left >= right || top >= bottom) return;
    float dx = (left - sx) * xs, dy = (top - sy) * ys;
    float rad = -angle * 0.017453292519943295f, cs = cosf(rad), sn = sinf(rad);
    float px = angle == 0 ? x : ox + (x - ox) * cs - (y - oy) * sn;
    float py = angle == 0 ? y : oy + (x - ox) * sn + (y - oy) * cs;
    swQuad(sw, *page, px, py, dx, dy, (right - left) * xs, (bottom - top) * ys, angle,
           (p->sourceX + left) / page->width, (p->sourceY + top) / page->height,
           (p->sourceX + right) / page->width, (p->sourceY + bottom) / page->height,
           c1, c2, c3, c4, alpha);
}

static void swViewport(SWRenderer* sw, int x, int y, int w, int h) {
    sw->base.CPortX = x;
    sw->base.CPortY = y;
    sw->base.CPortW = w;
    sw->base.CPortH = h;
}

static void swText(SWRenderer* sw, const char* text, float x, float y, float xs, float ys, float angle,
                   float separation, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, float alpha, bool ui) {
    if (!text) return;
    Renderer* r = &sw->base;
    Font* font = !ui && r->drawFont >= 0 && (uint32_t)r->drawFont < r->dataWin->font.count
                     ? &r->dataWin->font.fonts[r->drawFont]
                     : &sw->debugFont;
    bool debug = font == &sw->debugFont;
    xs *= font->scaleX;
    ys *= font->scaleY;
    float stride = separation < 0 ? TextUtils_lineStride(font) : separation;
    int length = (int)strlen(text), lines = TextUtils_countLines(text, length);
    float cursorY = -r->drawValign * 0.5f * lines * stride;
    int start = 0;
    while (start < length) {
        int end = start;
        while (end < length && !TextUtils_isNewlineChar(text[end]))
            end++;
        float lineWidth = TextUtils_measureLineWidth(font, text + start, end - start);
        float cursorX = -r->drawHalign * 0.5f * lineWidth;
        int pos = start;
        while (pos < end) {
            uint16_t ch = TextUtils_decodeUtf8(text, end, &pos);
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            if (!glyph) continue;
            float gx = cursorX + glyph->offset, gy = cursorY;
            SWImage image = {0};
            float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            if (debug) {
                image = sw->debugAtlas;
                const DebugFontGlyphEntry* entry = &debugFontGlyphs[glyph - font->glyphs];
                gy += entry->yoffset;
                u0 = (float)glyph->sourceX / image.width;
                v0 = (float)glyph->sourceY / image.height;
                u1 = (float)(glyph->sourceX + glyph->sourceWidth) / image.width;
                v1 = (float)(glyph->sourceY + glyph->sourceHeight) / image.height;
            } else if (font->isSpriteFont && font->spriteIndex >= 0 &&
                       (uint32_t)font->spriteIndex < r->dataWin->sprt.count) {
                Sprite* sprite = &r->dataWin->sprt.sprites[font->spriteIndex];
                uint32_t index = (uint32_t)(glyph - font->glyphs);
                TexturePageItem* p = index < sprite->textureCount ? swTpag(sw, sprite->tpagIndices[index]) : NULL;
                SWImage* page = p && p->texturePageId >= 0 ? swPage(sw, p->texturePageId) : NULL;
                if (page) {
                    image = *page;
                    gy += p->targetY - font->spriteOriginYAdjust;
                    u0 = (float)p->sourceX / image.width;
                    v0 = (float)p->sourceY / image.height;
                    u1 = (float)(p->sourceX + p->sourceWidth) / image.width;
                    v1 = (float)(p->sourceY + p->sourceHeight) / image.height;
                }
            } else {
                TexturePageItem* p = swTpag(sw, font->tpagIndex);
                SWImage* page = p && p->texturePageId >= 0 ? swPage(sw, p->texturePageId) : NULL;
                if (page) {
                    image = *page;
                    u0 = (float)(p->sourceX + glyph->sourceX) / image.width;
                    v0 = (float)(p->sourceY + glyph->sourceY) / image.height;
                    u1 = (float)(p->sourceX + glyph->sourceX + glyph->sourceWidth) / image.width;
                    v1 = (float)(p->sourceY + glyph->sourceY + glyph->sourceHeight) / image.height;
                }
            }
            if (image.pixels && glyph->sourceWidth && glyph->sourceHeight)
                swQuad(sw, image, x, y, gx * xs, gy * ys, glyph->sourceWidth * xs, glyph->sourceHeight * ys, angle, u0,
                       v0, u1, v1, c1, c2, c3, c4, alpha);
            cursorX += glyph->shift;
            if (pos < end) {
                int next = pos;
                cursorX += TextUtils_getKerningOffset(glyph, TextUtils_decodeUtf8(text, end, &next));
            }
        }
        start = end < length ? TextUtils_skipNewline(text, end, length) : length;
        cursorY += stride;
    }
}

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color) {
    SWRenderer* sw = (SWRenderer*)renderer;
    uint32_t pixel = swPackColor(color, 1);
    for (size_t i = 0; i < (size_t)sw->frame.width * sw->frame.height; i++)
        sw->frame.pixels[i] = pixel;
}

static void swDrawTextUI(Renderer* renderer, const char* text, float x, float y, float xs, float ys, float angle,
                         int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha, float separation) {
    swText((SWRenderer*)renderer, text, x, y, xs, ys, angle, separation, c1, c2, c3, c4, alpha, true);
}

static bool swSurfaceSetPixels(Renderer* renderer, int32_t id, const uint8_t* rgba) {
    SWImage image = swSurface((SWRenderer*)renderer, id);
    if (!image.pixels || !rgba) return false;
    for (size_t i = 0; i < (size_t)image.width * image.height; i++)
        image.pixels[i] = ((uint32_t)rgba[i * 4 + 3] << 24) | ((uint32_t)rgba[i * 4] << 16) |
                          ((uint32_t)rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
    return true;
}

// ===[ Surface allocation ]===
static void swEnsureSurfaceCapacity(SWRenderer* sw, uint32_t needed) {
    if (needed > sw->surfaceCount) sw->surfaceCount = needed;
    if (needed <= sw->surfaceCapacity) return;
    uint32_t cap = sw->surfaceCapacity ? sw->surfaceCapacity * 2 : 16;
    while (cap < needed)
        cap *= 2;
    sw->surfaceWidths = safeRealloc(sw->surfaceWidths, cap * sizeof(int32_t));
    sw->surfaceHeights = safeRealloc(sw->surfaceHeights, cap * sizeof(int32_t));
    sw->surfaceExistsFlag = safeRealloc(sw->surfaceExistsFlag, cap * sizeof(bool));
    sw->surfacePixels = safeRealloc(sw->surfacePixels, cap * sizeof(uint32_t*));
    for (uint32_t i = sw->surfaceCapacity; i < cap; i++) {
        sw->surfaceWidths[i] = sw->surfaceHeights[i] = 0;
        sw->surfaceExistsFlag[i] = false;
        sw->surfacePixels[i] = NULL;
    }
    sw->surfaceCapacity = cap;
}

// ===[ Renderer implementation ]===

static void swInit(Renderer* renderer, DataWin* dataWin) {
    SWRenderer* sw = (SWRenderer*)renderer;
    renderer->dataWin = dataWin;
    sw->pages = safeCalloc(dataWin->txtr.count, sizeof(SWImage));
    sw->target = sw->application = -1;
    Matrix4f_identity(&renderer->gmlMatrices[MATRIX_WORLD]);
    Matrix4f_identity(&sw->viewProjection);
    sw->debugAtlas =
        (SWImage){DEBUGFONT_ATLAS_W, DEBUGFONT_ATLAS_H, safeMalloc(DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H * 4)};
    for (int i = 0; i < DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H; i++)
        sw->debugAtlas.pixels[i] = ((uint32_t)debugFontPixels[i] << 24) | 0xFFFFFF;
    sw->debugFont.scaleX = sw->debugFont.scaleY = 1;
    sw->debugFont.emSize = sw->debugFont.maxGlyphHeight = DEBUGFONT_LINE_HEIGHT;
    sw->debugFont.glyphs = sw->debugGlyphs;
    sw->debugFont.glyphCount = DEBUGFONT_GLYPH_COUNT;
    for (int i = 0; i < DEBUGFONT_GLYPH_COUNT; i++) {
        const DebugFontGlyphEntry* e = &debugFontGlyphs[i];
        FontGlyph* g = &sw->debugGlyphs[i];
        g->character = DEBUGFONT_FIRST_CP + i;
        g->sourceX = e->x;
        g->sourceY = e->y;
        g->sourceWidth = e->w;
        g->sourceHeight = e->h;
        g->shift = e->xadvance;
        g->offset = e->xoffset;
    }
    Font_buildGlyphLUT(&sw->debugFont);
    logInfo("Software renderer initialized\n");
}

static void swDestroy(Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    VMBuiltins_resetD3D(renderer);
    for (uint32_t i = 0; i < sw->surfaceCount; i++)
        free(sw->surfacePixels[i]);
    for (uint32_t i = 0; i < renderer->dataWin->txtr.count; i++)
        free(sw->pages[i].pixels);
    free(sw->pages);
    free(sw->debugAtlas.pixels);
    free(sw->vertices);
    free(sw->frame.pixels);
    free(sw->surfaceWidths);
    free(sw->surfaceHeights);
    free(sw->surfaceExistsFlag);
    free(sw->surfacePixels);
    free(sw);
}

static void swBeginFrame(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t gameW, MAYBE_UNUSED int32_t gameH,
                         MAYBE_UNUSED int32_t windowW, MAYBE_UNUSED int32_t windowH) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (!swValidSize(windowW, windowH)) return;
    if (sw->frame.width != windowW || sw->frame.height != windowH) {
        free(sw->frame.pixels);
        sw->frame = (SWImage){windowW, windowH, safeCalloc((size_t)windowW * windowH, 4)};
    }
    sw->target = renderer->runner->usingAppSurface ? sw->application : -1;
    SWImage image = swSurface(sw, sw->target);
    swViewport(sw, 0, 0, image.width, image.height);
    Matrix4f_viewProjection(&sw->viewProjection, 0, 0, gameW, gameH, 0);
}
static void swEndFrameInit(MAYBE_UNUSED Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (!renderer->runner->usingAppSurface) return;
    if (!renderer->runner->appSurfaceAutoDraw) {
        sw->target = -1;
        swViewport(sw, 0, 0, sw->frame.width, sw->frame.height);
        return;
    }
    SWImage app = swSurface(sw, sw->application);
    if (!app.pixels || !sw->frame.pixels) return;
    float scale = fminf((float)sw->frame.width / app.width, (float)sw->frame.height / app.height);
    int w = (int)(app.width * scale), h = (int)(app.height * scale), left = (sw->frame.width - w) / 2,
        top = (sw->frame.height - h) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            sw->frame.pixels[(size_t)(top + y) * sw->frame.width + left + x] =
                app.pixels[(size_t)((int)((float)y / h * app.height)) * app.width + (int)((float)x / w * app.width)];
    sw->target = -1;
    swViewport(sw, 0, 0, sw->frame.width, sw->frame.height);
}
static void swEndFrameEnd(MAYBE_UNUSED Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (sw->frame.pixels) Runner_setNextFrame(sw->frame.pixels, sw->frame.width, sw->frame.height);
}
static void swBeginView(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t viewX, MAYBE_UNUSED int32_t viewY,
                        MAYBE_UNUSED int32_t viewW, MAYBE_UNUSED int32_t viewH, MAYBE_UNUSED int32_t portX,
                        MAYBE_UNUSED int32_t portY, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH,
                        MAYBE_UNUSED float viewAngle) {
    SWRenderer* sw = (SWRenderer*)renderer;
    swViewport(sw, portX, portY, portW, portH);
    Matrix4f_viewProjection(&sw->viewProjection, viewX, viewY, viewW, viewH, viewAngle);
    if (renderer->runner) {
        int index = renderer->runner->viewsEnabled ? renderer->runner->viewCurrent : 0;
        renderer->cameraCurrent = renderer->runner->views[index].cameraId;
        GMLCamera* camera = Runner_getCameraById(renderer->runner, renderer->cameraCurrent);
        if (camera) Matrix4f_multiply(&sw->viewProjection, &camera->projectionMatrix, &camera->viewMatrix);
    }
}
static void swEndView(MAYBE_UNUSED Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWImage image = swSurface(sw, sw->target);
    swViewport(sw, 0, 0, image.width, image.height);
}
static void swApplyProjection(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED const Matrix4f* viewMatrix,
                              MAYBE_UNUSED const Matrix4f* projectionMatrix) {
    SWRenderer* sw = (SWRenderer*)renderer;
    renderer->gmlMatrices[MATRIX_VIEW] = *viewMatrix;
    renderer->gmlMatrices[MATRIX_PROJECTION] = *projectionMatrix;
    Matrix4f_multiply(&sw->viewProjection, projectionMatrix, viewMatrix);
}
static void swBeginGUI(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH,
                       MAYBE_UNUSED int32_t portX, MAYBE_UNUSED int32_t portY, MAYBE_UNUSED int32_t portW,
                       MAYBE_UNUSED int32_t portH, MAYBE_UNUSED int32_t targetSurfaceId) {
    SWRenderer* sw = (SWRenderer*)renderer;
    sw->target = targetSurfaceId;
    swViewport(sw, portX, portY, portW, portH);
    Matrix4f_viewProjection(&sw->viewProjection, 0, 0, guiW, guiH, 0);
}
static void swSetGuiProjection(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH,
                               MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH,
                               MAYBE_UNUSED bool renderingToUserSurface) {
    Matrix4f_viewProjection(&((SWRenderer*)renderer)->viewProjection, 0, 0, guiW, guiH, 0);
}
static void swEndGUI(MAYBE_UNUSED Renderer* renderer) {}

static void swDrawSprite(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED float x,
                         MAYBE_UNUSED float y, MAYBE_UNUSED float originX, MAYBE_UNUSED float originY,
                         MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg,
                         MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    TexturePageItem* p = swTpag(sw, tpagIndex);
    if (!p || p->texturePageId < 0) return;
    SWImage* page = swPage(sw, p->texturePageId);
    if (!page) return;
    swQuad(sw, *page, x, y, (p->targetX - originX) * xscale, (p->targetY - originY) * yscale, p->targetWidth * xscale,
           p->targetHeight * yscale, angleDeg, (float)p->sourceX / page->width, (float)p->sourceY / page->height,
           (float)(p->sourceX + p->sourceWidth) / page->width, (float)(p->sourceY + p->sourceHeight) / page->height,
           color, color, color, color, alpha);
}
static void swDrawSpritePart(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex,
                             MAYBE_UNUSED float srcOffX, MAYBE_UNUSED float srcOffY, MAYBE_UNUSED float srcW,
                             MAYBE_UNUSED float srcH, MAYBE_UNUSED float x, MAYBE_UNUSED float y,
                             MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg,
                             MAYBE_UNUSED float pivotX, MAYBE_UNUSED float pivotY, MAYBE_UNUSED uint32_t color,
                             MAYBE_UNUSED float alpha) {
    swSpritePart((SWRenderer*)renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX,
                 pivotY, color, color, color, color, alpha);
}
static void swDrawSpritePartColor(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex,
                                  MAYBE_UNUSED float srcOffX, MAYBE_UNUSED float srcOffY, MAYBE_UNUSED float srcW,
                                  MAYBE_UNUSED float srcH, MAYBE_UNUSED float x, MAYBE_UNUSED float y,
                                  MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg,
                                  MAYBE_UNUSED float pivotX, MAYBE_UNUSED float pivotY, MAYBE_UNUSED uint32_t color1,
                                  MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3,
                                  MAYBE_UNUSED uint32_t color4, MAYBE_UNUSED float alpha) {
    swSpritePart((SWRenderer*)renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX,
                 pivotY, color1, color2, color3, color4, alpha);
}
static void swDrawSpritePos(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED float x1,
                            MAYBE_UNUSED float y1, MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED float x3,
                            MAYBE_UNUSED float y3, MAYBE_UNUSED float x4, MAYBE_UNUSED float y4,
                            MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    TexturePageItem* p = swTpag(sw, tpagIndex);
    if (!p || p->texturePageId < 0) return;
    SWImage* image = swPage(sw, p->texturePageId);
    if (!image) return;
    float u0 = (float)p->sourceX / image->width, v0 = (float)p->sourceY / image->height;
    float u1 = (float)(p->sourceX + p->sourceWidth) / image->width,
          v1 = (float)(p->sourceY + p->sourceHeight) / image->height;
    SWVertex a = swVertex(sw, x1, y1, 0, 0xFFFFFF, alpha, u0, v0), b = swVertex(sw, x2, y2, 0, 0xFFFFFF, alpha, u1, v0);
    SWVertex c = swVertex(sw, x3, y3, 0, 0xFFFFFF, alpha, u1, v1), d = swVertex(sw, x4, y4, 0, 0xFFFFFF, alpha, u0, v1);
    swTriangle(sw, *image, a, b, c);
    swTriangle(sw, *image, a, c, d);
}
static void swDrawRectangle(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1,
                            MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED uint32_t color,
                            MAYBE_UNUSED float alpha, MAYBE_UNUSED bool outline) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (!outline) {
        swQuad(sw, (SWImage){0}, x1, y1, 0, 0, x2 - x1, y2 - y1, 0, 0, 0, 0, 0, color, color, color, color, alpha);
        return;
    }
    SWVertex a = swVertex(sw, x1, y1, 0, color, alpha, 0, 0), b = swVertex(sw, x2, y1, 0, color, alpha, 0, 0);
    SWVertex c = swVertex(sw, x2, y2, 0, color, alpha, 0, 0), d = swVertex(sw, x1, y2, 0, color, alpha, 0, 0);
    swLine(sw, a, b, 1);
    swLine(sw, b, c, 1);
    swLine(sw, c, d, 1);
    swLine(sw, d, a, 1);
}
static void swDrawRectangleColor(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1,
                                 MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED uint32_t color1,
                                 MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3,
                                 MAYBE_UNUSED uint32_t color4, MAYBE_UNUSED float alpha, MAYBE_UNUSED bool outline) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWVertex a = swVertex(sw, x1, y1, 0, color1, alpha, 0, 0), b = swVertex(sw, x2, y1, 0, color2, alpha, 0, 0);
    SWVertex c = swVertex(sw, x2, y2, 0, color3, alpha, 0, 0), d = swVertex(sw, x1, y2, 0, color4, alpha, 0, 0);
    if (outline) {
        swLine(sw, a, b, 1);
        swLine(sw, b, c, 1);
        swLine(sw, c, d, 1);
        swLine(sw, d, a, 1);
    } else {
        swTriangle(sw, (SWImage){0}, a, b, c);
        swTriangle(sw, (SWImage){0}, a, c, d);
    }
}
static void swDrawLine(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1,
                       MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED float width,
                       MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    swLine(sw, swVertex(sw, x1, y1, 0, color, alpha, 0, 0), swVertex(sw, x2, y2, 0, color, alpha, 0, 0), width);
}
static void swDrawLineColor(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1,
                            MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED float width,
                            MAYBE_UNUSED uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    swLine(sw, swVertex(sw, x1, y1, 0, color1, alpha, 0, 0), swVertex(sw, x2, y2, 0, color2, alpha, 0, 0), width);
}
static void swDrawTriangle(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1,
                           MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED float x3, MAYBE_UNUSED float y3,
                           MAYBE_UNUSED uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3,
                           MAYBE_UNUSED float alpha, MAYBE_UNUSED bool outline) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWVertex a = swVertex(sw, x1, y1, 0, color1, alpha, 0, 0), b = swVertex(sw, x2, y2, 0, color2, alpha, 0, 0),
             c = swVertex(sw, x3, y3, 0, color3, alpha, 0, 0);
    if (outline) {
        swLine(sw, a, b, 1);
        swLine(sw, b, c, 1);
        swLine(sw, c, a, 1);
    } else
        swTriangle(sw, (SWImage){0}, a, b, c);
}
static void swDrawText(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED const char* text, MAYBE_UNUSED float x,
                       MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale,
                       MAYBE_UNUSED float angleDeg, MAYBE_UNUSED float lineSeparation) {
    swText((SWRenderer*)renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation, renderer->drawColor,
           renderer->drawColor, renderer->drawColor, renderer->drawColor, renderer->drawAlpha, false);
}
static void swDrawTextColor(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED const char* text, MAYBE_UNUSED float x,
                            MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale,
                            MAYBE_UNUSED float angleDeg, MAYBE_UNUSED int32_t c1, MAYBE_UNUSED int32_t c2,
                            MAYBE_UNUSED int32_t c3, MAYBE_UNUSED int32_t c4, MAYBE_UNUSED float alpha,
                            MAYBE_UNUSED float lineSeparation) {
    swText((SWRenderer*)renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation, c1, c2, c3, c4, alpha, false);
}
static void swFlush(MAYBE_UNUSED Renderer* renderer) {}
static void swClearScreen(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWImage image = swSurface(sw, sw->target);
    uint32_t pixel = swPackColor(color, alpha);
    for (size_t i = 0; i < (size_t)image.width * image.height; i++)
        image.pixels[i] = pixel;
}

static int32_t swCreateSpriteFromSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID,
                                         MAYBE_UNUSED int32_t x, MAYBE_UNUSED int32_t y, MAYBE_UNUSED int32_t w,
                                         MAYBE_UNUSED int32_t h, MAYBE_UNUSED bool removeback, MAYBE_UNUSED bool smooth,
                                         MAYBE_UNUSED int32_t xorig, MAYBE_UNUSED int32_t yorig) {
    return -1;
}
static void swDeleteSprite(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t spriteIndex) {}

static BlendFactors swGpuGetBlendFactors(Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    return sw->blendFactors;
}
static int32_t swGpuGetBlendMode(Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    return sw->blendMode;
}
static void swGpuSetBlendMode(Renderer* renderer, int32_t mode) {
    SWRenderer* sw = (SWRenderer*)renderer;
    sw->blendMode = mode;
    sw->blendFactors.src = mode == bm_subtract ? bm_zero : mode == bm_min ? bm_one : bm_src_alpha;
    sw->blendFactors.dst = mode == bm_normal ? bm_inv_src_alpha :
        (mode == bm_subtract || mode == bm_max) ? bm_inv_src_color : bm_one;
    sw->blendFactors.srcAlpha = sw->blendFactors.src;
    sw->blendFactors.dstAlpha = sw->blendFactors.dst;
}
static void swGpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha,
                                 int32_t dfactor_alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    sw->blendMode = bm_complex;
    sw->blendFactors.src = sfactor;
    sw->blendFactors.dst = dfactor;
    sw->blendFactors.srcAlpha = sfactor_alpha;
    sw->blendFactors.dstAlpha = dfactor_alpha;
}
static void swGpuSetBlendEnable(Renderer* renderer, bool enable) { ((SWRenderer*)renderer)->blendEnable = enable; }
static void swGpuSetAlphaTestEnable(Renderer* renderer, bool enable) {
    ((SWRenderer*)renderer)->alphaTestEnable = enable;
}
static bool swGpuGetAlphaTestEnable(Renderer* renderer) { return ((SWRenderer*)renderer)->alphaTestEnable; }
static void swGpuSetAlphaTestRef(Renderer* renderer, uint8_t ref) { ((SWRenderer*)renderer)->alphaTestRef = ref; }
static void swGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    sw->colorWriteR = red;
    sw->colorWriteG = green;
    sw->colorWriteB = blue;
    sw->colorWriteA = alpha;
}
static void swGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (red) *red = sw->colorWriteR;
    if (green) *green = sw->colorWriteG;
    if (blue) *blue = sw->colorWriteB;
    if (alpha) *alpha = sw->colorWriteA;
}
static bool swGpuGetBlendEnable(Renderer* renderer) { return ((SWRenderer*)renderer)->blendEnable; }
static void swGpuSetFog(Renderer* renderer, bool enable, uint32_t color, float start, float end) {
    (void)start;
    (void)end;
    SWRenderer* sw = (SWRenderer*)renderer;
    sw->fogEnable = enable;
    sw->fogColor = color;
}
static void swDrawSpriteTiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex,
                              MAYBE_UNUSED float originX, MAYBE_UNUSED float originY, MAYBE_UNUSED float x,
                              MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale,
                              MAYBE_UNUSED bool tileX, MAYBE_UNUSED bool tileY, MAYBE_UNUSED float roomW,
                              MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    TexturePageItem* p = swTpag(sw, tpagIndex);
    if (!p) return;
    float w = fabsf(p->boundingWidth * xscale), h = fabsf(p->boundingHeight * yscale);
    if (w < 0.01f || h < 0.01f) return;
    float sx = tileX ? x - floorf(x / w) * w - w : x, sy = tileY ? y - floorf(y / h) * h - h : y;
    for (float yy = sy; yy < (tileY ? roomH : sy + 0.5f); yy += h)
        for (float xx = sx; xx < (tileX ? roomW : sx + 0.5f); xx += w)
            swDrawSprite(renderer, tpagIndex, xx, yy, originX, originY, xscale, yscale, 0, color, alpha);
}
static void swPrimitiveBegin(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t kind) {
    SWRenderer* sw = (SWRenderer*)renderer;
    sw->primitive = kind;
    sw->primitiveTexture = 0;
    sw->vertexCount = 0;
}
static void swPrimitiveBeginTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t kind,
                                    MAYBE_UNUSED int32_t texture) {
    swPrimitiveBegin(renderer, kind);
    ((SWRenderer*)renderer)->primitiveTexture = (uint32_t)texture;
}
static void swPrimitiveEnd(MAYBE_UNUSED Renderer* renderer) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWImage image = swTexture(sw, sw->primitiveTexture);
    SWVertex* v = sw->vertices;
    uint32_t n = sw->vertexCount;
    switch (sw->primitive) {
    case PRIMITIVE_POINTS:
        for (uint32_t i = 0; i < n; i++)
            swPixel(sw, swSurface(sw, sw->target), (int)v[i].x, (int)v[i].y, v[i].color);
        break;
    case PRIMITIVE_LINES:
        for (uint32_t i = 1; i < n; i += 2)
            swLine(sw, v[i - 1], v[i], 1);
        break;
    case PRIMITIVE_LINE_STRIP:
        for (uint32_t i = 1; i < n; i++)
            swLine(sw, v[i - 1], v[i], 1);
        break;
    case PRIMITIVE_TRIANGLES:
        for (uint32_t i = 2; i < n; i += 3)
            swTriangle(sw, image, v[i - 2], v[i - 1], v[i]);
        break;
    case PRIMITIVE_TRIANGLE_STRIP:
        for (uint32_t i = 2; i < n; i++)
            swTriangle(sw, image, v[i - 2], v[i - 1], v[i]);
        break;
    case PRIMITIVE_TRIANGLE_FAN:
        for (uint32_t i = 2; i < n; i++)
            swTriangle(sw, image, v[0], v[i - 1], v[i]);
        break;
    default:
        break;
    }
    sw->vertexCount = 0;
}
static void swDrawVertex(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED float x, MAYBE_UNUSED float y,
                         MAYBE_UNUSED float z, MAYBE_UNUSED uint32_t colour, MAYBE_UNUSED float alpha,
                         MAYBE_UNUSED float u, MAYBE_UNUSED float v) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (sw->vertexCount == sw->vertexCapacity) {
        sw->vertexCapacity = sw->vertexCapacity ? sw->vertexCapacity * 2 : 64;
        sw->vertices = safeRealloc(sw->vertices, sw->vertexCapacity * sizeof(SWVertex));
    }
    sw->vertices[sw->vertexCount++] = swVertex(sw, x, y, z, colour, alpha, u, v);
}

static int32_t swCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    if (!swValidSize(width, height)) return -1;
    SWRenderer* sw = (SWRenderer*)renderer;
    uint32_t id = 0;
    while (id < sw->surfaceCount && sw->surfaceExistsFlag[id])
        id++;
    swEnsureSurfaceCapacity(sw, id + 1);
    sw->surfaceWidths[id] = width;
    sw->surfaceHeights[id] = height;
    sw->surfacePixels[id] = safeCalloc((size_t)width * height, 4);
    sw->surfaceExistsFlag[id] = true;
    return (int32_t)id;
}
static bool swSurfaceExists(Renderer* renderer, int32_t surfaceID) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount) return false;
    return sw->surfaceExistsFlag[surfaceID];
}
static bool swSetRenderTarget(MAYBE_UNUSED Renderer* renderer, int32_t surfaceID,
                              MAYBE_UNUSED bool implicitApplicationSurface) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (surfaceID >= 0 && !swSurfaceExists(renderer, surfaceID)) return false;
    sw->target = surfaceID;
    SWImage image = swSurface(sw, surfaceID);
    swViewport(sw, 0, 0, image.width, image.height);
    Matrix4f_viewProjection(&sw->viewProjection, 0, 0, image.width, image.height, 0);
    if (implicitApplicationSurface && surfaceID == sw->application) {
        int index = renderer->runner->viewsEnabled ? renderer->runner->viewCurrent : 0;
        GMLCamera* camera = Runner_getCameraById(renderer->runner, renderer->runner->views[index].cameraId);
        if (camera) Matrix4f_multiply(&sw->viewProjection, &camera->projectionMatrix, &camera->viewMatrix);
    }
    return true;
}
static int32_t swEnsureApplicationSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t width,
                                          MAYBE_UNUSED int32_t height) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (sw->application < 0 || !swSurfaceExists(renderer, sw->application))
        sw->application = swCreateSurface(renderer, width, height);
    else if (sw->surfaceWidths[sw->application] != width || sw->surfaceHeights[sw->application] != height)
        swSurfaceResize(renderer, sw->application, width, height);
    return sw->application;
}
static float swGetSurfaceWidth(Renderer* renderer, int32_t surfaceID) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount) return 0.0f;
    if (!sw->surfaceExistsFlag[surfaceID]) return 0.0f;
    return (float)sw->surfaceWidths[surfaceID];
}
static float swGetSurfaceHeight(Renderer* renderer, int32_t surfaceID) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount) return 0.0f;
    if (!sw->surfaceExistsFlag[surfaceID]) return 0.0f;
    return (float)sw->surfaceHeights[surfaceID];
}
static void swDrawSurface(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED int32_t srcLeft,
                          MAYBE_UNUSED int32_t srcTop, MAYBE_UNUSED int32_t srcWidth, MAYBE_UNUSED int32_t srcHeight,
                          MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float xscale,
                          MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED uint32_t color,
                          MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWImage image = swSurface(sw, surfaceID);
    if (!image.pixels) return;
    if (srcWidth < 0) { srcLeft = srcTop = 0; srcWidth = image.width; srcHeight = image.height; }
    swQuad(sw, image, x, y, 0, 0, srcWidth * xscale, srcHeight * yscale, angleDeg, (float)srcLeft / image.width,
           (float)srcTop / image.height, (float)(srcLeft + srcWidth) / image.width,
           (float)(srcTop + srcHeight) / image.height, color, color, color, color, alpha);
}
static void swDrawSurfaceColor(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID,
                               MAYBE_UNUSED int32_t srcLeft, MAYBE_UNUSED int32_t srcTop, MAYBE_UNUSED int32_t srcWidth,
                               MAYBE_UNUSED int32_t srcHeight, MAYBE_UNUSED float x, MAYBE_UNUSED float y,
                               MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg,
                               MAYBE_UNUSED uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3,
                               MAYBE_UNUSED uint32_t color4, MAYBE_UNUSED float alpha) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWImage image = swSurface(sw, surfaceID);
    if (!image.pixels) return;
    if (srcWidth < 0) { srcLeft = srcTop = 0; srcWidth = image.width; srcHeight = image.height; }
    swQuad(sw, image, x, y, 0, 0, srcWidth * xscale, srcHeight * yscale, angleDeg, (float)srcLeft / image.width,
           (float)srcTop / image.height, (float)(srcLeft + srcWidth) / image.width,
           (float)(srcTop + srcHeight) / image.height, color1, color2, color3, color4, alpha);
}
static void swDrawSurfaceTiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED float x,
                               MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale,
                               MAYBE_UNUSED float roomW, MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color,
                               MAYBE_UNUSED float alpha) {
    SWImage image = swSurface((SWRenderer*)renderer, surfaceID);
    float w = fabsf(image.width * xscale), h = fabsf(image.height * yscale);
    if (!image.pixels || w < 0.01f || h < 0.01f) return;
    float sx = x - floorf(x / w) * w - w, sy = y - floorf(y / h) * h - h;
    for (float yy = sy; yy < roomH; yy += h)
        for (float xx = sx; xx < roomW; xx += w)
            swDrawSurface(renderer, surfaceID, 0, 0, image.width, image.height, xx, yy, xscale, yscale, 0, color,
                          alpha);
}
static void swSurfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (!swValidSize(width, height) || !swSurfaceExists(renderer, surfaceID)) return;
    free(sw->surfacePixels[surfaceID]);
    sw->surfacePixels[surfaceID] = safeCalloc((size_t)width * height, 4);
    sw->surfaceWidths[surfaceID] = width;
    sw->surfaceHeights[surfaceID] = height;
}
static void swSurfaceFree(Renderer* renderer, int32_t surfaceID) {
    SWRenderer* sw = (SWRenderer*)renderer;
    if (!swSurfaceExists(renderer, surfaceID)) return;
    free(sw->surfacePixels[surfaceID]);
    sw->surfacePixels[surfaceID] = NULL;
    sw->surfaceExistsFlag[surfaceID] = false;
    if (sw->target == surfaceID) sw->target = -1;
    if (sw->application == surfaceID) sw->application = -1;
}
static void swSurfaceCopy(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t destSurfaceID,
                          MAYBE_UNUSED int32_t destX, MAYBE_UNUSED int32_t destY, MAYBE_UNUSED int32_t srcSurfaceID,
                          MAYBE_UNUSED int32_t srcX, MAYBE_UNUSED int32_t srcY, MAYBE_UNUSED int32_t srcW,
                          MAYBE_UNUSED int32_t srcH, MAYBE_UNUSED bool part) {
    SWRenderer* sw = (SWRenderer*)renderer;
    SWImage src = swSurface(sw, srcSurfaceID), dst = swSurface(sw, destSurfaceID);
    if (!src.pixels || !dst.pixels) return;
    if (!part) {
        srcX = srcY = 0;
        srcW = src.width;
        srcH = src.height;
    }
    if (!swValidSize(srcW, srcH)) return;
    uint32_t* copy = safeCalloc((size_t)srcW * srcH, 4);
    for (int y = 0; y < srcH; y++)
        for (int x = 0; x < srcW; x++)
            if (srcX + x >= 0 && srcY + y >= 0 && srcX + x < src.width && srcY + y < src.height)
                copy[(size_t)y * srcW + x] = src.pixels[(size_t)(srcY + y) * src.width + srcX + x];
    for (int y = 0; y < srcH; y++)
        for (int x = 0; x < srcW; x++)
            if (destX + x >= 0 && destY + y >= 0 && destX + x < dst.width && destY + y < dst.height)
                dst.pixels[(size_t)(destY + y) * dst.width + destX + x] = copy[(size_t)y * srcW + x];
    free(copy);
}
static bool swSurfaceGetPixels(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID,
                               MAYBE_UNUSED uint8_t* outRGBA) {
    SWImage image = swSurface((SWRenderer*)renderer, surfaceID);
    if (!image.pixels || !outRGBA) return false;
    for (size_t i = 0; i < (size_t)image.width * image.height; i++) {
        uint32_t p = image.pixels[i];
        outRGBA[i * 4] = p >> 16;
        outRGBA[i * 4 + 1] = p >> 8;
        outRGBA[i * 4 + 2] = p;
        outRGBA[i * 4 + 3] = p >> 24;
    }
    return true;
}
static void swDrawTiledPart(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED int32_t srcX,
                            MAYBE_UNUSED int32_t srcY, MAYBE_UNUSED int32_t srcW, MAYBE_UNUSED int32_t srcH,
                            MAYBE_UNUSED float dstX, MAYBE_UNUSED float dstY, MAYBE_UNUSED float dstW,
                            MAYBE_UNUSED float dstH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    if (srcW <= 0 || srcH <= 0) return;
    for (float yy = 0; yy < dstH; yy += srcH)
        for (float xx = 0; xx < dstW; xx += srcW)
            swSpritePart((SWRenderer*)renderer, tpagIndex, srcX, srcY, fminf(srcW, dstW - xx), fminf(srcH, dstH - yy),
                         dstX + xx, dstY + yy, 1, 1, 0, 0, 0, color, color, color, color, alpha);
}

static void swGpuSetShader(Renderer* renderer, int32_t shaderIndex) { renderer->currentShader = shaderIndex; }
static void swGpuResetShader(Renderer* renderer) { renderer->currentShader = -1; }
static int32_t swShaderGetUniform(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex,
                                  MAYBE_UNUSED char* uniform) {
    return -1;
}
static int32_t swShaderGetSamplerIndex(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex,
                                       MAYBE_UNUSED char* uniform) {
    return -1;
}
static void swShaderSetUniformF(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle,
                                MAYBE_UNUSED int32_t count, MAYBE_UNUSED float v1, MAYBE_UNUSED float v2,
                                MAYBE_UNUSED float v3, MAYBE_UNUSED float v4) {}
static void swShaderSetUniformFArray(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle,
                                     MAYBE_UNUSED float* values, MAYBE_UNUSED uint32_t count) {}
static void swShaderSetUniformI(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle,
                                MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t v1, MAYBE_UNUSED int32_t v2,
                                MAYBE_UNUSED int32_t v3, MAYBE_UNUSED int32_t v4) {}
static uint32_t swSpriteGetTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex) {
    SWRenderer* sw = (SWRenderer*)renderer;
    TexturePageItem* p = swTpag(sw, tpagIndex);
    return p && p->texturePageId >= 0 ? (uint32_t)p->texturePageId + 1 : 0;
}
static uint32_t swSurfaceGetTexture(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) {
    return swSurfaceExists(renderer, surfaceID) ? 0x80000000U | ((uint32_t)surfaceID + 1) : 0;
}
static float swTextureGetTexelWidth(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) {
    SWImage image = swTexture((SWRenderer*)renderer, texID);
    return image.width ? 1.0f / image.width : 0;
}
static float swTextureGetTexelHeight(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) {
    SWImage image = swTexture((SWRenderer*)renderer, texID);
    return image.height ? 1.0f / image.height : 0;
}
static bool swTextureGetUVs(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID, MAYBE_UNUSED float* outUVs) {
    SWImage image = swTexture((SWRenderer*)renderer, texID);
    if (!image.pixels || !outUVs) return false;
    outUVs[0] = outUVs[1] = 0;
    outUVs[2] = outUVs[3] = 1;
    return true;
}
static void swTextureSetStage(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texID) {
}
static bool swShaderIsCompiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shader) { return false; }
static bool swShadersSupported(void) { return false; }
static void swSetMatrix(Renderer* renderer, int32_t matrixType, Matrix4f matrix) {
    if (matrixType >= 0 && matrixType < MATRICES_MAX) renderer->gmlMatrices[matrixType] = matrix;
    if (matrixType == MATRIX_VIEW || matrixType == MATRIX_PROJECTION)
        Matrix4f_multiply(&((SWRenderer*)renderer)->viewProjection, &renderer->gmlMatrices[MATRIX_PROJECTION],
                          &renderer->gmlMatrices[MATRIX_VIEW]);
}
static void swGpuSetTexFilter(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED bool enable) {
    renderer->texFilter = enable;
}

static RendererVtable swVtable;

Renderer* SWRenderer_create(void) {
    SWRenderer* sw = (SWRenderer*)safeCalloc(1, sizeof(SWRenderer));
    sw->base.vtable = &swVtable;
    swVtable.init = swInit;
    swVtable.destroy = swDestroy;
    swVtable.beginFrame = swBeginFrame;
    swVtable.endFrameInit = swEndFrameInit;
    swVtable.endFrameEnd = swEndFrameEnd;
    swVtable.beginView = swBeginView;
    swVtable.endView = swEndView;
    swVtable.applyProjection = swApplyProjection;
    swVtable.beginGUI = swBeginGUI;
    swVtable.setGuiProjection = swSetGuiProjection;
    swVtable.endGUI = swEndGUI;
    swVtable.drawSprite = swDrawSprite;
    swVtable.drawSpritePart = swDrawSpritePart;
    swVtable.drawSpritePartColor = swDrawSpritePartColor;
    swVtable.drawSpritePos = swDrawSpritePos;
    swVtable.drawRectangle = swDrawRectangle;
    swVtable.drawRectangleColor = swDrawRectangleColor;
    swVtable.drawLine = swDrawLine;
    swVtable.drawTriangle = swDrawTriangle;
    swVtable.drawLineColor = swDrawLineColor;
    swVtable.drawText = swDrawText;
    swVtable.drawTextColor = swDrawTextColor;
    swVtable.drawTextUI = swDrawTextUI;
    swVtable.surfaceSetPixels = swSurfaceSetPixels;
    swVtable.flush = swFlush;
    swVtable.clearScreen = swClearScreen;
    swVtable.createSpriteFromSurface = swCreateSpriteFromSurface;
    swVtable.deleteSprite = swDeleteSprite;
    swVtable.gpuGetBlendFactors = swGpuGetBlendFactors;
    swVtable.gpuGetBlendMode = swGpuGetBlendMode;
    swVtable.gpuSetBlendMode = swGpuSetBlendMode;
    swVtable.gpuSetBlendModeExt = swGpuSetBlendModeExt;
    swVtable.gpuSetBlendEnable = swGpuSetBlendEnable;
    swVtable.gpuSetAlphaTestEnable = swGpuSetAlphaTestEnable;
    swVtable.gpuGetAlphaTestEnable = swGpuGetAlphaTestEnable;
    swVtable.gpuSetAlphaTestRef = swGpuSetAlphaTestRef;
    swVtable.gpuSetColorWriteEnable = swGpuSetColorWriteEnable;
    swVtable.gpuGetColorWriteEnable = swGpuGetColorWriteEnable;
    swVtable.gpuGetBlendEnable = swGpuGetBlendEnable;
    swVtable.gpuSetTexFilter = swGpuSetTexFilter;
    swVtable.gpuSetFog = swGpuSetFog;
    swVtable.drawTile = NULL;
    swVtable.drawSpriteTiled = swDrawSpriteTiled;
    swVtable.primitiveBegin = swPrimitiveBegin;
    swVtable.primitiveBeginTexture = swPrimitiveBeginTexture;
    swVtable.primitiveEnd = swPrimitiveEnd;
    swVtable.drawVertex = swDrawVertex;
    swVtable.createSurface = swCreateSurface;
    swVtable.surfaceExists = swSurfaceExists;
    swVtable.setRenderTarget = swSetRenderTarget;
    swVtable.ensureApplicationSurface = swEnsureApplicationSurface;
    swVtable.getSurfaceWidth = swGetSurfaceWidth;
    swVtable.getSurfaceHeight = swGetSurfaceHeight;
    swVtable.drawSurface = swDrawSurface;
    swVtable.drawSurfaceColor = swDrawSurfaceColor;
    swVtable.drawSurfaceTiled = swDrawSurfaceTiled;
    swVtable.surfaceResize = swSurfaceResize;
    swVtable.surfaceFree = swSurfaceFree;
    swVtable.surfaceCopy = swSurfaceCopy;
    swVtable.surfaceGetPixels = swSurfaceGetPixels;
    swVtable.drawTiledPart = swDrawTiledPart;
    swVtable.gpuSetShader = swGpuSetShader;
    swVtable.gpuResetShader = swGpuResetShader;
    swVtable.shaderGetUniform = swShaderGetUniform;
    swVtable.shaderGetSamplerIndex = swShaderGetSamplerIndex;
    swVtable.shaderSetUniformF = swShaderSetUniformF;
    swVtable.shaderSetUniformFArray = swShaderSetUniformFArray;
    swVtable.shaderSetUniformI = swShaderSetUniformI;
    swVtable.spriteGetTexture = swSpriteGetTexture;
    swVtable.surfaceGetTexture = swSurfaceGetTexture;
    swVtable.textureGetTexelWidth = swTextureGetTexelWidth;
    swVtable.textureGetTexelHeight = swTextureGetTexelHeight;
    swVtable.textureGetUVs = swTextureGetUVs;
    swVtable.textureSetStage = swTextureSetStage;
    swVtable.shaderIsCompiled = swShaderIsCompiled;
    swVtable.shadersSupported = swShadersSupported;
    swVtable.setMatrix = swSetMatrix;
    sw->base.drawColor = 0xFFFFFF;
    sw->base.drawAlpha = 1.0f;
    sw->base.drawFont = -1;
    sw->base.drawHalign = 0;
    sw->base.drawValign = 0;
    sw->base.circlePrecision = 24;
    sw->base.currentShader = -1;
    Matrix4f_identity(&sw->base.gmlMatrices[MATRIX_WORLD]);
    sw->blendEnable = true;
    sw->blendMode = bm_normal;
    sw->blendFactors.src = bm_src_alpha;
    sw->blendFactors.dst = bm_inv_src_alpha;
    sw->blendFactors.srcAlpha = bm_src_alpha;
    sw->blendFactors.dstAlpha = bm_inv_src_alpha;
    sw->alphaTestEnable = false;
    sw->alphaTestRef = 0;
    sw->colorWriteR = sw->colorWriteG = sw->colorWriteB = sw->colorWriteA = true;
    sw->fogEnable = false;
    sw->fogColor = 0;
    sw->base.currentShader = -1;
    return (Renderer*)sw;
}

#endif /* ENABLE_SW_RENDERER */
