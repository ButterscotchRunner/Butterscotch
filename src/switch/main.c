#include "data_win.h"
#include "runner_gamepad.h"
#include "vm.h"

#include "gl_renderer.h"
#include "overlay_file_system.h"
#include "gl_common.h"
#include "utils.h"

#if defined(USE_OPENAL) 
#include "al_audio_system.h"
#elif defined(USE_MINIAUDIO)
#include "ma_audio_system.h"
#endif
#include "noop_audio_system.h"

#include <GLES3/gl3.h>
#include <SDL2/SDL.h>

#include <switch.h>
#include <sys/stat.h>
#include <stdio.h>

#define GAME_DATA_PATH "sdmc:/data/butterscotch/"
#define GAME_DATA_WIN_PATH GAME_DATA_PATH "data.win"

const GLuint *hostFramebuffer;

// for game_change
char* pendingDataWinPath = NULL;

int32_t currentWindowWidth = 0;
int32_t currentWindowHeight = 0;

uint32_t utf8_to_codepoint(const char *s) {
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

#pragma region switch funcs

double osTime() {
    return (double)armGetSystemTick() / (double)armGetSystemTickFreq();
}

#pragma endregion

#pragma region sdl2 stuff
// code is mostly taken from desktop.c
SDL_Window* sdlWindow;
SDL_GameController* openControllers[MAX_GAMEPADS];

int32_t SDLMouseButtonToGml(int sdlButton) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: return GML_MB_LEFT;
        case SDL_BUTTON_RIGHT: return GML_MB_RIGHT;
        case SDL_BUTTON_MIDDLE: return GML_MB_MIDDLE;
        default: return -1;
    }
}
bool sdl2GetWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH) return false;
    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(sdlWindow, &w, &h);
    if (w <= 0 || h <= 0) return false;
    *outW = w;
    *outH = h;
    return true;
}
float sdl2GetWindowScale(void) {
    int32_t draw_w = 0, draw_h = 0;
    int logical_w = 0, logical_h = 0;
    sdl2GetWindowSize(&draw_w, &draw_h);
    SDL_GetWindowSize(sdlWindow, &logical_w, &logical_h);
    return (logical_h > 0) ? (float)draw_h / logical_h : 1.0f;
}
void sdl2SetWindowSize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    float scale = sdl2GetWindowScale();
    SDL_SetWindowSize(sdlWindow, (int)(width / scale), (int)(height / scale));
}
bool sdl2GetScaledWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH) return false;
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(sdlWindow, &w, &h);
    if (w <= 0 || h <= 0) return false;
    *outW = w;
    *outH = h;
    return true;
}
bool sdl2GetWindowFocus(void) {
    return SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_INPUT_FOCUS;
}
void sdl2GetMousePos(double *xPos, double *yPos) {
    if (!xPos || !yPos) return;
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    float scale = sdl2GetWindowScale();
    *xPos = (double)mx * scale;
    *yPos = (double)my * scale;
}
int32_t SDLKeyToGml(int sdlkey) {
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
void mapSdl2ToGml(SDL_GameController* gc, GamepadSlot* slot) {
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) slot->buttonDown[0] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) slot->buttonDown[1] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) slot->buttonDown[2] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y)) slot->buttonDown[3] = true;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) slot->buttonDown[4] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) slot->buttonDown[5] = true;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK)) slot->buttonDown[8] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) slot->buttonDown[9] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_GUIDE)) slot->buttonDown[16] = true;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK)) slot->buttonDown[10] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) slot->buttonDown[11] = true;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP)) slot->buttonDown[12] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) slot->buttonDown[13] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) slot->buttonDown[14] = true;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) slot->buttonDown[15] = true;

    float lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    float rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    if (lt < 0.0f) lt = 0.0f;
    if (rt < 0.0f) rt = 0.0f;
    slot->buttonValue[6] = lt;
    slot->buttonValue[7] = rt;
    if (lt >= slot->triggerThreshold) slot->buttonDown[6] = true;
    if (rt >= slot->triggerThreshold) slot->buttonDown[7] = true;

    float lh = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    float lv = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    float rh = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
    float rv = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;

    slot->axisValue[0] = lh;
    slot->axisValue[1] = lv;
    slot->axisValue[2] = rh;
    slot->axisValue[3] = rv;

    for (int i = 0; GP_BUTTON_COUNT > i; i++) {
        if (i == 6 || i == 7) continue;
        slot->buttonValue[i] = slot->buttonDown[i] ? 1.0f : 0.0f;
    }
}
bool sdl2HandleEvents(Runner* runner) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_WINDOWEVENT:
            case SDL_QUIT:
                break;
        }
        switch(e.type) {
            case SDL_KEYDOWN:
                // During playback, suppress real keyboard input
                if (e.key.repeat != 0)
                    break;
                RunnerKeyboard_onKeyDown(runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                break;
            case SDL_KEYUP:
                // During playback, suppress real keyboard input
                RunnerKeyboard_onKeyUp(runner->keyboard, SDLKeyToGml(e.key.keysym.sym));
                break;
            case SDL_TEXTINPUT:
                // During playback, suppress real keyboard input
                RunnerKeyboard_onCharacter(runner->keyboard, utf8_to_codepoint(e.text.text));
                break;
            case SDL_MOUSEBUTTONDOWN: {
                int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                if (gmlBtn >= 0) RunnerMouse_onButtonDown(runner->mouse, gmlBtn);
            } break;
            case SDL_MOUSEBUTTONUP: {
                int32_t gmlBtn = SDLMouseButtonToGml(e.button.button);
                if (gmlBtn >= 0) RunnerMouse_onButtonUp(runner->mouse, gmlBtn);
            } break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y != 0)
                    RunnerMouse_onWheel(runner->mouse, (float)e.wheel.y);
                break;
            case SDL_WINDOWEVENT:
                break;
            case SDL_CONTROLLERDEVICEADDED: {
                int device_index = e.cdevice.which;
                for (int i = 0; i < MAX_GAMEPADS; i++) {
                    if (openControllers[i] == NULL) {
                        openControllers[i] = SDL_GameControllerOpen(device_index);
                        break;
                    }
                }
                break;
            }
            case SDL_CONTROLLERDEVICEREMOVED: {
                int instance_id = e.cdevice.which;
                for (int i = 0; i < MAX_GAMEPADS; i++) {
                    if (openControllers[i]) {
                        SDL_Joystick* joy = SDL_GameControllerGetJoystick(openControllers[i]);
                        if (joy && SDL_JoystickInstanceID(joy) == instance_id) {
                            SDL_GameControllerClose(openControllers[i]);
                            openControllers[i] = NULL;
                            break;
                        }
                    }
                }
                break;
            }
            case SDL_QUIT:
                return true;
                break;
            default:
                break;
        }
    }

    runner->gamepads->connectedCount = 0;
    for (int slotIdx = 0; slotIdx < MAX_GAMEPADS; slotIdx++) {
        GamepadSlot* slot = runner->gamepads->slots + slotIdx;
        SDL_GameController* gc = openControllers[slotIdx];

        memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
        memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
        memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
        memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
        memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
        memset(slot->axisValue, 0, sizeof(slot->axisValue));

        if (gc && SDL_GameControllerGetAttached(gc)) {
            slot->connected = true;
            slot->jid = slotIdx;

            const char* name = SDL_GameControllerName(gc);
            if (name != NULL) {
                strncpy(slot->description, name, sizeof(slot->description) - 1);
                slot->description[sizeof(slot->description) - 1] = '\0';
            } else {
                slot->description[0] = '\0';
            }

            char guidStr[64] = {0};
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(gc);
            if (joy) {
                SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guidStr, sizeof(guidStr));
            }
            strncpy(slot->guid, guidStr, sizeof(slot->guid) - 1);
            slot->guid[sizeof(slot->guid) - 1] = '\0';

            mapSdl2ToGml(gc, slot);

            for (int btn = 0; GP_BUTTON_COUNT > btn; btn++) {
                bool wasDown = slot->buttonDownPrev[btn];
                if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
                if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
            }
            runner->gamepads->connectedCount++;
        } else {
            if (gc) {
                SDL_GameControllerClose(gc);
                openControllers[slotIdx] = NULL;
            }
            slot->connected = false;
            slot->guid[0] = '\0';
        }
    }

    return false;
}
void sdl2Close(void) {
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (openControllers[i]) {
            SDL_GameControllerClose(openControllers[i]);
            openControllers[i] = NULL;
        }
    }
    SDL_Quit();
}

static const struct {
    uint8_t major, minor;
    bool gles;
} GLCommon_versions[] = {
    /* Desktop GL */
    { 4, 6, false },
    { 4, 5, false },
    { 4, 4, false },
    { 4, 3, false },
    { 4, 2, false },
    { 4, 1, false },
    { 4, 0, false },
    { 3, 3, false },
    { 3, 2, false },
    { 3, 1, false },
    { 3, 0, false },
    { 2, 1, false },
    { 2, 0, false },
#ifndef USE_GLFW2
    /* GLES */
    { 3, 2, true  },
    { 3, 1, true  },
    { 3, 0, true  },
    { 2, 0, true  },
#endif
};
SDL_Window *sdl2TryOpenWindow(int reqW, int reqH, const char* title, Uint32 flags) {
    for (size_t i = 0; i < sizeof(GLCommon_versions)/sizeof(GLCommon_versions[0]); i++) {
        SDL_Window *newWindow;
        int contextFlags = 0;

#ifndef NDEBUG
        contextFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
#endif

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GLCommon_versions[i].major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GLCommon_versions[i].minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);

        if (GLCommon_versions[i].gles) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        } else {
            if (GLCommon_versions[i].major >= 3) {
                if (GLCommon_versions[i].major == 3 && GLCommon_versions[i].minor == 2) {
                    contextFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
                }
            } else {
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
            }
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags);

        newWindow = SDL_CreateWindow(
            title,
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            reqW, reqH,
            flags
        );

        if (newWindow) {
            if (SDL_GL_CreateContext(newWindow)) {
                return newWindow;
            }
            SDL_DestroyWindow(newWindow);
        }

    }
    return NULL;
}
bool sdl2Init(int reqW, int reqH) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_GAMECONTROLLER)) {
        logError("Failed to initialize SDL\n");
        return false;
    }
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        openControllers[i] = NULL;
    }
    sdlWindow = sdl2TryOpenWindow(reqW, reqH, "Butterscotch", SDL_WINDOW_OPENGL);
    if (!sdlWindow) {
        logError("Fatal: Could not open window: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(0); // disable vsync
    sdl2SetWindowSize(reqW, reqH);
    return true;
}

#pragma endregion


// Extracts the Runner arguments from a string, returning the values on stb_ds array
// The "Runner arguments" is used for the "--game-args" and for the game_change GML function
// Returns the modified array
// COPIED FROM src/desktop/main.c
static char** extractRunnerArguments(char* rawArguments) {
    // The "saveptr" is used for strtok_r to store its state
    // So it is thread safe™
    char *saveptr;
    // We create a copy because strtok_r completely obliterates the original char buffer
    char* copy = safeStrdup(rawArguments);
    char* token = strtok_r(copy, " \t\r\n", &saveptr);
    char** array = nullptr;

    while (token != nullptr) {
        arrput(array, safeStrdup(token));
        token = strtok_r(nullptr, " \t\r\n", &saveptr);
    }

    free(copy);

    return array;
}

int fileExists(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file != NULL) {
        fclose(file);
        return 1;
    }
    return 0;
}

void loop(const char* dataWinPath) {
    char* safePath = safeStrdup(dataWinPath);
    logInfo("Loading %s...\n", safePath);
    if (pendingDataWinPath) free(pendingDataWinPath);
    char* bundleDir = safeStrdup(safePath);
    {
        char* lastSlash = strrchr(bundleDir, '/');
        if (lastSlash) {
            *lastSlash = '\0';
        }
    }

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

#if defined(USE_MINIAUDIO) || defined(USE_OPENAL)
    options.parseAudo = true;
#endif
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = false;
    options.lazyLoadTextures = false;
    options.lazyLoadAudio = false;

    DataWin* dataWin = DataWin_parse(safePath, options);
    Gen8* gen8 = &dataWin->gen8;
    logInfo("Loaded \"%s\" (%d) successfully! [WAD Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->wadVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);

    VMContext* vm = VM_create(dataWin);
    Profiler_setEnabled(&vm->profiler, false);
#ifdef ENABLE_VM_OPCODE_PROFILER
    vm->opcodeProfilerEnabled = true;
    if (vm->opcodeProfilerEnabled) {
        vm->opcodeVariantCounts = (uint64_t *)safeCalloc(256 * 256, sizeof(uint64_t));
        vm->opcodeRValueTypeCounts = (uint64_t *)safeCalloc(256 * 256, sizeof(uint64_t));
    }
#endif
    OverlayFileSystem* overlayFs = OverlayFileSystem_create(bundleDir, GAME_DATA_PATH);

    if (!sdl2Init(gen8->defaultWindowWidth, gen8->defaultWindowHeight)) {
        logError("Failed to initialize SDL2\n");
        DataWin_free(dataWin);
    }

    Renderer* renderer = (Renderer*)GLRenderer_create();
    hostFramebuffer = (&((GLRenderer*)renderer)->hostFramebuffer);

    if (!renderer) {
        logError("Failed to initialize a renderer\n");
        DataWin_free(dataWin);
        return;
    }

#if defined(USE_OPENAL)
    AudioSystem* audioSystem = (AudioSystem*) AlAudioSystem_create();
#elif defined(USE_MINIAUDIO)
    AudioSystem* audioSystem = (AudioSystem*) MaAudioSystem_create(dataWin);
#else
    AudioSystem* audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif2

    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->debugMode = true; // for now
    runner->setWindowSize = sdl2SetWindowSize;
    runner->getWindowSize = sdl2GetWindowSize;
    Runner_initFirstRoom(runner);

    logInfo("Runner successfully created and inited first room!!\n");
    double lastFrameStartTime = osTime();

    bool shouldExit = false;
    while(true) {
        if (shouldExit || runner->shouldExit) {
            break;
        }
        bool shouldStep = true;

        double frameStartTime = osTime();
        runner->deltaTime = (frameStartTime - lastFrameStartTime);
        lastFrameStartTime = frameStartTime;

        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
        RunnerMouse_beginFrame(runner->mouse);
        if (sdl2HandleEvents(runner)) {
            shouldExit = true;
            continue;
        }

        //double stepTime = 0.0;
        //double audioTime = 0.0;
        if (shouldStep) {
            // Go to next room
            if (runner->debugMode) {
                if (RunnerGamepad_buttonCheck(runner->gamepads, 0, GP_PADD) && RunnerGamepad_buttonCheckPressed(runner->gamepads, 0, GP_START)) {
                    DataWin* dw = runner->dataWin;
                    if ((int32_t) dw->gen8.roomOrderCount > runner->currentRoomOrderPosition + 1) {
                        int32_t nextIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition + 1];
                        runner->pendingRoom = nextIdx;
                        runner->audioSystem->vtable->stopAll(runner->audioSystem);
                        logDebug("Debug: Going to next room -> %s\n", dw->room.rooms[nextIdx].name);
                    }
                }
                // Go to previous room
                if (RunnerGamepad_buttonCheck(runner->gamepads, 0, GP_PADU) && RunnerGamepad_buttonCheckPressed(runner->gamepads, 0, GP_START)) {
                    DataWin* dw = runner->dataWin;
                    if (runner->currentRoomOrderPosition > 0) {
                        int32_t prevIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition - 1];
                        runner->pendingRoom = prevIdx;
                        runner->audioSystem->vtable->stopAll(runner->audioSystem);
                        logDebug("Debug: Going to previous room -> %s\n", dw->room.rooms[prevIdx].name);
                    }
                }
            }

            Runner_step(runner);
            float dt = (float)runner->deltaTime;
            if (0.0f > dt) dt = 0.0f;
            if (dt > 0.1f) dt = 0.1f;
            runner->audioSystem->vtable->update(runner->audioSystem, dt);
        }
        
        // taken from desktop/main.c
        if (runner->pendingWorkingDirectory != NULL) {
            logInfo("game_change has been called! (%s, %s)\n", runner->pendingWorkingDirectory, runner->pendingLaunchParameters ? runner->pendingLaunchParameters : "NULL");

            char** newArguments = nullptr;
            newArguments = extractRunnerArguments(runner->pendingLaunchParameters);

            char* dataWinFilename = nullptr;
            {
                // After extraction, we now need to figure out where is the "-game" argument
                size_t length = arrlen(newArguments);
                repeat(length, i) {
                    if (strcmp(newArguments[i], "-game") == 0) {
                        // So we already know that the data.win file will be the NEXT one
                        if (length - 1 == i)
                            break; // Where's the value?? Bailing...

                        dataWinFilename = safeStrdup(newArguments[i + 1]);
                        break;
                    }
                }
            }

            if (dataWinFilename == nullptr) {
                logError("No data.win... bailing!\n");
                free(dataWinFilename);
                goto free_butterscotch;
                return;
            } else {
                char* parentDir = safeStrdup(safePath);
                {
                    char* lastSlash = strrchr(parentDir, '/');
                    char* lastBackslash = strrchr(parentDir, '\\');
                    char* sep = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
                    if (sep != nullptr) {
                        *sep = '\0';
                    } else {
                        parentDir[0] = '.';
                        parentDir[1] = '\0';
                    }
                }
                size_t newPathLen = strlen(parentDir) + strlen(runner->pendingWorkingDirectory) + 1 + strlen(dataWinFilename) + 1;
                pendingDataWinPath = (char *)safeMalloc(newPathLen);
                snprintf(pendingDataWinPath, newPathLen, "%s%s/%s", parentDir, runner->pendingWorkingDirectory, dataWinFilename);
                free(parentDir);
            }
            goto free_butterscotch;
            return;
        }
        // taken from desktop/main.c

        glBindFramebuffer(GL_FRAMEBUFFER, *hostFramebuffer);
        //glClear(GL_COLOR_BUFFER_BIT);

        // Query actual framebuffer size
        int32_t fbWidth = 960, fbHeight = 544;
        sdl2GetWindowSize(&fbWidth, &fbHeight);

        if (!runner->appSurfaceEnabled) {
            runner->applicationWidth = fbWidth;
            runner->applicationHeight = fbHeight;
            runner->usingAppSurface = false;
        } else {
            if (runner->applicationWidth <= 0 || runner->applicationHeight <= 0) {
                runner->applicationWidth = (int32_t) gen8->defaultWindowWidth;
                runner->applicationHeight = (int32_t) gen8->defaultWindowHeight;
            }
            runner->usingAppSurface = true;
        }

        int32_t gameW = runner->applicationWidth;
        int32_t gameH = runner->applicationHeight;

        Runner_drawPre(runner, fbWidth, fbHeight);

        int32_t winW = 960, winH = 544;
        sdl2GetScaledWindowSize(&winW, &winH);

        Runner_beginFrame(runner, gameW, gameH, winW, winH, fbWidth, fbHeight);

        double mx, my;
        sdl2GetMousePos(&mx, &my);
        Runner_updateMousePosition(runner, winW, winH, mx, my);

        Runner_drawViews(runner, gameW, gameH, false);
        renderer->vtable->endFrameInit(renderer);
        Runner_drawPost(runner, fbWidth, fbHeight);
        renderer->vtable->endFrameEnd(renderer);
        Runner_drawGUI(runner, fbWidth, fbHeight, gameW, gameH);

        if (runner->pendingRoom == -1) {
            SDL_GL_SwapWindow(sdlWindow);
        }
        Runner_handlePendingRoomChange(runner);

        if (runner->currentRoom->speed > 0) {
            double targetFrameTime = 1.0 / runner->currentRoom->speed;
            double nextFrameTime = lastFrameStartTime + targetFrameTime;
            while (osTime() < nextFrameTime) {
                YIELD();
            }
        }
    }

free_butterscotch:
    free(safePath);
    Runner_free(runner);
    OverlayFileSystem_destroy(overlayFs);
#ifdef ENABLE_VM_OPCODE_PROFILER
    VM_printOpcodeProfilerReport(vm);
#endif
    VM_free(vm);
    runner->audioSystem->vtable->destroy(runner->audioSystem);
    runner->audioSystem = nullptr;
    renderer->vtable->destroy(renderer);
    sdl2Close();
    DataWin_free(dataWin);
}

int main(void) {
    fsdevMountSdmc();

    mkdir(GAME_DATA_PATH, 0777);

    if (!fileExists(GAME_DATA_WIN_PATH)) {
        logError("Could not find %s\n", GAME_DATA_WIN_PATH);
        logError("Please put your game's data.win in sdmc:/data/butterscotch/ and restart!\n");
        return 0;
    }

loop_start:
    loop(pendingDataWinPath ? pendingDataWinPath : GAME_DATA_WIN_PATH);
    if (pendingDataWinPath) goto loop_start;
    return 0;
}