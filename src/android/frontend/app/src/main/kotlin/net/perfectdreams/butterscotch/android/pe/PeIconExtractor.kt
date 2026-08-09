package net.perfectdreams.butterscotch.android.pe

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Reads icon resources out of a Windows PE (.exe / .dll) file and assembles them
 * into standalone .ico byte arrays.
 *
 * Pure JVM — no Android dependencies, no native code. Decode the resulting bytes
 * with BitmapFactory or your renderer of choice.
 */
object PeIconExtractor {
    private const val RT_ICON = 3
    private const val RT_GROUP_ICON = 14

    data class IconEntry(
        val width: Int,         // 0 means 256
        val height: Int,        // 0 means 256
        val colorCount: Int,
        val planes: Int,
        val bitCount: Int,
        val bytes: ByteArray,   // raw BMP-or-PNG payload as stored in RT_ICON
    )

    /** One .ico worth of images — one of these per RT_GROUP_ICON resource. */
    data class IconGroup(
        val groupId: Int,
        val images: List<IconEntry>,
    ) {
        /** Pack [images] into a standalone .ico file byte array. */
        fun toIcoBytes(): ByteArray {
            val headerSize = 6 + 16 * images.size
            val totalSize = headerSize + images.sumOf { it.bytes.size }
            val buf = ByteBuffer.allocate(totalSize).order(ByteOrder.LITTLE_ENDIAN)

            // ICONDIR
            buf.putShort(0)                      // reserved
            buf.putShort(1)                      // type = icon
            buf.putShort(images.size.toShort())

            // ICONDIRENTRY[]
            var dataOffset = headerSize
            for (img in images) {
                buf.put(img.width.toByte())
                buf.put(img.height.toByte())
                buf.put(img.colorCount.toByte())
                buf.put(0)                                 // reserved
                buf.putShort(img.planes.toShort())
                buf.putShort(img.bitCount.toShort())
                buf.putInt(img.bytes.size)
                buf.putInt(dataOffset)
                dataOffset += img.bytes.size
            }

            // Image payloads
            for (img in images) buf.put(img.bytes)
            return buf.array()
        }
    }

    fun extractFromFile(file: File): List<IconGroup> = extract(file.readBytes())

    /**
     * Parse [data] as a PE file and return every icon group it contains. Throws
     * [IllegalArgumentException] if [data] is not a recognizable PE.
     */
    fun extract(data: ByteArray): List<IconGroup> {
        val buf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)

        require(data.size >= 0x40 && data[0] == 'M'.code.toByte() && data[1] == 'Z'.code.toByte()) {
            "not a PE: missing MZ"
        }
        val peOff = buf.getInt(0x3C)
        require(peOff in 0..data.size - 24 &&
                data[peOff] == 'P'.code.toByte() && data[peOff + 1] == 'E'.code.toByte() &&
                data[peOff + 2] == 0.toByte() && data[peOff + 3] == 0.toByte()) {
            "not a PE: missing PE\\0\\0"
        }

        val coff = peOff + 4
        val numSections = buf.getShort(coff + 2).toInt() and 0xFFFF
        val optSize = buf.getShort(coff + 16).toInt() and 0xFFFF
        val opt = coff + 20
        val rsrcDirEntry = when (val magic = buf.getShort(opt).toInt() and 0xFFFF) {
            0x10b -> opt + 96     // PE32
            0x20b -> opt + 112    // PE32+
            else -> throw IllegalArgumentException("unknown optional header magic 0x${magic.toString(16)}")
        } + 2 * 8                  // DataDirectory[2] = resources

        val rsrcRva = buf.getInt(rsrcDirEntry)
        if (rsrcRva == 0) return emptyList()

        // Section table: each entry is 40 bytes
        val sectionsStart = opt + optSize
        data class Section(val vAddr: Int, val vSize: Int, val rAddr: Int, val rSize: Int)
        val sections = List(numSections) { i ->
            val s = sectionsStart + i * 40
            Section(
                vSize = buf.getInt(s + 8),
                vAddr = buf.getInt(s + 12),
                rSize = buf.getInt(s + 16),
                rAddr = buf.getInt(s + 20),
            )
        }

        fun rvaToOffset(rva: Int): Int? {
            for (s in sections) {
                val span = maxOf(s.vSize, s.rSize)
                if (rva in s.vAddr until s.vAddr + span) return s.rAddr + (rva - s.vAddr)
            }
            return null
        }

        val rsrcOff = rvaToOffset(rsrcRva) ?: return emptyList()

        // --- Resource directory walk ----------------------------------------
        // Each IMAGE_RESOURCE_DIRECTORY: 12-byte header + entries (8 bytes each).
        // High bit of an entry's "offset" field: 1 = subdirectory, 0 = data entry.
        fun readDir(off: Int): List<Pair<Int, Int>> {
            val named = buf.getShort(off + 12).toInt() and 0xFFFF
            val ided  = buf.getShort(off + 14).toInt() and 0xFFFF
            val total = named + ided
            return List(total) { i ->
                val e = off + 16 + i * 8
                buf.getInt(e) to buf.getInt(e + 4)
            }
        }

        fun readDataEntry(entryOff: Int): ByteArray {
            val rva = buf.getInt(entryOff)
            val size = buf.getInt(entryOff + 4)
            val fileOff = rvaToOffset(rva) ?: return ByteArray(0)
            return data.copyOfRange(fileOff, fileOff + size)
        }

        fun resourcesOfType(typeId: Int): Map<Int, ByteArray> {
            val out = LinkedHashMap<Int, ByteArray>()
            for ((nameOrId, child) in readDir(rsrcOff)) {
                if (child and 0x80000000.toInt() == 0) continue   // expect subdir
                if (nameOrId != typeId) continue
                val lvl2 = rsrcOff + (child and 0x7FFFFFFF)
                for ((rid, child2) in readDir(lvl2)) {
                    if (child2 and 0x80000000.toInt() == 0) continue
                    val lvl3 = rsrcOff + (child2 and 0x7FFFFFFF)
                    val langs = readDir(lvl3)
                    if (langs.isEmpty()) continue
                    val (_, dataEntryOff) = langs.first()
                    if (dataEntryOff and 0x80000000.toInt() != 0) continue  // must be leaf
                    out[rid] = readDataEntry(rsrcOff + (dataEntryOff and 0x7FFFFFFF))
                }
            }
            return out
        }

        val groups = resourcesOfType(RT_GROUP_ICON)
        if (groups.isEmpty()) return emptyList()
        val icons = resourcesOfType(RT_ICON)

        return groups.mapNotNull { (groupId, gdata) ->
            val gbuf = ByteBuffer.wrap(gdata).order(ByteOrder.LITTLE_ENDIAN)
            val type = gbuf.getShort(2).toInt() and 0xFFFF
            if (type != 1) return@mapNotNull null            // 1 = icon, 2 = cursor
            val count = gbuf.getShort(4).toInt() and 0xFFFF

            // GRPICONDIRENTRY is 14 bytes (one byte shorter than ICONDIRENTRY:
            // it stores a 2-byte resource ID instead of a 4-byte file offset).
            val images = (0 until count).mapNotNull { i ->
                val e = 6 + i * 14
                val w = gdata[e].toInt() and 0xFF
                val h = gdata[e + 1].toInt() and 0xFF
                val colorCount = gdata[e + 2].toInt() and 0xFF
                val planes = gbuf.getShort(e + 4).toInt() and 0xFFFF
                val bitCount = gbuf.getShort(e + 6).toInt() and 0xFFFF
                val iconId = gbuf.getShort(e + 12).toInt() and 0xFFFF
                val blob = icons[iconId] ?: return@mapNotNull null
                IconEntry(w, h, colorCount, planes, bitCount, blob)
            }
            IconGroup(groupId, images)
        }
    }
}
