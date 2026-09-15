package net.perfectdreams.butterscotch.android

import android.content.Context
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * Save-slot import/export over Android's Storage Access Framework.
 *
 * Export: walks every regular file under [slotDir] and writes a flat zip to the user-picked Uri,
 * mirroring the layout the web build uses (see ButterscotchWebKT's `exportSavesAsZip`).
 *
 * Import: extracts a user-picked zip into a fresh temp directory next to the slot, then swaps the
 * temp dir into place atomically. A half-failed extraction never leaves the live slot half-empty.
 */
object SaveSlotZip {
    suspend fun exportSlotToZip(
        context: Context,
        slotDir: File,
        destination: Uri,
    ): Int = withContext(Dispatchers.IO) {
        val files = slotDir.walkTopDown().filter { it.isFile }.toList()
        val output = context.contentResolver.openOutputStream(destination, "w")
            ?: error("Could not open output stream for $destination")
        output.use { os ->
            ZipOutputStream(os.buffered()).use { zip ->
                for (file in files) {
                    val relative = file.relativeTo(slotDir).invariantSeparatorsPath
                    zip.putNextEntry(ZipEntry(relative))
                    file.inputStream().use { it.copyTo(zip) }
                    zip.closeEntry()
                }
            }
        }
        files.size
    }

    /**
     * Replace the contents of [slotDir] with the entries in the zip at [source]. Extraction goes
     * into a sibling temp directory first; on success we delete the live slot and rename temp into
     * its place. On failure the live slot is left untouched and the temp dir is deleted.
     */
    suspend fun importZipIntoSlot(
        context: Context,
        source: Uri,
        slotDir: File,
    ): Int = withContext(Dispatchers.IO) {
        val parent = slotDir.parentFile ?: error("Slot dir has no parent: $slotDir")
        val temp = File(parent, slotDir.name + ".import-tmp")
        if (temp.exists()) temp.deleteRecursively()
        temp.mkdirs()

        var count = 0
        try {
            val input = context.contentResolver.openInputStream(source)
                ?: error("Could not open input stream for $source")
            input.use { ins ->
                ZipInputStream(ins.buffered()).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        val safeName = entry.name.replace('\\', '/')
                        if (safeName.contains("..")) {
                            // Defend against zip slip — refuse entries that would escape the slot dir.
                            zip.closeEntry()
                            continue
                        }
                        val target = File(temp, safeName)
                        if (entry.isDirectory) {
                            target.mkdirs()
                        } else {
                            target.parentFile?.mkdirs()
                            target.outputStream().use { out -> zip.copyTo(out) }
                            count++
                        }
                        zip.closeEntry()
                    }
                }
            }

            if (slotDir.exists()) slotDir.deleteRecursively()
            if (!temp.renameTo(slotDir)) {
                // Rename can fail on some FUSE mounts; fall back to a recursive copy + delete.
                temp.copyRecursively(slotDir, overwrite = true)
                temp.deleteRecursively()
            }
        } catch (e: Throwable) {
            temp.deleteRecursively()
            throw e
        }
        count
    }
}
