package net.perfectdreams.butterscotch.android.screens

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import io.ktor.client.request.get
import io.ktor.client.statement.bodyAsBytes
import io.ktor.client.statement.bodyAsText
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import net.perfectdreams.butterscotch.android.BuildConfig
import net.perfectdreams.butterscotch.android.ButterscotchUtils
import net.perfectdreams.butterscotch.android.GameImporter
import net.perfectdreams.butterscotch.android.R
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.components.FrameAnimationImage
import net.perfectdreams.butterscotch.android.components.MetadataForm
import net.perfectdreams.butterscotch.android.components.rememberGameMetadataFormState
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameEntry
import net.perfectdreams.butterscotch.android.library.GameEntry.GameType.GameMakerStudio
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.network.SampleGamesResponse
import java.util.UUID

/**
 * Folder-picker → copy → configure flow. Lives under [net.perfectdreams.butterscotch.android.Route.ImportGame] in the nav graph.
 *
 * State machine:
 *   Intro -> (user taps "Select folder") -> picker -> Copying -> Configure -> commit -> onDone()
 *                                                              \-> Cancel -> deletes staging, back to Intro
 *                                       -> MissingWad / Failure error screen -> back to Intro
 */
private sealed interface ImportUIState {
    data object Intro : ImportUIState
    data object SampleList : ImportUIState
    class Copying : ImportUIState {
        var currentFile by mutableStateOf<String?>(null)
    }
    data class Configure(val result: GameImporter.Result.Success) : ImportUIState
    data class Error(val message: String, val previous: ImportUIState = Intro) : ImportUIState
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ImportScreen(
    library: GameLibrary,
    layoutLibrary: LayoutLibrary,
    nav: NavHostController
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var state by remember { mutableStateOf<ImportUIState>(ImportUIState.Intro) }

    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
        if (uri == null) {
            // User cancelled the picker — stay on Intro.
            return@rememberLauncherForActivityResult
        }
        val copyingState = ImportUIState.Copying()
        state = copyingState
        scope.launch {
            state = when (val result = GameImporter.import(context, uri, library) { copyingState.currentFile = it }) {
                is GameImporter.Result.Success -> ImportUIState.Configure(result)
                is GameImporter.Result.MissingWad -> ImportUIState.Error(
                    "Missing WAD in folder!\n\nExpected one of: ${GameImporter.WAD_FILENAMES.joinToString(", ")}"
                )
                is GameImporter.Result.Failure -> ImportUIState.Error(result.message)
            }
        }
    }

    val pickZip = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri == null) {
            // User cancelled the picker — stay on Intro.
            return@rememberLauncherForActivityResult
        }
        val copyingState = ImportUIState.Copying()
        state = copyingState
        scope.launch {
            state = when (val result = GameImporter.importZip(context, uri, library) { copyingState.currentFile = it }) {
                is GameImporter.Result.Success -> ImportUIState.Configure(result)
                is GameImporter.Result.MissingWad -> ImportUIState.Error(
                    "Missing WAD in ZIP!\n\nExpected one of: ${GameImporter.WAD_FILENAMES.joinToString(", ")}"
                )
                is GameImporter.Result.Failure -> ImportUIState.Error(result.message)
            }
        }
    }

    Scaffold(
        topBar = {
            ButterscotchTopBar(
                {
                    Text(
                        when (state) {
                            is ImportUIState.Configure -> "Configure Game"
                            else -> "Add Game"
                        }
                    )
                },
                nav,
                navigationIcon = {
                    when (val s = state) {
                        is ImportUIState.Configure -> {
                            ButterscotchBackButton(nav) {
                                library.discardStaging(s.result.staged)
                                state = ImportUIState.Intro
                            }
                        }
                        else -> ButterscotchBackButton(nav)
                    }

                }
            )
        },
    ) { innerPadding ->
        Box(Modifier.fillMaxSize().padding(innerPadding).padding(24.dp)) {
            when (val s = state) {
                ImportUIState.Intro -> IntroPane(
                    onSelectFolder = { pickFolder.launch(null) },
                    onSelectZip = { pickZip.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) },
                    onSelectSample = {
                        state = ImportUIState.SampleList
                    }
                )
                is ImportUIState.Copying -> CopyingPane(s.currentFile)
                is ImportUIState.Configure -> ConfigurePane(
                    result = s.result,
                    layoutLibrary = layoutLibrary,
                    onSave = { title, icon, portraitLayout, landscapeLayout, runnerOs, enablePhysicalControllers, enablePhysicalKeyboard, enableWidescreenHack, postProcessing ->
                        library.commit(
                            s.result.staged,
                            title,
                            GameMakerStudio(
                                s.result.wadVersion,
                                s.result.wadFilename
                            ),
                            icon = icon,
                            portraitLayout = portraitLayout,
                            landscapeLayout = landscapeLayout,
                            runnerOs = runnerOs,
                            enablePhysicalControllers = enablePhysicalControllers,
                            enablePhysicalKeyboard = enablePhysicalKeyboard,
                            enableWidescreenHack = enableWidescreenHack,
                            postProcessing = postProcessing
                        )
                        nav.popBackStack()
                    }
                )
                is ImportUIState.Error -> ErrorPane(
                    message = s.message,
                    onDismiss = { state = s.previous },
                )

                ImportUIState.SampleList -> {
                    var gameList by remember { mutableStateOf<SampleGamesResponse?>(null) }
                    LaunchedEffect(Unit) {
                        try {
                            gameList = ButterscotchUtils.http.get("${BuildConfig.API_BASE_URL}/api/v1/samples").let {
                                Json.decodeFromString<SampleGamesResponse>(it.bodyAsText())
                            }
                        } catch (e: Exception) {
                            Toast.makeText(context, "Failed to connect to server!", Toast.LENGTH_SHORT).show()
                            state = ImportUIState.Intro
                        }
                    }

                    if (gameList != null) {
                        Column {
                            Text("Sample games that you can download and play in Butterscotch!")

                            Spacer(Modifier.height(24.dp))
                            for (game in gameList!!.games) {
                                SampleGameTile(game) {
                                    val copyingState = ImportUIState.Copying()
                                    state = copyingState
                                    scope.launch {
                                        try {
                                            val zipAsBytes = ButterscotchUtils.http.get("/samples/${game.slug}/${game.version}/game.zip").bodyAsBytes()
                                            val iconAsBytes = ButterscotchUtils.http.get("/samples/${game.slug}/${game.version}/icon.png").bodyAsBytes()
                                            state = when (val result = GameImporter.importZip(library, zipAsBytes, game.name, iconAsBytes) { copyingState.currentFile = it }) {
                                                is GameImporter.Result.Success -> ImportUIState.Configure(result)
                                                is GameImporter.Result.MissingWad -> ImportUIState.Error(
                                                    "Missing WAD in ZIP!\n\nExpected one of: ${GameImporter.WAD_FILENAMES.joinToString(", ")}"
                                                )
                                                is GameImporter.Result.Failure -> ImportUIState.Error(result.message)
                                            }
                                        } catch (e: Exception) {
                                            Toast.makeText(context, "Failed to connect to server!", Toast.LENGTH_SHORT).show()
                                            state = ImportUIState.SampleList
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        Column(
                            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
                            verticalArrangement = Arrangement.Center,
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            CircularProgressIndicator()
                            Spacer(Modifier.height(16.dp))
                            Text("Loading...")
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun IntroPane(onSelectFolder: () -> Unit, onSelectZip: () -> Unit, onSelectSample: () -> (Unit)) {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        FrameAnimationImage(
            frames = listOf(R.drawable.import_1, R.drawable.import_2),
            frameDurationMs = 500,
            contentDescription = null,
            originalImageWidth = 32,
            originalImageHeight = 22,
            scaleUpBy = 4
        )
        Spacer(Modifier.height(24.dp))
        Text(
            "Select a folder or a ZIP with a GameMaker WAD file (${GameImporter.WAD_FILENAMES.joinToString(", ")})",
            style = MaterialTheme.typography.bodyLarge,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(24.dp))
        Button(onClick = onSelectFolder) {
            Text("Import Folder")
        }
        Spacer(Modifier.height(12.dp))
        Button(onClick = onSelectZip) {
            Text("Import ZIP")
        }
        Spacer(Modifier.height(12.dp))
        Button(onClick = onSelectSample) {
            Text("Import Sample")
        }
    }
}

@Composable
private fun CopyingPane(currentFile: String?) {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        CircularProgressIndicator()
        Spacer(Modifier.height(16.dp))
        Text("Copying game files...")
        if (currentFile != null) {
            Text(currentFile)
        }
    }
}

@Composable
private fun ConfigurePane(
    result: GameImporter.Result.Success,
    layoutLibrary: LayoutLibrary,
    onSave: (title: String, icon: Bitmap?, portraitLayout: UUID, landscapeLayout: UUID, runnerOs: GameEntry.RunnerOs, enablePhysicalControllers: Boolean, enablePhysicalKeyboard: Boolean, enableWidescreenHack: Boolean, postProcessing: GameEntry.PostProcessingSettings) -> Unit
) {
    // suggestedTitle comes from GEN8 (may be null for pre-WAD10 games); fall back to the folder
    // name so the user never sees an empty field.
    val initial = result.suggestedTitle

    // The game is not committed yet, so there is no entry to compare against: these start from the same defaults commit() would apply and are passed straight through onSave at commit time.
    val state = rememberGameMetadataFormState(
        key = result.staged.id,
        title = initial,
        icon = result.iconCandidates.firstOrNull()?.bitmap,
        portraitLayout = LayoutLibrary.DEFAULT_PORTRAIT_LAYOUT,
        landscapeLayout = LayoutLibrary.DEFAULT_LANDSCAPE_LAYOUT,
        runnerOs = GameEntry.RunnerOs.WINDOWS,
        enablePhysicalControllers = true,
        enablePhysicalKeyboard = true,
        enableWidescreenHack = false
    )

    MetadataForm(
        layoutLibrary = layoutLibrary,
        state = state,
        loadCandidates = { result.iconCandidates },
        saveEnabled = state.title.isNotBlank(),
        saveLabel = "Import",
        onSave = { onSave(state.title.ifBlank { initial }, state.selectedIcon, state.portraitLayout, state.landscapeLayout, state.runnerOs, state.enablePhysicalControllers, state.enablePhysicalKeyboard, state.enableWidescreenHack, state.postProcessing) },
    )
}

@Composable
private fun ErrorPane(message: String, onDismiss: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(message, style = MaterialTheme.typography.bodyLarge)
        Spacer(Modifier.height(24.dp))
        Button(onClick = onDismiss) { Text("OK") }
    }
}

@Composable
private fun SampleGameIcon(
    entry: SampleGamesResponse.Game,
    modifier: Modifier = Modifier,
) {
    var bitmap by remember { mutableStateOf<Bitmap?>(null) }

    LaunchedEffect(Unit) {
        try {
            bitmap = ButterscotchUtils.http.get("/samples/${entry.slug}/${entry.version}/icon.png")
                .bodyAsBytes()
                .let { BitmapFactory.decodeByteArray(it, 0, it.size) }
        } catch (e: Exception) {
            // Keep the icon empty, the list itself already shows a toast if the server is unreachable
        }
    }

    val _bitmap = bitmap
    if (_bitmap != null) {
        Image(
            painter = BitmapPainter(_bitmap.asImageBitmap(), filterQuality = FilterQuality.None),
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = modifier,
        )
    }
}

@Composable
private fun SampleGameTile(
    entry: SampleGamesResponse.Game,
    onSelect: () -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    val chevronRotation by animateFloatAsState(if (expanded) 180f else 0f)

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.clickable { expanded = !expanded }.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Row(
                    modifier = Modifier
                        .weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    SampleGameIcon(entry, modifier = Modifier.size(42.dp))

                    Column(modifier = Modifier.padding(start = 12.dp)) {
                        Text(entry.name, style = MaterialTheme.typography.titleMedium.copy(lineHeight = 20.sp))
                        Text(
                            entry.author,
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }

                Icon(
                    imageVector = Icons.Filled.ExpandMore,
                    contentDescription = if (expanded) "Collapse" else "Expand",
                    modifier = Modifier.rotate(chevronRotation),
                )
            }

            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(),
                exit = shrinkVertically(),
            ) {
                Column {
                    Spacer(Modifier.height(12.dp))

                    HorizontalDivider()

                    Spacer(Modifier.height(12.dp))

                    val paragraphs = entry.description
                        .split(Regex("\\n\\s*\\n"))   // blank line(s) separate paragraphs
                        .map { it.trim() }
                        .filter { it.isNotEmpty() }

                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (paragraph in paragraphs) {
                            Text(paragraph, style = MaterialTheme.typography.bodyMedium)
                        }
                    }

                    Spacer(Modifier.height(12.dp))

                    Button(onClick = onSelect, modifier = Modifier.align(Alignment.End)) {
                        Text("Import")
                    }
                }
            }
        }
    }
}