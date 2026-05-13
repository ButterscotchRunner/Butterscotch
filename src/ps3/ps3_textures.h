#pragma once

#include <ps3gl.h>
#include <stdbool.h>
#include <stdint.h>

// Loader for the PS3 textures.bin bundle produced by ButterscotchPreprocessor.

bool PS3Textures_init(const char* texturesBinPath);
void PS3Textures_free();

// Total number of texture pages from the textures.bin file.
uint32_t PS3Textures_getPageCount();

// Reads a page's 8bpp index data from disk into a freshly allocated buffer.
// Caller MUST free *outPixels with free() after uploading to the GPU.
// Returns false if the page is missing/empty.
bool PS3Textures_loadPage(uint32_t pageId, int* outW, int* outH, uint8_t** outPixels);

// The shared CLUT atlas as a GL_RGBA texture. Bind on TEXUNIT1 when drawing.
// Returns 0 if not initialized.
GLuint PS3Textures_getClutTexture();

// Pre-normalized V coordinate (row center) for the CLUT belonging to a given
// TPAG. Returns -1.0f if the TPAG is unmapped (no pixels) or out of range.
float PS3Textures_getTpagPaletteV(int32_t tpagIndex);
