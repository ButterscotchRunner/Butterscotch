package net.perfectdreams.butterscotch.android.components

import android.graphics.Bitmap
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import net.perfectdreams.butterscotch.android.library.GameEntry
import java.util.UUID

/**
 * The editable fields of [MetadataForm], hoisted into a single state holder.
 *
 * The form mutates these directly; the screens read them back when committing (Import button, or on back for Game Settings).
 */
@Stable
class GameMetadataFormState(
    initialTitle: String,
    initialIcon: Bitmap?,
    initialPortraitLayout: UUID,
    initialLandscapeLayout: UUID,
    initialRunnerOs: GameEntry.RunnerOs,
    initialEnablePhysicalControllers: Boolean,
    initialEnablePhysicalKeyboard: Boolean,
    initialEnableWidescreenHack: Boolean,
    initialPostProcessing: GameEntry.PostProcessingSettings,
) {
    var title by mutableStateOf(initialTitle)
    var selectedIcon by mutableStateOf(initialIcon)
    var portraitLayout by mutableStateOf(initialPortraitLayout)
    var landscapeLayout by mutableStateOf(initialLandscapeLayout)
    var runnerOs by mutableStateOf(initialRunnerOs)
    var enablePhysicalControllers by mutableStateOf(initialEnablePhysicalControllers)
    var enablePhysicalKeyboard by mutableStateOf(initialEnablePhysicalKeyboard)
    var enableWidescreenHack by mutableStateOf(initialEnableWidescreenHack)
    var postProcessing by mutableStateOf(initialPostProcessing)

    val titleTrimmed: String get() = title.trim()
}

// key resets the holder when the underlying game changes (a new staged import, a different entry)
@Composable
fun rememberGameMetadataFormState(
    key: Any?,
    title: String,
    icon: Bitmap?,
    portraitLayout: UUID,
    landscapeLayout: UUID,
    runnerOs: GameEntry.RunnerOs,
    enablePhysicalControllers: Boolean,
    enablePhysicalKeyboard: Boolean,
    enableWidescreenHack: Boolean,
    postProcessing: GameEntry.PostProcessingSettings = GameEntry.PostProcessingSettings()
): GameMetadataFormState = remember(key) {
    GameMetadataFormState(
        initialTitle = title,
        initialIcon = icon,
        initialPortraitLayout = portraitLayout,
        initialLandscapeLayout = landscapeLayout,
        initialRunnerOs = runnerOs,
        initialEnablePhysicalControllers = enablePhysicalControllers,
        initialEnablePhysicalKeyboard = enablePhysicalKeyboard,
        initialEnableWidescreenHack = enableWidescreenHack,
        initialPostProcessing = postProcessing
    )
}
