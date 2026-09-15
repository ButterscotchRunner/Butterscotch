package net.perfectdreams.butterscotch.android.layouts

import kotlinx.serialization.Serializable
import net.perfectdreams.butterscotch.UUIDAsStringSerializer
import java.util.UUID

@Serializable
data class GamepadLayout(
    @Serializable(with = UUIDAsStringSerializer::class)
    val id: UUID,
    val fancyName: String,
    val orientation: GamepadTargetOrientation,
    val elements: List<GamepadElement>
) {
    enum class GamepadTargetOrientation {
        PORTRAIT,
        LANDSCAPE
    }
}