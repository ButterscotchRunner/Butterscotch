package net.perfectdreams.butterscotch.android.layouts

import android.content.Context
import android.content.res.AssetManager
import android.util.Log
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import io.ktor.http.DEFAULT_PORT
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File
import java.io.OutputStream
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

class LayoutLibrary private constructor(
    private val indexFile: File,
    private val spritesDir: File,
    private val initial: List<GamepadLayout>
) {
    companion object {
        private const val TAG = "LayoutLibrary"
        private const val ROOT_DIR_NAME = "butterscotch"
        private const val INDEX_FILE_NAME = "layouts.json"
        private const val SPRITES_DIR_NAME = "layout-sprites"
        val DEFAULT_PORTRAIT_LAYOUT = UUID.fromString("cb231fdd-df1d-44da-b850-ebb5a0a225f3")
        val DEFAULT_LANDSCAPE_LAYOUT = UUID.fromString("6fab9f1d-60c7-46ea-909b-9b5a92da0dd5")

        private val json = Json {
            prettyPrint = true
            ignoreUnknownKeys = true
            encodeDefaults = false
        }

        fun load(context: Context): LayoutLibrary {
            val rootDir = File(context.filesDir, ROOT_DIR_NAME).apply { mkdirs() }
            val indexFile = File(rootDir, INDEX_FILE_NAME)

            val initial = if (indexFile.exists()) {
                try {
                    json.decodeFromString<List<GamepadLayout>>(indexFile.readText(Charsets.UTF_8))
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to parse $indexFile; starting with empty library", e)
                    emptyList()
                }
            } else {
                emptyList()
            }

            // To avoid upgrading issues, we WON'T persist the default gamepads on the config
            val realInitial = mutableListOf(
                context.assets.open("layouts/default_portrait.json").readBytes().toString(Charsets.UTF_8)
                    .let {
                        Json.decodeFromString<GamepadLayout>(it)
                            .copy(
                                id = DEFAULT_PORTRAIT_LAYOUT,
                                fancyName = "Default Portrait",
                            )
                    },
                context.assets.open("layouts/default_landscape.json").readBytes().toString(Charsets.UTF_8)
                    .let {
                        Json.decodeFromString<GamepadLayout>(it)
                            .copy(
                                id = DEFAULT_LANDSCAPE_LAYOUT,
                                fancyName = "Default Landscape",
                            )
                    }
            )

            realInitial.addAll(initial)

            return LayoutLibrary(indexFile, File(rootDir, SPRITES_DIR_NAME), realInitial)
        }
    }

    val entries: SnapshotStateList<GamepadLayout> = mutableStateListOf<GamepadLayout>().apply { addAll(initial) }

    fun findById(id: UUID): GamepadLayout? = entries.firstOrNull { it.id == id }

    // Insert or replace a layout by id, then persist. Backs the in-game editor's Save / Save As.
    fun upsert(layout: GamepadLayout) {
        val index = entries.indexOfFirst { it.id == layout.id }
        if (index >= 0) entries[index] = layout else entries.add(layout)
        save()
    }

    // Writes a shareable .bslayout (a zip): layout.json at the root plus every referenced sprite under sprites/
    fun exportToZip(layout: GamepadLayout, output: OutputStream) {
        ZipOutputStream(output.buffered()).use { zip ->
            zip.putNextEntry(ZipEntry("layout.json"))
            zip.write(json.encodeToString(layout).toByteArray(Charsets.UTF_8))
            zip.closeEntry()
            for (name in layout.elements.flatMapTo(mutableSetOf()) { it.spriteReferences() }) {
                val file = spriteFile(name)
                if (!file.exists()) continue
                zip.putNextEntry(ZipEntry("sprites/$name"))
                zip.write(file.readBytes())
                zip.closeEntry()
            }
        }
    }

    // Imports a shared .bslayout and stores it as a brand-new copy. The id is always regenerated so an
    // import never overwrites an existing layout nor collides with a built-in default (which would be shadowed
    // on the next load). Throws if the bytes aren't a valid layout zip. Returns the stored copy.
    fun importFromZip(bytes: ByteArray): GamepadLayout {
        var layoutJson: String? = null
        val spriteBytes = mutableMapOf<String, ByteArray>()
        ZipInputStream(bytes.inputStream()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val name = entry.name.replace('\\', '/')
                when {
                    name == "layout.json" -> layoutJson = zip.readBytes().toString(Charsets.UTF_8)
                    name.startsWith("sprites/") && !entry.isDirectory -> spriteBytes[name.removePrefix("sprites/")] = zip.readBytes()
                }
                zip.closeEntry()
            }
        }
        val parsed = json.decodeFromString<GamepadLayout>(layoutJson ?: error("Not a valid layout file"))
        // Sprites are re-stored under the hash of their actual bytes, so a crafted zip can never plant wrong content under a hash name it doesn't match. References to files missing from the zip are dropped
        val storedNames = spriteBytes.mapValues { storeSprite(it.value) }
        val remappedElements = parsed.elements.map { element ->
            when (element) {
                is GamepadElement.Key -> {
                    val sprite = element.sprite?.let { storedNames[it] }
                    element.copy(sprite = sprite, spritePressed = if (sprite == null) null else element.spritePressed?.let { storedNames[it] })
                }
                is GamepadElement.Joystick -> element.copy(sprite = element.sprite?.let { storedNames[it] }, spriteThumb = element.spriteThumb?.let { storedNames[it] })
                is GamepadElement.AnalogJoystick -> element.copy(sprite = element.sprite?.let { storedNames[it] }, spriteThumb = element.spriteThumb?.let { storedNames[it] })
                is GamepadElement.Menu -> element.copy(sprite = element.sprite?.let { storedNames[it] })
                is GamepadElement.FastForward -> {
                    val sprite = element.sprite?.let { storedNames[it] }
                    element.copy(sprite = sprite, spritePressed = if (sprite == null) null else element.spritePressed?.let { storedNames[it] })
                }
                is GamepadElement.MouseButton -> {
                    val sprite = element.sprite?.let { storedNames[it] }
                    element.copy(sprite = sprite, spritePressed = if (sprite == null) null else element.spritePressed?.let { storedNames[it] })
                }
            }
        }
        val copy = parsed.copy(id = UUID.randomUUID(), elements = remappedElements)
        upsert(copy)
        return copy
    }

    fun spriteFile(name: String): File = File(spritesDir, name)

    // Stores PNG bytes in the sprite pool under their SHA-256 hash and returns the file name. Stored via a temp file + rename so a partially written file can never sit under a valid hash name
    fun storeSprite(bytes: ByteArray): String {
        spritesDir.mkdirs()
        val hash = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
        val name = "$hash.png"
        val target = File(spritesDir, name)
        if (!target.exists()) {
            val temp = File(spritesDir, "$name.tmp")
            temp.writeBytes(bytes)
            if (!temp.renameTo(target)) temp.delete()
        }
        return name
    }

    // True for the two built-in layouts, which are re-seeded on every load and can't be edited or deleted
    fun isBuiltIn(id: UUID): Boolean = id == DEFAULT_PORTRAIT_LAYOUT || id == DEFAULT_LANDSCAPE_LAYOUT

    // Deletes a user layout. Built-in defaults are re-seeded on every load, so they can't be removed.
    // Games still pointing at the deleted id fall back to the matching default at launch (see GameActivity)
    fun remove(id: UUID) {
        require(!isBuiltIn(id)) { "Cannot delete a built-in layout" }
        entries.removeAll { it.id == id }
        save()
    }

    // Built-in defaults are re-seeded on every load, so we never write them out - that keeps them
    // upgradeable and out of layouts.json. Only user layouts are persisted.
    private fun save() {
        val persistable = entries.filter { !isBuiltIn(it.id) }
        try {
            indexFile.writeText(json.encodeToString(persistable), Charsets.UTF_8)
            sweepSprites()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to write $indexFile", e)
        }
    }

    // Deletes pool files (including stale temps) not referenced by any layout. Only runs right after a successful index write, never at load, so a corrupt layouts.json alone can never cause sprite loss
    private fun sweepSprites() {
        val referenced = entries.flatMapTo(mutableSetOf()) { layout -> layout.elements.flatMap { it.spriteReferences() } }
        spritesDir.listFiles()?.forEach { if (it.name !in referenced) it.delete() }
    }
}
