#include <common.h>
#include "ps2_gamepad.h"

#include <libpad.h>
#include <stdio.h>
#include <string.h>

// Track DualShock-mode handshake completion per port so poll() can lazily kick it off if it hasn't run yet.
static bool analogModeReady[2] = {false, false};

static float applyDeadzone(float value, float deadzone) {
    if (value < 0.0f) {
        if (value > -deadzone) return 0.0f;
        return (value + deadzone) / (1.0f - deadzone);
    } else {
        if (value < deadzone) return 0.0f;
        return (value - deadzone) / (1.0f - deadzone);
    }
}

static float stickByteToFloat(unsigned char raw) {
    return ((float) raw - 128.0f) * (1.0f / 127.5f);
}

static bool waitForRequest(int port) {
    int spins = 0;
    while (100000 > spins) {
        unsigned char rstat = padGetReqState(port, 0);
        if (rstat != PAD_RSTAT_BUSY) return rstat == PAD_RSTAT_COMPLETE;
        spins++;
    }
    return false;
}

// Caller must guarantee padGetState(port, 0) == PAD_STATE_STABLE before invoking.
static void setupAnalogMode(int port) {
    int modes = padInfoMode(port, 0, PAD_MODETABLE, -1);
    bool supportsDualshock = false;
    for (int i = 0; modes > i; i++) {
        if (padInfoMode(port, 0, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
            supportsDualshock = true;
            break;
        }
    }
    if (!supportsDualshock) {
        printf("Ps2Gamepad: port %d does not support DualShock mode\n", port);
        analogModeReady[port] = true;
        return;
    }

    if (padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK) == 0) {
        printf("Ps2Gamepad: padSetMainMode failed on port %d\n", port);
        return;
    }
    if (!waitForRequest(port)) {
        printf("Ps2Gamepad: DualShock mode request did not complete on port %d\n", port);
        return;
    }
    analogModeReady[port] = true;
    printf("Ps2Gamepad: port %d set to DualShock analog mode\n", port);
}

void Ps2Gamepad_poll(RunnerGamepadState* gp, int port) {
    if (0 > port || port >= 2) return;
    if (port >= MAX_GAMEPADS) return;

    GamepadSlot* slot = &gp->slots[port];

    memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));

    int state = padGetState(port, 0);
    if (state != PAD_STATE_STABLE) {
        slot->connected = false;
        slot->guid[0] = '\0';
        return;
    }

    if (!analogModeReady[port]) {
        setupAnalogMode(port);
    }

    struct padButtonStatus padStatus;
    if (padRead(port, 0, &padStatus) == 0) {
        slot->connected = false;
        return;
    }

    uint16_t buttons = ~padStatus.btns;

    if (buttons & PAD_CROSS) slot->buttonDown[0] = true;
    if (buttons & PAD_CIRCLE) slot->buttonDown[1] = true;
    if (buttons & PAD_SQUARE) slot->buttonDown[2] = true;
    if (buttons & PAD_TRIANGLE) slot->buttonDown[3] = true;
    if (buttons & PAD_L1) slot->buttonDown[4] = true;
    if (buttons & PAD_R1) slot->buttonDown[5] = true;
    if (buttons & PAD_L2) slot->buttonDown[6] = true;
    if (buttons & PAD_R2) slot->buttonDown[7] = true;
    if (buttons & PAD_SELECT) slot->buttonDown[8] = true;
    if (buttons & PAD_START) slot->buttonDown[9] = true;
    if (buttons & PAD_L3) slot->buttonDown[10] = true;
    if (buttons & PAD_R3) slot->buttonDown[11] = true;
    if (buttons & PAD_UP) slot->buttonDown[12] = true;
    if (buttons & PAD_DOWN) slot->buttonDown[13] = true;
    if (buttons & PAD_LEFT) slot->buttonDown[14] = true;
    if (buttons & PAD_RIGHT) slot->buttonDown[15] = true;

    if (analogModeReady[port]) {
        slot->axisValue[0] = applyDeadzone(stickByteToFloat(padStatus.ljoy_h), slot->deadzone);
        slot->axisValue[1] = applyDeadzone(stickByteToFloat(padStatus.ljoy_v), slot->deadzone);
        slot->axisValue[2] = applyDeadzone(stickByteToFloat(padStatus.rjoy_h), slot->deadzone);
        slot->axisValue[3] = applyDeadzone(stickByteToFloat(padStatus.rjoy_v), slot->deadzone);
    }

    for (int i = 0; GP_BUTTON_COUNT > i; i++) {
        slot->buttonValue[i] = slot->buttonDown[i] ? 1.0f : 0.0f;
    }

    if (!slot->connected) {
        snprintf(slot->description, sizeof(slot->description), "PlayStation 2 Controller (port %d)", port);
        slot->guid[0] = '\0';
        slot->jid = port;
    }
    slot->connected = true;

    for (int btn = 0; GP_BUTTON_COUNT > btn; btn++) {
        bool wasDown = slot->buttonDownPrev[btn];
        if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
        if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
    }
    gp->connectedCount++;
}
