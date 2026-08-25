#include <SDL2/SDL.h>
#include <SDL2/SDL_webOS.h>

#include <glad/glad.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "common.h"
#include "data_win.h"
#include "runner.h"
#include "runner_keyboard.h"
#include "runner_mouse.h"
#include "overlay_file_system.h"
#include "ma_audio_system.h"
#include "noop_audio_system.h"
#include "gl/gl_renderer.h"
#include "gettime.h"
#include "log.h"
#include "stb_ds.h"

static SDL_Window *gWindow = NULL;
static SDL_GLContext gGLContext = NULL;
static Runner *gRunner = NULL;
static SDL_GameController *gController = NULL;
static FILE *gInputLog = NULL;

void platformLog(const logType type, const char *format, va_list va)
{
    FILE *out = stderr;

    switch (type) {
        case LOG_TYPE_NORMAL:  out = stdout; break;
        case LOG_TYPE_WARNING: fputs("Warning: ", out); break;
        case LOG_TYPE_ERROR:   fputs("Error: ", out); break;
        case LOG_TYPE_DEBUG:   fputs("Debug: ", out); break;
    }

    vfprintf(out, format, va);
}

static void *webosGetProcAddress(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

static bool webosGetWindowSize(int32_t *outW, int32_t *outH)
{
    if (gWindow == NULL || outW == NULL || outH == NULL)
        return false;

    int w = 0;
    int h = 0;

    SDL_GL_GetDrawableSize(gWindow, &w, &h);

    if (w <= 0 || h <= 0)
        return false;

    *outW = w;
    *outH = h;
    return true;
}

static void webosSetWindowTitle(const char *title)
{
    if (gWindow == NULL)
        return;

    if (title == NULL)
        title = "Butterscotch";

    SDL_SetWindowTitle(gWindow, title);
}

static int32_t SDLKeyToGml(SDL_Keycode key)
{
    if (key >= SDLK_a && key <= SDLK_z)
        return (int32_t)(key - SDLK_a + 'A');

    if (key >= SDLK_0 && key <= SDLK_9)
        return (int32_t)key;

    switch (key) {
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
        default:             return -1;
    }
}

static int32_t SDLMouseButtonToGml(uint8_t button)
{
    switch (button) {
        case SDL_BUTTON_LEFT:   return GML_MB_LEFT;
        case SDL_BUTTON_RIGHT:  return GML_MB_RIGHT;
        case SDL_BUTTON_MIDDLE: return GML_MB_MIDDLE;
        default:                return -1;
    }
}

static uint32_t utf8_to_codepoint(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    if (p == NULL || p[0] == '\0')
        return 0;

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

    return 0xFFFD;
}

static bool webosInitGraphics(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        logError("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);

    gWindow = SDL_CreateWindow(
        "Butterscotch",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_OPENGL
    );

    if (gWindow == NULL) {
        logError("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    gGLContext = SDL_GL_CreateContext(gWindow);

    if (gGLContext == NULL) {
        logError("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(gWindow);
        gWindow = NULL;
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(gWindow, gGLContext);

    if (!gladLoadGLES2Loader(webosGetProcAddress)) {
        logError("gladLoadGLES2Loader failed\n");
        SDL_GL_DeleteContext(gGLContext);
        gGLContext = NULL;
        SDL_DestroyWindow(gWindow);
        gWindow = NULL;
        SDL_Quit();
        return false;
    }

    logInfo("WebOS GL version: %s\n", (const char *)glGetString(GL_VERSION));
    logInfo("WebOS GL renderer: %s\n", (const char *)glGetString(GL_RENDERER));

    return true;
}

static bool mkdirP(const char *path)
{
    char buf[512];
    size_t len;

    if (path == NULL || path[0] == '\0')
        return true;

    len = strlen(path);

    if (len >= sizeof(buf))
        return false;

    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';

            if (mkdir(buf, 0777) != 0 && errno != EEXIST)
                return false;

            buf[i] = '/';
        }
    }

    if (mkdir(buf, 0777) != 0 && errno != EEXIST)
        return false;

    return true;
}

static bool startRunner(const char *dataWinPath, const char *savesPath)
{
    DataWinParserOptions options = {0};

    options.parseGen8 = true;
    options.parseOptn = true;
    options.parseLang = true;
    options.parseExtn = true;
    options.parseSond = true;
    options.parseAgrp = true;
    options.parseSprt = true;
    options.parseBgnd = true;
    options.parsePath = true;
    options.parseScpt = true;
    options.parseGlob = true;
    options.parseShdr = true;
    options.parseFont = true;
    options.parseTmln = true;
    options.parseObjt = true;
    options.parseRoom = true;
    options.parseTpag = true;
    options.parseCode = true;
    options.parseVari = true;
    options.parseFunc = true;
    options.parseStrg = true;
    options.parseTxtr = true;
    options.parseAudo = true;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = false;
    options.eagerlyLoadedRooms = NULL;

    if (!mkdirP(savesPath)) {
        logWarn("Could not create saves directory: %s\n", savesPath);
    }

    logInfo("Loading data.win: %s\n", dataWinPath);

    DataWin *dataWin = DataWin_parse(dataWinPath, options);

    if (dataWin == NULL) {
        logError("Failed to parse data.win: %s\n", dataWinPath);
        return false;
    }

    VMContext *vm = VM_create(dataWin);

    if (vm == NULL) {
        logError("VM_create failed\n");
        DataWin_free(dataWin);
        return false;
    }

    Renderer *renderer = GLRenderer_create();

    if (renderer == NULL) {
        logError("GLRenderer_create failed\n");
        VM_free(vm);
        DataWin_free(dataWin);
        return false;
    }

    const char *lastSlash = strrchr(dataWinPath, '/');
    char *bundleDir = NULL;

    if (lastSlash != NULL) {
        size_t len = (size_t)(lastSlash - dataWinPath + 1);

        bundleDir = (char *)safeMalloc(len + 1);
        memcpy(bundleDir, dataWinPath, len);
        bundleDir[len] = '\0';
    } else {
        bundleDir = safeStrdup("./");
    }

    OverlayFileSystem *overlayFs =
        OverlayFileSystem_create(bundleDir, savesPath);

    free(bundleDir);

    if (overlayFs == NULL) {
        logError("OverlayFileSystem_create failed\n");
        renderer->vtable->destroy(renderer);
        VM_free(vm);
        DataWin_free(dataWin);
        return false;
    }

    AudioSystem *audioSystem =
        (AudioSystem *)MaAudioSystem_create(dataWin);

    if (audioSystem == NULL) {
        logWarn("MaAudioSystem_create failed; using silent audio\n");
        audioSystem = (AudioSystem *)NoopAudioSystem_create();
    }

    gRunner = Runner_create(
        dataWin,
        vm,
        renderer,
        (FileSystem *)overlayFs,
        audioSystem,
        0
    );

    if (gRunner == NULL) {
        logError("Runner_create failed\n");
        audioSystem->vtable->destroy(audioSystem);
        renderer->vtable->destroy(renderer);
        VM_free(vm);
        DataWin_free(dataWin);
        return false;
    }

    gRunner->osType = OS_LINUX;
    gRunner->setWindowTitle = webosSetWindowTitle;
    gRunner->getWindowSize = webosGetWindowSize;
    gRunner->windowHasFocus = NULL;

    char **args = NULL;
    arrput(args, safeStrdup("butterscotch"));
    Runner_setGameArgs(gRunner, args, (int32_t)arrlen(args));
    free(args[0]);
    arrfree(args);

    const char *title = dataWin->gen8.displayName;

    if (title == NULL || title[0] == '\0')
        title = dataWin->gen8.name;

    webosSetWindowTitle(title);

    gRunner->gameStartTime = nowNanos();

    Runner_initFirstRoom(gRunner);

    logInfo("Runner started: %s\n", title);

    return true;
}


static void updateWebOSGamepad(void)
{
    if (gController == NULL ||
        !SDL_GameControllerGetAttached(gController))
        return;

    GamepadSlot *slot = &gRunner->gamepads->slots[0];

    slot->connected = true;
    slot->jid = 0;

    const char *name = SDL_GameControllerName(gController);

    if (name != NULL) {
        strncpy(
            slot->description,
            name,
            sizeof(slot->description) - 1
        );
        slot->description[
            sizeof(slot->description) - 1
        ] = '\0';
    }

    slot->axisValue[0] =
        (float)SDL_GameControllerGetAxis(
            gController,
            SDL_CONTROLLER_AXIS_LEFTX
        ) / 32767.0f;

    slot->axisValue[1] =
        (float)SDL_GameControllerGetAxis(
            gController,
            SDL_CONTROLLER_AXIS_LEFTY
        ) / 32767.0f;

    slot->axisValue[2] =
        (float)SDL_GameControllerGetAxis(
            gController,
            SDL_CONTROLLER_AXIS_RIGHTX
        ) / 32767.0f;

    slot->axisValue[3] =
        (float)SDL_GameControllerGetAxis(
            gController,
            SDL_CONTROLLER_AXIS_RIGHTY
        ) / 32767.0f;

    /* Feed SDL controller buttons into the existing RunnerGamepad slot. */
    {
        const SDL_GameControllerButton sdlButtons[] = {
            SDL_CONTROLLER_BUTTON_A,
            SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_X,
            SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
            SDL_CONTROLLER_BUTTON_BACK,
            SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK,
            SDL_CONTROLLER_BUTTON_DPAD_UP,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT
        };

        const int runnerIndices[] = {
            0, 1, 2, 3,
            4, 5,
            8, 9,
            10, 11,
            12, 13, 14, 15
        };

        for (size_t i = 0; i < sizeof(sdlButtons) / sizeof(sdlButtons[0]); i++) {
            int idx = runnerIndices[i];
            bool wasDown = slot->buttonDown[idx];
            bool isDown = SDL_GameControllerGetButton(
                gController, sdlButtons[i]
            ) != 0;

            if (isDown && !wasDown)
                slot->buttonPressed[idx] = true;

            if (!isDown && wasDown)
                slot->buttonReleased[idx] = true;

            slot->buttonDown[idx] = isDown;
            slot->buttonValue[idx] = isDown ? 1.0f : 0.0f;
        }
    }

    gRunner->gamepads->connectedCount = 1;

    if (gInputLog != NULL) {
    fprintf(
        gInputLog,
        "RUNNER: count=%d connected=%d axes=%d buttons=%d desc=%s\n",
        RunnerGamepad_getDeviceCount(gRunner->gamepads),
        RunnerGamepad_isConnected(gRunner->gamepads, 0),
        RunnerGamepad_getAxisCount(gRunner->gamepads, 0),
        RunnerGamepad_getButtonCount(gRunner->gamepads, 0),
        RunnerGamepad_getDescription(gRunner->gamepads, 0)
    );
    fflush(gInputLog);
}

    if (gInputLog != NULL) {
        int a = SDL_GameControllerGetButton(
            gController, SDL_CONTROLLER_BUTTON_A
        );
        int b = SDL_GameControllerGetButton(
            gController, SDL_CONTROLLER_BUTTON_B
        );
        int x = SDL_GameControllerGetButton(
            gController, SDL_CONTROLLER_BUTTON_X
        );
        int y = SDL_GameControllerGetButton(
            gController, SDL_CONTROLLER_BUTTON_Y
        );

        fprintf(
            gInputLog,
            "LX=%.3f LY=%.3f RX=%.3f RY=%.3f A=%d B=%d X=%d Y=%d\n",
            slot->axisValue[0],
            slot->axisValue[1],
            slot->axisValue[2],
            slot->axisValue[3],
            a, b, x, y
        );
        fflush(gInputLog);
    }
}

static void initControllerDebug(void)
{
    int count = SDL_NumJoysticks();

    gInputLog = fopen("/tmp/butterscotch_input.log", "w");

    if (gInputLog != NULL) {
        fprintf(gInputLog, "SDL joystick count: %d\n", count);

        for (int i = 0; i < count; i++) {
            const char *name = SDL_GameControllerNameForIndex(i);

            fprintf(
                gInputLog,
                "joystick %d: isGameController=%d name=%s\n",
                i,
                SDL_IsGameController(i),
                name ? name : "(null)"
            );
        }

        fflush(gInputLog);
    }

    for (int i = 0; i < count; i++) {
        if (!SDL_IsGameController(i))
            continue;

        gController = SDL_GameControllerOpen(i);

        if (gController != NULL) {
            logInfo(
                "WebOS controller opened: %s\n",
                SDL_GameControllerName(gController)
                    ? SDL_GameControllerName(gController)
                    : "(unknown)"
            );
            break;
        }
    }
}

static void handleEvents(void)
{
    SDL_Event e;

    RunnerKeyboard_beginFrame(gRunner->keyboard);
    RunnerGamepad_beginFrame(gRunner->gamepads);

    SDL_GameControllerUpdate();
    updateWebOSGamepad();

    while (SDL_PollEvent(&e)) {
        logInfo("WEBOS SDL EVENT: type=%u\\n", (unsigned)e.type);

        if (gInputLog != NULL) {
            fprintf(gInputLog, "SDL EVENT: type=%u\\n", (unsigned)e.type);

            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                fprintf(
                    gInputLog,
                    "  KEY: type=%u sym=%d name=%s\\n",
                    (unsigned)e.type,
                    (int)e.key.keysym.sym,
                    SDL_GetKeyName(e.key.keysym.sym)
                );
            }

            if (e.type == SDL_CONTROLLERBUTTONDOWN ||
                e.type == SDL_CONTROLLERBUTTONUP) {
                fprintf(
                    gInputLog,
                    "  CONTROLLER: type=%u button=%d\\n",
                    (unsigned)e.type,
                    (int)e.cbutton.button
                );
            }

            fflush(gInputLog);
        }

        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            logInfo(
                "KEY EVENT: type=%u sym=%d name=%s repeat=%d\\n",
                (unsigned)e.type,
                (int)e.key.keysym.sym,
                SDL_GetKeyName(e.key.keysym.sym),
                (int)e.key.repeat
            );
        }

        if (e.type == SDL_CONTROLLERBUTTONDOWN ||
            e.type == SDL_CONTROLLERBUTTONUP) {
            logInfo(
                "CONTROLLER EVENT: type=%u button=%d\\n",
                (unsigned)e.type,
                (int)e.cbutton.button
            );
        }

        switch (e.type) {
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                /* Consume duplicate raw joystick events. */
                break;

            case SDL_QUIT:
                gRunner->shouldExit = true;
                break;

            case SDL_KEYDOWN: {
                if (e.key.repeat)
                    break;

                int32_t key = SDLKeyToGml(e.key.keysym.sym);

                if (key >= 0)
                    RunnerKeyboard_onKeyDown(gRunner->keyboard, key);
                break;
            }

            case SDL_KEYUP: {
                int32_t key = SDLKeyToGml(e.key.keysym.sym);

                if (key >= 0)
                    RunnerKeyboard_onKeyUp(gRunner->keyboard, key);
                break;
            }

            case SDL_TEXTINPUT:
                RunnerKeyboard_onCharacter(
                    gRunner->keyboard,
                    utf8_to_codepoint(e.text.text)
                );
                break;

            case SDL_MOUSEBUTTONDOWN: {
                int32_t button =
                    SDLMouseButtonToGml(e.button.button);

                if (button >= 0)
                    RunnerMouse_onButtonDown(
                        gRunner->mouse,
                        button
                    );
                break;
            }

            case SDL_MOUSEBUTTONUP: {
                int32_t button =
                    SDLMouseButtonToGml(e.button.button);

                if (button >= 0)
                    RunnerMouse_onButtonUp(
                        gRunner->mouse,
                        button
                    );
                break;
            }

            case SDL_MOUSEWHEEL:
                if (e.wheel.y != 0)
                    RunnerMouse_onWheel(
                        gRunner->mouse,
                        (float)e.wheel.y
                    );
                break;

            case SDL_MOUSEMOTION:
                Runner_updateMousePosition(
                    gRunner,
                    e.motion.windowID ? 1280 : 1280,
                    720,
                    e.motion.x,
                    e.motion.y
                );
                break;

            default:
                break;
        }
    }
}

static void runFrame(void)
{
    static uint64_t lastTime = 0;
    uint64_t now = nowNanos();

    if (lastTime == 0)
        lastTime = now;

    gRunner->deltaTime =
        (double)(now - lastTime) / 1000.0;

    lastTime = now;

    int32_t winW = 0;
    int32_t winH = 0;

    if (!webosGetWindowSize(&winW, &winH)) {
        winW = 1280;
        winH = 720;
    }

    Runner_step(gRunner);

    if (gRunner->audioSystem != NULL) {
        float audioDt =
            (float)(gRunner->deltaTime / 1000000.0);

        if (audioDt < 0.0f)
            audioDt = 0.0f;

        if (audioDt > 0.1f)
            audioDt = 0.1f;

        gRunner->audioSystem->vtable->update(
            gRunner->audioSystem,
            audioDt
        );
    }

    int32_t gameW =
        gRunner->dataWin->gen8.defaultWindowWidth;

    int32_t gameH =
        gRunner->dataWin->gen8.defaultWindowHeight;

    if (gRunner->appSurfaceEnabled) {
        if (gRunner->applicationWidth > 0)
            gameW = gRunner->applicationWidth;

        if (gRunner->applicationHeight > 0)
            gameH = gRunner->applicationHeight;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Runner_drawPre(gRunner, winW, winH);

    Runner_beginFrame(
        gRunner,
        gameW,
        gameH,
        winW,
        winH,
        winW,
        winH
    );

    Runner_drawViews(
        gRunner,
        gameW,
        gameH,
        false
    );

    gRunner->renderer->vtable->endFrameInit(
        gRunner->renderer
    );

    Runner_drawPost(gRunner, winW, winH);

    gRunner->renderer->vtable->endFrameEnd(
        gRunner->renderer
    );

    Runner_drawGUI(
        gRunner,
        winW,
        winH,
        gameW,
        gameH
    );

    bool shouldSwap = (gRunner->pendingRoom == -1);

    Runner_handlePendingRoomChange(gRunner);

    if (shouldSwap)
        SDL_GL_SwapWindow(gWindow);
}

static void destroyRunner(void)
{
    if (gRunner == NULL)
        return;

    AudioSystem *audio = gRunner->audioSystem;
    Renderer *renderer = gRunner->renderer;
    DataWin *dataWin = gRunner->dataWin;
    VMContext *vm = gRunner->vmContext;

    gRunner = NULL;

    if (audio != NULL)
        audio->vtable->destroy(audio);

    if (renderer != NULL)
        renderer->vtable->destroy(renderer);

    Runner_free(gRunner);
    VM_free(vm);
    DataWin_free(dataWin);
}

static void webosShutdownGraphics(void)
{
    if (gGLContext != NULL) {
        SDL_GL_DeleteContext(gGLContext);
        gGLContext = NULL;
    }

    if (gWindow != NULL) {
        SDL_DestroyWindow(gWindow);
        gWindow = NULL;
    }

    SDL_Quit();
}

int main(int argc, char **argv)
{
    const char *dataWinPath = "./data.win";
    const char *savesPath = "./saves";

    /*
     * webOS native apps receive a JSON launch object in argv[1].
     * It is NOT a data.win path, so deliberately ignore it here.
     */

    if (!webosInitGraphics(1280, 720)) {
        return 1;
    }

    SDL_StartTextInput();

    {
        gInputLog = fopen("/tmp/butterscotch_input.log", "w");

        if (gInputLog != NULL) {
            int count = SDL_NumJoysticks();

            fprintf(gInputLog, "SDL joystick count: %d\n", count);

            for (int i = 0; i < count; i++) {
                const char *name = SDL_GameControllerNameForIndex(i);

                fprintf(
                    gInputLog,
                    "SDL joystick %d: isGameController=%d name=%s\n",
                    i,
                    SDL_IsGameController(i),
                    name ? name : "(null)"
                );

                if (gController == NULL &&
                    SDL_IsGameController(i)) {
                    gController = SDL_GameControllerOpen(i);

                    if (gController != NULL) {
                        fprintf(
                            gInputLog,
                            "Opened controller %d: %s\n",
                            i,
                            SDL_GameControllerName(gController)
                                ? SDL_GameControllerName(gController)
                                : "(unknown)"
                        );
                    } else {
                        fprintf(
                            gInputLog,
                            "Failed to open controller %d: %s\n",
                            i,
                            SDL_GetError()
                        );
                    }
                }
            }

            fflush(gInputLog);
        }
    }

    if (!startRunner(dataWinPath, savesPath)) {
        logError("Could not start Butterscotch Runner\n");
        webosShutdownGraphics();
        return 1;
    }

    bool running = true;
    uint64_t nextFrameTime = nowNanos();

    while (running && !gRunner->shouldExit) {
        handleEvents();

        if (gRunner->shouldExit)
            break;

        runFrame();

        if (gRunner->currentRoom != NULL &&
            gRunner->currentRoom->speed > 0) {
            uint64_t targetFrameNs =
                1000000000ULL / (uint64_t)gRunner->currentRoom->speed;

            nextFrameTime += targetFrameNs;

            uint64_t now = nowNanos();

            if (now > nextFrameTime + targetFrameNs * 4) {
                nextFrameTime = now + targetFrameNs;
            } else {
                while (now < nextFrameTime) {
                    uint64_t remaining = nextFrameTime - now;

                    if (remaining > 2000000ULL) {
                        SDL_Delay((uint32_t)((remaining - 1000000ULL) / 1000000ULL));
                    }

                    now = nowNanos();
                }
            }
        } else {
            nextFrameTime = nowNanos();
        }
    }

    destroyRunner();
    SDL_StopTextInput();
    webosShutdownGraphics();

    return 0;
}
