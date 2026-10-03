package net.perfectdreams.butterscotch.android.library

import androidx.compose.ui.unit.IntSize
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import net.perfectdreams.butterscotch.UUIDAsStringSerializer
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import java.util.UUID

@Serializable
data class GameEntry(
    @Serializable(with = UUIDAsStringSerializer::class)
    val id: UUID,
    val title: String,
    val gameType: GameType,
    val importedAtMillis: Long,
    val favorited: Boolean,
    val saveSlots: List<SaveSlot>,
    /** Bumped whenever the icon file is rewritten, so launcher UI invalidates its bitmap cache. */
    val iconRevision: Long = 0,
    @Serializable(with = UUIDAsStringSerializer::class)
    val portraitLayout: UUID = LayoutLibrary.DEFAULT_PORTRAIT_LAYOUT,
    @Serializable(with = UUIDAsStringSerializer::class)
    val landscapeLayout: UUID = LayoutLibrary.DEFAULT_LANDSCAPE_LAYOUT,
    /** OS reported to the game through GML's os_type / os_* builtins. Defaults to Windows, which is what the C runner hardcoded before this was selectable. */
    val runnerOs: RunnerOs = RunnerOs.WINDOWS,
    /** When true, physical controllers (Bluetooth/USB gamepads) feed the GML gamepad_* builtins. Default on; turn off for games that misbehave with a controller attached. */
    val enablePhysicalControllers: Boolean = true,
    /** When true, a physical (USB/Bluetooth) keyboard feeds the GML keyboard_* builtins. Default on; turn off for games that misbehave with a keyboard attached. */
    val enablePhysicalKeyboard: Boolean = true,
    val enableWidescreenHack: Boolean = false,
    val postProcessing: PostProcessingSettings = PostProcessingSettings(),
) {
    // Mirrors the YoYoOperatingSystem enum in Butterscotch's runner.h. nativeValue MUST match the
    // C enum's integer value, since it is passed straight through startRunner to runner->osType.
    // The OS_LLVM_* debug variants (65536+) are intentionally omitted; they are internal build
    // flavors, not meaningful targets to pick for a WAD.
    @Serializable
    enum class RunnerOs(val nativeValue: Int, val fancyName: String, val displayResolution: IntSize? = null) {
        WINDOWS(0, "Windows"),
        MACOSX(1, "macOS"),
        PSP(2, "PSP"),
        IOS(3, "iOS"),
        ANDROID(4, "Android"),
        SYMBIAN(5, "Symbian"),
        LINUX(6, "Linux"),
        WINPHONE(7, "Windows Phone"),
        TIZEN(8, "Tizen"),
        WIN8NATIVE(9, "Windows 8 Native"),
        WIIU(10, "Wii U"),
        THREEDS(11, "3DS"),
        PSVITA(12, "PS Vita", IntSize(960, 544)),
        BB10(13, "BlackBerry 10"),
        PS4(14, "PS4", IntSize(1920, 1080)),
        XBOXONE(15, "Xbox One", IntSize(1920, 1080)),
        PS3(16, "PS3", IntSize(1920, 1080)),
        XBOX360(17, "Xbox 360", IntSize(1920, 1080)),
        UWP(18, "UWP"),
        AMAZON(19, "Amazon"),
        SWITCH(20, "Switch", IntSize(1280, 720)),
    }

    @Serializable
    sealed class GameType {
        @Serializable
        @SerialName("GameMakerStudio")
        class GameMakerStudio(
            val wadVersion: Int,
            val filename: String
        ) : GameType()

        // We keep it like this for when we decide to add new GameMaker versions :3
    }

    @Serializable
    data class SaveSlot(
        @Serializable(with = UUIDAsStringSerializer::class)
        val id: UUID,
        val active: Boolean,
        val fancyName: String,
    )

    // Which fullscreen post-processing pass the host blits the game through. OFF is the plain passthrough blit
    @Serializable
    enum class PostProcessingShader(val fancyName: String) {
        OFF("Off"),
        CRT("CRT Shader"),
    }

    // Per-effect strengths for the CRT shader, each a 0.0..1.0 fraction fed straight to the matching shader uniform. 1.0 keeps the shader's baseline look, 0.0 disables that effect
    @Serializable
    data class CrtSettings(
        val curvature: Double = 1.0,
        val aberration: Double = 1.0,
        val halation: Double = 1.0,
        val scanlines: Double = 1.0,
        val mask: Double = 1.0,
        val vignette: Double = 1.0,
    )

    @Serializable
    data class PostProcessingSettings(
        val shader: PostProcessingShader = PostProcessingShader.OFF,
        val crt: CrtSettings = CrtSettings(),
    )
}