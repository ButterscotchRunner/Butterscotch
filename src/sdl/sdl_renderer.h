#ifndef _BS_SDL_RENDERER_H_
#define _BS_SDL_RENDERER_H_

#include <SDL2/SDL.h>

#include "common.h"
#include "renderer.h"

typedef enum {
    SDL_RENDERER_MODE_SOFTWARE = 0,
    SDL_RENDERER_MODE_HARDWARE = 1,
} SDLRendererMode;

Renderer* SDLRenderer_create(void);
Renderer* SDLRenderer_createSoftware(void);
Renderer* SDLRenderer_createHardware(void);
Renderer* SDLRenderer_getCurrent(void);
bool SDLRenderer_isHardwareAccelerated(const Renderer* renderer);
void SDLRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color);
void SDLRenderer_presentCurrentFrame(SDL_Window* window);

#endif /* _BS_SDL_RENDERER_H_ */
