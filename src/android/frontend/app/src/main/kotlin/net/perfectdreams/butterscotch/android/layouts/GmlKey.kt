package net.perfectdreams.butterscotch.android.layouts

// GameMaker vk_ key codes the runner understands (mirror of Butterscotch's runner_keyboard.h, plus
// the ASCII letter/digit ranges). Used by the layout editor's key dropdown so a user picks a real
// key by name instead of typing a raw code. Not serialized - InputBinding still stores the raw vk.
//
// [label] is the full name shown in the editor dropdown, [shortLabel] is the compact text drawn on
// the on-screen button itself (arrows are the only keys that use a symbol)
enum class GmlKey(val code: Int, val label: String, val shortLabel: String = label) {
    LEFT(37, "Left Arrow", "←"),
    UP(38, "Up Arrow", "↑"),
    RIGHT(39, "Right Arrow", "→"),
    DOWN(40, "Down Arrow", "↓"),
    ENTER(13, "Enter"),
    ESCAPE(27, "Escape", "Esc"),
    SPACE(32, "Space"),
    SHIFT(16, "Shift"),
    CONTROL(17, "Control", "Ctrl"),
    ALT(18, "Alt"),
    TAB(9, "Tab"),
    BACKSPACE(8, "Backspace", "Bksp"),
    DELETE(46, "Delete", "Del"),
    INSERT(45, "Insert", "Ins"),
    HOME(36, "Home"),
    END(35, "End"),
    PAGE_UP(33, "Page Up", "PgUp"),
    PAGE_DOWN(34, "Page Down", "PgDn"),

    A(65, "A"), B(66, "B"), C(67, "C"), D(68, "D"), E(69, "E"), F(70, "F"), G(71, "G"),
    H(72, "H"), I(73, "I"), J(74, "J"), K(75, "K"), L(76, "L"), M(77, "M"), N(78, "N"),
    O(79, "O"), P(80, "P"), Q(81, "Q"), R(82, "R"), S(83, "S"), T(84, "T"), U(85, "U"),
    V(86, "V"), W(87, "W"), X(88, "X"), Y(89, "Y"), Z(90, "Z"),

    DIGIT_0(48, "0"), DIGIT_1(49, "1"), DIGIT_2(50, "2"), DIGIT_3(51, "3"), DIGIT_4(52, "4"),
    DIGIT_5(53, "5"), DIGIT_6(54, "6"), DIGIT_7(55, "7"), DIGIT_8(56, "8"), DIGIT_9(57, "9"),

    F1(112, "F1"), F2(113, "F2"), F3(114, "F3"), F4(115, "F4"), F5(116, "F5"), F6(117, "F6"),
    F7(118, "F7"), F8(119, "F8"), F9(120, "F9"), F10(121, "F10"), F11(122, "F11"), F12(123, "F12");

    companion object {
        fun fromCode(code: Int): GmlKey? = values().firstOrNull { it.code == code }
    }
}
