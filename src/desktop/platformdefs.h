#pragma once

#include <stdbool.h>

#include "runner.h"
#include "input_recording.h"

bool platformInit(int reqW, int reqH, const char *title, bool headless);
void platformInitFunctions(Runner *);
void platformExit(void);
void platformSwapBuffers(void);
void *platformGetProcAddress(const char *name);
double platformGetTime(void);
bool platformHandleEvents(void);
bool platformGetWindowSize(int32_t* outW, int32_t* outH);
void platformGamepad_poll(RunnerGamepadState* gp);
void platformSleepUntil(double time);

enum gfx_api {
    SOFTWARE,
    MODERN_GL,
    LEGACY_GL
};

extern enum gfx_api gfx;

extern InputRecording *globalInputRecording;
