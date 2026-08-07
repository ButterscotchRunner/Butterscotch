package net.perfectdreams.butterscotch.android

import android.content.Context
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.settings.SettingsStore

object Libraries {
    // We NEED to cache this because we WANT a singleton instance
    // If not, it WILL cause issues (example: ANYTHING that edits the data during the GameActivity and then we come back to the MainActivity)
    // All functions MUST be called on the main thread
    var gameLibrary: GameLibrary? = null
    var layoutLibrary: LayoutLibrary? = null
    var settingsStore: SettingsStore? = null

    fun loadGameLibrary(context: Context): GameLibrary {
        return this.gameLibrary ?: GameLibrary.load(context).also { this@Libraries.gameLibrary = it }
    }

    fun loadLayoutLibrary(context: Context): LayoutLibrary {
        return this.layoutLibrary ?: LayoutLibrary.load(context).also { this@Libraries.layoutLibrary = it }
    }

    fun loadSettingsStore(context: Context): SettingsStore {
        return this.settingsStore ?: SettingsStore.load(context).also { this@Libraries.settingsStore = it }
    }
}