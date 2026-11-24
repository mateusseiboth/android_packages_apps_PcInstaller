package org.lineageos.pcinstaller.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// LineageOS teal, with calm surfaces for a large PC screen.
private val Light = lightColorScheme(
    primary = Color(0xFF167C80),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFA8EFF2),
    onPrimaryContainer = Color(0xFF002022),
    secondary = Color(0xFF4A6365),
    secondaryContainer = Color(0xFFCCE8E9),
    onSecondaryContainer = Color(0xFF051F21),
    tertiary = Color(0xFF4D5F7C),
    error = Color(0xFFBA1A1A),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
    background = Color(0xFFF6FBFA),
    surface = Color(0xFFF6FBFA),
    surfaceContainer = Color(0xFFEAEFEF),
    surfaceContainerHigh = Color(0xFFE4E9E9),
    outlineVariant = Color(0xFFBEC8C9),
)

private val Dark = darkColorScheme(
    primary = Color(0xFF8CD3D6),
    onPrimary = Color(0xFF00373A),
    primaryContainer = Color(0xFF004F53),
    onPrimaryContainer = Color(0xFFA8EFF2),
    secondary = Color(0xFFB0CCCD),
    secondaryContainer = Color(0xFF324B4D),
    onSecondaryContainer = Color(0xFFCCE8E9),
    tertiary = Color(0xFFB5C7E9),
    error = Color(0xFFFFB4AB),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF0E1514),
    surface = Color(0xFF0E1514),
    surfaceContainer = Color(0xFF1A2121),
    surfaceContainerHigh = Color(0xFF252B2B),
    outlineVariant = Color(0xFF3F4949),
)

private val SCHEMES = mapOf(false to Light, true to Dark)

@Composable
fun PcInstallerTheme(content: @Composable () -> Unit) {
    val scheme = SCHEMES.getValue(isSystemInDarkTheme())
    MaterialTheme(colorScheme = scheme, content = content)
}
