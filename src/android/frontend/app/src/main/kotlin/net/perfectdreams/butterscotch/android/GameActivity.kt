package net.perfectdreams.butterscotch.android

import android.hardware.input.InputManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.movableContentOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import kotlinx.coroutines.flow.update
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.viewinterop.AndroidView
import net.perfectdreams.butterscotch.android.components.FreeCameraOverlay
import net.perfectdreams.butterscotch.android.components.GameControls
import net.perfectdreams.butterscotch.android.components.GamepadEditorState
import net.perfectdreams.butterscotch.android.components.GamepadEditorToolbar
import net.perfectdreams.butterscotch.android.components.MenuOverlay
import net.perfectdreams.butterscotch.android.input.Haptics
import net.perfectdreams.butterscotch.android.layouts.GamepadElement
import net.perfectdreams.butterscotch.android.layouts.GamepadLayout
import net.perfectdreams.butterscotch.android.layouts.GmlMouseButton
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.settings.SettingsStore
import net.perfectdreams.butterscotch.android.theme.ButterscotchAndroidTheme
import java.io.File
import java.util.UUID
import java.util.zip.CRC32

/**
 * Hosts a [SurfaceView] that the native side draws into, with a Compose-based virtual gamepad
 * overlay on top.
 *
 * The native side owns the EGL context + render thread. We forward [SurfaceHolder.Callback]
 * lifecycle events to [ButterscotchNative], and that's the whole bridge — there's no Kotlin-side
 * renderer to manage, no GLSurfaceView, no preserveEGLContextOnPause hack. Rotation and re-parent
 * just destroy/recreate the underlying Android Surface; the native context keeps its GL state.
 *
 * Launched with [EXTRA_GAME_ID]; the WAD path and saves directory are resolved through
 * [net.perfectdreams.butterscotch.android.library.GameLibrary] from that id. Finishes immediately if the id is missing or unknown (e.g. the
 * library entry was removed between the launcher's snapshot and the click).
 */
class GameActivity : ComponentActivity() {
    var butterscotchRunner: ButterscotchDroidRunner? = null
    lateinit var gameLibrary: GameLibrary
    lateinit var layoutLibrary: LayoutLibrary
    lateinit var settingsStore: SettingsStore
    lateinit var haptics: Haptics
    var isVirtualKeysVisible by mutableStateOf<Boolean>(true)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        gameLibrary = Libraries.loadGameLibrary(this.applicationContext)
        layoutLibrary = Libraries.loadLayoutLibrary(this.applicationContext)
        settingsStore = Libraries.loadSettingsStore(this.applicationContext)
        haptics = Haptics(this.applicationContext)

        // Keep the screen on while the game runs - matches the GLFW build's default behavior.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }

        val gameIdAsString = intent.getStringExtra(EXTRA_GAME_ID)
        if (gameIdAsString == null) {
            Log.e(TAG, "GameActivity launched without $EXTRA_GAME_ID extra")
            finish()
            return
        }

        val gameId = UUID.fromString(gameIdAsString)
        val entry = gameLibrary.findById(gameId)
        if (entry == null) {
            Log.e(TAG, "No library entry for gameId=$gameId")
            finish()
            return
        }
        val wadFile = gameLibrary.wadPath(entry)
        val savesDir = gameLibrary.savesDir(entry).apply { mkdirs() }
        if (!wadFile.exists()) {
            Log.e(TAG, "WAD file missing at $wadFile (library entry is stale)")
            finish()
            return
        }

        // Reset the exit latch — hasExited is process-singleton state, so a previous session's
        // exit would otherwise immediately finish() us via the LaunchedEffect below.
        ButterscotchNative.resetExitLatch()

        val logsDir = gameLibrary.logsDir(entry)
        logsDir.mkdirs()
        val logsFile = File(logsDir, "latest.log")
        logsFile.delete()

        val butterscotchRunner = ButterscotchDroidRunner(
            this.assets,
            wadFile.absolutePath,
            crc32(wadFile),
            savesDir.absolutePath,
            logsFile,
            entry.runnerOs.nativeValue,
            entry.enablePhysicalControllers,
            entry.enablePhysicalKeyboard,
            entry.enableWidescreenHack,
            entry.postProcessing,
            true
        )
        this.butterscotchRunner = butterscotchRunner

        setContent {
            ButterscotchAndroidTheme {
                // Reads both settings fields at call time, so toggling them takes effect live
                val performHaptic = {
                    val s = settingsStore.settings
                    if (s.enableHapticFeedback) haptics.tick(s.hapticStrength)
                }

                // VirtualKeyState lives here (not inside the gamepad) so we can release-all on layout
                // changes that reflow the gamepad's Composable tree. If it lived inside GameControls
                // and Compose discarded that subtree across an orientation flip, held keys could "leak"
                // into the runner with no Composable left to release them.
                val keys = remember { VirtualKeyState(butterscotchRunner, performHaptic) }

                var menuOpen by remember { mutableStateOf(false) }
                // Free cam state lives on the runner (single source of truth, read on the render thread);
                // we observe it here so the UI reacts to it.
                val freeCam by butterscotchRunner.freeCamera.collectAsState()
                var fastForwardActiveButtonId by remember { mutableStateOf<UUID?>(null) }
                var activeMouseButtonId by remember { mutableStateOf<UUID?>(null) }
                // The mouse button a viewport press currently emits. Defaults to left; an active Mouse Button element overrides it.
                val mouseButtonOverride = remember { mutableStateOf(GmlMouseButton.LEFT_BUTTON) }
                // Hoisted so the in-game Settings switch reflects (and persists) the live runner flag.
                var widescreenHackEnabled by remember { mutableStateOf(entry.enableWidescreenHack) }
                // Same pattern for the post-processing shader: drives the live runner field and persists
                var postProcessing by remember { mutableStateOf(entry.postProcessing) }
                var editorState by remember { mutableStateOf<GamepadEditorState?>(null) }

                val isPaused = editorState != null || menuOpen

                LaunchedEffect(isPaused) {
                    if (!isPaused) {
                        // When we stop being paused, we release all keys
                        keys.releaseAll()
                    }

                    butterscotchRunner.paused.value = isPaused
                    // We also reset the fast forward speed to avoid a fast forward button being deleted while it is still active
                    fastForwardActiveButtonId = null
                    activeMouseButtonId = null
                }

                // Toggling edit mode drops any held keys so a press in flight does not stick.
                LaunchedEffect(editorState != null) { keys.releaseAll() }

                // Auto-finish when the native runner reports it has exited (game quit, fatal error).
                val hasExited = ButterscotchNative.hasExited
                LaunchedEffect(hasExited) {
                    if (hasExited)
                        finish()
                }

                val targetOperatingSystemDisplaySize: IntSize? = entry.runnerOs.displayResolution

                // movableContentOf wraps the SurfaceView so re-parenting it between Overlay/Stacked
                // layouts doesn't destroy and recreate the View. The SurfaceView's underlying Android
                // Surface MAY still be recreated by the system (and that's fine — the native side
                // handles surfaceDestroyed/Created), but keeping the View instance means we don't
                // re-attach the SurfaceHolder.Callback every time and the transition is cheaper.
                val gameSurface = remember {
                    movableContentOf<Modifier> { modifier ->
                        AndroidView(
                            modifier = modifier.onSizeChanged {
                                butterscotchRunner.setGameSurfaceSize(it)
                            }.pointerInput(Unit) {
                                val buttonForPointer = mutableMapOf<PointerId, GmlMouseButton>()
                                awaitPointerEventScope {
                                    while (true) {
                                        val event = awaitPointerEvent()
                                        this@GameActivity.isVirtualKeysVisible = true

                                        for (change in event.changes) {
                                            if (change.pressed != change.previousPressed) {
                                                if (change.pressed) {
                                                    val button = mouseButtonOverride.value
                                                    buttonForPointer[change.id] = button
                                                    butterscotchRunner.onMouseButton(button, true)
                                                } else {
                                                    val button = buttonForPointer.remove(change.id) ?: GmlMouseButton.LEFT_BUTTON
                                                    butterscotchRunner.onMouseButton(button, false)
                                                }
                                            }

                                            val position = change.position

                                            val fractionX = position.x / size.width
                                            val fractionY = position.y / size.height

                                            butterscotchRunner.onMouseMove(fractionX, fractionY)
                                        }
                                    }
                                }
                            },
                            factory = { ctx ->
                                SurfaceView(ctx).apply {
                                    holder.addCallback(object : SurfaceHolder.Callback {
                                        override fun surfaceCreated(holder: SurfaceHolder) {
                                            butterscotchRunner.startRenderLoop(holder.surface)
                                        }

                                        override fun surfaceChanged(
                                            holder: SurfaceHolder,
                                            format: Int,
                                            width: Int,
                                            height: Int
                                        ) {
                                            butterscotchRunner.onSurfaceResized(width, height)
                                        }

                                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                                            butterscotchRunner.stopRenderLoop()
                                        }
                                    })
                                }
                            }
                        )
                    }
                }

                Box(
                    Modifier
                        .background(Color.Black)
                        .fillMaxSize()
                ) {
                    BoxWithConstraints(Modifier.fillMaxSize()) {
                        val gameSize = ButterscotchNative.currentGameSize

                        val contentAspect = if (targetOperatingSystemDisplaySize != null) {
                            targetOperatingSystemDisplaySize.width.toFloat() / targetOperatingSystemDisplaySize.height.toFloat()
                        } else if (gameSize != null && gameSize.height > 0) {
                            gameSize.width.toFloat() / gameSize.height.toFloat()
                        } else {
                            4f / 3f
                        }
                        val deviceAspect = constraints.maxWidth.toFloat() / constraints.maxHeight.toFloat()
                        val layoutMode = pickLayoutMode(contentAspect, deviceAspect)

                        LaunchedEffect(layoutMode) {
                            keys.releaseAll()
                        }

                        val isPortrait = constraints.maxHeight >= constraints.maxWidth

                        val liveEntry = requireNotNull(gameLibrary.findById(gameId))

                        // Resolve this game's assigned layout for the current orientation, falling back to the built-in default if the id is missing.
                        // Keyed on (isPortrait, assignedId) so it  re-resolves on rotation AND when the assignment changes (after Save As).
                        // An Overlay/Stacked reflow changes neither, so in-progress edits survive it.
                        val assignedId = if (isPortrait) liveEntry.portraitLayout else liveEntry.landscapeLayout
                        val fallbackId = if (isPortrait) LayoutLibrary.DEFAULT_PORTRAIT_LAYOUT else LayoutLibrary.DEFAULT_LANDSCAPE_LAYOUT

                        Log.i(
                            TAG,
                            "Using $assignedId for the gamepad layout... The fallback ID is $fallbackId"
                        )

                        var layout by remember(isPortrait, assignedId) {
                            val newLayout = layoutLibrary.findById(assignedId) ?: layoutLibrary.findById(fallbackId)!!
                            mutableStateOf(newLayout)
                        }

                        // Update editor state if the layout was changed
                        LaunchedEffect(layout) {
                            val currentEditorState = editorState
                            if (currentEditorState != null) {
                                currentEditorState.layout = layout
                                currentEditorState.initialLayout = layout // Keep it synchronized if we ever add a option to revert changes without exiting
                            }

                            // Also disable fast forward and mouse button state
                            fastForwardActiveButtonId = null
                            mouseButtonOverride.value = GmlMouseButton.LEFT_BUTTON
                        }

                        val onFastForwardPress = { it: GamepadElement.FastForward ->
                            performHaptic()
                            if (it.toggle && fastForwardActiveButtonId == it.id) {
                                fastForwardActiveButtonId = null
                            } else {
                                fastForwardActiveButtonId = it.id
                            }
                        }
                        val onFastForwardRelease = { it: GamepadElement.FastForward ->
                            if (fastForwardActiveButtonId == it.id)
                                fastForwardActiveButtonId = null
                        }

                        // We need to have this here because we NEED the current element
                        LaunchedEffect(fastForwardActiveButtonId) {
                            if (fastForwardActiveButtonId == null) {
                                butterscotchRunner.fastForwardSpeed = 1.0f
                            } else {
                                val element = layout.elements.firstOrNull { it.id == fastForwardActiveButtonId } as GamepadElement.FastForward?

                                if (element != null) {
                                    butterscotchRunner.fastForwardSpeed = element.speed
                                } else {
                                    // I doubt this can happen because we do set it to null when the menu is open/paused, but...
                                    fastForwardActiveButtonId = null
                                }
                            }
                        }

                        val onMouseButtonPress = { it: GamepadElement.MouseButton ->
                            performHaptic()
                            if (it.toggle && activeMouseButtonId == it.id) {
                                activeMouseButtonId = null
                            } else {
                                activeMouseButtonId = it.id
                            }
                        }
                        val onMouseButtonRelease = { it: GamepadElement.MouseButton ->
                            if (activeMouseButtonId == it.id)
                                activeMouseButtonId = null
                        }

                        LaunchedEffect(activeMouseButtonId) {
                            val element = layout.elements.firstOrNull { it.id == activeMouseButtonId } as GamepadElement.MouseButton?
                            mouseButtonOverride.value = element?.button ?: GmlMouseButton.LEFT_BUTTON
                        }

                        when (layoutMode) {
                            LayoutMode.Overlay -> {
                                Box(Modifier.fillMaxSize()) {
                                    gameSurface(Modifier.fillMaxSize())
                                    if (!freeCam.active && isVirtualKeysVisible) {
                                        GameControls(
                                            layout = layout,
                                            editModeState = editorState,
                                            activeFastForwardButtonId = fastForwardActiveButtonId,
                                            activeMouseButtonId = activeMouseButtonId,
                                            onMenuOpen = {
                                                performHaptic()
                                                menuOpen = true
                                            },
                                            onFastForwardPress = onFastForwardPress,
                                            onFastForwardRelease = onFastForwardRelease,
                                            onMouseButtonPress = onMouseButtonPress,
                                            onMouseButtonRelease = onMouseButtonRelease,
                                            keys = keys,
                                            modifier = Modifier.fillMaxSize()
                                        )
                                    }
                                }
                            }

                            LayoutMode.Stacked -> {
                                Column(Modifier.fillMaxSize()) {
                                    Box(
                                        Modifier
                                            .fillMaxWidth()
                                            .weight(1f),
                                        contentAlignment = Alignment.Center
                                    ) {
                                        gameSurface(Modifier.fillMaxSize())
                                    }

                                    if (!freeCam.active && isVirtualKeysVisible) {
                                        GameControls(
                                            layout = layout,
                                            editModeState = editorState,
                                            activeFastForwardButtonId = fastForwardActiveButtonId,
                                            activeMouseButtonId = activeMouseButtonId,
                                            onMenuOpen = {
                                                performHaptic()
                                                menuOpen = true
                                            },
                                            onFastForwardPress = onFastForwardPress,
                                            onFastForwardRelease = onFastForwardRelease,
                                            onMouseButtonPress = onMouseButtonPress,
                                            onMouseButtonRelease = onMouseButtonRelease,
                                            keys = keys,
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .weight(1f)
                                        )
                                    }
                                }
                            }
                        }

                        MenuOverlay(
                            menuOpen,
                            onMenuToggle = {
                                menuOpen = it
                            },
                            onExitGame = {
                                // This is blocking
                                butterscotchRunner.requestExit()
                            },
                            onEditLayout = {
                                editorState = GamepadEditorState(liveEntry.title, keys, layout)
                            },
                            onEnableFreeCam = {
                                butterscotchRunner.freeCamera.update { it.copy(active = true) }
                            },
                            // Settings exposes both orientations at once, so each picker only lists layouts for its own orientation
                            portraitLayouts = layoutLibrary.entries.filter { it.orientation == GamepadLayout.GamepadTargetOrientation.PORTRAIT },
                            landscapeLayouts = layoutLibrary.entries.filter { it.orientation == GamepadLayout.GamepadTargetOrientation.LANDSCAPE },
                            selectedPortraitLayoutId = liveEntry.portraitLayout,
                            selectedLandscapeLayoutId = liveEntry.landscapeLayout,
                            onSelectPortraitLayout = { id ->
                                gameLibrary.update(entry.id) { e -> e.copy(portraitLayout = id) }
                                // Only swap the rendered layout if it is the one currently on screen
                                if (isPortrait) layoutLibrary.findById(id)?.let { layout = it }
                            },
                            onSelectLandscapeLayout = { id ->
                                gameLibrary.update(entry.id) { e -> e.copy(landscapeLayout = id) }
                                if (!isPortrait) layoutLibrary.findById(id)?.let { layout = it }
                            },
                            widescreenHackEnabled = widescreenHackEnabled,
                            onToggleWidescreenHack = { enabled ->
                                widescreenHackEnabled = enabled
                                butterscotchRunner.enableWidescreenHack = enabled
                                gameLibrary.update(entry.id) { it.copy(enableWidescreenHack = enabled) }
                            },
                            postProcessing = postProcessing,
                            onChangePostProcessing = { newSettings ->
                                postProcessing = newSettings
                                butterscotchRunner.postProcessing = newSettings
                                gameLibrary.update(entry.id) { it.copy(postProcessing = newSettings) }
                            },
                            logsDir = gameLibrary.logsDir(entry.id),
                        )

                        // We want this to be on top of the menu
                        if (freeCam.active) {
                            FreeCameraOverlay(
                                onCameraChange = { px, py, z -> butterscotchRunner.freeCamera.update { it.copy(panX = px, panY = py, zoom = z) } },
                                // Reset everything (pan/zoom + active) to identity in one atomic assignment.
                                onClose = { butterscotchRunner.freeCamera.value = ButterscotchDroidRunner.FreeCameraState() }
                            )
                        }

                        val currentEditorState = editorState
                        if (currentEditorState != null) {
                            // Save only applies to user layouts; the built-in defaults are read-only.
                            val canSave = !layoutLibrary.isBuiltIn(layout.id)
                            val onSave = {
                                layoutLibrary.upsert(currentEditorState.layout)
                                layout = currentEditorState.layout
                            }
                            val onSaveAs = { name: String ->
                                // Fork to a fresh id with the given name, persist it, and point this game's
                                // orientation-specific layout at the copy so it loads next time. Keep editing it.
                                val forked = currentEditorState.layout.copy(id = UUID.randomUUID(), fancyName = name)
                                layoutLibrary.upsert(forked)
                                gameLibrary.update(entry.id) { e ->
                                    if (isPortrait) e.copy(portraitLayout = forked.id) else e.copy(
                                        landscapeLayout = forked.id
                                    )
                                }
                                layout = forked
                            }

                            GamepadEditorToolbar(
                                currentEditorState,
                                onExitEditMode = { editorState = null },
                                canSave = canSave,
                                onSave = onSave,
                                onSaveAs = onSaveAs,
                            )
                        }
                    }
                }
            }
        }
    }

    override fun onResume() {
        // onResume is called AFTER onCreate (actually it is after onStart, but shhh you get what I'm saying)
        super.onResume()
        val runner = this.butterscotchRunner ?: return

        if (runner.enablePhysicalControllers) {
            getSystemService(InputManager::class.java)?.registerInputDeviceListener(runner.gamepadRouter.listener, Handler(Looper.getMainLooper()))
            // Pick up controllers that are already attached (and re-attach any that arrived while paused)
            runner.gamepadRouter.refresh()
        }
    }

    override fun onPause() {
        super.onPause()
        val runner = this.butterscotchRunner ?: return

        if (runner.enablePhysicalControllers) {
            getSystemService(InputManager::class.java)?.unregisterInputDeviceListener(runner.gamepadRouter.listener)
            // Drop every controller so nothing is left held down while we're backgrounded
            runner.gamepadRouter.releaseAll()
        }

        // Drop every held keyboard key too, so nothing stays stuck down while we're backgrounded
        if (runner.enablePhysicalKeyboard)
            runner.keyboardRouter.releaseAll()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val runner = this.butterscotchRunner ?: return false

        // We return true here so that gamepad keypresses don't bubble up to Compose
        if (runner.enablePhysicalControllers && runner.gamepadRouter.handleKeyEvent(event)) {
            if (this.settingsStore.settings.hideVirtualGamepadWhenUsingPhysicalController)
                this.isVirtualKeysVisible = false
            return true
        }

        // Same idea for a physical keyboard: consume it so it doesn't reach Compose
        if (runner.enablePhysicalKeyboard && runner.keyboardRouter.handleKeyEvent(event)) {
            if (this.settingsStore.settings.hideVirtualGamepadWhenUsingPhysicalKeyboard)
                this.isVirtualKeysVisible = false
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val runner = this.butterscotchRunner ?: return false

        // We return true here so that gamepad keypresses don't bubble up to Compose
        if (runner.enablePhysicalControllers && runner.gamepadRouter.handleMotionEvent(event)) {
            if (this.settingsStore.settings.hideVirtualGamepadWhenUsingPhysicalController)
                this.isVirtualKeysVisible = false
            return true
        }

        return super.dispatchGenericMotionEvent(event)
    }

    override fun onDestroy() {
        butterscotchRunner?.requestExit()
        super.onDestroy()
    }

    fun crc32(file: File): Long {
        val crc = CRC32()
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            var read = input.read(buffer)
            while (read != -1) {
                crc.update(buffer, 0, read)
                read = input.read(buffer)
            }
        }
        return crc.value
    }

    companion object {
        private const val TAG = "GameActivity"
        const val EXTRA_GAME_ID = "net.perfectdreams.butterscotch.android.EXTRA_GAME_ID"
    }
}
