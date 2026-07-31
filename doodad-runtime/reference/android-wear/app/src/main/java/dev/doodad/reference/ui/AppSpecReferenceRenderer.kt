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
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.SemanticsPropertyKey
import androidx.compose.ui.semantics.SemanticsPropertyReceiver
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
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
import dev.doodad.reference.model.SceneNode
import dev.doodad.reference.model.SceneSnapshot
import dev.doodad.reference.model.SceneSnapshotValidator
import dev.doodad.reference.model.ThemeSpec

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

    ReferenceTheme(spec = theme) {
        AppScaffold(timeText = {}) {
            Box(
                modifier =
                    modifier
                        .fillMaxSize()
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
    if (pattern == AppSpecPattern.Empty) {
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
    if (pattern == AppSpecPattern.Keypad) {
        SquareKeypadSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.Countdown) {
        SquareCountdownSurface(children, context)
        return
    }
    if (pattern == AppSpecPattern.WeatherHero) {
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
            Box(Modifier.weight(1f)) {
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
        style = node.props.variant.textStyle(),
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
private fun PackageImage(
    node: SceneNode,
    context: RenderContext,
    modifier: Modifier,
) {
    val assetHash = requireNotNull(node.props.primaryText)
    val assetManager = LocalContext.current.assets
    val bitmap =
        remember(assetManager, assetHash) {
            DimgPackageAssets.decode(assetManager, assetHash)
        }
    val imageModifier =
        modifier
            .clip(RoundedCornerShape(24.dp))
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
    val label =
        node.props.icon?.let { icon ->
            "${icon.symbol()} ${requireNotNull(node.props.primaryText)}"
        } ?: requireNotNull(node.props.primaryText)

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
        Text(
            text = label,
            maxLines = if (node.props.size == "compact") 1 else 3,
            overflow = TextOverflow.Ellipsis,
        )
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
