#include "data_win.h"
#include "ps3gl.h"
#include "rsxutil.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include "runner_keyboard.h"
#include "runner.h"
#include "input_recording.h"
#include "debug_overlay.h"
#include "gl_legacy_renderer.h"
#include "overlay_file_system.h"
#include "al_audio_system.h"
#include "stb_ds.h"
#include "stb_image_write.h"

#include "utils.h"
#include "profiler.h"

#include <io/pad.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#include <ppu_intrinsics.h>

typedef struct {
    uint8_t digital;
    uint8_t mask;
    int32_t gmlKey;
} PadMapping;

const PadMapping PAD_MAPPINGS[] = {
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_UP,       VK_UP },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_DOWN,     VK_DOWN },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_LEFT,     VK_LEFT },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_RIGHT,    VK_RIGHT },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_START,    'C' },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_SELECT,   VK_ESCAPE },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_CROSS,    'Z' },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_SQUARE,   'X' },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_TRIANGLE, 'C' },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_L1,       VK_PAGEDOWN },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_R1,       VK_PAGEUP },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_L2,       VK_F10 },
};
static const int PAD_MAPPING_COUNT = sizeof(PAD_MAPPINGS) / sizeof(PAD_MAPPINGS[0]);
static bool prevState[sizeof(PAD_MAPPINGS) / sizeof(PAD_MAPPINGS[0])] = {0};

#define STICK_CENTER 0x80 // The center of the stick (range 0x00-0xFF)
#define STICK_THRESHOLD 0x40 // The threshold for treating stick movement as a d-pad press

typedef struct {
    uint8_t axis;
    int8_t  sign;
    int32_t gmlKey;
} StickMapping;

const StickMapping STICK_MAPPINGS[] = {
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_X, -1, VK_LEFT  },
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_X, +1, VK_RIGHT },
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_Y, -1, VK_UP    },
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_Y, +1, VK_DOWN  },
};
static const int STICK_MAPPING_COUNT = sizeof(STICK_MAPPINGS) / sizeof(STICK_MAPPINGS[0]);
static bool prevStickState[sizeof(STICK_MAPPINGS) / sizeof(STICK_MAPPINGS[0])] = {0};

// ===[ MAIN ]===
static double freq = 0; 
#define PS3_GET_TIME ((double)__builtin_ppc_get_timebase() / (double)freq)
bool shouldExit = false;

// ===[ MAIN ]===

static void sys_callback(uint64_t status, uint64_t param, void* userdata) {
    switch (status) {
        case SYSUTIL_EXIT_GAME:
            shouldExit = true;
            break;
        
        case SYSUTIL_MENU_OPEN:
        case SYSUTIL_MENU_CLOSE:
            break;

        default:
            break;
    }
}

// Source - https://stackoverflow.com/a/779960
// Posted by jmucchiello, modified by community. See post 'Timeline' for change history
// Retrieved 2026-05-10, License - CC BY-SA 4.0

// You must free the result if result is non-NULL.
char *str_replace(char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

static char buffer[9999];
int main(int argc, char* argv[]) {
    printf("%s\n", argv[0]);
    strcpy(buffer, argv[0]);
    char* tmp = str_replace(buffer, "butterscotch.elf", "");
    char* tmp2 = str_replace(buffer, "EBOOT.BIN", "");
    char* dataWinPath = malloc(strlen(tmp2) + strlen("data.win") + 1);
    if (!dataWinPath) {
        free(tmp);
        return 1;
    }
    strcpy(dataWinPath, tmp2);
    strcat(dataWinPath, "data.win");
    free(tmp);
    free(tmp2);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_callback, NULL);
    freq = sysGetTimebaseFrequency();

    printf("Loading %s...\n", dataWinPath);

    DataWin* dataWin = DataWin_parse(
        dataWinPath,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseExtn = false,
            .parseSond = true,
            .parseAgrp = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parsePath = true,
            .parseScpt = true,
            .parseGlob = true,
            .parseShdr = true,
            .parseFont = true,
            .parseTmln = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTpag = true,
            .parseCode = true,
            .parseVari = true,
            .parseFunc = true,
            .parseStrg = true,
            .parseTxtr = true,
            .parseAudo = true,
            .skipLoadingPreciseMasksForNonPreciseSprites = true,
            .lazyLoadRooms = true,
            //.eagerlyLoadedRooms = args.eagerRooms
        }
    );

    Gen8* gen8 = &dataWin->gen8;
    printf("Loaded \"%s\" (%d) successfully! [Bytecode Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->bytecodeVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);

    // Initialize VM
    VMContext* vm = VM_create(dataWin);

    Profiler_setEnabled(&vm->profiler, false);
#ifdef ENABLE_VM_OPCODE_PROFILER
    vm->opcodeProfilerEnabled = true;
    if (vm->opcodeProfilerEnabled) {
        vm->opcodeVariantCounts = safeCalloc(256 * 256, sizeof(uint64_t));
        vm->opcodeRValueTypeCounts = safeCalloc(256 * 256, sizeof(uint64_t));
    }
#endif

    // Initialize the file system
    char* dataWinDir = nullptr;
    {
        const char* lastSlash = strrchr(dataWinPath, '/');
        const char* lastBackslash = strrchr(dataWinPath, '\\');
        if (lastBackslash != nullptr && (lastSlash == nullptr || lastBackslash > lastSlash))
            lastSlash = lastBackslash;
        if (lastSlash != nullptr) {
            size_t len = (size_t) (lastSlash - dataWinPath + 1);
            dataWinDir = safeMalloc(len + 1);
            memcpy(dataWinDir, dataWinPath, len);
            dataWinDir[len] = '\0';
        } else {
            dataWinDir = safeStrdup("./");
        }
    }
    const char* savePath = dataWinDir;
    OverlayFileSystem* overlayFs = OverlayFileSystem_create(dataWinDir, savePath);

    // Init GLFW
    ps3glInit();
    ioPadInit(7);

    // Initialize the renderer
    Renderer* renderer = GLLegacyRenderer_create();

    // Initialize the audio system
    AudioSystem* audioSystem = (AudioSystem*) AlAudioSystem_create();

    // Initialize the runner
    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->debugMode = false;
    //runner->osType = OS_PS3;

    // Initialize the first room and fire Game Start / Room Start events
    Runner_initFirstRoom(runner);

    // Main loop
    bool debugPaused = false;
    bool debugShowCollisionMasks = false;
    double lastFrameTime = PS3_GET_TIME;
    while (!shouldExit && !runner->shouldExit) {
        // Clear last frame's pressed/released state, then poll new input events
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);


        // Run the game step if the game is paused
        bool shouldStep = true;
        if (runner->debugMode && debugPaused) {
            shouldStep = RunnerKeyboard_checkPressed(runner->keyboard, 'O');
            if (shouldStep) fprintf(stderr, "Debug: Frame advance (frame %d)\n", runner->frameCount);
        }


        padInfo padinfo;
        ioPadGetInfo(&padinfo);

        if (padinfo.status[0])
        {
            padData paddata;
            ioPadGetData(0, &paddata);

            // "The padData structure is only filled if there is a change in input since the last call.
            // If there is no change, the structure is zero-filled. If the len member is zero, there was no new input."
            // So we'll check if there WAS a change before trying to process the keys, to avoid releasing the keys on every frame.
            // -ioPadGetData
            if (paddata.len > 0) {
                repeat(PAD_MAPPING_COUNT, i) {
                    uint8_t byte = (uint8_t) paddata.button[PAD_MAPPINGS[i].digital];
                    uint8_t mask = PAD_MAPPINGS[i].mask;
                    int32_t gmlKey = PAD_MAPPINGS[i].gmlKey;

                    bool isPressed = (byte & mask) != 0;
                    bool wasPressed = prevState[i];

                    if (isPressed && !wasPressed) {
                        RunnerKeyboard_onKeyDown(runner->keyboard, gmlKey);
                    } else if (!isPressed && wasPressed) {
                        RunnerKeyboard_onKeyUp(runner->keyboard, gmlKey);
                    }

                    prevState[i] = isPressed;
                }

                repeat(STICK_MAPPING_COUNT, i) {
                    int axisValue = (int) paddata.button[STICK_MAPPINGS[i].axis];
                    int signedDelta = STICK_MAPPINGS[i].sign * (axisValue - STICK_CENTER);

                    bool isPressed = signedDelta > STICK_THRESHOLD;
                    bool wasPressed = prevStickState[i];
                    int32_t gmlKey = STICK_MAPPINGS[i].gmlKey;

                    if (isPressed && !wasPressed) {
                        RunnerKeyboard_onKeyDown(runner->keyboard, gmlKey);
                    } else if (!isPressed && wasPressed) {
                        RunnerKeyboard_onKeyUp(runner->keyboard, gmlKey);
                    }

                    prevStickState[i] = isPressed;
                }
            }
        }

        double frameStartTime = PS3_GET_TIME;
        if (shouldStep) {
            // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
            Runner_step(runner);

            // Update audio system (gain fading, cleanup ended sounds)
            float dt = (float) (PS3_GET_TIME - lastFrameTime);
            if (0.0f > dt) dt = 0.0f;
            if (dt > 0.1f) dt = 0.1f; // cap delta to avoid huge fades on lag spikes
            runner->audioSystem->vtable->update(runner->audioSystem, dt);
        }

        // Query actual framebuffer size (differs from window size on Wayland with fractional scaling)
        int fbWidth = display_width, fbHeight = display_height;

        // Clear the default framebuffer (window background) to black
        glClear(GL_COLOR_BUFFER_BIT);

        int32_t gameW = (int32_t) gen8->defaultWindowWidth;
        int32_t gameH = (int32_t) gen8->defaultWindowHeight;

        // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
        // It is a bit hard to understand, but here's how it works:
        // The Port X/Port Y controls the position of the game viewport within the application surface.
        // The Port W/Port H controls the size of the game viewport within the application surface.
        // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
        // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
        float displayScaleX;
        float displayScaleY;

        Runner_computeViewDisplayScale(runner, gameW, gameH, &displayScaleX, &displayScaleY);

        renderer->vtable->beginFrame(renderer, gameW, gameH, fbWidth, fbHeight);

        // Clear FBO with room background color
        if (runner->drawBackgroundColor) {
            int rInt = BGR_R(runner->backgroundColor);
            int gInt = BGR_G(runner->backgroundColor);
            int bInt = BGR_B(runner->backgroundColor);
            glClearColor(rInt / 255.0f, gInt / 255.0f, bInt / 255.0f, 1.0f);
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT);

        Runner_drawViews(runner, gameW, gameH, displayScaleX, displayScaleY, debugShowCollisionMasks);

        renderer->vtable->endFrame(renderer);

        sysUtilCheckCallback();
        // Only swap when there isn't a room change to match the original runner.
        if (runner->pendingRoom == -1) {
            ps3glSwapBuffers();
        }
        Runner_handlePendingRoomChange(runner);

        double now = PS3_GET_TIME;

        // Limit frame rate to room speed
        if (runner->currentRoom->speed > 0) {
            double targetFrameTime = 1.0 / runner->currentRoom->speed;
            double nextFrameTime = lastFrameTime + targetFrameTime;

            if (now < nextFrameTime) {
                while (PS3_GET_TIME < nextFrameTime) {
                    __sync();
                    sysUtilCheckCallback();
                    sysUsleep(5);
                }
                lastFrameTime = nextFrameTime;
            } else {
                // Frame took too long → resync
                lastFrameTime = now;
            }
        } else {
            lastFrameTime = now;
        }
    }


    // Cleanup
    runner->audioSystem->vtable->destroy(runner->audioSystem);
    runner->audioSystem = nullptr;
    renderer->vtable->destroy(renderer);

    Runner_free(runner);
    OverlayFileSystem_destroy(overlayFs);
#ifdef ENABLE_VM_OPCODE_PROFILER
    VM_printOpcodeProfilerReport(vm);
#endif
    VM_free(vm);
    DataWin_free(dataWin);

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	gcmSetWaitFlip(context);
	rsxFinish(context,1);
    printf("Bye! :3\n");
    return 0;
}
