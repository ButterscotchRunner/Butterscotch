package net.perfectdreams.butterscotch.android.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val DarkColorScheme = darkColorScheme(
    primary = ButterscotchPrimary,
    onPrimary = ButterscotchOnPrimary,
    secondary = ButterscotchSecondary,
    onSecondary = ButterscotchOnSecondary,
    tertiary = ButterscotchTertiary,
    onTertiary = ButterscotchOnTertiary,
    background = ButterscotchBackground,
    onBackground = ButterscotchOnBackground,
    surface = ButterscotchSurface,
    onSurface = ButterscotchOnSurface,
    primaryContainer = ButterscotchPrimaryContainer,
    onPrimaryContainer = ButterscotchOnPrimaryContainer,
)

@Composable
fun ButterscotchAndroidTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        typography = Typography,
        content = content
    )
}
