package net.perfectdreams.butterscotch.android.layouts

class Gamepad {
    // See runner_gamepad.c
    enum class Button(val index: Int, val label: String, val shortLabel: String) {
        FACE1(0, "A / Cross (bottom)", "A"),
        FACE2(1, "B / Circle (right)", "B"),
        FACE3(2, "X / Square (left)", "X"),
        FACE4(3, "Y / Triangle (top)", "Y"),
        SHOULDER_L(4, "L1 / Left Bumper", "L1"),
        SHOULDER_R(5, "R1 / Right Bumper", "R1"),
        TRIGGER_L(6, "L2 / Left Trigger", "L2"),
        TRIGGER_R(7, "R2 / Right Trigger", "R2"),
        SELECT(8, "Select / Back", "Sel"),
        START(9, "Start", "Start"),
        STICK_L(10, "Left Stick (click)", "L3"),
        STICK_R(11, "Right Stick (click)", "R3"),
        DPAD_UP(12, "D-Pad Up", "↑"),
        DPAD_DOWN(13, "D-Pad Down", "↓"),
        DPAD_LEFT(14, "D-Pad Left", "←"),
        DPAD_RIGHT(15, "D-Pad Right", "→"),
        HOME(16, "Home / Guide", "⌂");

        companion object {
            fun fromIndex(index: Int): Button? = Button.entries.firstOrNull { it.index == index }
        }
    }
}