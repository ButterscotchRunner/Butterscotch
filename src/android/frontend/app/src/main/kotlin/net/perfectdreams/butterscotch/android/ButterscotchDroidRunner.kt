package net.perfectdreams.butterscotch.android

import android.content.res.AssetManager
import android.opengl.GLES20
import android.opengl.GLES30
import android.util.Log
import android.view.Surface
import androidx.compose.ui.unit.IntSize
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.yield
import net.perfectdreams.butterscotch.android.layouts.GmlMouseButton
import net.perfectdreams.butterscotch.android.library.GameEntry
import net.perfectdreams.butterscotch.android.shaders.BlitShader
import net.perfectdreams.butterscotch.android.shaders.CrtShader
import net.perfectdreams.harmony.gl.shaders.ShaderManager
import net.perfectdreams.harmony.gl.shaders.bind
import java.io.File
import java.util.concurrent.Executors

// The Butterscotch Android API is actually "global bound", but we use a class to help managing things here (and will be useful if we refactor down the road)
class ButterscotchDroidRunner(
    val assets: AssetManager,
    val dataWinPath: String,
    val wadHash: Long,
    val savesPath: String,
    val logFile: File,
    val osType: Int,
    val enablePhysicalControllers: Boolean,
    val enablePhysicalKeyboard: Boolean,
    var enableWidescreenHack: Boolean,
    var postProcessing: GameEntry.PostProcessingSettings,
    val isPlus: Boolean
) {
    companion object {
        private const val TAG = "ButterscotchDroidRunner"

        private val glExec = Executors.newSingleThreadExecutor { Thread(it, "ButterscotchGLRender") }
        private val glDispatcher = glExec.asCoroutineDispatcher()
        private val glScope = CoroutineScope(glDispatcher + SupervisorJob())
    }

    private val egl = ButterscotchEGL()
    private var renderJob: Job? = null
    private var runnerStarted = false
    private var started = false
    var paused = MutableStateFlow(false)
    private val inputChannel = Channel<InputEvent>(
        capacity = 256,
        onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val gamepadRouter = GamepadRouter(this)
    val keyboardRouter = KeyboardRouter(this)
    var fastForwardSpeed = 1.0f
    var surfaceSize = IntSize.Zero
    val freeCamera = MutableStateFlow(FreeCameraState())
    var fboId: Int = 0
    var blitTextureId: Int = 0
    private var fboWidth: Int = 0
    private var fboHeight: Int = 0
    private var renderW: Int = 1
    private var renderH: Int = 1
    private var renderX: Int = 0
    private var renderY: Int = 0
    val shaderManager = ShaderManager()
    lateinit var crtShader: CrtShader
    lateinit var blitShader: BlitShader
    private var stdioCallback: ((String) -> (Unit))? = null

    data class FreeCameraState(
        val active: Boolean = false,
        val panX: Float = 0.0f,
        val panY: Float = 0.0f,
        val zoom: Float = 1.0f
    )

    init {
        this.stdioCallback = ButterscotchNative.registerStdioListener {
            this@ButterscotchDroidRunner.logFile.appendText(it + "\n")
        }
    }

    /**
     * Forward a SurfaceHolder size change to the EGL layer so the next frame renders at the right resolution.
     *
     * This NEEDS to be synchronized with the render thread, which is why we use a coroutine.
     */
    fun onSurfaceResized(width: Int, height: Int) {
        glScope.launch {
            egl.updateSize(width, height)
        }
    }

    fun startRenderLoop(surface: Surface) {
        require(renderJob == null) { "Trying to start a renderJob while one is already active! Bug?" }

        Log.i(TAG, "Starting rendering loop...")

        renderJob = glScope.launch {
            try {
                Log.i(TAG, "Binding surface to window...")

                if (!egl.bindWindow(surface)) {
                    Log.e(TAG, "Failed to bind EGL surface! Aborting render loop...")
                    return@launch
                }

                if (!runnerStarted) {
                    // We only start the runner here because we NEED to have an EGL context, because the GLRenderer needs it on glInit
                    Log.i(TAG, "Starting runner...")

                    prepare()

                    ButterscotchNative.startRunner(dataWinPath, savesPath, osType, this@ButterscotchDroidRunner.fboId)
                    val dataWinHandle = ButterscotchNative.getRunningDataWinHandle()
                    val dataWinName = ButterscotchNative.dataWinName(dataWinHandle)
                    val dataWinDisplayName = ButterscotchNative.dataWinDisplayName(dataWinHandle)
                    val dataWinWadVersion = ButterscotchNative.dataWinWadVersion(dataWinHandle)
                    val gmsVersion = ButterscotchNative.dataWinGmsVersion(dataWinHandle)
                    val detectedGmsVersion = ButterscotchNative.dataWinDetectedGmsVersion(dataWinHandle)

                    // glGetString requires a current GL context, which we have here because bindWindow succeeded on this thread
                    val gpuVendor = GLES20.glGetString(GLES20.GL_VENDOR) ?: "Unknown"
                    val gpuRenderer = GLES20.glGetString(GLES20.GL_RENDERER) ?: "Unknown"
                    val gpuVersion = GLES20.glGetString(GLES20.GL_VERSION) ?: "Unknown"

                    ButterscotchUtils.fireGameLaunchEvent(wadHash, dataWinName, dataWinDisplayName, dataWinWadVersion, gmsVersion, detectedGmsVersion, gpuVendor, gpuRenderer, gpuVersion, isPlus)

                    runnerStarted = true
                }

                var lastFrameStartNs = System.nanoTime()

                // Don't worry about the egl.hasSurface check, if the surface dies, the finally block will be executed and (hopefully) a new surface will be created,
                // starting a whole new render loop :3
                while (isActive && egl.hasSurface) {
                    if (this@ButterscotchDroidRunner.paused.value) {
                        // Silence audio while paused
                        ButterscotchNative.suspendAudio()
                        this@ButterscotchDroidRunner.paused.first { !it } // Wait until we are NOT paused
                        // Reset the frame clock so resuming doesn't inject a giant delta_time spike
                        lastFrameStartNs = System.nanoTime()
                    }

                    ButterscotchNative.resumeAudio()

                    val freeCam = this@ButterscotchDroidRunner.freeCamera.value

                    if ((this@ButterscotchDroidRunner.enableWidescreenHack || freeCam.active) && this@ButterscotchDroidRunner.surfaceSize.width != 0 && this@ButterscotchDroidRunner.surfaceSize.height != 0) {
                        ButterscotchNative.setWidescreenHackAspectRatio(this@ButterscotchDroidRunner.surfaceSize.width / this@ButterscotchDroidRunner.surfaceSize.height.toFloat())
                    } else {
                        ButterscotchNative.setWidescreenHackAspectRatio(0.0f)
                    }

                    ButterscotchNative.setFreeCamera(freeCam.panX, freeCam.panY, freeCam.zoom)

                    val frameStartNs = System.nanoTime()
                    val deltaTimeSeconds = ((frameStartNs - lastFrameStartNs) / 1_000_000_000.0).toFloat()
                    lastFrameStartNs = frameStartNs

                    // Size the host framebuffer to the game (so the runner fills it edge to edge, no native letterboxing) before stepAndDraw blits into it
                    computeRenderTarget()
                    ensureFramebufferSize()

                    ButterscotchNative.beginFrame()
                    drainPendingInput()
                    val stepStatus = ButterscotchNative.stepAndDraw(renderW, renderH, deltaTimeSeconds)

                    when (stepStatus) {
                        ButterscotchNative.BUTTERSCOTCH_DROID_CONTINUE,
                        ButterscotchNative.BUTTERSCOTCH_DROID_CONTINUE_NO_SWAP -> {
                            if (stepStatus == ButterscotchNative.BUTTERSCOTCH_DROID_CONTINUE) {
                                // Blit the runner's framebuffer to the screen through the CRT post-processing pass
                                GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
                                GLES20.glViewport(0, 0, egl.width, egl.height)

                                // The runner enables blending and scissor once and expects that global state to persist across frames, so save what we toggle here and restore it after the blit, otherwise transparent sprites break
                                val blendWasEnabled = GLES20.glIsEnabled(GLES20.GL_BLEND)
                                val scissorWasEnabled = GLES20.glIsEnabled(GLES20.GL_SCISSOR_TEST)
                                GLES20.glDisable(GLES20.GL_BLEND)
                                GLES20.glDisable(GLES20.GL_SCISSOR_TEST)

                                // Clear the whole surface to black, then draw the game into its letterbox rect so post-processing only touches the game pixels
                                GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
                                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                                GLES20.glViewport(renderX, renderY, renderW, renderH)

                                GLES30.glBindVertexArray(0)
                                when (postProcessing.shader) {
                                    GameEntry.PostProcessingShader.OFF -> blitShader.bind {
                                        uTexture.set(GLES20.GL_TEXTURE0, this@ButterscotchDroidRunner.blitTextureId)
                                    }
                                    GameEntry.PostProcessingShader.CRT -> crtShader.bind {
                                        uTexture.set(GLES20.GL_TEXTURE0, this@ButterscotchDroidRunner.blitTextureId)
                                        uResolution.set(renderW.toFloat(), renderH.toFloat())
                                        val crt = postProcessing.crt
                                        uCurvature.set(crt.curvature.toFloat())
                                        uAberration.set(crt.aberration.toFloat())
                                        uHalation.set(crt.halation.toFloat())
                                        uScanlines.set(crt.scanlines.toFloat())
                                        uMask.set(crt.mask.toFloat())
                                        uVignette.set(crt.vignette.toFloat())
                                    }
                                }
                                GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)

                                if (blendWasEnabled) GLES20.glEnable(GLES20.GL_BLEND)
                                if (scissorWasEnabled) GLES20.glEnable(GLES20.GL_SCISSOR_TEST)

                                egl.swapBuffers()
                            }

                            val hz = (ButterscotchNative.getTargetFrameHz() * this@ButterscotchDroidRunner.fastForwardSpeed).toLong()
                            if (hz > 0) {
                                val targetNs = 1_000_000_000L / hz
                                // lastFrameStartNs is this frame's start (set above), so we pace one period from there
                                val nextDeadline = lastFrameStartNs + targetNs
                                val remaining = nextDeadline - System.nanoTime()
                                if (remaining > 2_000_000L) {
                                    Thread.sleep((remaining - 1_000_000L) / 1_000_000L)
                                }
                                while (System.nanoTime() < nextDeadline) {
                                    // spin spin spin!
                                }
                            }

                            // Yield so other tasks (like our clean up task) can be executed
                            // The reason for that is because we don't have any suspension points here on the loop, because Butterscotch sleeps the thread
                            // (probably my first time using yield tbh)
                            yield()
                        }

                        ButterscotchNative.BUTTERSCOTCH_DROID_SHOULD_EXIT -> {
                            // Game requested exit
                            requestExitInternal()
                            break
                        }
                        
                        else -> error("Unsupported return status! $stepStatus")
                    }
                }
            } finally {
                egl.unbindWindow()
                ButterscotchNative.suspendAudio()
            }
        }
    }

    // Blocks the main thread because we NEED to block it to avoid crashes because we need to release the old surface
    fun stopRenderLoop() {
        Log.i(TAG, "Stopping rendering loop...")
        runBlocking {
            withContext(glDispatcher) {
                renderJob?.cancelAndJoin()
                renderJob = null
            }
        }
    }

    fun requestExit() {
        Log.i(TAG, "Requesting exit!")

        runBlocking {
            withContext(glDispatcher) {
                renderJob?.cancelAndJoin()

                requestExitInternal()
            }
        }
    }

    fun setGameSurfaceSize(surfaceSize: IntSize) {
        runBlocking {
            withContext(glDispatcher) {
                this@ButterscotchDroidRunner.surfaceSize = surfaceSize
            }
        }
    }

    // Should ONLY be called in the render thread!
    fun requestExitInternal() {
        ButterscotchNative.stopRunner()
        egl.teardown()
        ButterscotchNative.markExited()

        renderJob = null
        started = false
        runnerStarted = false
        stdioCallback?.let { ButterscotchNative.unregisterStdioListener(it) }
    }

    sealed interface InputEvent {
        data class Key(val code: Int, val isDown: Boolean) : InputEvent
        data class Char(val codePoint: Int) : InputEvent
        data class MousePosition(val x: Float, val y: Float) : InputEvent
        data class MouseButton(val button: GmlMouseButton, val isDown: Boolean) : InputEvent
        data class GamepadButton(val device: Int, val button: Int, val isDown: Boolean) : InputEvent
        data class GamepadAxis(val device: Int, val axis: Int, val value: Float) : InputEvent
        data class GamepadConnected(val device: Int, val name: String?) : InputEvent
        data class GamepadDisconnected(val device: Int) : InputEvent
    }

    /**
     * Queue a key event for the runner.
     *
     * Safe to use from the UI thread because this only queues the input.
     */
    fun onKey(keyCode: Int, isDown: Boolean) {
        inputChannel.trySend(InputEvent.Key(keyCode, isDown))
    }

    /**
     * Queue a typed character (Unicode codepoint) for the runner, backing keyboard_lastchar.
     *
     * Safe to use from the UI thread because this only queues the input.
     */
    fun onChar(codePoint: Int) {
        inputChannel.trySend(InputEvent.Char(codePoint))
    }

    // Gamepad queueing. All safe to call from the UI thread (just enqueues into the Channel). The
    // render thread drains them in drainPendingInput and forwards to the native gamepad feed. The
    // Channel preserves ordering, so a Connected enqueued before its buttons is applied first.

    fun onGamepadButton(device: Int, button: Int, isDown: Boolean) {
        inputChannel.trySend(InputEvent.GamepadButton(device, button, isDown))
    }

    fun onGamepadAxis(device: Int, axis: Int, value: Float) {
        inputChannel.trySend(InputEvent.GamepadAxis(device, axis, value))
    }

    fun onGamepadConnected(device: Int, name: String?) {
        inputChannel.trySend(InputEvent.GamepadConnected(device, name))
    }

    fun onGamepadDisconnected(device: Int) {
        inputChannel.trySend(InputEvent.GamepadDisconnected(device))
    }

    fun onMouseMove(x: Float, y: Float) {
        inputChannel.trySend(InputEvent.MousePosition(x, y))
    }

    fun onMouseButton(button: GmlMouseButton, isDown: Boolean) {
        inputChannel.trySend(InputEvent.MouseButton(button, isDown))
    }

    /**
     * Drains all queued inputs and forwards it to the runner.
     *
     * Should ONLY be called from the render thread!
     */
    fun drainPendingInput() {
        while (true) {
            val event = inputChannel.tryReceive().getOrNull() ?: break
            when (event) {
                is InputEvent.Key -> if (event.isDown) ButterscotchNative.onKeyDown(event.code) else ButterscotchNative.onKeyUp(event.code)
                is InputEvent.Char -> ButterscotchNative.onCharacter(event.codePoint)
                is InputEvent.GamepadButton -> ButterscotchNative.gamepadButton(event.device, event.button, event.isDown)
                is InputEvent.GamepadAxis -> ButterscotchNative.gamepadAxis(event.device, event.axis, event.value)
                is InputEvent.GamepadConnected -> ButterscotchNative.gamepadConnected(event.device, event.name)
                is InputEvent.GamepadDisconnected -> ButterscotchNative.gamepadDisconnected(event.device)
                is InputEvent.MousePosition -> {
                    // event.x/y are fractions of the full surface; remap them into the game's letterbox rect
                    val gameX = if (renderW > 0) (event.x * egl.width - renderX) / renderW else event.x
                    val gameY = if (renderH > 0) (event.y * egl.height - renderY) / renderH else event.y
                    ButterscotchNative.setNormalizedCursorPosition(gameX, gameY)
                }
                is InputEvent.MouseButton -> ButterscotchNative.setMouseButtonState(event.button.id, event.isDown)
            }
        }
    }

    fun prepare() {
        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        blitTextureId = textures[0]
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, blitTextureId)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)

        val framebuffers = IntArray(1)
        GLES20.glGenFramebuffers(1, framebuffers, 0)
        fboId = framebuffers[0]
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, fboId)
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, blitTextureId, 0)

        // Allocate the color attachment storage at the current render size
        computeRenderTarget()
        ensureFramebufferSize()

        val status = GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER)
        check(status == GLES20.GL_FRAMEBUFFER_COMPLETE) { "Framebuffer is not complete! Status is $status" }

        // blit.vsh is just a fullscreen triangle emitting vTexCoord, so the CRT pass reuses it as is
        val crtVertexShader = assets.open("shaders/blit.vsh").readBytes().toString(Charsets.UTF_8)
        val crtFragmentShader = assets.open("shaders/crt.fsh").readBytes().toString(Charsets.UTF_8)
        crtShader = shaderManager.loadShader(crtVertexShader, crtFragmentShader) { CrtShader(it) }

        // The plain passthrough blit, used when post-processing is set to Off
        val blitFragmentShader = assets.open("shaders/blit.fsh").readBytes().toString(Charsets.UTF_8)
        blitShader = shaderManager.loadShader(crtVertexShader, blitFragmentShader) { BlitShader(it) }
    }

    // Pick the game's render size and on-screen letterbox rect. The host framebuffer is sized to the render size and the runner fills it edge to edge, so the letterboxing happens later in our own present blit instead of inside the runner.
    private fun computeRenderTarget() {
        val surfaceW = egl.width
        val surfaceH = egl.height
        val gameSize = ButterscotchNative.currentGameSize

        // Widescreen hack and free camera want the full surface; before the game reports its size we have no aspect to fit, so also fall back to the full surface
        if (enableWidescreenHack || freeCamera.value.active || gameSize == null || 0 >= gameSize.width || 0 >= gameSize.height) {
            renderW = surfaceW
            renderH = surfaceH
            renderX = 0
            renderY = 0
            return
        }

        val gameW = gameSize.width
        val gameH = gameSize.height
        if (surfaceW > gameW * surfaceH / gameH) {
            renderW = (gameW * surfaceH / gameH).coerceAtLeast(1)
            renderH = surfaceH
        } else {
            renderW = surfaceW
            renderH = (gameH * surfaceW / gameW).coerceAtLeast(1)
        }
        renderX = (surfaceW - renderW) / 2
        renderY = (surfaceH - renderH) / 2
    }

    // Reallocate the host framebuffer color texture when the render size changes, otherwise the runner blits into a stale sized texture after a rotation or a game size change
    // The texture id and the framebuffer attachment stay valid across the reallocation, so nothing needs re-attaching
    private fun ensureFramebufferSize() {
        if (renderW == fboWidth && renderH == fboHeight)
            return

        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, blitTextureId)
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, renderW, renderH, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null)
        fboWidth = renderW
        fboHeight = renderH
    }
}
