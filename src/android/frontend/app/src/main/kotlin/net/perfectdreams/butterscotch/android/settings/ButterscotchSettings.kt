package net.perfectdreams.butterscotch.android.settings

import kotlinx.serialization.Serializable

@Serializable
data class ButterscotchSettings(
    // Bump this when we need to migrate an old settings.json to a newer shape
    val version: Int = 1,
    val enableHapticFeedback: Boolean = true,
    // Vibration strength as a 10..100 percentage (the UI clamps the minimum to 10)
    val hapticStrength: Int = 100,
    val hideVirtualGamepadWhenUsingPhysicalController: Boolean = true,
    val hideVirtualGamepadWhenUsingPhysicalKeyboard: Boolean = true,
)
