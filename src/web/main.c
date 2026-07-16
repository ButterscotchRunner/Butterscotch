#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/wasmfs.h>
#include <GLES3/gl3.h>

#include "data_win.h"
#include "noop_audio_system.h"
#include "web_audio_system.h"
#include "overlay_file_system.h"
#include "runner.h"
#include "gl/gl_renderer.h"
#include "gettime.h"

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
static Runner* gRunner = NULL;
static WebAudioSystem* gWebAudio = NULL;
static int32_t gAudioSampleRate = 48000;

static double gLastFrameStartMs = 0.0;
static int gFrameLoopActive = 0;

uint8_t keyDown[GML_KEY_COUNT] = {0};
uint8_t keyUp[GML_KEY_COUNT] = {0};

static void frameTick(void* arg);
static void cleanupRunner(void);
static void startFrameLoop(void);
static void setWindowTitle(const char* title);
static void postHostMessage(const char* type, const char* title);

// Configures the sample rate that miniaudio will mix at. Must match the AudioContext's sampleRate
// on the JS side, and must be called BEFORE startRunner.
void setAudioSampleRate(int32_t rate) {
    if (rate > 0) {
        gAudioSampleRate = rate;
    }
}

// Pulls frameCount interleaved-stereo float32 frames into outPtr (which must point into wasm memory).
// Called from JS by the audio pull loop. Safe to call before the runner starts (returns silence).
void pullAudioFrames(float* outPtr, int32_t frameCount) {
    if (gWebAudio == NULL || frameCount <= 0) {
        if (outPtr != NULL && frameCount > 0) {
            memset(outPtr, 0, (size_t)frameCount * 2 * sizeof(float));
        }
        return;
    }

    WebAudioSystem_pullFrames(gWebAudio, outPtr, frameCount);
}

uint8_t* getKeyDownPtr() {
    return keyDown;
}

uint8_t* getKeyUpPtr() {
    return keyUp;
}

int getKeyCount() {
    return GML_KEY_COUNT;
}

int main(void) {
    printf("Butterscotch Web single-thread build ready.\n");
    emscripten_exit_with_live_runtime();
    return 0;
}

// Mounts the browser's OPFS at "/butterscotch" in the WASMFS virtual filesystem.
int mountOpfs(void) {
    backend_t opfs = wasmfs_create_opfs_backend();
    if (!opfs) {
        fprintf(stderr, "Failed to create OPFS backend\n");
        return -1;
    }

    int rc = wasmfs_create_directory("/butterscotch", 0777, opfs);
    if (rc != 0) {
        fprintf(stderr, "Failed to mount OPFS at /butterscotch: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

// mkdir -p for WASMFS paths. Used to ensure the saves directory exists before the runner tries to write into it.
static int mkdirP(const char* path) {
    char buf[512];
    size_t len = strlen(path);

    if (len >= sizeof(buf)) {
        return -1;
    }

    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            buf[i] = '/';
        }
    }

    if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static void postHostMessage(const char* type, const char* title) {
    EM_ASM({
        const payload = { type: UTF8ToString($0) };

        if ($1) {
            payload.title = UTF8ToString($1);
        }

        try {
            if (typeof window !== 'undefined' &&
                window.parent &&
                window.parent !== window &&
                typeof window.parent.postMessage === 'function') {
                window.parent.postMessage(payload, '*');
            } else if (typeof self !== 'undefined' && typeof self.postMessage === 'function') {
                self.postMessage(payload);
            } else if (typeof postMessage === 'function') {
                try { postMessage(payload); } catch (_) {}
            }
        } catch (_) {}
    }, type, title);
}

static void cleanupRunner(void) {
    if (gRunner == NULL) {
        return;
    }

    fprintf(stderr, "Cleaning up runner!\n");

    if (gRunner->audioSystem != NULL) {
        gRunner->audioSystem->vtable->destroy(gRunner->audioSystem);
        gRunner->audioSystem = NULL;
    }

    gWebAudio = NULL;

    if (gRunner->renderer != NULL) {
        gRunner->renderer->vtable->destroy(gRunner->renderer);
    }

    DataWin* dataWin = gRunner->dataWin;
    VMContext* vm = gRunner->vmContext;

    Runner_free(gRunner);
    gRunner = NULL;

    VM_free(vm);
    DataWin_free(dataWin);

    postHostMessage("runnerExit", NULL);
}

static void frameTick(void* arg) {
    (void)arg;

    if (gRunner == NULL) {
        gFrameLoopActive = 0;
        return;
    }

    if (gRunner->shouldExit) {
        gFrameLoopActive = 0;
        cleanupRunner();
        return;
    }

    double frameStartMs = emscripten_get_now();
    gRunner->deltaTime = (frameStartMs - gLastFrameStartMs) * 1000.0;
    gLastFrameStartMs = frameStartMs;

    RunnerKeyboard_beginFrame(gRunner->keyboard);

    // Process inputs
    repeat(GML_KEY_COUNT, i) {
        if (keyDown[i]) {
            RunnerKeyboard_onKeyDown(gRunner->keyboard, i);
            keyDown[i] = 0;
        }

        if (keyUp[i]) {
            RunnerKeyboard_onKeyUp(gRunner->keyboard, i);
            keyUp[i] = 0;
        }
    }

    emscripten_webgl_make_context_current(ctx);

    float audioDt = (float)(gRunner->deltaTime / 1000000.0);
    if (audioDt < 0.0f) {
        audioDt = 0.0f;
    }
    if (audioDt > 0.1f) {
        audioDt = 0.1f;
    }

    if (gRunner->audioSystem != NULL) {
        gRunner->audioSystem->vtable->update(gRunner->audioSystem, audioDt);
    }

    // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
    Runner_step(gRunner);

    int32_t gameW = (int32_t)gRunner->dataWin->gen8.defaultWindowWidth;
    int32_t gameH = (int32_t)gRunner->dataWin->gen8.defaultWindowHeight;

    Runner_drawPre(gRunner, 640, 480);
    Runner_beginFrame(gRunner, gameW, gameH, 640, 480, 640, 480);
    Runner_drawViews(gRunner, gameW, gameH, false);
    gRunner->renderer->vtable->endFrameInit(gRunner->renderer);
    Runner_drawPost(gRunner, 640, 480);
    gRunner->renderer->vtable->endFrameEnd(gRunner->renderer);
    Runner_drawGUI(gRunner, 640, 480, gameW, gameH);

    // In single-thread WebGL mode, let the browser present the frame normally.
    glFlush();

    Runner_handlePendingRoomChange(gRunner);

    if (gRunner->shouldExit) {
        gFrameLoopActive = 0;
        cleanupRunner();
        return;
    }

    unsigned int nextDelayMs = 16;
    if (gRunner->currentRoom != NULL && gRunner->currentRoom->speed > 0) {
        double targetFrameTimeMs = 1000.0 / (double)gRunner->currentRoom->speed;
        double nextFrameTimeMs = gLastFrameStartMs + targetFrameTimeMs;
        double remainingMs = nextFrameTimeMs - emscripten_get_now();

        if (remainingMs < 1.0) {
            nextDelayMs = 1;
        } else if (remainingMs > 1000.0) {
            nextDelayMs = 1000;
        } else {
            nextDelayMs = (unsigned int)remainingMs;
        }
    }

    emscripten_async_call(frameTick, NULL, nextDelayMs);
}

static void startFrameLoop(void) {
    if (gFrameLoopActive) {
        return;
    }

    gFrameLoopActive = 1;
    gLastFrameStartMs = emscripten_get_now();
    emscripten_async_call(frameTick, NULL, 0);
}

// gamePath: WASMFS path to the data.win to load (example: "/butterscotch/games/undertale/data.win").
// savesPath: WASMFS directory where saves should live (example: "/butterscotch/saves/undertale" - Created if it does not exist).
void startRunner(const char* gamePath, const char* savesPath) {
    fprintf(stderr, "Starting runner! gamePath=%s savesPath=%s\n", gamePath, savesPath);

    if (gRunner != NULL) {
        fprintf(stderr, "A runner is already active. Cleaning up the previous instance first.\n");
        gRunner->shouldExit = true;
        cleanupRunner();
    }

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.antialias = 0;

    // Keep this build simple: no explicit swap control, no offscreen backbuffer, no worker dependence.
    ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        printf("Failed to create WebGL context: %d\n", (int)ctx);
        return;
    }

    emscripten_webgl_make_context_current(ctx);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Make sure the saves directory exists. The FileSystem impl will write into it.
    if (savesPath != NULL && savesPath[0] != '\0') {
        if (mkdirP(savesPath) != 0) {
            fprintf(stderr, "Warning: failed to ensure saves dir exists at %s: %s\n", savesPath, strerror(errno));
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
    options.parseAudo = true;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = false;
    options.eagerlyLoadedRooms = NULL;

    DataWin* dataWin = DataWin_parse(gamePath, options);
    if (dataWin == NULL) {
        fprintf(stderr, "Failed to parse DataWin: %s\n", gamePath);
        return;
    }

    VMContext* vm = VM_create(dataWin);
    if (vm == NULL) {
        fprintf(stderr, "Failed to create VMContext\n");
        DataWin_free(dataWin);
        return;
    }

    Renderer* renderer = GLRenderer_create();
    if (renderer == NULL) {
        fprintf(stderr, "Failed to create GLRenderer\n");
        VM_free(vm);
        DataWin_free(dataWin);
        return;
    }

    // Bundle path = directory containing data.win, e.g. "/butterscotch/games/undertale/".
    // Save path = whatever was passed in, e.g. "/butterscotch/saves/undertale/".
    char* bundleDir = NULL;
    const char* lastSlash = strrchr(gamePath, '/');
    if (lastSlash != NULL) {
        size_t len = (size_t)(lastSlash - gamePath + 1);
        bundleDir = (char*)safeMalloc(len + 1);
        memcpy(bundleDir, gamePath, len);
        bundleDir[len] = '\0';
    } else {
        bundleDir = safeStrdup("./");
    }

    OverlayFileSystem* overlayFs = OverlayFileSystem_create(bundleDir, savesPath);
    free(bundleDir);

    if (overlayFs == NULL) {
        fprintf(stderr, "Failed to create overlay filesystem\n");
        renderer->vtable->destroy(renderer);
        VM_free(vm);
        DataWin_free(dataWin);
        return;
    }

    gWebAudio = WebAudioSystem_create(dataWin, gAudioSampleRate);
    if (gWebAudio == NULL) {
        fprintf(stderr, "Failed to create WebAudioSystem\n");
        renderer->vtable->destroy(renderer);
        VM_free(vm);
        DataWin_free(dataWin);
        return;
    }

    AudioSystem* audioSystem = (AudioSystem*)gWebAudio;

    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*)overlayFs, audioSystem);
    if (runner == NULL) {
        fprintf(stderr, "Failed to create runner\n");
        gWebAudio = NULL;
        renderer->vtable->destroy(renderer);
        VM_free(vm);
        DataWin_free(dataWin);
        return;
    }

    runner->setWindowTitle = setWindowTitle;
    runner->windowHasFocus = NULL;

    gRunner = runner;

    setWindowTitle(dataWin->gen8.name);

    // Initialize the first room and fire Game Start / Room Start events.
    Runner_initFirstRoom(runner);

    // Start the single-thread frame loop.
    startFrameLoop();
}

void stopRunner(void) {
    fprintf(stderr, "Marked runner to exit!\n");
    if (gRunner != NULL) {
        gRunner->shouldExit = true;
    }
}

static void setWindowTitle(const char* title) {
    postHostMessage("windowTitle", title);
}
