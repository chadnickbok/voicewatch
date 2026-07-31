package dev.doodad.reference.ui

import androidx.compose.animation.core.FiniteAnimationSpec
import androidx.compose.animation.core.snap
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.wear.compose.foundation.AmbientMode
import androidx.wear.compose.foundation.LocalAmbientModeManager
import androidx.wear.compose.material3.ColorScheme
import androidx.wear.compose.material3.MaterialTheme
import androidx.wear.compose.material3.MotionScheme
import androidx.wear.compose.material3.Shapes
import androidx.wear.compose.material3.Typography
import androidx.wear.compose.material3.dynamicColorScheme
import dev.doodad.reference.model.ThemeSpec

private val BaselineScheme = ColorScheme()

private val VioletScheme =
    BaselineScheme.copy(
        primary = Color(0xFFD8B9FF),
        primaryDim = Color(0xFFBE8DFF),
        primaryContainer = Color(0xFF4E286E),
        onPrimary = Color(0xFF351151),
        onPrimaryContainer = Color(0xFFF0DBFF),
        secondary = Color(0xFFCFC0DA),
        secondaryDim = Color(0xFFB4A5BE),
        secondaryContainer = Color(0xFF4B4054),
        onSecondary = Color(0xFF342B3D),
        onSecondaryContainer = Color(0xFFEBDCF5),
    )

private val AmbientScheme =
    BaselineScheme.copy(
        primary = Color(0xFFE6E1E5),
        primaryDim = Color(0xFFCAC5C9),
        primaryContainer = Color(0xFF202020),
        onPrimary = Color.Black,
        onPrimaryContainer = Color(0xFFE6E1E5),
        background = Color.Black,
        onBackground = Color(0xFFE6E1E5),
        surfaceContainerLow = Color.Black,
        surfaceContainer = Color(0xFF101010),
        surfaceContainerHigh = Color(0xFF202020),
        onSurface = Color(0xFFE6E1E5),
        onSurfaceVariant = Color(0xFFCAC5C9),
    )

private object ReducedMotionScheme : MotionScheme {
    override fun <T> defaultSpatialSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> fastSpatialSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> slowSpatialSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> defaultEffectsSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> fastEffectsSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> slowEffectsSpec(): FiniteAnimationSpec<T> = snap()
}

@Composable
fun ReferenceTheme(
    spec: ThemeSpec,
    content: @Composable (ambient: Boolean) -> Unit,
) {
    val systemAmbient =
        LocalAmbientModeManager.current?.currentAmbientMode is AmbientMode.Ambient
    val ambient = spec.ambient || systemAmbient
    val dynamicScheme =
        if (spec.dynamicColor && !ambient) dynamicColorScheme(LocalContext.current) else null
    val colorScheme =
        when {
            ambient -> AmbientScheme
            dynamicScheme != null -> dynamicScheme
            spec.colorScheme == "violet-dark" -> VioletScheme
            else -> BaselineScheme
        }
    val motionScheme =
        when {
            spec.reducedMotion || ambient -> ReducedMotionScheme
            spec.motionScheme == "standard" -> MotionScheme.standard()
            else -> MotionScheme.expressive()
        }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography(),
        shapes = Shapes(),
        motionScheme = motionScheme,
    ) {
        content(ambient)
    }
}
