#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "switch_input.h"
#include "platformdefs.h"

#define SWITCH_NPAD_COUNT 8

static PadState pads[SWITCH_NPAD_COUNT];
static bool initialized = false;

static void mapLibnxToGml(GamepadSlot* slot, PadState* pad, u64 cur) {
    if (cur & HidNpadButton_A) slot->buttonDown[0] = true;
    if (cur & HidNpadButton_B) slot->buttonDown[1] = true;
    if (cur & HidNpadButton_Y) slot->buttonDown[2] = true;
    if (cur & HidNpadButton_X) slot->buttonDown[3] = true;
    if (cur & HidNpadButton_L) slot->buttonDown[4] = true;
    if (cur & HidNpadButton_R) slot->buttonDown[5] = true;
    slot->buttonValue[6] = (cur & HidNpadButton_ZL) ? 1.0f : 0.0f;
    slot->buttonValue[7] = (cur & HidNpadButton_ZR) ? 1.0f : 0.0f;
    if (cur & HidNpadButton_Minus) slot->buttonDown[8] = true;
    if (cur & HidNpadButton_Plus) slot->buttonDown[9] = true;
    if (cur & HidNpadButton_StickL) slot->buttonDown[10] = true;
    if (cur & HidNpadButton_StickR) slot->buttonDown[11] = true;
    if (cur & HidNpadButton_AnyUp) slot->buttonDown[12] = true;
    if (cur & HidNpadButton_AnyDown) slot->buttonDown[13] = true;
    if (cur & HidNpadButton_AnyLeft) slot->buttonDown[14] = true;
    if (cur & HidNpadButton_AnyRight) slot->buttonDown[15] = true;

    HidAnalogStickState l = padGetStickPos(pad, 0);
    HidAnalogStickState r = padGetStickPos(pad, 1);
    slot->axisValue[0] = l.x / 32767.0f;
    slot->axisValue[1] = -l.y / 32767.0f;
    slot->axisValue[2] = r.x / 32767.0f;
    slot->axisValue[3] = -r.y / 32767.0f;
}

static void fillGamepadSlot(GamepadSlot* slot, PadState* pad, u64 cur, bool connected, int jid, const char* guid) {
    memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDownPrev));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
    memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));

    slot->connected = connected;
    slot->jid = jid;
    strncpy(slot->description, "Nintendo Switch Controller", sizeof(slot->description) - 1);
    strncpy(slot->guid, guid, sizeof(slot->guid) - 1);

    if (connected) mapLibnxToGml(slot, pad, cur);

    for (int btn = 0; GP_BUTTON_COUNT > btn; btn++) {
        bool wasDown = slot->buttonDownPrev[btn];
        if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
        if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
    }
}

bool SwitchInput_handleEvents(struct Runner* runner) {
    if (!initialized) {
        padConfigureInput(SWITCH_NPAD_COUNT, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pads[0]);
        for (int i = 1; SWITCH_NPAD_COUNT > i; i++) {
            padInitialize(&pads[i], HidNpadIdType_No1 + i);
        }
        initialized = true;
    }

    if (!appletMainLoop()) return true;

    int connectedCount = 0;
    for (int i = 0; SWITCH_NPAD_COUNT > i; i++) {
        padUpdate(&pads[i]);
        u64 cur = pads[i].buttons_cur;
        bool connected = padIsConnected(&pads[i]);

        char guid[32];
        snprintf(guid, sizeof(guid), "switch-nx-%d", i + 1);
        fillGamepadSlot(&runner->gamepads->slots[i], &pads[i], cur, connected, i, guid);

        if (connected) connectedCount++;
    }

    runner->gamepads->connectedCount = connectedCount;

    return false;
}
