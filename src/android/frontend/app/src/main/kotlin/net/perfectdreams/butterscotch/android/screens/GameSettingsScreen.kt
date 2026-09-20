package net.perfectdreams.butterscotch.android.screens

import android.graphics.BitmapFactory
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.components.MetadataForm
import net.perfectdreams.butterscotch.android.components.rememberGameMetadataFormState
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.pe.scanIconCandidates
import java.util.UUID

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GameSettingsScreen(
    gameLibrary: GameLibrary,
    layoutLibrary: LayoutLibrary,
    gameId: UUID,
    nav: NavHostController,
) {
    val entry = gameLibrary.findById(gameId) ?: return

    val originalIcon = remember {
        val f = gameLibrary.iconFile(entry)
        if (f.exists())
            BitmapFactory.decodeFile(f.absolutePath)
        else
            null
    }

    val state = rememberGameMetadataFormState(
        key = entry.id,
        title = entry.title,
        icon = originalIcon,
        portraitLayout = entry.portraitLayout,
        landscapeLayout = entry.landscapeLayout,
        runnerOs = entry.runnerOs,
        enablePhysicalControllers = entry.enablePhysicalControllers,
        enablePhysicalKeyboard = entry.enablePhysicalKeyboard,
        enableWidescreenHack = entry.enableWidescreenHack,
        postProcessing = entry.postProcessing
    )

    // Changes persist when the screen leaves composition (back press or navigating away), there is no explicit Save button
    // The diffs are computed inside onDispose because the lambda captures the state holder, not the flag values, and we only
    // write back what the user actually touched instead of rewriting (and cache-invalidating) every field
    DisposableEffect(Unit) {
        onDispose {
            if (state.titleTrimmed.isNotBlank() && state.titleTrimmed != entry.title) gameLibrary.setTitle(entry.id, state.titleTrimmed)
            if (state.selectedIcon !== originalIcon) gameLibrary.setIcon(entry.id, state.selectedIcon)
            if (state.portraitLayout != entry.portraitLayout || state.landscapeLayout != entry.landscapeLayout) gameLibrary.update(entry.id) { it.copy(portraitLayout = state.portraitLayout, landscapeLayout = state.landscapeLayout) }
            if (state.runnerOs != entry.runnerOs) gameLibrary.update(entry.id) { it.copy(runnerOs = state.runnerOs) }
            if (state.enablePhysicalControllers != entry.enablePhysicalControllers) gameLibrary.update(entry.id) { it.copy(enablePhysicalControllers = state.enablePhysicalControllers) }
            if (state.enablePhysicalKeyboard != entry.enablePhysicalKeyboard) gameLibrary.update(entry.id) { it.copy(enablePhysicalKeyboard = state.enablePhysicalKeyboard) }
            if (state.enableWidescreenHack != entry.enableWidescreenHack) gameLibrary.update(entry.id) { it.copy(enableWidescreenHack = state.enableWidescreenHack) }
            if (state.postProcessing != entry.postProcessing) gameLibrary.update(entry.id) { it.copy(postProcessing = state.postProcessing) }
        }
    }

    Scaffold(
        topBar = {
            ButterscotchTopBar({ Text("Game Settings") }, nav, navigationIcon = { ButterscotchBackButton(nav) })
        },
    ) { innerPadding ->
        Box(Modifier.fillMaxSize().padding(innerPadding).padding(24.dp)) {
            MetadataForm(
                layoutLibrary = layoutLibrary,
                state = state,
                loadCandidates = { scanIconCandidates(gameLibrary.bundleDir(entry)) },
                onSave = null,
            )
        }
    }
}