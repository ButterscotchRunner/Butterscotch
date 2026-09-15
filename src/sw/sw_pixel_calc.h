#ifndef _SW_PIXEL_CALC_H
#define _SW_PIXEL_CALC_H

#include "sw_defines.h"
#include "sw_pixel_convert.h"

// Random number generator to be used for 8-bpp blending operations.
FORCE_INLINE int fastRandomIsh()
{
    static int rng = 0;
    rng += 1339;
    if (rng > 601000)
        rng = 0;
    return rng;
}

// Check if a pixel is opaque.
//
// Later, this should be changed to perform full alpha-blending
// (at least in 32-bit pixel mode)
FORCE_INLINE bool opaque(uintpixel_t color)
{
#if PIXEL_SIZE == 8
    return (color != PXL_TRANSPARENT);
#else
    return (color & TRANSPARENT_MASK) != 0;
#endif
}

// Multiplies a color value (`color`) by another color value (`tintColor`).
FORCE_INLINE uintpixel_t tint(uintpixel_t tintColor, uintpixel_t color)
{
#if PIXEL_SIZE == 8
    if (tintColor == 0xFF || tintColor == PXL_TRANSPARENT)
        return color;
#elif PIXEL_SIZE == 16
    if ((tintColor & 0x7FFF) == 0x7FFF)
        return color;
#else
    if ((tintColor & 0xFFFFFF) == 0xFFFFFF)
        return color;
#endif
    
#if PIXEL_SIZE == 8 || defined SW_INACCURATE_TINTING
    // fast but probably doesn't really work all that well
    return color & tintColor;
#elif PIXEL_SIZE == 32
    Pixel32ARGB x, y;
    
    x.l = color;
    y.l = tintColor;
    
    x.p.b = (int)x.p.b * y.p.b / 255;
    x.p.g = (int)x.p.g * y.p.g / 255;
    x.p.r = (int)x.p.r * y.p.r / 255;
    return x.l;
#elif PIXEL_SIZE == 16
    int tcb = tintColor & 0x1F;
    int tcg = (tintColor >> 5) & 0x1F;
    int tcr = (tintColor >> 10) & 0x1F;
    
    int cb = color & 0x1F;
    int cg = (color >> 5) & 0x1F;
    int cr = (color >> 10) & 0x1F;
    int ca = color & 0x8000;
    
    cb = (cb * tcb) / 32;
    cg = (cg * tcg) / 32;
    cr = (cr * tcr) / 32;
    return ca | cb | (cg << 5) | (cr << 10);
#endif
}

// Performs alpha blending on a pixel, with another pixel.
//
// NOTE: alpha is between 0 and 256, NOT between 0 and 255!
//
// TODO: This routine could use some optimization.  Obviously I tried my best, but clearly
// it's still true that too many calculations are being performed.
//
// NOTE: Obviously I could use SIMD here, but old computers didn't have SIMD, and the code
// runs fast enough on modern computers to not need to do SIMD.
//
// Another note: Wow, it' become a huge mess of ifdef's...
FORCE_INLINE
void alphaBlend(uintpixel_t* dcolor, uintpixel_t scolor, int blendmode, int srcalpha, int dstalpha)
{
#ifdef SW_DITHERED_BLENDING
    /* Dithered blending does not use the dstalpha member */
    (void) dstalpha;
#endif

    /* If we didn't disable subtract support and are in dithered blending mode */
#if defined SW_DITHERED_BLENDING
#ifdef SW_NO_SUBTRACT_SUPPORT

#ifndef SW_BAD_BLEND_MODE_SUPPORT
    if (UNLIKELY(blendmode == bm_subtract))
        return;
#endif // SW_NO_BLEND_MODE_SUPPORT

#else // SW_NO_SUBTRACT_SUPPORT
    if (UNLIKELY(blendmode == bm_subtract))
    {
    #if PIXEL_SIZE == 8
        srcalpha = scolor & 0x7;
        srcalpha = (srcalpha << 5) | (srcalpha << 2) | (srcalpha >> 1);
    #elif PIXEL_SIZE == 16
        srcalpha = scolor & 0x1F;
        srcalpha = (srcalpha << 3) | (srcalpha >> 2);
    #elif PIXEL_SIZE == 32
        srcalpha = scolor & 0xFF;
    #endif
    
        if (UNLIKELY(srcalpha < 5))
            return;
        if (UNLIKELY(srcalpha > 250))
            *dcolor = 0;
        
        if ((fastRandomIsh() & 0xFF) < srcalpha)
            *dcolor = 0;
        
        return;
    }
#endif // !SW_NO_SUBTRACT_SUPPORT
#endif // defined SW_DITHERED_BLENDING

#if PIXEL_SIZE == 32 || PIXEL_SIZE == 16

    /* Check extremely common cases in 32- and 16-bit modes */
#ifndef SW_BAD_BLEND_MODE_SUPPORT
    if (LIKELY(blendmode == bm_normal))
#endif
    {
        // it's so significant here we might as well fill in the whole color
        if (LIKELY(srcalpha > 253)) {
            *dcolor = scolor;
            return;
        }
        
        // it's so insignificant here nobody will notice if we just don't...
        if (UNLIKELY(srcalpha < 4))
            return;
    }

#endif // PIXEL_SIZE == 32 || PIXEL_SIZE == 16

    /* With dithered blending, randomly place the color, or don't */
#ifdef SW_DITHERED_BLENDING
    if (srcalpha < 240)
    {
        if ((fastRandomIsh() & 0xFF) >= srcalpha)
            return;
    }
    
#ifndef SW_BAD_BLEND_MODE_SUPPORT
    if (UNLIKELY(blendmode == bm_add))
    {
        *dcolor |= scolor;
        return;
    }
#endif

    *dcolor = scolor;

#else

    /* Extract pixel channels */
#if PIXEL_SIZE == 32
    Pixel32ARGB dc, sc;
    dc.l = *dcolor;
    sc.l = scolor;
    
    int scr = sc.p.r;
    int scg = sc.p.g;
    int scb = sc.p.b;
    int sca = sc.p.a;
    int dcr = dc.p.r;
    int dcg = dc.p.g;
    int dcb = dc.p.b;
    int dca = 0xFF;
#elif PIXEL_SIZE == 16
    int scb = scolor & 0x1F;
    int scg = (scolor >> 5) & 0x1F;
    int scr = (scolor >> 10) & 0x1F;
    int sca = (scolor & 0x8000) ? 255 : 0;

    uintpixel_t _dcolor = *dcolor;
    int dcb = _dcolor & 0x1F;
    int dcg = (_dcolor >> 5) & 0x1F;
    int dcr = (_dcolor >> 10) & 0x1F;
    int dca = 0xFF;
#endif // PIXEL_SIZE
    
    if (UNLIKELY(blendmode == bm_subtract))
    {
        dcr = (dcr * (255 - scr)) >> 8;
        dcg = (dcg * (255 - scg)) >> 8;
        dcb = (dcb * (255 - scb)) >> 8;
        dca = (dca * (255 - sca)) >> 8;
    }
    else
    {
        /* Perform the actual blending ops on them */
        dcr = (dcr * dstalpha + scr * srcalpha) >> 8;
        dcg = (dcg * dstalpha + scg * srcalpha) >> 8;
        dcb = (dcb * dstalpha + scb * srcalpha) >> 8;
    }
    
#ifndef SW_BAD_BLEND_MODE_SUPPORT
    /* Clamp them if needed */
    if (UNLIKELY(blendmode != bm_normal))
    {
        //clamp to 0
        dcr &= ((-dcr) >> 31);
        dcg &= ((-dcg) >> 31);
        dcb &= ((-dcb) >> 31);
    #if PIXEL_SIZE == 16
        if (dcr > 31) dcr = 31;
        if (dcg > 31) dcg = 31;
        if (dcb > 31) dcb = 31;
    #else
        //clamp to 255
        dcr |= ((signed char)(dcr >> 1) >> 7);
        dcg |= ((signed char)(dcg >> 1) >> 7);
        dcb |= ((signed char)(dcb >> 1) >> 7);
    #endif
    }
#endif // SW_BAD_BLEND_MODE_SUPPORT

    /* Then re-assemble the pixel. */
#if PIXEL_SIZE == 32
    dc.p.r = dcr;
    dc.p.g = dcg;
    dc.p.b = dcb;
    dc.p.a = dca;
    
    *dcolor = dc.l;
#elif PIXEL_SIZE == 16
    *dcolor = (dca ? 0x8000 : 0) | dcb | (dcg << 5) | (dcr << 10);
#endif

#endif // SW_DITHERED_BLENDING
}

// Calculates an internal "alpha" value from GML-provided "alpha" values.
FORCE_INLINE int swrIntAlpha(float alphaf)
{
    return (int)(alphaf * 256);
}

// Calculates the source alpha for a pixel based on the current blend mode.
FORCE_INLINE int swrCalcSrcAlpha(SWRenderer* swr, int alpha)
{
    // Here you would depend on swr->blendMode, but all
    // of them return the same value right now.
    (void) swr;
    return alpha;
}

// Calculates the destination alpha for a pixel based on the current blend mode.
FORCE_INLINE int swrCalcDstAlpha(SWRenderer* swr, int alpha)
{
#ifdef SW_DITHERED_BLENDING
    /* Dithered blending does not use the dstalpha member */
    (void) swr;
    (void) alpha;
    return 0;
#else
    switch (swr->blendMode)
    {
        default:
            return 256 - alpha;
        case bm_add:
            return 256;
        case bm_subtract:
            return 256;
    }
#endif
}

// Blends a pixel between three colors.
// frac means 0-65535 where 65535 means one.  And frac1 + frac2 + frac3 MUST be equal to 65535.
FORCE_INLINE uintpixel_t swrThreeWayBlend(uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, uint16_t frac1, uint16_t frac2, uint16_t frac3)
{
#if defined SW_DITHERED_BLENDING
    int rng = fastRandomIsh() & 0xFFFF;
	if (rng < frac1) return color1; else rng -= frac1;
	if (rng < frac2) return color2;
    (void) frac3;
	return color3;
#elif PIXEL_SIZE == 32
    Pixel32ARGB x1, x2, x3, out;
    x1.l = color1;
    x2.l = color2;
    x3.l = color3;
    out.p.r = (x1.p.r * frac1 + x2.p.r * frac2 + x3.p.r * frac3) >> 16;
    out.p.g = (x1.p.g * frac1 + x2.p.g * frac2 + x3.p.g * frac3) >> 16;
    out.p.b = (x1.p.b * frac1 + x2.p.b * frac2 + x3.p.b * frac3) >> 16;
    out.p.a = x1.p.a;
    return out.l;
#elif PIXEL_SIZE == 16
    int c1b = color1 & 0x1F, c1g = (color1 >> 5) & 0x1F, c1r = (color1 >> 10) & 0x1F;
    int c2b = color2 & 0x1F, c2g = (color2 >> 5) & 0x1F, c2r = (color2 >> 10) & 0x1F;
    int c3b = color3 & 0x1F, c3g = (color3 >> 5) & 0x1F, c3r = (color3 >> 10) & 0x1F;
    int ca = color1 & 0x8000;
    int cr = (c1r * frac1 + c2r * frac2 + c3r * frac3) >> 16;
    int cg = (c1g * frac1 + c2g * frac2 + c3g * frac3) >> 16;
    int cb = (c1b * frac1 + c2b * frac2 + c3b * frac3) >> 16;
    return ca | cb | (cg << 5) | (cr << 10);
#endif
}

#endif//_SW_PIXEL_CALC_H
