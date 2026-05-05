#include "glfw_gamepad.h"

#include <string.h>
#include <stdio.h>

// ===[ Public API ]===

void GlfwGamepad_poll(RunnerGamepadState* gp) {
    for (int slotIdx = 0; slotIdx < 1 && slotIdx < MAX_GAMEPADS; slotIdx++) {
        GamepadSlot* slot = &gp->slots[slotIdx];

        slot->connected = false;
        slot->guid[0] = '\0';
    }
}
