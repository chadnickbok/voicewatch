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
    WeatherHero("weather_hero"),
    CalendarAgenda("calendar_agenda"),
    NotificationStack("notification_stack"),
    TaskList("task_list"),
    Powerlifting("powerlifting"),
    WorkoutSet("workout_set"),
    WorkoutRest("workout_rest"),
    WorkoutSummary("workout_summary"),
    NutritionDashboard("nutrition_dashboard"),
    NutritionQuickAdd("nutrition_quick_add"),
    NutritionReview("nutrition_review"),
    VoiceReady("voice_ready"),
    LiveActionDetail("live_action_detail"),
    MediaPlayer("media_player"),
    CameraRemote("camera_remote"),
    WalletQr("wallet_qr"),
    CanvasGame("canvas_game"),
    ProgressDashboard("progress_dashboard"),
    Empty("empty"),
}

data class AppSpecStructuralFacts(
    val kindCounts: Map<String, Int>,
    val actionCount: Int,
    val interactiveCount: Int,
    val screenId: String? = null,
)

object AppSpecPatternSelector {
    val authoredCorpusExpectation: Map<AppSpecPattern, Int> =
        linkedMapOf(
            AppSpecPattern.StatusDetail to 4,
            AppSpecPattern.ActionList to 1,
            AppSpecPattern.Keypad to 1,
            AppSpecPattern.Countdown to 1,
            AppSpecPattern.CalendarAgenda to 5,
            AppSpecPattern.NotificationStack to 6,
            AppSpecPattern.TaskList to 4,
            AppSpecPattern.Powerlifting to 12,
            AppSpecPattern.NutritionDashboard to 1,
            AppSpecPattern.NutritionQuickAdd to 1,
            AppSpecPattern.NutritionReview to 1,
            AppSpecPattern.VoiceReady to 1,
            AppSpecPattern.LiveActionDetail to 43,
            AppSpecPattern.MediaPlayer to 5,
            AppSpecPattern.CameraRemote to 5,
            AppSpecPattern.WalletQr to 1,
            AppSpecPattern.CanvasGame to 2,
            AppSpecPattern.Empty to 1,
        )

    fun select(snapshot: SceneSnapshot): AppSpecPattern =
        select(
            AppSpecStructuralFacts(
                kindCounts = snapshot.nodes.groupingBy { it.kind }.eachCount(),
                actionCount =
                    snapshot.nodes.sumOf { node ->
                        node.actions.count { it.kind != "page_changed" }
                    },
                interactiveCount =
                    snapshot.nodes.count {
                        it.kind in interactiveKinds
                    },
                screenId = snapshot.root.id,
            ),
        )

    fun select(facts: AppSpecStructuralFacts): AppSpecPattern =
        when {
            facts.screenId?.startsWith("powerlifting.") == true ->
                AppSpecPattern.Powerlifting
            facts.count("canvas") == 1 &&
                facts.count("keypad") == 1 &&
                facts.count("text") == 2 ->
                AppSpecPattern.CanvasGame
            facts.count("keypad") > 0 -> AppSpecPattern.Keypad
            facts.count("progress") > 0 && facts.count("stepper") > 0 ->
                AppSpecPattern.Countdown
            facts.count("pager") == 1 &&
                facts.count("icon") > 0 &&
                facts.count("chart") > 0 ->
                AppSpecPattern.WeatherHero
            facts.count("card") == 1 &&
                facts.count("button") == 1 &&
                facts.count("text") == 4 ->
                AppSpecPattern.WeatherHero
            facts.count("scroll") == 1 &&
                facts.count("column") == 1 &&
                facts.count("card") > 0 ->
                AppSpecPattern.CalendarAgenda
            facts.count("scroll") == 1 && facts.count("card") > 0 ->
                AppSpecPattern.NotificationStack
            facts.count("scroll") == 1 && facts.count("toggle") > 0 ->
                AppSpecPattern.TaskList
            facts.count("stepper") == 1 &&
                facts.count("live_card") == 1 &&
                facts.count("button") == 1 &&
                facts.count("text") == 1 ->
                AppSpecPattern.WorkoutSet
            facts.count("row") == 1 &&
                facts.count("live_card") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 2 ->
                AppSpecPattern.LiveActionDetail
            facts.count("image") == 1 &&
                facts.count("progress") == 1 &&
                facts.count("row") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 2 ->
                AppSpecPattern.MediaPlayer
            facts.count("image") == 1 &&
                facts.count("row") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 2 ->
                AppSpecPattern.CameraRemote
            facts.count("image") == 1 &&
                facts.count("row") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 1 ->
                AppSpecPattern.WalletQr
            facts.count("live_card") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 2 ->
                AppSpecPattern.WorkoutRest
            facts.count("row") == 1 &&
                facts.count("card") == 1 &&
                facts.count("button") == 1 &&
                facts.count("text") == 3 ->
                AppSpecPattern.WorkoutSummary
            facts.count("progress") == 1 &&
                facts.count("card") == 1 &&
                facts.count("button") == 1 &&
                facts.count("text") == 2 ->
                AppSpecPattern.NutritionDashboard
            facts.count("row") == 1 &&
                facts.count("stepper") == 1 &&
                facts.count("card") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 1 ->
                AppSpecPattern.NutritionQuickAdd
            facts.count("row") == 1 &&
                facts.count("card") == 1 &&
                facts.count("button") == 2 &&
                facts.count("text") == 2 ->
                AppSpecPattern.NutritionReview
            facts.count("voice_orb") == 1 &&
                facts.count("card") == 1 &&
                facts.count("text") == 1 ->
                AppSpecPattern.VoiceReady
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
        setOf(
            "button",
            "stepper",
            "toggle",
            "keypad",
            "voice_orb",
            "canvas",
            "pager",
        )
}
