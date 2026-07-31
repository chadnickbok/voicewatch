/*
 * Screenshot-test structure derived from Android's Wear OS ComposeStarter sample.
 *
 * Copyright 2026 The Android Open Source Project
 * Licensed under the Apache License, Version 2.0.
 */
package dev.doodad.reference

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.ComposeContentTestRule
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.printToString
import androidx.compose.ui.unit.Density
import com.github.takahirom.roborazzi.ExperimentalRoborazziApi
import com.github.takahirom.roborazzi.RoborazziOptions
import com.github.takahirom.roborazzi.captureScreenRoboImage
import dev.doodad.reference.model.ReferenceScenario
import java.io.File
import org.junit.Rule
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [33])
abstract class WearScreenshotTest {
    @get:Rule
    val composeRule: ComposeContentTestRule = createComposeRule()

    abstract val device: WearDevice
    open val tolerance: Float = 0.02f

    fun renderScenario(
        scenario: ReferenceScenario,
        content: @Composable () -> Unit,
    ) {
        RuntimeEnvironment.setQualifiers(device.qualifier)
        composeRule.mainClock.autoAdvance = false
        composeRule.setContent {
            CompositionLocalProvider(
                LocalDensity provides
                    Density(
                        density = device.density,
                        fontScale = scenario.fontScale * device.fontScale,
                    ),
            ) {
                content()
            }
        }
        composeRule.mainClock.advanceTimeBy(3_000L)
        composeRule.waitForIdle()
        assertSemanticContract(scenario)
        writeSemanticTree(scenario)
        captureScreenshot(scenario)
    }

    private fun assertSemanticContract(scenario: ReferenceScenario) {
        scenario.expectedSemantics.flatten().forEach { expected ->
            composeRule
                .onNodeWithTag(expected.id, useUnmergedTree = true)
                .assertExists()
                .assertContentDescriptionEquals(expected.label)
        }
    }

    private fun writeSemanticTree(scenario: ReferenceScenario) {
        val output =
            File(
                "build/reports/reference-semantics",
                "${scenario.scene}_${device.id}.txt",
            )
        output.parentFile?.mkdirs()
        output.writeText(
            composeRule
                .onAllNodes(isRoot(), useUnmergedTree = true)
                .printToString(maxDepth = 64),
        )
    }

    @OptIn(ExperimentalRoborazziApi::class)
    private fun captureScreenshot(scenario: ReferenceScenario) {
        captureScreenRoboImage(
            filePath =
                "src/test/screenshots/" +
                    "${scenario.scene}_${device.id}.png",
            roborazziOptions =
                RoborazziOptions(
                    recordOptions =
                        RoborazziOptions.RecordOptions(
                            applyDeviceCrop = true,
                        ),
                    compareOptions =
                        RoborazziOptions.CompareOptions(
                            changeThreshold = tolerance,
                        ),
                ),
        )
    }
}
