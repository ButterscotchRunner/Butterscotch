package net.perfectdreams.butterscotch.android

import android.view.InputDevice
import android.view.KeyEvent

// Translates Android physical-keyboard input into the runner's GameMaker vk_ keyboard feed.
class KeyboardRouter(private val runner: ButterscotchDroidRunner) {
    // vk codes currently held down, so onPause can release them, and we can swallow auto-repeat without re-firing the keyPressed edge on the C side
    private val heldVk = HashSet<Int>()

    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (!isPhysicalKeyboard(event)) return false

        val vk = keyCodeToVk(event.keyCode)

        when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                // Character feed is independent of the vk mapping: keys like ; , / = have no vk_ code but are still typeable glyphs that must reach keyboard_lastchar.
                // Mirrors GLFW's separate char callback.
                // Fires on auto-repeat too, since text entry repeats
                val ch = event.getUnicodeChar(event.metaState)
                if (ch != 0)
                    runner.onChar(ch)

                // vk key-down edge: only for keys we have a vk_ code for, de-duped against auto-repeat so the keyPressed edge fires once per physical press
                if (vk != -1 && heldVk.add(vk))
                    runner.onKey(vk, true)

                // Consume only if we actually produced something, else let it bubble to the system
                return ch != 0 || vk != -1
            }
            KeyEvent.ACTION_UP -> {
                // No character on release; only the vk edge matters
                if (vk == -1)
                    return false

                if (heldVk.remove(vk))
                    runner.onKey(vk, false)

                return true
            }
            else -> return false
        }
    }

    // Release everything still held, mirroring GamepadRouter.releaseAll, so backgrounding never leaves a key stuck down in the runner
    fun releaseAll() {
        for (vk in heldVk)
            runner.onKey(vk, false)

        heldVk.clear()
    }

    companion object {
        private fun isPhysicalKeyboard(event: KeyEvent): Boolean {
            // We ONLY care about alphabetic keyboards
            val device = event.device ?: return false
            return (event.source and InputDevice.SOURCE_KEYBOARD == InputDevice.SOURCE_KEYBOARD) && device.keyboardType == InputDevice.KEYBOARD_TYPE_ALPHABETIC
        }

        // Android KeyEvent keycode -> GameMaker vk code, or -1 to leave the event alone
        private fun keyCodeToVk(keyCode: Int): Int = when (keyCode) {
            // Letters, digits and function keys are contiguous in both spaces, so map by offset
            in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> keyCode - KeyEvent.KEYCODE_A + 65
            in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> keyCode - KeyEvent.KEYCODE_0 + 48
            in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> keyCode - KeyEvent.KEYCODE_F1 + 112

            KeyEvent.KEYCODE_DPAD_LEFT -> 37
            KeyEvent.KEYCODE_DPAD_UP -> 38
            KeyEvent.KEYCODE_DPAD_RIGHT -> 39
            KeyEvent.KEYCODE_DPAD_DOWN -> 40

            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 13
            KeyEvent.KEYCODE_ESCAPE -> 27
            KeyEvent.KEYCODE_SPACE -> 32
            KeyEvent.KEYCODE_TAB -> 9
            KeyEvent.KEYCODE_DEL -> 8 // Android DEL is Backspace
            KeyEvent.KEYCODE_FORWARD_DEL -> 46 // FORWARD_DEL is the actual Delete

            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> 16
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> 17
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> 18

            KeyEvent.KEYCODE_INSERT -> 45
            KeyEvent.KEYCODE_MOVE_HOME -> 36
            KeyEvent.KEYCODE_MOVE_END -> 35
            KeyEvent.KEYCODE_PAGE_UP -> 33
            KeyEvent.KEYCODE_PAGE_DOWN -> 34

            else -> -1
        }
    }
}
