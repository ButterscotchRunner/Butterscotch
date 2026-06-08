#include <stdio.h>

#include <SDL2/SDL_events.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>

#include "common.h"
#include "input_recording.h"
#include "desktop/platformdefs.h"

static Runner *g_runner;
static SDL_Surface* scr;
static SDL_Window *window;

void platformSetWindowTitle(const char* title) {
    char windowTitle[256];
    snprintf(windowTitle, sizeof(windowTitle), "Butterscotch - %s", title);
    SDL_SetWindowTitle(window, windowTitle);
}

bool platformGetWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH) return false;
    if (gfx == SOFTWARE) {
        if (scr->w <= 0 || scr->h <= 0) return false;
        *outW = scr->w;
        *outH = scr->h;
    } else {
        int w = 0;
        int h = 0;
        SDL_GL_GetDrawableSize(window, &w, &h);
        if (w <= 0 || h <= 0) return false;
        *outW = w;
        *outH = h;
    }
    return true;
}

bool platformGetScaledWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH) return false;
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return false;
    *outW = w;
    *outH = h;
    return true;
}

static void platformGetWindowScale(float *scale_x, float *scale_y) {
    if (!scale_x || !scale_y) return;
    int32_t draw_w, draw_h;
    int logical_w, logical_h;
    platformGetWindowSize(&draw_w, &draw_h);
    SDL_GetWindowSize(window, &logical_w, &logical_h);
    *scale_x = (logical_w > 0) ? (float)draw_w / logical_w : 1.0f;
    *scale_y = (logical_h > 0) ? (float)draw_h / logical_h : 1.0f;
}

void platformSetWindowSize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;

    float scale_x, scale_y;
    platformGetWindowScale(&scale_x, &scale_y);
    SDL_SetWindowSize(window, (int)(width / scale_x), (int)(height / scale_y));

    if (gfx == SOFTWARE)
        scr = SDL_GetWindowSurface(window);
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (!xPos || !yPos) return;
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    float scale_x, scale_y;
    platformGetWindowScale(&scale_x, &scale_y);
    *xPos = (double)mx * scale_x;
    *yPos = (double)my * scale_y;
}

static bool platformGetWindowFocus(void) {
    return SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
}

bool platformInit(int reqW, int reqH, const char *title, bool headless) {
    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)) {
        fprintf(stderr, "Failed to initialize SDL\n");
        return false;
    }

    if (gfx == LEGACY_GL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    } else if (gfx == MODERN_GL) {
#ifdef ENABLE_GLES
#ifdef SDL_GL_CONTEXT_PROFILE_MASK
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    }

    Uint32 flags;
    if (headless)
        flags = (gfx == SOFTWARE ? 0 : SDL_WINDOW_OPENGL) | SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;
    else
        flags = (gfx == SOFTWARE ? 0 : SDL_WINDOW_OPENGL) | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    window = SDL_CreateWindow(
            title,
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            reqW, reqH,
            flags
    );
    if (!window && gfx == SOFTWARE) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(0, 0, &mode) == 0) {
            fprintf(stderr, "Warning: %dx%d unavailable, falling back to %dx%d: %s\n",
                    reqW, reqH, mode.w, mode.h, SDL_GetError());
            reqW = mode.w;
            reqH = mode.h;
            window = SDL_CreateWindow(
                    title,
                    SDL_WINDOWPOS_UNDEFINED,
                    SDL_WINDOWPOS_UNDEFINED,
                    mode.w, mode.h,
                    flags
            );
        }
    }
    if (!window) {
        fprintf(stderr, "Fatal: Could not set any video mode: %s\n", SDL_GetError());
        return false;
    }
    if (gfx != SOFTWARE) {
        if (!SDL_GL_CreateContext(window)) {
            fprintf(stderr, "Fatal: Could not create GL context: %s\n", SDL_GetError());
            return false;
        }
        SDL_GL_SetSwapInterval(0); // disable vsync
    } else
        scr = SDL_GetWindowSurface(window);

    // If we don't do this, the window will be larger than it should be on HiDPI displays.
    platformSetWindowSize(reqW, reqH);

    return true;
}

void platformExit(void) {
    SDL_Quit();
}

void platformInitFunctions(Runner *runner) {
    g_runner = runner;
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
        SDL_UpdateWindowSurface(window);
    }
#endif
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
    if (gfx == LEGACY_GL || gfx == MODERN_GL)
        SDL_GL_SwapWindow(window);
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

static uint32_t utf8_to_codepoint(const char *s) {
    const unsigned char *p = (const unsigned char *)s;

    if (p[0] < 0x80)
        return p[0];

    if ((p[0] & 0xE0) == 0xC0)
        return ((p[0] & 0x1F) << 6) |
               (p[1] & 0x3F);

    if ((p[0] & 0xF0) == 0xE0)
        return ((p[0] & 0x0F) << 12) |
               ((p[1] & 0x3F) << 6) |
               (p[2] & 0x3F);

    if ((p[0] & 0xF8) == 0xF0)
        return ((p[0] & 0x07) << 18) |
               ((p[1] & 0x3F) << 12) |
               ((p[2] & 0x3F) << 6) |
               (p[3] & 0x3F);

    return 0xFFFD; // replacement character
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
            case SDL_WINDOWEVENT:
            case SDL_QUIT:
        }
        switch(e.type) {
            case SDL_KEYDOWN:
                // During playback, suppress real keyboard input
                if (e.key.repeat != 0)
                    break;
                RunnerKeyboard_onKeyDown(g_runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                break;
            case SDL_KEYUP:
                // During playback, suppress real keyboard input
                RunnerKeyboard_onKeyUp(g_runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                break;
            case SDL_TEXTINPUT:
                // During playback, suppress real keyboard input
                RunnerKeyboard_onCharacter(g_runner->keyboard, utf8_to_codepoint(e.text.text));
                break;
            case SDL_MOUSEBUTTONDOWN: {
                int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                if (gmlBtn >= 0) RunnerMouse_onButtonDown(g_runner->mouse, gmlBtn);
            } break;
            case SDL_MOUSEBUTTONUP: {
                int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                if (gmlBtn >= 0) RunnerMouse_onButtonUp(g_runner->mouse, gmlBtn);
            } break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y != 0)
                    RunnerMouse_onWheel(g_runner->mouse, (float)e.wheel.y);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && gfx == SOFTWARE)
                    scr = SDL_GetWindowSurface(window);
                break;
            case SDL_QUIT:
                return true;
        }
    }

    return false;
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
