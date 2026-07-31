package dev.doodad.reference

import dev.doodad.reference.model.ScenarioRepository
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@Config(sdk = [33])
@RunWith(RobolectricTestRunner::class)
class ScenarioRepositoryTest {
    @Test
    fun loadsTenVersionedUniqueScenarios() {
        val scenarios =
            ScenarioRepository(
                RuntimeEnvironment.getApplication().assets,
            ).loadAll()

        assertEquals(10, scenarios.size)
        assertEquals(10, scenarios.map { it.id }.distinct().size)
        assertEquals(10, scenarios.map { it.scene }.distinct().size)
        assertTrue(scenarios.all { it.schemaVersion == 1 })
        assertTrue(scenarios.all { it.expectedSemantics.flatten().isNotEmpty() })
    }
}
