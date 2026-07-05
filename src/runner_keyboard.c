#include "runner_keyboard.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

static bool isValidKey(int32_t key) {
    return key >= 0 && GML_KEY_COUNT > key;
}

RunnerKeyboardState* RunnerKeyboard_create(void) {
    RunnerKeyboardState* kb = (RunnerKeyboardState *)safeCalloc(1, sizeof(RunnerKeyboardState));
    kb->lastKey = VK_NOKEY;
    kb->lastChar[0] = 0;
    kb->lastChar[1] = 0;
    kb->string[0] = 0;
    kb->stringLen = 0;
    repeat(GML_KEY_COUNT, i) {
        kb->keyMap[i] = i;
    }
    return kb;
}

static int32_t mapKey(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (!isValidKey(gmlKeyCode)) return gmlKeyCode;
    return kb->keyMap[gmlKeyCode];
}

void RunnerKeyboard_free(RunnerKeyboardState* kb) {
    free(kb);
}

void RunnerKeyboard_beginFrame(RunnerKeyboardState* kb) {
    memset(kb->keyPressed, 0, sizeof(kb->keyPressed));
    memset(kb->keyReleased, 0, sizeof(kb->keyReleased));
}

void RunnerKeyboard_onKeyDown(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    gmlKeyCode = mapKey(kb, gmlKeyCode);
    if (!isValidKey(gmlKeyCode)) return;
    kb->keyDown[gmlKeyCode] = true;
    kb->keyPressed[gmlKeyCode] = true;
    kb->lastKey = gmlKeyCode;
}

void RunnerKeyboard_onKeyUp(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    gmlKeyCode = mapKey(kb, gmlKeyCode);
    if (!isValidKey(gmlKeyCode)) return;
    kb->keyDown[gmlKeyCode] = false;
    kb->keyReleased[gmlKeyCode] = true;
}

void RunnerKeyboard_onCharacter(RunnerKeyboardState* kb, unsigned int character) {
    kb->lastChar[0] = (character >= ' ' && character <= '~') ? (char) character : 0;
    if (character == 8) {
        if (kb->stringLen > 0) {
            kb->stringLen--;
            kb->string[kb->stringLen] = '\0';
        }
    } else if (character >= 32) {
        if (kb->stringLen < 1023) {
            kb->string[kb->stringLen] = (char)character;
            kb->stringLen++;
            kb->string[kb->stringLen] = '\0';
        }
    }
}

bool checkIfAnyKey(const bool* array) {
    for (int32_t i = 2; GML_KEY_COUNT > i; i++) {
        if (array[i]) {
            return true;
        }
    }
    return false;
}

bool RunnerKeyboard_check(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (gmlKeyCode == VK_ANYKEY) {
        return checkIfAnyKey(kb->keyDown);
    }
    if (gmlKeyCode == VK_NOKEY) {
        return !checkIfAnyKey(kb->keyDown);
    }
    if (!isValidKey(gmlKeyCode)) return false;
    return kb->keyDown[gmlKeyCode];
}

bool RunnerKeyboard_checkPressed(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (gmlKeyCode == VK_ANYKEY) {
        return checkIfAnyKey(kb->keyPressed);
    }
    if (gmlKeyCode == VK_NOKEY) {
        return !checkIfAnyKey(kb->keyPressed);
    }
    if (!isValidKey(gmlKeyCode)) return false;
    return kb->keyPressed[gmlKeyCode];
}

bool RunnerKeyboard_checkReleased(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (gmlKeyCode == VK_ANYKEY) {
        return checkIfAnyKey(kb->keyReleased);
    }
    if (gmlKeyCode == VK_NOKEY) {
        return !checkIfAnyKey(kb->keyReleased);
    }
    if (!isValidKey(gmlKeyCode)) return false;
    return kb->keyReleased[gmlKeyCode];
}

void RunnerKeyboard_simulatePress(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (!isValidKey(gmlKeyCode)) return;
    kb->keyDown[gmlKeyCode] = true;
    kb->keyPressed[gmlKeyCode] = true;
    kb->lastKey = gmlKeyCode;
}

void RunnerKeyboard_simulateRelease(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (!isValidKey(gmlKeyCode)) return;
    kb->keyDown[gmlKeyCode] = false;
    kb->keyReleased[gmlKeyCode] = true;
}

void RunnerKeyboard_clear(RunnerKeyboardState* kb, int32_t gmlKeyCode) {
    if (gmlKeyCode == VK_ANYKEY) {
        memset(kb->keyDown, 0, sizeof(kb->keyDown));
        memset(kb->keyPressed, 0, sizeof(kb->keyPressed));
        memset(kb->keyReleased, 0, sizeof(kb->keyReleased));
        kb->lastKey = VK_NOKEY;
        return;
    }
    if (!isValidKey(gmlKeyCode)) return;
    kb->keyDown[gmlKeyCode] = false;
    kb->keyPressed[gmlKeyCode] = false;
    kb->keyReleased[gmlKeyCode] = false;
}

void RunnerKeyboard_setMap(RunnerKeyboardState* kb, int32_t fromKey, int32_t toKey) {
    if (!isValidKey(fromKey)) return;
    kb->keyMap[fromKey] = toKey;
}

int32_t RunnerKeyboard_getMap(RunnerKeyboardState* kb, int32_t fromKey) {
    if (!isValidKey(fromKey)) return fromKey;
    return kb->keyMap[fromKey];
}

void RunnerKeyboard_unsetMap(RunnerKeyboardState* kb) {
    repeat(GML_KEY_COUNT, i) {
        kb->keyMap[i] = i;
    }
}
