package net.perfectdreams.butterscotch.android

/**
 * RAII wrapper around the opaque [ButterscotchNative.dataWinParseLight] handle. Use with [use]:
 *
 * ```
 * ParsedDataWin.parseLight(path)?.use { dw ->
 *     val title = dw.displayName ?: dw.name ?: folderName
 * }
 * ```
 *
 * KNOWN LIMITATION: the underlying C `DataWin_parse` currently calls `exit(1)` on file-open
 * failure, empty file, missing FORM magic, and various deeper parse errors — so in practice
 * `parseLight` either returns a valid handle or *crashes the process*. The null branch exists for
 * the day the C side is refactored to actually return failure. Callers should pre-validate that
 * the WAD file exists before calling; deeper validation (header check etc.) is deliberately not
 * done — if the file is corrupt, the process dies.
 *
 * Once obtained, the handle is freed by [close]; double-close is a no-op.
 */
class ParsedDataWin private constructor(private var handle: Long) : AutoCloseable {
    val name: String? get() = checkOpen().let { ButterscotchNative.dataWinName(handle) }
    val displayName: String? get() = checkOpen().let { ButterscotchNative.dataWinDisplayName(handle) }
    val wadVersion: Int get() = checkOpen().let { ButterscotchNative.dataWinWadVersion(handle) }

    override fun close() {
        if (handle == 0L) return
        val h = handle
        handle = 0L
        ButterscotchNative.dataWinFree(h)
    }

    private fun checkOpen() {
        check(handle != 0L) { "ParsedDataWin used after close()" }
    }

    companion object {
        fun parseLight(wadPath: String): ParsedDataWin? {
            val handle = ButterscotchNative.dataWinParseLight(wadPath)
            return if (handle == 0L) null else ParsedDataWin(handle)
        }
    }
}
