package dev.doodad.reference

import dev.doodad.reference.ui.AppSpecPattern
import dev.doodad.reference.ui.AppSpecPatternSelector
import dev.doodad.reference.ui.AppSpecStructuralFacts
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Test

class AppSpecPatternTest {
    @Test
    fun selectorReproducesTheFrozenEightyThreeDocumentCorpus() {
        val root = findDoodadRuntimeRoot()
        val inventory =
            Json.parseToJsonElement(
                File(root, "reference/parallax-corpus-inventory.json")
                    .readText(),
            ).jsonObject
        val sources = inventory.getValue("sources").jsonArray
        val selected =
            sources.map { source ->
                val path =
                    source.jsonObject
                        .getValue("path")
                        .jsonPrimitive
                        .content
                val document =
                    Json.parseToJsonElement(File(root, path).readText())
                        .jsonObject
                AppSpecPatternSelector.select(
                    structuralFacts(document.getValue("screen").jsonObject),
                )
            }

        assertEquals(83, selected.size)
        assertEquals(
            AppSpecPatternSelector.authoredCorpusExpectation,
            selected.groupingBy { it }.eachCount(),
        )
        assertEquals(
            mapOf(
                "countdown" to 1,
                "calendar_agenda" to 5,
                "camera_remote" to 5,
                "empty" to 1,
                "keypad" to 2,
                "notification_stack" to 6,
                "nutrition_dashboard" to 1,
                "nutrition_quick_add" to 1,
                "nutrition_review" to 1,
                "task_list" to 4,
                "live_action_detail" to 43,
                "media_player" to 5,
                "wallet_qr" to 1,
                "voice_ready" to 1,
                "workout_rest" to 1,
                "workout_set" to 2,
                "workout_summary" to 1,
                "status_detail" to 1,
                "weather_hero" to 1,
            ),
            inventory
                .getValue("authored")
                .jsonObject
                .getValue("patterns")
                .jsonObject
                .mapValues { it.value.jsonPrimitive.content.toInt() },
        )
    }

    @Test
    fun everyPatternIsSelectedByStructureAlone() {
        val examples =
            mapOf(
                AppSpecPattern.Keypad to
                    AppSpecStructuralFacts(mapOf("keypad" to 1), 1, 1),
                AppSpecPattern.Countdown to
                    AppSpecStructuralFacts(
                        mapOf("progress" to 1, "stepper" to 1),
                        2,
                        2,
                    ),
                AppSpecPattern.WeatherHero to
                    AppSpecStructuralFacts(
                        mapOf("card" to 1, "button" to 1, "text" to 4),
                        1,
                        1,
                    ),
                AppSpecPattern.CalendarAgenda to
                    AppSpecStructuralFacts(
                        mapOf(
                            "scroll" to 1,
                            "column" to 1,
                            "card" to 2,
                            "button" to 1,
                        ),
                        2,
                        1,
                    ),
                AppSpecPattern.NotificationStack to
                    AppSpecStructuralFacts(
                        mapOf("scroll" to 1, "card" to 1, "button" to 1),
                        2,
                        1,
                    ),
                AppSpecPattern.TaskList to
                    AppSpecStructuralFacts(
                        mapOf("scroll" to 1, "toggle" to 2, "button" to 1),
                        3,
                        3,
                    ),
                AppSpecPattern.WorkoutSet to
                    AppSpecStructuralFacts(
                        mapOf(
                            "stepper" to 1,
                            "live_card" to 1,
                            "button" to 1,
                            "text" to 1,
                        ),
                        2,
                        3,
                    ),
                AppSpecPattern.WorkoutRest to
                    AppSpecStructuralFacts(
                        mapOf(
                            "live_card" to 1,
                            "button" to 2,
                            "text" to 2,
                        ),
                        2,
                        2,
                    ),
                AppSpecPattern.WorkoutSummary to
                    AppSpecStructuralFacts(
                        mapOf(
                            "row" to 1,
                            "card" to 1,
                            "button" to 1,
                            "text" to 3,
                        ),
                        1,
                        1,
                    ),
                AppSpecPattern.NutritionDashboard to
                    AppSpecStructuralFacts(
                        mapOf(
                            "progress" to 1,
                            "card" to 1,
                            "button" to 1,
                            "text" to 2,
                        ),
                        1,
                        1,
                    ),
                AppSpecPattern.NutritionQuickAdd to
                    AppSpecStructuralFacts(
                        mapOf(
                            "row" to 1,
                            "stepper" to 1,
                            "card" to 1,
                            "button" to 2,
                            "text" to 1,
                        ),
                        3,
                        3,
                    ),
                AppSpecPattern.NutritionReview to
                    AppSpecStructuralFacts(
                        mapOf(
                            "row" to 1,
                            "card" to 1,
                            "button" to 2,
                            "text" to 2,
                        ),
                        2,
                        2,
                    ),
                AppSpecPattern.VoiceReady to
                    AppSpecStructuralFacts(
                        mapOf(
                            "voice_orb" to 1,
                            "card" to 1,
                            "text" to 1,
                        ),
                        1,
                        1,
                    ),
                AppSpecPattern.MediaPlayer to
                    AppSpecStructuralFacts(
                        mapOf(
                            "image" to 1,
                            "progress" to 1,
                            "row" to 1,
                            "button" to 2,
                            "text" to 2,
                        ),
                        2,
                        2,
                    ),
                AppSpecPattern.CameraRemote to
                    AppSpecStructuralFacts(
                        mapOf(
                            "image" to 1,
                            "row" to 1,
                            "button" to 2,
                            "text" to 2,
                        ),
                        2,
                        2,
                    ),
                AppSpecPattern.WalletQr to
                    AppSpecStructuralFacts(
                        mapOf(
                            "image" to 1,
                            "row" to 1,
                            "button" to 2,
                            "text" to 1,
                        ),
                        2,
                        2,
                    ),
                AppSpecPattern.LiveActionDetail to
                    AppSpecStructuralFacts(
                        mapOf(
                            "row" to 1,
                            "live_card" to 1,
                            "button" to 2,
                            "text" to 2,
                        ),
                        2,
                        2,
                    ),
                AppSpecPattern.ProgressDashboard to
                    AppSpecStructuralFacts(mapOf("progress" to 1), 0, 0),
                AppSpecPattern.MetricControl to
                    AppSpecStructuralFacts(mapOf("stepper" to 1), 1, 1),
                AppSpecPattern.Empty to
                    AppSpecStructuralFacts(mapOf("text" to 2), 0, 0),
                AppSpecPattern.ActionList to
                    AppSpecStructuralFacts(mapOf("button" to 2), 2, 2),
                AppSpecPattern.StatusDetail to
                    AppSpecStructuralFacts(
                        mapOf("card" to 1, "button" to 1),
                        1,
                        1,
                    ),
            )

        examples.forEach { (expected, facts) ->
            assertEquals(expected, AppSpecPatternSelector.select(facts))
        }
    }

    private fun structuralFacts(root: JsonObject): AppSpecStructuralFacts {
        val nodes = walk(root).toList()
        return AppSpecStructuralFacts(
            kindCounts =
                nodes.groupingBy {
                    it.getValue("type").jsonPrimitive.content
                }.eachCount(),
            actionCount =
                nodes.sumOf {
                    it["events"]?.jsonObject?.size ?: 0
                },
            interactiveCount =
                nodes.count {
                    it.getValue("type").jsonPrimitive.content in
                        setOf(
                            "button",
                            "stepper",
                            "toggle",
                            "keypad",
                            "voice_orb",
                        )
                },
        )
    }

    private fun walk(node: JsonObject): Sequence<JsonObject> =
        sequence {
            yield(node)
            node.getValue("props")
                .jsonObject["children"]
                ?.jsonArray
                ?.forEach { child ->
                    yieldAll(walk(child.jsonObject))
                }
        }

    private fun findDoodadRuntimeRoot(): File =
        generateSequence(
            File(requireNotNull(System.getProperty("user.dir"))).absoluteFile,
        ) { it.parentFile }
            .take(8)
            .firstOrNull {
                File(it, "apps/conformance-suite.json").isFile &&
                    File(
                        it,
                        "reference/parallax-corpus-inventory.json",
                    ).isFile
            }
            ?: error("Could not locate the doodad-runtime repository root")
}
