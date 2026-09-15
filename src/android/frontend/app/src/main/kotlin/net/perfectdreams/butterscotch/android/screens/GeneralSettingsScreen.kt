package net.perfectdreams.butterscotch.android.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.VideogameAsset
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import net.perfectdreams.butterscotch.android.Route
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.components.InputToggle
import net.perfectdreams.butterscotch.android.input.Haptics
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.settings.SettingsStore
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GeneralSettingsScreen(
    gameLibrary: GameLibrary,
    layoutLibrary: LayoutLibrary,
    settingsStore: SettingsStore,
    nav: NavHostController,
) {
    val context = LocalContext.current
    // Used only to play a sample buzz so the user can feel the strength they picked
    val previewHaptics = remember { Haptics(context) }
    val settings = settingsStore.settings

    Scaffold(
        topBar = {
            ButterscotchTopBar({ Text("Settings") }, nav, navigationIcon = { ButterscotchBackButton(nav) })
        },
    ) { innerPadding ->
        LazyColumn(
            Modifier.fillMaxSize().padding(innerPadding),
        ) {
            item {
                Column(modifier = Modifier.padding(horizontal = 24.dp, vertical = 16.dp)) {
                    InputToggle(
                        "Enable Haptic Feedback",
                        null,
                        settings.enableHapticFeedback
                    ) { enabled ->
                        settingsStore.update { copy(enableHapticFeedback = enabled) }
                    }

                    if (settings.enableHapticFeedback) {
                        Text("Haptic Strength: ${settings.hapticStrength}%")
                        Slider(
                            value = settings.hapticStrength.toFloat(),
                            onValueChange = { settingsStore.update { copy(hapticStrength = it.roundToInt()) } },
                            // Buzz once at the chosen level when the user lets go, so they can feel it
                            onValueChangeFinished = { previewHaptics.tick(settings.hapticStrength) },
                            valueRange = 10f..100f,
                            // 10..100 in steps of 10 = 10 stops, which is 8 ticks between the endpoints
                            steps = 8,
                        )
                    }

                    HorizontalDivider()
                }
            }

            item {
                Column(modifier = Modifier.padding(horizontal = 24.dp, vertical = 16.dp)) {
                    InputToggle(
                        "Hide Virtual Gamepad when using a physical controller",
                        "When enabled, the virtual gamepad will automatically hide when a key is pressed on a physical controller. The virtual gamepad will reappear when touching the screen.",
                        settings.hideVirtualGamepadWhenUsingPhysicalController
                    ) { enabled ->
                        settingsStore.update { copy(hideVirtualGamepadWhenUsingPhysicalController = enabled) }
                    }

                    HorizontalDivider()
                }
            }

            item {
                Column(modifier = Modifier.padding(horizontal = 24.dp, vertical = 16.dp)) {
                    InputToggle(
                        "Hide Virtual Gamepad when using a physical keyboard",
                        "When enabled, the virtual gamepad will automatically hide when a key is pressed on a physical keyboard. The virtual gamepad will reappear when touching the screen.",
                        settings.hideVirtualGamepadWhenUsingPhysicalKeyboard
                    ) { enabled ->
                        settingsStore.update { copy(hideVirtualGamepadWhenUsingPhysicalKeyboard = enabled) }
                    }

                    HorizontalDivider()
                }
            }

            item {
                Row(
                    Modifier.fillMaxWidth().clickable { nav.navigate(Route.LayoutManager) }.padding(horizontal = 24.dp, vertical = 16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Icon(Icons.Filled.VideogameAsset, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.padding(end = 16.dp))
                    Column(Modifier.weight(1f)) {
                        Text("Gamepad Layouts", style = MaterialTheme.typography.titleMedium)
                        Text("Rename, duplicate, or delete on-screen control layouts", style = MaterialTheme.typography.bodyMedium)
                    }
                }
            }
        }
    }
}
