package net.perfectdreams.butterscotch.android

import androidx.compose.ui.graphics.Color
import kotlin.math.cbrt
import kotlin.math.floor
import kotlin.math.pow

object ColorUtils {
    private fun s2l(c: Float) = if (c <= 0.04045f) c / 12.92f else ((c + 0.055f) / 1.055f).pow(2.4f)
    private fun l2s(c: Float) = if (c <= 0.0031308f) 12.92f * c else 1.055f * c.pow(1f / 2.4f) - 0.055f

    private fun Color.toOklab(): FloatArray {
        val lr = s2l(this.red)
        val lg = s2l(this.green)
        val lb = s2l(this.blue)
        val l = cbrt(0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb)
        val m = cbrt(0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb)
        val s = cbrt(0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb)
        return floatArrayOf(
            0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
        )
    }

    private fun oklabToColor(L: Float, a: Float, b: Float): Color {
        val l_ = L + 0.3963377774f * a + 0.2158037573f * b
        val m_ = L - 0.1055613458f * a - 0.0638541728f * b
        val s_ = L - 0.0894841775f * a - 1.2914855480f * b
        val l = l_ * l_ * l_; val m = m_ * m_ * m_; val s = s_ * s_ * s_
        val r =  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s
        val g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s
        val bl = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
        return Color(l2s(r.coerceIn(0f, 1f)), l2s(g.coerceIn(0f, 1f)), l2s(bl.coerceIn(0f, 1f)))
    }

    fun lerp(c1: Color, c2: Color, t: Float): Color {
        val a = c1.toOklab()
        val b = c2.toOklab()
        return oklabToColor(
            a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t,
        )
    }

    /**
     * Converts the components of a color, as specified by the default RGB
     * model, to an equivalent set of values for hue, saturation, and
     * brightness that are the three components of the HSB model.
     *
     *
     * If the `hsbvals` argument is `null`, then a
     * new array is allocated to return the result. Otherwise, the method
     * returns the array `hsbvals`, with the values put into
     * that array.
     * @param     r   the red component of the color
     * @param     g   the green component of the color
     * @param     b   the blue component of the color
     * @param     hsbvals  the array used to return the
     * three HSB values, or `null`
     * @return    an array of three elements containing the hue, saturation,
     * and brightness (in that order), of the color with
     * the indicated red, green, and blue components.
     * @see java.awt.Color.getRGB
     * @see java.awt.Color.Color
     * @see java.awt.image.ColorModel.getRGBdefault
     * @since     1.0
     */
    // From JDK
    fun RGBtoHSB(r: Int, g: Int, b: Int, hsbvals: FloatArray?): FloatArray {
        var hsbvals = hsbvals
        var hue: Float
        val saturation: Float
        val brightness: Float
        if (hsbvals == null) {
            hsbvals = FloatArray(3)
        }
        var cmax = if (r > g) r else g
        if (b > cmax) cmax = b
        var cmin = if (r < g) r else g
        if (b < cmin) cmin = b
        brightness = cmax.toFloat() / 255.0f
        saturation = if (cmax != 0) (cmax - cmin).toFloat() / cmax.toFloat() else 0f
        if (saturation == 0f) hue = 0f else {
            val redc = (cmax - r).toFloat() / (cmax - cmin).toFloat()
            val greenc = (cmax - g).toFloat() / (cmax - cmin).toFloat()
            val bluec = (cmax - b).toFloat() / (cmax - cmin).toFloat()
            hue = if (r == cmax) bluec - greenc else if (g == cmax) 2.0f + redc - bluec else 4.0f + greenc - redc
            hue /= 6.0f
            if (hue < 0) hue += 1.0f
        }
        hsbvals[0] = hue
        hsbvals[1] = saturation
        hsbvals[2] = brightness
        return hsbvals
    }

    /**
     * Converts the components of a color, as specified by the HSB
     * model, to an equivalent set of values for the default RGB model.
     *
     *
     * The `saturation` and `brightness` components
     * should be floating-point values between zero and one
     * (numbers in the range 0.0-1.0).  The `hue` component
     * can be any floating-point number.  The floor of this number is
     * subtracted from it to create a fraction between 0 and 1.  This
     * fractional number is then multiplied by 360 to produce the hue
     * angle in the HSB color model.
     *
     *
     * The integer that is returned by `HSBtoRGB` encodes the
     * value of a color in bits 0-23 of an integer value that is the same
     * format used by the method [getRGB][.getRGB].
     * This integer can be supplied as an argument to the
     * `Color` constructor that takes a single integer argument.
     * @param     hue   the hue component of the color
     * @param     saturation   the saturation of the color
     * @param     brightness   the brightness of the color
     * @return    the RGB value of the color with the indicated hue,
     * saturation, and brightness.
     * @see java.awt.Color.getRGB
     * @see java.awt.Color.Color
     * @see java.awt.image.ColorModel.getRGBdefault
     * @since     1.0
     */
    // From JDK
    fun HSBtoRGB(hue: Float, saturation: Float, brightness: Float): Int {
        var r = 0
        var g = 0
        var b = 0
        if (saturation == 0f) {
            b = (brightness * 255.0f + 0.5f).toInt()
            g = b
            r = g
        } else {
            val h: Float = (hue - floor(hue.toDouble()).toFloat()) * 6.0f
            val f: Float = h - floor(h.toDouble()).toFloat()
            val p = brightness * (1.0f - saturation)
            val q = brightness * (1.0f - saturation * f)
            val t = brightness * (1.0f - saturation * (1.0f - f))
            when (h.toInt()) {
                0 -> {
                    r = (brightness * 255.0f + 0.5f).toInt()
                    g = (t * 255.0f + 0.5f).toInt()
                    b = (p * 255.0f + 0.5f).toInt()
                }

                1 -> {
                    r = (q * 255.0f + 0.5f).toInt()
                    g = (brightness * 255.0f + 0.5f).toInt()
                    b = (p * 255.0f + 0.5f).toInt()
                }

                2 -> {
                    r = (p * 255.0f + 0.5f).toInt()
                    g = (brightness * 255.0f + 0.5f).toInt()
                    b = (t * 255.0f + 0.5f).toInt()
                }

                3 -> {
                    r = (p * 255.0f + 0.5f).toInt()
                    g = (q * 255.0f + 0.5f).toInt()
                    b = (brightness * 255.0f + 0.5f).toInt()
                }

                4 -> {
                    r = (t * 255.0f + 0.5f).toInt()
                    g = (p * 255.0f + 0.5f).toInt()
                    b = (brightness * 255.0f + 0.5f).toInt()
                }

                5 -> {
                    r = (brightness * 255.0f + 0.5f).toInt()
                    g = (p * 255.0f + 0.5f).toInt()
                    b = (q * 255.0f + 0.5f).toInt()
                }
            }
        }
        return -0x1000000 or (r shl 16) or (g shl 8) or (b shl 0)
    }
}