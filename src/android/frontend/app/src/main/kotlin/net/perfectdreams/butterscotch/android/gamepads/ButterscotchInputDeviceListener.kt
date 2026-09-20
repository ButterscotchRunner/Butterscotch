package net.perfectdreams.butterscotch.android.gamepads

import android.hardware.input.InputManager
import net.perfectdreams.butterscotch.android.GamepadRouter

class ButterscotchInputDeviceListener(val router: GamepadRouter) : InputManager.InputDeviceListener {
    override fun onInputDeviceAdded(deviceId: Int) = router.onDeviceAdded(deviceId)
    override fun onInputDeviceRemoved(deviceId: Int) = router.onDeviceRemoved(deviceId)
    override fun onInputDeviceChanged(deviceId: Int) = router.onDeviceChanged(deviceId)
}