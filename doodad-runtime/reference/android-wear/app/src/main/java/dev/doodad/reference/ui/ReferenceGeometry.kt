package dev.doodad.reference.ui

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

enum class ReferenceGeometryProfile(
    val id: String,
    val physicalWidthPx: Int,
    val physicalHeightPx: Int,
    val isRound: Boolean,
    val horizontalPadding: Dp,
    val verticalPadding: Dp,
    val itemSpacing: Dp,
) {
    WearRoundSmall(
        id = "wear_round_small",
        physicalWidthPx = 384,
        physicalHeightPx = 384,
        isRound = true,
        horizontalPadding = 12.dp,
        verticalPadding = 4.dp,
        itemSpacing = 4.dp,
    ),
    WearRoundLarge(
        id = "wear_round_large",
        physicalWidthPx = 454,
        physicalHeightPx = 454,
        isRound = true,
        horizontalPadding = 16.dp,
        verticalPadding = 6.dp,
        itemSpacing = 6.dp,
    ),
    WatchSquare240(
        id = "watch_square_240",
        physicalWidthPx = 240,
        physicalHeightPx = 240,
        isRound = false,
        horizontalPadding = 8.dp,
        verticalPadding = 8.dp,
        itemSpacing = 4.dp,
    ),
    ;

    companion object {
        fun fromId(id: String): ReferenceGeometryProfile =
            entries.singleOrNull { it.id == id }
                ?: error("Unsupported reference geometry profile: $id")
    }
}
