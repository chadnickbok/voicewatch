package dev.doodad.reference.ui

import androidx.compose.foundation.Image
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import dev.doodad.reference.ui.generated.WeatherIcon
import dev.doodad.reference.ui.generated.WeatherIcons

/** Renders the pinned Meteocons/Material Symbols Weather asset catalog. */
@Composable
fun WeatherGlyph(
    icon: WeatherIcon,
    modifier: Modifier = Modifier,
    contentDescription: String? = null,
) {
    val iconSpec = checkNotNull(WeatherIcons.icons[icon])
    Image(
        painter = painterResource(iconSpec.drawableRes),
        contentDescription = contentDescription,
        modifier = modifier,
        contentScale = ContentScale.Fit,
    )
}
