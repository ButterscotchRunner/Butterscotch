package net.perfectdreams.butterscotch.android.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Badge
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.FloatingActionButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import net.perfectdreams.butterscotch.android.Route
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.library.GameEntry
import net.perfectdreams.butterscotch.android.library.GameLibrary
import java.util.UUID

/**
 * The list of save slots for one game — mirrors the launcher's game list but at the slot level.
 * Tap a row's cog to manage that slot ([SaveSlotDetailScreen]); the FAB creates a new empty slot.
 *
 * Pops itself if the game vanishes (e.g. user deleted it from another flow).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SaveSlotListScreen(
    library: GameLibrary,
    gameId: UUID,
    nav: NavHostController,
) {
    val entry = library.findById(gameId) ?: return
    val gameUuid = entry.id

    var showCreateDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            ButterscotchTopBar(
                title = { Text("Save Slots") },
                nav = nav,
                navigationIcon = { ButterscotchBackButton(nav) },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showCreateDialog = true }) {
                Icon(Icons.Filled.Add, contentDescription = "New save slot")
            }
        },
    ) { innerPadding ->
        if (entry.saveSlots.isEmpty()) {
            // Should be unreachable (commit always creates one) — keep a placeholder anyway.
            Box(
                Modifier.fillMaxSize().padding(innerPadding).padding(24.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text("No save slots yet.")
            }
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize().padding(innerPadding),
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(entry.saveSlots, key = { it.id.toString() }) { slot ->
                    SaveSlotTile(
                        slot = slot,
                        onOpen = {
                            nav.navigate(Route.SaveSlotDetail(gameId.toString(), slot.id.toString()))
                        },
                    )
                }
            }
        }
    }

    if (showCreateDialog) {
        NewSlotDialog(
            onConfirm = { name ->
                library.addSlot(gameUuid, name.ifBlank { "Save Slot" })
                showCreateDialog = false
            },
            onDismiss = { showCreateDialog = false },
        )
    }
}

@Composable
private fun SaveSlotTile(
    slot: GameEntry.SaveSlot,
    onOpen: () -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(
                modifier = Modifier
                    .weight(1f)
                    .clickable(onClick = onOpen)
                    .padding(16.dp),
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(slot.fancyName, style = MaterialTheme.typography.titleMedium)
                    if (slot.active) {
                        Badge(
                            containerColor = MaterialTheme.colorScheme.primary,
                            contentColor = MaterialTheme.colorScheme.onPrimary,
                        ) {
                            Text("Active")
                        }
                    }
                }
            }

            IconButton(onClick = onOpen) {
                Icon(Icons.Filled.Settings, contentDescription = "Manage slot")
            }
        }
    }
}

@Composable
private fun NewSlotDialog(
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("New save slot") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Name") },
                singleLine = true,
            )
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name) }) { Text("Create") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}
