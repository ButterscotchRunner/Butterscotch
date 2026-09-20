package net.perfectdreams.butterscotch.android.components

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt

private const val MIN_ZOOM = 0.25f
private const val MAX_ZOOM = 8.0f

/**
 * Full-screen overlay for the visual-only free camera (photo mode). Drag with one finger to pan,
 * pinch to zoom. It owns the pan/zoom state and reports every change through [onCameraChange], which
 * the caller forwards to [net.perfectdreams.butterscotch.android.ButterscotchDroidRunner.setFreeCamera].
 *
 * Sits above the gamepad controls so it captures every touch while active; the only way back to the
 * game is the Close button (or the system back gesture), which resets the camera to identity. The
 * controls are a small pill in the top-left corner so they stay out of the way and fit in portrait.
 */
@Composable
fun FreeCameraOverlay(
    onCameraChange: (panX: Float, panY: Float, zoom: Float) -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier
) {
    var panX by remember { mutableFloatStateOf(0.0f) }
    var panY by remember { mutableFloatStateOf(0.0f) }
    var zoom by remember { mutableFloatStateOf(1.0f) }

    // Single place that mirrors the state out to the runner, so resets and gestures both go through it.
    LaunchedEffect(panX, panY, zoom) { onCameraChange(panX, panY, zoom) }

    val reset = {
        panX = 0.0f
        panY = 0.0f
        zoom = 1.0f
    }
    val resetAndClose = {
        reset()
        onClose()
    }

    // Composed after MenuOverlay, so this handler wins: back exits free cam instead of toggling the menu.
    BackHandler(enabled = true) { resetAndClose() }

    Box(modifier.fillMaxSize()) {
        // Gesture surface. pointerInput(Unit) never restarts, but reading the mutableFloatState-backed
        // panX/panY/zoom inside the callback gives fresh values each event (the closure captures the
        // state objects, not a snapshot), so accumulating onto them is correct.
        Box(
            Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    detectTransformGestures { centroid, pan, gestureZoom, _ ->
                        val w = size.width.toFloat().coerceAtLeast(1.0f)
                        val h = size.height.toFloat().coerceAtLeast(1.0f)
                        val oldZoom = zoom
                        val newZoom = (oldZoom * gestureZoom).coerceIn(MIN_ZOOM, MAX_ZOOM)

                        // Pivot the zoom around the pinch centroid: keep the world point under the
                        // fingers fixed by shifting the pan as the zoom changes (pan is a base-view
                        // fraction on the native side, so this is independent of the zoom level).
                        val pivotX = centroid.x / w - 0.5f
                        val pivotY = centroid.y / h - 0.5f
                        val zoomShift = 1.0f / oldZoom - 1.0f / newZoom
                        var newPanX = panX + pivotX * zoomShift
                        var newPanY = panY + pivotY * zoomShift

                        // Drag follows the finger: moving it right scrolls the world right (camera
                        // shifts left, so the pan fraction decreases). The world distance covered by a
                        // given screen drag shrinks as you zoom in, hence the divide by newZoom.
                        newPanX -= (pan.x / w) / newZoom
                        newPanY -= (pan.y / h) / newZoom

                        panX = newPanX
                        panY = newPanY
                        zoom = newZoom
                    }
                }
        )

        // Compact pill in the top-left: zoom readout + reset + close. Small enough to fit portrait.
        Row(
            Modifier
                .safeDrawingPadding()
                .padding(8.dp)
                .align(Alignment.TopStart)
                .background(Color(0xCC1E1E1E), RoundedCornerShape(percent = 50))
                .padding(start = 12.dp, end = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "${(zoom * 100).roundToInt()}%",
                color = Color.White,
                fontSize = 13.sp,
                fontWeight = FontWeight.Medium
            )

            IconButton(onClick = reset) {
                Icon(Icons.Filled.Refresh, contentDescription = "Reset camera", tint = Color.White)
            }

            IconButton(onClick = resetAndClose) {
                Icon(Icons.Filled.Close, contentDescription = "Close free camera", tint = Color.White)
            }
        }
    }
}
