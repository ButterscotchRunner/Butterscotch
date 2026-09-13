#ifndef _BS_SDL_RENDERER_H_
#define _BS_SDL_RENDERER_H_

#include <SDL2/SDL.h>

#include "common.h"
#include "renderer.h"

Renderer* SDLRenderer_create(void);
Renderer* SDLRenderer_getCurrent(void);
void SDLRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color);
void SDLRenderer_presentCurrentFrame(SDL_Window* window);

#endif /* _BS_SDL_RENDERER_H_ */
