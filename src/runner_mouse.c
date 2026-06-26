#include "runner_mouse.h"
#include "utils.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool isValidButtonVirtual(int32_t button) {
    return button >= -1 && GML_MOUSE_BUTTONS > button;
}

static bool isValidButton(int32_t button) {
    return button >= 1 && GML_MOUSE_BUTTONS > button;
}

RunnerMouseState* RunnerMouse_create(void) {
    RunnerMouseState* m = (RunnerMouseState *)safeCalloc(1, sizeof(RunnerMouseState));
    return m;
}

void RunnerMouse_free(RunnerMouseState* m) {
    free(m);
}

void RunnerMouse_beginFrame(RunnerMouseState* m) {
    memset(m->buttonPressed, 0, sizeof(m->buttonPressed));
    memset(m->buttonReleased, 0, sizeof(m->buttonReleased));
    m->wheelUp = false;
    m->wheelDown = false;
}

void RunnerMouse_onButtonDown(RunnerMouseState* m, int32_t button) {
    if (!isValidButton(button)) return;
    m->buttonDown[button-1] = true;
    m->buttonPressed[button-1] = true;
    m->lastButton = button;
    m->currentButton = button;
}

void RunnerMouse_onButtonUp(RunnerMouseState* m, int32_t button) {
    if (!isValidButton(button)) return;
    m->buttonDown[button-1] = false;
    m->buttonReleased[button-1] = true;
    if (m->currentButton == button) {
        m->currentButton = GML_MB_NONE;
        for (int i = GML_MOUSE_BUTTON_COUNT - 1; i >= 0; i--) {
            if (m->buttonDown[i]) {
                m->currentButton = i + 1;
                break;
            }
        }
    }
}

bool RunnerMouse_checkButton(RunnerMouseState* m, int32_t button) {
    if (!isValidButtonVirtual(button)) return false;

    if (isValidButton(button)) {
        return m->buttonDown[button-1];
    }

    bool any = false;
    repeat(GML_MOUSE_BUTTON_COUNT, i) {
        any |= m->buttonDown[i];
    }

    if (button == GML_MB_ANY) return any;
    else if (button == GML_MB_NONE) return !any;
    return false;
}

bool RunnerMouse_checkButtonPressed(RunnerMouseState* m, int32_t button) {
    if (!isValidButtonVirtual(button)) return false;

    if (isValidButton(button)) {
        return m->buttonPressed[button-1];
    }

    bool any = false;
    repeat(GML_MOUSE_BUTTON_COUNT, i) {
        any |= m->buttonPressed[i];
    }

    if (button == GML_MB_ANY) return any;
    else if (button == GML_MB_NONE) return !any;
    return false;
}

bool RunnerMouse_checkButtonReleased(RunnerMouseState* m, int32_t button) {
    if (!isValidButtonVirtual(button)) return false;

    if (isValidButton(button)) {
        return m->buttonReleased[button-1];
    }

    bool any = false;
    repeat(GML_MOUSE_BUTTON_COUNT, i) {
        any |= m->buttonReleased[i];
    }

    if (button == GML_MB_ANY) return any;
    else if (button == GML_MB_NONE) return !any;
    return false;
}

void RunnerMouse_clear(RunnerMouseState* m, int32_t button) {
    if (!isValidButtonVirtual(button)) return;

    if (isValidButton(button)) {
        m->buttonDown[button-1] = false;
        m->buttonPressed[button-1] = false;
        m->buttonReleased[button-1] = false;
        if (m->currentButton == button) m->currentButton = GML_MB_NONE;
    } else if (button == GML_MB_ANY) {
        memset(m->buttonDown, 0, sizeof(m->buttonDown));
        memset(m->buttonPressed, 0, sizeof(m->buttonPressed));
        memset(m->buttonReleased, 0, sizeof(m->buttonReleased));
        m->currentButton = GML_MB_NONE;
    }
}

int32_t RunnerMouse_getButton(RunnerMouseState* m) {
    return m->currentButton;
}

int32_t RunnerMouse_getLastButton(RunnerMouseState* m) {
    return m->lastButton;
}

void RunnerMouse_onWheel(RunnerMouseState* m, double yoffset) {
    if (yoffset > 0) m->wheelUp = true;
    if (yoffset < 0) m->wheelDown = true;
}

bool RunnerMouse_getWheelUp(RunnerMouseState* m) {
    return m->wheelUp;
}

bool RunnerMouse_getWheelDown(RunnerMouseState* m) {
    return m->wheelDown;
}
