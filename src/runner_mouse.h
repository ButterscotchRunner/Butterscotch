#pragma once

#include "common.h"
#include <stdint.h>

// Mouse buttons
// TODO verify side buttons are correct
#define GML_MB_ANY -1
#define GML_MB_NONE 0
#define GML_MB_LEFT 1
#define GML_MB_RIGHT 2
#define GML_MB_MIDDLE 3
#define GML_MB_SIDE1 4
#define GML_MB_SIDE2 5
#define GML_MOUSE_BUTTONS 6

#define GML_MOUSE_BUTTON_COUNT 5

// Cursor constants
#define GML_CR_SIZE_ALL -22
#define GML_CR_HANDPOINT -21
#define GML_CR_APPSTART -19
#define GML_CR_DRAG -12
#define GML_CR_HOURGLASS -11
#define GML_CR_UPARROW -10
#define GML_CR_SIZE_WE -9
#define GML_CR_SIZE_NWSE -8
#define GML_CR_SIZE_NS -7
#define GML_CR_SIZE_NESW -6
#define GML_CR_BEAM -4
#define GML_CR_CROSS -3
#define GML_CR_ARROW -2
#define GML_CR_NONE -1
#define GML_CR_DEFAULT 0

typedef struct RunnerMouseState {
    // Cursor cached in app-surface (FBO) pixel space
    double screenX, screenY;
    double normalizedX, normalizedY;
    bool buttonDown[GML_MOUSE_BUTTON_COUNT];
    bool buttonPressed[GML_MOUSE_BUTTON_COUNT];
    bool buttonReleased[GML_MOUSE_BUTTON_COUNT];
    bool wheelUp;
    bool wheelDown;
    int32_t currentButton;
    int32_t lastButton;
} RunnerMouseState;

RunnerMouseState* RunnerMouse_create(void);
void RunnerMouse_free(RunnerMouseState* m);
void RunnerMouse_beginFrame(RunnerMouseState* m);
void RunnerMouse_onButtonDown(RunnerMouseState* m, int32_t button);
void RunnerMouse_onButtonUp(RunnerMouseState* m, int32_t button);
bool RunnerMouse_checkButton(RunnerMouseState* m, int32_t button);
bool RunnerMouse_checkButtonPressed(RunnerMouseState* m, int32_t button);
bool RunnerMouse_checkButtonReleased(RunnerMouseState* m, int32_t button);
void RunnerMouse_clear(RunnerMouseState* m, int32_t button);
int32_t RunnerMouse_getButton(RunnerMouseState* m);
int32_t RunnerMouse_getLastButton(RunnerMouseState* m);
void RunnerMouse_onWheel(RunnerMouseState* m, double yoffset);
bool RunnerMouse_getWheelUp(RunnerMouseState* m);
bool RunnerMouse_getWheelDown(RunnerMouseState* m);
