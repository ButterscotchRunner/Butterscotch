package net.perfectdreams.butterscotch.android.layouts

// Mirror of runner_gamepad.h
const val GP_AXIS_LH = 32785
const val GP_AXIS_LV = 32786
const val GP_AXIS_RH = 32787
const val GP_AXIS_RV = 32788

enum class GamepadStick(val horizontalAxis: Int, val verticalAxis: Int) {
    LEFT(GP_AXIS_LH, GP_AXIS_LV),
    RIGHT(GP_AXIS_RH, GP_AXIS_RV);

    // Canonical RunnerGamepad axis indices (0 = LH, 1 = LV, 2 = RH, 3 = RV) that
    // ButterscotchDroidRunner.onGamepadAxis / ButterscotchNative.gamepadAxis expect. The constants
    // above are the GML-facing gp_axis* values; the index is just their offset from GP_AXIS_LH.
    val horizontalAxisIndex: Int get() = horizontalAxis - GP_AXIS_LH
    val verticalAxisIndex: Int get() = verticalAxis - GP_AXIS_LH
}