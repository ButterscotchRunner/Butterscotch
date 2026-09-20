package net.perfectdreams.butterscotch.android.layouts

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
sealed class KeyTrigger {
    @Serializable
    @SerialName("Press")
    data object Press : KeyTrigger()

    @Serializable
    @SerialName("RapidFire")
    data class RapidFire(
        /**
         * When true, the rapid fire will act as a toggle. The rapid fire will trigger while it is toggled.
         */
        val toggle: Boolean,
        val frameInterval: Int
    ) : KeyTrigger()
}