package net.perfectdreams.butterscotch.android.pe

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Decode a Windows .ico file into a [Bitmap]. Android's BitmapFactory doesn't speak ICO, so we
 * parse the directory ourselves and handle the two encodings used inside icon frames:
 *   - PNG payload (Vista+ large icons) — handed straight to BitmapFactory.
 *   - DIB payload (the original ICO encoding) — a BITMAPINFOHEADER with no BMP file header,
 *     followed by pixel data and a 1bpp AND mask. We decode the common bit-depths (1/4/8/24/32)
 *     manually because the doubled height field trips up generic BMP decoders.
 */
object IcoDecoder {

    /** Decode the largest frame of an ICO file. Returns null if [bytes] isn't a usable ICO. */
    fun decodeLargest(bytes: ByteArray): Bitmap? {
        if (bytes.size < 6) return null
        val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (buf.getShort(0).toInt() != 0) return null
        if (buf.getShort(2).toInt() != 1) return null     // type 1 = icon
        val count = buf.getShort(4).toInt() and 0xFFFF
        if (count == 0) return null

        data class Entry(val width: Int, val height: Int, val bitCount: Int, val offset: Int, val size: Int)
        val entries = (0 until count).map { i ->
            val e = 6 + i * 16
            val w = (bytes[e].toInt() and 0xFF).let { if (it == 0) 256 else it }
            val h = (bytes[e + 1].toInt() and 0xFF).let { if (it == 0) 256 else it }
            val bitCount = buf.getShort(e + 6).toInt() and 0xFFFF
            val size = buf.getInt(e + 8)
            val off = buf.getInt(e + 12)
            Entry(w, h, bitCount, off, size)
        }
        // Largest pixel area, ties broken by higher bit depth.
        val best = entries.maxWithOrNull(
            compareBy<Entry> { it.width * it.height }.thenBy { it.bitCount }
        ) ?: return null

        if (best.offset < 0 || best.offset + best.size > bytes.size) return null
        val payload = bytes.copyOfRange(best.offset, best.offset + best.size)
        return decodeFrame(payload)
    }

    private val PNG_SIG = byteArrayOf(
        0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    )

    private fun decodeFrame(payload: ByteArray): Bitmap? {
        if (payload.size >= 8 && payload.copyOfRange(0, 8).contentEquals(PNG_SIG)) {
            return BitmapFactory.decodeByteArray(payload, 0, payload.size)
        }
        return decodeDib(payload)
    }

    /**
     * Decode a DIB frame from an ICO entry. The DIB consists of:
     *   BITMAPINFOHEADER (40 bytes, biHeight is 2× the real height — XOR mask + AND mask),
     *   optional color table (for ≤8bpp), XOR pixel data, then AND mask (1bpp).
     */
    private fun decodeDib(data: ByteArray): Bitmap? {
        if (data.size < 40) return null
        val buf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val headerSize = buf.getInt(0)
        if (headerSize < 40 || headerSize > data.size) return null
        val width = buf.getInt(4)
        val rawHeight = buf.getInt(8)
        val bitCount = buf.getShort(14).toInt() and 0xFFFF
        val compression = buf.getInt(16)
        if (compression != 0) return null  // BI_RGB only — no BI_BITFIELDS/JPEG/PNG-in-DIB
        val height = rawHeight / 2
        if (width <= 0 || height <= 0) return null

        var clrUsed = buf.getInt(32)
        if (clrUsed == 0 && bitCount <= 8) clrUsed = 1 shl bitCount

        val paletteStart = headerSize
        val paletteBytes = if (bitCount <= 8) clrUsed * 4 else 0
        val pixelsStart = paletteStart + paletteBytes
        if (pixelsStart > data.size) return null

        // Read palette (BGRA in file → ARGB in memory). Alpha byte in ICO palettes is unused.
        val palette = IntArray(if (bitCount <= 8) clrUsed else 0)
        for (i in palette.indices) {
            val p = paletteStart + i * 4
            val b = data[p].toInt() and 0xFF
            val g = data[p + 1].toInt() and 0xFF
            val r = data[p + 2].toInt() and 0xFF
            palette[i] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
        }

        val xorRowBytes = ((bitCount * width + 31) / 32) * 4
        val andRowBytes = ((width + 31) / 32) * 4
        val xorSize = xorRowBytes * height
        val andSize = andRowBytes * height
        if (pixelsStart + xorSize > data.size) return null
        val andOffset = pixelsStart + xorSize
        val hasAndMask = andOffset + andSize <= data.size

        val pixels = IntArray(width * height)

        // DIB is bottom-up: file row 0 is the visual bottom row.
        for (y in 0 until height) {
            val visualRow = height - 1 - y
            val xorBase = pixelsStart + y * xorRowBytes
            val andBase = andOffset + y * andRowBytes
            for (x in 0 until width) {
                val argb = when (bitCount) {
                    32 -> {
                        val o = xorBase + x * 4
                        val b = data[o].toInt() and 0xFF
                        val g = data[o + 1].toInt() and 0xFF
                        val r = data[o + 2].toInt() and 0xFF
                        val a = data[o + 3].toInt() and 0xFF
                        (a shl 24) or (r shl 16) or (g shl 8) or b
                    }
                    24 -> {
                        val o = xorBase + x * 3
                        val b = data[o].toInt() and 0xFF
                        val g = data[o + 1].toInt() and 0xFF
                        val r = data[o + 2].toInt() and 0xFF
                        (0xFF shl 24) or (r shl 16) or (g shl 8) or b
                    }
                    8 -> palette.getOrElse(data[xorBase + x].toInt() and 0xFF) { 0 }
                    4 -> {
                        val byte = data[xorBase + x / 2].toInt() and 0xFF
                        val idx = if (x % 2 == 0) (byte shr 4) and 0x0F else byte and 0x0F
                        palette.getOrElse(idx) { 0 }
                    }
                    1 -> {
                        val byte = data[xorBase + x / 8].toInt() and 0xFF
                        val idx = (byte shr (7 - x % 8)) and 0x01
                        palette.getOrElse(idx) { 0 }
                    }
                    else -> return null
                }

                // Apply AND mask only when the XOR pixel doesn't already carry alpha (32bpp does).
                val finalArgb = if (hasAndMask && bitCount != 32) {
                    val byte = data[andBase + x / 8].toInt() and 0xFF
                    val maskBit = (byte shr (7 - x % 8)) and 0x01
                    if (maskBit == 1) 0 else argb
                } else argb

                pixels[visualRow * width + x] = finalArgb
            }
        }

        return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
    }
}
