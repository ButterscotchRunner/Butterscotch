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

// Define if you want to decrease the quality of additional blend modes and
// only fully support bm_normal.  Automatically set if you enable SW_DITHERED_BLENDING.
//#define SW_BAD_BLEND_MODE_SUPPORT

// Completely exclude bm_subtract support.
//#define SW_NO_SUBTRACT_SUPPORT

// Amount of textures that can be loaded in at once.  If too many are loaded,
// start unloading.  Note that this isn't particularly effective towards memory
// optimization, and we should be looking into something else.
#define TEXTURE_LRU_LENGTH 64

// Amount of surfaces that can be created at the same time.
#define SURFACE_MAX_COUNT 64

// -======- USER CONFIG END -======-

// Force enable dithered blending if in 8bpp mode.
#if PIXEL_SIZE == 8

#ifndef SW_DITHERED_BLENDING
#define SW_DITHERED_BLENDING
#endif

#ifndef SW_NO_SUBTRACT_SUPPORT
#define SW_NO_SUBTRACT_SUPPORT
#endif

#endif // PIXEL_SIZE == 8

#endif // __SW_CONFIG_H
