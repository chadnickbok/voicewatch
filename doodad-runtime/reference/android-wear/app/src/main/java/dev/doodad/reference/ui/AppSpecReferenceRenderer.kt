package dev.doodad.reference.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
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
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
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
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.SemanticsPropertyKey
import androidx.compose.ui.semantics.SemanticsPropertyReceiver
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.wear.compose.foundation.lazy.TransformingLazyColumn
import androidx.wear.compose.foundation.lazy.rememberTransformingLazyColumnState
import androidx.wear.compose.material3.AppScaffold
import androidx.wear.compose.material3.Button
import androidx.wear.compose.material3.ButtonDefaults
import androidx.wear.compose.material3.ButtonGroup
import androidx.wear.compose.material3.Card
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
    val children = snapshot.childrenOf(snapshot.root).filter { it.visible }
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
) {
    ButtonGroup(
        modifier =
            Modifier
                .fillMaxWidth()
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
