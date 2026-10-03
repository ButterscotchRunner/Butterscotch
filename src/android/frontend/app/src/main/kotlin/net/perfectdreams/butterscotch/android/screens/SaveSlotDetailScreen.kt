package net.perfectdreams.butterscotch.android.screens

import android.content.Context
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.CallMade
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import net.perfectdreams.butterscotch.android.SaveSlotZip
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.library.GameLibrary
import java.io.File
import java.text.DateFormat
import java.util.Date
import java.util.UUID

/**
 * Per-slot management: make active, import zip, export zip, rename, delete, plus a read-only file
 * list of everything currently inside the slot. Mirrors how the web build handles per-game saves
 * (`exportSavesAsZip` / `importSavesIntoOpfs`), with active-slot tracking layered on top.
 *
 * Pops itself if the game/slot disappears.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SaveSlotDetailScreen(
    library: GameLibrary,
    gameId: UUID,
    slotId: String,
    nav: NavHostController,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val entry = library.findById(gameId) ?: return
    val slotUuid = remember(slotId) { UUID.fromString(slotId) }
    val slot = entry.saveSlots.firstOrNull { it.id == slotUuid } ?: return

    val slotDir = library.slotDir(entry, slotUuid)

    // Bump this to force the file-list LaunchedEffect to re-read the directory after
    // import/export/etc. — Compose can't observe FS changes on its own.
    var fileListVersion by remember { mutableIntStateOf(0) }
    var files by remember { mutableStateOf<List<SlotFile>>(emptyList()) }
    LaunchedEffect(fileListVersion, slotDir.absolutePath) {
        files = withContext(Dispatchers.IO) {
            if (!slotDir.exists()) emptyList()
            else slotDir.walkTopDown()
                .filter { it.isFile }
                .map { SlotFile(it.relativeTo(slotDir).invariantSeparatorsPath, it.length(), it.lastModified()) }
                .sortedBy { it.relativePath }
                .toList()
        }
    }

    var showRenameDialog by remember { mutableStateOf(false) }
    var showCopyDialog by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }
    var showOverwriteConfirm by remember { mutableStateOf<Uri?>(null) }
    var busy by remember { mutableStateOf(false) }

    val exportLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/zip"),
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        busy = true
        scope.launch {
            try {
                val count = SaveSlotZip.exportSlotToZip(context, slotDir, uri)
                Toast.makeText(context, "Exported $count file${if (count == 1) "" else "s"}", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(context, "Export failed: ${e.message ?: "unknown error"}", Toast.LENGTH_LONG).show()
            } finally {
                busy = false
            }
        }
    }

    val importLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        if (files.isNotEmpty()) {
            // Confirm before clobbering existing files (matches the web build's "replace, don't merge").
            showOverwriteConfirm = uri
        } else {
            runImport(context, scope, uri, slotDir, onBusyChange = { busy = it }, onDone = { fileListVersion++ })
        }
    }

    Scaffold(
        topBar = {
            ButterscotchTopBar(
                title = { Text(slot.fancyName) },
                nav = nav,
                navigationIcon = { ButterscotchBackButton(nav) },
            )
        },
    ) { innerPadding ->
        // Single LazyColumn so the action rows and the file list scroll as one — a plain Column
        // would leave the action rows pinned and only let the file LazyColumn scroll, which on
        // tall content makes the bottom rows unreachable.
        LazyColumn(
            Modifier.fillMaxSize().padding(innerPadding),
        ) {
            item("action-active") {
                if (!slot.active) {
                    ActionRow(
                        icon = Icons.Filled.CheckCircle,
                        title = "Make active",
                        subtitle = "Use this slot the next time the game launches",
                        enabled = !busy,
                        onClick = {
                            library.setActiveSlot(entry.id, slotUuid)
                            Toast.makeText(context, "${slot.fancyName} is now active", Toast.LENGTH_SHORT).show()
                        },
                    )
                } else {
                    ActionRow(
                        icon = Icons.Filled.CheckCircle,
                        title = "Active slot",
                        subtitle = "This slot is currently loaded by the game",
                        enabled = false,
                        onClick = {},
                    )
                }
            }
            item("action-import") {
                ActionRow(
                    icon = Icons.AutoMirrored.Filled.CallMade,
                    title = "Import saves",
                    subtitle = "Replace this slot with files from a .zip",
                    enabled = !busy,
                    iconModifier = Modifier.graphicsLayer(scaleY = -1f),
                    onClick = { importLauncher.launch(arrayOf("application/zip")) },
                )
            }
            item("action-export") {
                ActionRow(
                    icon = Icons.AutoMirrored.Filled.CallMade,
                    title = "Export saves",
                    subtitle = "Save this slot's files as a .zip",
                    enabled = !busy && files.isNotEmpty(),
                    onClick = {
                        val sanitized = slot.fancyName.replace(Regex("[^A-Za-z0-9._-]"), "_")
                        exportLauncher.launch("${entry.title}-$sanitized.zip")
                    },
                )
            }
            item("action-copy") {
                ActionRow(
                    icon = Icons.Filled.ContentCopy,
                    title = "Duplicate slot",
                    subtitle = "Create a new slot with a copy of these files",
                    enabled = !busy,
                    onClick = { showCopyDialog = true },
                )
            }
            item("action-rename") {
                ActionRow(
                    icon = Icons.Filled.Edit,
                    title = "Rename",
                    subtitle = "Change the display name of this slot",
                    enabled = !busy,
                    onClick = { showRenameDialog = true },
                )
            }
            item("action-delete") {
                ActionRow(
                    icon = Icons.Filled.Delete,
                    title = "Delete slot",
                    subtitle = if (entry.saveSlots.size <= 1) "Can't delete the last save slot"
                    else "Removes this slot's files permanently",
                    titleColor = MaterialTheme.colorScheme.error,
                    enabled = !busy && entry.saveSlots.size > 1,
                    onClick = { showDeleteDialog = true },
                )
            }

            item("files-header") {
                HorizontalDivider()
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 24.dp, vertical = 16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("Files", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(0.dp).fillMaxWidth().weight(1f))
                    Text("${files.size}", style = MaterialTheme.typography.bodyMedium)
                }
            }
            if (files.isEmpty()) {
                item("files-empty") {
                    Box(Modifier.fillMaxWidth().padding(24.dp), contentAlignment = Alignment.Center) {
                        Text("This slot has no save files yet.")
                    }
                }
            } else {
                items(files, key = { it.relativePath }) { f ->
                    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
                        Text(f.relativePath, style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "${formatBytes(f.size)} · ${DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(f.modifiedMillis))}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }
        }
    }

    if (showRenameDialog) {
        RenameDialog(
            current = slot.fancyName,
            onConfirm = { newName ->
                library.renameSlot(entry.id, slotUuid, newName.ifBlank { slot.fancyName })
                showRenameDialog = false
            },
            onDismiss = { showRenameDialog = false },
        )
    }
    if (showCopyDialog) {
        CopySlotDialog(
            defaultName = "${slot.fancyName} (Copy)",
            onConfirm = { newName ->
                showCopyDialog = false
                val finalName = newName.ifBlank { "${slot.fancyName} (Copy)" }
                try {
                    library.copySlot(entry.id, slotUuid, finalName)
                    Toast.makeText(context, "Duplicated to \"$finalName\"", Toast.LENGTH_SHORT).show()
                } catch (e: Exception) {
                    Toast.makeText(context, "Duplicate failed: ${e.message ?: "unknown error"}", Toast.LENGTH_LONG).show()
                }
            },
            onDismiss = { showCopyDialog = false },
        )
    }
    if (showDeleteDialog) {
        AlertDialog(
            onDismissRequest = { showDeleteDialog = false },
            title = { Text("Delete ${slot.fancyName}?") },
            text = { Text("This permanently removes the slot's save files. This can't be undone.") },
            confirmButton = {
                TextButton(onClick = {
                    showDeleteDialog = false
                    nav.popBackStack()
                    library.removeSlot(entry.id, slotUuid)
                }) { Text("Delete", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteDialog = false }) { Text("Cancel") }
            },
        )
    }
    showOverwriteConfirm?.let { uri ->
        AlertDialog(
            onDismissRequest = { showOverwriteConfirm = null },
            title = { Text("Replace existing saves?") },
            text = { Text("This slot already has ${files.size} file${if (files.size == 1) "" else "s"}. Importing will replace them.") },
            confirmButton = {
                TextButton(onClick = {
                    showOverwriteConfirm = null
                    runImport(context, scope, uri, slotDir, onBusyChange = { busy = it }, onDone = { fileListVersion++ })
                }) { Text("Replace", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { showOverwriteConfirm = null }) { Text("Cancel") }
            },
        )
    }
}

private fun runImport(
    context: Context,
    scope: CoroutineScope,
    uri: Uri,
    slotDir: File,
    onBusyChange: (Boolean) -> Unit,
    onDone: () -> Unit,
) {
    onBusyChange(true)
    scope.launch {
        try {
            val count = SaveSlotZip.importZipIntoSlot(context, uri, slotDir)
            Toast.makeText(context, "Imported $count file${if (count == 1) "" else "s"}", Toast.LENGTH_SHORT).show()
            onDone()
        } catch (e: Exception) {
            Toast.makeText(context, "Import failed: ${e.message ?: "unknown error"}", Toast.LENGTH_LONG).show()
        } finally {
            onBusyChange(false)
        }
    }
}

@Composable
private fun ActionRow(
    icon: ImageVector,
    title: String,
    subtitle: String,
    enabled: Boolean,
    titleColor: Color = Color.Unspecified,
    iconModifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val rowModifier = if (enabled) Modifier.clickable(onClick = onClick) else Modifier
    Row(
        Modifier.fillMaxWidth().then(rowModifier).padding(horizontal = 24.dp, vertical = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            icon,
            contentDescription = null,
            modifier = iconModifier,
            tint = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f),
        )
        Spacer(Modifier.fillMaxWidth(0f).padding(end = 16.dp))
        Column(Modifier.padding(start = 16.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                color = if (enabled) titleColor else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f),
            )
            Text(
                subtitle,
                style = MaterialTheme.typography.bodyMedium,
                color = if (enabled) Color.Unspecified else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f),
            )
        }
    }
    HorizontalDivider()
}

@Composable
private fun RenameDialog(
    current: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Rename slot") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Name") },
                singleLine = true,
            )
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name) }) { Text("Save") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

@Composable
private fun CopySlotDialog(
    defaultName: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember { mutableStateOf(defaultName) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Duplicate slot") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Name for the new slot") },
                singleLine = true,
            )
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name) }) { Text("Duplicate") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

private data class SlotFile(
    val relativePath: String,
    val size: Long,
    val modifiedMillis: Long,
)

private fun formatBytes(bytes: Long): String {
    if (bytes < 1024) return "$bytes B"
    val kb = bytes / 1024.0
    if (kb < 1024) return String.format("%.1f KB", kb)
    val mb = kb / 1024.0
    return String.format("%.1f MB", mb)
}
