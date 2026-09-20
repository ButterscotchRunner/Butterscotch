package net.perfectdreams.butterscotch.android.library

import android.content.Context
import android.graphics.Bitmap
import android.util.Log
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import java.io.File
import java.util.UUID

/**
 * Persistent index of imported games. Backed by a single `library.json` under
 * `filesDir/butterscotch/`; per-game files live under `filesDir/butterscotch/games/<id>/`.
 *
 * On-disk layout:
 * ```
 * filesDir/butterscotch/
 *   library.json
 *   games/<id>/
 *     bundle/        <- contains the WAD file (data.win / game.unx / …) and any sibling assets
 *     saves/
 *       <slotId>/    <- one folder per save slot, UUID-named so renames/deletes don't
 *                       shuffle indices
 * ```
 *
 * Not thread-safe — call from the UI thread. The library is tiny (one row per game) so we just
 * rewrite the whole JSON file on each mutation; no DB, no diffing.
 *
 * [entries] is a [androidx.compose.runtime.snapshots.SnapshotStateList] — composables reading it recompose automatically when
 * [commit]/[remove] mutate the list, so screens that share a single [GameLibrary] instance stay in
 * sync without needing lifecycle-based refresh tricks.
 */
class GameLibrary private constructor(
    private val rootDir: File,
    private val indexFile: File,
    initial: List<GameEntry>
) {
    /** Live, Compose-observable list of entries in import order. */
    val entries: SnapshotStateList<GameEntry> = mutableStateListOf<GameEntry>().apply { addAll(initial) }

    fun bundleDir(entry: GameEntry): File = File(gameDir(entry.id), "bundle")

    /** Directory of the entry's currently active slot. Invariant: every entry has exactly one active slot. */
    fun savesDir(entry: GameEntry): File {
        val activeSlot = entry.saveSlots.first { it.active }
        return slotDir(entry, activeSlot.id)
    }

    fun slotDir(entry: GameEntry, slotId: UUID): File = File(gameDir(entry.id), "saves/$slotId")

    fun logsDir(entry: GameEntry): File = File(gameDir(entry.id), "logs")
    fun logsDir(entryId: UUID): File = File(gameDir(entryId), "logs")

    fun wadPath(entry: GameEntry): File = File(
        bundleDir(entry),
        when (entry.gameType) {
            is GameEntry.GameType.GameMakerStudio -> entry.gameType.filename
        }
    )
    fun gameDir(id: UUID): File = File(rootDir, "games/data/$id")

    /** Per-game derived assets (icon, future thumbnails). Separate from `data/` so we can rebuild them. */
    fun assetsDir(id: UUID): File = File(rootDir, "games/assets/$id")
    fun assetsDir(entry: GameEntry): File = assetsDir(entry.id)

    /** Cached PNG icon for [id], if extraction succeeded. The file may not exist. */
    fun iconFile(id: UUID): File = File(assetsDir(id), "icon.png")
    fun iconFile(entry: GameEntry): File = iconFile(entry.id)

    fun findById(id: UUID): GameEntry? = entries.firstOrNull { it.id == id }

    /**
     * Allocate a fresh per-game directory for the importer to stage files into. The directory is
     * created on disk but no library entry exists yet — call [commit] once staging succeeds, or
     * [discardStaging] to delete it if the user cancels.
     */
    fun beginStaging(): StagedGame {
        val id = UUID.randomUUID()
        val dir = gameDir(id).apply { mkdirs() }
        File(dir, "bundle").mkdirs()
        File(dir, "saves").mkdirs()
        File(dir, "logs").mkdirs()
        return StagedGame(id, File(dir, "bundle"), File(dir, "saves"))
    }

    /**
     * Add a staged import to the library and persist. If [icon] is non-null, it is encoded as PNG
     * into the per-game assets dir as `icon.png`; pass null to commit without an icon (the
     * library list will fall back to the app icon at display time).
     */
    fun commit(
        staged: StagedGame,
        title: String,
        gameType: GameEntry.GameType,
        icon: Bitmap? = null,
        portraitLayout: UUID = LayoutLibrary.DEFAULT_PORTRAIT_LAYOUT,
        landscapeLayout: UUID = LayoutLibrary.DEFAULT_LANDSCAPE_LAYOUT,
        runnerOs: GameEntry.RunnerOs = GameEntry.RunnerOs.WINDOWS,
        enablePhysicalControllers: Boolean = true,
        enablePhysicalKeyboard: Boolean = true,
        enableWidescreenHack: Boolean = false,
        postProcessing: GameEntry.PostProcessingSettings = GameEntry.PostProcessingSettings()
    ) {
        val initialSlotId = UUID.randomUUID()
        File(gameDir(staged.id), "saves/$initialSlotId").mkdirs()
        val entry = GameEntry(
            id = staged.id,
            title = title,
            gameType = gameType,
            importedAtMillis = System.currentTimeMillis(),
            favorited = false,
            saveSlots = listOf(
                GameEntry.SaveSlot(
                    id = initialSlotId,
                    active = true,
                    fancyName = "Default",
                )
            ),
            portraitLayout = portraitLayout,
            landscapeLayout = landscapeLayout,
            runnerOs = runnerOs,
            enablePhysicalControllers = enablePhysicalControllers,
            enablePhysicalKeyboard = enablePhysicalKeyboard,
            enableWidescreenHack = enableWidescreenHack,
            postProcessing = postProcessing,
        )
        entries.add(entry)
        // Sync order after adding a new entry
        syncOrder()

        if (icon != null) {
            runCatching {
                val out = iconFile(staged.id).apply { parentFile?.mkdirs() }
                out.outputStream().use { icon.compress(Bitmap.CompressFormat.PNG, 100, it) }
            }.onFailure { Log.w(TAG, "Failed to write icon for ${staged.id}", it) }
        }

        save()
    }

    /** Recursively delete a staged directory the user chose not to keep. */
    fun discardStaging(staged: StagedGame) {
        gameDir(staged.id).deleteRecursively()
    }

    fun update(id: UUID, block: (GameEntry) -> (GameEntry)) {
        val index = entries.indexOfFirst { it.id == id }
        if (index == -1)
            error("Trying to update a entry that doesn't exist! $id")
        entries[index] = block.invoke(entries[index])
        save()
    }

    fun remove(id: UUID) {
        if (entries.removeAll { it.id == id }) {
            gameDir(id).deleteRecursively()
            assetsDir(id).deleteRecursively()
            logsDir(id).deleteRecursively()
            save()
        }
    }

    fun setTitle(id: UUID, title: String) {
        require(title.isNotBlank()) { "Title cannot be blank" }
        update(id) { it.copy(title = title) }
        syncOrder()
        save()
    }

    /**
     * Replace (or clear) the per-game icon at runtime. Writes the bitmap as PNG to
     * `assetsDir/icon.png`, or deletes it when [bitmap] is null. Bumps the entry afterward so any
     * UI that reads the icon file recomposes — without that, the library list would happily keep
     * displaying the old decoded bitmap from its remember cache.
     */
    fun setIcon(id: UUID, bitmap: Bitmap?) {
        val out = iconFile(id)
        if (bitmap == null) {
            out.delete()
        } else {
            out.parentFile?.mkdirs()
            runCatching {
                out.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            }.onFailure {
                Log.w(TAG, "Failed to write icon for $id", it)
                return
            }
        }
        // Bump revision so observers (notably GameIcon's remember cache) recompose.
        update(id) { it.copy(iconRevision = it.iconRevision + 1) }
        save()
    }

    /**
     * Adds a new empty save slot to [gameId]. Does NOT make it active — call [setActiveSlot] if the
     * caller wants that. Returns the freshly created slot id.
     */
    fun addSlot(gameId: UUID, name: String): UUID {
        val slotId = UUID.randomUUID()
        update(gameId) { entry ->
            entry.copy(
                saveSlots = entry.saveSlots + GameEntry.SaveSlot(
                    id = slotId,
                    active = false,
                    fancyName = name,
                )
            )
        }
        val entry = findById(gameId) ?: error("Game vanished while adding slot: $gameId")
        slotDir(entry, slotId).mkdirs()
        save()
        return slotId
    }

    /**
     * Deletes [slotId] from [gameId], including its on-disk directory. Refuses to remove the last
     * slot (every game must have at least one). If the deleted slot was active, the first
     * remaining slot becomes active.
     */
    fun removeSlot(gameId: UUID, slotId: UUID) {
        val entry = findById(gameId) ?: error("No such game: $gameId")
        require(entry.saveSlots.size > 1) { "Cannot delete the last save slot" }
        val target = entry.saveSlots.first { it.id == slotId }

        val remaining = entry.saveSlots.filter { it.id != slotId }
        val finalSlots = if (target.active) {
            remaining.mapIndexed { i, slot -> slot.copy(active = i == 0) }
        } else {
            remaining
        }
        update(gameId) { it.copy(saveSlots = finalSlots) }
        slotDir(entry, slotId).deleteRecursively()
        save()
    }

    /**
     * Duplicates [sourceSlotId] in [gameId] into a new inactive slot named [name], copying the
     * source slot's directory contents verbatim. Returns the freshly created slot id.
     */
    fun copySlot(gameId: UUID, sourceSlotId: UUID, name: String): UUID {
        val entry = findById(gameId) ?: error("No such game: $gameId")
        require(entry.saveSlots.any { it.id == sourceSlotId }) { "Unknown slot $sourceSlotId for game $gameId" }
        val newId = UUID.randomUUID()
        val srcDir = slotDir(entry, sourceSlotId)
        val dstDir = slotDir(entry, newId).apply { mkdirs() }
        if (srcDir.exists()) srcDir.copyRecursively(dstDir, overwrite = true)
        update(gameId) { e ->
            e.copy(
                saveSlots = e.saveSlots + GameEntry.SaveSlot(
                    id = newId,
                    active = false,
                    fancyName = name,
                )
            )
        }
        save()
        return newId
    }

    fun renameSlot(gameId: UUID, slotId: UUID, name: String) {
        update(gameId) { entry ->
            entry.copy(
                saveSlots = entry.saveSlots.map { slot ->
                    if (slot.id == slotId) slot.copy(fancyName = name) else slot
                }
            )
        }
        save()
    }

    fun setActiveSlot(gameId: UUID, slotId: UUID) {
        update(gameId) { entry ->
            require(entry.saveSlots.any { it.id == slotId }) { "Unknown slot $slotId for game $gameId" }
            entry.copy(
                saveSlots = entry.saveSlots.map { slot -> slot.copy(active = slot.id == slotId) }
            )
        }
        save()
    }

    fun save() {
        val payload = json.encodeToString<List<GameEntry>>(entries.toList())
        val tmp = File(indexFile.parentFile, indexFile.name + ".tmp")
        tmp.writeText(payload)
        if (!tmp.renameTo(indexFile)) {
            // renameTo can fail across filesystems or if the target exists on some FUSE mounts; fall
            // back to overwrite + delete-tmp so we don't lose the write entirely.
            indexFile.writeText(payload)
            tmp.delete()
        }
    }

    /**
     * Sorts the entries, we don't automatically sort it because we want to keep the entries stable when favoriting/unfavoriting
     */
    fun syncOrder() {
        entries.sortWith(compareByDescending<GameEntry> { it.favorited }.thenBy { it.title })
    }

    data class StagedGame(val id: UUID, val bundleDir: File, val savesDir: File)

    companion object {
        private const val TAG = "GameLibrary"
        private const val ROOT_DIR_NAME = "butterscotch"
        private const val INDEX_FILE_NAME = "library.json"

        // prettyPrint: library.json is small and occasionally inspected by hand.
        // ignoreUnknownKeys: forward-compat — a newer file written by a future version that adds
        // fields can still be read by older code instead of crashing.
        // encodeDefaults = false: future fields with default values stay out of files that don't
        // need them, keeping the on-disk shape minimal.
        private val json = Json {
            prettyPrint = true
            ignoreUnknownKeys = true
            encodeDefaults = false
        }

        fun load(context: Context): GameLibrary {
            val rootDir = File(context.filesDir, ROOT_DIR_NAME).apply { mkdirs() }
            File(rootDir, "games").mkdirs()
            val indexFile = File(rootDir, INDEX_FILE_NAME)
            val initial = if (indexFile.exists()) parse(indexFile) else emptyList()
            return GameLibrary(rootDir, indexFile, initial).apply {
                this.syncOrder()
            }
        }

        private fun parse(file: File): List<GameEntry> = try {
            json.decodeFromString<List<GameEntry>>(file.readText())
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse $file; starting with empty library", e)
            emptyList()
        }
    }
}
