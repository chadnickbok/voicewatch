package dev.doodad.reference

import dev.doodad.reference.model.SceneSnapshotRepository
import dev.doodad.reference.ui.AppSpecComponentRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@Config(sdk = [33])
@RunWith(RobolectricTestRunner::class)
class SceneSnapshotRepositoryTest {
    private val repository: SceneSnapshotRepository
        get() =
            SceneSnapshotRepository(
                RuntimeEnvironment.getApplication().assets,
            )

    @Test
    fun loadsEveryHashedRuntimeSnapshotAcrossTheTwentyApps() {
        val loaded = repository.loadAll()

        assertEquals(96, loaded.size)
        assertEquals(96, loaded.map { it.sha256 }.distinct().size)
        assertEquals(20, loaded.map { it.snapshot.appId }.distinct().size)
        assertTrue(
            loaded.all {
                it.assetPath.endsWith("${it.sha256}.json")
            },
        )
        assertEquals(
            mapOf(
                "button" to 138,
                "card" to 64,
                "column" to 4,
                "image" to 4,
                "keypad" to 7,
                "live_card" to 15,
                "progress" to 10,
                "row" to 17,
                "screen" to 96,
                "scroll" to 13,
                "stepper" to 10,
                "text" to 174,
                "toggle" to 11,
                "voice_orb" to 1,
            ),
            loaded
                .flatMap { it.snapshot.nodes }
                .groupingBy { it.kind }
                .eachCount()
                .toSortedMap(),
        )
    }

    @Test
    fun registryCoversEveryPublicAppSpecKind() {
        assertEquals(
            setOf(
                "screen",
                "column",
                "row",
                "scroll",
                "text",
                "button",
                "card",
                "progress",
                "stepper",
                "toggle",
                "keypad",
                "voice_orb",
                "live_card",
                "image",
            ),
            AppSpecComponentRegistry.supportedKinds,
        )
    }

    @Test
    fun rejectsUnknownFieldsNullsAndKindPropertyMismatches() {
        val valid = minimalSnapshotJson()

        assertThrows(Exception::class.java) {
            repository.decode(valid.replace("\"nodes\":", "\"extra\":1,\"nodes\":"))
        }
        assertThrows(IllegalStateException::class.java) {
            repository.decode(
                valid.replace("\"label\": \"Run\"", "\"label\": null"),
            )
        }
        assertThrows(IllegalStateException::class.java) {
            repository.decode(
                valid.replace(
                    "\"props\": {\"alignment\": \"stretch\", \"gap\": \"sm\"}",
                    "\"props\": {\"alignment\": \"stretch\", \"gap\": \"sm\", \"tone\": \"primary\"}",
                ),
            )
        }
        assertThrows(IllegalStateException::class.java) {
            repository.decode(
                valid.replace(
                    "\"parent_id\": \"fixture.screen\"",
                    "\"parent_id\": \"missing.parent\"",
                ),
            )
        }
    }

    @Test
    fun parsesAValidSnapshotWithoutDefaultsOrCoercion() {
        val snapshot = repository.decode(minimalSnapshotJson())

        assertEquals("fixture", snapshot.appId)
        assertEquals("fixture.screen", snapshot.screenId)
        assertEquals("Run", snapshot.nodes[1].props.primaryText)
        assertEquals("fixture.activate", snapshot.nodes[1].actions.single().actionId)
    }
}

internal fun minimalSnapshotJson(): String =
    """
    {
      "schema_version": 1,
      "app_id": "fixture",
      "screen_id": "fixture.screen",
      "origin": "guest_appspec",
      "nodes": [
        {
          "id": "fixture.screen",
          "parent_id": null,
          "kind": "screen",
          "depth": 0,
          "child_count": 1,
          "visible": true,
          "enabled": true,
          "props": {"alignment": "stretch", "gap": "sm"},
          "semantics": {"role": "screen", "label": "Fixture"},
          "actions": []
        },
        {
          "id": "fixture.button",
          "parent_id": "fixture.screen",
          "kind": "button",
          "depth": 1,
          "child_count": 0,
          "visible": true,
          "enabled": true,
          "props": {
            "primary_text": "Run",
            "variant": "filled",
            "tone": "primary",
            "size": "default"
          },
          "semantics": {"role": "button", "label": "Run"},
          "actions": [{"kind": "tap", "action_id": "fixture.activate"}]
        }
      ]
    }
    """.trimIndent()
