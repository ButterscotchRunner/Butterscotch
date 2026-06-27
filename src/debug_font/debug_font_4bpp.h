// This is auto generated!
// Font: "JetBrains Mono ExtraBold"
// 4-bit-per-pixel alpha atlas. Two pixels per byte: low nibble = x even, high nibble = x odd.
#ifndef _BS_DEBUG_FONT_4BPP_H_
#define _BS_DEBUG_FONT_4BPP_H_

#include <stdint.h>
#include "debug_font.h"

#define DEBUGFONT_PIXELS_4BPP_BYTES (DEBUGFONT_ATLAS_W * DEBUGFONT_ATLAS_H / 2)
#define DEBUGFONT_CLUT_ENTRIES 16

// 32 KiB for 256x256.
extern const uint8_t debugFontPixels4bpp[DEBUGFONT_PIXELS_4BPP_BYTES];

// CLUT in PSMCT32 layout (PS2-native): word memory is 0xAABBGGRR, alpha 0..0x80 (0x80 = fully opaque on the GS).
// Index 0 = fully transparent; index 15 = opaque white. White RGB lets the renderer tint via per-vertex color.
extern const uint32_t debugFontClutPs2[DEBUGFONT_CLUT_ENTRIES];

// Same CLUT but with standard 0..0xFF alpha for desktop GL / PS3 (PSGL accepts 0..0xFF).
extern const uint32_t debugFontClutRgba8[DEBUGFONT_CLUT_ENTRIES];

#endif /* _BS_DEBUG_FONT_4BPP_H_ */
