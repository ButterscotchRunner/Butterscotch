package net.perfectdreams.butterscotch.android.input

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import kotlin.math.roundToInt

// Fires short vibration pulses for on-screen control presses. Safe to call from the UI thread
class Haptics(context: Context) {
    private val vibrator: Vibrator? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        (context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager)?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
    }

    // strength is a 0..100 percentage. Where the device exposes amplitude control we scale the
    // amplitude (1..255), otherwise we approximate strength by scaling the pulse duration instead
    fun tick(strength: Int) {
        val vibrator = vibrator ?: return
        if (!vibrator.hasVibrator() || strength <= 0) return
        val pct = strength.coerceIn(1, 100)
        when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.O -> {
                if (vibrator.hasAmplitudeControl()) {
                    val amplitude = (pct / 100f * 255f).roundToInt().coerceIn(1, 255)
                    vibrator.vibrate(VibrationEffect.createOneShot(18, amplitude))
                } else {
                    vibrator.vibrate(VibrationEffect.createOneShot(durationFor(pct), VibrationEffect.DEFAULT_AMPLITUDE))
                }
            }
            else -> {
                @Suppress("DEPRECATION")
                vibrator.vibrate(durationFor(pct))
            }
        }
    }

    // No amplitude control, so spread strength across a 8..40ms pulse instead
    private fun durationFor(pct: Int): Long = (8 + (pct / 100f) * 32f).toLong()
}
