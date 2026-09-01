#include "common.h"
#include "platformdefs.h"
#include "gettime.h"
#include "runner_mouse.h"

#include <string.h>
#include <time.h>

// No-op windowing backend: does not open a window or require SDL/GLFW.

static Runner *g_runner = NULL;
static int32_t g_width = 0;
static int32_t g_height = 0;
static bool g_initialized = false;

static bool noopWindowHasFocus(void) {
    return true;
}

bool platformInit(int32_t reqW, int32_t reqH, const char *title, bool headless) {
    (void)title;
    (void)headless;
    g_width = reqW > 0 ? reqW : 640;
    g_height = reqH > 0 ? reqH : 480;
    g_initialized = true;
    logInfo("No-op platform backend: %dx%d (no window)\n", g_width, g_height);
    return true;
}

void platformExit(void) {
    g_initialized = false;
}

void platformInitFunctions(Runner *runner) {
    g_runner = runner;
    runner->windowHasFocus = noopWindowHasFocus;
    runner->setCursor = NULL;
    runner->currentCursor = GML_CR_DEFAULT;
}

bool platformGetWindowSize(int32_t *outW, int32_t *outH) {
    if (!outW || !outH) return false;
    if (!g_initialized) return false;
    *outW = g_width;
    *outH = g_height;
    return true;
}

bool platformGetScaledWindowSize(int32_t *outW, int32_t *outH) {
    return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height) {
    if (width > 0) g_width = width;
    if (height > 0) g_height = height;
}

void platformSetWindowTitle(const char *title) {
    (void)title;
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (xPos) *xPos = 0.0;
    if (yPos) *yPos = 0.0;
}

void platformSwapBuffers(void) {
    // No-op
}

void *platformGetProcAddress(const char *name) {
    (void)name;
    return NULL;
}

double platformGetTime(void) {
    return (double)nowNanos() / 1e9;
}

bool platformHandleEvents(void) {
    // No window, never requests close unless runner signals. Gamepad polling: mark none connected.
    if (g_runner && g_runner->gamepads) {
        g_runner->gamepads->connectedCount = 0;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            GamepadSlot *slot = &g_runner->gamepads->slots[i];
            memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
            memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
            memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
            memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
            memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
            memset(slot->axisValue, 0, sizeof(slot->axisValue));
            slot->connected = false;
            slot->guid[0] = '\0';
        }
    }
    return false;
}

void platformSleepUntil(uint64_t time) {
    int64_t remaining = (int64_t)time - (int64_t)nowNanos();
    if (remaining > 2000000) {
        remaining -= 1000000;
#ifdef _WIN32
        Sleep(remaining / 1000000);
#else
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = remaining;
        nanosleep(&ts, NULL);
#endif
    }
    while (nowNanos() < time) {
        YIELD();
    }
}

#ifdef ENABLE_SW_RENDERER
void Runner_setNextFrame(uint32_t *framebuffer, int width, int height) {
    (void)framebuffer;
    (void)width;
    (void)height;
}
#endif
