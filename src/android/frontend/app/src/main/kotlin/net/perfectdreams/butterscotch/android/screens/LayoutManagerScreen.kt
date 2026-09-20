package net.perfectdreams.butterscotch.android.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Badge
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.layouts.GamepadLayout
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameLibrary
import java.util.UUID

/**
 * Manages the on-screen gamepad layouts. Rename, duplicate, or delete user layouts; the two built-in
 * defaults are shown but locked (they're re-seeded every load). Visual editing still happens in-game.
 *
 * Deletion is safe even for a layout a game points at: [net.perfectdreams.butterscotch.android.GameActivity]
 * falls back to the matching default when the assigned id no longer resolves.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LayoutManagerScreen(
    gameLibrary: GameLibrary,
    layoutLibrary: LayoutLibrary,
    nav: NavHostController,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var renameTarget by remember { mutableStateOf<GamepadLayout?>(null) }
    var deleteTarget by remember { mutableStateOf<GamepadLayout?>(null) }
    // Held so the CreateDocument callback knows which layout to write once the user picks a destination
    var exportTarget by remember { mutableStateOf<GamepadLayout?>(null) }

    // octet-stream instead of application/zip so SAF providers don't "helpfully" append a .zip extension to the .bslayout name
    val exportLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val target = exportTarget
        exportTarget = null
        if (uri == null || target == null) return@rememberLauncherForActivityResult
        scope.launch {
            try {
                withContext(Dispatchers.IO) {
                    context.contentResolver.openOutputStream(uri)?.use { layoutLibrary.exportToZip(target, it) } ?: error("Could not open destination")
                }
                Toast.makeText(context, "Exported ${target.fancyName}", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(context, "Export failed: ${e.message ?: "unknown error"}", Toast.LENGTH_LONG).show()
            }
        }
    }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            try {
                val bytes = withContext(Dispatchers.IO) {
                    context.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: error("Could not open file")
                }
                val imported = layoutLibrary.importFromZip(bytes)
                Toast.makeText(context, "Imported ${imported.fancyName}", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(context, "Import failed: ${e.message ?: "invalid layout file"}", Toast.LENGTH_LONG).show()
            }
        }
    }

    Scaffold(
        topBar = {
            ButterscotchTopBar({ Text("Gamepad Layouts") }, nav, navigationIcon = { ButterscotchBackButton(nav) })
        },
        floatingActionButton = {
            // Wide open filter because shared zips arrive with all sorts of mime types depending on the app that sent them
            FloatingActionButton(onClick = { importLauncher.launch(arrayOf("*/*")) }) {
                Icon(Icons.Filled.Add, contentDescription = "Import layout")
            }
        },
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(innerPadding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(layoutLibrary.entries, key = { it.id.toString() }) { layout ->
                // Reading gameLibrary.entries here keeps the count reactive if games change while this screen is open
                val usedInGames = gameLibrary.entries.count { it.portraitLayout == layout.id || it.landscapeLayout == layout.id }
                LayoutTile(
                    layout = layout,
                    builtIn = layoutLibrary.isBuiltIn(layout.id),
                    usedInGames = usedInGames,
                    onRename = { renameTarget = layout },
                    onDelete = { deleteTarget = layout },
                    onExport = {
                        exportTarget = layout
                        val sanitized = layout.fancyName.replace(Regex("[^A-Za-z0-9._-]"), "_")
                        exportLauncher.launch("$sanitized.bslayout")
                    },
                )
            }
        }
    }

    renameTarget?.let { target ->
        RenameLayoutDialog(
            currentName = target.fancyName,
            onConfirm = { name ->
                layoutLibrary.upsert(target.copy(fancyName = name))
                renameTarget = null
            },
            onDismiss = { renameTarget = null },
        )
    }

    deleteTarget?.let { target ->
        AlertDialog(
            onDismissRequest = { deleteTarget = null },
            title = { Text("Delete ${target.fancyName}?") },
            text = { Text("Games using this layout will fall back to the default. This can't be undone.") },
            confirmButton = {
                TextButton(onClick = {
                    layoutLibrary.remove(target.id)
                    deleteTarget = null
                }) { Text("Delete", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { deleteTarget = null }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun LayoutTile(
    layout: GamepadLayout,
    builtIn: Boolean,
    usedInGames: Int,
    onRename: () -> Unit,
    onDelete: () -> Unit,
    onExport: () -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }

    Card(modifier = Modifier.fillMaxWidth()) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).padding(16.dp)) {
                Text(layout.fancyName, style = MaterialTheme.typography.titleMedium)
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(orientationLabel(layout.orientation), style = MaterialTheme.typography.bodyMedium)
                    if (builtIn) {
                        Badge(
                            containerColor = MaterialTheme.colorScheme.secondary,
                            contentColor = MaterialTheme.colorScheme.onSecondary,
                        ) {
                            Text("Default")
                        }
                    }
                    Badge(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    ) {
                        Text(if (usedInGames == 0) "Unused" else "Used in $usedInGames game${if (usedInGames == 1) "" else "s"}")
                    }
                }
            }

            // The menu must live in the same Box as its anchor, otherwise the popup spawns at the row's origin
            Box {
                IconButton(onClick = { menuOpen = true }) {
                    Icon(Icons.Filled.MoreVert, contentDescription = "Manage layout")
                }
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    // Export works for every layout, including the built-in defaults (handy as a starting point)
                    DropdownMenuItem(
                        text = { Text("Export") },
                        leadingIcon = { Icon(Icons.Filled.Share, contentDescription = null) },
                        onClick = { menuOpen = false; onExport() },
                    )
                    // Built-in defaults are re-seeded every load, so they can't be renamed or deleted
                    if (!builtIn) {
                        DropdownMenuItem(
                            text = { Text("Rename") },
                            leadingIcon = { Icon(Icons.Filled.Edit, contentDescription = null) },
                            onClick = { menuOpen = false; onRename() },
                        )
                        DropdownMenuItem(
                            text = { Text("Delete", color = MaterialTheme.colorScheme.error) },
                            leadingIcon = { Icon(Icons.Filled.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error) },
                            onClick = { menuOpen = false; onDelete() },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun RenameLayoutDialog(
    currentName: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember { mutableStateOf(currentName) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Rename layout") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Name") },
                singleLine = true,
            )
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name.ifBlank { currentName }) }) { Text("Rename") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

private fun orientationLabel(orientation: GamepadLayout.GamepadTargetOrientation): String = when (orientation) {
    GamepadLayout.GamepadTargetOrientation.PORTRAIT -> "Portrait"
    GamepadLayout.GamepadTargetOrientation.LANDSCAPE -> "Landscape"
}
