#include <string.h>
#include <stdio.h>

#include <SDL/SDL_events.h>
#include <SDL/SDL.h>
#include <SDL/SDL_video.h>

#include "common.h"
#include "input_recording.h"
#include "desktop/platformdefs.h"

static Runner *g_runner;
static int32_t fbWidth, fbHeight;
static SDL_Surface* scr;

static void platformSetWindowTitle(const char* title) {
    char windowTitle[256];
    snprintf(windowTitle, sizeof(windowTitle), "Butterscotch - %s", title);
    SDL_WM_SetCaption(windowTitle, NULL);
}

bool platformGetWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH) return false;
    *outW = fbWidth;
    *outH = fbHeight;
    return true;
}

static void platformSetWindowSize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    fbWidth = width;
    fbHeight = height;
    scr = SDL_SetVideoMode(width, height, 0, (gfx == SOFTWARE ? 0 : SDL_OPENGL) | SDL_RESIZABLE);
}

static bool platformGetWindowFocus(void) {
    return SDL_GetAppState() & SDL_APPINPUTFOCUS;
}

bool platformInit(int reqW, int reqH, const char *title, bool headless) {
    if (headless && gfx != SOFTWARE) {
        fprintf(stderr, "Headless mode on SDL requires the software renderer!\n");
        return false;
    }

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)) {
        fprintf(stderr, "Failed to initialize SDL\n");
        return false;
    }

    fbWidth = reqW;
    fbHeight = reqH;
    if(!headless) {
        if (gfx == LEGACY_GL || gfx == MODERN_GL)
            SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 0); // disable vsync
        scr = SDL_SetVideoMode(fbWidth, fbHeight, 0, (gfx == SOFTWARE ? 0 : SDL_OPENGL) | SDL_RESIZABLE);
        if (!scr && gfx == SOFTWARE) {
            SDL_Rect** modes = SDL_ListModes(NULL, SDL_FULLSCREEN);
            if (modes && modes != (SDL_Rect**) -1 && modes[0]) {
                fprintf(stderr, "Warning: %dx%d unavailable, falling back to %dx%d: %s\n",
                        reqW, reqH, modes[0]->w, modes[0]->h, SDL_GetError());
                scr = SDL_SetVideoMode(modes[0]->w, modes[0]->h, 0, 0);
                fbWidth = modes[0]->w;
                fbHeight = modes[0]->h;
            }
        }
        if (!scr) {
            fprintf(stderr, "Fatal: Could not set any video mode: %s\n", SDL_GetError());
            return false;
        }
    }

    SDL_EnableKeyRepeat(0, 0);

    return true;
}

void platformExit(void) {
    SDL_Quit();
}

void platformInitFunctions(Runner *runner) {
    g_runner = runner;
    runner->setWindowTitle = platformSetWindowTitle;
    runner->getWindowSize = platformGetWindowSize;
    runner->setWindowSize = platformSetWindowSize;
    runner->windowHasFocus = platformGetWindowFocus;
}

#ifdef ENABLE_SW_RENDERER

static SDL_Surface* nextFb = NULL;

void Runner_setNextFrame(uint32_t* framebuffer, int width, int height) {
    if (nextFb) {
        SDL_FreeSurface(nextFb);
        nextFb = NULL;
    }

    nextFb = SDL_CreateRGBSurfaceFrom(
        framebuffer,
        width,
        height,
        32,
        width * 4,
        0x00ff0000, // Rmask
        0x0000ff00, // Gmask
        0x000000ff, // Bmask
        0x00000000  // Amask
    );
}

#endif

void platformSwapBuffers(void) {
#ifdef ENABLE_SW_RENDERER
    if(gfx == SOFTWARE) {
        SDL_BlitSurface(nextFb, NULL, scr, NULL);
        SDL_Flip(scr);
    }
#endif
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
    if (gfx == LEGACY_GL || gfx == MODERN_GL)
        SDL_GL_SwapBuffers();
#endif
}

#if defined(ENABLE_MODERN_GL) || defined(ENABLE_LEGACY_GL)

void *platformGetProcAddress(const char *name) {
    return SDL_GL_GetProcAddress(name);
}

#endif

double platformGetTime(void) {
    return (double)SDL_GetTicks() / 1000.0;
}

static int32_t SDLKeyToGml(int sdlkey) {
    // Letters and numbers are the same as GML
    if (sdlkey >= 'a' && sdlkey <= 'z') return toupper(sdlkey);
    if (sdlkey >= '0' && sdlkey <= '9') return sdlkey;
    // Special keys need mapping
    switch (sdlkey) {
        case SDLK_ESCAPE:    return VK_ESCAPE;
        case SDLK_RETURN:    return VK_ENTER;
        case SDLK_TAB:       return VK_TAB;
        case SDLK_BACKSPACE: return VK_BACKSPACE;
        case SDLK_SPACE:     return VK_SPACE;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:    return VK_SHIFT;
        case SDLK_LCTRL:
        case SDLK_RCTRL:     return VK_CONTROL;
        case SDLK_LALT:
        case SDLK_RALT:      return VK_ALT;
        case SDLK_UP:        return VK_UP;
        case SDLK_DOWN:      return VK_DOWN;
        case SDLK_LEFT:      return VK_LEFT;
        case SDLK_RIGHT:     return VK_RIGHT;
        case SDLK_F1:        return VK_F1;
        case SDLK_F2:        return VK_F2;
        case SDLK_F3:        return VK_F3;
        case SDLK_F4:        return VK_F4;
        case SDLK_F5:        return VK_F5;
        case SDLK_F6:        return VK_F6;
        case SDLK_F7:        return VK_F7;
        case SDLK_F8:        return VK_F8;
        case SDLK_F9:        return VK_F9;
        case SDLK_F10:       return VK_F10;
        case SDLK_F11:       return VK_F11;
        case SDLK_F12:       return VK_F12;
        case SDLK_INSERT:    return VK_INSERT;
        case SDLK_DELETE:    return VK_DELETE;
        case SDLK_HOME:      return VK_HOME;
        case SDLK_END:       return VK_END;
        case SDLK_PAGEUP:    return VK_PAGEUP;
        case SDLK_PAGEDOWN:  return VK_PAGEDOWN;
        default:             return -1; // Unknown
    }
}

bool platformHandleEvents(void) {
    bool should_exit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch(e.type) {
            case SDL_KEYDOWN:
                // During playback, suppress real keyboard input
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                RunnerKeyboard_onKeyDown(g_runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                if (e.key.keysym.unicode != 0)
                    RunnerKeyboard_onCharacter(g_runner->keyboard, e.key.keysym.unicode);
                break;
            case SDL_KEYUP:
                // During playback, suppress real keyboard input
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                RunnerKeyboard_onKeyUp(g_runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                break;
            case SDL_VIDEORESIZE:
                fbWidth = e.resize.w;
                fbHeight = e.resize.h;
                scr = SDL_SetVideoMode(fbWidth, fbHeight, 0, (gfx == SOFTWARE ? 0 : SDL_OPENGL) | SDL_RESIZABLE);
                break;
            case SDL_QUIT:
                should_exit = true;
                break;
            default:
                break;
        }
    }

    return should_exit;
}

void platformSleepUntil(double time) {
    double remaining = time - platformGetTime();
    if (remaining > 0.002)
        SDL_Delay((Uint32)((remaining - 0.001) * 1000));

    while (platformGetTime() < time) {
        // Spin-wait for the remaining sub-millisecond
    }
}

void platformGamepad_poll(RunnerGamepadState* gp) {
    (void)gp;
}
