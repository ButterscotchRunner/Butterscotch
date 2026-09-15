package net.perfectdreams.butterscotch.android.settings

import android.content.Context
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

class SettingsStore private constructor(
    private val indexFile: File,
    initial: ButterscotchSettings
) {
    companion object {
        private const val TAG = "SettingsStore"
        private const val ROOT_DIR_NAME = "butterscotch"
        private const val INDEX_FILE_NAME = "settings.json"

        // encodeDefaults = true so "version" is always written out, even while it equals its default
        private val json = Json {
            prettyPrint = true
            ignoreUnknownKeys = true
            encodeDefaults = true
        }

        fun load(context: Context): SettingsStore {
            val rootDir = File(context.filesDir, ROOT_DIR_NAME).apply { mkdirs() }
            val indexFile = File(rootDir, INDEX_FILE_NAME)

            val initial = if (indexFile.exists()) {
                try {
                    json.decodeFromString<ButterscotchSettings>(indexFile.readText(Charsets.UTF_8))
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to parse $indexFile; starting with defaults", e)
                    ButterscotchSettings()
                }
            } else {
                ButterscotchSettings()
            }

            return SettingsStore(indexFile, initial)
        }
    }

    // Compose-observable, reading this in a composable subscribes it to changes
    var settings: ButterscotchSettings by mutableStateOf(initial)
        private set

    // Mutate via copy(), then persist, e.g. store.update { copy(enableHapticFeedback = it) }
    fun update(transform: ButterscotchSettings.() -> ButterscotchSettings) {
        settings = settings.transform()
        save()
    }

    private fun save() {
        try {
            indexFile.writeText(json.encodeToString(settings), Charsets.UTF_8)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to write $indexFile", e)
        }
    }
}
