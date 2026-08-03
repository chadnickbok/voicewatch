package dev.doodad.reference

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.unit.Density
import com.github.takahirom.roborazzi.ExperimentalRoborazziApi
import com.github.takahirom.roborazzi.RoborazziOptions
import com.github.takahirom.roborazzi.captureScreenRoboImage
import dev.doodad.reference.model.SceneSnapshotRepository
import dev.doodad.reference.ui.AppSpecReferenceRenderer
import dev.doodad.reference.ui.ComposeNodeEvidenceCollector
import dev.doodad.reference.ui.ReferenceGeometryProfile
import java.io.File
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [33])
@RunWith(RobolectricTestRunner::class)
class AppSpecDynamicScreenshotTest {
    @get:Rule
    val composeRule = createComposeRule()

    @OptIn(ExperimentalRoborazziApi::class)
    @Test
    fun capturesARealDynamicTimerSnapshotAndNodeEvidence() {
        val profile = ReferenceGeometryProfile.WatchSquare240
        RuntimeEnvironment.setQualifiers(profile.qualifier())
        val repository =
            SceneSnapshotRepository(
                RuntimeEnvironment.getApplication().assets,
            )
        val loaded =
            repository.loadAll().single { candidate ->
                candidate.snapshot.appId == "timer" &&
                    candidate.snapshot.nodes.any {
                        it.id == "timer.primary" &&
                            it.props.primaryText == "Cancel"
                    }
            }
        val collector = ComposeNodeEvidenceCollector()
        composeRule.mainClock.autoAdvance = false
        composeRule.setContent {
            CompositionLocalProvider(
                LocalDensity provides Density(1.25f, 1f),
            ) {
                AppSpecReferenceRenderer(
                    snapshot = loaded.snapshot,
                    profile = profile,
                    evidenceCollector = collector,
                )
            }
        }
        composeRule.mainClock.advanceTimeBy(3_000L)
        composeRule.waitForIdle()
        loaded.snapshot.nodes.filter { it.visible }.forEach { node ->
            composeRule
                .onNodeWithTag(node.id, useUnmergedTree = true)
                .assertExists()
        }

        captureScreenRoboImage(
            filePath =
                "src/test/screenshots/" +
                    "appspec_timer-running_watch_square_240.png",
            roborazziOptions =
                RoborazziOptions(
                    recordOptions =
                        RoborazziOptions.RecordOptions(
                            applyDeviceCrop = true,
                        ),
                    compareOptions =
                        RoborazziOptions.CompareOptions(
                            changeThreshold = 0.02f,
                        ),
                ),
        )

        val evidence =
            collector.build(
                snapshot = loaded.snapshot,
                snapshotSha256 = loaded.sha256,
                profile = profile,
                density = 1.25f,
            )
        val output =
            File(
                "build/reports/reference-semantics",
                "appspec_timer-running_watch_square_240.node-evidence.json",
            )
        output.parentFile?.mkdirs()
        output.writeText(evidence.toJson())
        Json.parseToJsonElement(output.readText())

        assertEquals(loaded.snapshot.nodes.size, evidence.nodes.size)
        assertEquals(240, evidence.physicalWidthPx)
        assertEquals(240, evidence.physicalHeightPx)
        assertTrue(evidence.nodes.any { !it.visible })
        assertTrue(
            evidence.nodes
                .filter {
                    it.boundsPx.width > 0 &&
                        it.boundsPx.height > 0
                }
                .all {
                    it.boundsPx.width > 0 &&
                        it.boundsPx.height > 0
                },
        )
        assertTrue(evidence.nodes.count { it.boundsPx.width > 0 } >= 4)
    }
}
