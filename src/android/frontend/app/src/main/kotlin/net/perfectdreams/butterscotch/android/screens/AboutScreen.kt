package net.perfectdreams.butterscotch.android.screens

import androidx.compose.animation.core.EaseInOut
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import net.perfectdreams.butterscotch.android.R
import net.perfectdreams.butterscotch.android.Route
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchBobImage
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar

@Composable
fun AboutScreen(nav: NavHostController) {
    val resources = LocalResources.current
    val contributors = remember(resources) {
        resources.openRawResource(R.raw.contributors)
            .bufferedReader()
            .use { it.readText() }
            .lines()
            .filter { it.isNotBlank() }
    }

    Scaffold(
        topBar = { ButterscotchTopBar({ Text("About") }, nav, navigationIcon = { ButterscotchBackButton(nav) }) },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            ButterscotchBobImage(R.drawable.butterscotch_logo, "Butterscotch logo")

            Text("Butterscotch", style = MaterialTheme.typography.headlineMedium)

            Spacer(Modifier.height(4.dp))

            Text("\"Finally... Droidtale 2\"", style = MaterialTheme.typography.headlineSmall.copy(fontStyle = FontStyle.Italic))

            Spacer(Modifier.height(16.dp))

            Text("Created by MrPowerGamerBR", style = MaterialTheme.typography.bodyLarge)

            Spacer(Modifier.height(24.dp))
            Text("Contributors", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            contributors.forEach { name ->
                Text(name, style = MaterialTheme.typography.bodyMedium)
            }

            Spacer(Modifier.height(24.dp))
            OutlinedButton(onClick = { nav.navigate(Route.Licenses) }) {
                Text("Open-Source Licenses")
            }
        }
    }
}
