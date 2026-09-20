package net.perfectdreams.butterscotch.android

/**
 * How the game viewport and the gamepad controls are arranged on screen.
 *
 * Decision is driven by `(gameAspect, deviceAspect)`, not by device orientation alone, because
 * GameMaker games can target any aspect ratio - including mobile-portrait games whose "natural"
 * fit on a portrait phone is the same Overlay layout that a desktop landscape game gets on a
 * landscape phone.
 */
enum class LayoutMode {
    /**
     * Game fills the available area (letterboxed by the runner), controls float on top of it. The
     * controls naturally sit on top of the letterbox black bars on most aspect-ratio mismatches, so
     * they don't actually occlude gameplay. Used when game and device are "the same orientation."
     */
    Overlay,

    /**
     * Game occupies one side of the screen at its native aspect ratio, controls occupy the rest as
     * a dedicated region. Currently only implemented as vertical stacking (game on top, controls
     * below) - used when a landscape game is shown on a portrait device. The mirror case (portrait
     * game on landscape device, which would want horizontal stacking) falls back to Overlay for now
     * because it's rare and the horizontal-stack layout isn't worth building until we have a
     * specific game that needs it.
     */
    Stacked,
}

/**
 * Pick the layout mode that best matches the game's aspect ratio against the device's current
 * aspect ratio. See [LayoutMode] doc-comments for the rationale.
 *
 * @param gameAspect width / height of the game's native resolution. e.g. 640/480 = 1.333 (4:3
 *   landscape), 720/1280 = 0.5625 (9:16 portrait mobile).
 * @param deviceAspect width / height of the available drawing surface. > 1.0 means landscape, < 1.0
 *   means portrait.
 */
fun pickLayoutMode(gameAspect: Float, deviceAspect: Float): LayoutMode {
    val gameIsLandscape = gameAspect >= 1f
    val deviceIsLandscape = deviceAspect >= 1f
    return when {
        // Same orientation - overlay puts controls on the letterbox bars without occluding the game.
        gameIsLandscape == deviceIsLandscape -> LayoutMode.Overlay
        // Landscape game on portrait device - vertical stack with controls below.
        !deviceIsLandscape -> LayoutMode.Stacked
        // Portrait game on landscape device - would want horizontal stacking (Row with controls on
        // the side). Not implemented yet; Overlay is the least-bad fallback - it'll show the game
        // narrow in the center with controls floating, which is at least playable.
        else -> LayoutMode.Overlay
    }
}
