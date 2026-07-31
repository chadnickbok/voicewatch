/*
 * Derived from Android's Wear OS ComposeStarter sample.
 *
 * Copyright 2026 The Android Open Source Project
 * Licensed under the Apache License, Version 2.0.
 */
package dev.doodad.reference

import kotlin.math.roundToInt

data class WearDevice(
    val id: String,
    val modelName: String,
    val screenWidthPx: Int,
    val screenHeightPx: Int = screenWidthPx,
    val density: Float,
    val fontScale: Float = 1f,
    val isRound: Boolean,
) {
    val widthDp: Int = (screenWidthPx / density).roundToInt()
    val heightDp: Int = (screenHeightPx / density).roundToInt()
    val dpi: Int = (density * 160).roundToInt()
    val qualifier: String =
        "w${widthDp}dp-h${heightDp}dp-small-notlong-" +
            "${if (isRound) "round" else "notround"}-watch-" +
            "${dpi}dpi-keyshidden-nonav"

    companion object {
        val SmallRound =
            WearDevice(
                id = "wear_round_small",
                modelName = "Wear OS Small Round",
                screenWidthPx = 384,
                density = 2f,
                isRound = true,
            )

        val LargeRound =
            WearDevice(
                id = "wear_round_large",
                modelName = "Wear OS Large Round",
                screenWidthPx = 454,
                density = 2f,
                isRound = true,
            )

        val WatchSquare240 =
            WearDevice(
                id = "watch_square_240",
                modelName = "Doodad 240px Square",
                screenWidthPx = 240,
                density = 1.25f,
                isRound = false,
            )

        val entries: List<WearDevice> =
            listOf(
                SmallRound,
                LargeRound,
                WatchSquare240,
            )
    }
}
