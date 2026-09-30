#include "common.h"
#include "platformdefs.h"
#include "runner_mouse.h"
#include "gettime.h"

#include <nds.h>

static Runner* g_runner = NULL;

static int32_t g_width = 256;
static int32_t g_height = 192;


// Initialise the DS "platform".
// Rendering itself is currently handled by NoopRenderer.
bool platformInit(int32_t reqW, int32_t reqH, const char* title, bool headless){
    (void)reqW;
    (void)reqH;
    (void)title;
    (void)headless;

    g_width = 256;
    g_height = 192;

    return true;
}


void platformExit(void){

}


void platformInitFunctions(Runner* runner){
    g_runner = runner;
    runner->setCursor = NULL;
    runner->currentCursor = GML_CR_DEFAULT;
}


bool platformGetWindowSize(int32_t* outW, int32_t* outH){
    if (outW)
        *outW = g_width;

    if (outH)
        *outH = g_height;

    return true;
}


bool platformGetScaledWindowSize(int32_t* outW, int32_t* outH){
    return platformGetWindowSize(outW, outH);
}


void platformSetWindowSize(int32_t width, int32_t height){
    if (width > 0)
        g_width = width;

    if (height > 0)
        g_height = height;
}


void platformSetWindowTitle(const char* title){
    // No window title on Nintendo DS.
    (void)title;
}


void platformGetMousePos(double* xPos, double* yPos){
    // No mouse support yet.
    if (xPos)
        *xPos = 0.0;

    if (yPos)
        *yPos = 0.0;
}


void platformSwapBuffers(void) {
    // No rendering yet.
    // NoopRenderer handles rendering calls.
}


void* platformGetProcAddress(const char* name){
    // No OpenGL / dynamic graphics API.
    (void)name;

    return NULL;
}


bool platformHandleEvents(void){
    // No controls yet.
    //
    // Returning false means:
    // "Keep running, don't close the application."

    return false;
}


void platformSleepUntil(uint64_t targetTime){
    while (nowNanos() < targetTime){
        swiWaitForVBlank();
    }
}