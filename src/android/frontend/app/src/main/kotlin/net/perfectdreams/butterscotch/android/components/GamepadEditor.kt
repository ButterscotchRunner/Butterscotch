package net.perfectdreams.butterscotch.android.components

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import net.perfectdreams.butterscotch.android.Libraries
import net.perfectdreams.butterscotch.android.layouts.Gamepad
import net.perfectdreams.butterscotch.android.layouts.GamepadElement
import net.perfectdreams.butterscotch.android.layouts.GamepadStick
import net.perfectdreams.butterscotch.android.layouts.GmlKey
import net.perfectdreams.butterscotch.android.layouts.GmlMouseButton
import net.perfectdreams.butterscotch.android.layouts.InputBinding
import net.perfectdreams.butterscotch.android.layouts.KeyTrigger
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import java.io.ByteArrayOutputStream
import java.util.UUID
import kotlin.math.absoluteValue
import kotlin.math.roundToInt

@Composable
fun BoxWithConstraintsScope.GamepadEditorToolbar(
    state: GamepadEditorState,
    onSave: () -> (Unit),
    onSaveAs: (String) -> (Unit),
    onExitEditMode: () -> (Unit),
    canSave: Boolean
) {
    var showSaveAsDialog by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier.align(Alignment.TopCenter),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Edit mode - drag to move, long-press to edit",
            color = Color.White,
            style = TextStyle(fontSize = 13.sp)
        )
        Spacer(Modifier.height(8.dp))
        // We use FlowRow instead of a normal row so that it behaves more like flex-wrap
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            modifier = Modifier
                .align(Alignment.CenterHorizontally)
                .padding(top = 16.dp)
        ) {
            Box {
                var addMenuExpanded by remember { mutableStateOf(false) }
                Button(onClick = { addMenuExpanded = true }) { Text("Add") }
                DropdownMenu(
                    expanded = addMenuExpanded,
                    onDismissRequest = { addMenuExpanded = false }) {
                    DropdownMenuItem(text = { Text("Button") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.Key(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.22,
                            opacity = 1.0,
                            label = null,
                            trigger = KeyTrigger.Press,
                            binding = InputBinding.Keyboard(GmlKey.Z.code),
                            id = UUID.randomUUID()
                        ))
                    })
                    DropdownMenuItem(text = { Text("Joystick") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.Joystick(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.42,
                            opacity = 1.0,
                            up = InputBinding.Keyboard(GmlKey.UP.code),
                            down = InputBinding.Keyboard(GmlKey.DOWN.code),
                            left = InputBinding.Keyboard(GmlKey.LEFT.code),
                            right = InputBinding.Keyboard(GmlKey.RIGHT.code),
                            id = UUID.randomUUID()
                        ))
                    })
                    DropdownMenuItem(text = { Text("Gamepad Button") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.Key(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.22,
                            opacity = 1.0,
                            label = null,
                            trigger = KeyTrigger.Press,
                            binding = InputBinding.GamepadButton(device = 0, button = Gamepad.Button.FACE1.index),
                            id = UUID.randomUUID()
                        ))
                    })
                    DropdownMenuItem(text = { Text("Gamepad Joystick") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.AnalogJoystick(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.42,
                            opacity = 1.0,
                            stick = GamepadStick.LEFT,
                            device = 0,
                            id = UUID.randomUUID()
                        ))
                    })
                    DropdownMenuItem(text = { Text("Menu") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.Menu(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.22,
                            opacity = 1.0,
                            id = UUID.randomUUID()
                        ))
                    })
                    DropdownMenuItem(text = { Text("Fast Forward") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.FastForward(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.22,
                            opacity = 1.0,
                            id = UUID.randomUUID(),
                            speed = 2.0f,
                            toggle = true
                        ))
                    })
                    DropdownMenuItem(text = { Text("Mouse Button") }, onClick = {
                        addMenuExpanded = false
                        state.add(GamepadElement.MouseButton(
                            positionX = 0.5,
                            positionY = 0.5,
                            scale = 0.22,
                            opacity = 1.0,
                            id = UUID.randomUUID(),
                            button = GmlMouseButton.RIGHT_BUTTON,
                            toggle = true
                        ))
                    })
                }
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = { state.snapToGrid = !state.snapToGrid }) { Text(if (state.snapToGrid) "Grid: On" else "Grid: Off") }
            Spacer(Modifier.height(8.dp))
            Button(onClick = {
                onSave()
                onExitEditMode()
            }, enabled = canSave) { Text("Save") }
            Spacer(Modifier.height(8.dp))
            Button(onClick = { showSaveAsDialog = true }) { Text("Save As") }
            Spacer(Modifier.height(8.dp))
            Button(onClick = {
                state.reset()
                onExitEditMode()
            }) { Text("Discard Changes") }
        }
    }

    if (showSaveAsDialog) {
        SaveAsLayoutDialog(
            initialName = state.gameName,
            onConfirm = { name ->
                showSaveAsDialog = false
                onSaveAs(name)
                onExitEditMode()
            },
            onDismiss = { showSaveAsDialog = false }
        )
    }
}

// The editor: every element becomes a draggable / long-pressable stand-in, plus a toolbar (add /
// save / save as) and the per-element + "save as" dialogs. Elements are addressed by their stable
// [GamepadElement.id], never by list index, so adding or deleting one can never make an in-flight
// drag or open dialog point at the wrong element.
@Composable
fun BoxWithConstraintsScope.GamepadEditor(state: GamepadEditorState) {
    // Container size in pixels, used to convert drag deltas (px) into 0..1 position fractions.
    val widthPx = constraints.maxWidth.toFloat()
    val heightPx = constraints.maxHeight.toFloat()

    // Grid lines sit behind every element. The cell is a square sized off the shorter side (same reference placementOf uses), then tiled across both axes so cells stay square on any aspect ratio instead of stretching into rectangles. Drawn at the same pixel step the snap rounds to, so a snapped element lands on the intersection it shows.
    if (state.snapToGrid) {
        Canvas(Modifier.matchParentSize()) {
            val cell = minOf(size.width, size.height) / GRID_DIVISIONS
            var x = cell
            while (x < size.width) {
                drawLine(GRID_LINE_COLOR, Offset(x, 0f), Offset(x, size.height))
                x += cell
            }
            var y = cell
            while (y < size.height) {
                drawLine(GRID_LINE_COLOR, Offset(0f, y), Offset(size.width, y))
                y += cell
            }
        }
    }

    state.layout.elements.forEach { element ->
        // Two gesture detectors on the same element: drag moves it (immediately), a long press with no movement opens its editor.
        // Movement past touch slop cancels the long press, so the two do not fight.
        val editModifier = placementOf(element)
            .pointerInput(element.id, widthPx, heightPx) {
                // Accumulate position locally (synchronous, immune to recomposition timing)
                // seeded from the latest committed position at drag start, pushing each step
                // to the model.
                var px = 0.0
                var py = 0.0
                detectDragGestures(
                    onDragStart = {
                        state.layout.elements.firstOrNull { it.id == element.id }?.let {
                            px = it.positionX
                            py = it.positionY
                        }
                    },
                    onDrag = { change, dragAmount ->
                        change.consume()
                        val el = state.layout.elements.firstOrNull { it.id == element.id }
                        if (el != null) {
                            px = (px + dragAmount.x / widthPx).coerceIn(0.0, 1.0)
                            py = (py + dragAmount.y / heightPx).coerceIn(0.0, 1.0)
                            // Snapping rounds only what we commit to the model; px/py keep the raw position so the snap does not fight continued dragging.
                            // The cell is square (sized off the shorter side), so each axis snaps in pixel space rather than to a fraction of its own length.
                            if (state.snapToGrid) {
                                val cellPx = minOf(widthPx, heightPx) / GRID_DIVISIONS
                                state.update(el.movedTo(snapFraction(px, widthPx, cellPx), snapFraction(py, heightPx, cellPx)))
                            } else state.update(el.movedTo(px, py))
                        }
                    }
                )
            }
            .pointerInput(element.id) {
                detectTapGestures(onLongPress = { state.editingId = element.id })
            }

        when (element) {
            is GamepadElement.Joystick -> Joystick(
                up = element.up,
                down = element.down,
                left = element.left,
                right = element.right,
                keys = state.keys,
                interactive = false,
                sprite = element.sprite,
                spriteThumb = element.spriteThumb,
                modifier = editModifier
            )

            is GamepadElement.AnalogJoystick -> AnalogJoystick(
                stick = element.stick,
                device = element.device,
                keys = state.keys,
                interactive = false,
                sprite = element.sprite,
                spriteThumb = element.spriteThumb,
                modifier = editModifier
            )

            is GamepadElement.Key -> {
                ActionButton(
                    defaultLabelFor(element.binding),
                    element.binding,
                    element.trigger,
                    state.keys,
                    false,
                    element.sprite,
                    element.spritePressed,
                    editModifier
                )
            }
            is GamepadElement.Menu -> {
                MenuButton(false, {}, element.sprite, editModifier)
            }

            is GamepadElement.FastForward -> {
                FastForwardButton(
                    false,
                    false,
                    element,
                    {},
                    {},
                    editModifier
                )
            }

            is GamepadElement.MouseButton -> {
                MouseButtonOverrideButton(
                    false,
                    false,
                    element,
                    {},
                    {},
                    editModifier
                )
            }
        }
    }

    // key(editing.id) so the dialog's internal field state resets when a different element is picked.
    val editing = state.editingId?.let { id -> state.layout.elements.firstOrNull { it.id == id } }
    if (editing != null) {
        key(editing.id) {
            ElementEditDialog(
                element = editing,
                onChange = { state.update(it) },
                onDelete = { state.delete(editing.id) },
                onDismiss = { state.editingId = null }
            )
        }
    }
}

// Editor dialog for one element: size, opacity, the bound key(s), and delete. Edits are applied live
// through [onChange] so the change is visible behind the dialog.
@Composable
private fun ElementEditDialog(
    element: GamepadElement,
    onChange: (GamepadElement) -> Unit,
    onDelete: () -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text("Done") } },
        dismissButton = { TextButton(onClick = onDelete) { Text("Delete", color = Color(0xFFE53935)) } },
        title = {
            Text(
                when (element) {
                    is GamepadElement.Key -> "Edit button"
                    is GamepadElement.Joystick -> "Edit joystick"
                    is GamepadElement.AnalogJoystick -> "Edit analog stick"
                    is GamepadElement.Menu -> "Edit menu"
                    is GamepadElement.FastForward -> "Edit fast forward"
                    is GamepadElement.MouseButton -> "Edit mouse button"
                }
            )
        },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                Text("Size: ${percent(element.scale)}")
                Slider(
                    value = element.scale.toFloat(),
                    onValueChange = { onChange(element.withScale(it.toDouble())) },
                    valueRange = 0.05f..1f
                )
                Text("Opacity: ${percent(element.opacity)}")
                Slider(
                    value = element.opacity.toFloat(),
                    onValueChange = { onChange(element.withOpacity(it.toDouble())) },
                    valueRange = 0f..1f
                )
                Spacer(Modifier.height(8.dp))
                when (element) {
                    // The binding type is fixed at creation ("Button" -> keyboard, "Gamepad Button" ->
                    // gamepad); BindingEditor shows the matching picker for whichever it currently is.
                    is GamepadElement.Key -> {
                        BindingEditor("Button", element.binding) { onChange(element.copy(binding = it)) }
                        // Removing the base sprite also clears the pressed one, a pressed-only sprite makes no sense
                        SpriteField("Sprite", element.sprite) { onChange(element.copy(sprite = it, spritePressed = if (it == null) null else element.spritePressed)) }
                        SpriteField("Pressed sprite", element.spritePressed, enabled = element.sprite != null) { onChange(element.copy(spritePressed = it)) }
                    }
                    is GamepadElement.Joystick -> {
                        VkField("Up", element.up) { onChange(element.copy(up = it)) }
                        VkField("Down", element.down) { onChange(element.copy(down = it)) }
                        VkField("Left", element.left) { onChange(element.copy(left = it)) }
                        VkField("Right", element.right) { onChange(element.copy(right = it)) }
                        SpriteField("Base sprite", element.sprite) { onChange(element.copy(sprite = it)) }
                        SpriteField("Thumb sprite", element.spriteThumb) { onChange(element.copy(spriteThumb = it)) }
                    }
                    is GamepadElement.AnalogJoystick -> {
                        SlotField(element.device) { onChange(element.copy(device = it)) }
                        RadioButtonWithContent(element.stick == GamepadStick.LEFT, onClick = {
                            onChange(element.copy(stick = GamepadStick.LEFT))
                        }) {
                            Text("Left Stick")
                        }

                        RadioButtonWithContent(element.stick == GamepadStick.RIGHT, onClick = {
                            onChange(element.copy(stick = GamepadStick.RIGHT))
                        }) {
                            Text("Right Stick")
                        }

                        SpriteField("Base sprite", element.sprite) { onChange(element.copy(sprite = it)) }
                        SpriteField("Thumb sprite", element.spriteThumb) { onChange(element.copy(spriteThumb = it)) }
                    }

                    is GamepadElement.Menu -> {
                        SpriteField("Sprite", element.sprite) { onChange(element.copy(sprite = it)) }
                    }
                    is GamepadElement.FastForward -> {
                        Text(text = element.speed.toString())

                        val rangeStart = 0.25f
                        val rangeEnd = 8f
                        val stepSize = 0.25f
                        val steps = ((rangeEnd - rangeStart) / stepSize).toInt() - 1

                        Slider(
                            value = element.speed,
                            onValueChange = {
                                // We do this to avoid floating point inaccuracies
                                onChange(element.copy(speed = (it.absoluteValue / 0.25f).roundToInt() * 0.25f))
                            },
                            // The "steps" is how many "steps" there will be, NOT how much the value will change
                            steps = steps,
                            valueRange = 0.25f..8f
                        )

                        RadioButtonWithContent(element.toggle, onClick = {
                            onChange(element.copy(toggle = true))
                        }) {
                            Text("Toggle")
                        }

                        RadioButtonWithContent(!element.toggle, onClick = {
                            onChange(element.copy(toggle = false))
                        }) {
                            Text("Hold")
                        }

                        SpriteField("Sprite", element.sprite) { onChange(element.copy(sprite = it, spritePressed = if (it == null) null else element.spritePressed)) }
                        SpriteField("Pressed sprite", element.spritePressed, enabled = element.sprite != null) { onChange(element.copy(spritePressed = it)) }
                    }
                    is GamepadElement.MouseButton -> {
                        MouseButtonField(element.button) { onChange(element.copy(button = it)) }

                        Spacer(Modifier.height(8.dp))

                        RadioButtonWithContent(element.toggle, onClick = {
                            onChange(element.copy(toggle = true))
                        }) {
                            Text("Toggle")
                        }

                        RadioButtonWithContent(!element.toggle, onClick = {
                            onChange(element.copy(toggle = false))
                        }) {
                            Text("Hold")
                        }

                        SpriteField("Sprite", element.sprite) { onChange(element.copy(sprite = it, spritePressed = if (it == null) null else element.spritePressed)) }
                        SpriteField("Pressed sprite", element.spritePressed, enabled = element.sprite != null) { onChange(element.copy(spritePressed = it)) }
                    }
                }
            }
        }
    )
}

// Name prompt for "Save As". The entered name becomes the layout's fancyName, which the layout
// manager will surface later. Confirm is disabled on a blank name.
@Composable
private fun SaveAsLayoutDialog(initialName: String, onConfirm: (String) -> Unit, onDismiss: () -> Unit) {
    var name by remember { mutableStateOf(initialName) }
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = { onConfirm(name) }, enabled = name.isNotBlank()) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
        title = { Text("Save layout as") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Name") },
                singleLine = true
            )
        }
    )
}

// Key picker for the binding behind a control: a Material exposed dropdown (outlined field with a
// floating label + trailing chevron) listing the keys the runner understands, so a user chooses by
// name instead of typing a raw vk code. Selecting one always produces a keyboard binding. If the
// current code is not in the list (e.g. a gamepad button) we show the raw code.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun VkField(label: String, binding: InputBinding, onChange: (InputBinding) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val current = GmlKey.fromCode(binding.code())
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        OutlinedTextField(
            value = current?.label ?: binding.code().toString(),
            onValueChange = {},
            readOnly = true,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable)
                .fillMaxWidth()
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            GmlKey.entries.forEach { gmlKey ->
                DropdownMenuItem(
                    text = { Text(gmlKey.label) },
                    onClick = {
                        onChange(InputBinding.Keyboard(gmlKey.code))
                        expanded = false
                    }
                )
            }
        }
    }
}

private const val VIRTUAL_SLOT_COUNT = 16

// Picker for a Key's binding. The binding's concrete type is fixed when the element is added, so we
// just render the matching editor: a keyboard key picker, or a controller slot + gamepad button pair.
@Composable
private fun BindingEditor(label: String, binding: InputBinding, onChange: (InputBinding) -> Unit) {
    when (binding) {
        is InputBinding.Keyboard -> VkField(label, binding, onChange)
        is InputBinding.GamepadButton -> {
            SlotField(binding.device) { onChange(binding.copy(device = it)) }
            GamepadButtonField(binding.button) { onChange(binding.copy(button = it)) }
        }
    }
}

// Picks which controller slot (player) a gamepad element feeds.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SlotField(device: Int, onChange: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    fun labelFor(slot: Int) = "Player ${slot + 1} (slot $slot)"
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        OutlinedTextField(
            value = labelFor(device),
            onValueChange = {},
            readOnly = true,
            label = { Text("Controller slot") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable)
                .fillMaxWidth()
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            for (slot in 0 until VIRTUAL_SLOT_COUNT) {
                DropdownMenuItem(
                    text = { Text(labelFor(slot)) },
                    onClick = {
                        onChange(slot)
                        expanded = false
                    }
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun MouseButtonField(button: GmlMouseButton, onChange: (GmlMouseButton) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val options = listOf(
        GmlMouseButton.RIGHT_BUTTON,
        GmlMouseButton.MIDDLE_BUTTON
    )
    fun labelFor(b: GmlMouseButton) = when (b) {
        GmlMouseButton.LEFT_BUTTON -> error("This should NEVER happen!")
        GmlMouseButton.RIGHT_BUTTON -> "Right Button"
        GmlMouseButton.MIDDLE_BUTTON -> "Middle Button"
    }
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        OutlinedTextField(
            value = labelFor(button),
            onValueChange = {},
            readOnly = true,
            label = { Text("Mouse button") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable)
                .fillMaxWidth()
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            for (b in options) {
                DropdownMenuItem(
                    text = { Text(labelFor(b)) },
                    onClick = {
                        onChange(b)
                        expanded = false
                    }
                )
            }
        }
    }
}

// Picks which gamepad button a "Gamepad Button" element sends. Stores the canonical button index.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun GamepadButtonField(button: Int, onChange: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val current = Gamepad.Button.fromIndex(button)
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        OutlinedTextField(
            value = current?.label ?: button.toString(),
            onValueChange = {},
            readOnly = true,
            label = { Text("Button") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable)
                .fillMaxWidth()
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            Gamepad.Button.entries.forEach { gpButton ->
                DropdownMenuItem(
                    text = { Text(gpButton.label) },
                    onClick = {
                        onChange(gpButton.index)
                        expanded = false
                    }
                )
            }
        }
    }
}

// One sprite slot of a Key element, styled like the game metadata IconSection: a tappable row with
// a thumbnail and a "Tap to choose/change" hint, plus a trailing X to remove the image. Picking only
// writes the file into the pool and updates the in-memory element, deletion of unreferenced files is
// handled by the sprite sweep on save.
@Composable
private fun SpriteField(label: String, fileName: String?, enabled: Boolean = true, onChange: (String?) -> Unit) {
    val context = LocalContext.current
    val layoutLibrary = Libraries.loadLayoutLibrary(context)
    val scope = rememberCoroutineScope()
    val currentOnChange by rememberUpdatedState(onChange)
    val pickerLauncher = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) {
            scope.launch {
                val name = withContext(Dispatchers.IO) { importSpriteFromUri(context, layoutLibrary, uri) }
                if (name != null) currentOnChange(name)
            }
        }
    }
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .clickable(enabled = enabled) { pickerLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) }
            .padding(8.dp)
            .alpha(if (enabled) 1f else 0.38f)
    ) {
        val bitmap = rememberSpriteBitmap(fileName)
        if (bitmap != null) {
            Image(bitmap = bitmap, contentDescription = null, contentScale = ContentScale.Fit, modifier = Modifier.size(48.dp))
        } else {
            Box(
                modifier = Modifier
                    .size(48.dp)
                    .border(1.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center
            ) {
                Icon(Icons.Filled.Add, contentDescription = null)
            }
        }
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(label, style = MaterialTheme.typography.titleSmall)
            Text(
                text = when {
                    !enabled -> "Set a sprite first"
                    fileName == null -> "Tap to choose"
                    else -> "Tap to change"
                },
                style = MaterialTheme.typography.bodySmall
            )
        }
        if (fileName != null) {
            IconButton(onClick = { currentOnChange(null) }) {
                Icon(Icons.Filled.Close, contentDescription = "Remove sprite")
            }
        }
    }
}

// Decodes the picked image, downscales it to at most 512px on the longest edge and stores it in the sprite pool as PNG
private fun importSpriteFromUri(context: Context, layoutLibrary: LayoutLibrary, uri: Uri): String? {
    return try {
        val source = context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it) } ?: return null
        val maxDimension = maxOf(source.width, source.height)
        val bitmap = if (maxDimension > 512) Bitmap.createScaledBitmap(source, (source.width * 512 / maxDimension).coerceAtLeast(1), (source.height * 512 / maxDimension).coerceAtLeast(1), true) else source
        val output = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)
        layoutLibrary.storeSprite(output.toByteArray())
    } catch (e: Exception) {
        null
    }
}

// Number of cells the placement grid is split into along each axis
private const val GRID_DIVISIONS = 16
private val GRID_LINE_COLOR = Color.White.copy(alpha = 0.15f)

// Round a 0..1 position fraction to the nearest square-cell intersection. The cell is given in pixels (same on both axes), so we convert the fraction to pixels, snap, then back to a fraction of this axis' length.
private fun snapFraction(fraction: Double, axisLengthPx: Float, cellPx: Float): Double {
    if (cellPx <= 0f) return fraction
    val snappedPx = (fraction * axisLengthPx / cellPx).roundToInt() * cellPx
    return (snappedPx / axisLengthPx).toDouble().coerceIn(0.0, 1.0)
}

private fun percent(value: Double): String = "${(value * 100).toInt()}%"

// Position/size/opacity copies. GamepadElement is sealed with no copy on the base, so each variant
// has to be copied explicitly.
private fun GamepadElement.movedTo(px: Double, py: Double): GamepadElement = when (this) {
    is GamepadElement.Key -> copy(positionX = px, positionY = py)
    is GamepadElement.Joystick -> copy(positionX = px, positionY = py)
    is GamepadElement.AnalogJoystick -> copy(positionX = px, positionY = py)
    is GamepadElement.Menu -> copy(positionX = px, positionY = py)
    is GamepadElement.FastForward -> copy(positionX = px, positionY = py)
    is GamepadElement.MouseButton -> copy(positionX = px, positionY = py)
}
private fun GamepadElement.withScale(s: Double): GamepadElement = when (this) {
    is GamepadElement.Key -> copy(scale = s)
    is GamepadElement.Joystick -> copy(scale = s)
    is GamepadElement.AnalogJoystick -> copy(scale = s)
    is GamepadElement.Menu -> copy(scale = s)
    is GamepadElement.FastForward -> copy(scale = s)
    is GamepadElement.MouseButton -> copy(scale = s)
}
private fun GamepadElement.withOpacity(o: Double): GamepadElement = when (this) {
    is GamepadElement.Key -> copy(opacity = o)
    is GamepadElement.Joystick -> copy(opacity = o)
    is GamepadElement.AnalogJoystick -> copy(opacity = o)
    is GamepadElement.Menu -> copy(opacity = o)
    is GamepadElement.FastForward -> copy(opacity = o)
    is GamepadElement.MouseButton -> copy(opacity = o)
}

// Read the integer code behind a binding (keyboard vk or gamepad button number), used to find the
// matching entry for the key dropdown.
private fun InputBinding.code(): Int = when (this) {
    is InputBinding.Keyboard -> vk
    is InputBinding.GamepadButton -> button
}