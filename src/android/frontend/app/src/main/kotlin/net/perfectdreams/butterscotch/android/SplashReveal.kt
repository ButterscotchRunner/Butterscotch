package net.perfectdreams.butterscotch.android

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.hypot

@Composable
fun SplashReveal(onFinished: () -> Unit) {
    val holeProgress = remember { Animatable(0f) }
    val pieAlpha = remember { Animatable(1f) }

    LaunchedEffect(Unit) {
        // Pie holds for 0.2s then fades over 0.3s; the hole expands the whole time, so both land together at 0.5s
        launch {
            delay(200)
            pieAlpha.animateTo(0f, tween(500))
        }

        holeProgress.animateTo(1f, tween(1_000, easing = FastOutSlowInEasing))
        onFinished()
    }

    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        // Cream layer: the cut-out is punched only through this, never the pie.
        // Offscreen compositing is required so BlendMode.Clear erases to transparent (revealing the app) instead of painting black
        Box(
            Modifier
                .fillMaxSize()
                .graphicsLayer(compositingStrategy = CompositingStrategy.Offscreen)
                .drawWithContent {
                    drawContent()
                    val maxRadius = hypot(size.width / 2f, size.height / 2f)
                    drawCircle(color = Color.Black, radius = maxRadius * holeProgress.value, center = center, blendMode = BlendMode.Clear)
                }
                .background(Color(0xFFF1E6D4))
        )

        // Pie sits on top, fades on its own delayed timeline, untouched by the cut-out
        Image(
            bitmap = ImageBitmap.imageResource(R.drawable.splash_pie),
            contentDescription = null,
            // filterQuality None keeps the pixel art crisp when scaled up, matching filter="false" in splash_logo.xml
            filterQuality = FilterQuality.None,
            // 288dp matches the Android 12+ splash icon area for the no-icon-background case (themes.xml sets no windowSplashScreenIconBackgroundColor), so the replica pie is exactly the size the OS splash drew
            modifier = Modifier.size(288.dp).graphicsLayer { alpha = pieAlpha.value }
        )
    }
}
