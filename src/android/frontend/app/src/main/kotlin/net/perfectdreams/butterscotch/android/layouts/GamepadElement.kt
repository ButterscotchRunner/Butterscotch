package net.perfectdreams.butterscotch.android.layouts

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import net.perfectdreams.butterscotch.UUIDAsStringSerializer
import java.util.UUID

@Serializable
sealed class GamepadElement {
    /**
     * Stable identity for this element, independent of its position in the layout list.
     *
     * The editor addresses elements by this id (drag/edit/delete) so mutating the list (add/remove) can never make an in-flight edit point at the wrong element.
     */
    abstract val id: UUID
    /**
     * The X position of the element on the overlay (percentage, 0..1, based on the overlay width)
     */
    abstract val positionX: Double
    /**
     * The Y position of the element on the overlay (percentage, 0..1, based on the overlay height)
     */
    abstract val positionY: Double
    /**
     * The scale (X and Y) of the element (scaled by min(overlayWidth, overlayHeight))
     */
    abstract val scale: Double
    /**
     * The opacity of the element
     */
    abstract val opacity: Double

    /**
     * Every sprite file name referenced by this element. The sprite sweep and the layout zip export walk these, so keep this in sync when adding sprite fields to other element types
     */
    fun spriteReferences(): List<String> = when (this) {
        is Key -> listOfNotNull(sprite, spritePressed)
        is Joystick -> listOfNotNull(sprite, spriteThumb)
        is AnalogJoystick -> listOfNotNull(sprite, spriteThumb)
        is Menu -> listOfNotNull(sprite)
        is FastForward -> listOfNotNull(sprite, spritePressed)
        is MouseButton -> listOfNotNull(sprite, spritePressed)
    }

    @Serializable
    @SerialName("Key")
    data class Key(
        @Serializable(with = UUIDAsStringSerializer::class)
        override val id: UUID,
        override val positionX: Double,
        override val positionY: Double,
        override val scale: Double,
        override val opacity: Double,

        /**
         * The label of the button. If null falls back to a value derived from the [binding].
         */
        val label: String?,
        val trigger: KeyTrigger,
        val binding: InputBinding,

        /**
         * Custom sprite file names inside the layout sprites pool, see [LayoutLibrary.spriteFile]. [spritePressed] is only meaningful when [sprite] is set
         */
        val sprite: String? = null,
        val spritePressed: String? = null,
    ) : GamepadElement()

    @Serializable
    @SerialName("Joystick")
    data class Joystick(
        @Serializable(with = UUIDAsStringSerializer::class)
        override val id: UUID,
        override val positionX: Double,
        override val positionY: Double,
        override val scale: Double,
        override val opacity: Double,
        val up: InputBinding,
        val down: InputBinding,
        val left: InputBinding,
        val right: InputBinding,

        /**
         * Custom sprite file names for the base and the thumb knob. Independent of each other, whichever is unset falls back to the default drawn look
         */
        val sprite: String? = null,
        val spriteThumb: String? = null,
    ) : GamepadElement()

    @Serializable
    @SerialName("AnalogJoystick")
    data class AnalogJoystick(
        @Serializable(with = UUIDAsStringSerializer::class)
        override val id: UUID,
        override val positionX: Double,
        override val positionY: Double,
        override val scale: Double,
        override val opacity: Double,
        val stick: GamepadStick,
        /** Which controller slot (0-based) this stick feeds. The on-screen pad is player 1 by default. */
        val device: Int = 0,

        /**
         * Custom sprite file names for the base and the thumb knob. Independent of each other, whichever is unset falls back to the default drawn look
         */
        val sprite: String? = null,
        val spriteThumb: String? = null,
    ) : GamepadElement()

    @Serializable
    @SerialName("Menu")
    data class Menu(
        @Serializable(with = UUIDAsStringSerializer::class)
        override val id: UUID,
        override val positionX: Double,
        override val positionY: Double,
        override val scale: Double,
        override val opacity: Double,

        /**
         * Custom sprite file name inside the layout sprites pool. No pressed variant, the menu button has no active state
         */
        val sprite: String? = null,
    ) : GamepadElement()

    @Serializable
    @SerialName("MouseButton")
    data class MouseButton(
        @Serializable(with = UUIDAsStringSerializer::class)
        override val id: UUID,
        override val positionX: Double,
        override val positionY: Double,
        override val scale: Double,
        override val opacity: Double,
        val button: GmlMouseButton,
        val toggle: Boolean,

        /**
         * Custom sprite file names inside the layout sprites pool. [spritePressed] shows while active and is only meaningful when [sprite] is set
         */
        val sprite: String? = null,
        val spritePressed: String? = null,
    ) : GamepadElement()

    @Serializable
    @SerialName("FastForward")
    data class FastForward(
        @Serializable(with = UUIDAsStringSerializer::class)
        override val id: UUID,
        override val positionX: Double,
        override val positionY: Double,
        override val scale: Double,
        override val opacity: Double,
        val speed: Float,
        val toggle: Boolean,

        /**
         * Custom sprite file names inside the layout sprites pool. [spritePressed] shows while active and is only meaningful when [sprite] is set
         */
        val sprite: String? = null,
        val spritePressed: String? = null,
    ) : GamepadElement()
}