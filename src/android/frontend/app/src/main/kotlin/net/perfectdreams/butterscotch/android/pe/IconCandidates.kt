package net.perfectdreams.butterscotch.android.pe

import android.graphics.Bitmap
import java.io.File

/**
 * One pickable icon, decoded into memory. Lives for the lifetime of whatever UI is presenting the
 * picker; the chosen bitmap is what eventually gets persisted via [net.perfectdreams.butterscotch.android.library.GameLibrary.setIcon] /
 * [net.perfectdreams.butterscotch.android.library.GameLibrary.commit].
 */
data class IconCandidate(
    val bitmap: Bitmap,
    /** The source of the image (for display in the picker). */
    val source: String,
    /** Pixel size of the icon's longest edge — used to label and rank picks. */
    val size: Int,
)

/**
 * Walk every `.exe` in [bundleDir] and decode every icon group's largest frame into a Bitmap. One
 * frame per group (not every frame) since the user is choosing what to *display*, not editing a
 * multi-resolution .ico.
 *
 * Sorted largest-first so the default pick (first entry) is the most useful one.
 *
 * Per-file failures are swallowed: a single malformed PE shouldn't cost the user the icons in the
 * other .exes. A completely unreadable [bundleDir] still returns an empty list.
 */
fun scanIconCandidates(bundleDir: File): List<IconCandidate> {
    val exes = bundleDir.walkTopDown()
        .filter { it.isFile && it.extension.equals("exe", ignoreCase = true) }
        .sortedBy { it.name.lowercase() }
        .toList()
    if (exes.isEmpty()) return emptyList()

    val candidates = mutableListOf<IconCandidate>()
    for (exe in exes) {
        val groups = runCatching { PeIconExtractor.extractFromFile(exe) }.getOrNull().orEmpty()
        for (group in groups) {
            val bitmap = IcoDecoder.decodeLargest(group.toIcoBytes()) ?: continue
            candidates += IconCandidate(
                bitmap = bitmap,
                source = exe.name,
                size = maxOf(bitmap.width, bitmap.height),
            )
        }
    }
    return candidates.sortedByDescending { it.size }
}
