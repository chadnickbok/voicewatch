package dev.doodad.reference.ui

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.wear.compose.foundation.lazy.TransformingLazyColumn
import androidx.wear.compose.foundation.lazy.rememberTransformingLazyColumnState
import androidx.wear.compose.material3.AppScaffold
import androidx.wear.compose.material3.Button
import androidx.wear.compose.material3.ButtonDefaults
import androidx.wear.compose.material3.ButtonGroup
import androidx.wear.compose.material3.CircularProgressIndicator
import androidx.wear.compose.material3.CompactButton
import androidx.wear.compose.material3.ConfirmationDialog
import androidx.wear.compose.material3.ConfirmationDialogDefaults
import androidx.wear.compose.material3.EdgeButton
import androidx.wear.compose.material3.EdgeButtonSize
import androidx.wear.compose.material3.ListHeader
import androidx.wear.compose.material3.MaterialTheme
import androidx.wear.compose.material3.ScreenScaffold
import androidx.wear.compose.material3.Stepper
import androidx.wear.compose.material3.SurfaceTransformation
import androidx.wear.compose.material3.SwitchButton
import androidx.wear.compose.material3.Text
import androidx.wear.compose.material3.TextButton
import androidx.wear.compose.material3.TextButtonDefaults
import androidx.wear.compose.material3.TitleCard
import androidx.wear.compose.material3.lazy.rememberTransformationSpec
import androidx.wear.compose.material3.lazy.transformedHeight
import androidx.wear.compose.ui.tooling.preview.WearPreviewDevices
import androidx.wear.compose.ui.tooling.preview.WearPreviewFontScales
import dev.doodad.reference.model.InteractionSpec
import dev.doodad.reference.model.ReferenceScenario
import dev.doodad.reference.model.SemanticNode
import dev.doodad.reference.model.ThemeSpec
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.floatOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put

@Composable
fun OracleScene(scenario: ReferenceScenario) {
    ReferenceTheme(spec = scenario.theme) { ambient ->
        AppScaffold(timeText = {}) {
            when (scenario.scene) {
                "transforming-list" -> TransformingListScene(scenario)
                "hero-metric" -> HeroMetricScene(scenario)
                "two-button-group" -> TwoButtonGroupScene(scenario)
                "timer-running" -> TimerRunningScene(scenario)
                "calculator-keypad" -> CalculatorKeypadScene(scenario)
                "workout-set-entry" -> WorkoutSetEntryScene(scenario)
                "calorie-dashboard" -> CalorieDashboardScene(scenario)
                "confirmation" -> ConfirmationScene(scenario)
                "theme-switcher" -> ThemeSwitcherScene(scenario)
                "ambient-live-activity" -> AmbientLiveActivityScene(scenario, ambient)
                else -> error("Unsupported oracle scene: ${scenario.scene}")
            }
        }
    }
}

@Composable
private fun TransformingListScene(scenario: ReferenceScenario) {
    val listState = rememberTransformingLazyColumnState()
    val transformationSpec = rememberTransformationSpec()
    val items = scenario.data.stringList("items")

    Box(
        Modifier
            .fillMaxSize()
            .oracleNode(scenario, "screen.transforming-list"),
    ) {
        ScreenScaffold(scrollState = listState) { contentPadding ->
            Box(
                Modifier
                    .fillMaxSize()
                    .oracleNode(scenario, "list.catalog"),
            ) {
                TransformingLazyColumn(
                    state = listState,
                    contentPadding = contentPadding,
                ) {
                    item {
                        ListHeader(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .transformedHeight(this, transformationSpec),
                            transformation = SurfaceTransformation(transformationSpec),
                        ) {
                            Text("Today")
                        }
                    }
                    items.forEach { label ->
                        item(key = label) {
                            val semanticId = "item.${label.slug()}"
                            Button(
                                onClick = {},
                                modifier =
                                    Modifier
                                        .fillMaxWidth()
                                        .transformedHeight(this, transformationSpec)
                                        .oracleNodeIfDeclared(scenario, semanticId),
                                transformation = SurfaceTransformation(transformationSpec),
                                label = {
                                    Text(label)
                                },
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun HeroMetricScene(scenario: ReferenceScenario) {
    val label = scenario.data.string("label")
    val value = scenario.data.string("value")
    val unit = scenario.data.string("unit")
    val progress = scenario.data.float("progress")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .oracleNode(scenario, "screen.hero-metric"),
        contentAlignment = Alignment.Center,
    ) {
        CircularProgressIndicator(
            progress = { progress },
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(10.dp)
                    .oracleNode(scenario, "progress.move"),
            startAngle = 135f,
            endAngle = 45f,
        )
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = value,
                style = MaterialTheme.typography.displayMedium,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = unit,
                modifier = Modifier.oracleNode(scenario, "value.move"),
                style = MaterialTheme.typography.labelMedium,
            )
        }
    }
}

@Composable
private fun TwoButtonGroupScene(scenario: ReferenceScenario) {
    val leftSource = remember { MutableInteractionSource() }
    val rightSource = remember { MutableInteractionSource() }
    val left = scenario.data.string("left")
    val right = scenario.data.string("right")

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(horizontal = 10.dp)
                .oracleNode(scenario, "screen.two-button-group"),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = scenario.data.string("label"),
            style = MaterialTheme.typography.titleMedium,
        )
        Spacer(Modifier.height(12.dp))
        ButtonGroup(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .oracleNode(scenario, "group.quick-actions"),
            contentPadding = PaddingValues(horizontal = 8.dp),
        ) {
            TextButton(
                onClick = {},
                modifier =
                    Modifier
                        .weight(1f)
                        .animateWidth(leftSource)
                        .oracleNode(scenario, "button.pause"),
                interactionSource = leftSource,
                shapes = TextButtonDefaults.animatedShapes(),
                colors = TextButtonDefaults.filledTonalTextButtonColors(),
            ) {
                Text(left)
            }
            TextButton(
                onClick = {},
                modifier =
                    Modifier
                        .weight(1f)
                        .animateWidth(rightSource)
                        .oracleNode(scenario, "button.finish"),
                interactionSource = rightSource,
                shapes = TextButtonDefaults.animatedShapes(),
                colors = TextButtonDefaults.filledTextButtonColors(),
            ) {
                Text(right)
            }
        }
    }
}

@Composable
private fun TimerRunningScene(scenario: ReferenceScenario) {
    val listState = rememberTransformingLazyColumnState()
    val progress = scenario.data.float("progress")
    val remaining = scenario.data.string("remaining")

    Box(
        Modifier
            .fillMaxSize()
            .oracleNode(scenario, "screen.timer"),
    ) {
        ScreenScaffold(
            scrollState = listState,
            edgeButton = {
                EdgeButton(
                    onClick = {},
                    modifier = Modifier.oracleNode(scenario, "button.timer-action"),
                    buttonSize = EdgeButtonSize.Small,
                ) {
                    Text(scenario.data.string("action"))
                }
            },
        ) { contentPadding ->
            TransformingLazyColumn(
                state = listState,
                contentPadding = contentPadding,
            ) {
                item {
                    Box(
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .height(122.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        CircularProgressIndicator(
                            progress = { progress },
                            modifier =
                                Modifier
                                    .size(116.dp)
                                    .oracleNode(scenario, "progress.timer"),
                            startAngle = 130f,
                            endAngle = 50f,
                        )
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text(
                                text = scenario.data.string("label"),
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                text = remaining,
                                modifier = Modifier.oracleNode(scenario, "value.timer"),
                                style = MaterialTheme.typography.displayMedium,
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CalculatorKeypadScene(scenario: ReferenceScenario) {
    val rows = scenario.data.stringRows("rows")

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp, vertical = 10.dp)
                .oracleNode(scenario, "screen.calculator"),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = scenario.data.string("expression"),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.labelMedium,
        )
        Text(
            text = scenario.data.string("result"),
            modifier = Modifier.oracleNode(scenario, "value.calculator"),
            style = MaterialTheme.typography.displaySmall,
        )
        Spacer(Modifier.height(4.dp))
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .oracleNode(scenario, "group.keypad"),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            rows.forEach { row ->
                ButtonGroup(
                    modifier = Modifier.fillMaxWidth(),
                    spacing = 2.dp,
                    expansionWidth = 6.dp,
                    contentPadding = PaddingValues(horizontal = 0.dp),
                ) {
                    row.forEach { key ->
                        val semanticId = "button.key-${key.keySlug()}"
                        CompactButton(
                            onClick = {},
                            modifier =
                                Modifier
                                    .weight(1f)
                                    .oracleNodeIfDeclared(scenario, semanticId),
                            colors =
                                if (key == "=") {
                                    ButtonDefaults.buttonColors()
                                } else {
                                    ButtonDefaults.filledTonalButtonColors()
                                },
                            label = {
                                Text(key)
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun WorkoutSetEntryScene(scenario: ReferenceScenario) {
    val minimum = scenario.data.int("minimum_reps")
    val maximum = scenario.data.int("maximum_reps")
    var reps by remember(scenario.id) {
        mutableIntStateOf(scenario.data.int("reps"))
    }

    Box(
        Modifier
            .fillMaxSize()
            .oracleNode(scenario, "screen.workout-set"),
    ) {
        Stepper(
            value = reps,
            onValueChange = { reps = it },
            valueProgression = minimum..maximum,
            modifier = Modifier.oracleNode(scenario, "stepper.reps"),
            decreaseIcon = {
                Text("−", style = MaterialTheme.typography.titleLarge)
            },
            increaseIcon = {
                Text("+", style = MaterialTheme.typography.titleLarge)
            },
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    text = scenario.data.string("exercise"),
                    style = MaterialTheme.typography.labelMedium,
                    textAlign = TextAlign.Center,
                )
                Text(
                    text = reps.toString(),
                    modifier = Modifier.oracleNode(scenario, "value.reps"),
                    style = MaterialTheme.typography.displayMedium,
                )
                Text(
                    text = scenario.data.string("set"),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.labelSmall,
                )
            }
        }
    }
}

@Composable
private fun CalorieDashboardScene(scenario: ReferenceScenario) {
    val consumed = scenario.data.int("consumed")
    val goal = scenario.data.int("goal")
    val remaining = scenario.data.int("remaining")
    val progress = scenario.data.float("progress")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .oracleNode(scenario, "screen.calories"),
        contentAlignment = Alignment.Center,
    ) {
        CircularProgressIndicator(
            progress = { progress },
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(10.dp)
                    .oracleNode(scenario, "progress.calories"),
            startAngle = 130f,
            endAngle = 50f,
        )
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = "Calories",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = "%,d".format(consumed),
                modifier = Modifier.oracleNode(scenario, "value.calories"),
                style = MaterialTheme.typography.displayMedium,
            )
            Text(
                text = "of %,d".format(goal),
                style = MaterialTheme.typography.labelSmall,
            )
            Spacer(Modifier.height(6.dp))
            TitleCard(
                onClick = {},
                modifier =
                    Modifier
                        .fillMaxWidth(0.72f)
                        .oracleNode(scenario, "value.remaining"),
                title = {
                    Text("$remaining left")
                },
            ) {
                Text("Daily goal")
            }
        }
    }
}

@Composable
private fun ConfirmationScene(scenario: ReferenceScenario) {
    Box(
        Modifier
            .fillMaxSize()
            .oracleNode(scenario, "screen.confirmation"),
    )
    ConfirmationDialog(
        visible = true,
        onDismissRequest = {},
        text = {
            Text(
                text = scenario.data.string("message"),
                textAlign = TextAlign.Center,
            )
        },
        modifier = Modifier.oracleNode(scenario, "dialog.confirmation"),
        durationMillis = 60_000L,
    ) {
        ConfirmationDialogDefaults.SuccessIcon()
    }
}

@Composable
private fun ThemeSwitcherScene(scenario: ReferenceScenario) {
    val listState = rememberTransformingLazyColumnState()
    var violet by remember { mutableStateOf(true) }
    var dynamicColor by remember {
        mutableStateOf(scenario.data.boolean("dynamic_color"))
    }
    var reducedMotion by remember {
        mutableStateOf(scenario.data.boolean("reduced_motion"))
    }

    Box(
        Modifier
            .fillMaxSize()
            .oracleNode(scenario, "screen.theme-switcher"),
    ) {
        ScreenScaffold(scrollState = listState) { contentPadding ->
            TransformingLazyColumn(
                state = listState,
                contentPadding = contentPadding,
            ) {
                item {
                    ListHeader {
                        Text("Theme")
                    }
                }
                item {
                    SwitchButton(
                        checked = violet,
                        onCheckedChange = { violet = it },
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .oracleNode(scenario, "toggle.violet"),
                        label = {
                            Text("Violet")
                        },
                    )
                }
                item {
                    SwitchButton(
                        checked = dynamicColor,
                        onCheckedChange = { dynamicColor = it },
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .oracleNode(scenario, "toggle.dynamic-color"),
                        label = {
                            Text("Dynamic color")
                        },
                    )
                }
                item {
                    SwitchButton(
                        checked = reducedMotion,
                        onCheckedChange = { reducedMotion = it },
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .oracleNode(scenario, "toggle.reduced-motion"),
                        label = {
                            Text("Reduced motion")
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun AmbientLiveActivityScene(
    scenario: ReferenceScenario,
    ambient: Boolean,
) {
    val progress = scenario.data.float("progress")

    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .oracleNode(scenario, "screen.ambient-activity"),
        contentAlignment = Alignment.Center,
    ) {
        CircularProgressIndicator(
            progress = { progress },
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(if (ambient) 16.dp else 10.dp)
                    .oracleNode(scenario, "progress.activity"),
            startAngle = 130f,
            endAngle = 50f,
        )
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = scenario.data.string("label"),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = scenario.data.string("value"),
                modifier = Modifier.oracleNode(scenario, "value.activity-time"),
                style = MaterialTheme.typography.displayMedium,
            )
            Text(
                text = scenario.data.string("secondary"),
                modifier = Modifier.oracleNode(scenario, "value.activity-distance"),
                style = MaterialTheme.typography.labelLarge,
            )
            if (ambient) {
                Text(
                    text = "AMBIENT",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.outline,
                )
            }
        }
    }
}

private fun Modifier.oracleNodeIfDeclared(
    scenario: ReferenceScenario,
    id: String,
): Modifier =
    if (scenario.expectedSemantics.flatten().any { it.id == id }) {
        oracleNode(scenario, id)
    } else {
        this
    }

private fun JsonObject.string(key: String): String =
    get(key)?.jsonPrimitive?.contentOrNull
        ?: error("Missing string data.$key")

private fun JsonObject.int(key: String): Int =
    get(key)?.jsonPrimitive?.intOrNull
        ?: error("Missing integer data.$key")

private fun JsonObject.float(key: String): Float =
    get(key)?.jsonPrimitive?.floatOrNull
        ?: error("Missing number data.$key")

private fun JsonObject.boolean(key: String): Boolean =
    get(key)?.jsonPrimitive?.contentOrNull?.toBooleanStrictOrNull()
        ?: error("Missing boolean data.$key")

private fun JsonObject.stringList(key: String): List<String> =
    get(key)?.jsonArray?.map { it.jsonPrimitive.content }
        ?: error("Missing array data.$key")

private fun JsonObject.stringRows(key: String): List<List<String>> =
    get(key)?.jsonArray?.map { row ->
        row.jsonArray.map { it.jsonPrimitive.content }
    } ?: error("Missing nested array data.$key")

private fun String.slug(): String =
    lowercase()
        .replace(Regex("[^a-z0-9]+"), "-")
        .trim('-')

private fun String.keySlug(): String =
    when (this) {
        "=" -> "equals"
        "×" -> "multiply"
        "÷" -> "divide"
        else -> slug()
    }

@WearPreviewDevices
@WearPreviewFontScales
@Composable
private fun HeroMetricPreview() {
    OracleScene(PreviewScenario)
}

private val PreviewScenario =
    ReferenceScenario(
        schemaVersion = 1,
        id = "preview.hero",
        scene = "hero-metric",
        title = "Hero metric",
        data =
            buildJsonObject {
                put("label", "Move")
                put("value", "7,420")
                put("unit", "steps")
                put("progress", 0.74)
            },
        uiState = buildJsonObject { put("status", "current") },
        theme =
            ThemeSpec(
                colorScheme = "baseline-dark",
                typography = "wear-default",
                shapes = "wear-default",
                motionScheme = "expressive",
                dynamicColor = false,
                ambient = false,
                reducedMotion = false,
            ),
        renderProfiles = listOf("wear_round_small"),
        fontScale = 1f,
        interaction = InteractionSpec(state = "resting", animationFraction = 0f),
        expectedSemantics =
            SemanticNode(
                id = "screen.hero-metric",
                role = "screen",
                label = "Move progress",
                children =
                    listOf(
                        SemanticNode(
                            id = "progress.move",
                            role = "progress",
                            label = "Move progress",
                        ),
                        SemanticNode(
                            id = "value.move",
                            role = "text",
                            label = "7,420 steps",
                        ),
                    ),
            ),
    )
