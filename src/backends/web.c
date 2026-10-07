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

enum {
    INPUTTYPE_KEY,
    INPUTTYPE_MOUSE_BUTTON
} typedef InputType;

struct {
    InputType inputType;
    int key;
    bool down;
} typedef InputEntry;

static InputEntry gInputQueue[MAX_KEY_QUEUE];
static int gInputCount;


static Runner* gRunner = nullptr;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gWebGLContextHandle = -1;
static int32_t gWidth = 0;
static int32_t gHeight = 0;
static bool gInitialized = false;
static bool gRequestedRunnerExit = false;
static double gMouseX = 0.0;
static double gMouseY = 0.0;

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

// mkdir -p for WASMFS paths
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

    // Make sure the saves directory exists. The FileSystem impl will write into it.
    requireMessageFormatted(__FILE__, __LINE__, mkdirP(args.saveFolder) == 0, "Failed to ensure saves dir exists at %s: %s\n", args.saveFolder, strerror(errno));

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

    emscripten_set_canvas_element_size("#canvas", gWidth, gHeight);

    return true;
}

void onKeyUp(int keyCode) {
    if (gInputCount == MAX_KEY_QUEUE)
        return;

    int entryIndex = gInputCount++;
    gInputQueue[entryIndex].inputType = INPUTTYPE_KEY;
    gInputQueue[entryIndex].key = keyCode;
    gInputQueue[entryIndex].down = false;
}

void onKeyDown(int keyCode) {
    if (gInputCount == MAX_KEY_QUEUE)
        return;

    int entryIndex = gInputCount++;
    gInputQueue[entryIndex].inputType = INPUTTYPE_KEY;
    gInputQueue[entryIndex].key = keyCode;
    gInputQueue[entryIndex].down = true;
}

void onMouseUp(int button) {
    if (gInputCount == MAX_KEY_QUEUE)
        return;

    int entryIndex = gInputCount++;
    gInputQueue[entryIndex].inputType = INPUTTYPE_MOUSE_BUTTON;
    gInputQueue[entryIndex].key = button;
    gInputQueue[entryIndex].down = false;
}

void onMouseDown(int button) {
    if (gInputCount == MAX_KEY_QUEUE)
        return;

    int entryIndex = gInputCount++;
    gInputQueue[entryIndex].inputType = INPUTTYPE_MOUSE_BUTTON;
    gInputQueue[entryIndex].key = button;
    gInputQueue[entryIndex].down = true;
}

void onMouseMove(double x, double y) {
    gMouseX = x;
    gMouseY = y;
}

void platformExit(void) {
    gInitialized = false;
    emscripten_webgl_destroy_context(gWebGLContextHandle);
}

bool platformGetWindowSize(int32_t *outW, int32_t *outH) {
    if (!outW || !outH) return false;
    if (!gInitialized) return false;
    *outW = gWidth;
    *outH = gHeight;
    return true;
}

bool platformGetWindowPosition(int32_t *outX, int32_t *outY) {
    (void)outX;
    (void)outY;
    return false;
}

void platformSetWindowPosition(int32_t x, int32_t y) {
    (void)x;
    (void)y;
}

void platformInitFunctions(Runner *runner) {
    gRunner = runner;
    runner->setCursor = nullptr;
    runner->currentCursor = GML_CR_DEFAULT;
    runner->getWindowPosition = platformGetWindowPosition;
    runner->setWindowPosition = platformSetWindowPosition;
}

bool platformGetScaledWindowSize(int32_t *outW, int32_t *outH) {
    return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height) {
    if (width > 0) gWidth = width;
    if (height > 0) gHeight = height;

    if (gInitialized)
        emscripten_set_canvas_element_size("#canvas", gWidth, gHeight);
}

void platformSetWindowTitle(MAYBE_UNUSED const char *title) {
    MAIN_THREAD_EM_ASM({ postMessage({ type: 'windowTitle', title: UTF8ToString($0) }); }, title);
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (xPos)
        *xPos = gMouseX;

    if (yPos)
        *yPos = gMouseY;
}

void platformSwapBuffers(void) {
    emscripten_webgl_commit_frame();
}

void *platformGetProcAddress(MAYBE_UNUSED const char *name) {
    return nullptr;
}

bool platformHandleEvents(void) {
    int inputIndex = 0;
    while (gInputCount != inputIndex) {
        InputEntry entry = gInputQueue[inputIndex];

        switch (entry.inputType) {
            case INPUTTYPE_KEY: {
                if (entry.down) {
                    RunnerKeyboard_onKeyDown(gRunner->keyboard, entry.key);
                } else {
                    RunnerKeyboard_onKeyUp(gRunner->keyboard, entry.key);
                }
                break;
            }
            case INPUTTYPE_MOUSE_BUTTON: {
                if (entry.down) {
                    RunnerMouse_onButtonDown(gRunner->mouse, entry.key);
                } else {
                    RunnerMouse_onButtonUp(gRunner->mouse, entry.key);
                }
                break;
            }
        }

        inputIndex++;
    }

    gInputCount = 0;
    
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