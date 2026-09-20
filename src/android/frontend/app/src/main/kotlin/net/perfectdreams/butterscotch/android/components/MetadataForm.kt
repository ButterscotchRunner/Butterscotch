package net.perfectdreams.butterscotch.android.components

import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Slider
import androidx.compose.material3.TextButton
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import net.perfectdreams.butterscotch.android.layouts.GamepadLayout
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameEntry
import net.perfectdreams.butterscotch.android.pe.IconCandidate
import java.util.UUID
import kotlin.math.roundToInt

/**
 * A shared game metadata editor.
 *
 * When [onSave] is null no save button is rendered, for screens that persist the state themselves (e.g. on back).
 */
@Composable
fun MetadataForm(
    layoutLibrary: LayoutLibrary,
    state: GameMetadataFormState,
    onSave: (() -> Unit)?,
    saveEnabled: Boolean = true,
    saveLabel: String = "Save",
    loadCandidates: suspend () -> List<IconCandidate>,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var pickerOpen by remember { mutableStateOf(false) }

    val pickCustom = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null)
            return@rememberLauncherForActivityResult

        scope.launch {
            val bitmap = withContext(Dispatchers.IO) { decodeUriToBitmap(context, uri) }
            if (bitmap != null) state.selectedIcon = bitmap
        }
    }

    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        OutlinedTextField(
            value = state.title,
            onValueChange = { state.title = it },
            label = { Text("Title") },
            singleLine = true,
            isError = state.title.isBlank(),
            modifier = Modifier.fillMaxWidth(),
        )

        Spacer(Modifier.height(16.dp))

        IconSection(
            selected = state.selectedIcon,
            onClick = {
                pickerOpen = true
            },
        )

        LayoutDropdown(
            label = "Portrait Layout",
            selectedId = state.portraitLayout,
            options = layoutLibrary.entries.filter { it.orientation == GamepadLayout.GamepadTargetOrientation.PORTRAIT },
            onSelect = { id -> state.portraitLayout = id },
        )

        Spacer(Modifier.height(16.dp))

        LayoutDropdown(
            label = "Landscape Layout",
            selectedId = state.landscapeLayout,
            options = layoutLibrary.entries.filter { it.orientation == GamepadLayout.GamepadTargetOrientation.LANDSCAPE },
            onSelect = { id -> state.landscapeLayout = id },
        )

        Spacer(Modifier.height(16.dp))

        OsDropdown(
            selected = state.runnerOs,
            onSelect = { state.runnerOs = it },
        )

        Spacer(Modifier.height(16.dp))

        InputToggle(
            title = "Enable physical controllers",
            subtitle = null,
            checked = state.enablePhysicalControllers,
            onChange = { state.enablePhysicalControllers = it },
        )

        Spacer(Modifier.height(16.dp))

        InputToggle(
            title = "Enable physical keyboard",
            subtitle = null,
            checked = state.enablePhysicalKeyboard,
            onChange = { state.enablePhysicalKeyboard = it },
        )

        Spacer(Modifier.height(24.dp))

        InputToggle(
            title = "Enable Widescreen Hack",
            subtitle = "May cause visual glitches",
            checked = state.enableWidescreenHack,
            onChange = { state.enableWidescreenHack = it },
        )

        Spacer(Modifier.height(24.dp))

        PostProcessingSection(
            settings = state.postProcessing,
            onChange = { state.postProcessing = it },
        )

        if (onSave != null) {
            Spacer(Modifier.height(24.dp))

            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterEnd) {
                Column(
                    horizontalAlignment = Alignment.End,
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(onClick = onSave, enabled = saveEnabled) { Text(saveLabel) }
                }
            }
        }
    }

    // We keep it outside to avoid rescaning on every open
    var candidates by remember { mutableStateOf<CandidatesState>(CandidatesState.Loading) }

    if (pickerOpen) {
        LaunchedEffect(Unit) {
            candidates = try {
                val result = withContext(Dispatchers.IO) {
                    loadCandidates()
                }

                CandidatesState.Loaded(result)
            } catch (e: Exception) {
                Log.w("MetadataForm", "Failed to load icon candidates", e)
                CandidatesState.Failed(e.message ?: e.javaClass.simpleName)
            }
        }

        IconPickerDialog(
            candidates = candidates,
            selected = state.selectedIcon,
            onPickCandidate = {
                state.selectedIcon = it
                pickerOpen = false
            },
            onPickCustom = {
                pickerOpen = false
                pickCustom.launch("image/*")
            },
            onDismiss = { pickerOpen = false },
        )
    }
}

// Exposed dropdown that assigns one of the LayoutLibrary's layouts to this game for an orientation.
// The selection persists immediately via the caller's onSelect. "(unknown)" shows when the assigned
// id has no matching layout (e.g. a default that is not seeded yet).
@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
fun LayoutDropdown(
    label: String,
    selectedId: UUID,
    options: List<GamepadLayout>,
    onSelect: (UUID) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val selectedName = options.firstOrNull { it.id == selectedId }?.fancyName ?: "(unknown)"
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier.fillMaxWidth(),
    ) {
        OutlinedTextField(
            value = selectedName,
            onValueChange = {},
            readOnly = true,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier.menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable).fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { layout ->
                DropdownMenuItem(
                    text = { Text(layout.fancyName) },
                    onClick = {
                        onSelect(layout.id)
                        expanded = false
                    },
                )
            }
        }
    }
}

// Picks which OS the runner reports to the game through GML's os_type / os_* builtins. Staged in the
// form state and committed by the host screen just like the layout dropdowns. Most games never read
// os_type, but chapter-aware or platform-gated titles can branch on it.
@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
private fun OsDropdown(
    selected: GameEntry.RunnerOs,
    onSelect: (GameEntry.RunnerOs) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier.fillMaxWidth(),
    ) {
        OutlinedTextField(
            value = selected.fancyName,
            onValueChange = {},
            readOnly = true,
            label = { Text("Reported OS") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier.menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable).fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            GameEntry.RunnerOs.entries.forEach { os ->
                DropdownMenuItem(
                    text = { Text(os.fancyName) },
                    onClick = {
                        onSelect(os)
                        expanded = false
                    },
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PostProcessingSection(
    settings: GameEntry.PostProcessingSettings,
    onChange: (GameEntry.PostProcessingSettings) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    var crtDialogOpen by remember { mutableStateOf(false) }

    Row(verticalAlignment = Alignment.CenterVertically) {
        ExposedDropdownMenuBox(
            expanded = expanded,
            onExpandedChange = { expanded = it },
            modifier = Modifier.weight(1f),
        ) {
            OutlinedTextField(
                value = settings.shader.fancyName,
                onValueChange = {},
                readOnly = true,
                label = { Text("Post-Processing Shaders") },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                modifier = Modifier.menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable).fillMaxWidth(),
            )
            ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                GameEntry.PostProcessingShader.entries.forEach { shader ->
                    DropdownMenuItem(
                        text = { Text(shader.fancyName) },
                        onClick = {
                            onChange(settings.copy(shader = shader))
                            expanded = false
                        },
                    )
                }
            }
        }

        // Always shown so the row layout stays stable, but only the CRT shader has anything to tweak
        IconButton(onClick = { crtDialogOpen = true }, enabled = settings.shader == GameEntry.PostProcessingShader.CRT) {
            Icon(Icons.Default.Settings, contentDescription = "Customize CRT shader")
        }
    }

    if (crtDialogOpen) {
        CrtSettingsDialog(
            settings = settings.crt,
            onChange = { onChange(settings.copy(crt = it)) },
            onDismiss = { crtDialogOpen = false },
        )
    }
}

// Sliders for each CRT effect strength (0..100%). Edits write straight back through onChange so the preview state
// stays live; the values only persist when the host screen commits the form state.
@Composable
private fun CrtSettingsDialog(
    settings: GameEntry.CrtSettings,
    onChange: (GameEntry.CrtSettings) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text("Done") } },
        title = { Text("CRT Shader") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                CrtSlider("Curvature", settings.curvature) { onChange(settings.copy(curvature = it)) }
                CrtSlider("Chromatic aberration", settings.aberration) { onChange(settings.copy(aberration = it)) }
                CrtSlider("Halation glow", settings.halation) { onChange(settings.copy(halation = it)) }
                CrtSlider("Scanlines", settings.scanlines) { onChange(settings.copy(scanlines = it)) }
                CrtSlider("Phosphor mask", settings.mask) { onChange(settings.copy(mask = it)) }
                CrtSlider("Vignette", settings.vignette) { onChange(settings.copy(vignette = it)) }
            }
        },
    )
}

// The strength is stored as a 0.0..1.0 fraction (matching the shader uniform), but the slider works in whole
// percent for feel, so we scale by 100 at this UI boundary only
@Composable
private fun CrtSlider(label: String, value: Double, onChange: (Double) -> Unit) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label)
            Text("${(value * 100).roundToInt()}%", color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Slider(
            value = (value * 100).toFloat(),
            onValueChange = { onChange(it.roundToInt() / 100.0) },
            valueRange = 0f..100f,
        )
    }
}