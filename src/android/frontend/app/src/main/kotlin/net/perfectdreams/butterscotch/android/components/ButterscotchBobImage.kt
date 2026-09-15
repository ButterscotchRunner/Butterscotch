package net.perfectdreams.butterscotch.android.components

import androidx.annotation.DrawableRes
import androidx.compose.animation.core.EaseInOut
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import net.perfectdreams.butterscotch.android.R

@Composable
fun ButterscotchBobImage(@DrawableRes id: Int, contentDescription: String) {
    val transition = rememberInfiniteTransition(label = "logoBob")
    val offsetY by transition.animateFloat(
        initialValue = 0f,
        targetValue = -12f,
        animationSpec = infiniteRepeatable(
            animation = tween(1_800, easing = EaseInOut),
            repeatMode = RepeatMode.Reverse,
        ),
        label = "offsetY",
    )

    Image(
        bitmap = ImageBitmap.imageResource(id),
        contentDescription = contentDescription,
        // Nearest-neighbor keeps the pixel-art crisp when scaled up
        filterQuality = FilterQuality.None,
        modifier = Modifier
            .size(160.dp)
            .offset { IntOffset(0, offsetY.dp.roundToPx()) },
    )
}