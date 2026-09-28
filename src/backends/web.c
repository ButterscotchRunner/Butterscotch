#include "gettime.h"
#include "runner.h"
#include "loop.h"
#include "../cli/args.h"
#include <time.h>
#include <errno.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/wasmfs.h>
#include <GL/gl.h>

#define MAX_KEY_QUEUE 64

static int gKeyUpQueue[MAX_KEY_QUEUE];
static int gKeyDownQueue[MAX_KEY_QUEUE];
static int gKeyUpCount;
static int gKeyDownCount;

static Runner* gRunner = nullptr;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gWebGLContextHandle = -1;
static int32_t gWidth = 0;
static int32_t gHeight = 0;
static bool gInitialized = false;
static bool gRequestedRunnerExit = false;

// Mounts the browser's OPFS at "/butterscotch" in the WASMFS virtual filesystem.
int mountOpfs() {
    logInfo("Trying to mount OPFS in WASMFS...\n");

    backend_t opfs = wasmfs_create_opfs_backend();
    if (!opfs) {
        logWarn("Failed to create OPFS backend\n");
        return -1;
    }
    int rc = wasmfs_create_directory("/butterscotch", 0777, opfs);
    if (rc != 0) {
        logWarn("Failed to mount OPFS at /butterscotch: %s\n", strerror(errno));
        return -1;
    }

    logInfo("Successfully mounted OPFS in WASMFS!\n");
    return 0;
}

int startRunner(int butterscotchArgsCount, char* butterscotchArgs[]) {
    setbuf(stderr, NULL);

    int argc = butterscotchArgsCount + 1;
    char** argv;

    argv = safeCalloc(argc, sizeof(char*));

    repeat(argc, i) {
        argv[i] = i == 0 ? "butterscotch.wasm" : butterscotchArgs[i - 1];
    }

    gRequestedRunnerExit = false;

    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);
    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);

    free(argv);

    return ret;
}

void requestRunnerExit() {
    gRequestedRunnerExit = true;
}

void platformLog(MAYBE_UNUSED const logType type, const char *format, va_list va) {
    vfprintf(stdout, format, va);
}

bool platformInit(int32_t reqW, int32_t reqH, MAYBE_UNUSED const char *title, MAYBE_UNUSED bool headless) {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.antialias = 0; // Required to avoid "WebGL warning: blitFramebuffer: DRAW_FRAMEBUFFER may not have multiple samples."
    // These two are required because, if we don't, the canvas itself handles framebuffer swapping, and we DON'T want that because sometimes we don't swap the framebuffer
    // For example: When switching rooms, if you DIDN'T have these, it would cause a brief flicker of the content of the room
    // Essentially what it does is that it creates a framebuffer on the canvas and blits the result manually to the WebGL canvas, mimicking how desktop OpenGL works
    attrs.explicitSwapControl = 1;
    attrs.renderViaOffscreenBackBuffer = 1;

    // Yes, "#canvas" feels nasty as HELL
    // But that's how Emscripten works for SOME REASON
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (0 >= ctx) {
        logError("Failed to create WebGL context: %d\n", (int)ctx);
        abort();
    }

    emscripten_webgl_make_context_current(ctx);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    gWidth = reqW > 0 ? reqW : 640;
    gHeight = reqH > 0 ? reqH : 480;
    gInitialized = true;
    gWebGLContextHandle = ctx;

    return true;
}

void onKeyUp(int keyCode) {
    if (gKeyUpCount == MAX_KEY_QUEUE)
        return;

    gKeyUpQueue[gKeyUpCount++] = keyCode;
}

void onKeyDown(int keyCode) {
    if (gKeyDownCount == MAX_KEY_QUEUE)
        return;

    gKeyDownQueue[gKeyDownCount++] = keyCode;
}

void platformExit(void) {
    gInitialized = false;
    emscripten_webgl_destroy_context(gWebGLContextHandle);
}

void platformInitFunctions(Runner *runner) {
    gRunner = runner;
    runner->setCursor = nullptr;
    runner->currentCursor = GML_CR_DEFAULT;
}

bool platformGetWindowSize(int32_t *outW, int32_t *outH) {
    if (!outW || !outH) return false;
    if (!gInitialized) return false;
    *outW = gWidth;
    *outH = gHeight;
    return true;
}

bool platformGetScaledWindowSize(int32_t *outW, int32_t *outH) {
    return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height) {
    if (width > 0) gWidth = width;
    if (height > 0) gHeight = height;
}

void platformSetWindowTitle(MAYBE_UNUSED const char *title) {
    MAIN_THREAD_EM_ASM({ postMessage({ type: 'windowTitle', title: UTF8ToString($0) }); }, title);
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (xPos)
        *xPos = 0.0;

    if (yPos)
        *yPos = 0.0;
}

void platformSwapBuffers(void) {
    emscripten_webgl_commit_frame();
}

void *platformGetProcAddress(MAYBE_UNUSED const char *name) {
    return nullptr;
}

bool platformHandleEvents(void) {
    while (gKeyUpCount != 0) {
        RunnerKeyboard_onKeyUp(gRunner->keyboard, gKeyUpQueue[--gKeyUpCount]);
    }

    while (gKeyDownCount != 0) {
        RunnerKeyboard_onKeyDown(gRunner->keyboard, gKeyDownQueue[--gKeyDownCount]);
    }

    return gRequestedRunnerExit;
}

void platformSleepUntil(uint64_t time) {
    int64_t remaining = (int64_t) time - (int64_t) nowNanos();
    emscripten_sleep(remaining > 0 ? (unsigned int) (remaining / 1000000) : 0);
}

// ===[ METADATA THINGS ]===
DataWin* parseDataWin(const char* path) {
    DataWinParserOptions opts = {0};
    opts.parseGen8 = true;
    opts.parseStrg = true; // GEN8 stores string offsets that point into STRG
    return DataWin_parse(path, opts);
}

void freeDataWin(DataWin* dw) {
    if (dw != nullptr) DataWin_free(dw);
}

const char* getGameName(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.name : nullptr;
}

const char* getGameDisplayName(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.displayName : nullptr;
}

uint32_t getMajorVersion(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.major : 0;
}

uint32_t getMinorVersion(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.minor : 0;
}

uint32_t getRelease(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.release : 0;
}

uint32_t getBuild(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.build : 0;
}

uint32_t getDefaultWindowWidth(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.defaultWindowWidth : 0;
}

uint32_t getDefaultWindowHeight(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.defaultWindowHeight : 0;
}