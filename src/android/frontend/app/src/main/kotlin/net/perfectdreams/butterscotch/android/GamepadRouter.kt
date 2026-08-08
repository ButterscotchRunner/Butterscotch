package net.perfectdreams.butterscotch.android

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import net.perfectdreams.butterscotch.android.gamepads.ButterscotchInputDeviceListener
import net.perfectdreams.butterscotch.android.layouts.Gamepad

// Translates Android physical-controller input into the runner's canonical RunnerGamepad feed.
//
// Android hands us two things: KeyEvents (face buttons, shoulders, dpad, sometimes triggers) and
// joystick MotionEvents (analog sticks, hat-as-axis dpad, analog triggers). The native runner, on
// the other hand, speaks "canonical slot indices" (0 = face1/A .. 16 = home; axes 0..3 = LH/LV/RH/RV).
// This class maps one onto the other and forwards everything through [ButterscotchDroidRunner]'s
// thread-safe queue, so the render thread applies it between beginFrame and stepAndDraw.
//
// Android device ids are arbitrary ints; we assign each controller the lowest free slot (0..15,
// mirroring MAX_GAMEPADS on the C side). All methods here run on the UI/main thread (input dispatch
// + the InputManager device listener with a main-looper Handler), so the maps need no locking.
class GamepadRouter(private val runner: ButterscotchDroidRunner) {
    // Android deviceId -> slot index, plus a free-list so we reuse the lowest slot on reconnect.
    private val deviceToSlot = HashMap<Int, Int>()
    private val slotInUse = BooleanArray(MAX_GAMEPADS)
    private val padStates = arrayOfNulls<PadState>(MAX_GAMEPADS)
    val listener = ButterscotchInputDeviceListener(this)

    // Per-slot edge/dedup state. Sticks dedup so a single MotionEvent (which carries every axis)
    // doesn't re-send the axes that didn't move; hat + triggers are turned into digital button
    // edges, which need the previous value to know when to fire.
    private class PadState {
        val lastAxis = FloatArray(GP_AXIS_COUNT) { Float.NaN }
        var lastHatX = 0
        var lastHatY = 0
        var leftTriggerDown = false
        var rightTriggerDown = false
    }

    // ===[ Device lifecycle — driven by GameActivity's InputManager listener ]===

    /** Enumerate currently-attached controllers and assign them slots. Call on start/resume. */
    fun refresh() {
        for (id in InputDevice.getDeviceIds()) {
            val device = InputDevice.getDevice(id) ?: continue
            if (isGameController(device)) ensureSlot(id, device)
        }
    }

    fun onDeviceAdded(deviceId: Int) {
        val device = InputDevice.getDevice(deviceId) ?: return
        if (isGameController(device)) ensureSlot(deviceId, device)
    }

    fun onDeviceRemoved(deviceId: Int) {
        val slot = deviceToSlot.remove(deviceId) ?: return
        slotInUse[slot] = false
        padStates[slot] = null
        runner.onGamepadDisconnected(slot)
    }

    // A device "change" (e.g. capabilities became available) is treated as a (re)connect; ensureSlot
    // is idempotent for an already-mapped device.
    fun onDeviceChanged(deviceId: Int) = onDeviceAdded(deviceId)

    /** Drop every controller. Used when the activity pauses so no button is left stuck down. */
    fun releaseAll() {
        for (slot in deviceToSlot.values) runner.onGamepadDisconnected(slot)
        deviceToSlot.clear()
        slotInUse.fill(false)
        for (i in padStates.indices) padStates[i] = null
    }

    // ===[ Event routing — called from GameActivity's dispatch overrides ]===

    // Returns true if the event belonged to a managed controller and we consumed it. Unmapped keys
    // (e.g. BACK on a controller) return false so they keep their normal behavior.
    fun handleKeyEvent(event: KeyEvent): Boolean {
        val slot = ensureSlot(event.deviceId, event.device)
        if (slot < 0) return false
        val button = keyCodeToButton(event.keyCode)
        if (button < 0) return false
        when (event.action) {
            // GML uses an edge model, so key-repeat (repeatCount > 0) is ignored: the button is
            // already held from the first ACTION_DOWN.
            KeyEvent.ACTION_DOWN -> if (event.repeatCount == 0) runner.onGamepadButton(slot, button, true)
            KeyEvent.ACTION_UP -> runner.onGamepadButton(slot, button, false)
            else -> return false
        }
        return true
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false
        val slot = ensureSlot(event.deviceId, event.device)
        if (slot < 0) return false
        val state = padStates[slot] ?: return false

        // Analog sticks: forward raw, the runner deadzones on read. Left = X/Y, right = Z/RZ (the
        // common Android mapping; controllers that expose the right stick on RX/RY won't drive it).
        sendAxis(slot, state, AXIS_LH, event.getAxisValue(MotionEvent.AXIS_X))
        sendAxis(slot, state, AXIS_LV, event.getAxisValue(MotionEvent.AXIS_Y))
        sendAxis(slot, state, AXIS_RH, event.getAxisValue(MotionEvent.AXIS_Z))
        sendAxis(slot, state, AXIS_RV, event.getAxisValue(MotionEvent.AXIS_RZ))

        // Many controllers report the dpad as a hat axis rather than dpad keycodes.
        updateHat(slot, state, event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y))

        // Analog triggers -> digital L2/R2. Different controllers use LTRIGGER/RTRIGGER or BRAKE/GAS.
        val left = maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE))
        val right = maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS))
        updateTrigger(slot, state, isLeft = true, value = left)
        updateTrigger(slot, state, isLeft = false, value = right)
        return true
    }

    // ===[ Internals ]===

    private fun ensureSlot(deviceId: Int, device: InputDevice?): Int {
        deviceToSlot[deviceId]?.let { return it }
        if (device == null || !isGameController(device)) return -1
        val slot = slotInUse.indexOfFirst { !it }
        if (slot < 0) return -1 // all slots taken
        slotInUse[slot] = true
        padStates[slot] = PadState()
        deviceToSlot[deviceId] = slot
        runner.onGamepadConnected(slot, device.name)
        return slot
    }

    private fun sendAxis(slot: Int, state: PadState, axis: Int, value: Float) {
        if (state.lastAxis[axis] == value) return
        state.lastAxis[axis] = value
        runner.onGamepadAxis(slot, axis, value)
    }

    private fun updateHat(slot: Int, state: PadState, hatX: Float, hatY: Float) {
        val newX = signOf(hatX)
        if (newX != state.lastHatX) {
            state.lastHatX = newX
            runner.onGamepadButton(slot, Gamepad.Button.DPAD_LEFT.index, newX < 0)
            runner.onGamepadButton(slot, Gamepad.Button.DPAD_RIGHT.index, newX > 0)
        }
        val newY = signOf(hatY)
        if (newY != state.lastHatY) {
            state.lastHatY = newY
            runner.onGamepadButton(slot, Gamepad.Button.DPAD_UP.index, newY < 0)
            runner.onGamepadButton(slot, Gamepad.Button.DPAD_DOWN.index, newY > 0)
        }
    }

    private fun updateTrigger(slot: Int, state: PadState, isLeft: Boolean, value: Float) {
        val pressed = value >= TRIGGER_THRESHOLD
        val last = if (isLeft) state.leftTriggerDown else state.rightTriggerDown
        if (pressed == last) return
        if (isLeft) state.leftTriggerDown = pressed else state.rightTriggerDown = pressed
        runner.onGamepadButton(slot, if (isLeft) Gamepad.Button.TRIGGER_L.index else Gamepad.Button.TRIGGER_R.index, pressed)
    }

    companion object {
        // Mirror of MAX_GAMEPADS / GP_AXIS_COUNT in Butterscotch's runner_gamepad.h.
        private const val MAX_GAMEPADS = 16
        private const val GP_AXIS_COUNT = 4

        // Hat axes and trigger axes are continuous; treat them as digital past these thresholds.
        private const val HAT_THRESHOLD = 0.5f
        private const val TRIGGER_THRESHOLD = 0.5f

        // Canonical axis indices.
        private const val AXIS_LH = 0
        private const val AXIS_LV = 1
        private const val AXIS_RH = 2
        private const val AXIS_RV = 3

        private fun isGameController(device: InputDevice): Boolean {
            val sources = device.sources
            return (sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD) ||
                (sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK)
        }

        private fun signOf(value: Float): Int = when {
            value > HAT_THRESHOLD -> 1
            value < -HAT_THRESHOLD -> -1
            else -> 0
        }

        // Android KeyEvent keycode -> canonical button index, or -1 to leave the event alone.
        private fun keyCodeToButton(keyCode: Int): Int = when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> Gamepad.Button.FACE1.index
            KeyEvent.KEYCODE_BUTTON_B -> Gamepad.Button.FACE2.index
            KeyEvent.KEYCODE_BUTTON_X -> Gamepad.Button.FACE3.index
            KeyEvent.KEYCODE_BUTTON_Y -> Gamepad.Button.FACE4.index
            KeyEvent.KEYCODE_BUTTON_L1 -> Gamepad.Button.SHOULDER_L.index
            KeyEvent.KEYCODE_BUTTON_R1 -> Gamepad.Button.SHOULDER_R.index
            KeyEvent.KEYCODE_BUTTON_L2 -> Gamepad.Button.TRIGGER_L.index
            KeyEvent.KEYCODE_BUTTON_R2 -> Gamepad.Button.TRIGGER_R.index
            KeyEvent.KEYCODE_BUTTON_SELECT -> Gamepad.Button.SELECT.index
            KeyEvent.KEYCODE_BUTTON_START -> Gamepad.Button.START.index
            KeyEvent.KEYCODE_BUTTON_THUMBL -> Gamepad.Button.STICK_L.index
            KeyEvent.KEYCODE_BUTTON_THUMBR -> Gamepad.Button.STICK_R.index
            KeyEvent.KEYCODE_DPAD_UP -> Gamepad.Button.DPAD_UP.index
            KeyEvent.KEYCODE_DPAD_DOWN -> Gamepad.Button.DPAD_DOWN.index
            KeyEvent.KEYCODE_DPAD_LEFT -> Gamepad.Button.DPAD_LEFT.index
            KeyEvent.KEYCODE_DPAD_RIGHT -> Gamepad.Button.DPAD_RIGHT.index
            KeyEvent.KEYCODE_BUTTON_MODE -> Gamepad.Button.HOME.index
            else -> -1
        }
    }
}
