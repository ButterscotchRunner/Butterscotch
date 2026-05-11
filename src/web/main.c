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

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
static Runner* gRunner;
static WebAudioSystem* gWebAudio = nullptr;
static int32_t gAudioSampleRate = 48000;

uint8_t keyDown[GML_KEY_COUNT] = {0};
uint8_t keyUp[GML_KEY_COUNT] = {0};

// Configures the sample rate that miniaudio will mix at. Must match the AudioContext's sampleRate
// on the JS side, and must be called BEFORE startRunner.
void setAudioSampleRate(int32_t rate) {
    if (rate > 0) gAudioSampleRate = rate;
}

// Pulls frameCount interleaved-stereo float32 frames into outPtr (which must point into wasm memory).
// Called from JS by the worker's audio pull loop. Safe to call before the runner starts (returns silence).
void pullAudioFrames(float* outPtr, int32_t frameCount) {
    if (gWebAudio == nullptr || frameCount <= 0) {
        if (outPtr != nullptr && frameCount > 0) {
            memset(outPtr, 0, (size_t) frameCount * 2 * sizeof(float));
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

int main() {
    printf("Howdy! Loritta is so cute! lol\n");
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
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; len > i; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

void* loop() {
    double lastFrameTimeMs = emscripten_get_now();

    while (!gRunner->shouldExit) {
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

        double nowMs = emscripten_get_now();
        float audioDt = (float) ((nowMs - lastFrameTimeMs) / 1000.0);
        if (0.0f > audioDt) audioDt = 0.0f;
        if (audioDt > 0.1f) audioDt = 0.1f;
        gRunner->audioSystem->vtable->update(gRunner->audioSystem, audioDt);

        // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
        Runner_step(gRunner);

        int32_t gameW = (int32_t) gRunner->dataWin->gen8.defaultWindowWidth;
        int32_t gameH = (int32_t) gRunner->dataWin->gen8.defaultWindowHeight;

        // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
        // It is a bit hard to understand, but here's how it works:
        // The Port X/Port Y controls the position of the game viewport within the application surface.
        // The Port W/Port H controls the size of the game viewport within the application surface.
        // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
        // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
        float displayScaleX;
        float displayScaleY;

        Runner_computeViewDisplayScale(gRunner, gameW, gameH, &displayScaleX, &displayScaleY);

        gRunner->renderer->vtable->beginFrame(gRunner->renderer, gameW, gameH, 640, 480);

        // Clear FBO with room background color
        if (gRunner->drawBackgroundColor) {
            int rInt = BGR_R(gRunner->backgroundColor);
            int gInt = BGR_G(gRunner->backgroundColor);
            int bInt = BGR_B(gRunner->backgroundColor);
            glClearColor(rInt / 255.0f, gInt / 255.0f, bInt / 255.0f, 1.0f);
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT);

        Runner_drawViews(gRunner, gameW, gameH, displayScaleX, displayScaleY, false);

        gRunner->renderer->vtable->endFrame(gRunner->renderer);

        // Just like glfwSwapBuffers
        emscripten_webgl_commit_frame();
        Runner_handlePendingRoomChange(gRunner);

        // Frame pacing: sleep until the next frame is due, based on the room's speed.
        // emscripten_get_now() returns milliseconds (performance.now()) and works in workers.
        if (gRunner->currentRoom != nullptr && gRunner->currentRoom->speed > 0) {
            double targetFrameTimeMs = 1000.0 / (double) gRunner->currentRoom->speed;
            double nextFrameTimeMs = lastFrameTimeMs + targetFrameTimeMs;
            double remainingMs = nextFrameTimeMs - emscripten_get_now();
            // Sleep for most of the remaining time, then spin-wait for precision.
            if (remainingMs > 2.0) {
                struct timespec ts = {
                    .tv_sec = 0,
                    .tv_nsec = (long) ((remainingMs - 1.0) * 1000000.0)
                };
                nanosleep(&ts, nullptr);
            }
            while (emscripten_get_now() < nextFrameTimeMs) {
                // Spin-wait for the remaining sub-millisecond
            }
            lastFrameTimeMs = nextFrameTimeMs;
        } else {
            lastFrameTimeMs = emscripten_get_now();
        }
    }

    // Cleanup
    fprintf(stderr, "Cleaning up runner!\n");

    gRunner->audioSystem->vtable->destroy(gRunner->audioSystem);
    gRunner->audioSystem = nullptr;
    gWebAudio = nullptr;
    gRunner->renderer->vtable->destroy(gRunner->renderer);

    DataWin* dataWin = gRunner->dataWin;
    VMContext* vm = gRunner->vmContext;
    Runner_free(gRunner);
    VM_free(vm);
    DataWin_free(dataWin);

    // We want to *know* when the runner has actually exited, because we also need to track things like "Leave Game" buttons/actions in the game
    MAIN_THREAD_EM_ASM({ postMessage({ type: 'runnerExit' }); });

    return nullptr;
}

void setWindowTitle(MAYBE_UNUSED void* nativeWindow, const char* title) {
    MAIN_THREAD_EM_ASM({ postMessage({ type: 'windowTitle', title: UTF8ToString($0) }); }, title);
}

// gamePath: WASMFS path to the data.win to load (example: "/butterscotch/games/undertale/data.win").
// savesPath: WASMFS directory where saves should live (example: "/butterscotch/saves/undertale" - Created if it does not exist).
void startRunner(const char* gamePath, const char* savesPath) {
    fprintf(stderr, "Starting runner! gamePath=%s savesPath=%s\n", gamePath, savesPath);

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.antialias = 0; // Required to avoid "WebGL warning: blitFramebuffer: DRAW_FRAMEBUFFER may not have multiple samples."
    // Both of these are required to allow us to use emscripten_webgl_commit_frame
    attrs.explicitSwapControl = true;
    attrs.renderViaOffscreenBackBuffer = true;

    // Yes, "#canvas" feels nasty as HELL
    // But that's how Emscripten works for SOME REASON
    ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (0 >= ctx) {
        printf("Failed to create WebGL context: %d\n", (int)ctx);
        abort();
    }

    emscripten_webgl_make_context_current(ctx);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Make sure the saves directory exists. The FileSystem impl will write into it.
    if (savesPath != nullptr && savesPath[0] != '\0') {
        if (mkdirP(savesPath) != 0) {
            fprintf(stderr, "Warning: failed to ensure saves dir exists at %s: %s\n", savesPath, strerror(errno));
        }
    }

    DataWin* dataWin = DataWin_parse(
        gamePath,
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
            .lazyLoadRooms = false,
            .eagerlyLoadedRooms = nullptr
        }
    );

    // return strdup(dataWin->gen8.name);

    // Initialize VM
    VMContext* vm = VM_create(dataWin);

    Renderer* renderer = GLRenderer_create();

    // Bundle path = directory containing data.win, e.g. "/butterscotch/games/undertale/".
    // Save path = whatever the worker passed in, e.g. "/butterscotch/saves/undertale/".
    char* bundleDir = nullptr;
    const char* lastSlash = strrchr(gamePath, '/');
    if (lastSlash != nullptr) {
        size_t len = (size_t) (lastSlash - gamePath + 1);
        bundleDir = safeMalloc(len + 1);
        memcpy(bundleDir, gamePath, len);
        bundleDir[len] = '\0';
    } else {
        bundleDir = safeStrdup("./");
    }
    OverlayFileSystem* overlayFs = OverlayFileSystem_create(bundleDir, savesPath);
    free(bundleDir);

    gWebAudio = WebAudioSystem_create(gAudioSampleRate);
    AudioSystem* audioSystem = (AudioSystem*) gWebAudio;

    // Initialize the runner
    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->nativeWindow = nullptr;
    runner->setWindowTitle = setWindowTitle;
    runner->windowHasFocus = nullptr;

    setWindowTitle(nullptr, dataWin->gen8.name);

    gRunner = runner;

    // Initialize the first room and fire Game Start / Room Start events
    Runner_initFirstRoom(runner);

    // Start a new thread
    pthread_t tid;
    pthread_create(&tid, NULL, loop, NULL);
    pthread_detach(tid);
}

void stopRunner() {
    fprintf(stderr, "Marked runner to exit!\n");
    gRunner->shouldExit = true;
}