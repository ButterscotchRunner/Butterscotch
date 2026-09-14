#ifndef __SW_CONFIG_H
#define __SW_CONFIG_H

// -======- USER CONFIG START -======-

// Change the bit-depth of the final image. Valid values are 8, 16, or 32.
#define PIXEL_SIZE 32
//#define PIXEL_SIZE 16
//#define PIXEL_SIZE 8

// Pixel formats for each mode:
// 32-bit: 0xAARRGGBB
// 16-bit: 0b0RRRRRGGGGGBBBBB
// 8-bit:  0bBBGGGRRR

// Define if you want dithered alpha blending and triangle color blending.
// It might be a good bit faster than doing blending the proper way.
//#define SW_DITHERED_BLENDING

// Define if you want tinting to be implemented inaccurately
//#define SW_INACCURATE_TINTING

// Define if you want to disable support for additional blend modes and only support bm_normal.
// Should be set if you enable SW_DITHERED_BLENDING.
//#define SW_NO_BLEND_MODE_SUPPORT

// -======- USER CONFIG END -======-

// Disable blend mode support forcefully if we're on 8-bpp color
// or if we requested dithered blending
#if PIXEL_SIZE == 8 || defined SW_DITHERED_BLENDING

#ifndef SW_NO_BLEND_MODE_SUPPORT
#define SW_NO_BLEND_MODE_SUPPORT
#endif // !SW_NO_BLEND_MODE_SUPPORT

#endif // SW_DITHERED_BLENDING

#endif // __SW_CONFIG_H
