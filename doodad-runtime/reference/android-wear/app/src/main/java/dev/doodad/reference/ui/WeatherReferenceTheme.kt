package dev.doodad.reference.ui

import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.wear.compose.material3.ColorScheme
import androidx.wear.compose.material3.Shapes
import androidx.wear.compose.material3.Typography
import dev.doodad.reference.ui.generated.WeatherColorRole
import dev.doodad.reference.ui.generated.WeatherFoundations

internal fun weatherColor(role: WeatherColorRole): Color {
    val token = checkNotNull(WeatherFoundations.colors[role])
    return Color(0xFF000000L or token.rgb888.toLong())
}

internal val WeatherColorScheme = ColorScheme().copy(
    primary = weatherColor(WeatherColorRole.Primary),
    primaryDim = weatherColor(WeatherColorRole.Rain),
    primaryContainer = weatherColor(WeatherColorRole.PrimaryContainer),
    onPrimary = weatherColor(WeatherColorRole.OnPrimary),
    onPrimaryContainer = weatherColor(WeatherColorRole.OnPrimaryContainer),
    secondaryContainer = weatherColor(WeatherColorRole.SecondaryContainer),
    onSecondaryContainer = weatherColor(WeatherColorRole.OnSecondaryContainer),
    tertiary = weatherColor(WeatherColorRole.Tertiary),
    tertiaryDim = weatherColor(WeatherColorRole.Stale),
    onTertiary = weatherColor(WeatherColorRole.OnTertiary),
    surfaceContainerLow = weatherColor(WeatherColorRole.SurfaceLow),
    surfaceContainer = weatherColor(WeatherColorRole.Surface),
    surfaceContainerHigh = weatherColor(WeatherColorRole.SurfaceHigh),
    onSurface = weatherColor(WeatherColorRole.OnSurface),
    onSurfaceVariant = weatherColor(WeatherColorRole.OnSurfaceVariant),
    outline = weatherColor(WeatherColorRole.OnSurfaceVariant),
    outlineVariant = weatherColor(WeatherColorRole.OutlineVariant),
    background = weatherColor(WeatherColorRole.Background),
    onBackground = weatherColor(WeatherColorRole.OnBackground),
    error = weatherColor(WeatherColorRole.Error),
    errorDim = weatherColor(WeatherColorRole.Error),
    errorContainer = weatherColor(WeatherColorRole.ErrorContainer),
    onError = weatherColor(WeatherColorRole.OnError),
    onErrorContainer = weatherColor(WeatherColorRole.OnErrorContainer),
)

internal val WeatherShapes = Shapes(
    extraSmall = RoundedCornerShape(14.dp),
    small = RoundedCornerShape(14.dp),
    medium = RoundedCornerShape(18.dp),
    large = RoundedCornerShape(22.dp),
    extraLarge = RoundedCornerShape(28.dp),
)

// The generated token sizes are physical pixels on the 240px / 192dp square
// oracle, so Compose uses 0.8sp per token pixel at the emulator's 1.25 density.
// Wear Compose's default family is Roboto; the LVGL side uses subsets made from
// the same Android Studio Roboto source.
private val DefaultWeatherTypography = Typography()
private val WeatherMedium = FontWeight.Medium
internal val WeatherTypography = DefaultWeatherTypography.copy(
    bodyExtraSmall = DefaultWeatherTypography.bodyExtraSmall.copy(
        fontSize = 8.sp,
        lineHeight = 9.6.sp,
        fontWeight = WeatherMedium,
    ),
    labelSmall = DefaultWeatherTypography.labelSmall.copy(
        fontSize = 11.2.sp,
        lineHeight = 13.6.sp,
        fontWeight = WeatherMedium,
    ),
    labelMedium = DefaultWeatherTypography.labelMedium.copy(
        fontSize = 11.2.sp,
        lineHeight = 13.6.sp,
        fontWeight = WeatherMedium,
    ),
    labelLarge = DefaultWeatherTypography.labelLarge.copy(
        fontSize = 11.2.sp,
        lineHeight = 13.6.sp,
        fontWeight = WeatherMedium,
    ),
    bodySmall = DefaultWeatherTypography.bodySmall.copy(
        fontSize = 14.4.sp,
        lineHeight = 17.6.sp,
        fontWeight = WeatherMedium,
    ),
    bodyMedium = DefaultWeatherTypography.bodyMedium.copy(
        fontSize = 14.4.sp,
        lineHeight = 17.6.sp,
        fontWeight = WeatherMedium,
    ),
    titleMedium = DefaultWeatherTypography.titleMedium.copy(
        fontSize = 14.4.sp,
        lineHeight = 17.6.sp,
        fontWeight = WeatherMedium,
    ),
    numeralSmall = DefaultWeatherTypography.numeralSmall.copy(
        fontSize = 22.4.sp,
        lineHeight = 24.8.sp,
        fontWeight = WeatherMedium,
    ),
    titleLarge = DefaultWeatherTypography.titleLarge.copy(
        fontSize = 25.6.sp,
        lineHeight = 27.2.sp,
        fontWeight = WeatherMedium,
    ),
    numeralLarge = DefaultWeatherTypography.numeralLarge.copy(
        fontSize = 54.4.sp,
        lineHeight = 58.4.sp,
        fontWeight = WeatherMedium,
    ),
)

internal fun weatherMetricCutShape() = CutCornerShape(10.dp)
