package dev.doodad.reference

import dev.doodad.reference.model.ScenarioRepository
import dev.doodad.reference.ui.OracleScene
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.ParameterizedRobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(ParameterizedRobolectricTestRunner::class)
class OracleSceneScreenshotTest(
    private val sceneId: String,
    override val device: WearDevice,
) : WearScreenshotTest() {
    @Test
    fun oracleSceneMatchesGoldenAndSemanticContract() {
        val scenarios =
            ScenarioRepository(
                RuntimeEnvironment.getApplication().assets,
            ).loadAll()
        val scenario = scenarios.single { it.scene == sceneId }
        check(device.id in scenario.renderProfiles) {
            "${scenario.id} does not opt into ${device.id}"
        }
        renderScenario(scenario) {
            OracleScene(scenario)
        }
    }

    companion object {
        private val sceneIds =
            listOf(
                "transforming-list",
                "hero-metric",
                "two-button-group",
                "timer-running",
                "calculator-keypad",
                "workout-set-entry",
                "calorie-dashboard",
                "confirmation",
                "theme-switcher",
                "ambient-live-activity",
            )

        @JvmStatic
        @ParameterizedRobolectricTestRunner.Parameters(
            name = "{0}_{1}",
        )
        fun cases(): List<Array<Any>> =
            sceneIds.flatMap { scene ->
                WearDevice.entries.map { device ->
                    arrayOf(scene, device)
                }
            }
    }
}
