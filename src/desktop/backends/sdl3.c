#include <SDL3/SDL_timer.h>
#include <stdio.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#include "common.h"
#include "input_recording.h"
#include "desktop/platformdefs.h"
#include <ctype.h>

static Runner *g_runner;
static int32_t fbWidth, fbHeight;
static SDL_Surface* scr;
static SDL_Window *window;

void platformSetWindowTitle(const char* title) {
    char windowTitle[256];
    snprintf(windowTitle, sizeof(windowTitle), "Butterscotch - %s", title);
    SDL_SetWindowTitle(window, windowTitle);
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
    fbWidth = width;
    fbHeight = height;
    SDL_SetWindowSize(window, width, height);
    if (gfx == SOFTWARE)
        scr = SDL_GetWindowSurface(window);
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (!xPos || !yPos) return;
    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    *xPos = (double)mx;
    *yPos = (double)my;
}

static bool platformGetWindowFocus(void) {
    return SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
}

bool platformInit(int reqW, int reqH, const char *title, bool headless) {
    // Init SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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

    Uint32 flags = (gfx == SOFTWARE ? 0 : SDL_WINDOW_OPENGL) | (headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    fbWidth = reqW;
    fbHeight = reqH;
    window = SDL_CreateWindow(
        title,
        fbWidth,
        fbHeight,
        flags
    );
    if (!window && gfx == SOFTWARE) {
        SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);
        if (mode != NULL) {
            fprintf(stderr, "Warning: %dx%d unavailable, falling back to %dx%d: %s\n",
                    reqW, reqH, mode->w, mode->h, SDL_GetError());
            fbWidth = mode->w;
            fbHeight = mode->h;
            window = SDL_CreateWindow(
                title,
                fbWidth,
                fbHeight,
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
        SDL_DestroySurface(nextFb);
        nextFb = NULL;
    }

    nextFb = SDL_CreateSurfaceFrom(
        width,
        height,
        SDL_PIXELFORMAT_XRGB8888,
        framebuffer,
        width * 4
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
    // SDL_GetTicksNS() returns Uint64 nanoseconds
    return (double)SDL_GetTicksNS() / 1000000000.0;
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
    bool should_exit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch(e.type) {
            case SDL_EVENT_KEY_DOWN:
                // During playback, suppress real keyboard input
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                if (e.key.repeat != 0)
                    break;
                RunnerKeyboard_onKeyDown(g_runner->keyboard, SDLKeyToGml(e.key.key));
                break;
            case SDL_EVENT_KEY_UP:
                // During playback, suppress real keyboard input
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                RunnerKeyboard_onKeyUp(g_runner->keyboard, SDLKeyToGml(e.key.key));
                break;
            case SDL_EVENT_TEXT_INPUT:
                // During playback, suppress real keyboard input
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                RunnerKeyboard_onCharacter(g_runner->keyboard, utf8_to_codepoint(e.text.text));
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                if (gmlBtn >= 0) RunnerMouse_onButtonDown(g_runner->mouse, gmlBtn);
            } break;
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                if (gmlBtn >= 0) RunnerMouse_onButtonUp(g_runner->mouse, gmlBtn);
            } break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (InputRecording_isPlaybackActive(globalInputRecording)) break;
                if (e.wheel.y != 0)
                    RunnerMouse_onWheel(g_runner->mouse, (float)e.wheel.y);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                fbWidth = e.window.data1;
                fbHeight = e.window.data2;
                if (gfx == SOFTWARE)
                    scr = SDL_GetWindowSurface(window);
                break;
            case SDL_EVENT_QUIT:
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

    if (remaining > 0.0) {
        Uint64 remainingNS = (Uint64)(remaining * 1000000000.0);
        SDL_DelayPrecise(remainingNS);
    }
}

void platformGamepad_poll(RunnerGamepadState* gp) {
    (void)gp;
}
