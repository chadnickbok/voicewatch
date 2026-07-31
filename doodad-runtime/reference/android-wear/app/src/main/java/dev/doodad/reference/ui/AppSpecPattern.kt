package dev.doodad.reference.ui

import dev.doodad.reference.model.SceneSnapshot

enum class AppSpecPattern(
    val wireName: String,
) {
    StatusDetail("status_detail"),
    ActionList("action_list"),
    MetricControl("metric_control"),
    Keypad("keypad"),
    Countdown("countdown"),
    ProgressDashboard("progress_dashboard"),
    Empty("empty"),
}

data class AppSpecStructuralFacts(
    val kindCounts: Map<String, Int>,
    val actionCount: Int,
    val interactiveCount: Int,
)

object AppSpecPatternSelector {
    val authoredCorpusExpectation: Map<AppSpecPattern, Int> =
        linkedMapOf(
            AppSpecPattern.StatusDetail to 68,
            AppSpecPattern.ActionList to 6,
            AppSpecPattern.MetricControl to 4,
            AppSpecPattern.Keypad to 2,
            AppSpecPattern.Countdown to 1,
            AppSpecPattern.ProgressDashboard to 1,
            AppSpecPattern.Empty to 1,
        )

    fun select(snapshot: SceneSnapshot): AppSpecPattern =
        select(
            AppSpecStructuralFacts(
                kindCounts = snapshot.nodes.groupingBy { it.kind }.eachCount(),
                actionCount = snapshot.nodes.sumOf { it.actions.size },
                interactiveCount =
                    snapshot.nodes.count {
                        it.kind in interactiveKinds
                    },
            ),
        )

    fun select(facts: AppSpecStructuralFacts): AppSpecPattern =
        when {
            facts.count("keypad") > 0 -> AppSpecPattern.Keypad
            facts.count("progress") > 0 && facts.count("stepper") > 0 ->
                AppSpecPattern.Countdown
            facts.count("progress") > 0 -> AppSpecPattern.ProgressDashboard
            facts.count("stepper") > 0 || facts.count("live_card") > 0 ->
                AppSpecPattern.MetricControl
            facts.interactiveCount == 0 -> AppSpecPattern.Empty
            facts.count("card") == 0 && facts.actionCount >= 2 ->
                AppSpecPattern.ActionList
            else -> AppSpecPattern.StatusDetail
        }

    private fun AppSpecStructuralFacts.count(kind: String): Int =
        kindCounts[kind] ?: 0

    private val interactiveKinds =
        setOf("button", "stepper", "toggle", "keypad", "voice_orb")
}
