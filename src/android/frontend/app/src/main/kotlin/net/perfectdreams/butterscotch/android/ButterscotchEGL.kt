package net.perfectdreams.butterscotch.android

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.util.Log
import android.view.Surface

/**
 * Self-managed EGL, we self-manage the EGL context because we want to persist the GL context through surface creation/destroy.
 *
 * The GL context is per-thread, so all calls MUST be on the same thread!
 */
class ButterscotchEGL {
    companion object {
        private const val TAG = "ButterscotchEGL"
    }

    private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var config: EGLConfig? = null
    private var context: EGLContext = EGL14.EGL_NO_CONTEXT
    private var surface: EGLSurface = EGL14.EGL_NO_SURFACE

    var width: Int = 1
        private set
    var height: Int = 1
        private set

    val hasSurface
        get() = surface != EGL14.EGL_NO_SURFACE

    /**
     * Update the cached surface size after a [android.view.SurfaceHolder] size change (example: a `setFixedSize` taking effect, or a rotation).
     */
    fun updateSize(newWidth: Int, newHeight: Int) {
        if (newWidth > 0) width = newWidth
        if (newHeight > 0) height = newHeight
    }

    private fun initOnce(): Boolean {
        if (context != EGL14.EGL_NO_CONTEXT)
            return true

        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (display == EGL14.EGL_NO_DISPLAY) {
            Log.e(TAG, "eglGetDisplay failed")
            return false
        }
        val version = IntArray(2)
        if (!EGL14.eglInitialize(display, version, 0, version, 1)) {
            Log.e(TAG, "eglInitialize failed: 0x${EGL14.eglGetError().toString(16)}")
            return false
        }

        val cfgAttribs = intArrayOf(
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT,
            EGL14.EGL_RENDERABLE_TYPE, 0x0040, // EGL_OPENGL_ES3_BIT
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,
            EGL14.EGL_DEPTH_SIZE, 0,
            EGL14.EGL_STENCIL_SIZE, 0,
            EGL14.EGL_NONE
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val nConfigs = IntArray(1)
        if (!EGL14.eglChooseConfig(display, cfgAttribs, 0, configs, 0, 1, nConfigs, 0) || nConfigs[0] < 1) {
            Log.e(TAG, "eglChooseConfig failed: 0x${EGL14.eglGetError().toString(16)}")
            return false
        }
        this.config = configs[0]

        val ctxAttribs = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE)
        this.context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, ctxAttribs, 0)
        if (this.context == EGL14.EGL_NO_CONTEXT) {
            Log.e(TAG, "eglCreateContext failed: 0x${EGL14.eglGetError().toString(16)}")
            return false
        }
        return true
    }

    /**
     * Create an EGL window surface for [androidSurface] and make the context current on the calling thread.
     *
     * From here on, all render-side native calls must come from this thread.
     */
    fun bindWindow(androidSurface: Surface): Boolean {
        if (!initOnce())
            return false

        require(!this.hasSurface) { "Trying to bind a window while we already have a surface! Did you forget to call unbindWindow? $surface" }

        val surfaceAttribs = intArrayOf(EGL14.EGL_NONE)
        this.surface = EGL14.eglCreateWindowSurface(display, config, androidSurface, surfaceAttribs, 0)
        if (this.surface == EGL14.EGL_NO_SURFACE) {
            Log.e(TAG, "eglCreateWindowSurface failed: 0x${EGL14.eglGetError().toString(16)}")
            return false
        }
        if (!EGL14.eglMakeCurrent(this.display, this.surface, this.surface, context)) {
            Log.e(TAG, "eglMakeCurrent failed: 0x${EGL14.eglGetError().toString(16)}")
            return false
        }

        val w = IntArray(1)
        val h = IntArray(1)
        EGL14.eglQuerySurface(this.display, this.surface, EGL14.EGL_WIDTH, w, 0)
        EGL14.eglQuerySurface(this.display, this.surface, EGL14.EGL_HEIGHT, h, 0)
        this.width = w[0].coerceAtLeast(1)
        this.height = h[0].coerceAtLeast(1)
        Log.i(TAG, "EGL surface bound: ${ this.width}x${ this.height}")
        return true
    }

    /** Destroy the EGL window surface. Context survives so GL state persists. */
    fun unbindWindow() {
        if (this.surface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglMakeCurrent(this.display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, this.context)
            EGL14.eglDestroySurface(this.display, this.surface)
            this.surface = EGL14.EGL_NO_SURFACE
        }
    }

    fun swapBuffers() {
        if (surface == EGL14.EGL_NO_SURFACE)
            return

        if (!EGL14.eglSwapBuffers(this.display, this.surface)) {
            val err = EGL14.eglGetError()
            if (err == EGL14.EGL_BAD_SURFACE || err == EGL14.EGL_BAD_NATIVE_WINDOW) {
                // This may happen if the surface was destroyed between the frame start and the swapBuffers call!
                // Don't worry about it, we'll unbind it later :3
                Log.w(TAG, "eglSwapBuffers reported surface gone (0x${err.toString(16)})")
            } else {
                Log.e(TAG, "eglSwapBuffers failed: 0x${err.toString(16)}")
            }
        }
    }

    /** Final teardown — destroys the EGL context and terminates the display. */
    fun teardown() {
        if (this.surface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglMakeCurrent(this.display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, this.context)
            EGL14.eglDestroySurface(this.display, this.surface)
            this.surface = EGL14.EGL_NO_SURFACE
        }
        if (this.context != EGL14.EGL_NO_CONTEXT) {
            EGL14.eglDestroyContext(this.display, this.context)
            this. context = EGL14.EGL_NO_CONTEXT
        }
        if (this.display != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglTerminate(this.display)
            this.display = EGL14.EGL_NO_DISPLAY
        }
    }
}
