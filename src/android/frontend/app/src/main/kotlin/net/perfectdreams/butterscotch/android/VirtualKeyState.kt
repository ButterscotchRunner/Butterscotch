package net.perfectdreams.butterscotch.android

import net.perfectdreams.butterscotch.android.layouts.InputBinding

// Tracks which input bindings are currently held and forwards only edge transitions to JNI.
// Without this, a finger dragging across the joystick would re-fire "key down" on every pointer
// move event, which would spuriously re-trigger GameMaker's keyboard_check_pressed flag.
//
// Refcounting handles the case where two pointers happen to map to the same binding at once (e.g.
// two fingers both landing on the joystick area mapped to "up"): the binding stays down until *all*
// pointers release it. Matches what the WebKT reference frontend does with pressedRefs.
//
// Bindings are the digital InputBinding model (keyboard key or gamepad button). The data classes
// give us correct equals/hashCode, so they work directly as refcount/set keys.
// onPress fires on every digital down edge (a binding going from up to held), so on-screen action
// buttons and the dpad can buzz. The analog stick bypasses this (it uses setAxis), so it stays silent.
class VirtualKeyState(val runner: ButterscotchDroidRunner, private val onPress: () -> Unit = {}) {
    private val refs = HashMap<InputBinding, Int>()

    // Last non-zero analog axis value per (device, axisIndex), packed into a Long key. Analog sticks
    // are continuous, so they bypass the digital refcount model entirely; we only keep this so
    // releaseAll can re-center a stick whose subtree was torn down mid-drag (e.g. an Overlay/Stacked
    // reflow), exactly like we drop held keys.
    private val axes = HashMap<Long, Float>()

    private fun axisKey(device: Int, axisIndex: Int): Long = (device.toLong() shl 32) or (axisIndex.toLong() and 0xffffffffL)

    // We do NOT need to use @Synchronized here because there is only one UI Thread, and we only dispatch it to a channel

    fun acquire(binding: InputBinding) {
        val newCount = (refs[binding] ?: 0) + 1
        refs[binding] = newCount
        if (newCount == 1) {
            onPress()
            dispatch(binding, isDown = true)
        }
    }

    fun release(binding: InputBinding) {
        val newCount = (refs[binding] ?: return) - 1
        if (newCount <= 0) {
            refs.remove(binding)
            dispatch(binding, isDown = false)
        } else {
            refs[binding] = newCount
        }
    }

    // Apply a new "currently-pressed" set for a single pointer, emitting only the delta.
    fun transition(oldKeys: Set<InputBinding>, newKeys: Set<InputBinding>) {
        if (oldKeys == newKeys) return
        for (k in oldKeys) if (k !in newKeys) release(k)
        for (k in newKeys) if (k !in oldKeys) acquire(k)
    }

    // Push a raw analog axis value in [-1, 1] for an on-screen stick. Continuous, so it does not go
    // through acquire/release - it forwards straight to the runner (which deadzones on read). We skip
    // re-sending an unchanged value and forget a re-centered (0) axis so releaseAll has nothing to do.
    fun setAxis(device: Int, axisIndex: Int, value: Float) {
        val key = axisKey(device, axisIndex)
        if (axes[key] == value) return
        if (value == 0f) axes.remove(key) else axes[key] = value
        runner.onGamepadAxis(device, axisIndex, value)
    }

    fun releaseAll() {
        for ((binding, _) in refs)
            dispatch(binding, isDown = false)
        refs.clear()
        // Re-center any analog stick still pushed (its composable went away before it could send 0).
        for ((key, _) in axes)
            runner.onGamepadAxis((key shr 32).toInt(), (key and 0xffffffffL).toInt(), 0f)
        axes.clear()
    }

    // Forward a binding edge to the runner. Keyboard goes through the existing key path; gamepad
    // buttons go through the gamepad feed (the slot auto-connects on the C side on first input).
    private fun dispatch(binding: InputBinding, isDown: Boolean) {
        when (binding) {
            is InputBinding.Keyboard -> runner.onKey(binding.vk, isDown)
            is InputBinding.GamepadButton -> runner.onGamepadButton(binding.device, binding.button, isDown)
        }
    }
}