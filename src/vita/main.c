#include "data_win.h"
#include "runner_gamepad.h"
#include "vm.h"

#include "gl_renderer.h"
#include "gl_legacy_renderer.h"
#include "overlay_file_system.h"
#include "gl_common.h"
#include "utils.h"

#include "vita_textures.h"

#if defined(USE_OPENAL) 
#include "al_audio_system.h"
#elif defined(USE_MINIAUDIO)
#include "ma_audio_system.h"
#endif
#include "noop_audio_system.h"

#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h> 
#include <psp2/kernel/processmgr.h>

#include <stdio.h>

#define GAME_DATA_PATH "ux0:data/butterscotch/"
#define GAME_DATA_WIN_PATH GAME_DATA_PATH "data.win"

const GLuint *hostFramebuffer;
SceCtrlData pad = {0};

// for game_change
char* pendingDataWinPath = NULL;

int32_t currentWindowWidth = 0;
int32_t currentWindowHeight = 0;

double osTime() {
    return (double)sceKernelGetProcessTimeWide() / 1000000.0;
}
static float stickByteToFloat(unsigned char raw) {
    return ((float) raw - 128.0f) * (1.0f / 127.5f);
}
void handleVitaGamepad(RunnerGamepadState* gp, int port) {
    GamepadSlot* slot = &gp->slots[port];
    
    memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));

    if (pad.buttons & SCE_CTRL_CROSS) slot->buttonDown[0] = true;
    if (pad.buttons & SCE_CTRL_CIRCLE) slot->buttonDown[1] = true;
    if (pad.buttons & SCE_CTRL_SQUARE) slot->buttonDown[2] = true;
    if (pad.buttons & SCE_CTRL_TRIANGLE) slot->buttonDown[3] = true;

    if (pad.buttons & SCE_CTRL_L1) slot->buttonDown[4] = true;
    if (pad.buttons & SCE_CTRL_R1) slot->buttonDown[5] = true;
    //if (pad.buttons & SCE_CTRL_L2) slot->buttonDown[6] = true;
    //if (pad.buttons & SCE_CTRL_R2) slot->buttonDown[7] = true;

    if (pad.buttons & SCE_CTRL_SELECT) slot->buttonDown[8] = true;
    if (pad.buttons & SCE_CTRL_START) slot->buttonDown[9] = true;
    if (pad.buttons & SCE_CTRL_PSBUTTON) slot->buttonDown[16] = true;

    if (pad.buttons & SCE_CTRL_L3) slot->buttonDown[10] = true;
    if (pad.buttons & SCE_CTRL_R3) slot->buttonDown[11] = true;

    if (pad.buttons & SCE_CTRL_UP) slot->buttonDown[12] = true;
    if (pad.buttons & SCE_CTRL_DOWN) slot->buttonDown[13] = true;
    if (pad.buttons & SCE_CTRL_LEFT) slot->buttonDown[14] = true;
    if (pad.buttons & SCE_CTRL_RIGHT) slot->buttonDown[15] = true;

    float lx = stickByteToFloat(pad.lx);
    float ly = stickByteToFloat(pad.ly);
    float rx = stickByteToFloat(pad.rx);
    float ry = stickByteToFloat(pad.ry);
    slot->axisValue[0] = lx;
    slot->axisValue[1] = ly;
    slot->axisValue[2] = rx;
    slot->axisValue[3] = ry;

    for (int i = 0; GP_BUTTON_COUNT > i; i++) {
        slot->buttonValue[i] = slot->buttonDown[i] ? 1.0f : 0.0f;
    }

    if (!slot->connected) {
        snprintf(slot->description, sizeof(slot->description), "PlayStation Vita");
        slot->guid[0] = '\0';
        slot->jid = port;
    }
    slot->connected = true;

    for (int btn = 0; GP_BUTTON_COUNT > btn; btn++) {
        bool wasDown = slot->buttonDownPrev[btn];
        if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
        if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
    }
    gp->connectedCount++;
}

bool vitaGetWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH) return false;
    if (currentWindowWidth <= 0 || currentWindowHeight <= 0) return false;
    *outW = currentWindowWidth;
    *outH = currentWindowHeight;
    return true;
}
void vitaSetWindowSize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    currentWindowWidth = width;
    currentWindowHeight = height;
}


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
    sceClibPrintf("Loading %s...\n", safePath);
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
    int texBinPathLen = strlen(bundleDir) + strlen("/textures.bin") + 1;
    char* texBinPath = (char*)safeMalloc(texBinPathLen);
    snprintf(texBinPath, texBinPathLen, "%s%s", bundleDir, "/textures.bin");
    if (fileExists(texBinPath)) {
        options.parseTxtr = false;
        if (!VitaTextures_Init(texBinPath)) {
            fprintf(stderr, "FATAL: failed to load %s\n", texBinPath);
            return;
        }
    }
    free(texBinPath);

#if defined(USE_MINIAUDIO) || defined(USE_OPENAL)
    options.parseAudo = true;
#endif
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = true;
    options.lazyLoadTextures = true;
    options.lazyLoadAudio = true;

    bool forceLegacyGL = false;

    DataWin* dataWin = DataWin_parse(safePath, options);
    Gen8* gen8 = &dataWin->gen8;
    sceClibPrintf("Loaded \"%s\" (%d) successfully! [WAD Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->wadVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);

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

    Renderer* renderer = NULL;
    if (forceLegacyGL)
        renderer = GLLegacyRenderer_create();
    else
        renderer = GLRenderer_create();
    hostFramebuffer = &((GLRenderer *)renderer)->hostFramebuffer;

    if (!renderer) {
        sceClibPrintf("Failed to initialize a renderer\n");
        DataWin_free(dataWin);
        VitaTextures_Free();
        return;
    }

#if defined(USE_OPENAL)
    AudioSystem* audioSystem = (AudioSystem*) AlAudioSystem_create();
#elif defined(USE_MINIAUDIO)
    AudioSystem* audioSystem = (AudioSystem*) MaAudioSystem_create(dataWin);
#else
    AudioSystem* audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif

    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->debugMode = true; // for now
    runner->setWindowSize = vitaSetWindowSize;
    runner->getWindowSize = vitaGetWindowSize;
    Runner_initFirstRoom(runner);

    sceClibPrintf("Runner successfully created and inited first room!!\n");

    int32_t gameW = (int32_t) gen8->defaultWindowWidth;
    int32_t gameH = (int32_t) gen8->defaultWindowHeight;

    double lastFrameStartTime = osTime();

    while(!runner->shouldExit) {
        RunnerKeyboard_beginFrame(runner->keyboard);

        RunnerGamepad_beginFrame(runner->gamepads);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        handleVitaGamepad(runner->gamepads, 0);

        bool shouldStep = true;

        double frameStartTime = osTime();
        runner->deltaTime = (frameStartTime - lastFrameStartTime);
        lastFrameStartTime = frameStartTime;

        //double stepTime = 0.0;
        //double audioTime = 0.0;
        if (shouldStep) {
            // Go to next room
            if (runner->debugMode) {
                if (RunnerGamepad_buttonCheck(runner->gamepads, 0, GP_PADR) && RunnerGamepad_buttonCheckPressed(runner->gamepads, 0, GP_START)) {
                    DataWin* dw = runner->dataWin;
                    if ((int32_t) dw->gen8.roomOrderCount > runner->currentRoomOrderPosition + 1) {
                        int32_t nextIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition + 1];
                        runner->pendingRoom = nextIdx;
                        runner->audioSystem->vtable->stopAll(runner->audioSystem);
                        fprintf(stderr, "Debug: Going to next room -> %s\n", dw->room.rooms[nextIdx].name);
                    }
                }
                // Go to previous room
                if (RunnerGamepad_buttonCheck(runner->gamepads, 0, GP_PADL) && RunnerGamepad_buttonCheckPressed(runner->gamepads, 0, GP_START)) {
                    DataWin* dw = runner->dataWin;
                    if (runner->currentRoomOrderPosition > 0) {
                        int32_t prevIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition - 1];
                        runner->pendingRoom = prevIdx;
                        runner->audioSystem->vtable->stopAll(runner->audioSystem);
                        fprintf(stderr, "Debug: Going to previous room -> %s\n", dw->room.rooms[prevIdx].name);
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
            sceClibPrintf("game_change has been called! (%s, %s)\n", runner->pendingWorkingDirectory, runner->pendingLaunchParameters ? runner->pendingLaunchParameters : "NULL");

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
                sceClibPrintf("No data.win... bailing!\n");
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
        glClear(GL_COLOR_BUFFER_BIT);

        int32_t fbWidth = 960;
        int32_t fbHeight = 544;
        gameW = runner->applicationWidth;
        gameH = runner->applicationHeight;
        
        Runner_drawPre(runner, fbWidth, fbHeight);
        Runner_beginFrame(runner, gameW, gameH, currentWindowWidth, currentWindowHeight, fbWidth, fbHeight);
        Runner_drawViews(runner, gameW, gameH, false);
        renderer->vtable->endFrameInit(renderer);
        Runner_drawPost(runner, fbWidth, fbHeight);
        renderer->vtable->endFrameEnd(renderer);
        Runner_drawGUI(runner, fbWidth, fbHeight, gameW, gameH);

        if (runner->pendingRoom == -1) {
            vglSwapBuffers(GL_FALSE);
        }
        Runner_handlePendingRoomChange(runner);

        if (runner->currentRoom->speed > 0) {
            double targetFrameTime = 1.0 / runner->currentRoom->speed;
            double nextFrameTime = lastFrameStartTime + targetFrameTime;
            while (osTime() < nextFrameTime) {
                sceKernelCheckCallback();
                sceKernelDelayThread(5);
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
    DataWin_free(dataWin);
    VitaTextures_Free();
}

int main() {
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
    vglSetupGarbageCollector(127, 0x20000);
    // let's try to get as much ram as possible
    vglSetParamBufferSize(2 * 1024 * 1024);
    vglInitWithCustomThreshold(0, 960, 544, 12 * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

loop_start:
    loop(pendingDataWinPath ? pendingDataWinPath : GAME_DATA_WIN_PATH);
    if (pendingDataWinPath) goto loop_start;
    return 0;
}