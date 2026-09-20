package net.perfectdreams.butterscotch.android.components

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toBitmap
import net.perfectdreams.butterscotch.android.pe.IconCandidate

/**
 * Loading-state for the icon picker's candidates list. Explicit because the picker has three
 * visually distinct outcomes the caller can't otherwise express with a `List<IconCandidate>?`:
 * still scanning, scan-succeeded-but-empty, and scan-threw.
 */
sealed interface CandidatesState {
    data object Loading : CandidatesState
    data class Loaded(val list: List<IconCandidate>) : CandidatesState
    data class Failed(val message: String) : CandidatesState
}

/**
 * Tappable row that previews the currently-selected icon (or the app icon as a fallback) and a
 * short title/subtitle. Used in both the import flow and the per-game settings screen.
 */
@Composable
fun IconSection(
    selected: Bitmap?,
    onClick: () -> Unit,
    title: String = "Icon",
    subtitle: String = if (selected == null) "Tap to choose" else "Tap to change",
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(8.dp),
    ) {
        IconThumb(bitmap = selected, fallbackContext = context, modifier = Modifier.size(64.dp))
        Spacer(Modifier.size(16.dp))
        Column {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(subtitle, style = MaterialTheme.typography.bodySmall)
        }
    }
}

/**
 * Dialog that lets the user pick from a list of pre-scanned [candidates] or upload a custom image
 * via the system picker.
 *
 * [candidates] is null while still loading (shows a spinner inside the dialog) — useful for the
 * settings screen, which scans on demand. Pass an empty list to render the "no icons found"
 * message immediately.
 */
@Composable
fun IconPickerDialog(
    candidates: CandidatesState,
    selected: Bitmap?,
    onPickCandidate: (Bitmap) -> Unit,
    onPickCustom: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Choose icon") },
        text = {
            Column {
                when (val s = candidates) {
                    is CandidatesState.Loading -> {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.padding(vertical = 8.dp),
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(20.dp))
                            Spacer(Modifier.size(12.dp))
                            Text("Scanning .exe icons...", style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                    is CandidatesState.Failed -> {
                        Text(
                            "Couldn't scan icons: ${s.message}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.error,
                            modifier = Modifier.padding(vertical = 8.dp),
                        )
                    }
                    is CandidatesState.Loaded -> {
                        if (s.list.isEmpty()) {
                            Text(
                                "No icons found in any .exe in the bundle...",
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        } else {
                            LazyVerticalGrid(
                                columns = GridCells.Adaptive(minSize = 80.dp),
                                modifier = Modifier.heightIn(max = 320.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp),
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                items(s.list, key = { System.identityHashCode(it.bitmap) }) { candidate ->
                                    CandidateTile(
                                        candidate = candidate,
                                        isSelected = candidate.bitmap === selected,
                                        onClick = { onPickCandidate(candidate.bitmap) },
                                    )
                                }
                            }
                        }
                    }
                }
                Spacer(Modifier.height(16.dp))
                TextButton(onClick = onPickCustom) {
                    Icon(Icons.Filled.Add, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text("Upload custom image")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("Done") }
        },
    )
}

@Composable
private fun CandidateTile(
    candidate: IconCandidate,
    isSelected: Boolean,
    onClick: () -> Unit,
) {
    val borderColor =
        if (isSelected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .border(if (isSelected) 2.dp else 1.dp, borderColor, RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(6.dp),
    ) {
        IconThumb(bitmap = candidate.bitmap, fallbackContext = null, modifier = Modifier.size(64.dp))
        Spacer(Modifier.height(4.dp))
        Text(candidate.source, style = MaterialTheme.typography.labelSmall, maxLines = 1)
        Text("${candidate.size}px", style = MaterialTheme.typography.labelSmall)
    }
}

/**
 * Render a Bitmap, or — if [bitmap] is null and [fallbackContext] is provided — the app icon
 * (matching what the library list shows for entries with no icon).
 */
@Composable
fun IconThumb(bitmap: Bitmap?, fallbackContext: Context?, modifier: Modifier = Modifier) {
    val effective = bitmap ?: remember(fallbackContext) {
        fallbackContext?.let { ctx ->
            ctx.packageManager.getApplicationIcon(ctx.packageName).toBitmap()
        }
    }
    if (effective != null) {
        Image(
            painter = BitmapPainter(effective.asImageBitmap(), filterQuality = FilterQuality.None),
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = modifier,
        )
    } else {
        Box(modifier) // empty placeholder when no bitmap and no fallback
    }
}

/** Decode a user-picked content URI to a Bitmap. Returns null if the read or decode fails. */
fun decodeUriToBitmap(context: Context, uri: Uri): Bitmap? = try {
    context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it) }
} catch (e: Exception) {
    android.util.Log.w("IconPicker", "Failed to decode picked icon from $uri", e)
    null
}
