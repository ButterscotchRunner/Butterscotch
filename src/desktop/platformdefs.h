#ifndef _BS_PLATFORMDEFS_H_
#define _BS_PLATFORMDEFS_H_

#include <stdbool.h>

#include <stdbool.h>
#include <stdint.h>
#include "runner.h"
#include "input_recording.h"
#include "data_win.h"

#ifdef ENABLE_GUI
typedef struct {
    const char* renderer;
    DataWinLoadType loadType;
    bool lazyRooms;
    bool lazyTextures;
    bool lazyAudio;
    double speedMultiplier;
    double fastForwardSpeed;
} PlatformGuiSettings;

void platformInitGuiSettings(const PlatformGuiSettings *settings);
void platformGetGuiSettings(PlatformGuiSettings *settingsOut);
void platformGetSpeeds(double *speedMultiplier, double *fastForwardSpeed);

void platformShowWindow(void);
char* platformConsumePendingDataWinPath(void);
#endif

bool platformInit(int32_t reqW, int32_t reqH, const char *title, bool headless);
void platformInitFunctions(Runner *);
void platformExit(void);
void platformSwapBuffers(void);
void *platformGetProcAddress(const char *name);
double platformGetTime(void);
bool platformHandleEvents(void);
void platformGetMousePos(double *xPos, double *yPos);
bool platformGetWindowSize(int32_t* outW, int32_t* outH);
bool platformGetScaledWindowSize(int32_t* outW, int32_t* outH);
void platformSetWindowSize(int32_t width, int32_t height);
void platformSetWindowTitle(const char* title);
void platformSleepUntil(uint64_t time);

enum GraphicsAPI {
    SOFTWARE,
    MODERN_GL,
    LEGACY_GL
};

extern enum GraphicsAPI gfx;

extern InputRecording *globalInputRecording;

// ===[ GL Versions ]===
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

#endif /* _BS_PLATFORMDEFS_H_ */
