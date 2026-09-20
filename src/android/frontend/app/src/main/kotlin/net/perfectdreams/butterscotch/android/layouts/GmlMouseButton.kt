package net.perfectdreams.butterscotch.android.layouts

import kotlinx.serialization.Serializable

@Serializable
enum class GmlMouseButton(val id: Int) {
    LEFT_BUTTON(1),
    RIGHT_BUTTON(2),
    MIDDLE_BUTTON(3)
}