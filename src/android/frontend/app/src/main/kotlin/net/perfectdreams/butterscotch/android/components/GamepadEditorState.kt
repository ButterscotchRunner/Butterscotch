package net.perfectdreams.butterscotch.android.components

import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import net.perfectdreams.butterscotch.android.VirtualKeyState
import net.perfectdreams.butterscotch.android.layouts.GamepadElement
import net.perfectdreams.butterscotch.android.layouts.GamepadLayout
import java.util.UUID

class GamepadEditorState(val gameName: String, val keys: VirtualKeyState, var initialLayout: GamepadLayout) {
    var layout by mutableStateOf(initialLayout)
    var snapToGrid by mutableStateOf(false)
    var editingId by mutableStateOf<UUID?>(null)

    // Replace the element sharing [element]'s id with the new value, leaving the list order intact.
    fun update(element: GamepadElement) {
        layout = layout.copy(elements = layout.elements.map { if (it.id == element.id) element else it })
    }

    // Deletes a element
    fun delete(id: UUID) {
        layout = layout.copy(elements = layout.elements.filter { it.id != id })
        editingId = null
    }

    // Append a freshly-built element at the overlay center
    fun add(element: GamepadElement) {
        layout = layout.copy(elements = layout.elements + element)
    }

    fun reset() {
        layout = initialLayout
    }
}