#pragma once

#include "runner_gamepad.h"

void Ps2Gamepad_init(int port);
void Ps2Gamepad_poll(RunnerGamepadState* gp, int port);
