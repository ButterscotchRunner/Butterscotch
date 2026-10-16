#!/usr/bin/env python3
from PIL import Image, ImageFont, ImageDraw
import os

TTF_PATH = "src/default_font/JetBrainsMono-Medium.ttf"
OUT_C = "src/default_font/jetbrains_mono_medium_font.c"
OUT_H = "src/default_font/jetbrains_mono_medium_font.h"

FIRST_CP = 32
LAST_CP = 126
GLYPH_COUNT = LAST_CP - FIRST_CP + 1
ATLAS_W = 512
ATLAS_H = 512
PIXEL_SIZE = 12

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
    font = ImageFont.truetype(TTF_PATH, PIXEL_SIZE)

    # Get the glyph bbox
    bbox = font.getbbox(ch)
    w = max(1, bbox[2] - bbox[0])
    h = max(1, bbox[3] - bbox[1])

    # Avoid overflow
    if x + w + 1 >= ATLAS_W:
        x = 1
        y += row_h + 1
        row_h = 0

    # Create a temporary image for the glyph
    glyph = Image.new("L", (w + 2, h + 2), 0)
    d = ImageDraw.Draw(glyph)
    # PIL draws with baseline anchored at font metrics; you can adjust as needed
    d.text((1, 1), ch, font=font, fill=255)

    # Copy glyph into atlas
    atlas.paste(glyph.crop((0, 0, w + 2, h + 2)), (x, y))
    glyphs.append({
        "cp": cp,
        "x": x,
        "y": y,
        "w": w + 2,
        "h": h + 2,
        "xoffset": 0,
        "yoffset": 0,
        "xadvance": w + 1,
    })
    row_h = max(row_h, h + 2)
    x += w + 3

# Convert atlas to byte array
pixels = list(atlas.tobytes())

# Header
header = """#ifndef _BS_JETBRAINS_MONO_MEDIUM_FONT_H_
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
"""

# C file
c_lines = [
    "#include \"jetbrains_mono_medium_font.h\"",
    "",
    "const uint8_t jetbrainsMonoMediumPixels[JBMMED_ATLAS_W * JBMMED_ATLAS_H] = {",
]

for i in range(0, len(pixels), 16):
    chunk = pixels[i:i+16]
    c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

c_lines.append("};")
c_lines.append("")
c_lines.append("const JetBrainsMonoMediumGlyph jetbrainsMonoMediumGlyphs[JBMMED_GLYPH_COUNT] = {")

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