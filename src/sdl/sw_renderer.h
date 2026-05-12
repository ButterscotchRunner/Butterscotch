#pragma once

#include "renderer.h"

Renderer* SWRenderer_create(int windowWidth, int windowHeight);

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color);
