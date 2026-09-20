package net.perfectdreams.butterscotch.android

import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.unit.IntSize
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import java.io.FileInputStream

/**
 * Flat JNI surface — direct mirror of [src/web/main.c]'s shape. Each function does one thing and
 * returns; the loop, the lifecycle, EGL, frame pacing, and the input queue all live in
 * [ButterscotchDroidRunner] / this object on the JVM side.
 *
 * Threading contract: the render-side externals ([startRunner], [beginFrame], [onKeyDown],
 * [onKeyUp], [stepAndDraw], [stopRunner]) MUST be called from the same OS thread that owns the
 * EGL context (the one [ButterscotchEgl] was bound on). [ButterscotchDroidRunner] is the only
 * thing that should call these.
 *
 * The exception is [onKey], which is safe to call from any thread — it does no JNI work itself,
 * just pushes into a [Channel] that the render thread drains on each frame.
 *
 * Native -> Kotlin push notifications ([onTitleChanged], [onGameSizeChanged]) come in via
 * @JvmStatic methods that the C side invokes through cached jmethodIDs. They fire from the render
 * thread; Compose's snapshot system handles the cross-thread state write safely.
 */
object ButterscotchNative {
    const val BUTTERSCOTCH_DROID_CONTINUE = 0
    const val BUTTERSCOTCH_DROID_SHOULD_EXIT = 1
    const val BUTTERSCOTCH_DROID_CONTINUE_NO_SWAP = 2
    val stdioListener = mutableListOf<(String) -> (Unit)>()

    init {
        System.loadLibrary("butterscotch")
        redirectStdioToLogcat()
        init()
    }

    external fun init()

    /**
     * Registers a stdio listener
     */
    fun registerStdioListener(callback: (String) -> (Unit)): (String) -> Unit {
        stdioListener.add(callback)
        return callback
    }

    /**
     * Unregister a stdio listener
     */
    fun unregisterStdioListener(callback: (String) -> (Unit)) {
        stdioListener.remove(callback)
    }

    /**
     * Points the native fds 1/2 (stdout/stderr) at a pipe and pumps it into logcat from a daemon thread.
     *
     * dup2 retargets the fds themselves, so this captures printf/fprintf output from the C runtime, not just JVM writes.
     *
     * The matching setvbuf calls live in the native [init], because stdio buffering is libc FILE* state that cannot be reached from the fd level.
     *
     * The pump thread must outlive every native writer: if it died, the next printf after the 64KB pipe buffer fills would block the render thread forever.
     * The reader loop only exits on EOF, which never happens since the write end stays open for the life of the process.
     */
    private fun redirectStdioToLogcat() {
        try {
            val pipe = Os.pipe()
            Os.dup2(pipe[1], OsConstants.STDOUT_FILENO)
            Os.dup2(pipe[1], OsConstants.STDERR_FILENO)
            Thread {
                FileInputStream(pipe[0])
                    .bufferedReader()
                    .forEachLine {
                        Log.i("Butterscotch", it)
                        for (listener in stdioListener) {
                            listener.invoke(it)
                        }
                    }
            }.apply {
                name = "ButterscotchLogPump"
                isDaemon = true
                start()
            }
        } catch (e: ErrnoException) {
            Log.w("Butterscotch", "Could not redirect stdio to logcat", e)
        }
    }

    // ===[ DataWin handle API — safe to call from any thread, no EGL needed ]===
    //
    // Parses a WAD file into an opaque native handle. Callers own the handle and MUST call
    // [dataWinFree] exactly once. Today this only parses GEN8 + STRG (enough for the importer);
    // if a fuller parse is needed later, expose a separate entry point rather than overloading
    // this one.
    //
    // Prefer the [ParsedDataWin] wrapper below over calling these externals directly — it handles
    // freeing for you via [AutoCloseable].

    /** Returns 0 on parse failure. */
    external fun dataWinParseLight(wadPath: String): Long
    external fun dataWinFree(handle: Long)
    external fun dataWinName(handle: Long): String?
    external fun dataWinDisplayName(handle: Long): String?
    /** Returns -1 if the handle is 0. */
    external fun dataWinWadVersion(handle: Long): Int
    external fun dataWinGmsVersion(handle: Long): String
    external fun dataWinDetectedGmsVersion(handle: Long): String
    external fun getRunningDataWinHandle(): Long
    external fun getRunnerFrameCount(): Long
    external fun getProfilerStartedAtFrame(): Long
    external fun isProfilerEnabled(): Boolean
    external fun setProfilerEnabled(enabled: Boolean)
    external fun getProfilerEntriesCount(): Long
    external fun getProfilerEntryKey(index: Long): String
    external fun getProfilerEntryNanos(index: Long): Long
    external fun getProfilerEntryOps(index: Long): Long

    // ===[ Render-side JNI — all must run on the EGL-owning thread ]===

    /**
     * Parse data.win, build VM/renderer/audio, fire first room. Requires a current EGL context.
     *
     * [osType] is the value reported to GML's os_type / os_* builtins (a [GameEntry.RunnerOs.nativeValue]).
     * The C side stashes it in a static and reuses it across game_change rebuilds.
     */
    external fun startRunner(dataWinPath: String, savesPath: String, osType: Int, hostFramebuffer: Int): Boolean

    /**
     * Clear the runner's "key pressed this frame" state. Must be called at the top of each frame,
     * before draining input via [drainPendingInput], so the GameMaker `keyboard_check_pressed`
     * flag is computed correctly.
     */
    external fun beginFrame()

    /**
     * Forward a key-down to the runner directly (no queue). Render thread only — call between
     * [beginFrame] and [stepAndDraw]. Use [onKey] if you're not on the render thread.
     */
    external fun onKeyDown(keyCode: Int)

    /** Forward a key-up to the runner directly. See [onKeyDown] for threading rules. */
    external fun onKeyUp(keyCode: Int)

    /**
     * Forward a typed character (Unicode codepoint, layout + shift already applied) to the runner,
     * backing keyboard_lastchar. Render thread only, same threading rules as [onKeyDown]. This is
     * separate from [onKeyDown]: a key press produces both a key edge and (if it maps to a glyph) a
     * character, just like GLFW's separate key/char callbacks.
     */
    external fun onCharacter(codePoint: Int)

    // ===[ Gamepad feed — render thread only, same rules as onKeyDown ]===
    //
    // Event-driven feed into the native RunnerGamepad subsystem. The host translates Android
    // KeyEvent / MotionEvent into canonical slot indices and pushes them through the same input
    // [Channel] as keys (see ButterscotchDroidRunner.onGamepad*); the render thread drains them
    // between [beginFrame] and [stepAndDraw] and calls these. Never call these off the render thread.
    //
    // [button] is the canonical RunnerGamepad slot index (0 = face1/A .. 16 = home), [axis] is
    // 0 = LH, 1 = LV, 2 = RH, 3 = RV, [value] is a raw normalized axis reading in [-1, 1] (the
    // runner applies its own deadzone on read). Button/axis events auto-connect their slot on the C
    // side, so the feed survives the runner rebuild that game_change performs.

    /** Mark a controller present in [device] (0-based slot). [name] is shown via GML gamepad_get_description. */
    external fun gamepadConnected(device: Int, name: String?)

    /** Mark the controller in [device] gone and clear its held state. */
    external fun gamepadDisconnected(device: Int)

    /** Forward a gamepad button edge. [button] is a canonical slot index (0..16). */
    external fun gamepadButton(device: Int, button: Int, isDown: Boolean)

    /** Forward a raw analog axis value in [-1, 1]. [axis] is 0..3 (LH, LV, RH, RV). */
    external fun gamepadAxis(device: Int, axis: Int, value: Float)

    /**
     * Audio update + `Runner_step` + draw sequence. Returns false when the runner has asked to
     * exit. The caller is responsible for `eglSwapBuffers` after this returns (see
     * [ButterscotchEgl.swapBuffers]) and for pacing the loop (see [getTargetFrameHz]).
     *
     * [winW]/[winH] are the current EGL window surface dimensions. [deltaTimeSeconds] is the time
     * since the previous frame in seconds. The native side stamps it into the runner so it drives
     * both the GML delta_time builtin and the audio system.
     */
    external fun stepAndDraw(winW: Int, winH: Int, deltaTimeSeconds: Float): Int

    /**
     * The current room's tick rate in Hz (GM:S `room_speed`). Returns 0 if no room is active yet.
     * The render loop uses this to decide how long to sleep between frames.
     */
    external fun getTargetFrameHz(): Int

    /** Tear down runner/renderer/audio. */
    external fun stopRunner()

    /**
     * Suspend the audio backend (stops the audio device) without tearing the runner down. Used when the
     * app is backgrounded: the audio device mixes on its own thread, so parking the render loop alone
     * would not silence it. Render thread only, same threading rules as [stopRunner].
     */
    external fun suspendAudio()

    /** Resume the audio backend after [suspendAudio]. Render thread only. */
    external fun resumeAudio()

    external fun getRoomCount(): Int
    external fun getRoomName(roomIndex: Int): String
    external fun gotoRoom(roomIndex: Int)
    external fun setWidescreenHackAspectRatio(aspectRatio: Float)
    external fun setNormalizedCursorPosition(x: Float, y: Float)
    external fun setMouseButtonState(button: Int, down: Boolean)

    /**
     * Set the visual-only free camera (photo mode). [panX]/[panY] are fractions of the (zoomed) view,
     * [zoom] is a magnification multiplier (1.0 = identity, > 1.0 = zoom in). Render thread only, same
     * threading rules as the other render-side externals. This only affects the projection matrix, so
     * game logic, collisions and camera-follow are untouched.
     */
    external fun setFreeCamera(panX: Float, panY: Float, zoom: Float)

    // ===[ Native -> Kotlin push state ]===

    var currentTitle: String? by mutableStateOf(null)
        private set

    @JvmStatic
    fun onTitleChanged(title: String) {
        currentTitle = title
    }

    var currentGameSize: IntSize? by mutableStateOf(null)
        private set

    @JvmStatic
    fun onGameSizeChanged(width: Int, height: Int) {
        currentGameSize = IntSize(width, height)
    }

    /**
     * Flips true when the runner has exited (either the game requested quit, or [ButterscotchDroidRunner] tore
     * it down on user request). The Activity observes this and calls finish().
     */
    var hasExited: Boolean by mutableStateOf(false)
        private set

    internal fun markExited() {
        hasExited = true
    }

    /** Clear the exit latch — process-singleton state, so a previous session would otherwise
     *  immediately finish a freshly-launched GameActivity. */
    fun resetExitLatch() {
        hasExited = false
    }
}
