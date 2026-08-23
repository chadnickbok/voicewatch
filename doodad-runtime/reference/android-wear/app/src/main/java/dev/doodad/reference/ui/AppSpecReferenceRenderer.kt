package dev.doodad.reference.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.SemanticsPropertyKey
import androidx.compose.ui.semantics.SemanticsPropertyReceiver
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.wear.compose.foundation.lazy.TransformingLazyColumn
import androidx.wear.compose.foundation.lazy.rememberTransformingLazyColumnState
import androidx.wear.compose.material3.AppScaffold
import androidx.wear.compose.material3.Button
import androidx.wear.compose.material3.ButtonDefaults
import androidx.wear.compose.material3.ButtonGroup
import androidx.wear.compose.material3.Card
import androidx.wear.compose.material3.CheckboxButton
import androidx.wear.compose.material3.ChildButton
import androidx.wear.compose.material3.CircularProgressIndicator
import androidx.wear.compose.material3.CompactButton
import androidx.wear.compose.material3.FilledTonalButton
import androidx.wear.compose.material3.LinearProgressIndicator
import androidx.wear.compose.material3.MaterialTheme
import androidx.wear.compose.material3.OutlinedButton
import androidx.wear.compose.material3.ScreenScaffold
import androidx.wear.compose.material3.SegmentedCircularProgressIndicator
import androidx.wear.compose.material3.Stepper
import androidx.wear.compose.material3.SwitchButton
import androidx.wear.compose.material3.Text
import androidx.wear.compose.material3.TitleCard
import dev.doodad.reference.model.SceneAction
import dev.doodad.reference.model.CanvasCommand
import dev.doodad.reference.model.CanvasDisplayListCodec
import dev.doodad.reference.model.SceneNode
import dev.doodad.reference.model.SceneSnapshot
import dev.doodad.reference.model.SceneSnapshotValidator
import dev.doodad.reference.model.ThemeSpec
import dev.doodad.reference.ui.generated.WeatherIcons
import dev.doodad.reference.ui.generated.WeatherColorRole

data class ReferenceActionEnvelope(
    val nodeId: String,
    val actionId: String,
    val eventKind: String,
    val payload: ReferenceActionPayload = ReferenceActionPayload.None,
)

sealed interface ReferenceActionPayload {
    data object None : ReferenceActionPayload

    data class Text(
        val value: String,
    ) : ReferenceActionPayload

    data class Number(
        val value: Int,
    ) : ReferenceActionPayload

    data class Checked(
        val value: Boolean,
    ) : ReferenceActionPayload
}

enum class AppSpecComponentMapping(
    val kind: String,
) {
    Screen("screen"),
    Column("column"),
    Row("row"),
    Scroll("scroll"),
    Text("text"),
    Button("button"),
    Card("card"),
    Progress("progress"),
    Stepper("stepper"),
    Toggle("toggle"),
    Keypad("keypad"),
    VoiceOrb("voice_orb"),
    LiveCard("live_card"),
    Image("image"),
    Canvas("canvas"),
    Icon("icon"),
    Surface("surface"),
    Chart("chart"),
    Pager("pager"),
}

object AppSpecComponentRegistry {
    val supportedKinds: Set<String> =
        AppSpecComponentMapping.entries.mapTo(linkedSetOf()) { it.kind }

    fun mappingFor(kind: String): AppSpecComponentMapping =
        AppSpecComponentMapping.entries.singleOrNull { it.kind == kind }
            ?: error("No Material mapping registered for AppSpec kind $kind")

    @Composable
    internal fun Render(
        node: SceneNode,
        snapshot: SceneSnapshot,
        profile: ReferenceGeometryProfile,
        evidenceCollector: ComposeNodeEvidenceCollector?,
        onAction: (ReferenceActionEnvelope) -> Unit,
    ) {
        if (!node.visible) {
            return
        }
        val context =
            RenderContext(
                snapshot = snapshot,
                profile = profile,
                evidenceCollector = evidenceCollector,
                onAction = onAction,
            )
        when (mappingFor(node.kind)) {
            AppSpecComponentMapping.Screen,
            AppSpecComponentMapping.Column,
            -> ContainerColumn(node, context)
            AppSpecComponentMapping.Row -> ContainerRow(node, context)
            AppSpecComponentMapping.Scroll -> ScrollContainer(node, context)
            AppSpecComponentMapping.Text -> AppSpecText(node, context)
            AppSpecComponentMapping.Button -> AppSpecButton(node, context)
            AppSpecComponentMapping.Card -> AppSpecCard(node, context)
            AppSpecComponentMapping.Progress -> AppSpecProgress(node, context)
            AppSpecComponentMapping.Stepper -> AppSpecStepper(node, context)
            AppSpecComponentMapping.Toggle -> AppSpecToggle(node, context)
            AppSpecComponentMapping.Keypad -> AppSpecKeypad(node, context)
            AppSpecComponentMapping.VoiceOrb -> AppSpecVoiceOrb(node, context)
            AppSpecComponentMapping.LiveCard -> AppSpecLiveCard(node, context)
            AppSpecComponentMapping.Image -> AppSpecImage(node, context)
            AppSpecComponentMapping.Canvas -> AppSpecCanvas(node, context)
            AppSpecComponentMapping.Icon -> AppSpecIcon(node, context)
            AppSpecComponentMapping.Surface -> AppSpecSurface(node, context)
            AppSpecComponentMapping.Chart -> AppSpecChart(node, context)
            AppSpecComponentMapping.Pager -> AppSpecPager(node, context)
        }
    }
}

@Composable
fun AppSpecReferenceRenderer(
    snapshot: SceneSnapshot,
    profile: ReferenceGeometryProfile,
    modifier: Modifier = Modifier,
    theme: ThemeSpec = AppSpecReferenceDefaults.theme,
    evidenceCollector: ComposeNodeEvidenceCollector? = null,
    onAction: (ReferenceActionEnvelope) -> Unit = {},
) {
    SceneSnapshotValidator.validate(snapshot)
    snapshot.nodes.forEach {
        AppSpecComponentRegistry.mappingFor(it.kind)
    }
    val pattern = AppSpecPatternSelector.select(snapshot)
    val root = snapshot.root

    val resolvedTheme =
        if (snapshot.appId == "weather" && theme == AppSpecReferenceDefaults.theme) {
            AppSpecReferenceDefaults.weatherTheme
        } else {
            theme
        }
    ReferenceTheme(spec = resolvedTheme) {
        AppScaffold(timeText = {}) {
            val pageChanged = root.action("page_changed")
            val dragDistance = remember { floatArrayOf(0f) }
            Box(
                modifier =
                    modifier
                        .fillMaxSize()
                        .then(
                            if (pageChanged == null) {
                                Modifier
                            } else {
                                Modifier.pointerInput(
                                    root.id,
                                    pageChanged.actionId,
                                ) {
                                    val threshold = 48.dp.toPx()
                                    detectHorizontalDragGestures(
                                        onDragStart = { dragDistance[0] = 0f },
                                        onHorizontalDrag = { _, amount ->
                                            dragDistance[0] += amount
                                        },
                                        onDragEnd = {
                                            val delta =
                                                when {
                                                    dragDistance[0] <= -threshold -> 1
                                                    dragDistance[0] >= threshold -> -1
                                                    else -> 0
                                                }
                                            if (delta != 0) {
                                                onAction(
                                                    ReferenceActionEnvelope(
                                                        nodeId = root.id,
                                                        actionId = pageChanged.actionId,
                                                        eventKind = pageChanged.kind,
                                                        payload = ReferenceActionPayload.Number(delta),
                                                    ),
                                                )
                                            }
                                            dragDistance[0] = 0f
                                        },
                                        onDragCancel = { dragDistance[0] = 0f },
                                    )
                                }
                            },
                        )
                        .appSpecNode(root, evidenceCollector),
            ) {
                PatternSurface(
                    pattern = pattern,
                    snapshot = snapshot,
                    profile = profile,
                    evidenceCollector = evidenceCollector,
                    onAction = onAction,
                )
            }
        }
    }
}

object AppSpecReferenceDefaults {
    val theme =
        ThemeSpec(
            colorScheme = "violet-dark",
            typography = "wear-material-3",
            shapes = "expressive",
            motionScheme = "expressive",
            dynamicColor = false,
            ambient = false,
            reducedMotion = true,
        )
    val weatherTheme =
        ThemeSpec(
            colorScheme = "weather-dark",
            typography = "weather-roboto",
            shapes = "weather-square",
            motionScheme = "expressive",
            dynamicColor = false,
            ambient = false,
            reducedMotion = true,
        )
}

@Composable
private fun PatternSurface(
    pattern: AppSpecPattern,
    snapshot: SceneSnapshot,
    profile: ReferenceGeometryProfile,
    evidenceCollector: ComposeNodeEvidenceCollector?,
    onAction: (ReferenceActionEnvelope) -> Unit,
) {
    val rootChildren = snapshot.childrenOf(snapshot.root)
    val children =
        if (pattern == AppSpecPattern.Countdown) {
            rootChildren
        } else {
            rootChildren.filter { it.visible }
        }
    val context =
        RenderContext(
            snapshot = snapshot,
            profile = profile,
            evidenceCollector = evidenceCollector,
            onAction = onAction,
        )
    if (!profile.isRound && snapshot.root.id.startsWith("powerlifting.")) {
        SquarePowerliftingSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.Empty && snapshot.appId != "weather") {
        val state = rememberScrollState()
        ScreenScaffold(
            scrollState = state,
            scrollIndicator = null,
        ) { contentPadding ->
            Box(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .padding(contentPadding)
                        .padding(
                            horizontal = profile.horizontalPadding,
                            vertical = profile.verticalPadding,
                        ),
                contentAlignment = Alignment.Center,
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement =
                        Arrangement.spacedBy(profile.itemSpacing),
                ) {
                    children.forEach { child ->
                        AppSpecComponentRegistry.Render(
                            child,
                            snapshot,
                            profile,
                            evidenceCollector,
                            onAction,
                        )
                    }
                }
            }
        }
        return
    }

    if (profile.isRound) {
        RoundPatternSurface(children, context)
    } else {
        SquarePatternSurface(children, context, pattern)
    }
}

@Composable
private fun RoundPatternSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val state = rememberTransformingLazyColumnState()
    ScreenScaffold(
        scrollState = state,
        scrollIndicator = null,
    ) { contentPadding ->
        TransformingLazyColumn(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(horizontal = context.profile.horizontalPadding),
            state = state,
            contentPadding = contentPadding,
            verticalArrangement =
                Arrangement.spacedBy(context.profile.itemSpacing),
        ) {
            children.forEach { child ->
                item(key = child.id) {
                    AppSpecComponentRegistry.Render(
                        child,
                        context.snapshot,
                        context.profile,
                        context.evidenceCollector,
                        context.onAction,
                    )
                }
            }
        }
    }
}

@Composable
private fun SquarePatternSurface(
    children: List<SceneNode>,
    context: RenderContext,
    pattern: AppSpecPattern,
) {
    if (context.snapshot.root.id.startsWith("powerlifting.")) {
        SquarePowerliftingSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.CanvasGame) {
        SquareCanvasGameSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.Keypad) {
        SquareKeypadSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.Countdown) {
        SquareCountdownSurface(children, context)
        return
    }
    if (context.snapshot.appId == "weather" && children.size == 1) {
        when (children.single().id) {
            "weather.current" -> SquareWeatherCurrentSurface(context)
            "weather.hourly" -> SquareWeatherHourlySurface(context)
            "weather.daily-page" -> SquareWeatherDailySurface(context)
            "weather.details-page" -> SquareWeatherDetailsSurface(context)
            "weather.rain-page" -> SquareWeatherRainSurface(context)
            else -> Unit
        }
        if (children.single().id in setOf(
                "weather.current",
                "weather.hourly",
                "weather.daily-page",
                "weather.details-page",
                "weather.rain-page",
            )
        ) {
            return
        }
    }
    if (pattern == AppSpecPattern.WeatherHero &&
        children.none { it.kind == "pager" }
    ) {
        SquareWeatherHeroSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.CalendarAgenda) {
        SquareCalendarAgendaSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.WorkoutSet) {
        SquareWorkoutSetSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.WorkoutRest) {
        SquareWorkoutRestSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.WorkoutSummary) {
        SquareWorkoutSummarySurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.NutritionDashboard) {
        SquareNutritionDashboardSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.NutritionQuickAdd) {
        SquareNutritionQuickAddSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.NutritionReview) {
        SquareNutritionReviewSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.VoiceReady) {
        SquareVoiceReadySurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.LiveActionDetail) {
        SquareLiveActionDetailSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.MediaPlayer) {
        SquareMediaPlayerSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.CameraRemote) {
        SquareCameraRemoteSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.WalletQr) {
        SquareWalletQrSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.NotificationStack) {
        SquareNotificationStackSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.TaskList) {
        SquareTaskListSurface(children, context)
        return
    }
    val state = rememberScrollState()
    ScreenScaffold(
        scrollState = state,
        contentPadding = PaddingValues(0.dp),
        scrollIndicator = null,
    ) { contentPadding ->
        Column(
            modifier =
                Modifier
                    .fillMaxSize()
                    .verticalScroll(state)
                    .padding(contentPadding)
                    .padding(
                        horizontal = context.profile.horizontalPadding,
                        vertical = context.profile.verticalPadding,
                    ),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement =
                if (pattern == AppSpecPattern.Keypad) {
                    Arrangement.Center
                } else {
                    Arrangement.spacedBy(context.profile.itemSpacing)
                },
        ) {
            children.forEach { child ->
                AppSpecComponentRegistry.Render(
                    child,
                    context.snapshot,
                    context.profile,
                    context.evidenceCollector,
                    context.onAction,
                )
            }
        }
    }
}

private val PowerliftingBackground = Color(0xFF00102B)
private val PowerliftingSurface = Color(0xFF0A2046)
private val PowerliftingSurfaceHigh = Color(0xFF132B5A)
private val PowerliftingPrimary = Color(0xFFA98CFF)
private val PowerliftingPrimaryContainer = Color(0xFF5146B6)
private val PowerliftingOnSurface = Color(0xFFF3F0FF)
private val PowerliftingOnSurfaceVariant = Color(0xFFB8C7F2)
private val PowerliftingSuccess = Color(0xFF72DDA7)
private val PowerliftingError = Color(0xFFFF817D)
private val PowerliftingAmber = Color(0xFFFFC857)

private data class PowerliftingRect(
    val x: Int,
    val y: Int,
    val width: Int,
    val height: Int,
)

private fun powerliftingRect(id: String): PowerliftingRect =
    when (id) {
        "powerlifting.today.kicker" -> PowerliftingRect(8, 4, 176, 18)
        "powerlifting.today.hero" -> PowerliftingRect(8, 26, 176, 76)
        "powerlifting.today.volume" -> PowerliftingRect(8, 106, 176, 30)
        "powerlifting.today.start" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.training-hub.title" -> PowerliftingRect(8, 4, 176, 18)
        "powerlifting.training-hub.plan" -> PowerliftingRect(8, 24, 176, 48)
        "powerlifting.training-hub.goal" -> PowerliftingRect(8, 76, 176, 56)
        "powerlifting.training-hub.progress" -> PowerliftingRect(16, 132, 160, 5)
        "powerlifting.training-hub.done" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.workout-builder.title" -> PowerliftingRect(8, 2, 130, 20)
        "powerlifting.workout-builder.count" -> PowerliftingRect(140, 4, 44, 18)
        "powerlifting.workout-builder.squat" -> PowerliftingRect(8, 26, 176, 48)
        "powerlifting.workout-builder.remaining" -> PowerliftingRect(8, 76, 176, 14)
        "powerlifting.workout-builder.add" -> PowerliftingRect(8, 92, 176, 48)
        "powerlifting.workout-builder.save" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.exercise-prescription.title" -> PowerliftingRect(8, 2, 176, 18)
        "powerlifting.exercise-prescription.sets" -> PowerliftingRect(8, 24, 176, 48)
        "powerlifting.exercise-prescription.reps" -> PowerliftingRect(8, 74, 176, 48)
        "powerlifting.exercise-prescription.context" -> PowerliftingRect(8, 124, 176, 14)
        "powerlifting.exercise-prescription.done" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.strength-goal.title" -> PowerliftingRect(8, 2, 176, 18)
        "powerlifting.strength-goal.lift" -> PowerliftingRect(8, 24, 176, 30)
        "powerlifting.strength-goal.target" -> PowerliftingRect(8, 58, 176, 64)
        "powerlifting.strength-goal.context" -> PowerliftingRect(8, 126, 176, 12)
        "powerlifting.strength-goal.save" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.session.title" -> PowerliftingRect(8, 2, 176, 18)
        "powerlifting.session.count" -> PowerliftingRect(8, 20, 176, 32)
        "powerlifting.session.progress" -> PowerliftingRect(8, 52, 176, 4)
        "powerlifting.session.squat" -> PowerliftingRect(8, 58, 176, 48)
        "powerlifting.session.bench" -> PowerliftingRect(8, 108, 176, 15)
        "powerlifting.session.deadlift" -> PowerliftingRect(8, 125, 176, 15)
        "powerlifting.session.begin" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.exercise-picker.title" -> PowerliftingRect(8, 2, 176, 18)
        "powerlifting.exercise-picker.back-squat" -> PowerliftingRect(8, 24, 176, 48)
        "powerlifting.exercise-picker.front-squat" -> PowerliftingRect(8, 76, 176, 48)
        "powerlifting.exercise-picker.paused-squat" -> PowerliftingRect(8, 128, 176, 48)
        "powerlifting.active-set.exercise" -> PowerliftingRect(8, 4, 128, 18)
        "powerlifting.active-set.set" -> PowerliftingRect(8, 24, 128, 14)
        "powerlifting.active-set.progress" -> PowerliftingRect(142, 7, 42, 6)
        "powerlifting.active-set.target" -> PowerliftingRect(8, 42, 176, 76)
        "powerlifting.active-set.previous" -> PowerliftingRect(8, 122, 176, 16)
        "powerlifting.active-set.complete" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.weight-editor.title" -> PowerliftingRect(8, 5, 176, 18)
        "powerlifting.weight-editor.value" -> PowerliftingRect(8, 28, 176, 90)
        "powerlifting.weight-editor.plates" -> PowerliftingRect(8, 122, 176, 16)
        "powerlifting.weight-editor.done" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.set-result.summary" -> PowerliftingRect(8, 4, 176, 36)
        "powerlifting.set-result.reps" -> PowerliftingRect(8, 40, 176, 48)
        "powerlifting.set-result.rpe" -> PowerliftingRect(8, 90, 176, 48)
        "powerlifting.set-result.save" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.rest.label" -> PowerliftingRect(8, 2, 176, 16)
        "powerlifting.rest.time" -> PowerliftingRect(8, 18, 176, 50)
        "powerlifting.rest.progress" -> PowerliftingRect(24, 70, 144, 5)
        "powerlifting.rest.next" -> PowerliftingRect(8, 78, 176, 54)
        "powerlifting.rest.controls" -> PowerliftingRect(8, 136, 176, 48)
        "powerlifting.plate-loading.total" -> PowerliftingRect(8, 4, 176, 36)
        "powerlifting.plate-loading.side" -> PowerliftingRect(8, 40, 176, 16)
        "powerlifting.plate-loading.diagram" -> PowerliftingRect(8, 58, 176, 80)
        "powerlifting.plate-loading.ready" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.exercise-switcher.count" -> PowerliftingRect(8, 4, 176, 20)
        "powerlifting.exercise-switcher.squat" -> PowerliftingRect(8, 28, 176, 48)
        "powerlifting.exercise-switcher.bench" -> PowerliftingRect(8, 80, 176, 48)
        "powerlifting.exercise-switcher.deadlift" -> PowerliftingRect(8, 132, 176, 48)
        "powerlifting.missed-set.label" -> PowerliftingRect(8, 2, 176, 18)
        "powerlifting.missed-set.actual" -> PowerliftingRect(8, 24, 176, 66)
        "powerlifting.missed-set.options" -> PowerliftingRect(8, 92, 176, 48)
        "powerlifting.missed-set.next" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.summary.title" -> PowerliftingRect(8, 4, 176, 18)
        "powerlifting.summary.metrics" -> PowerliftingRect(8, 28, 176, 58)
        "powerlifting.summary.pr" -> PowerliftingRect(8, 92, 176, 46)
        "powerlifting.summary.done" -> PowerliftingRect(8, 140, 176, 48)
        "powerlifting.resume.label" -> PowerliftingRect(8, 2, 176, 18)
        "powerlifting.resume.state" -> PowerliftingRect(8, 24, 176, 68)
        "powerlifting.resume.action" -> PowerliftingRect(8, 96, 176, 48)
        "powerlifting.resume.discard" -> PowerliftingRect(8, 144, 176, 48)
        else -> error("No Powerlifting bounds for $id")
    }

private fun Modifier.powerliftingBounds(node: SceneNode): Modifier {
    val bounds = powerliftingRect(node.id)
    return offset(x = bounds.x.dp, y = bounds.y.dp)
        .width(bounds.width.dp)
        .height(bounds.height.dp)
}

@Composable
private fun SquarePowerliftingSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(PowerliftingBackground),
    ) {
        children.forEach { node ->
            when (node.kind) {
                "text" -> PowerliftingText(node, context)
                "progress" -> PowerliftingProgress(node, context)
                "card" -> PowerliftingCard(node, context)
                "stepper" -> PowerliftingStepper(node, context)
                "row" -> PowerliftingRow(node, context)
                "button" -> PowerliftingButton(node, context)
                else -> error("Unsupported Powerlifting node ${node.kind}")
            }
        }
    }
}

@Composable
private fun PowerliftingText(node: SceneNode, context: RenderContext) {
    val isNumeral = node.props.variant == "numeral"
    val isSuccess = node.id == "powerlifting.summary.title"
    val isError = node.id == "powerlifting.missed-set.label"
    Text(
        text = requireNotNull(node.props.primaryText),
        modifier =
            Modifier
                .powerliftingBounds(node)
                .appSpecNode(node, context.evidenceCollector),
        color =
            when {
                isSuccess -> PowerliftingSuccess
                isError -> PowerliftingError
                isNumeral -> PowerliftingOnSurface
                else -> PowerliftingOnSurfaceVariant
            },
        style =
            when {
                node.id == "powerlifting.session.count" ->
                    MaterialTheme.typography.displaySmall.copy(fontSize = 26.sp)
                isNumeral -> MaterialTheme.typography.displayMedium
                node.props.variant == "title" -> MaterialTheme.typography.titleLarge
                else -> MaterialTheme.typography.labelMedium
            },
        textAlign = TextAlign.Center,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
private fun PowerliftingProgress(node: SceneNode, context: RenderContext) {
    LinearProgressIndicator(
        progress = {
            requireNotNull(node.props.value).toFloat() /
                requireNotNull(node.props.maximum).toFloat()
        },
        modifier =
            Modifier
                .powerliftingBounds(node)
                .appSpecNode(node, context.evidenceCollector),
        enabled = node.enabled,
        colors =
            androidx.wear.compose.material3.ProgressIndicatorDefaults
                .colors(
                    indicatorColor = PowerliftingPrimary,
                    trackColor = PowerliftingSurfaceHigh,
                ),
    )
}

@Composable
private fun PowerliftingCard(node: SceneNode, context: RenderContext) {
    val action = node.action("tap")
    val isHero =
        node.id in
            setOf(
                "powerlifting.today.hero",
                "powerlifting.active-set.target",
                "powerlifting.missed-set.actual",
                "powerlifting.resume.state",
            )
    val isPlateDiagram = node.id == "powerlifting.plate-loading.diagram"
    val isPr = node.id == "powerlifting.summary.pr"
    val bounds = powerliftingRect(node.id)
    val container =
        when {
            isPr -> Color(0xFF123F3C)
            node.props.tone == "error" -> Color(0xFF3B1F31)
            node.props.tone == "primary" -> PowerliftingSurfaceHigh
            else -> PowerliftingSurface
        }
    val modifier =
        Modifier
            .powerliftingBounds(node)
            .clip(RoundedCornerShape(if (isHero) 15.dp else 11.dp))
            .background(container)
            .then(
                if (action == null) {
                    Modifier
                } else {
                    Modifier.clickable(
                        enabled = node.enabled,
                        onClick = { context.dispatch(node, action) },
                    )
                },
            )
            .appSpecNode(node, context.evidenceCollector)

    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        when {
            bounds.height <= 30 -> {
                Text(
                    text =
                        if (node.id == "powerlifting.today.volume") {
                            "14 SETS  /  3 LIFTS"
                        } else {
                            requireNotNull(node.props.primaryText)
                        },
                    color = PowerliftingOnSurface,
                    fontSize = if (node.id == "powerlifting.today.volume") 14.sp else 11.sp,
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            isPlateDiagram -> PowerliftingPlateDiagram()
            node.id == "powerlifting.active-set.target" -> {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = "140",
                        color = PowerliftingPrimary,
                        fontSize = 46.sp,
                        lineHeight = 46.sp,
                    )
                    Text(
                        text = "KG   X 5",
                        color = PowerliftingPrimary,
                        style = MaterialTheme.typography.titleMedium,
                    )
                }
            }
            node.id == "powerlifting.missed-set.actual" -> {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = "140 KG  X  3",
                        color = PowerliftingOnSurface,
                        fontSize = 30.sp,
                        maxLines = 1,
                    )
                    Text(
                        text = requireNotNull(node.props.secondaryText),
                        color = PowerliftingOnSurfaceVariant,
                        style = MaterialTheme.typography.labelSmall,
                    )
                }
            }
            node.id == "powerlifting.today.hero" -> {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = "HEAVY",
                        color = PowerliftingPrimary,
                        fontSize = 31.sp,
                        lineHeight = 31.sp,
                    )
                    Text(
                        text = "DAY",
                        color = PowerliftingPrimary,
                        fontSize = 31.sp,
                        lineHeight = 31.sp,
                    )
                }
            }
            node.id == "powerlifting.resume.state" -> {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = requireNotNull(node.props.secondaryText).substringBefore(" / SAVED"),
                        color = PowerliftingOnSurfaceVariant,
                        style = MaterialTheme.typography.labelMedium,
                    )
                    Text(
                        text = "142.5 KG  X 5",
                        color = PowerliftingPrimary,
                        fontSize = 24.sp,
                        maxLines = 1,
                    )
                    Text(
                        text = "SAVED 24 SEC AGO",
                        color = PowerliftingSuccess,
                        style = MaterialTheme.typography.labelSmall,
                    )
                }
            }
            else -> {
                Column(
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 3.dp),
                    verticalArrangement = Arrangement.Center,
                ) {
                    Text(
                        text = requireNotNull(node.props.primaryText),
                        color = if (isPr) PowerliftingSuccess else PowerliftingOnSurface,
                        style =
                            if (isHero) MaterialTheme.typography.titleLarge
                            else MaterialTheme.typography.titleSmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        text = requireNotNull(node.props.secondaryText),
                        color = PowerliftingOnSurfaceVariant,
                        style = MaterialTheme.typography.labelSmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}

@Composable
private fun PowerliftingStepper(node: SceneNode, context: RenderContext) {
    val value = requireNotNull(node.props.value)
    val action = node.action("value_committed")
        ?: error("Powerlifting stepper requires valueCommitted")
    val isWeight = node.id.endsWith("weight-editor.value")
    Box(
        modifier =
            Modifier
                .powerliftingBounds(node)
                .clip(RoundedCornerShape(14.dp))
                .background(PowerliftingSurface)
                .appSpecNode(node, context.evidenceCollector),
    ) {
        Text(
            text = if (isWeight) "$value.0" else value.toString(),
            modifier = Modifier.align(Alignment.Center),
            color = PowerliftingPrimary,
            fontSize = if (isWeight) 46.sp else 40.sp,
            maxLines = 1,
        )
        Text(
            text = requireNotNull(node.props.secondaryText).uppercase(),
            modifier = Modifier.align(Alignment.CenterEnd).padding(end = 10.dp),
            color = PowerliftingOnSurfaceVariant,
            style = MaterialTheme.typography.labelMedium,
        )
        CompactButton(
            onClick = {
                context.dispatch(
                    node,
                    action,
                    ReferenceActionPayload.Number(value - requireNotNull(node.props.step)),
                )
            },
            modifier = Modifier.align(Alignment.BottomStart).size(40.dp),
            colors = ButtonDefaults.filledTonalButtonColors(),
            label = { Text("−") },
        )
        CompactButton(
            onClick = {
                context.dispatch(
                    node,
                    action,
                    ReferenceActionPayload.Number(value + requireNotNull(node.props.step)),
                )
            },
            modifier = Modifier.align(Alignment.BottomEnd).size(40.dp),
            colors = ButtonDefaults.filledTonalButtonColors(),
            label = { Text("+") },
        )
    }
}

@Composable
private fun PowerliftingRow(node: SceneNode, context: RenderContext) {
    val children = context.snapshot.childrenOf(node).filter { it.visible }
    if (node.id == "powerlifting.set-result.rpe") {
        Box(
            modifier =
                Modifier
                    .powerliftingBounds(node)
                    .appSpecNode(node, context.evidenceCollector),
        ) {
            children.forEachIndexed { index, child ->
                NotificationActionButton(
                    node = child,
                    context = context,
                    modifier =
                        Modifier
                            .offset(x = (index * 43).dp)
                            .width(48.dp)
                            .fillMaxHeight(),
                )
            }
        }
        return
    }
    Row(
        modifier =
            Modifier
                .powerliftingBounds(node)
                .appSpecNode(node, context.evidenceCollector),
        horizontalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        children.forEach { child ->
            if (child.kind == "button") {
                NotificationActionButton(
                    node = child,
                    context = context,
                    modifier =
                        Modifier
                            .weight(1f)
                            .fillMaxHeight(),
                )
            } else {
                Box(
                    modifier =
                        Modifier
                            .weight(1f)
                            .fillMaxHeight()
                            .clip(RoundedCornerShape(10.dp))
                            .background(PowerliftingSurface)
                            .appSpecNode(child, context.evidenceCollector),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = requireNotNull(child.props.primaryText),
                        color = PowerliftingOnSurface,
                        style = MaterialTheme.typography.labelMedium,
                        textAlign = TextAlign.Center,
                        maxLines = 2,
                    )
                }
            }
        }
    }
}

@Composable
private fun PowerliftingButton(node: SceneNode, context: RenderContext) {
    if (node.props.variant == "text") {
        val tap = node.action("tap") ?: error("Powerlifting text action requires tap")
        Text(
            text = requireNotNull(node.props.primaryText),
            modifier =
                Modifier
                    .powerliftingBounds(node)
                    .clickable(enabled = node.enabled) { context.dispatch(node, tap) }
                    .appSpecNode(node, context.evidenceCollector),
            color = PowerliftingError,
            style = MaterialTheme.typography.labelSmall,
            textAlign = TextAlign.Center,
        )
        return
    }
    NotificationActionButton(
        node = node,
        context = context,
        modifier = Modifier.powerliftingBounds(node),
    )
}

@Composable
private fun PowerliftingPlateDiagram() {
    Canvas(modifier = Modifier.fillMaxSize().padding(10.dp)) {
        val centerY = size.height * 0.52f
        drawRoundRect(
            color = PowerliftingOnSurfaceVariant.copy(alpha = 0.6f),
            topLeft = Offset(size.width * 0.08f, centerY - 4f),
            size = Size(size.width * 0.84f, 8f),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(3f, 3f),
        )
        val colors =
            listOf(
                PowerliftingAmber,
                Color(0xFFE55D5D),
                Color(0xFFE55D5D),
                Color(0xFF74A9E8),
                PowerliftingSuccess,
            )
        colors.forEachIndexed { index, color ->
            val plateWidth = if (index in 1..2) 11f else 6f
            val plateHeight = 28f - index * 3f
            val left = size.width * 0.5f - 8f - index * 11f
            val right = size.width * 0.5f + 8f + index * 11f - plateWidth
            drawRoundRect(
                color = color,
                topLeft = Offset(left, centerY - plateHeight / 2f),
                size = Size(plateWidth, plateHeight),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(2f, 2f),
            )
            drawRoundRect(
                color = color,
                topLeft = Offset(right, centerY - plateHeight / 2f),
                size = Size(plateWidth, plateHeight),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(2f, 2f),
            )
        }
    }
}

@Composable
private fun SquareWeatherCurrentSurface(context: RenderContext) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }

    val locationIcon = node("weather.location-icon")
    val location = node("weather.location")
    val summary = node("weather.summary")
    val conditionIcon = node("weather.condition-icon")
    val condition = node("weather.symbol")
    val highIcon = node("weather.high-icon")
    val high = node("weather.high")
    val lowIcon = node("weather.low-icon")
    val low = node("weather.low")
    val feelsIcon = node("weather.feels-icon")
    val feelsLabel = node("weather.feels-label")
    val feels = node("weather.feels")
    val statusChip = node("weather.status-chip")
    val status = node("weather.status")
    val action = node("weather.primary")
    val tap = action.action("tap")
    val largeText = LocalDensity.current.fontScale >= 1.2f

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background),
    ) {
        Row(
            modifier =
                Modifier
                    .offset(x = 8.dp, y = 6.4.dp)
                    .width(128.dp)
                    .height(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(locationIcon.props.icon)),
                modifier =
                    Modifier
                        .size(10.4.dp)
                        .appSpecNode(locationIcon, context.evidenceCollector),
                contentDescription = locationIcon.semantics.label,
            )
            Text(
                text = requireNotNull(location.props.primaryText),
                modifier =
                    Modifier
                        .weight(1f)
                        .appSpecNode(location, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 148.8.dp, y = 5.6.dp)
                    .width(38.4.dp)
                    .height(17.6.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.secondaryContainer)
                    .appSpecNode(statusChip, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            Canvas(Modifier.size(4.8.dp)) {
                drawCircle(weatherColor(WeatherColorRole.Fresh))
            }
            Spacer(Modifier.width(3.2.dp))
            Text(
                text = "Now",
                modifier = Modifier.appSpecNode(status, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall,
                maxLines = 1,
            )
        }

        Text(
            text = requireNotNull(summary.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 8.dp, y = 21.6.dp)
                    .width(107.2.dp)
                    .height(60.8.dp)
                    .appSpecNode(summary, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onBackground,
            style =
                if (largeText) {
                    MaterialTheme.typography.numeralLarge.copy(
                        fontSize = 41.85.sp,
                        lineHeight = 44.92.sp,
                    )
                } else {
                    MaterialTheme.typography.numeralLarge
                },
            maxLines = 1,
            overflow = TextOverflow.Clip,
            textAlign = TextAlign.Start,
        )

        Box(
            modifier =
                Modifier
                    .offset(x = 110.4.dp, y = 22.4.dp)
                    .width(72.dp)
                    .height(60.dp)
                    .appSpecNode(conditionIcon, context.evidenceCollector),
            contentAlignment = Alignment.Center,
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(conditionIcon.props.icon)),
                modifier = Modifier.fillMaxSize().scale(1.45f),
                contentDescription = conditionIcon.semantics.label,
            )
        }

        Text(
            text = requireNotNull(condition.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 9.6.dp, y = 80.8.dp)
                    .width(172.8.dp)
                    .height(17.6.dp)
                    .appSpecNode(condition, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onBackground,
            style = MaterialTheme.typography.bodySmall,
            maxLines = 1,
        )

        WeatherMetricPill(
            icon = highIcon,
            label = high,
            x = 8.dp,
            context = context,
        )
        WeatherMetricPill(
            icon = lowIcon,
            label = low,
            x = 98.4.dp,
            context = context,
        )

        Row(
            modifier =
                Modifier
                    .offset(x = 8.dp, y = 127.2.dp)
                    .width(176.dp)
                    .height(19.2.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surfaceContainerLow)
                    .padding(horizontal = 8.dp)
                    .appSpecNode(node("weather.feels-pill"), context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(feelsIcon.props.icon)),
                modifier =
                    Modifier
                        .size(12.8.dp)
                        .appSpecNode(feelsIcon, context.evidenceCollector),
                contentDescription = feelsIcon.semantics.label,
            )
            Spacer(Modifier.width(5.6.dp))
            Text(
                text = requireNotNull(feelsLabel.props.primaryText),
                modifier =
                    Modifier.appSpecNode(feelsLabel, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelSmall.copy(fontSize = 9.6.sp),
                maxLines = 1,
            )
            Spacer(Modifier.weight(1f))
            Text(
                text = requireNotNull(feels.props.primaryText),
                modifier = Modifier.appSpecNode(feels, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onBackground,
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
            )
        }

        Box(
            modifier =
                Modifier
                    .offset(y = 144.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .clickable(
                        enabled = action.enabled && tap != null,
                        onClick = { tap?.let { context.dispatch(action, it) } },
                    )
                    .appSpecNode(action, context.evidenceCollector),
            contentAlignment = Alignment.Center,
        ) {
            Button(
                onClick = { tap?.let { context.dispatch(action, it) } },
                enabled = action.enabled && tap != null,
                modifier =
                    Modifier
                        .offset(y = 6.4.dp)
                        .width(179.2.dp)
                        .height(28.8.dp),
                colors = ButtonDefaults.buttonColors(),
                contentPadding = PaddingValues(horizontal = 14.4.dp),
                label = {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = requireNotNull(action.props.primaryText),
                            modifier = Modifier.weight(1f),
                            color = MaterialTheme.colorScheme.onPrimary,
                            style = MaterialTheme.typography.bodySmall,
                            maxLines = 1,
                            textAlign = TextAlign.Start,
                        )
                        WeatherGlyph(
                            icon = WeatherIcons.fromWireName("utility_chevron_right"),
                            modifier = Modifier.size(16.dp),
                        )
                    }
                },
            )
        }

        // Structural containers and the off-screen hourly page remain part of
        // the shared semantic snapshot even though this exact-grid oracle
        // paints the selected page directly.
        Box(
            Modifier
                .fillMaxSize()
                .appSpecNode(node("weather.current"), context.evidenceCollector),
        )
        listOf(
            "weather.location-row",
            "weather.hero-row",
            "weather.high-low",
            "weather.high-row",
            "weather.low-row",
            "weather.feels-row",
            "weather.status-row",
        ).forEach { id ->
            Box(
                Modifier
                    .size(1.dp)
                    .appSpecNode(node(id), context.evidenceCollector),
            )
        }
    }
}

@Composable
private fun WeatherMetricPill(
    icon: SceneNode,
    label: SceneNode,
    x: androidx.compose.ui.unit.Dp,
    context: RenderContext,
) {
    Row(
        modifier =
            Modifier
                .offset(x = x, y = 100.8.dp)
                .width(85.6.dp)
                .height(24.dp)
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.secondaryContainer)
                .appSpecNode(
                    context.snapshot.nodes.single {
                        it.id == if (label.id == "weather.high") {
                            "weather.high-pill"
                        } else {
                            "weather.low-pill"
                        }
                    },
                    context.evidenceCollector,
                ),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        WeatherGlyph(
            icon = WeatherIcons.fromWireName(requireNotNull(icon.props.icon)),
            modifier =
                Modifier
                    .size(14.4.dp)
                    .appSpecNode(icon, context.evidenceCollector),
            contentDescription = icon.semantics.label,
        )
        Spacer(Modifier.width(3.2.dp))
        Text(
            text = requireNotNull(label.props.primaryText),
            modifier = Modifier.appSpecNode(label, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onBackground,
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
        )
    }
}

@Composable
private fun SquareWeatherHourlySurface(context: RenderContext) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }

    val summary = node("weather.hourly-summary")
    val conditionIcon = node("weather.hourly-condition-icon")
    val summaryCopy = node("weather.hourly-summary-copy")
    val now = node("weather.hourly-now")
    val condition = node("weather.hourly-condition")
    val statusChip = node("weather.hourly-status-chip")
    val status = node("weather.hourly-status")
    val chartCard = node("weather.hourly-chart-card")
    val chartHeading = node("weather.hourly-chart-heading")
    val rainIcon = node("weather.hourly-rain-icon")
    val rainLabel = node("weather.hourly-rain-label")
    val rainValue = node("weather.hourly-rain-value")
    val chart = node("weather.rain-chart")
    val times = node("weather.hourly-times")
    val tiles = node("weather.hourly-tiles")
    val action = node("weather.daily-action")
    val tap = action.action("tap")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background),
    ) {
        Row(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 4.dp)
                    .width(123.2.dp)
                    .height(34.4.dp)
                    .appSpecNode(summary, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.6.dp),
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(conditionIcon.props.icon)),
                modifier =
                    Modifier
                        .width(32.dp)
                        .height(27.2.dp)
                        .scale(1.4f)
                        .appSpecNode(conditionIcon, context.evidenceCollector),
                contentDescription = conditionIcon.semantics.label,
            )
            Column(
                modifier =
                    Modifier
                        .weight(1f)
                        .appSpecNode(summaryCopy, context.evidenceCollector),
                verticalArrangement = Arrangement.Center,
            ) {
                Text(
                    text = requireNotNull(now.props.primaryText),
                    modifier = Modifier.appSpecNode(now, context.evidenceCollector),
                    color = MaterialTheme.colorScheme.onBackground,
                    style = MaterialTheme.typography.bodySmall.copy(fontSize = 16.8.sp),
                    maxLines = 1,
                )
                Text(
                    text = requireNotNull(condition.props.primaryText),
                    modifier = Modifier.appSpecNode(condition, context.evidenceCollector),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyExtraSmall,
                    maxLines = 1,
                )
            }
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 148.8.dp, y = 5.6.dp)
                    .width(38.4.dp)
                    .height(17.6.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.secondaryContainer)
                    .appSpecNode(statusChip, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            Canvas(Modifier.size(4.8.dp)) {
                drawCircle(weatherColor(WeatherColorRole.Fresh))
            }
            Spacer(Modifier.width(3.2.dp))
            Text(
                text = requireNotNull(status.props.primaryText),
                modifier = Modifier.appSpecNode(status, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall,
                maxLines = 1,
            )
        }

        Box(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 41.6.dp)
                    .width(179.2.dp)
                    .height(46.4.dp)
                    .clip(RoundedCornerShape(14.4.dp))
                    .background(MaterialTheme.colorScheme.surfaceContainer)
                    .appSpecNode(chartCard, context.evidenceCollector),
        ) {
            Row(
                modifier =
                    Modifier
                        .offset(x = 6.4.dp, y = 3.2.dp)
                        .width(166.4.dp)
                        .height(13.6.dp)
                        .appSpecNode(chartHeading, context.evidenceCollector),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                WeatherGlyph(
                    icon = WeatherIcons.fromWireName(requireNotNull(rainIcon.props.icon)),
                    modifier =
                        Modifier
                            .size(11.2.dp)
                            .appSpecNode(rainIcon, context.evidenceCollector),
                    contentDescription = rainIcon.semantics.label,
                )
                Spacer(Modifier.width(3.2.dp))
                Text(
                    text = requireNotNull(rainLabel.props.primaryText),
                    modifier = Modifier.appSpecNode(rainLabel, context.evidenceCollector),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyExtraSmall,
                    maxLines = 1,
                )
                Spacer(Modifier.weight(1f))
                Text(
                    text = requireNotNull(rainValue.props.primaryText),
                    modifier = Modifier.appSpecNode(rainValue, context.evidenceCollector),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = MaterialTheme.typography.labelSmall.copy(fontSize = 9.6.sp),
                    maxLines = 1,
                )
            }

            Canvas(
                Modifier
                    .offset(x = 6.4.dp, y = 16.8.dp)
                    .width(166.4.dp)
                    .height(19.2.dp)
                    .appSpecNode(chart, context.evidenceCollector),
            ) {
                val lineY = size.height * 0.64f
                val rain = weatherColor(WeatherColorRole.Rain)
                drawLine(
                    color = weatherColor(WeatherColorRole.OutlineVariant),
                    start = Offset(0f, lineY),
                    end = Offset(size.width, lineY),
                    strokeWidth = 1f,
                )
                drawLine(
                    color = rain,
                    start = Offset(0f, lineY),
                    end = Offset(size.width, lineY),
                    strokeWidth = 2f,
                )
                listOf(0f, size.width / 3f, size.width * 2f / 3f, size.width)
                    .forEach { x -> drawCircle(rain, radius = 2.5f, center = Offset(x, lineY)) }
            }

            Row(
                modifier =
                    Modifier
                        .offset(x = 6.4.dp, y = 37.6.dp)
                        .width(166.4.dp)
                        .height(6.4.dp)
                        .appSpecNode(times, context.evidenceCollector),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                listOf(
                    node("weather.hourly-time-now"),
                    node("weather.hourly-time-20"),
                    node("weather.hourly-time-40"),
                    node("weather.hourly-time-60"),
                ).forEach { time ->
                    Text(
                        text = requireNotNull(time.props.primaryText),
                        modifier = Modifier.appSpecNode(time, context.evidenceCollector),
                        color = weatherColor(WeatherColorRole.OutlineVariant),
                        style = MaterialTheme.typography.bodyExtraSmall.copy(fontSize = 6.4.sp),
                        maxLines = 1,
                    )
                }
            }
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 92.8.dp)
                    .width(179.2.dp)
                    .height(46.4.dp)
                    .appSpecNode(tiles, context.evidenceCollector),
            horizontalArrangement = Arrangement.spacedBy(4.8.dp),
        ) {
            WeatherHourlyTile("now", selected = true, context = context)
            WeatherHourlyTile("10", selected = false, context = context)
            WeatherHourlyTile("11", selected = false, context = context)
            WeatherHourlyTile("12", selected = false, context = context)
        }

        Box(
            modifier =
                Modifier
                    .offset(y = 144.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .clickable(
                        enabled = action.enabled && tap != null,
                        onClick = { tap?.let { context.dispatch(action, it) } },
                    )
                    .appSpecNode(action, context.evidenceCollector),
            contentAlignment = Alignment.Center,
        ) {
            Button(
                onClick = { tap?.let { context.dispatch(action, it) } },
                enabled = action.enabled && tap != null,
                modifier =
                    Modifier
                        .offset(y = 6.4.dp)
                        .width(179.2.dp)
                        .height(28.8.dp),
                colors =
                    ButtonDefaults.filledTonalButtonColors(
                        containerColor = MaterialTheme.colorScheme.secondaryContainer,
                        contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                    ),
                contentPadding = PaddingValues(horizontal = 14.4.dp),
                label = {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = requireNotNull(action.props.primaryText),
                            modifier = Modifier.weight(1f),
                            style = MaterialTheme.typography.bodySmall,
                            maxLines = 1,
                        )
                        WeatherGlyph(
                            icon = WeatherIcons.fromWireName("utility_chevron_right"),
                            modifier = Modifier.size(16.dp),
                        )
                    }
                },
            )
        }

        Box(Modifier.fillMaxSize().appSpecNode(node("weather.hourly"), context.evidenceCollector))
    }
}

@Composable
private fun RowScope.WeatherHourlyTile(
    suffix: String,
    selected: Boolean,
    context: RenderContext,
) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }
    val prefix = "weather.hour-$suffix"
    val tile = node("$prefix-tile")
    val column = node(prefix)
    val label = node("$prefix-label")
    val icon = node("$prefix-icon")
    val temperature = node("$prefix-temp")
    Box(
        modifier =
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .clip(RoundedCornerShape(13.6.dp))
                .background(
                    if (selected) {
                        MaterialTheme.colorScheme.primaryContainer
                    } else {
                        MaterialTheme.colorScheme.surfaceContainerLow
                    },
                )
                .appSpecNode(tile, context.evidenceCollector),
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxSize()
                    .appSpecNode(column, context.evidenceCollector),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Text(
                text = requireNotNull(label.props.primaryText),
                modifier = Modifier.appSpecNode(label, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall.copy(fontSize = 7.2.sp),
                maxLines = 1,
            )
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(icon.props.icon)),
                modifier =
                    Modifier
                        .width(19.2.dp)
                        .height(15.2.dp)
                        .scale(2.05f)
                        .appSpecNode(icon, context.evidenceCollector),
                contentDescription = icon.semantics.label,
            )
            Text(
                text = requireNotNull(temperature.props.primaryText),
                modifier = Modifier.appSpecNode(temperature, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurface,
                style = MaterialTheme.typography.bodySmall.copy(fontSize = 13.6.sp),
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun SquareWeatherDailySurface(context: RenderContext) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }

    val page = node("weather.daily-page")
    val locationRow = node("weather.daily-location-row")
    val locationIcon = node("weather.daily-location-icon")
    val location = node("weather.daily-location")
    val statusChip = node("weather.daily-status-chip")
    val status = node("weather.daily-status")
    val list = node("weather.daily-list")
    val dots = node("weather.daily-dots")
    val action = node("weather.details-action")
    val tap = action.action("tap")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .appSpecNode(page, context.evidenceCollector),
    ) {
        Row(
            modifier =
                Modifier
                    .offset(x = 8.dp, y = 6.4.dp)
                    .width(128.dp)
                    .height(16.dp)
                    .appSpecNode(locationRow, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(locationIcon.props.icon)),
                modifier =
                    Modifier
                        .size(10.4.dp)
                        .appSpecNode(locationIcon, context.evidenceCollector),
                contentDescription = locationIcon.semantics.label,
            )
            Text(
                text = requireNotNull(location.props.primaryText),
                modifier = Modifier.weight(1f).appSpecNode(location, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 148.8.dp, y = 5.6.dp)
                    .width(38.4.dp)
                    .height(17.6.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.secondaryContainer)
                    .appSpecNode(statusChip, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            Canvas(Modifier.size(4.8.dp)) {
                drawCircle(weatherColor(WeatherColorRole.Fresh))
            }
            Spacer(Modifier.width(3.2.dp))
            Text(
                text = requireNotNull(status.props.primaryText),
                modifier = Modifier.appSpecNode(status, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall,
                maxLines = 1,
            )
        }

        Column(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 30.4.dp)
                    .width(179.2.dp)
                    .height(149.6.dp)
                    .appSpecNode(list, context.evidenceCollector),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            WeatherDailyRow("today", selected = true, context = context)
            WeatherDailyRow("mon", selected = false, context = context)
            WeatherDailyRow("tue", selected = false, context = context)
            WeatherDailyRow("wed", selected = false, context = context)
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 78.4.dp, y = 184.dp)
                    .width(35.2.dp)
                    .height(4.8.dp)
                    .appSpecNode(dots, context.evidenceCollector),
            horizontalArrangement = Arrangement.spacedBy(3.2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            listOf(
                "weather.daily-dot-current",
                "weather.daily-dot-hourly",
                "weather.daily-dot-selected",
                "weather.daily-dot-details",
            ).forEach { id ->
                val dot = node(id)
                Box(
                    Modifier
                        .width(if (id == "weather.daily-dot-selected") 6.4.dp else 3.2.dp)
                        .height(3.2.dp)
                        .clip(CircleShape)
                        .background(
                            if (id == "weather.daily-dot-selected") {
                                MaterialTheme.colorScheme.primary
                            } else {
                                weatherColor(WeatherColorRole.OutlineVariant)
                            },
                        )
                        .appSpecNode(dot, context.evidenceCollector),
                )
            }
        }

        Box(
            modifier =
                Modifier
                    .offset(y = 144.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .clickable(
                        enabled = action.enabled && tap != null,
                        onClick = { tap?.let { context.dispatch(action, it) } },
                    )
                    .appSpecNode(action, context.evidenceCollector),
        )
    }
}

@Composable
private fun ColumnScope.WeatherDailyRow(
    suffix: String,
    selected: Boolean,
    context: RenderContext,
) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }
    val prefix = "weather.day-$suffix"
    val tile = node("$prefix-tile")
    val row = node(prefix)
    val label = node("$prefix-label")
    val icon = node("$prefix-icon")
    val low = node("$prefix-low")
    val high = node("$prefix-high")
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .height(34.4.dp)
                .clip(RoundedCornerShape(14.4.dp))
                .background(
                    if (selected) {
                        MaterialTheme.colorScheme.primaryContainer
                    } else {
                        MaterialTheme.colorScheme.surfaceContainerLow
                    },
                )
                .appSpecNode(tile, context.evidenceCollector),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(horizontal = 9.6.dp)
                    .appSpecNode(row, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = requireNotNull(label.props.primaryText),
                modifier = Modifier.width(54.4.dp).appSpecNode(label, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurface,
                style = MaterialTheme.typography.labelSmall.copy(fontSize = 12.sp),
                maxLines = 1,
            )
            Box(
                modifier = Modifier.width(36.8.dp),
                contentAlignment = Alignment.Center,
            ) {
                WeatherGlyph(
                    icon = WeatherIcons.fromWireName(requireNotNull(icon.props.icon)),
                    modifier =
                        Modifier
                            .width(24.dp)
                            .height(20.dp)
                            .scale(1.7f)
                            .appSpecNode(icon, context.evidenceCollector),
                    contentDescription = icon.semantics.label,
                )
            }
            Text(
                text = requireNotNull(low.props.primaryText),
                modifier = Modifier.width(34.4.dp).appSpecNode(low, context.evidenceCollector),
                color =
                    if (selected) {
                        MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                textAlign = TextAlign.End,
            )
            Text(
                text = requireNotNull(high.props.primaryText),
                modifier = Modifier.width(34.4.dp).appSpecNode(high, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurface,
                style = MaterialTheme.typography.bodySmall,
                maxLines = 1,
                textAlign = TextAlign.End,
            )
        }
    }
}

@Composable
private fun SquareWeatherDetailsSurface(context: RenderContext) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }

    val page = node("weather.details-page")
    val summary = node("weather.details-summary")
    val conditionIcon = node("weather.details-condition-icon")
    val temperature = node("weather.details-temperature")
    val condition = node("weather.details-condition")
    val statusChip = node("weather.details-status-chip")
    val status = node("weather.details-status")
    val grid = node("weather.details-grid")
    val dots = node("weather.details-dots")
    val action = node("weather.rain-preview-action")
    val tap = action.action("tap")
    val largeText = LocalDensity.current.fontScale >= 1.2f

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .appSpecNode(page, context.evidenceCollector),
    ) {
        Row(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 3.2.dp)
                    .width(128.dp)
                    .height(32.dp)
                    .appSpecNode(summary, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(conditionIcon.props.icon)),
                modifier =
                    Modifier
                        .width(28.8.dp)
                        .height(24.dp)
                        .appSpecNode(conditionIcon, context.evidenceCollector)
                        .scale(1.7f),
                contentDescription = conditionIcon.semantics.label,
            )
            Spacer(Modifier.width(4.dp))
            Text(
                text = requireNotNull(temperature.props.primaryText),
                modifier =
                    Modifier
                        .width(37.6.dp)
                        .appSpecNode(temperature, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurface,
                style = MaterialTheme.typography.numeralSmall,
                maxLines = 1,
            )
            Text(
                text = requireNotNull(condition.props.primaryText),
                modifier =
                    Modifier
                        .weight(1f)
                        .appSpecNode(condition, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style =
                    MaterialTheme.typography.bodyExtraSmall.copy(
                        fontSize = if (largeText) 6.8.sp else 7.2.sp,
                    ),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 148.8.dp, y = 5.6.dp)
                    .width(38.4.dp)
                    .height(17.6.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.secondaryContainer)
                    .appSpecNode(statusChip, context.evidenceCollector),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            Canvas(Modifier.size(4.8.dp)) {
                drawCircle(weatherColor(WeatherColorRole.Fresh))
            }
            Spacer(Modifier.width(3.2.dp))
            Text(
                text = requireNotNull(status.props.primaryText),
                modifier = Modifier.appSpecNode(status, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall,
                maxLines = 1,
            )
        }

        Column(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 37.6.dp)
                    .width(179.2.dp)
                    .height(147.2.dp)
                    .appSpecNode(grid, context.evidenceCollector),
            verticalArrangement = Arrangement.spacedBy(6.4.dp),
        ) {
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(70.4.dp)
                        .appSpecNode(node("weather.details-row-top"), context.evidenceCollector),
                horizontalArrangement = Arrangement.spacedBy(6.4.dp),
            ) {
                WeatherDetailsTile("humidity", Modifier.weight(1f), context)
                WeatherDetailsTile("wind", Modifier.weight(1f), context)
            }
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(70.4.dp)
                        .appSpecNode(node("weather.details-row-bottom"), context.evidenceCollector),
                horizontalArrangement = Arrangement.spacedBy(6.4.dp),
            ) {
                WeatherDetailsTile("uv", Modifier.weight(1f), context)
                WeatherDetailsTile("sunrise", Modifier.weight(1f), context)
            }
        }

        Row(
            modifier =
                Modifier
                    .offset(x = 78.4.dp, y = 184.dp)
                    .width(35.2.dp)
                    .height(4.8.dp)
                    .appSpecNode(dots, context.evidenceCollector),
            horizontalArrangement = Arrangement.spacedBy(3.2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            listOf(
                "weather.details-dot-current",
                "weather.details-dot-hourly",
                "weather.details-dot-daily",
                "weather.details-dot-selected",
            ).forEach { id ->
                val dot = node(id)
                Box(
                    Modifier
                        .width(if (id == "weather.details-dot-selected") 6.4.dp else 3.2.dp)
                        .height(3.2.dp)
                        .clip(CircleShape)
                        .background(
                            if (id == "weather.details-dot-selected") {
                                MaterialTheme.colorScheme.primary
                            } else {
                                weatherColor(WeatherColorRole.OutlineVariant)
                            },
                        )
                        .appSpecNode(dot, context.evidenceCollector),
                )
            }
        }

        Box(
            modifier =
                Modifier
                    .offset(y = 144.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .clickable(
                        enabled = action.enabled && tap != null,
                        onClick = { tap?.let { context.dispatch(action, it) } },
                    )
                    .appSpecNode(action, context.evidenceCollector),
        )
    }
}

@Composable
private fun RowScope.WeatherDetailsTile(
    suffix: String,
    modifier: Modifier,
    context: RenderContext,
) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }
    val tile = node("weather.$suffix-tile")
    val group = node("weather.$suffix")
    val icon = node("weather.$suffix-icon")
    val label = node("weather.$suffix-label")
    val value = node("weather.$suffix-value")
    val unit = context.snapshot.nodes.singleOrNull { it.id == "weather.$suffix-unit" }
    val selected = suffix == "uv"
    val largeText = LocalDensity.current.fontScale >= 1.2f
    val shape =
        when (suffix) {
            "humidity" -> RoundedCornerShape(18.4.dp, 12.8.dp, 18.4.dp, 12.8.dp)
            "wind" -> RoundedCornerShape(12.8.dp, 22.4.dp, 12.8.dp, 22.4.dp)
            "uv" -> CutCornerShape(9.6.dp)
            else -> RoundedCornerShape(22.4.dp, 12.8.dp, 22.4.dp, 12.8.dp)
        }
    val container =
        when (suffix) {
            "humidity" -> weatherColor(WeatherColorRole.SurfaceHigh)
            "wind" -> weatherColor(WeatherColorRole.PrimaryContainer)
            "uv" -> weatherColor(WeatherColorRole.Rain)
            else -> weatherColor(WeatherColorRole.SurfaceHigh)
        }
    val content =
        if (selected) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface

    Box(
        modifier =
            modifier
                .fillMaxHeight()
                .clip(shape)
                .background(container)
                .appSpecNode(tile, context.evidenceCollector),
    ) {
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .appSpecNode(group, context.evidenceCollector),
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(icon.props.icon)),
                modifier =
                    Modifier
                        .offset(x = 7.2.dp, y = 10.4.dp)
                        .size(22.4.dp)
                        .scale(1.35f)
                        .appSpecNode(icon, context.evidenceCollector),
                contentDescription = icon.semantics.label,
            )
            Text(
                text = requireNotNull(label.props.primaryText),
                modifier =
                    Modifier
                        .offset(x = 32.8.dp, y = 10.4.dp)
                        .width(48.dp)
                        .appSpecNode(label, context.evidenceCollector),
                color = if (selected) content.copy(alpha = 0.75f) else MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall.copy(fontSize = 7.2.sp),
                maxLines = 1,
            )
            Text(
                text = requireNotNull(value.props.primaryText),
                modifier =
                    Modifier
                        .offset(
                            x = if (selected && largeText) 12.dp else 31.2.dp,
                            y = if (selected && largeText) 32.dp else 35.2.dp,
                        )
                        .width(if (selected && largeText) 24.dp else 51.2.dp)
                        .height(28.dp)
                        .appSpecNode(value, context.evidenceCollector),
                color = content,
                style = MaterialTheme.typography.numeralSmall,
                maxLines = 1,
            )
            unit?.let {
                Text(
                    text = requireNotNull(it.props.primaryText),
                    modifier =
                        Modifier
                            .offset(
                                x = if (selected && largeText) 20.dp else 49.6.dp,
                                y = if (selected && largeText) 56.dp else 47.2.dp,
                            )
                            .width(if (selected && largeText) 60.dp else 28.8.dp)
                            .appSpecNode(it, context.evidenceCollector),
                    color = if (selected) content.copy(alpha = 0.75f) else MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyExtraSmall.copy(fontSize = 7.2.sp),
                    maxLines = 1,
                    textAlign = if (selected && largeText) TextAlign.End else TextAlign.Start,
                )
            }
        }
    }
}

@Composable
private fun SquareWeatherRainSurface(context: RenderContext) {
    fun node(id: String): SceneNode =
        context.snapshot.nodes.single { it.id == id }

    val page = node("weather.rain-page")
    val hero = node("weather.rain-hero")
    val headline = node("weather.rain-headline")
    val title = node("weather.rain-title")
    val duration = node("weather.rain-duration")
    val card = node("weather.rain-chart-card")
    val probability = node("weather.rain-probability")
    val probabilityIcon = node("weather.rain-probability-icon")
    val probabilityValue = node("weather.rain-probability-value")
    val probabilityLabel = node("weather.rain-probability-label")
    val chart = node("weather.rain-bars")
    val times = node("weather.rain-times")
    val actions = node("weather.rain-actions")
    val details = node("weather.rain-details")
    val detailsTap = details.action("tap")
    val status = node("weather.rain-status")
    val statusTap = status.action("tap")
    val largeText = LocalDensity.current.fontScale >= 1.2f

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .appSpecNode(page, context.evidenceCollector),
    ) {
        Box(
            modifier =
                Modifier
                    .offset(x = 12.dp, y = 4.dp)
                    .width(60.dp)
                    .height(56.dp)
                    .appSpecNode(hero, context.evidenceCollector),
            contentAlignment = Alignment.Center,
        ) {
            WeatherGlyph(
                icon = WeatherIcons.fromWireName(requireNotNull(hero.props.icon)),
                modifier = Modifier.fillMaxSize().scale(1.3f),
                contentDescription = hero.semantics.label,
            )
        }
        Column(
            modifier =
                Modifier
                    .offset(x = 82.4.dp, y = 6.4.dp)
                    .width(102.4.dp)
                    .height(56.dp)
                    .appSpecNode(headline, context.evidenceCollector),
        ) {
            Text(
                text = requireNotNull(title.props.primaryText),
                modifier = Modifier.appSpecNode(title, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurface,
                style =
                    if (largeText) {
                        MaterialTheme.typography.numeralSmall.copy(
                            fontSize = 17.23.sp,
                            lineHeight = 16.62.sp,
                        )
                    } else {
                        MaterialTheme.typography.numeralSmall.copy(lineHeight = 21.6.sp)
                    },
                maxLines = 2,
            )
            Text(
                text = requireNotNull(duration.props.primaryText),
                modifier = Modifier.appSpecNode(duration, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyExtraSmall,
                maxLines = 1,
            )
        }

        Box(
            modifier =
                Modifier
                    .offset(x = 6.4.dp, y = 68.8.dp)
                    .width(179.2.dp)
                    .height(89.6.dp)
                    .clip(RoundedCornerShape(14.4.dp))
                    .background(MaterialTheme.colorScheme.secondaryContainer)
                    .appSpecNode(card, context.evidenceCollector),
        ) {
            Row(
                modifier =
                    Modifier
                        .offset(x = 6.4.dp, y = 4.8.dp)
                        .width(166.4.dp)
                        .height(27.2.dp)
                        .appSpecNode(probability, context.evidenceCollector),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                WeatherGlyph(
                    icon = WeatherIcons.fromWireName(requireNotNull(probabilityIcon.props.icon)),
                    modifier =
                        Modifier
                            .size(20.dp)
                            .appSpecNode(probabilityIcon, context.evidenceCollector),
                    contentDescription = probabilityIcon.semantics.label,
                )
                Spacer(Modifier.width(3.2.dp))
                Text(
                    text = requireNotNull(probabilityValue.props.primaryText),
                    modifier = Modifier.appSpecNode(probabilityValue, context.evidenceCollector),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = MaterialTheme.typography.numeralSmall,
                    maxLines = 1,
                )
                Spacer(Modifier.width(5.6.dp))
                Text(
                    text = requireNotNull(probabilityLabel.props.primaryText),
                    modifier = Modifier.appSpecNode(probabilityLabel, context.evidenceCollector),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyExtraSmall.copy(fontSize = 6.4.sp, lineHeight = 7.2.sp),
                    maxLines = 2,
                )
            }
            Canvas(
                modifier =
                    Modifier
                        .offset(x = 6.4.dp, y = 33.6.dp)
                        .width(166.4.dp)
                        .height(44.dp)
                        .appSpecNode(chart, context.evidenceCollector),
            ) {
                val samples = requireNotNull(chart.props.samples)
                val maximum = requireNotNull(chart.props.maximum).toFloat()
                val gap = size.width / samples.size
                val barWidth = gap * 0.55f
                drawLine(
                    color = weatherColor(WeatherColorRole.OutlineVariant),
                    start = Offset(0f, size.height - 1f),
                    end = Offset(size.width, size.height - 1f),
                    strokeWidth = 1f,
                )
                samples.forEachIndexed { index, sample ->
                    val height = (sample / maximum) * (size.height - 3f)
                    drawRoundRect(
                        color = weatherColor(WeatherColorRole.Rain),
                        topLeft = Offset(index * gap + (gap - barWidth) / 2f, size.height - height),
                        size = Size(barWidth, height),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(barWidth / 2f),
                    )
                }
            }
            Row(
                modifier =
                    Modifier
                        .offset(x = 6.4.dp, y = 80.8.dp)
                        .width(166.4.dp)
                        .height(8.dp)
                        .appSpecNode(times, context.evidenceCollector),
            ) {
                listOf("now", "20", "40", "60").forEach { suffix ->
                    val item = node("weather.rain-time-$suffix")
                    Text(
                        text = requireNotNull(item.props.primaryText),
                        modifier = Modifier.weight(1f).appSpecNode(item, context.evidenceCollector),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = MaterialTheme.typography.bodyExtraSmall.copy(fontSize = 5.6.sp),
                        maxLines = 1,
                        textAlign = if (suffix == "now") TextAlign.Start else if (suffix == "60") TextAlign.End else TextAlign.Center,
                    )
                }
            }
        }

        Row(
            modifier =
                Modifier
                    .offset(y = 144.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .appSpecNode(actions, context.evidenceCollector),
        ) {
            Box(
                modifier =
                    Modifier
                        .width(132.8.dp)
                        .fillMaxHeight()
                        .clickable(
                            enabled = details.enabled && detailsTap != null,
                            onClick = { detailsTap?.let { context.dispatch(details, it) } },
                        )
                        .appSpecNode(details, context.evidenceCollector),
                contentAlignment = Alignment.Center,
            ) {
                Button(
                    onClick = { detailsTap?.let { context.dispatch(details, it) } },
                    modifier =
                        Modifier
                            .offset(y = 6.4.dp)
                            .width(120.dp)
                            .height(28.8.dp),
                    colors =
                        ButtonDefaults.filledTonalButtonColors(
                            containerColor = MaterialTheme.colorScheme.primaryContainer,
                            contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                        ),
                    contentPadding = PaddingValues(horizontal = 9.6.dp),
                    label = {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            WeatherGlyph(
                                icon = WeatherIcons.fromWireName("utility_details"),
                                modifier = Modifier.size(14.4.dp),
                            )
                            Spacer(Modifier.width(6.4.dp))
                            Text("Details", modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodySmall)
                            WeatherGlyph(
                                icon = WeatherIcons.fromWireName("utility_chevron_right"),
                                modifier = Modifier.size(14.4.dp),
                            )
                        }
                    },
                )
            }
            Column(
                modifier =
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clickable(
                            enabled = status.enabled && statusTap != null,
                            onClick = { statusTap?.let { context.dispatch(status, it) } },
                        )
                        .appSpecNode(status, context.evidenceCollector),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                WeatherGlyph(
                    icon = WeatherIcons.fromWireName("utility_clock"),
                    modifier = Modifier.size(15.2.dp),
                )
                Text(
                    text = requireNotNull(status.props.primaryText),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyExtraSmall,
                )
            }
        }
    }
}

@Composable
private fun SquareCanvasGameSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val canvas =
        children.singleOrNull { it.kind == "canvas" }
            ?: error("Canvas game requires one canvas")
    val label =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "label"
        } ?: error("Canvas game requires one score label")
    val score =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "numeral"
        } ?: error("Canvas game requires one score value")
    val controls =
        children.singleOrNull { it.kind == "keypad" }
            ?: error("Canvas game requires one three-key control row")
    val action =
        controls.action("tap")
            ?: error("Canvas game controls require tap")
    val keys = requireNotNull(controls.props.keys)
    check(keys.size == 3 && controls.props.keyColumns == 3) {
        "Canvas game controls must be a single three-key row"
    }

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        AppSpecCanvas(
            node = canvas,
            context = context,
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(128.dp),
        )
        Text(
            text = requireNotNull(label.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 128.dp, y = 14.dp)
                    .width(52.dp)
                    .height(20.dp)
                    .appSpecNode(label, context.evidenceCollector),
            color = Color(0xFF9CE8C2),
            style = MaterialTheme.typography.labelMedium,
            maxLines = 1,
            textAlign = TextAlign.Center,
        )
        Text(
            text = requireNotNull(score.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 128.dp, y = 36.dp)
                    .width(52.dp)
                    .height(52.dp)
                    .appSpecNode(score, context.evidenceCollector),
            color = Color(0xFFA8F279),
            style = MaterialTheme.typography.displaySmall,
            maxLines = 1,
            textAlign = TextAlign.Center,
        )
        ButtonGroup(
            modifier =
                Modifier
                    .offset(y = 132.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .appSpecNode(controls, context.evidenceCollector),
            spacing = 4.dp,
            expansionWidth = 8.dp,
            contentPadding = PaddingValues(0.dp),
        ) {
            keys.forEachIndexed { index, key ->
                CompactButton(
                    onClick = {
                        context.dispatch(
                            controls,
                            action,
                            ReferenceActionPayload.Text(key),
                        )
                    },
                    enabled = controls.enabled,
                    modifier =
                        Modifier
                            .weight(1f)
                            .fillMaxHeight(),
                    colors =
                        if (index == 1) {
                            ButtonDefaults.buttonColors(
                                containerColor = Color(0xFFA8F279),
                                contentColor = Color(0xFF07110D),
                            )
                        } else {
                            ButtonDefaults.filledTonalButtonColors(
                                containerColor = Color(0xFF163026),
                                contentColor = Color(0xFFD5F5E4),
                            )
                        },
                    label = {
                        Text(
                            text = key,
                            style = MaterialTheme.typography.titleSmall,
                        )
                    },
                )
            }
        }
    }
}

@Composable
private fun SquareWorkoutSetSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull { it.kind == "text" }
            ?: error("Square workout set requires one context label")
    val stepper =
        children.singleOrNull { it.kind == "stepper" }
            ?: error("Square workout set requires one weight stepper")
    val metric =
        children.singleOrNull { it.kind == "live_card" }
            ?: error("Square workout set requires one target card")
    val action =
        children.singleOrNull { it.kind == "button" }
            ?: error("Square workout set requires one action")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(20.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        InlineAppSpecStepper(
            node = stepper,
            context = context,
            action =
                stepper.action("value_committed")
                    ?: error("Workout weight requires valueCommitted"),
            current = requireNotNull(stepper.props.value),
            minimum = requireNotNull(stepper.props.minimum),
            maximum = requireNotNull(stepper.props.maximum),
            step = requireNotNull(stepper.props.step),
            modifier =
                Modifier
                    .offset(y = 24.dp)
                    .fillMaxWidth()
                    .height(48.dp),
        )
        WorkoutLiveMetricCard(
            node = metric,
            context = context,
            modifier =
                Modifier
                    .offset(y = 76.dp)
                    .fillMaxWidth()
                    .height(56.dp),
        )
        NotificationActionButton(
            node = action,
            context = context,
            modifier =
                Modifier
                    .offset(x = 32.dp, y = 136.dp)
                    .width(120.dp)
                    .height(48.dp),
        )
    }
}

@Composable
private fun SquareWorkoutRestSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "label"
        } ?: error("Square workout rest requires one context label")
    val time =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "numeral"
        } ?: error("Square workout rest requires one timer")
    val metric =
        children.singleOrNull { it.kind == "live_card" }
            ?: error("Square workout rest requires one recorded-set card")
    val actions = children.filter { it.kind == "button" }
    check(actions.size == 2) {
        "Square workout rest requires two actions"
    }

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(20.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        Text(
            text = requireNotNull(time.props.primaryText),
            modifier =
                Modifier
                    .offset(y = 20.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .appSpecNode(time, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurface,
            style = MaterialTheme.typography.displaySmall,
            maxLines = 1,
            textAlign = TextAlign.Center,
        )
        WorkoutLiveMetricCard(
            node = metric,
            context = context,
            modifier =
                Modifier
                    .offset(y = 72.dp)
                    .fillMaxWidth()
                    .height(60.dp),
        )
        actions.forEachIndexed { index, action ->
            NotificationActionButton(
                node = action,
                context = context,
                modifier =
                    Modifier
                        .offset(x = (index * 94).dp, y = 136.dp)
                        .width(90.dp)
                        .height(48.dp),
            )
        }
    }
}

@Composable
private fun SquareWorkoutSummarySurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull { it.kind == "text" }
            ?: error("Square workout summary requires one context label")
    val metrics =
        children.singleOrNull { it.kind == "row" }
            ?: error("Square workout summary requires one metric row")
    val metricNodes =
        context.snapshot.childrenOf(metrics).filter { it.visible }
    check(metricNodes.size == 2 && metricNodes.all { it.kind == "text" }) {
        "Square workout summary metric row requires two text metrics"
    }
    val card =
        children.singleOrNull { it.kind == "card" }
            ?: error("Square workout summary requires one detail card")
    val action =
        children.singleOrNull { it.kind == "button" }
            ?: error("Square workout summary requires one action")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(20.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            textAlign = TextAlign.Center,
        )
        Row(
            modifier =
                Modifier
                    .offset(y = 24.dp)
                    .fillMaxWidth()
                    .height(44.dp)
                    .appSpecNode(metrics, context.evidenceCollector),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            metricNodes.forEach { metric ->
                Box(
                    modifier =
                        Modifier
                            .weight(1f)
                            .fillMaxHeight()
                            .clip(RoundedCornerShape(16.dp))
                            .background(
                                MaterialTheme.colorScheme.surfaceContainerHigh,
                            )
                            .appSpecNode(
                                metric,
                                context.evidenceCollector,
                            ),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = requireNotNull(metric.props.primaryText),
                        style = MaterialTheme.typography.titleSmall,
                        maxLines = 1,
                    )
                }
            }
        }
        CalendarEventCard(
            node = card,
            context = context,
            modifier =
                Modifier
                    .offset(y = 72.dp)
                    .fillMaxWidth()
                    .height(60.dp),
        )
        NotificationActionButton(
            node = action,
            context = context,
            modifier =
                Modifier
                    .offset(x = 32.dp, y = 136.dp)
                    .width(120.dp)
                    .height(48.dp),
        )
    }
}

@Composable
private fun WorkoutLiveMetricCard(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    Card(
        modifier =
            modifier.appSpecNode(
                node,
                context.evidenceCollector,
            ),
        contentPadding =
            PaddingValues(horizontal = 12.dp, vertical = 5.dp),
    ) {
        Text(
            text = requireNotNull(node.props.primaryText),
            style = MaterialTheme.typography.titleSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = requireNotNull(node.props.secondaryText),
            style = MaterialTheme.typography.bodyExtraSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (node.props.value != null) {
            LinearProgressIndicator(
                progress = {
                    requireNotNull(node.props.value).toFloat() /
                        requireNotNull(node.props.maximum).toFloat()
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = node.enabled,
            )
        }
    }
}

@Composable
private fun SquareNutritionDashboardSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val texts = children.filter { it.kind == "text" }
    val heading =
        texts.singleOrNull { it.props.variant == "label" }
            ?: error("Nutrition dashboard requires one context label")
    val total =
        texts.singleOrNull { it.props.variant == "numeral" }
            ?: error("Nutrition dashboard requires one total")
    val progress =
        children.singleOrNull { it.kind == "progress" }
            ?: error("Nutrition dashboard requires one progress indicator")
    val card =
        children.singleOrNull { it.kind == "card" }
            ?: error("Nutrition dashboard requires one meal card")
    val action =
        children.singleOrNull { it.kind == "button" }
            ?: error("Nutrition dashboard requires one add action")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(20.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        Text(
            text = requireNotNull(total.props.primaryText),
            modifier =
                Modifier
                    .offset(y = 20.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .appSpecNode(total, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurface,
            style = MaterialTheme.typography.displaySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        LinearProgressIndicator(
            progress = {
                requireNotNull(progress.props.value).toFloat() /
                    requireNotNull(progress.props.maximum).toFloat()
            },
            modifier =
                Modifier
                    .offset(y = 72.dp)
                    .fillMaxWidth()
                    .height(12.dp)
                    .appSpecNode(progress, context.evidenceCollector),
            enabled = progress.enabled,
        )
        CalendarEventCard(
            node = card,
            context = context,
            modifier =
                Modifier
                    .offset(y = 88.dp)
                    .fillMaxWidth()
                    .height(44.dp),
        )
        NotificationActionButton(
            node = action,
            context = context,
            modifier =
                Modifier
                    .offset(x = 32.dp, y = 136.dp)
                    .width(120.dp)
                    .height(48.dp),
        )
    }
}

@Composable
private fun SquareNutritionQuickAddSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull { it.kind == "text" }
            ?: error("Nutrition quick add requires one context label")
    val stepper =
        children.singleOrNull { it.kind == "stepper" }
            ?: error("Nutrition quick add requires one calorie stepper")
    val card =
        children.singleOrNull { it.kind == "card" }
            ?: error("Nutrition quick add requires one context card")
    val actions =
        children.singleOrNull { it.kind == "row" }
            ?: error("Nutrition quick add requires one action row")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        NutritionContextLabel(
            node = heading,
            context = context,
        )
        InlineAppSpecStepper(
            node = stepper,
            context = context,
            action =
                stepper.action("value_committed")
                    ?: error("Nutrition amount requires valueCommitted"),
            current = requireNotNull(stepper.props.value),
            minimum = requireNotNull(stepper.props.minimum),
            maximum = requireNotNull(stepper.props.maximum),
            step = requireNotNull(stepper.props.step),
            modifier =
                Modifier
                    .offset(y = 24.dp)
                    .fillMaxWidth()
                    .height(48.dp),
        )
        CalendarEventCard(
            node = card,
            context = context,
            modifier =
                Modifier
                    .offset(y = 76.dp)
                    .fillMaxWidth()
                    .height(52.dp),
        )
        NutritionActionRow(
            node = actions,
            context = context,
        )
    }
}

@Composable
private fun SquareNutritionReviewSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val texts = children.filter { it.kind == "text" }
    val heading =
        texts.singleOrNull { it.props.variant == "label" }
            ?: error("Nutrition review requires one context label")
    val total =
        texts.singleOrNull { it.props.variant == "numeral" }
            ?: error("Nutrition review requires one total")
    val card =
        children.singleOrNull { it.kind == "card" }
            ?: error("Nutrition review requires one meal card")
    val actions =
        children.singleOrNull { it.kind == "row" }
            ?: error("Nutrition review requires one action row")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        NutritionContextLabel(
            node = heading,
            context = context,
        )
        Text(
            text = requireNotNull(total.props.primaryText),
            modifier =
                Modifier
                    .offset(y = 20.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .appSpecNode(total, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurface,
            style = MaterialTheme.typography.displaySmall,
            maxLines = 1,
            textAlign = TextAlign.Center,
        )
        CalendarEventCard(
            node = card,
            context = context,
            modifier =
                Modifier
                    .offset(y = 72.dp)
                    .fillMaxWidth()
                    .height(56.dp),
        )
        NutritionActionRow(
            node = actions,
            context = context,
        )
    }
}

@Composable
private fun NutritionContextLabel(
    node: SceneNode,
    context: RenderContext,
) {
    Text(
        text = requireNotNull(node.props.primaryText),
        modifier =
            Modifier
                .fillMaxWidth()
                .height(20.dp)
                .appSpecNode(node, context.evidenceCollector),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        style = MaterialTheme.typography.labelLarge,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        textAlign = TextAlign.Center,
    )
}

@Composable
private fun NutritionActionRow(
    node: SceneNode,
    context: RenderContext,
) {
    val actions =
        context.snapshot.childrenOf(node).filter { it.visible }
    check(actions.size == 2 && actions.all { it.kind == "button" }) {
        "Nutrition action row requires two buttons"
    }
    Row(
        modifier =
            Modifier
                .offset(y = 132.dp)
                .fillMaxWidth()
                .height(48.dp)
                .appSpecNode(node, context.evidenceCollector),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        actions.forEach { action ->
            NotificationActionButton(
                node = action,
                context = context,
                modifier =
                    Modifier
                        .weight(1f)
                        .fillMaxHeight(),
            )
        }
    }
}

@Composable
private fun SquareVoiceReadySurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull { it.kind == "text" }
            ?: error("Voice ready requires one context label")
    val orb =
        children.singleOrNull { it.kind == "voice_orb" }
            ?: error("Voice ready requires one record orb")
    val recent =
        children.singleOrNull { it.kind == "card" }
            ?: error("Voice ready requires one recent recording")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        NutritionContextLabel(
            node = heading,
            context = context,
        )
        Box(
            modifier =
                Modifier
                    .offset(x = 44.dp, y = 24.dp)
                    .width(96.dp)
                    .height(100.dp),
            contentAlignment = Alignment.TopCenter,
        ) {
            AppSpecVoiceOrb(
                node = orb,
                context = context,
            )
        }
        CalendarEventCard(
            node = recent,
            context = context,
            modifier =
                Modifier
                    .offset(y = 128.dp)
                    .fillMaxWidth()
                    .height(56.dp),
        )
    }
}

@Composable
private fun SquareLiveActionDetailSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val texts = children.filter { it.kind == "text" }
    val heading =
        texts.singleOrNull { it.props.variant == "label" }
            ?: error("Live action detail requires one context label")
    val elapsed =
        texts.singleOrNull { it.props.variant == "numeral" }
            ?: error("Live action detail requires one primary value")
    val detail =
        children.singleOrNull { it.kind == "live_card" }
            ?: error("Live action detail requires one detail card")
    val actions =
        children.singleOrNull { it.kind == "row" }
            ?: error("Live action detail requires one action row")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        NutritionContextLabel(
            node = heading,
            context = context,
        )
        Text(
            text = requireNotNull(elapsed.props.primaryText),
            modifier =
                Modifier
                    .offset(y = 20.dp)
                    .fillMaxWidth()
                    .height(48.dp)
                    .appSpecNode(elapsed, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurface,
            style = MaterialTheme.typography.displaySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        WorkoutLiveMetricCard(
            node = detail,
            context = context,
            modifier =
                Modifier
                    .offset(y = 72.dp)
                    .fillMaxWidth()
                    .height(56.dp),
        )
        NutritionActionRow(
            node = actions,
            context = context,
        )
    }
}

@Composable
private fun SquareCalendarAgendaSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val stack =
        children.singleOrNull { it.kind == "scroll" }
            ?: error("Square calendar agenda requires one scroll node")
    val column =
        context.snapshot.childrenOf(stack)
            .singleOrNull { it.visible && it.kind == "column" }
            ?: error("Square calendar agenda requires one content column")
    val content =
        context.snapshot.childrenOf(column).filter { it.visible }
    val heading =
        content.singleOrNull { it.kind == "text" }
            ?: error("Square calendar agenda requires one context label")
    val cards = content.filter { it.kind == "card" }
    val actions = content.filter { it.kind == "button" }
    check(
        ((cards.size == 2 && actions.size == 1) ||
            (cards.size == 1 && actions.size == 2)) &&
            content.all {
                it == heading || it in cards || it in actions
            },
    ) {
        "Square calendar agenda supports two events plus one action or one event plus two actions"
    }

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp)
                .appSpecNode(stack, context.evidenceCollector),
    ) {
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .appSpecNode(column, context.evidenceCollector),
        ) {
            Text(
                text = requireNotNull(heading.props.primaryText),
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(20.dp)
                        .appSpecNode(heading, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
            )

            cards.forEachIndexed { index, card ->
                val y = if (cards.size == 2) 24 + index * 54 else 24
                val height = if (cards.size == 2) 50 else 104
                CalendarEventCard(
                    node = card,
                    context = context,
                    modifier =
                        Modifier
                            .offset(y = y.dp)
                            .fillMaxWidth()
                            .height(height.dp),
                )
            }

            actions.forEachIndexed { index, action ->
                val paired = actions.size == 2
                NotificationActionButton(
                    node = action,
                    context = context,
                    modifier =
                        Modifier
                            .offset(
                                x =
                                    if (paired) {
                                        (index * 94).dp
                                    } else {
                                        32.dp
                                    },
                                y = 136.dp,
                            )
                            .width(if (paired) 90.dp else 120.dp)
                            .height(48.dp),
                )
            }
        }
    }
}

@Composable
private fun CalendarEventCard(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    val tap = node.action("tap")
    val evidenceModifier =
        modifier.appSpecNode(node, context.evidenceCollector)
    val content: @Composable ColumnScope.() -> Unit = {
        Text(
            text = requireNotNull(node.props.primaryText),
            style = MaterialTheme.typography.titleSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = requireNotNull(node.props.secondaryText),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
    val padding =
        PaddingValues(horizontal = 12.dp, vertical = 4.dp)
    if (tap == null) {
        Card(
            modifier = evidenceModifier,
            contentPadding = padding,
            content = content,
        )
    } else {
        Card(
            onClick = { context.dispatch(node, tap) },
            enabled = node.enabled,
            modifier = evidenceModifier,
            contentPadding = padding,
            content = content,
        )
    }
}

@Composable
private fun SquareTaskListSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val list =
        children.singleOrNull { it.kind == "scroll" }
            ?: error("Square task list requires one scroll node")
    val content =
        context.snapshot.childrenOf(list).filter { it.visible }
    val heading =
        content.singleOrNull { it.kind == "text" }
            ?: error("Square task list requires one context label")
    val tasks = content.filter { it.kind == "toggle" }
    val add = content.singleOrNull { it.kind == "button" }
    check(
        tasks.size in 2..3 &&
            ((tasks.size == 2 && add != null) ||
                (tasks.size == 3 && add == null)) &&
            content.all {
                it == heading || it in tasks || it == add
            },
    ) {
        "Square task list supports two tasks plus add or three task rows"
    }

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp)
                .appSpecNode(list, context.evidenceCollector),
    ) {
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(20.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )

        val taskHeight = if (tasks.size == 3) 48 else 52
        val taskStep = if (tasks.size == 3) 52 else 56
        tasks.forEachIndexed { index, task ->
            val action =
                task.action("checked_changed")
                    ?: error("Task row requires checkedChanged")
            val checked = requireNotNull(task.props.checked)
            CheckboxButton(
                checked = checked,
                onCheckedChange = { value ->
                    context.dispatch(
                        task,
                        action,
                        ReferenceActionPayload.Checked(value),
                    )
                },
                modifier =
                    Modifier
                        .offset(y = (24 + index * taskStep).dp)
                        .fillMaxWidth()
                        .height(taskHeight.dp)
                        .appSpecNode(task, context.evidenceCollector),
                enabled = task.enabled,
                label = {
                    Text(
                        text = requireNotNull(task.props.primaryText),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                },
            )
        }

        if (add != null) {
            val action =
                add.action("tap")
                    ?: error("Task add button requires tap")
            FilledTonalButton(
                onClick = { context.dispatch(add, action) },
                modifier =
                    Modifier
                        .offset(y = 136.dp)
                        .fillMaxWidth()
                        .height(48.dp)
                        .appSpecNode(add, context.evidenceCollector),
                enabled = add.enabled,
                label = {
                    Text(
                        text = requireNotNull(add.props.primaryText),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                },
            )
        }
    }
}

@Composable
private fun SquareMediaPlayerSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val artwork =
        children.singleOrNull { it.kind == "image" }
            ?: error("Square media player requires one package image")
    val text = children.filter { it.kind == "text" }
    val title =
        text.singleOrNull { it.props.variant == "title" }
            ?: error("Square media player requires one title")
    val detail =
        text.singleOrNull { it.props.variant == "caption" }
            ?: error("Square media player requires one caption")
    val progress =
        children.singleOrNull { it.kind == "progress" }
            ?: error("Square media player requires one progress indicator")
    val controls =
        children.singleOrNull { it.kind == "row" }
            ?: error("Square media player requires one control row")
    val buttons =
        context.snapshot.childrenOf(controls).filter { it.visible }
    check(buttons.size == 2 && buttons.all { it.kind == "button" }) {
        "Square media player requires exactly two actions"
    }

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        PackageImage(
            node = artwork,
            context = context,
            modifier =
                Modifier
                    .size(76.dp),
        )
        Text(
            text = requireNotNull(title.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 80.dp)
                    .width(104.dp)
                    .height(44.dp)
                    .appSpecNode(title, context.evidenceCollector)
                    .padding(top = 4.dp),
            color = MaterialTheme.colorScheme.onBackground,
            style = MaterialTheme.typography.titleMedium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Start,
        )
        Text(
            text = requireNotNull(detail.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 80.dp, y = 44.dp)
                    .width(104.dp)
                    .height(32.dp)
                    .appSpecNode(detail, context.evidenceCollector)
                    .padding(top = 2.dp),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodySmall,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Start,
        )
        val maximum = requireNotNull(progress.props.maximum)
        val value = requireNotNull(progress.props.value)
        LinearProgressIndicator(
            progress = { value.toFloat() / maximum.toFloat() },
            modifier =
                Modifier
                    .offset(y = 80.dp)
                    .fillMaxWidth()
                    .height(16.dp)
                    .appSpecNode(progress, context.evidenceCollector),
        )
        Row(
            modifier =
                Modifier
                    .offset(y = 100.dp)
                    .fillMaxWidth()
                    .height(80.dp)
                    .appSpecNode(controls, context.evidenceCollector),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            buttons.forEach { button ->
                MediaActionButton(
                    node = button,
                    context = context,
                    modifier =
                        Modifier
                            .width(90.dp)
                            .fillMaxHeight(),
                )
            }
        }
    }
}

@Composable
private fun SquareWalletQrSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull { it.kind == "text" }
            ?: error("Square wallet QR requires one context label")
    val code =
        children.singleOrNull { it.kind == "image" }
            ?: error("Square wallet QR requires one package image")
    val actions =
        children.singleOrNull { it.kind == "row" }
            ?: error("Square wallet QR requires one action row")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        NutritionContextLabel(
            node = heading,
            context = context,
        )
        PackageImage(
            node = code,
            context = context,
            modifier =
                Modifier
                    .offset(x = 38.dp, y = 22.dp)
                    .size(108.dp),
            cornerRadiusDp = 12,
        )
        NutritionActionRow(
            node = actions,
            context = context,
        )
    }
}

@Composable
private fun SquareCameraRemoteSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val viewfinder =
        children.singleOrNull { it.kind == "image" }
            ?: error("Square camera remote requires one package image")
    val texts = children.filter { it.kind == "text" }
    val contextLabel =
        texts.singleOrNull { it.props.variant == "label" }
            ?: error("Square camera remote requires one context label")
    val value =
        texts.singleOrNull { it.props.variant == "numeral" }
            ?: error("Square camera remote requires one primary value")
    val actions =
        children.singleOrNull { it.kind == "row" }
            ?: error("Square camera remote requires one action row")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        PackageImage(
            node = viewfinder,
            context = context,
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(120.dp),
            cornerRadiusDp = 24,
        )
        Text(
            text = requireNotNull(contextLabel.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 12.dp, y = 4.dp)
                    .width(160.dp)
                    .height(20.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(
                        MaterialTheme.colorScheme.background.copy(
                            alpha = 0.78f,
                        ),
                    )
                    .appSpecNode(
                        contextLabel,
                        context.evidenceCollector,
                    )
                    .padding(top = 2.dp),
            color = MaterialTheme.colorScheme.onBackground,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        Text(
            text = requireNotNull(value.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 20.dp, y = 40.dp)
                    .width(144.dp)
                    .height(48.dp)
                    .clip(RoundedCornerShape(24.dp))
                    .background(
                        MaterialTheme.colorScheme.background.copy(
                            alpha = 0.66f,
                        ),
                    )
                    .appSpecNode(value, context.evidenceCollector)
                    .padding(top = 5.dp),
            color = MaterialTheme.colorScheme.onBackground,
            style = MaterialTheme.typography.displaySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        NutritionActionRow(
            node = actions,
            context = context,
        )
    }
}

@Composable
private fun MediaActionButton(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    val tap =
        node.action("tap")
            ?: error("Media action requires a tap event")
    val evidenceModifier =
        modifier.appSpecNode(node, context.evidenceCollector)
    val label: @Composable RowScope.() -> Unit = {
        Text(
            text = requireNotNull(node.props.primaryText),
            style = MaterialTheme.typography.labelMedium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
    if (node.props.variant == "filled") {
        Button(
            onClick = { context.dispatch(node, tap) },
            enabled = node.enabled,
            modifier = evidenceModifier,
            label = label,
        )
    } else {
        FilledTonalButton(
            onClick = { context.dispatch(node, tap) },
            enabled = node.enabled,
            modifier = evidenceModifier,
            label = label,
        )
    }
}

@Composable
private fun SquareNotificationStackSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val stack =
        children.singleOrNull { it.kind == "scroll" }
            ?: error("Square notification stack requires one scroll node")
    val content =
        context.snapshot.childrenOf(stack).filter { it.visible }
    val heading =
        content.singleOrNull { it.kind == "text" }
            ?: error("Square notification stack requires one context label")
    val cards = content.filter { it.kind == "card" }
    val actions = content.filter { it.kind == "button" }
    check(
        cards.size in 1..2 &&
            actions.size in setOf(1, 3) &&
            content.all {
                it == heading || it in cards || it in actions
            },
    ) {
        "Square notification stack supports one label, one or two cards, and one or three actions"
    }

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp)
                .appSpecNode(stack, context.evidenceCollector),
    ) {
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(20.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )

        cards.forEachIndexed { index, card ->
            val cardY =
                if (cards.size == 2) {
                    24 + index * 58
                } else {
                    24
                }
            val cardHeight =
                when {
                    cards.size == 2 -> 54
                    actions.size == 3 -> 60
                    else -> 108
                }
            NotificationCard(
                node = card,
                context = context,
                modifier =
                    Modifier
                        .offset(y = cardY.dp)
                        .fillMaxWidth()
                        .height(cardHeight.dp),
            )
        }

        actions.forEachIndexed { index, action ->
            val paired = actions.size == 3 && index < 2
            val x =
                when {
                    paired && index == 0 -> 0
                    paired -> 94
                    else -> 32
                }
            val y = if (paired) 88 else 136
            val width = if (paired) 90 else 120
            NotificationActionButton(
                node = action,
                context = context,
                modifier =
                    Modifier
                        .offset(x = x.dp, y = y.dp)
                        .width(width.dp)
                        .height(48.dp),
            )
        }
    }
}

@Composable
private fun NotificationCard(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    val tap = node.action("tap")
    val title: @Composable () -> Unit = {
        Text(
            text = requireNotNull(node.props.primaryText),
            style = MaterialTheme.typography.titleSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
    val body: @Composable () -> Unit = {
        Text(
            text = requireNotNull(node.props.secondaryText),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
    val evidenceModifier =
        modifier.appSpecNode(node, context.evidenceCollector)
    if (tap == null) {
        TitleCard(
            title = { title() },
            modifier = evidenceModifier,
            content = body,
        )
    } else {
        TitleCard(
            onClick = { context.dispatch(node, tap) },
            enabled = node.enabled,
            title = { title() },
            modifier = evidenceModifier,
            content = body,
        )
    }
}

@Composable
private fun NotificationActionButton(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    val tap =
        node.action("tap")
            ?: error("Notification action requires a tap event")
    val content: @Composable () -> Unit = {
        Text(
            text = requireNotNull(node.props.primaryText),
            style = MaterialTheme.typography.labelMedium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
    val evidenceModifier =
        modifier.appSpecNode(node, context.evidenceCollector)
    when (node.props.variant) {
        "filled" ->
            Button(
                onClick = { context.dispatch(node, tap) },
                enabled = node.enabled,
                modifier = evidenceModifier,
                label = { content() },
            )
        "tonal" ->
            FilledTonalButton(
                onClick = { context.dispatch(node, tap) },
                enabled = node.enabled,
                modifier = evidenceModifier,
                label = { content() },
            )
        "text" ->
            ChildButton(
                onClick = { context.dispatch(node, tap) },
                enabled = node.enabled,
                modifier = evidenceModifier,
                label = { content() },
            )
        else ->
            error(
                "Notification stack does not support ${node.props.variant} actions",
            )
    }
}

@Composable
private fun SquareWeatherHeroSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val heading =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "label"
        } ?: error("Square weather hero requires one location label")
    val card =
        children.singleOrNull { it.kind == "card" }
            ?: error("Square weather hero requires one forecast card")
    val symbol =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "title"
        } ?: error("Square weather hero requires one condition symbol")
    val summary =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "numeral"
        } ?: error("Square weather hero requires one temperature")
    val status =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "caption"
        } ?: error("Square weather hero requires one freshness label")
    val action =
        children.singleOrNull { it.kind == "button" }
            ?: error("Square weather hero requires one refresh action")
    check(
        children.all {
            it == heading ||
                it == card ||
                it == symbol ||
                it == summary ||
                it == status ||
                it == action
        },
    ) {
        "Square weather hero only supports its six semantic children"
    }

    val cardColor = MaterialTheme.colorScheme.primaryContainer
    val contentColor = MaterialTheme.colorScheme.onPrimaryContainer
    val accentColor = MaterialTheme.colorScheme.tertiary
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
    ) {
        Box(
            modifier =
                Modifier
                    .offset(y = 28.dp)
                    .fillMaxWidth()
                    .height(156.dp)
                    .background(cardColor, RoundedCornerShape(32.dp))
                    .appSpecNode(card, context.evidenceCollector),
        )
        Text(
            text = requireNotNull(heading.props.primaryText),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(24.dp)
                    .appSpecNode(heading, context.evidenceCollector),
            color = MaterialTheme.colorScheme.onBackground,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        Canvas(
            modifier =
                Modifier
                    .offset(x = 12.dp, y = 52.dp)
                    .size(44.dp)
                    .appSpecNode(symbol, context.evidenceCollector),
        ) {
            val radius = size.minDimension * 0.32f
            drawCircle(
                color = accentColor.copy(alpha = 0.32f),
                radius = size.minDimension * 0.45f,
            )
            drawCircle(color = accentColor, radius = radius)
        }
        Text(
            text = requireNotNull(summary.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 56.dp, y = 44.dp)
                    .width(112.dp)
                    .height(64.dp)
                    .appSpecNode(summary, context.evidenceCollector),
            color = contentColor,
            style = MaterialTheme.typography.numeralLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        Text(
            text = requireNotNull(card.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 16.dp, y = 106.dp)
                    .width(152.dp)
                    .height(24.dp),
            color = contentColor,
            style = MaterialTheme.typography.titleMedium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = requireNotNull(card.props.secondaryText),
            modifier =
                Modifier
                    .offset(x = 16.dp, y = 130.dp)
                    .width(108.dp)
                    .height(38.dp),
            color = contentColor.copy(alpha = 0.78f),
            style = MaterialTheme.typography.bodySmall,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = requireNotNull(status.props.primaryText),
            modifier =
                Modifier
                    .offset(x = 16.dp, y = 164.dp)
                    .width(108.dp)
                    .height(16.dp)
                    .appSpecNode(status, context.evidenceCollector),
            color = contentColor.copy(alpha = 0.72f),
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        WeatherRefreshButton(action, context)
    }
}

@Composable
private fun WeatherRefreshButton(
    node: SceneNode,
    context: RenderContext,
) {
    val tap = node.action("tap")
    val glyphColor = MaterialTheme.colorScheme.onPrimary
    Button(
        onClick = {
            tap?.let {
                context.dispatch(node, it)
            }
        },
        modifier =
            Modifier
                .offset(x = 132.dp, y = 132.dp)
                .size(48.dp)
                .appSpecNode(node, context.evidenceCollector),
        enabled = node.enabled && tap != null,
        colors = ButtonDefaults.buttonColors(),
        contentPadding = PaddingValues(0.dp),
        label = {
            Canvas(Modifier.size(24.dp)) {
                drawArc(
                    color = glyphColor,
                    startAngle = -55f,
                    sweepAngle = 275f,
                    useCenter = false,
                    style = Stroke(width = size.minDimension * 0.13f),
                )
                drawCircle(
                    color = glyphColor,
                    radius = size.minDimension * 0.10f,
                    center =
                        androidx.compose.ui.geometry.Offset(
                            x = size.width * 0.81f,
                            y = size.height * 0.30f,
                        ),
                )
            }
        },
    )
}

@Composable
private fun SquareCountdownSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val progress =
        children.singleOrNull { it.kind == "progress" }
            ?: error("Square countdown pattern requires one progress node")
    val value =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "numeral"
        } ?: error("Square countdown pattern requires one numeral value")
    val stepper =
        children.singleOrNull { it.kind == "stepper" }
            ?: error("Square countdown pattern requires one stepper")
    val action =
        children.singleOrNull { it.kind == "button" }
            ?: error("Square countdown pattern requires one button")
    check(
        children.all {
            it == progress || it == value || it == stepper || it == action
        },
    ) {
        "Square countdown pattern only supports progress, value, stepper, and action"
    }
    val maximum = requireNotNull(progress.props.maximum)
    val fraction =
        requireNotNull(progress.props.value).toFloat() /
            maximum.coerceAtLeast(1).toFloat()

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .weight(1f),
            contentAlignment = Alignment.Center,
        ) {
            CircularProgressIndicator(
                progress = { fraction.coerceIn(0f, 1f) },
                modifier =
                    Modifier
                        .size(132.dp)
                        .appSpecNode(
                            progress,
                            context.evidenceCollector,
                        ),
                enabled = progress.enabled,
            )
            Text(
                text = requireNotNull(value.props.primaryText),
                modifier =
                    Modifier
                        .width(112.dp)
                        .appSpecNode(value, context.evidenceCollector),
                color = MaterialTheme.colorScheme.onBackground,
                style = MaterialTheme.typography.numeralLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
            )
            if (stepper.visible) {
                Box(
                    modifier =
                        Modifier
                            .align(Alignment.BottomCenter)
                            .width(140.dp)
                            .height(48.dp),
                ) {
                    AppSpecStepper(stepper, context)
                }
            }
        }
        Spacer(Modifier.height(4.dp))
        CountdownActionButton(action, context)
    }
}

@Composable
private fun CountdownActionButton(
    node: SceneNode,
    context: RenderContext,
) {
    val tap = node.action("tap")
    Button(
        onClick = {
            tap?.let {
                context.dispatch(node, it)
            }
        },
        modifier =
            Modifier
                .width(120.dp)
                .height(48.dp)
                .appSpecNode(node, context.evidenceCollector),
        enabled = node.enabled && tap != null,
        colors = ButtonDefaults.buttonColors(),
        label = {
            Text(
                text = requireNotNull(node.props.primaryText),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
    )
}

@Composable
private fun SquareKeypadSurface(
    children: List<SceneNode>,
    context: RenderContext,
) {
    val result =
        children.singleOrNull {
            it.kind == "text" && it.props.variant == "numeral"
        } ?: error("Square keypad pattern requires one numeral result")
    val keypad =
        children.singleOrNull { it.kind == "keypad" }
            ?: error("Square keypad pattern requires one keypad")
    check(children.all { it == result || it == keypad }) {
        "Square keypad pattern only supports a result and keypad"
    }

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(horizontal = 4.dp, vertical = 4.dp),
    ) {
        Box(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(40.dp)
                    .appSpecNode(result, context.evidenceCollector),
            contentAlignment = Alignment.CenterEnd,
        ) {
            Text(
                text = requireNotNull(result.props.primaryText),
                color = MaterialTheme.colorScheme.onBackground,
                style = MaterialTheme.typography.numeralSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.End,
            )
        }
        Spacer(Modifier.height(3.dp))
        SquareMaterialKeypad(
            node = keypad,
            context = context,
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
private fun SquareMaterialKeypad(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    val action = node.action("tap")
    val keys = requireNotNull(node.props.keys)
    val columns = requireNotNull(node.props.keyColumns)
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        verticalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        keys.chunked(columns).forEachIndexed { rowIndex, row ->
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .weight(1f),
                horizontalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                row.forEachIndexed { columnIndex, key ->
                    val keyIndex = rowIndex * columns + columnIndex
                    MaterialCalculatorKey(
                        key = key,
                        keyIndex = keyIndex,
                        rowIndex = rowIndex,
                        columnIndex = columnIndex,
                        columns = columns,
                        nodeId = node.id,
                        semanticLabel = node.semantics.label,
                        actionId = action?.actionId.orEmpty(),
                        enabled = node.enabled && action != null,
                        modifier =
                            Modifier
                                .weight(1f)
                                .fillMaxHeight(),
                        onClick = {
                            action?.let {
                                context.dispatch(
                                    node,
                                    it,
                                    ReferenceActionPayload.Text(key),
                                )
                            }
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun MaterialCalculatorKey(
    key: String,
    keyIndex: Int,
    rowIndex: Int,
    columnIndex: Int,
    columns: Int,
    nodeId: String,
    semanticLabel: String,
    actionId: String,
    enabled: Boolean,
    modifier: Modifier,
    onClick: () -> Unit,
) {
    val operator = columnIndex == columns - 1
    val utility = rowIndex == 0 || key == "<-"
    val containerColor =
        when {
            operator -> MaterialTheme.colorScheme.primary
            utility -> MaterialTheme.colorScheme.secondary
            else -> MaterialTheme.colorScheme.surfaceContainerHigh
        }
    val contentColor =
        when {
            operator -> MaterialTheme.colorScheme.onPrimary
            utility -> MaterialTheme.colorScheme.onSecondary
            else -> MaterialTheme.colorScheme.onSurface
        }
    val interactionSource = remember { MutableInteractionSource() }
    val pressed by interactionSource.collectIsPressedAsState()
    val keyScale by animateFloatAsState(
        targetValue = if (pressed) 0.94f else 1f,
        label = "calculator-key-$keyIndex",
    )

    Box(
        modifier =
            modifier
                .scale(keyScale)
                .clip(RoundedCornerShape(12.dp))
                .background(containerColor)
                .testTag("calculator-key-$keyIndex")
                .semantics {
                    contentDescription = "$key, $semanticLabel"
                    doodadNodeId = nodeId
                    doodadActionIds = actionId
                }
                .clickable(
                    interactionSource = interactionSource,
                    indication = null,
                    enabled = enabled,
                    onClick = onClick,
                ),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = key.calculatorGlyph(),
            color = contentColor,
            style =
                if (key.length > 1) {
                    MaterialTheme.typography.labelLarge
                } else {
                    MaterialTheme.typography.titleMedium
                },
            maxLines = 1,
        )
    }
}

private data class RenderContext(
    val snapshot: SceneSnapshot,
    val profile: ReferenceGeometryProfile,
    val evidenceCollector: ComposeNodeEvidenceCollector?,
    val onAction: (ReferenceActionEnvelope) -> Unit,
)

@Composable
private fun ContainerColumn(
    node: SceneNode,
    context: RenderContext,
) {
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        horizontalAlignment = node.props.alignment.horizontalAlignment(),
        verticalArrangement =
            Arrangement.spacedBy(node.props.gap.spacing()),
    ) {
        context.snapshot.childrenOf(node).forEach { child ->
            AppSpecComponentRegistry.Render(
                child,
                context.snapshot,
                context.profile,
                context.evidenceCollector,
                context.onAction,
            )
        }
    }
}

@Composable
private fun ContainerRow(
    node: SceneNode,
    context: RenderContext,
) {
    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        horizontalArrangement =
            Arrangement.spacedBy(
                node.props.gap.spacing(),
                node.props.alignment.horizontalArrangementAlignment(),
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        context.snapshot.childrenOf(node).forEach { child ->
            val childModifier =
                if (node.props.alignment == "stretch") {
                    Modifier.weight(1f)
                } else {
                    Modifier
                }
            Box(childModifier) {
                AppSpecComponentRegistry.Render(
                    child,
                    context.snapshot,
                    context.profile,
                    context.evidenceCollector,
                    context.onAction,
                )
            }
        }
    }
}

@Composable
private fun ScrollContainer(
    node: SceneNode,
    context: RenderContext,
) {
    val state = rememberScrollState()
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .verticalScroll(state)
                .appSpecNode(node, context.evidenceCollector),
        horizontalAlignment = node.props.alignment.horizontalAlignment(),
        verticalArrangement =
            Arrangement.spacedBy(node.props.gap.spacing()),
    ) {
        context.snapshot.childrenOf(node).forEach { child ->
            AppSpecComponentRegistry.Render(
                child,
                context.snapshot,
                context.profile,
                context.evidenceCollector,
                context.onAction,
            )
        }
    }
}

@Composable
private fun AppSpecText(
    node: SceneNode,
    context: RenderContext,
) {
    Text(
        text = requireNotNull(node.props.primaryText),
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        color =
            if (node.props.variant in setOf("caption", "label")) {
                MaterialTheme.colorScheme.onSurfaceVariant
            } else {
                MaterialTheme.colorScheme.onBackground
            },
        style =
            if (context.snapshot.appId == "weather" &&
                node.props.variant == "numeral"
            ) {
                MaterialTheme.typography.numeralSmall
            } else {
                node.props.variant.textStyle()
            },
        maxLines = node.props.maxLines ?: 4,
        overflow = TextOverflow.Ellipsis,
        textAlign = node.props.alignment.textAlign(),
    )
}

@Composable
private fun AppSpecImage(
    node: SceneNode,
    context: RenderContext,
) {
    PackageImage(
        node = node,
        context = context,
        modifier =
            Modifier
                .fillMaxWidth()
                .height(76.dp),
    )
}

@Composable
private fun AppSpecCanvas(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier =
        Modifier
            .fillMaxWidth()
            .height(requireNotNull(node.props.height).dp),
) {
    val width = requireNotNull(node.props.width)
    val height = requireNotNull(node.props.height)
    val parsed =
        remember(
            node.props.displayList,
            node.props.palette,
            width,
            height,
        ) {
            CanvasDisplayListCodec.parse(
                requireNotNull(node.props.displayList),
                requireNotNull(node.props.palette),
                width,
                height,
            )
        }
    val colors =
        remember(parsed.palette) {
            parsed.palette.map { rgb ->
                Color(0xFF000000L or rgb.toLong())
            }
        }
    val tap = node.action("tap")
    Canvas(
        modifier =
            modifier
                .clip(RoundedCornerShape(20.dp))
                .clickable(
                    enabled = node.enabled && tap != null,
                    onClick = {
                        tap?.let {
                            context.dispatch(node, it)
                        }
                    },
                )
                .appSpecNode(node, context.evidenceCollector),
    ) {
        val scaleX = size.width / width.toFloat()
        val scaleY = size.height / height.toFloat()
        parsed.commands.forEach { command ->
            when (command) {
                is CanvasCommand.Clear ->
                    drawRect(colors[command.color])
                is CanvasCommand.RoundedRect ->
                    drawRoundRect(
                        color = colors[command.color],
                        topLeft =
                            Offset(
                                command.x * scaleX,
                                command.y * scaleY,
                            ),
                        size =
                            Size(
                                command.width * scaleX,
                                command.height * scaleY,
                            ),
                        cornerRadius =
                            androidx.compose.ui.geometry.CornerRadius(
                                command.radius * scaleX,
                                command.radius * scaleY,
                            ),
                    )
                is CanvasCommand.Circle ->
                    drawCircle(
                        color = colors[command.color],
                        radius =
                            command.radius * minOf(scaleX, scaleY),
                        center =
                            Offset(
                                command.centerX * scaleX,
                                command.centerY * scaleY,
                            ),
                    )
                is CanvasCommand.Line ->
                    drawLine(
                        color = colors[command.color],
                        start =
                            Offset(
                                command.x1 * scaleX,
                                command.y1 * scaleY,
                            ),
                        end =
                            Offset(
                                command.x2 * scaleX,
                                command.y2 * scaleY,
                            ),
                        strokeWidth =
                            command.stroke * minOf(scaleX, scaleY),
                    )
                is CanvasCommand.TileMap ->
                    command.cells.forEachIndexed { index, color ->
                        if (color == 0) return@forEachIndexed
                        val column = index % command.columns
                        val row = index / command.columns
                        val innerWidth =
                            command.cellWidth - command.inset * 2
                        val innerHeight =
                            command.cellHeight - command.inset * 2
                        val radius =
                            minOf(
                                3,
                                minOf(innerWidth, innerHeight) / 3,
                            )
                        drawRoundRect(
                            color = colors[color],
                            topLeft =
                                Offset(
                                    (
                                        command.x +
                                            column * command.cellWidth +
                                            command.inset
                                    ) * scaleX,
                                    (
                                        command.y +
                                            row * command.cellHeight +
                                            command.inset
                                    ) * scaleY,
                                ),
                            size =
                                Size(
                                    innerWidth * scaleX,
                                    innerHeight * scaleY,
                                ),
                            cornerRadius =
                                androidx.compose.ui.geometry.CornerRadius(
                                    radius * scaleX,
                                    radius * scaleY,
                                ),
                        )
                    }
            }
        }
    }
}

@Composable
private fun PackageImage(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
    cornerRadiusDp: Int = 24,
) {
    val assetHash = requireNotNull(node.props.primaryText)
    val assetManager = LocalContext.current.assets
    val bitmap =
        remember(assetManager, assetHash) {
            DimgPackageAssets.decode(assetManager, assetHash)
        }
    val imageModifier =
        modifier
            .clip(RoundedCornerShape(cornerRadiusDp.dp))
            .appSpecNode(node, context.evidenceCollector)
    if (bitmap != null) {
        Image(
            bitmap = bitmap,
            contentDescription = node.semantics.label,
            modifier = imageModifier,
            contentScale =
                if (node.props.variant == "contain") {
                    ContentScale.Fit
                } else {
                    ContentScale.Crop
                },
        )
        return
    }

    val surface = MaterialTheme.colorScheme.surfaceContainerHigh
    val primary = MaterialTheme.colorScheme.primary
    val dim = MaterialTheme.colorScheme.onSurfaceVariant
    Canvas(
        modifier =
            imageModifier
                .background(surface),
    ) {
        drawCircle(
            color = primary.copy(alpha = 0.35f),
            radius = size.minDimension * 0.23f,
            center = center,
        )
        drawLine(
            color = dim,
            start = center.copy(x = size.width * 0.28f),
            end = center.copy(x = size.width * 0.72f),
            strokeWidth = size.minDimension * 0.055f,
        )
        drawLine(
            color = dim,
            start = center.copy(y = size.height * 0.28f),
            end = center.copy(y = size.height * 0.72f),
            strokeWidth = size.minDimension * 0.055f,
        )
    }
}

@Composable
private fun AppSpecButton(
    node: SceneNode,
    context: RenderContext,
) {
    val tap = node.action("tap")
    val longPress = node.action("long_press")
    val variant = requireNotNull(node.props.variant)
    val colors =
        when (variant) {
            "filled" -> ButtonDefaults.buttonColors()
            "tonal" -> ButtonDefaults.filledTonalButtonColors()
            "outlined" -> ButtonDefaults.outlinedButtonColors()
            "text" -> ButtonDefaults.childButtonColors()
            else -> error("Unsupported button variant $variant")
        }
    val border: BorderStroke? =
        if (variant == "outlined") {
            ButtonDefaults.outlinedButtonBorder(node.enabled)
        } else {
            null
        }
    val minimumHeight =
        when (node.props.size) {
            "compact" -> 40.dp
            "default" -> 48.dp
            "large" -> 56.dp
            else -> error("Unsupported button size ${node.props.size}")
        }
    val onClick: () -> Unit = {
        tap?.let {
            context.dispatch(node, it)
        }
        Unit
    }
    val onLongClick =
        longPress?.let { action ->
            {
                context.dispatch(node, action)
            }
        }
    val modifier =
        Modifier
            .fillMaxWidth()
            .heightIn(min = minimumHeight)
            .appSpecNode(node, context.evidenceCollector)
    val content: @Composable () -> Unit = {
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            node.props.icon?.let { icon ->
                if (icon.startsWith("utility_") || icon.startsWith("condition_")) {
                    WeatherGlyph(
                        icon = WeatherIcons.fromWireName(icon),
                        modifier = Modifier.size(18.dp),
                        contentDescription = null,
                    )
                } else {
                    Text(icon.symbol())
                }
            }
            Text(
                text = requireNotNull(node.props.primaryText),
                maxLines = if (node.props.size == "compact") 1 else 3,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
    if (node.props.size == "compact") {
        CompactButton(
            onClick = onClick,
            onLongClick = onLongClick,
            onLongClickLabel = longPress?.let { node.semantics.label },
            enabled = node.enabled,
            modifier = modifier,
            colors = colors,
            border = border,
            label = { content() },
        )
        return
    }
    when (variant) {
        "filled" ->
            Button(
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = longPress?.let { node.semantics.label },
                enabled = node.enabled,
                modifier = modifier,
            ) {
                content()
            }
        "tonal" ->
            FilledTonalButton(
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = longPress?.let { node.semantics.label },
                enabled = node.enabled,
                modifier = modifier,
            ) {
                content()
            }
        "outlined" ->
            OutlinedButton(
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = longPress?.let { node.semantics.label },
                enabled = node.enabled,
                modifier = modifier,
            ) {
                content()
            }
        "text" ->
            ChildButton(
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = longPress?.let { node.semantics.label },
                enabled = node.enabled,
                modifier = modifier,
            ) {
                content()
            }
    }
}

@Composable
private fun AppSpecIcon(
    node: SceneNode,
    context: RenderContext,
) {
    val dimension =
        when (node.props.size) {
            "compact" -> 18.dp
            "default" -> 24.dp
            "large" -> 32.dp
            "hero" -> 64.dp
            else -> error("Unsupported icon size ${node.props.size}")
        }
    WeatherGlyph(
        icon = WeatherIcons.fromWireName(requireNotNull(node.props.icon)),
        modifier =
            Modifier
                .size(dimension)
                .appSpecNode(node, context.evidenceCollector),
        contentDescription = node.semantics.label.takeIf(String::isNotEmpty),
    )
}

@Composable
private fun AppSpecSurface(
    node: SceneNode,
    context: RenderContext,
) {
    val shape =
        when (node.props.variant) {
            "standard" -> MaterialTheme.shapes.medium
            "hero" -> MaterialTheme.shapes.extraLarge
            "metric_a" -> RoundedCornerShape(22.dp, 14.dp, 22.dp, 14.dp)
            "metric_b" -> RoundedCornerShape(14.dp, 24.dp, 14.dp, 24.dp)
            "metric_c" -> CutCornerShape(10.dp)
            "pill" -> CircleShape
            else -> error("Unsupported surface shape ${node.props.variant}")
        }
    val container =
        when (node.props.tone) {
            "primary" -> MaterialTheme.colorScheme.primaryContainer
            "secondary" -> MaterialTheme.colorScheme.secondaryContainer
            "tertiary" -> MaterialTheme.colorScheme.tertiaryContainer
            "error" -> MaterialTheme.colorScheme.errorContainer
            else -> MaterialTheme.colorScheme.surfaceContainer
        }
    val tap = node.action("tap")
    val interaction =
        if (tap == null) {
            Modifier
        } else {
            Modifier.clickable(
                enabled = node.enabled,
                onClick = { context.dispatch(node, tap) },
            )
        }
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .clip(shape)
                .background(container)
                .then(interaction)
                .padding(
                    if (node.props.variant in setOf("hero", "pill")) 4.dp else 8.dp,
                )
                .appSpecNode(node, context.evidenceCollector),
        horizontalAlignment = node.props.alignment.horizontalAlignment(),
        verticalArrangement = Arrangement.spacedBy(node.props.gap.spacing()),
    ) {
        context.snapshot.childrenOf(node).filter { it.visible }.forEach { child ->
            AppSpecComponentRegistry.Render(
                child,
                context.snapshot,
                context.profile,
                context.evidenceCollector,
                context.onAction,
            )
        }
    }
}

@Composable
private fun AppSpecChart(
    node: SceneNode,
    context: RenderContext,
) {
    val samples = requireNotNull(node.props.samples)
    val maximum = requireNotNull(node.props.maximum).coerceAtLeast(1)
    val color =
        when (node.props.tone) {
            "tertiary" -> MaterialTheme.colorScheme.tertiary
            "error" -> MaterialTheme.colorScheme.error
            "secondary" -> MaterialTheme.colorScheme.secondary
            else -> MaterialTheme.colorScheme.primary
        }
    val grid = MaterialTheme.colorScheme.outlineVariant
    Canvas(
        modifier =
            Modifier
                .fillMaxWidth()
                .height(32.dp)
                .semantics { contentDescription = node.semantics.label }
                .appSpecNode(node, context.evidenceCollector),
    ) {
        drawLine(grid, Offset(0f, size.height - 1f), Offset(size.width, size.height - 1f), 1f)
        val slot = size.width / samples.size
        if (node.props.variant == "bars") {
            samples.forEachIndexed { index, sample ->
                val height = size.height * sample / maximum
                drawRoundRect(
                    color = color,
                    topLeft = Offset(index * slot + slot * 0.18f, size.height - height),
                    size = Size(slot * 0.64f, height),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(slot * 0.18f),
                )
            }
        } else {
            val points = samples.mapIndexed { index, sample ->
                Offset(
                    x = if (samples.size == 1) size.width / 2f else index * size.width / (samples.size - 1),
                    y = size.height - size.height * sample / maximum,
                )
            }
            points.zipWithNext().forEach { (start, end) ->
                drawLine(color, start, end, 3f, cap = androidx.compose.ui.graphics.StrokeCap.Round)
            }
            points.forEach { drawCircle(color, 3.5f, it) }
        }
    }
}

@Composable
private fun AppSpecPager(
    node: SceneNode,
    context: RenderContext,
) {
    val pages = context.snapshot.childrenOf(node).filter { it.visible }
    val selected = requireNotNull(node.props.value).coerceIn(0, pages.lastIndex)
    val pagerState =
        rememberPagerState(initialPage = selected) { pages.size }
    val pageChanged = node.action("page_changed")
    LaunchedEffect(pagerState.settledPage) {
        if (pagerState.settledPage != selected && pageChanged != null) {
            context.dispatch(
                node,
                pageChanged,
                ReferenceActionPayload.Number(pagerState.settledPage),
            )
        }
    }
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
    ) {
        HorizontalPager(
            state = pagerState,
            modifier = Modifier.fillMaxWidth(),
        ) { page ->
            AppSpecComponentRegistry.Render(
                pages[page],
                context.snapshot,
                context.profile,
                context.evidenceCollector,
                context.onAction,
            )
        }
        if (node.props.checked == true && pages.size > 1) {
            Row(
                modifier = Modifier.align(Alignment.BottomCenter),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                pages.indices.forEach { page ->
                    Box(
                        Modifier
                            .size(if (page == pagerState.currentPage) 6.dp else 4.dp)
                            .clip(CircleShape)
                            .background(
                                if (page == pagerState.currentPage) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.outlineVariant
                                },
                            ),
                    )
                }
            }
        }
    }
}

@Composable
private fun AppSpecCard(
    node: SceneNode,
    context: RenderContext,
) {
    val tap = node.action("tap")
    val modifier =
        Modifier
            .fillMaxWidth()
            .appSpecNode(node, context.evidenceCollector)
    val title: @Composable () -> Unit = {
        Text(
            requireNotNull(node.props.primaryText),
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
    val body: @Composable () -> Unit = {
        Text(
            requireNotNull(node.props.secondaryText),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 3,
            overflow = TextOverflow.Ellipsis,
        )
    }
    if (tap == null) {
        TitleCard(
            title = { title() },
            modifier = modifier,
            content = body,
        )
    } else {
        TitleCard(
            onClick = { context.dispatch(node, tap) },
            enabled = node.enabled,
            title = { title() },
            modifier = modifier,
            content = body,
        )
    }
}

@Composable
private fun AppSpecProgress(
    node: SceneNode,
    context: RenderContext,
) {
    val value = requireNotNull(node.props.value)
    val maximum = requireNotNull(node.props.maximum)
    val progress = value.toFloat() / maximum.toFloat()
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        node.props.primaryText?.takeIf(String::isNotEmpty)?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        when (node.props.variant) {
            "linear" ->
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = node.enabled,
                )
            "circular" ->
                CircularProgressIndicator(
                    progress = { progress },
                    modifier = Modifier.size(92.dp),
                    enabled = node.enabled,
                )
            "segmented" ->
                SegmentedCircularProgressIndicator(
                    segmentCount = 12,
                    progress = { progress },
                    modifier = Modifier.size(92.dp),
                    enabled = node.enabled,
                )
            else -> error("Unsupported progress variant ${node.props.variant}")
        }
        Text(
            text = node.semantics.value ?: "$value / $maximum",
            style = MaterialTheme.typography.labelSmall,
        )
    }
}

@Composable
private fun AppSpecStepper(
    node: SceneNode,
    context: RenderContext,
) {
    val action =
        node.action("value_committed")
            ?: node.action("value_changing")
    val minimum = requireNotNull(node.props.minimum)
    val maximum = requireNotNull(node.props.maximum)
    val step = requireNotNull(node.props.step)
    val current = requireNotNull(node.props.value)
    val isFullScreen =
        node.parentId == context.snapshot.root.id &&
            context.snapshot.childrenOf(context.snapshot.root)
                .count { it.visible } == 1
    if (!isFullScreen) {
        InlineAppSpecStepper(
            node = node,
            context = context,
            action = requireNotNull(action),
            current = current,
            minimum = minimum,
            maximum = maximum,
            step = step,
        )
        return
    }
    val progression =
        IntProgression.fromClosedRange(
            minimum,
            maximum,
            step,
        )
    Stepper(
        value = current,
        onValueChange = { value ->
            action?.let {
                context.dispatch(
                    node,
                    it,
                    ReferenceActionPayload.Number(value),
                )
            }
        },
        valueProgression = progression,
        modifier =
            Modifier
                .fillMaxWidth()
                .height(128.dp)
                .appSpecNode(node, context.evidenceCollector),
        enabled = node.enabled && action != null,
        decreaseIcon = {
            Text("−", style = MaterialTheme.typography.titleLarge)
        },
        increaseIcon = {
            Text("+", style = MaterialTheme.typography.titleLarge)
        },
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                requireNotNull(node.props.primaryText),
                style = MaterialTheme.typography.labelSmall,
            )
            Text(
                requireNotNull(node.props.value).toString(),
                style = MaterialTheme.typography.displaySmall,
            )
            node.props.secondaryText?.takeIf(String::isNotEmpty)?.let {
                Text(
                    it,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun InlineAppSpecStepper(
    node: SceneNode,
    context: RenderContext,
    action: SceneAction,
    current: Int,
    minimum: Int,
    maximum: Int,
    step: Int,
    modifier: Modifier = Modifier.fillMaxWidth(),
) {
    ButtonGroup(
        modifier =
            modifier
                .appSpecNode(node, context.evidenceCollector),
        spacing = 4.dp,
        expansionWidth = 8.dp,
        contentPadding = PaddingValues(0.dp),
    ) {
        CompactButton(
            onClick = {
                context.dispatch(
                    node,
                    action,
                    ReferenceActionPayload.Number(
                        (current - step).coerceAtLeast(minimum),
                    ),
                )
            },
            enabled = node.enabled && current > minimum,
            modifier =
                Modifier
                    .weight(1f)
                    .testTag("${node.id}.decrease"),
            colors = ButtonDefaults.filledTonalButtonColors(),
            label = {
                Text("−")
            },
        )
        Box(
            modifier =
                Modifier
                    .weight(1.7f)
                    .height(48.dp)
                    .clip(MaterialTheme.shapes.medium)
                    .background(
                        MaterialTheme.colorScheme.surfaceContainerHigh,
                    ),
            contentAlignment = Alignment.Center,
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    text = current.toString(),
                    style = MaterialTheme.typography.numeralSmall,
                )
                Text(
                    text =
                        node.props.secondaryText
                            ?.takeIf(String::isNotEmpty)
                            ?: requireNotNull(node.props.primaryText),
                    style = MaterialTheme.typography.bodyExtraSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        CompactButton(
            onClick = {
                context.dispatch(
                    node,
                    action,
                    ReferenceActionPayload.Number(
                        (current + step).coerceAtMost(maximum),
                    ),
                )
            },
            enabled = node.enabled && current < maximum,
            modifier =
                Modifier
                    .weight(1f)
                    .testTag("${node.id}.increase"),
            label = {
                Text("+")
            },
        )
    }
}

@Composable
private fun AppSpecToggle(
    node: SceneNode,
    context: RenderContext,
) {
    val action = node.action("checked_changed")
    SwitchButton(
        checked = requireNotNull(node.props.checked),
        onCheckedChange = { checked ->
            action?.let {
                context.dispatch(
                    node,
                    it,
                    ReferenceActionPayload.Checked(checked),
                )
            }
        },
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        enabled = node.enabled && action != null,
        label = {
            Text(requireNotNull(node.props.primaryText))
        },
    )
}

@Composable
private fun AppSpecKeypad(
    node: SceneNode,
    context: RenderContext,
) {
    val action = node.action("tap")
    val keys = requireNotNull(node.props.keys)
    val columns = requireNotNull(node.props.keyColumns)
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        keys.chunked(columns).forEachIndexed { rowIndex, row ->
            ButtonGroup(
                modifier = Modifier.fillMaxWidth(),
                spacing = 2.dp,
                expansionWidth = 6.dp,
                contentPadding = PaddingValues(0.dp),
            ) {
                row.forEachIndexed { columnIndex, key ->
                    val keyIndex = rowIndex * columns + columnIndex
                    CompactButton(
                        onClick = {
                            action?.let {
                                context.dispatch(
                                    node,
                                    it,
                                    ReferenceActionPayload.Text(key),
                                )
                            }
                        },
                        enabled = node.enabled && action != null,
                        modifier =
                            Modifier
                                .weight(1f)
                                .testTag("${node.id}.key.$keyIndex")
                                .semantics {
                                    contentDescription =
                                        "$key, ${node.semantics.label}"
                                    doodadNodeId = node.id
                                    doodadActionIds =
                                        action?.actionId.orEmpty()
                                },
                        colors =
                            if (key in setOf("=", "Go")) {
                                ButtonDefaults.buttonColors()
                            } else {
                                ButtonDefaults.filledTonalButtonColors()
                            },
                        label = {
                            Text(key, maxLines = 1)
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun AppSpecVoiceOrb(
    node: SceneNode,
    context: RenderContext,
) {
    val action = node.actions.firstOrNull()
    val active = node.props.state in setOf("listening", "thinking", "speaking")
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(
            modifier =
                Modifier
                    .size(76.dp)
                    .clip(CircleShape)
                    .background(
                        if (active) {
                            MaterialTheme.colorScheme.primaryContainer
                        } else {
                            MaterialTheme.colorScheme.surfaceContainerHigh
                        },
                    )
                    .border(
                        3.dp,
                        MaterialTheme.colorScheme.primary,
                        CircleShape,
                    ),
            contentAlignment = Alignment.Center,
        ) {
            CompactButton(
                onClick = {
                    action?.let {
                        context.dispatch(node, it)
                    }
                },
                enabled = node.enabled && action != null,
                modifier = Modifier.size(64.dp),
                label = {
                    Text(
                        text = requireNotNull(node.props.primaryText),
                        textAlign = TextAlign.Center,
                    )
                },
            )
        }
        node.props.secondaryText?.takeIf(String::isNotEmpty)?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.bodySmall,
                textAlign = TextAlign.Center,
                maxLines = 3,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun AppSpecLiveCard(
    node: SceneNode,
    context: RenderContext,
) {
    TitleCard(
        title = {
            Text(
                text = requireNotNull(node.props.primaryText),
                style = MaterialTheme.typography.titleSmall,
            )
        },
        modifier =
            Modifier
                .fillMaxWidth()
                .appSpecNode(node, context.evidenceCollector),
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                text = requireNotNull(node.props.secondaryText),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (node.props.value != null) {
                val maximum = requireNotNull(node.props.maximum)
                LinearProgressIndicator(
                    progress = {
                        requireNotNull(node.props.value).toFloat() /
                            maximum.toFloat()
                    },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = node.enabled,
                )
            }
        }
    }
}

private fun Modifier.appSpecNode(
    node: SceneNode,
    collector: ComposeNodeEvidenceCollector?,
): Modifier =
    this
        .testTag(node.id)
        .semantics(mergeDescendants = false) {
            contentDescription = node.semantics.label
            node.semantics.stateDescription?.let {
                stateDescription = it
            }
            doodadNodeId = node.id
            doodadActionIds =
                node.actions.joinToString(",") { it.actionId }
            doodadActionKinds =
                node.actions.joinToString(",") { it.kind }
        }
        .onGloballyPositioned {
            collector?.record(node.id, it.boundsInRoot())
        }

private fun RenderContext.dispatch(
    node: SceneNode,
    action: SceneAction,
    payload: ReferenceActionPayload = ReferenceActionPayload.None,
) {
    onAction(
        ReferenceActionEnvelope(
            nodeId = node.id,
            actionId = action.actionId,
            eventKind = action.kind,
            payload = payload,
        ),
    )
}

private fun SceneNode.action(kind: String): SceneAction? =
    actions.singleOrNull { it.kind == kind }

private fun String.symbol(): String =
    when (this) {
        "add" -> "+"
        "remove" -> "−"
        "play" -> "▶"
        "pause" -> "Ⅱ"
        "stop" -> "■"
        "next" -> ">"
        "previous" -> "<"
        else -> error("No deterministic icon mapping for $this")
    }

private fun String.calculatorGlyph(): String =
    when (this) {
        "+/-" -> "±"
        "/" -> "÷"
        "*" -> "×"
        "<-" -> "⌫"
        else -> this
    }

@Composable
private fun String?.textStyle(): TextStyle =
    when (this) {
        "display" -> MaterialTheme.typography.displayMedium
        "title" -> MaterialTheme.typography.titleMedium
        "label" -> MaterialTheme.typography.labelMedium
        "body" -> MaterialTheme.typography.bodyMedium
        "numeral" -> MaterialTheme.typography.numeralLarge
        "caption" -> MaterialTheme.typography.bodyExtraSmall
        else -> error("Unsupported text variant $this")
    }

private fun String?.textAlign(): TextAlign =
    when (this) {
        "start", "stretch" -> TextAlign.Start
        "center" -> TextAlign.Center
        "end" -> TextAlign.End
        else -> error("Unsupported text alignment $this")
    }

private fun String?.horizontalAlignment(): Alignment.Horizontal =
    when (this) {
        "start" -> Alignment.Start
        "center" -> Alignment.CenterHorizontally
        "end" -> Alignment.End
        "stretch" -> Alignment.CenterHorizontally
        else -> error("Unsupported container alignment $this")
    }

private fun String?.horizontalArrangementAlignment(): Alignment.Horizontal =
    when (this) {
        "start", "stretch" -> Alignment.Start
        "center" -> Alignment.CenterHorizontally
        "end" -> Alignment.End
        else -> error("Unsupported row alignment $this")
    }

private fun String?.spacing() =
    when (this) {
        "none" -> 0.dp
        "xs" -> 2.dp
        "sm" -> 4.dp
        "md" -> 8.dp
        "lg" -> 12.dp
        else -> error("Unsupported gap $this")
    }

val DoodadNodeIdKey = SemanticsPropertyKey<String>("DoodadNodeId")
var SemanticsPropertyReceiver.doodadNodeId by DoodadNodeIdKey

val DoodadActionIdsKey = SemanticsPropertyKey<String>("DoodadActionIds")
var SemanticsPropertyReceiver.doodadActionIds by DoodadActionIdsKey

val DoodadActionKindsKey = SemanticsPropertyKey<String>("DoodadActionKinds")
var SemanticsPropertyReceiver.doodadActionKinds by DoodadActionKindsKey
