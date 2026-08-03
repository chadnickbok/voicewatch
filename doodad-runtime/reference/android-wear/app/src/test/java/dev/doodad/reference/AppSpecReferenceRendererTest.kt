package dev.doodad.reference

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.Density
import dev.doodad.reference.model.SceneSnapshotRepository
import dev.doodad.reference.ui.AppSpecReferenceRenderer
import dev.doodad.reference.ui.DoodadActionIdsKey
import dev.doodad.reference.ui.ReferenceActionEnvelope
import dev.doodad.reference.ui.ReferenceActionPayload
import dev.doodad.reference.ui.ReferenceGeometryProfile
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@Config(sdk = [33])
@RunWith(RobolectricTestRunner::class)
class AppSpecReferenceRendererTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun actionEnvelopePreservesIdentityWithoutMutatingSnapshot() {
        RuntimeEnvironment.setQualifiers(
            ReferenceGeometryProfile.WatchSquare240.qualifier(),
        )
        val repository =
            SceneSnapshotRepository(
                RuntimeEnvironment.getApplication().assets,
            )
        val snapshot = repository.decode(minimalSnapshotJson())
        val before = snapshot.copy(nodes = snapshot.nodes.toList())
        val actions = mutableListOf<ReferenceActionEnvelope>()

        composeRule.setContent {
            CompositionLocalProvider(
                LocalDensity provides Density(1.25f, 1f),
            ) {
                AppSpecReferenceRenderer(
                    snapshot = snapshot,
                    profile = ReferenceGeometryProfile.WatchSquare240,
                    onAction = actions::add,
                )
            }
        }
        composeRule.waitForIdle()

        composeRule
            .onNodeWithTag("fixture.button", useUnmergedTree = true)
            .assert(
                SemanticsMatcher.expectValue(
                    DoodadActionIdsKey,
                    "fixture.activate",
                ),
            )
            .assertContentDescriptionEquals("Run")
            .performClick()
        composeRule
            .onNodeWithTag("fixture.button", useUnmergedTree = true)
            .assertContentDescriptionEquals("Run")

        assertEquals(
            listOf(
                ReferenceActionEnvelope(
                    nodeId = "fixture.button",
                    actionId = "fixture.activate",
                    eventKind = "tap",
                    payload = ReferenceActionPayload.None,
                ),
            ),
            actions,
        )
        assertEquals(before, snapshot)
        assertEquals("Run", snapshot.nodes[1].props.primaryText)
    }

    @Test
    fun weatherPrimitivesRenderAndHourlyActionPreservesIdentity() {
        RuntimeEnvironment.setQualifiers(
            ReferenceGeometryProfile.WatchSquare240.qualifier(),
        )
        val repository =
            SceneSnapshotRepository(
                RuntimeEnvironment.getApplication().assets,
            )
        val snapshot =
            repository.loadAll()
                .first { loaded ->
                    loaded.snapshot.appId == "weather" &&
                        loaded.snapshot.nodes.any { it.id == "weather.condition-icon" }
                }.snapshot
        val actions = mutableListOf<ReferenceActionEnvelope>()

        composeRule.setContent {
            CompositionLocalProvider(
                LocalDensity provides Density(1.25f, 1f),
            ) {
                AppSpecReferenceRenderer(
                    snapshot = snapshot,
                    profile = ReferenceGeometryProfile.WatchSquare240,
                    onAction = actions::add,
                )
            }
        }
        composeRule.waitForIdle()

        listOf(
            "weather.current",
            "weather.condition-icon",
            "weather.primary",
        ).forEach { id ->
            composeRule
                .onNodeWithTag(id, useUnmergedTree = true)
                .assertExists()
        }

        composeRule
            .onNodeWithTag("weather.primary", useUnmergedTree = true)
            .performClick()
        composeRule.waitForIdle()

        assertEquals(
            ReferenceActionEnvelope(
                nodeId = "weather.primary",
                actionId = "weather.hourly",
                eventKind = "tap",
                payload = ReferenceActionPayload.None,
            ),
            actions.last(),
        )
    }
}

internal fun ReferenceGeometryProfile.qualifier(): String {
    val density = physicalWidthPx / 192f
    val dpi = (density * 160).toInt()
    return "w192dp-h192dp-small-notlong-" +
        "${if (isRound) "round" else "notround"}-watch-" +
        "${dpi}dpi-keyshidden-nonav"
}
