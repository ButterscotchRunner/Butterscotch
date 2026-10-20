#!/usr/bin/env python3
from PIL import Image, ImageFont, ImageDraw
import os

TTF_PATH = "src/default_font/JetBrainsMono-ExtraBold.ttf"
OUT_C = "src/default_font/jetbrains_mono_extrabold_font.c"
OUT_H = "src/default_font/jetbrains_mono_extrabold_font.h"

FONT_SCALE = 1.1
FIRST_CP = 32
LAST_CP = 126
GLYPH_COUNT = LAST_CP - FIRST_CP + 1
ATLAS_W = 512
ATLAS_H = 512
PIXEL_SIZE = 12 * FONT_SCALE

# Create a simple packing grid: one row with fixed-size glyph boxes.
# For production, use a real packer if you want tighter packing.
glyphs = []
atlas = Image.new("L", (ATLAS_W, ATLAS_H), 0)
draw = ImageDraw.Draw(atlas)

x = 1
y = 1
row_h = 0

for cp in range(FIRST_CP, LAST_CP + 1):
    ch = chr(cp)
    font = ImageFont.truetype(TTF_PATH, PIXEL_SIZE * 4)

    # Use the full glyph bbox so we keep the true top/bottom extents and baseline
    # offset. The raw mask bbox can trim off letters with descenders or small
    # vertical overshoot, which is what causes clipping in the runner.
    bbox = font.getbbox(ch)
    if bbox is None:
        bbox = (0, 0, 1, 1)

    w = max(1, bbox[2] - bbox[0])
    h = max(1, bbox[3] - bbox[1])
    left_pad = 1
    top_pad = 1
    yoffset = max(0, int(bbox[1]))

    if x + w + left_pad + 2 >= ATLAS_W:
        x = 1
        y += row_h + 1
        row_h = 0

    glyph = Image.new("L", (w + left_pad * 2, h + top_pad * 2), 0)
    d = ImageDraw.Draw(glyph)
    d.text((left_pad - bbox[0], top_pad - bbox[1]), ch, font=font, fill=255)

    atlas.paste(glyph.crop((0, 0, w + left_pad * 2, h + top_pad * 2)), (x, y))
    glyphs.append({
        "cp": cp,
        "x": x,
        "y": y,
        "w": w + left_pad * 2,
        "h": h + top_pad * 2,
        "xoffset": 0,
        "yoffset": yoffset,
        "xadvance": max(w + 1, int((bbox[2] - bbox[0]) * 1.2)),
    })
    row_h = max(row_h, h + top_pad * 2)
    x += max(w + left_pad * 2 + 2, glyphs[-1]["xadvance"] + 2)

# Convert atlas to byte array
pixels = list(atlas.tobytes())

# Header
header = """#ifndef _BS_JETBRAINS_MONO_EXTRABOLD_FONT_H_
#define _BS_JETBRAINS_MONO_EXTRABOLD_FONT_H_

#include <stdint.h>

#define DEFAULTFONT_ATLAS_W 512
#define DEFAULTFONT_ATLAS_H 512
#define DEFAULTFONT_FIRST_CP 32
#define DEFAULTFONT_LAST_CP 126
#define DEFAULTFONT_GLYPH_COUNT 95
#define DEFAULTFONT_FONT_SCALE 1.1f

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
    int16_t xoffset;
    int16_t yoffset;
    int16_t xadvance;
} JetBrainsMonoExtraBoldGlyph;

extern const uint8_t JetBrainsMonoExtraBoldPixels[DEFAULTFONT_ATLAS_W * DEFAULTFONT_ATLAS_H];
extern const JetBrainsMonoExtraBoldGlyph JetBrainsMonoExtraBoldGlyphs[DEFAULTFONT_GLYPH_COUNT];

#endif
"""

# C file
c_lines = [
    "#include \"jetbrains_mono_extrabold_font.h\"",
    "",
    "const uint8_t JetBrainsMonoExtraBoldPixels[DEFAULTFONT_ATLAS_W * DEFAULTFONT_ATLAS_H] = {",
]

for i in range(0, len(pixels), 16):
    chunk = pixels[i:i+16]
    c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

c_lines.append("};")
c_lines.append("")
c_lines.append("const JetBrainsMonoExtraBoldGlyph JetBrainsMonoExtraBoldGlyphs[DEFAULTFONT_GLYPH_COUNT] = {")

for g in glyphs:
    c_lines.append(
        f"    {{ {g['x']}, {g['y']}, {g['w']}, {g['h']}, {g['xoffset']}, {g['yoffset']}, {g['xadvance']} }},"
    )

c_lines.append("};")
c_lines.append("")

with open(OUT_H, "w", encoding="utf-8") as f:
    f.write(header)

with open(OUT_C, "w", encoding="utf-8") as f:
    f.write("\n".join(c_lines) + "\n")

print("Generated:", OUT_C, OUT_H)