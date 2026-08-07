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
import androidx.compose.material.icons.automirrored.filled.AddToHomeScreen
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Save
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.ui.Alignment
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.navigation.NavHostController
import net.perfectdreams.butterscotch.android.Route
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.components.PlusBadge
import net.perfectdreams.butterscotch.android.shortcuts.requestPinGameShortcut
import java.util.UUID

/**
 * Per-game settings screen. Lives under [net.perfectdreams.butterscotch.android.Route.GameOverview] in the nav graph. Rows route to
 * sub-screens (metadata, save slots) or trigger destructive actions (delete).
 *
 * Pops itself via [onDone] when the user backs out OR after a successful delete. If the entry is
 * missing on entry (e.g. removed by another flow), we pop immediately rather than render an empty
 * shell.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    library: GameLibrary,
    gameId: UUID,
    nav: NavHostController
) {
    // Sadly we need to do this hacky hack because when deleting this gets recomposed and gets a null entry
    val entry = library.findById(gameId) ?: return

    val context = LocalContext.current
    val pinShortcutsSupported = remember { ShortcutManagerCompat.isRequestPinShortcutSupported(context) }
    var showDeleteDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            ButterscotchTopBar({ Text(entry.title) }, nav, navigationIcon = { ButterscotchBackButton(nav) })
        }
    ) { innerPadding ->
        LazyColumn(
            Modifier.fillMaxSize().padding(innerPadding),
        ) {
            item("settings") {
                OverviewRow(
                    icon = Icons.Filled.Edit,
                    title = "Game Settings",
                    subtitle = "Change game settings",
                    onClick = { nav.navigate(Route.GameSettings(gameId.toString())) },
                )
            }
            item("manage-slots") {
                OverviewRow(
                    icon = Icons.Filled.Save,
                    title = "Save Slots",
                    subtitle = "Manage this game save slots",
                    onClick = { nav.navigate(Route.SaveSlotList(gameId.toString())) },
                )
            }
            if (pinShortcutsSupported) {
                item("home-shortcut") {
                    OverviewRow(
                        icon = Icons.AutoMirrored.Default.AddToHomeScreen,
                        title = "Add Shortcut to Home Screen",
                        subtitle = "Pin a launcher icon for this game",
                        trailing = null,
                        onClick = {
                            requestPinGameShortcut(context, library, entry)
                        },
                    )
                }
            }
            item("game-logs") {
                OverviewRow(
                    icon = Icons.AutoMirrored.Filled.Article,
                    title = "Game Logs",
                    subtitle = "View the game logs",
                    onClick = { nav.navigate(Route.GameLogs(gameId.toString())) },
                )
            }
            item("delete") {
                OverviewRow(
                    icon = Icons.Filled.Delete,
                    title = "Delete",
                    subtitle = "Removes the game and all its saves",
                    titleColor = MaterialTheme.colorScheme.error,
                    onClick = { showDeleteDialog = true },
                )
            }
        }
    }

    if (showDeleteDialog) {
        AlertDialog(
            onDismissRequest = { showDeleteDialog = false },
            title = { Text("Delete ${entry.title}?") },
            text = { Text("This removes the game bundle and all of its saves. This can't be undone.") },
            confirmButton = {
                TextButton(onClick = {
                    showDeleteDialog = false
                    nav.popBackStack()
                    library.remove(entry.id)
                }) { Text("Delete", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteDialog = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun OverviewRow(
    icon: ImageVector,
    title: String,
    subtitle: String? = null,
    titleColor: Color = Color.Unspecified,
    trailing: @Composable (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(horizontal = 24.dp, vertical = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            icon,
            contentDescription = null,
            tint = if (titleColor == Color.Unspecified) MaterialTheme.colorScheme.primary else titleColor,
        )
        Spacer(Modifier.padding(end = 16.dp))
        Column(Modifier.padding(start = 16.dp).weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium, color = titleColor)
            if (subtitle != null) {
                Text(subtitle, style = MaterialTheme.typography.bodyMedium)
            }
        }
        if (trailing != null) {
            Spacer(Modifier.padding(end = 16.dp))
            trailing()
        }
    }
    HorizontalDivider()
}
