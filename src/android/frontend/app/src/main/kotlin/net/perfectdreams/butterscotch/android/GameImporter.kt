package net.perfectdreams.butterscotch.android

import android.content.Context
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.pe.IconCandidate
import net.perfectdreams.butterscotch.android.pe.scanIconCandidates
import java.io.File
import java.util.zip.ZipInputStream
import kotlin.math.max

/**
 * Copies a user-picked folder (via Storage Access Framework tree Uri) into the app's per-game
 * bundle directory and detects which GameMaker WAD filename it contains.
 *
 * Why copy and not just hold the tree Uri? `takePersistableUriPermission` is fragile (cleared on
 * reboot in some OEM ROMs, lost if the source folder moves) and `DocumentFile` access is slow.
 * Copying once at import time lets the runner read files via plain POSIX paths forever after.
 *
 * Validation policy: the only header check is "does a known WAD filename exist in the picked
 * folder?". The actual parse via [ParsedDataWin.parseLight] runs on the copied file *after*
 * staging — and may crash the process if the file isn't a real WAD (see
 * `datawin-parse-fatal-on-error` project memory). We accept that for now.
 */
object GameImporter {
    private const val TAG = "GameImporter"

    /** Filenames the runner recognizes as the GameMaker WAD across export targets. */
    val WAD_FILENAMES = listOf(
        "data.win",   // Windows
        "game.unx",   // Linux
        "game.ios",   // iOS
        "game.droid", // Android
        "game.psp",   // PSP
        "game.win",   // PSVita
        "game.osx",   // macOS
    )

    sealed interface Result {
        /**
         * Folder was successfully copied into [stagedDir]. The detected WAD filename is
         * [wadFilename]. [suggestedTitle] is the GEN8 displayName/name if non-empty, else null
         * (caller falls back to folder name). [wadVersion] is the raw GEN8 wadVersion byte; the
         * configure screen displays it.
         *
         * IMPORTANT: the caller must either call [net.perfectdreams.butterscotch.android.library.GameLibrary.commit] with the staged game (passed
         * back via [staged]) or [net.perfectdreams.butterscotch.android.library.GameLibrary.discardStaging] — leaving it unhandled leaks the
         * copied bundle on disk.
         */
        data class Success(
            val staged: GameLibrary.StagedGame,
            val wadFilename: String,
            val suggestedTitle: String,
            val wadVersion: Int,
            val folderName: String,
            val iconCandidates: List<IconCandidate>,
        ) : Result

        /** The picked folder had none of [WAD_FILENAMES] at its top level. */
        data class MissingWad(val folderName: String) : Result

        /** Tree Uri was unreadable, or copy failed midway. Staged dir (if any) is deleted. */
        data class Failure(val message: String) : Result
    }

    /**
     * Pick a folder → copy its contents into a fresh staging dir → peek the WAD metadata. Runs on
     * IO dispatcher.
     */
    suspend fun import(
        context: Context,
        treeUri: Uri,
        library: GameLibrary,
        writeFileCallback: (String) -> (Unit)
    ): Result = withContext(Dispatchers.IO) {
        val root = DocumentFile.fromTreeUri(context, treeUri)
        if (root == null || !root.isDirectory) {
            return@withContext Result.Failure("Selected location is not a readable folder.")
        }

        val folderName = root.name ?: "Imported Game"

        val wadDoc = root.listFiles().firstOrNull { doc ->
            doc.isFile && doc.name in WAD_FILENAMES
        } ?: return@withContext Result.MissingWad(folderName)

        val wadFilename = wadDoc.name!!
        val staged = library.beginStaging()

        try {
            copyTree(context, root, staged.bundleDir, writeFileCallback)
        } catch (e: Exception) {
            Log.e(TAG, "Copy failed for $treeUri", e)
            library.discardStaging(staged)
            return@withContext Result.Failure("Couldn't copy folder: ${e.message}")
        }

        finalize(library, staged, wadFilename, folderName, null, emptyList())
    }

    /**
     * Pick a ZIP → extract it into a fresh staging dir → peek the WAD metadata. Runs on IO
     * dispatcher.
     *
     * Unlike folder import, the WAD may live anywhere inside the archive (e.g. the user zipped the
     * whole game folder, producing `MyGame/data.win`). We extract everything to a temp dir, find the
     * first known WAD filename recursively, and promote the directory that holds it to be the bundle
     * root — so the WAD's sibling assets (icons, audio, the original .exe) come along.
     */
    suspend fun importZip(
        context: Context,
        zipUri: Uri,
        library: GameLibrary,
        writeFileCallback: (String) -> (Unit)
    ): Result = withContext(Dispatchers.IO) {
        val displayName = queryDisplayName(context, zipUri)
        val fallbackName = (displayName?.removeSuffix(".zip") ?: "").ifBlank { "Imported Game" }
        val staged = library.beginStaging()

        // Extract into a sibling temp dir first so we can locate the WAD wherever it is, then move
        // its containing directory into the bundle.
        val temp = File(staged.bundleDir.parentFile, "extract-tmp")
        if (temp.exists()) temp.deleteRecursively()
        temp.mkdirs()

        try {
            extractZip(context, zipUri, temp, writeFileCallback)
        } catch (e: Exception) {
            Log.e(TAG, "Zip extraction failed for $zipUri", e)
            temp.deleteRecursively()
            library.discardStaging(staged)
            return@withContext Result.Failure("Couldn't extract ZIP: ${e.message}")
        }

        // Prefer the shallowest WAD, so a root data.win beats one nested inside a subfolder
        val wadFile = temp.walkTopDown().filter { it.isFile && it.name in WAD_FILENAMES }.minByOrNull { it.relativeTo(temp).invariantSeparatorsPath.count { c -> c == '/' } }
        if (wadFile == null) {
            temp.deleteRecursively()
            library.discardStaging(staged)
            return@withContext Result.MissingWad(fallbackName)
        }

        // The directory holding the WAD becomes the bundle root; copy its contents into the bundle.
        val wadRoot = wadFile.parentFile ?: temp
        for (child in wadRoot.listFiles() ?: emptyArray()) {
            child.copyRecursively(File(staged.bundleDir, child.name), overwrite = true)
        }
        temp.deleteRecursively()

        finalize(library, staged, wadFile.name, fallbackName, null, emptyList())
    }

    /**
     * Import a ZIP already held in memory as a [ByteArray] (e.g. a sample game downloaded over HTTP).
     * Same locate-the-WAD-anywhere behavior as the Uri overload. [fallbackName] is used as the
     * suggested title when the WAD has no GEN8 name.
     */
    suspend fun importZip(
        library: GameLibrary,
        zipBytes: ByteArray,
        name: String,
        iconAsBytes: ByteArray,
        writeFileCallback: (String) -> (Unit)
    ): Result = withContext(Dispatchers.IO) {
        val staged = library.beginStaging()

        // Extract into a sibling temp dir first so we can locate the WAD wherever it is, then move
        // its containing directory into the bundle.
        val temp = File(staged.bundleDir.parentFile, "extract-tmp")
        if (temp.exists()) temp.deleteRecursively()
        temp.mkdirs()

        try {
            extractZip(zipBytes, temp, writeFileCallback)
        } catch (e: Exception) {
            Log.e(TAG, "Zip extraction failed for in-memory ZIP", e)
            temp.deleteRecursively()
            library.discardStaging(staged)
            return@withContext Result.Failure("Couldn't extract ZIP: ${e.message}")
        }

        // Prefer the shallowest WAD, so a root data.win beats one nested inside a subfolder
        val wadFile = temp.walkTopDown().filter { it.isFile && it.name in WAD_FILENAMES }.minByOrNull { it.relativeTo(temp).invariantSeparatorsPath.count { c -> c == '/' } }
        if (wadFile == null) {
            temp.deleteRecursively()
            library.discardStaging(staged)
            return@withContext Result.MissingWad(name)
        }

        // The directory holding the WAD becomes the bundle root; copy its contents into the bundle.
        val wadRoot = wadFile.parentFile ?: temp
        for (child in wadRoot.listFiles() ?: emptyArray()) {
            child.copyRecursively(File(staged.bundleDir, child.name), overwrite = true)
        }
        temp.deleteRecursively()

        val decodedIcon = BitmapFactory.decodeByteArray(iconAsBytes, 0, iconAsBytes.size)

        finalize(library, staged, wadFile.name, name, name, listOf(IconCandidate(decodedIcon, "Sample", max(decodedIcon.width, decodedIcon.height))))
    }

    /**
     * Shared tail for both import paths: verify the WAD landed in the bundle, peek its GEN8
     * metadata, scan for icon candidates, and build the [Result.Success]. On the (bug) case where
     * the WAD is missing after copy, discards the staging dir and returns [Result.Failure].
     */
    private fun finalize(
        library: GameLibrary,
        staged: GameLibrary.StagedGame,
        wadFilename: String,
        fallbackName: String,
        overrideName: String? = null,
        additionalIconCandidates: List<IconCandidate>
    ): Result {
        val wadFile = File(staged.bundleDir, wadFilename)
        if (!wadFile.exists()) {
            // Shouldn't happen — we just confirmed the WAD existed and the copy didn't throw.
            // But if it does, fail loud rather than letting the native parser explode.
            library.discardStaging(staged)
            return Result.Failure("WAD vanished after copy (this is a bug).")
        }

        // Peek metadata. parseLight may exit() on a corrupt WAD — accepted (see memory).
        val (suggestedTitle, wadVersion) = ParsedDataWin.parseLight(wadFile.absolutePath)?.use { dw ->
            val name = (dw.displayName ?: dw.name)?.takeIf { it.isNotBlank() }
            name to dw.wadVersion
        } ?: (null to -1)

        val iconCandidates = runCatching { scanIconCandidates(staged.bundleDir) }
            .onFailure { Log.w(TAG, "Icon extraction failed for ${staged.id}", it) }
            .getOrDefault(emptyList()) + additionalIconCandidates

        return Result.Success(
            staged = staged,
            wadFilename = wadFilename,
            suggestedTitle = overrideName ?: suggestedTitle ?: fallbackName,
            wadVersion = wadVersion,
            folderName = fallbackName,
            iconCandidates = iconCandidates,
        )
    }


    /**
     * Recursive DocumentFile → File copy. Mirrors `GameActivity.extractAssetTree`'s shape but
     * sources from the SAF tree instead of assets/.
     */
    private fun copyTree(context: Context, src: DocumentFile, dest: File, writeFileCallback: (String) -> (Unit)) {
        if (!dest.exists()) dest.mkdirs()
        for (child in src.listFiles()) {
            val name = child.name ?: continue
            val target = File(dest, name)
            if (child.isDirectory) {
                copyTree(context, child, target, writeFileCallback)
            } else if (child.isFile) {
                val fileName = child.name
                if (fileName != null) {
                    writeFileCallback.invoke(fileName)
                }
                context.contentResolver.openInputStream(child.uri)?.use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                } ?: error("Could not open child $name for reading")
            }
        }
    }

    /**
     * Extract a user-picked zip into [dest]. Mirrors the zip-slip guard and separator normalization
     * from [SaveSlotZip.importZipIntoSlot] — entries with `..` in their path are refused so a
     * malicious archive can't escape [dest].
     */
    private fun extractZip(context: Context, source: Uri, dest: File, writeFileCallback: (String) -> (Unit)) {
        val input = context.contentResolver.openInputStream(source)
            ?: error("Could not open input stream for $source")
        input.use { extractZipStream(it, dest, writeFileCallback) }
    }

    /** [extractZip] for a ZIP already held in memory. */
    private fun extractZip(zipBytes: ByteArray, dest: File, writeFileCallback: (String) -> (Unit)) {
        zipBytes.inputStream().use { extractZipStream(it, dest, writeFileCallback) }
    }

    /** Core ZIP extraction shared by both [extractZip] overloads. Refuses zip-slip entries. */
    private fun extractZipStream(input: java.io.InputStream, dest: File, writeFileCallback: (String) -> (Unit)) {
        ZipInputStream(input.buffered()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val safeName = entry.name.replace('\\', '/')
                if (safeName.contains("..")) {
                    // Defend against zip slip — refuse entries that would escape the dest dir.
                    zip.closeEntry()
                    continue
                }
                val target = File(dest, safeName)
                if (entry.isDirectory) {
                    target.mkdirs()
                } else {
                    target.parentFile?.mkdirs()
                    writeFileCallback.invoke(target.name)
                    target.outputStream().use { out -> zip.copyTo(out) }
                }
                zip.closeEntry()
            }
        }
    }

    /** Resolve a content Uri's display name (the file name) for use as a fallback title. */
    private fun queryDisplayName(context: Context, uri: Uri): String? {
        return context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
    }
}
