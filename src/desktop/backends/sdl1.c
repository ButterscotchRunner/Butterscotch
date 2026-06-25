#include <string.h>
#include <stdio.h>

#include <SDL/SDL_events.h>
#include <SDL/SDL.h>
#include <SDL/SDL_video.h>

#include "common.h"
#include "input_recording.h"
#include "desktop/platformdefs.h"
#include "gettime.h"

#ifndef SDL_BUTTON_WHEELUP
#define SDL_BUTTON_WHEELUP 4
#endif
#ifndef SDL_BUTTON_WHEELDOWN
#define SDL_BUTTON_WHEELDOWN 5
#endif
#include "runner_mouse.h"

static Runner *g_runner;
static int32_t fbWidth, fbHeight;
static SDL_Surface* scr;

void platformSetWindowTitle(const char* title) {
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

bool platformGetScaledWindowSize(int32_t* outW, int32_t* outH) {
    return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    if (!platformCacheWindowSize(width, height)) return;
    fbWidth = width;
    fbHeight = height;
    scr = SDL_SetVideoMode(fbWidth, fbHeight, 0, (gfx == SOFTWARE ? 0 : SDL_OPENGL) | SDL_RESIZABLE);
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (!xPos || !yPos) return;
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    *xPos = (double)mx;
    *yPos = (double)my;
}

static bool platformGetWindowFocus(void) {
    return SDL_GetAppState() & SDL_APPINPUTFOCUS;
}

#if !SDL_VERSION_ATLEAST(1, 2, 10)
static inline void platformGetTrueDesktopSize(int* outW, int* outH) {
    SDL_Rect** modes = SDL_ListModes(NULL, SDL_FULLSCREEN);
    if (modes != (SDL_Rect**)0 && modes != (SDL_Rect**)-1) {
        int maxHardwareW = 0;
        int maxHardwareH = 0;
        int i;

        for (i = 0; modes[i]; ++i) {
            if (modes[i]->w > maxHardwareW) maxHardwareW = modes[i]->w;
            if (modes[i]->h > maxHardwareH) maxHardwareH = modes[i]->h;
        }

        if (maxHardwareW > 0 && maxHardwareH > 0) {
            if (maxHardwareW < *outW) *outW = maxHardwareW;
            if (maxHardwareH < *outH) *outH = maxHardwareH;
        }
    }
}
#endif

bool platformInit(int32_t reqW, int32_t reqH, const char *title, bool headless) {
    if (headless && gfx != SOFTWARE) {
        fprintf(stderr, "Headless mode on SDL 1.2 requires the software renderer!\n");
        return false;
    }

#if SDL_VERSION_ATLEAST(1, 2, 10) // Old SDL1.2: Center pos doesn't matter assuming it's running in low res
    SDL_putenv("SDL_VIDEO_WINDOW_POS=center");
#endif

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)) {
        fprintf(stderr, "Failed to initialize SDL\n");
        return false;
    }

    int finalW = reqW;
    int finalH = reqH;
#if SDL_VERSION_ATLEAST(1, 2, 10)
    const SDL_VideoInfo* info = SDL_GetVideoInfo();
    if (info && (reqW >= info->current_w || reqH >= info->current_h)) {
        platformGetBestFitRes(reqW, reqH, info->current_w, info->current_h, &finalW, &finalH);
        fprintf(stderr, "Warning: Requested resolution %dx%d is bigger than %dx%d, adjusting to %dx%d\n",
                reqW, reqH, info->current_w, info->current_h, finalW, finalH);
    }
#else
    // Old SDL1.2: Set a default lower res then check if the screen supports it, if not, use the max supported res
    int oldW = 800;
    int oldH = 600;
    platformGetTrueDesktopSize(oldW, oldH);
    if (reqW >= oldW || reqH >= oldH) {
        platformGetBestFitRes(reqW, reqH, oldW, oldH, &finalW, &finalH);
        fprintf(stderr, "Warning: Requested resolution %dx%d is bigger than %dx%d, adjusting to %dx%d\n",
                reqW, reqH, oldW, oldH, finalW, finalH);
    }
#endif

    fbWidth = finalW;
    fbHeight = finalH;
    if (!headless) {
        scr = SDL_SetVideoMode(fbWidth, fbHeight, 0, (gfx == SOFTWARE ? 0 : SDL_OPENGL) | SDL_RESIZABLE);
        if (!scr && gfx == SOFTWARE) {
            SDL_Rect** modes = SDL_ListModes(NULL, SDL_FULLSCREEN);
            if (modes && modes != (SDL_Rect**) -1 && modes[0]) {
                fprintf(stderr, "Warning: %dx%d unavailable, falling back to %dx%d: %s\n",
                        fbWidth, fbHeight, modes[0]->w, modes[0]->h, SDL_GetError());
                fbWidth = modes[0]->w;
                fbHeight = modes[0]->h;
                scr = SDL_SetVideoMode(fbWidth, fbHeight, 0, 0);
            }
        }
        if (!scr) {
            fprintf(stderr, "Fatal: Could not set any video mode: %s\n", SDL_GetError());
            return false;
        }
    }

    SDL_WM_SetCaption(title, NULL);

    SDL_EnableKeyRepeat(0, 0);

    return true;
}

void platformExit(void) {
    SDL_Quit();
}

static void platformSetCursor(int32_t cursorType) {
    // SDL1.2 only supports showing/hiding
    SDL_ShowCursor(cursorType == GML_CR_NONE ? SDL_DISABLE : SDL_ENABLE);
}

void platformInitFunctions(Runner *runner) {
    g_runner = runner;
    runner->windowHasFocus = platformGetWindowFocus;
    runner->setCursor = platformSetCursor;
    runner->currentCursor = GML_CR_DEFAULT;
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
        if (!scr)
            return;
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

static int32_t SDLMouseButtonToGml(int sdlButton) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: return GML_MB_LEFT;
        case SDL_BUTTON_RIGHT: return GML_MB_RIGHT;
        case SDL_BUTTON_MIDDLE: return GML_MB_MIDDLE;
        default: return -1;
    }
}

bool platformHandleEvents(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            default:
                if (InputRecording_isPlaybackActive(globalInputRecording)) continue;
                break;
            case SDL_VIDEORESIZE:
            case SDL_QUIT:
                break;
        }
        switch(e.type) {
            case SDL_KEYDOWN:
                // SDL1.2 needs to manually intercept Alt+F4 to exit properly
                if (e.key.keysym.sym == SDLK_F4 && (e.key.keysym.mod & KMOD_ALT)) {
                    return true;
                }
                // During playback, suppress real keyboard input
                RunnerKeyboard_onKeyDown(g_runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                if (e.key.keysym.unicode != 0)
                    RunnerKeyboard_onCharacter(g_runner->keyboard, e.key.keysym.unicode);
                break;
            case SDL_KEYUP:
                // During playback, suppress real keyboard input
                RunnerKeyboard_onKeyUp(g_runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_WHEELUP) {
                    RunnerMouse_onWheel(g_runner->mouse, 1.0);
                } else if (e.button.button == SDL_BUTTON_WHEELDOWN) {
                    RunnerMouse_onWheel(g_runner->mouse, -1.0);
                } else {
                    int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                    if (gmlBtn >= 0) RunnerMouse_onButtonDown(g_runner->mouse, gmlBtn);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button != SDL_BUTTON_WHEELUP && e.button.button != SDL_BUTTON_WHEELDOWN) {
                    int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                    if (gmlBtn >= 0) RunnerMouse_onButtonUp(g_runner->mouse, gmlBtn);
                }
                break;
            case SDL_VIDEORESIZE:
                fbWidth = e.resize.w;
                fbHeight = e.resize.h;
                scr = SDL_SetVideoMode(fbWidth, fbHeight, 0, (gfx == SOFTWARE ? 0 : SDL_OPENGL) | SDL_RESIZABLE);
                break;
            case SDL_QUIT:
                return true;
        }
    }

    return false;
}

void platformSleepUntil(uint64_t time) {
    int64_t remaining = time - nowNanos();
    if (remaining > 2000000)
        SDL_Delay((remaining - 1000000) / 1000000);

    while (nowNanos() < time) {
        // Spin-wait for the remaining sub-millisecond
        YIELD();
    }
}
