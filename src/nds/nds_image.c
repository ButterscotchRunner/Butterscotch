#include "nds_image.h"

#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool nds_load_png_5551(const char* path, uint16_t** out_pixels, int* out_w, int* out_h)
{
	if (!path || !out_pixels || !out_w || !out_h)
		return false;

	int w = 0;
	int h = 0;
	int n = 0;
	unsigned char* rgba = stbi_load(path, &w, &h, &n, 4);
	if (!rgba || w <= 0 || h <= 0)
		return false;

	uint16_t* pixels = (uint16_t*)malloc((size_t)w * (size_t)h * sizeof(uint16_t));
	if (!pixels) {
		stbi_image_free(rgba);
		return false;
	}

	for (int i = 0; i < w * h; i++)
	{
		unsigned char r = rgba[(i * 4) + 0];
		unsigned char g = rgba[(i * 4) + 1];
		unsigned char b = rgba[(i * 4) + 2];
		unsigned char a = rgba[(i * 4) + 3];

		uint16_t rgb15 = (uint16_t)(((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10));
		if (a >= 128)
			rgb15 |= (1u << 15);
		pixels[i] = rgb15;
	}

	stbi_image_free(rgba);
	*out_pixels = pixels;
	*out_w = w;
	*out_h = h;
	return true;
}
