package net.perfectdreams.butterscotch.android.layouts

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
sealed class InputBinding {
    @Serializable
    @SerialName("Keyboard")
    data class Keyboard(val vk: Int) : InputBinding()

    @Serializable
    @SerialName("GamepadButton")
    data class GamepadButton(val device: Int, val button: Int) : InputBinding()
}