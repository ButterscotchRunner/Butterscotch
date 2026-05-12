#include <stdio.h>
#include <limits.h>
#include "sw_renderer.h"
#include "text_utils.h"
#include "image/image_decoder.h"

#define FLT_MAX 99999999999.0f

#define LIKELY(cond)   __builtin_expect(!!(cond), 1)
#define UNLIKELY(cond) __builtin_expect(!!(cond), 0)

#define UNUSED __attribute__ ((unused))
#define FORCE_INLINE static inline __attribute__((always_inline))

#define UNIMP() do { fprintf(stderr, "NYI %s\n", __func__); } while (0)
//#define UNIMP() do { } while (0)
#define UNIMP2() do { } while (0)

#ifndef M_PI
#define M_PI 3.1415926535897932384626
#endif

typedef struct
{
	uint32_t* buffer;
	uint16_t width, height;
}
SWTexture;

typedef struct
{
	Renderer base;
	
	// Window Properties
	uint16_t width;
	uint16_t height;
	// Framebuffer
	uint32_t* fb;
	uint16_t fbPitch;
	
	SWTexture** textures;
	size_t textureCount;
	
	bool viewActive;
	int viewX, viewY, viewW, viewH;
	int portX, portY, portW, portH;
}
SWRenderer;

void Runner_setNextFrame(uint32_t* framebuffer, int width, int height);

typedef union
{
	struct { uint8_t b, g, r, a; } p;
	uint32_t l;
}
Color;

FORCE_INLINE int swrMin(int a, int b) { return a < b ? a : b; }
FORCE_INLINE int swrMax(int a, int b) { return a > b ? a : b; }
FORCE_INLINE int swrAbs(int x) { return x < 0 ? -x : x; }

FORCE_INLINE uint32_t convertColor(uint32_t p)
{
	uint32_t np = p & 0xFF00FF00;
	np |= (p & 0xFF) << 16;
	np |= (p >> 16) & 0xFF;
	return np;
}

FORCE_INLINE bool opaque(uint32_t color)
{
	return (color & 0xFF000000) != 0;
}

FORCE_INLINE uint32_t tint(uint32_t tintColor, uint32_t color)
{
	Color x, y;
	
	if ((tintColor & 0xFFFFFF) == 0xFFFFFF)
		return color;
	
	x.l = color;
	y.l = tintColor;
	
	x.p.b = (int)x.p.b * y.p.b / 255;
	x.p.g = (int)x.p.g * y.p.g / 255;
	x.p.r = (int)x.p.r * y.p.r / 255;
	return x.l;
}

FORCE_INLINE void alphaBlend(uint32_t* dcolor, uint32_t scolor, float alphaf)
{
	// it's so insignificant here nobody will notice if we just don't...
	if (alphaf <= 0.05f)
		return;
	
	// it's so significant here we might as well fill in the whole color
	if (alphaf >= 0.98f)
	{
		*dcolor = scolor;
		return;
	}
	
	int alpha = (int)(alphaf * 256);
	int inval = 256 - alpha;
	
	Color dc, sc;
	dc.l = *dcolor;
	sc.l = scolor;
	
	dc.p.r = (dc.p.r * inval + sc.p.r * alpha) / 256;
	dc.p.g = (dc.p.g * inval + sc.p.g * alpha) / 256;
	dc.p.b = (dc.p.b * inval + sc.p.b * alpha) / 256;
	
	*dcolor = dc.l;
}

FORCE_INLINE bool swrMustRotate(float angleDeg)
{
	int angleDegInt = (int)(angleDeg * 4);
	angleDegInt %= 360*4;
	
	if (angleDegInt > 180*4)
		angleDegInt -= 360*4;
	
	return swrAbs(angleDegInt) < 1; // 0.25 degrees
}

FORCE_INLINE int swrFloor(float x)
{
	int i = (int) x;
	return i - (x < (float) i);
}

FORCE_INLINE int swrCeiling(float x)
{
	int i = (int) x;
	return i + (x > (float) i);
}

static SWTexture* createTexture(const uint8_t* srcBuffer, int width, int height)
{
	SWTexture* txt = safeCalloc(1, sizeof(SWTexture));
	txt->buffer = safeCalloc(width * height, sizeof(uint32_t));
	
	const uint32_t* rgbaSrc = (const uint32_t*) srcBuffer;

	size_t sz = width * height;
	for (size_t i = 0; i < sz; i++)
		txt->buffer[i] = convertColor(rgbaSrc[i]);
	
	txt->width = (uint16_t) width;
	txt->height = (uint16_t) height;
	
	return txt;
}

// Lazily decodes and uploads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to decode.
static bool swrEnsureTextureIsLoaded(SWRenderer* swr, uint32_t pageId)
{
	if (swr->textures[pageId])
		return true;

	DataWin* dw = swr->base.dataWin;
	Texture* txtr = &dw->txtr.textures[pageId];

	int w, h;
	bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
	uint8_t* pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
	if (pixels == nullptr) {
		fprintf(stderr, "swr: Failed to decode TXTR page %u\n", pageId);
		return false;
	}

	swr->textures[pageId] = createTexture(pixels, w, h);
	free(pixels);
	
	fprintf(stderr, "SWR: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
	return true;
}

static void SWRenderer_init(Renderer* renderer, DataWin* dataWin)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	
	renderer->dataWin = dataWin;
	
	//allocate frame buffer
	swr->fb = safeCalloc(swr->width * swr->height, sizeof(uint32_t));
	swr->fbPitch = swr->width;
	
	//allocate texture buffer
	swr->textureCount = dataWin->txtr.count;
	swr->textures = safeCalloc(swr->textureCount, sizeof(SWTexture*));
	
	fprintf(stderr, "SWRenderer initialized.\n");
}

static void SWRenderer_destroy(Renderer* renderer)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	
	(void) swr;
	
	fprintf(stderr, "SWRenderer destroyed.\n");
}

static void SWRenderer_beginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH)
{
	(void)renderer; (void)gameW; (void)gameH; (void)windowW; (void)windowH;
	UNIMP2();
}

static void SWRenderer_endFrame(Renderer* renderer)
{
	(void)renderer;
	UNIMP2();

	SWRenderer* swr = (SWRenderer*) renderer;
	Runner_setNextFrame(swr->fb, swr->width, swr->height);
}

static void SWRenderer_beginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH,
								 int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle)
{
	(void)renderer; (void)viewX; (void)viewY; (void)viewW; (void)viewH;
	(void)portX; (void)portY; (void)portW; (void)portH; (void)viewAngle;
	UNIMP2();
	
	SWRenderer* swr = (SWRenderer*) renderer;
	swr->viewActive = true;
	swr->viewX = viewX;
	swr->viewY = viewY;
	swr->viewW = viewW;
	swr->viewH = viewH;
	swr->portX = portX;
	swr->portY = portY;
	swr->portW = portW;
	swr->portH = portH;
}

static void SWRenderer_endView(Renderer* renderer)
{
	(void)renderer;
	UNIMP2();
	
	SWRenderer* swr = (SWRenderer*) renderer;
	swr->viewActive = false;
}

static void SWRenderer_beginGUI(Renderer* renderer, int32_t guiW, int32_t guiH,
								int32_t portX, int32_t portY, int32_t portW, int32_t portH)
{
	(void)renderer; (void)guiW; (void)guiH;
	(void)portX; (void)portY; (void)portW; (void)portH;
	UNIMP2();
}

static void SWRenderer_endGUI(Renderer* renderer)
{
	(void)renderer;
	UNIMP2();
}

static void swrTransformPosIfNeeded(SWRenderer* swr, int* dx, int* dy)
{
	if (!swr->viewActive) return;
	
	if (dx) { *dx -= swr->viewX; *dx = (int)((long) *dx * swr->width  / swr->viewW); }
	if (dy) { *dy -= swr->viewY; *dy = (int)((long) *dy * swr->height / swr->viewH); }
}

static void swrTransformSizeIfNeeded(SWRenderer* swr, int* dx, int* dy)
{
	if (!swr->viewActive || !swr->viewW || !swr->viewH) return;
	
	if (dx) *dx = (int)((long) *dx * swr->width  / swr->viewW);
	if (dy) *dy = (int)((long) *dy * swr->height / swr->viewH);
}

FORCE_INLINE void swrPlotPixel(Renderer* renderer, int x, int y, uint32_t color, float alpha)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	
	if (x < 0 || y < 0) return;
	if (x >= swr->width || y >= swr->height) return;
	
	alphaBlend(&swr->fb[y * swr->fbPitch + x], color, alpha);
}

static void swrDrawHLine(Renderer* renderer, int dx, int dy, int dw, uint32_t color, float alpha, bool xform)
{
	SWRenderer *swr = (SWRenderer*) renderer;
	if (xform) {
		swrTransformPosIfNeeded(swr, &dx, &dy);
		swrTransformSizeIfNeeded(swr, &dw, NULL);
	}
	
	if (dy < 0) return;
	if (dy >= swr->height) return;
	if (dx < 0) { dw += dx; dx = 0; }
	if (dx + dw >= swr->width) dw = swr->width - dx;
	if (dw <= 0) return;
	
	uint32_t *line = &swr->fb[dy * swr->fbPitch + dx];
	for (int i = 0; i < dw; i++)
		alphaBlend(&line[i], color, alpha);
}

static void swrDrawVLine(Renderer* renderer, int dx, int dy, int dh, uint32_t color, float alpha, bool xform)
{
	SWRenderer *swr = (SWRenderer*) renderer;
	if (xform) {
		swrTransformPosIfNeeded(swr, &dx, &dy);
		swrTransformSizeIfNeeded(swr, NULL, &dh);
	}
	
	if (dx < 0) return;
	if (dx >= swr->width) return;
	if (dy < 0) { dh += dy; dy = 0; }
	if (dy + dh >= swr->height) dh = swr->height - dy;
	if (dh <= 0) return;
	
	for (int i = 0; i < dh; i++)
	{
		uint32_t *line = &swr->fb[(dy + i) * swr->fbPitch + dx];
		alphaBlend(&line[0], color, alpha);
	}
}

static void swrDrawRectangle(Renderer* renderer, int x1, int y1, int x2, int y2, uint32_t color, float alpha)
{
	swrDrawHLine(renderer, x1, y1, (x2 - x1) + 1, color, alpha, true);
	swrDrawHLine(renderer, x1, y2, (x2 - x1) + 1, color, alpha, true);
	swrDrawVLine(renderer, x1, y1, (y2 - y1) + 1, color, alpha, true);
	swrDrawVLine(renderer, x2, y1, (y2 - y1) + 1, color, alpha, true);
}

static void swrDrawLine(Renderer* renderer, int x1, int y1, int x2, int y2, int width, uint32_t color, float alpha, bool xform)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	if (xform) {
		swrTransformPosIfNeeded(swr, &x1, &y1);
		swrTransformPosIfNeeded(swr, &x2, &y2);
		swrTransformSizeIfNeeded(swr, &width, NULL);
	}
	
	if (x1 == x2)
	{
		swrDrawVLine(renderer, x1, swrMin(y1, y2), swrAbs(y1 - y2), color, alpha, false);
		return;
	}
	if (y1 == y2)
	{
		swrDrawHLine(renderer, swrMin(x1, x2), y1, swrAbs(x1 - x2), color, alpha, false);
		return;
	}
	
	int dx = x2 - x1, dy = y2 - y1;
	int dx1 = swrAbs(dx), dy1 = swrAbs(dy), xe, ye, x, y;
	int px = 2 * dy1 - dx1, py = 2 * dx1 - dy1;
	
	if (dy1 <= dx1)
	{
		if (dx >= 0)
		{
			x = x1, y = y1, xe = x2;
		}
		else
		{
			x = x2, y = y2, xe = x1;
		}
		
		swrPlotPixel(renderer, x, y, color, alpha);
		
		for (int i = 0; x < xe; i++)
		{
			x++;
			if (px < 0)
			{
				px += 2 * dy1;
			}
			else
			{
				if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) y++; else y--;
				px += 2 * (dy1 - dx1);
			}
			
			swrPlotPixel(renderer, x, y, color, alpha);
		}
	}
	else
	{
		if (dy >= 0)
		{
			x = x1, y = y1, ye = y2;
		}
		else
		{
			x = x2, y = y2, ye = y1;
		}
		
		swrPlotPixel(renderer, x, y, color, alpha);
		
		for (int i = 0; y < ye; i++)
		{
			y++;
			if (py <= 0)
			{
				py += 2 * dx1;
			}
			else
			{
				if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) x++; else x--;
				py += 2 * (dx1 - dy1);
			}
			
			swrPlotPixel(renderer, x, y, color, alpha);
		}
	}
}

static void swrDrawSprite(
	Renderer* renderer, int dx, int dy, int dw, int dh,
	SWTexture* texture, int sx, int sy, int sw, int sh,
	uint32_t tintColor, float alpha
)
{
	SWRenderer *swr = (SWRenderer*) renderer;
	
	swrTransformPosIfNeeded(swr, &dx, &dy);
	swrTransformSizeIfNeeded(swr, &dw, &dh);
	
	tintColor = convertColor(tintColor);
	
	bool flipX = false, flipY = false;
	if (dw < 0) { dx += dw; dw = -dw; flipX = true; }
	if (dh < 0) { dy += dh; dh = -dh; flipY = true; }
	
	//basic out of bound checks
	if (dw == 0 || dh == 0) return;
	if (sw == 0) sw = 1;
	if (sh == 0) sh = 1;
	if (dx + dw <= 0) return;
	if (dy + dh <= 0) return;
	if (dx >= swr->width) return;
	if (dy >= swr->height) return;
	
	int odw = dw, odh = dh;
	int osw = sw, osh = sh;
	
	//adjust out of bounds checks
	int diffxl = 0, diffyl = 0, diffxu = 0, diffyu = 0;
	if (dx < 0) { diffxl = -dx; dx = 0; dw -= diffxl; }
	if (dy < 0) { diffyl = -dy; dy = 0; dh -= diffyl; }
	if (dx + dw > swr->width)  { diffxu = dx + dw - swr->width;  dw -= diffxu; }
	if (dy + dh > swr->height) { diffyu = dy + dh - swr->height; dh -= diffyu; }
	
	if (diffxl != 0 || diffyl != 0 || diffxu != 0 || diffyu != 0)
	{
		//adjust source coordinates too
		diffxl = (int)((long)diffxl * osw / odw);
		diffyl = (int)((long)diffyl * osh / odh);
		diffxu = (int)((long)(diffxu + 1) * osw / odw);
		diffyu = (int)((long)(diffyu + 1) * osh / odh);
		sx += diffxl;
		sy += diffyl;
		sw -= diffxl;
		sh -= diffyl;
		sw -= diffxu;
		sh -= diffyu;
		if (sw <= 0 || sh <= 0) return;
	}
	
	//clip the source coords into bounds too
	if (sx < 0) { sw += sx; sx = 0; }
	if (sy < 0) { sh += sy; sy = 0; }
	if (sx + sw >= texture->width)  { sw = texture->width  - sx; }
	if (sy + sh >= texture->height) { sh = texture->height - sy; }
	
	//okay, now we can finally get on with rendering
	
	int ixs = 0, oxs = 1, iys = 0, oys = 1;
	if (flipX) ixs = dw - 1, oxs = -1;
	if (flipY) iys = dh - 1, oys = -1;

	if (sw == dw)
	{
		for (int y = 0, ys = iys; y < dh; y++, ys += oys)
		{
			uint32_t* dstline;
			const uint32_t* srcline;
			dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
			if (dh == sh)
				srcline = &texture->buffer[(sy + ys) * texture->width + sx];
			else
				srcline = &texture->buffer[(sy + (int)((long)ys*osh/odh)) * texture->width + sx];
			
			for (int x = 0, xs = ixs; x < dw; x++, xs += oxs)
			{
				uint32_t pixel = srcline[xs];
				if (opaque(pixel))
					alphaBlend(&dstline[x], tint(tintColor, pixel), alpha);
			}
		}
	}
	else
	{
		for (int y = 0, ys = iys; y < dh; y++, ys += oys)
		{
			uint32_t* dstline;
			const uint32_t* srcline;
			dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
			if (dh == sh)
				srcline = &texture->buffer[(sy + ys) * texture->width + sx];
			else
				srcline = &texture->buffer[(sy + (int)((long)ys*osh/odh)) * texture->width + sx];
			
			for (int x = 0, xs = ixs; x < dw; x++, xs += oxs)
			{
				uint32_t pixel = srcline[(int)((long)xs*osw/odw)];
				if (opaque(pixel))
					alphaBlend(&dstline[x], tint(tintColor, pixel), alpha);
			}
		}
	}
}

static void swrDrawSpriteRotated(
	Renderer* renderer, int dx, int dy, int dw, int dh,
	SWTexture* texture, int sx, int sy, int sw, int sh,
	uint32_t tintColor, float alpha,
	float angleDeg,
	float pivotX,
	float pivotY
)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	float angleRad = -angleDeg * M_PI / 180.0f;
	
	swrTransformPosIfNeeded(swr, &dx, &dy);
	swrTransformSizeIfNeeded(swr, &dw, &dh);
	
	tintColor = convertColor(tintColor);
	
	bool flipX = false, flipY = false;
	if (dw < 0) { dw = -dw; dx -= dw; pivotX -= dw; flipX = true; }
	if (dh < 0) { dh = -dh; dy -= dh; pivotY -= dh; flipY = true; }
	
	float cosA = cosf(angleRad);
	float sinA = sinf(angleRad);
	
	float cnrx[4], cnry[4];
	cnrx[0] = cnrx[3] = dx;
	cnry[0] = cnry[1] = dy;
	cnrx[1] = cnrx[2] = dx + dw;
	cnry[2] = cnry[3] = dy + dh;
	
	float pxa = pivotX + dx;
	float pya = pivotY + dy;
	
	float minXf = FLT_MAX, minYf = FLT_MAX, maxXf = -FLT_MAX, maxYf = -FLT_MAX;
	for (int i = 0; i < 4; i++)
	{
		float cxi = cnrx[i] - pxa;
		float cyi = cnry[i] - pya;
		float rx = cosA * cxi - sinA * cyi + pxa;
		float ry = sinA * cxi + cosA * cyi + pya;
		if (minXf > rx) minXf = rx;
		if (maxXf < rx) maxXf = rx;
		if (minYf > ry) minYf = ry;
		if (maxYf < ry) maxYf = ry;
	}

	// minX, minY, maxX, maxY now represent an AABB of pixels we should loop over
	int minX = swrFloor(minXf);
	int minY = swrFloor(minYf);
	int maxX = swrCeiling(maxXf);
	int maxY = swrCeiling(maxYf);
	
	// basic out-of-bound checks
	if (maxX < 0) return;
	if (maxY < 0) return;
	if (minX >= swr->width) return;
	if (minY >= swr->height) return;
	
	// however, we'll need to clip it against out of bounds first
	int minXc = minX, minYc = minY, maxXc = maxX, maxYc = maxY;
	
	if (minXc < 0) minXc = 0;
	if (minYc < 0) minYc = 0;
	if (maxXc >= swr->width)  maxXc = swr->width;
	if (maxYc >= swr->height) maxYc = swr->height;
	
	// some final clip checks
	if (minXc >= maxXc || minYc >= maxYc) return;
	
	int sox = flipX ? sw - 1 : 0;
	int soy = flipY ? sh - 1 : 0;
	int six = flipX ? -1 : 1;
	int siy = flipY ? -1 : 1;
	
	for (int cy = minYc; cy < maxYc; cy++)
	{
		uint32_t *dstline = &swr->fb[cy * swr->fbPitch];
		for (int cx = minXc; cx < maxXc; cx++)
		{
			// we need to determine the texture-space coordinate of cx/cy
			float ox = (float) cx + 0.5f - pxa;
			float oy = (float) cy + 0.5f - pya;
			
			// "undo" the rotation
			float lx =  cosA * ox + sinA * oy;
			float ly = -sinA * ox + cosA * oy;
			
			// turn it into a texture-local coordinate
			lx += pxa - dx;
			ly += pya - dy;
			
			if (lx < 0 || ly < 0 || lx >= (float) dw || ly >= (float) dh) continue;
			
			lx = lx * sw / dw;
			ly = ly * sh / dh;
			
			int tx = (int)(sox + lx * six);
			int ty = (int)(soy + ly * siy);
			
			if (tx < 0) tx = 0;
			if (ty < 0) ty = 0;
			if (tx >= sw) tx = sw - 1;
			if (ty >= sh) ty = sh - 1;
			
			tx += sx;
			ty += sy;
			
			uint32_t src = texture->buffer[ty * texture->width + tx];
			
			if (opaque(src))
				alphaBlend(&dstline[cx], tint(tintColor, src), alpha);
		}
	}
}

static void SWRenderer_drawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y,
								  float originX, float originY, float xscale, float yscale,
								  float angleDeg, uint32_t color, float alpha)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	DataWin* dwin = renderer->dataWin;

	if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) return;

	TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
	int16_t pageId = tpag->texturePageId;
	if (0 > pageId || swr->textureCount <= (uint32_t) pageId) return;
	if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return;

	int sx = tpag->sourceX;
	int sy = tpag->sourceY;
	int sw = tpag->sourceWidth;
	int sh = tpag->sourceHeight;
	
	int dx = (int)(tpag->targetX - originX);
	int dy = (int)(tpag->targetY - originY);
	int dw = (int)(xscale * sw);
	int dh = (int)(yscale * sh);
	dx = (int)(dx * xscale);
	dy = (int)(dy * yscale);
	dx += (int) x;
	dy += (int) y;

	SWTexture* texture = swr->textures[pageId];
	
	if (UNLIKELY(!swrMustRotate(angleDeg)))
	{
		float pivotX = x - dx;
		float pivotY = y - dy;
		swrDrawSpriteRotated(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, pivotX, pivotY);
	}
	else
	{
		swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
	}
}

static void SWRenderer_drawSpritePart(Renderer* renderer, int32_t tpagIndex,
									  int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH,
									  float x, float y, float xscale, float yscale, float angleDeg,
									  float pivotX, float pivotY, uint32_t color, float alpha)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	DataWin* dwin = renderer->dataWin;
	
	if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) return;
	
	bool flipX = false, flipY = false;
	if (xscale < 0) flipX = true, xscale = -xscale;
	if (yscale < 0) flipY = true, yscale = -yscale;

	TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
	int16_t pageId = tpag->texturePageId;
	if (0 > pageId || swr->textureCount <= (uint32_t) pageId) return;
	if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return;
	
	int sx = tpag->sourceX + srcOffX;
	int sy = tpag->sourceY + srcOffY;
	int sw = srcW;
	int sh = srcH;
	
	int dx = (int)x;
	int dy = (int)y;
	int dw = (int)(xscale * sw);
	int dh = (int)(yscale * sh);
	if (flipX) dx -= dw;
	if (flipY) dy -= dh;
	
	SWTexture* texture = swr->textures[pageId];
	
	if (UNLIKELY(!swrMustRotate(angleDeg)))
	{
		swrDrawSpriteRotated(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, pivotX * dw, pivotY * dh);
	}
	else
	{
		swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
	}
}

static void SWRenderer_drawSpritePos(Renderer* renderer, int32_t tpagIndex,
									 float x1, float y1, float x2, float y2,
									 float x3, float y3, float x4, float y4, float alpha)
{
	(void)renderer; (void)tpagIndex;
	(void)x1; (void)y1; (void)x2; (void)y2;
	(void)x3; (void)y3; (void)x4; (void)y4; (void)alpha;
	UNIMP();
}

static void SWRenderer_drawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2,
									 uint32_t color, float alpha, bool outline)
{
	(void)alpha;
	color = convertColor(color);
	
	SWRenderer* swr = (SWRenderer*) renderer;
	
	if (outline)
	{
		swrDrawRectangle(renderer, (int) x1, (int) y1, (int) x2, (int) y2, color, alpha);
	}
	else
	{
		int x1i = (int)x1, x2i = (int)x2, y1i = (int)y1, y2i = (int)y2;
		int xd = x2i - x1i;
		int yd = y2i - y1i;
		if (xd <= 0 || yd <= 0) return;
		swrTransformPosIfNeeded(swr, &x1i, &y1i);
		swrTransformSizeIfNeeded(swr, &xd, &yd);
		
		for (int y = 0; y <= yd; y++) {
			swrDrawHLine(renderer, x1i, y1i + y, xd, color, alpha, false);
		}
	}
}

static void SWRenderer_drawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2,
										  uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4,
										  float alpha, bool outline)
{
	(void)renderer; (void)x1; (void)y1; (void)x2; (void)y2;
	(void)color1; (void)color2; (void)color3; (void)color4; (void)alpha; (void)outline;
	UNIMP();
}

static void SWRenderer_drawLine(Renderer* renderer, float x1, float y1, float x2, float y2,
								float width, uint32_t color, float alpha)
{
	(void)renderer; (void)x1; (void)y1; (void)x2; (void)y2;
	(void)width; (void)color; (void)alpha;
	
	swrDrawLine(renderer, (int)x1, (int)y1, (int)x2, (int)y2, (int)width, color, alpha, true);
}

static void SWRenderer_drawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2,
									float x3, float y3, bool outline)
{
	(void)renderer; (void)x1; (void)y1; (void)x2; (void)y2; (void)x3; (void)y3; (void)outline;
	
	swrDrawLine(renderer, (int) x1, (int) y1, (int) x2, (int) y2, 1, renderer->drawColor, renderer->drawAlpha, true);
	swrDrawLine(renderer, (int) x1, (int) y1, (int) x3, (int) y3, 1, renderer->drawColor, renderer->drawAlpha, true);
	swrDrawLine(renderer, (int) x2, (int) y2, (int) x3, (int) y3, 1, renderer->drawColor, renderer->drawAlpha, true);
}

static void SWRenderer_drawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2,
									 float width, uint32_t color1, uint32_t color2, float alpha)
{
	(void)renderer; (void)x1; (void)y1; (void)x2; (void)y2;
	(void)width; (void)color1; (void)color2; (void)alpha;
	UNIMP();
}

typedef struct
{
	Font* font;
	TexturePageItem* fontTpag; // single TPAG for regular fonts (NULL for sprite fonts)
	int fontTpagIndex;
	int fontPageId;
	Sprite* spriteFontSprite; // source sprite for sprite fonts (NULL for regular fonts)
} SwrFontState;

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
		if (0 > pageId || (uint32_t) pageId >= swr->textureCount) return false;
		if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return false;
		
		state->fontPageId = pageId;
	}
	
	return true;
}

static bool swrResolveGlyph(
	SWRenderer* swr, DataWin* dw, SwrFontState* state, FontGlyph* glyph, int cursorX, int cursorY,
	int* tpagIndex, int* pageId, int* sx, int* sy, int* sw, int* sh, int* dx, int* dy
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
		if (0 > pid || (uint32_t) pid >= swr->textureCount) return false;
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

static void swrDrawText(SWRenderer* swr, const char* text, float x, float y, float xscale, float yscale, UNUSED float angleDeg, int32_t color, UNUSED float alpha)
{
	Renderer* renderer = &swr->base;
	DataWin* dwin = renderer->dataWin;

	int32_t fontIndex = renderer->drawFont;
	if (0 > fontIndex || dwin->font.count <= (uint32_t) fontIndex) return;

	Font* font = &dwin->font.fonts[fontIndex];
	
	SwrFontState fontState;
	if (!swrResolveFontState(swr, dwin, font, &fontState)) return;
	
	// TODO: do we need to mirror the way the text scrolls too?!
	
	int textLen = (int) strlen(text);
	int lineCount = TextUtils_countLines(text, textLen);
	float lineStride = TextUtils_lineStride(font);

	// Vertical alignment offset
	float totalHeight = (float) lineCount * lineStride;
	float valignOffset = 0;
	if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
	else if (renderer->drawValign == 2) valignOffset = -totalHeight;

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
					int sx, sy, sw, sh, dx, dy, dw, dh;
					if (swrResolveGlyph(swr, dwin, &fontState, glyph, (int) cursorX, (int) cursorY,
							&fontTpagIndex, &pageId, &sx, &sy, &sw, &sh, &dx, &dy))
					{
						dx = (int)(xscale * dx) + (int) x;
						dy = (int)(xscale * dy) + (int) y;
						dw = (int)(xscale * glyph->sourceWidth);
						dh = (int)(yscale * glyph->sourceHeight);
						
						SWTexture* texture = swr->textures[pageId];
						swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
						
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

static void SWRenderer_drawText(Renderer* renderer, const char* text, float x, float y,
								float xscale, float yscale, float angleDeg)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	swrDrawText(swr, text, x, y, xscale, yscale, angleDeg, renderer->drawColor, renderer->drawAlpha);
}

static void SWRenderer_drawTextColor(Renderer* renderer, const char* text, float x, float y,
									 float xscale, float yscale, float angleDeg,
									 int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha)
{
	(void)renderer; (void)text; (void)x; (void)y;
	(void)xscale; (void)yscale; (void)angleDeg;
	(void)c1; (void)c2; (void)c3; (void)c4; (void)alpha;
	UNIMP();
}

static void SWRenderer_flush(Renderer* renderer)
{
	(void)renderer;
	UNIMP();
}

static void SWRenderer_clearScreen(Renderer* renderer, uint32_t color, float alpha)
{
	(void)renderer; (void)color; (void)alpha;
	UNIMP();
}

static int32_t SWRenderer_createSpriteFromSurface(Renderer* renderer, int32_t surfaceID,
												   int32_t x, int32_t y, int32_t w, int32_t h,
												   bool removeback, bool smooth,
												   int32_t xorig, int32_t yorig)
{
	(void)renderer; (void)surfaceID; (void)x; (void)y; (void)w; (void)h;
	(void)removeback; (void)smooth; (void)xorig; (void)yorig;
	UNIMP();
	return 0;
}

static void SWRenderer_deleteSprite(Renderer* renderer, int32_t spriteIndex)
{
	UNIMP();
	(void)renderer; (void)spriteIndex;
}

static void SWRenderer_gpuSetBlendMode(Renderer* renderer, int32_t mode)
{
	UNIMP();
	(void)renderer; (void)mode;
}

static void SWRenderer_gpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor)
{
	UNIMP();
	(void)renderer; (void)sfactor; (void)dfactor;
}

static void SWRenderer_gpuSetBlendEnable(Renderer* renderer, bool enable)
{
	UNIMP();
	(void)renderer; (void)enable;
}

static void SWRenderer_gpuSetAlphaTestEnable(Renderer* renderer, bool enable)
{
	UNIMP();
	(void)renderer; (void)enable;
}

static void SWRenderer_gpuSetAlphaTestRef(Renderer* renderer, uint8_t ref)
{
	UNIMP();
	(void)renderer; (void)ref;
}

static void SWRenderer_gpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha)
{
	UNIMP();
	(void)renderer; (void)red; (void)green; (void)blue; (void)alpha;
}

static bool SWRenderer_gpuGetBlendEnable(Renderer* renderer)
{
	UNIMP();
	(void)renderer;
	return false;
}

static void SWRenderer_gpuSetFog(Renderer* renderer, bool enable, uint32_t color)
{
	UNIMP();
	(void)renderer; (void)enable; (void)color;
}

static void SWRenderer_drawTile(Renderer* renderer, RoomTile* tile, float offsetX, float offsetY)
{
	UNIMP();
	(void)renderer; (void)tile; (void)offsetX; (void)offsetY;
}

static void SWRenderer_drawTiled(Renderer* renderer, int32_t tpagIndex,
								 float originX, float originY, float x, float y,
								 float xscale, float yscale, bool tileX, bool tileY,
								 float roomW, float roomH, uint32_t color, float alpha)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	DataWin* dwin = renderer->dataWin;

	if (0 > tpagIndex || dwin->tpag.count <= (uint32_t) tpagIndex) return;

	TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
	int16_t pageId = tpag->texturePageId;
	if (0 > pageId || swr->textureCount <= (uint32_t) pageId) return;
	if (!swrEnsureTextureIsLoaded(swr, (uint32_t) pageId)) return;

	float axScale = fabsf(xscale);
	float ayScale = fabsf(yscale);
	float tileW = (float) tpag->boundingWidth * axScale;
	float tileH = (float) tpag->boundingHeight * ayScale;
	if (0 >= tileW || 0 >= tileH) return;

	float startX, endX, startY, endY;
	if (tileX) {
		startX = fmodf(x - originX * axScale, tileW);
		if (startX > 0) startX -= tileW;
		endX = roomW;
	} else {
		startX = x - originX * axScale;
		endX = startX + tileW;
	}
	if (tileY) {
		startY = fmodf(y - originY * ayScale, tileH);
		if (startY > 0) startY -= tileH;
		endY = roomH;
	} else {
		startY = y - originY * ayScale;
		endY = startY + tileH;
	}
	
	int sx = tpag->sourceX;
	int sy = tpag->sourceY;
	int sw = tpag->sourceWidth;
	int sh = tpag->sourceHeight;

	int localX0 = tpag->targetX - originX;
	int localY0 = tpag->targetY - originY;
	int localX1 = localX0 + tpag->sourceWidth;
	int localY1 = localY0 + tpag->sourceHeight;
	int sx0 = xscale * localX0;
	int sy0 = yscale * localY0;
	int sx1 = xscale * localX1;
	int sy1 = yscale * localY1;

	for (int dy = startY; endY > dy; dy += tileH) {
		int cy = dy + (int)(originY * ayScale);
		int vy0 = cy + sy0;
		int vy1 = cy + sy1;
		int dh = vy1 - vy0;

		for (int dx = startX; endX > dx; dx += tileW) {
			int cx = dx + (int)(originX * axScale);
			int vx0 = cx + sx0;
			int vx1 = cx + sx1;
			int dw = vx1 - vx0;

			swrDrawSprite(renderer, vx0, vy0, dw, dh, swr->textures[pageId], sx, sy, sw, sh, color, alpha);
		}
	}
}

static int32_t SWRenderer_createSurface(Renderer* renderer, int32_t width, int32_t height)
{
	UNIMP();
	(void)renderer; (void)width; (void)height;
	return 0;
}

static bool SWRenderer_surfaceExists(Renderer* renderer, int32_t surfaceID)
{
	UNIMP();
	(void)renderer; (void)surfaceID;
	return false;
}

static bool SWRenderer_setSurfaceTarget(Renderer* renderer, int32_t surfaceID)
{
	UNIMP();
	(void)renderer; (void)surfaceID;
	return false;
}

static bool SWRenderer_resetSurfaceTarget(Renderer* renderer)
{
	UNIMP();
	(void)renderer;
	return false;
}

static float SWRenderer_getSurfaceWidth(Renderer* renderer, int32_t surfaceID)
{
	UNIMP();
	(void)renderer; (void)surfaceID;
	return 0.0f;
}

static float SWRenderer_getSurfaceHeight(Renderer* renderer, int32_t surfaceID)
{
	UNIMP();
	(void)renderer; (void)surfaceID;
	return 0.0f;
}

static void SWRenderer_drawSurface(Renderer* renderer, int32_t surfaceID, float x, float y,
								   float xscale, float yscale, float angleDeg,
								   uint32_t color, float alpha)
{
	UNIMP();
	(void)renderer; (void)surfaceID; (void)x; (void)y;
	(void)xscale; (void)yscale; (void)angleDeg; (void)color; (void)alpha;
}

static void SWRenderer_drawSurfacePart(Renderer* renderer, int32_t surfaceID,
									   int32_t x, int32_t y, int32_t left, int32_t top,
									   int32_t width, int32_t height,
									   float xscale, float yscale, uint32_t color, float alpha)
{
	UNIMP();
	(void)renderer; (void)surfaceID; (void)x; (void)y; (void)left; (void)top;
	(void)width; (void)height; (void)xscale; (void)yscale; (void)color; (void)alpha;
}

static void SWRenderer_drawSurfaceStretched(Renderer* renderer, int32_t surfaceID,
											float x, float y, float width, float height)
{
	UNIMP();
	(void)renderer; (void)surfaceID; (void)x; (void)y; (void)width; (void)height;
}

static void SWRenderer_surfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height)
{
	UNIMP();
	(void)renderer; (void)surfaceID; (void)width; (void)height;
}

static void SWRenderer_surfaceFree(Renderer* renderer, int32_t surfaceID)
{
	UNIMP();
	(void)renderer; (void)surfaceID;
}

static void SWRenderer_surfaceCopy(Renderer* renderer,
								   int32_t DestSurfaceID, int32_t DestX, int32_t DestY,
								   int32_t SrcSurfaceID, int32_t SrcX, int32_t SrcY,
								   int32_t SrcW, int32_t SrcH, bool part)
{
	UNIMP();
	(void)renderer;
	(void)DestSurfaceID; (void)DestX; (void)DestY;
	(void)SrcSurfaceID; (void)SrcX; (void)SrcY;
	(void)SrcW; (void)SrcH; (void)part;
}

static bool SWRenderer_surfaceGetPixels(Renderer* renderer, int32_t surfaceID, uint8_t* outRGBA)
{
	UNIMP();
	(void)renderer; (void)surfaceID; (void)outRGBA;
	return false;
}

static void SWRenderer_drawTiledPart(Renderer* renderer, int32_t tpagIndex,
									 int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH,
									 float dstX, float dstY, float dstW, float dstH,
									 uint32_t color, float alpha)
{
	UNIMP();
	(void)renderer; (void)tpagIndex;
	(void)srcX; (void)srcY; (void)srcW; (void)srcH;
	(void)dstX; (void)dstY; (void)dstW; (void)dstH;
	(void)color; (void)alpha;
}

static RendererVtable swrVtable =
{
	.init                    = SWRenderer_init,
	.destroy                 = SWRenderer_destroy,
	.beginFrame              = SWRenderer_beginFrame,
	.endFrame                = SWRenderer_endFrame,
	.beginView               = SWRenderer_beginView,
	.endView                 = SWRenderer_endView,
	.beginGUI                = SWRenderer_beginGUI,
	.endGUI                  = SWRenderer_endGUI,
	.drawSprite              = SWRenderer_drawSprite,
	.drawSpritePart          = SWRenderer_drawSpritePart,
	.drawSpritePos           = SWRenderer_drawSpritePos,
	.drawRectangle           = SWRenderer_drawRectangle,
	.drawRectangleColor      = SWRenderer_drawRectangleColor,
	.drawLine                = SWRenderer_drawLine,
	.drawTriangle            = SWRenderer_drawTriangle,
	.drawLineColor           = SWRenderer_drawLineColor,
	.drawText                = SWRenderer_drawText,
	.drawTextColor           = SWRenderer_drawTextColor,
	.flush                   = SWRenderer_flush,
	.clearScreen             = SWRenderer_clearScreen,
	.createSpriteFromSurface = SWRenderer_createSpriteFromSurface,
	.deleteSprite            = SWRenderer_deleteSprite,
	.gpuSetBlendMode         = SWRenderer_gpuSetBlendMode,
	.gpuSetBlendModeExt      = SWRenderer_gpuSetBlendModeExt,
	.gpuSetBlendEnable       = SWRenderer_gpuSetBlendEnable,
	.gpuSetAlphaTestEnable   = SWRenderer_gpuSetAlphaTestEnable,
	.gpuSetAlphaTestRef      = SWRenderer_gpuSetAlphaTestRef,
	.gpuSetColorWriteEnable  = SWRenderer_gpuSetColorWriteEnable,
	.gpuGetBlendEnable       = SWRenderer_gpuGetBlendEnable,
	.gpuSetFog               = SWRenderer_gpuSetFog,
	.drawTile                = NULL,
	.drawTiled               = SWRenderer_drawTiled,
	.createSurface           = SWRenderer_createSurface,
	.surfaceExists           = SWRenderer_surfaceExists,
	.setSurfaceTarget        = SWRenderer_setSurfaceTarget,
	.resetSurfaceTarget      = SWRenderer_resetSurfaceTarget,
	.getSurfaceWidth         = SWRenderer_getSurfaceWidth,
	.getSurfaceHeight        = SWRenderer_getSurfaceHeight,
	.drawSurface             = SWRenderer_drawSurface,
	.drawSurfacePart         = SWRenderer_drawSurfacePart,
	.drawSurfaceStretched    = SWRenderer_drawSurfaceStretched,
	.surfaceResize           = SWRenderer_surfaceResize,
	.surfaceFree             = SWRenderer_surfaceFree,
	.surfaceCopy             = SWRenderer_surfaceCopy,
	.surfaceGetPixels        = SWRenderer_surfaceGetPixels,
	.drawTiledPart           = SWRenderer_drawTiledPart,
};

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color)
{
	SWRenderer* swr = (SWRenderer*) renderer;
	
	size_t fbSize = swr->fbPitch;
	fbSize *= swr->height;
	for (size_t i = 0; i < fbSize; i++)
	{
		swr->fb[i] = color;
	}
}

Renderer* SWRenderer_create(int windowWidth, int windowHeight)
{
	SWRenderer* swr = safeCalloc(1, sizeof(SWRenderer));
	swr->base.vtable = &swrVtable;
	swr->base.drawColor = 0xFFFFFF;
	swr->base.drawAlpha = 1.0f;
	swr->base.drawFont = -1;
	swr->base.drawHalign = 0;
	swr->base.drawValign = 0;
	swr->base.circlePrecision = 24;
	
	swr->width = windowWidth;
	swr->height = windowHeight;

	return (Renderer*) swr;
}
