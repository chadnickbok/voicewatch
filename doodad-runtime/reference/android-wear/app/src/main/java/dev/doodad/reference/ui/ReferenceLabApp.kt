package dev.doodad.reference.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.wear.compose.foundation.lazy.TransformingLazyColumn
import androidx.wear.compose.foundation.lazy.rememberTransformingLazyColumnState
import androidx.wear.compose.material3.AppScaffold
import androidx.wear.compose.material3.Button
import androidx.wear.compose.material3.ListHeader
import androidx.wear.compose.material3.ScreenScaffold
import androidx.wear.compose.material3.Text
import dev.doodad.reference.model.ReferenceScenario
import dev.doodad.reference.model.ScenarioRepository

@Composable
fun ReferenceLabApp(initialScene: String? = null) {
    val context = LocalContext.current
    val scenarios =
        remember(context.applicationContext) {
            ScenarioRepository(context.applicationContext.assets).loadAll()
        }
    var selectedKey by rememberSaveable {
        mutableStateOf(
            initialScene?.takeIf { requested ->
                scenarios.any { it.id == requested || it.scene == requested }
            },
        )
    }
    val selected =
        selectedKey?.let { requested ->
            scenarios.firstOrNull { it.id == requested || it.scene == requested }
        }

    BackHandler(enabled = selected != null) {
        selectedKey = null
    }

    if (selected == null) {
        ReferenceTheme(spec = scenarios.first().theme) {
            CatalogScreen(
                scenarios = scenarios,
                onSelect = { selectedKey = it.id },
            )
        }
    } else {
        OracleScene(selected)
    }
}

@Composable
private fun CatalogScreen(
    scenarios: List<ReferenceScenario>,
    onSelect: (ReferenceScenario) -> Unit,
) {
    val state = rememberTransformingLazyColumnState()
    AppScaffold(timeText = {}) {
        ScreenScaffold(scrollState = state) { contentPadding ->
            TransformingLazyColumn(
                modifier = Modifier.fillMaxSize(),
                state = state,
                contentPadding = contentPadding,
            ) {
                item {
                    ListHeader {
                        Text("Wear reference lab")
                    }
                }
                scenarios.forEach { scenario ->
                    item(key = scenario.id) {
                        Box(Modifier.fillMaxWidth()) {
                            Button(
                                onClick = { onSelect(scenario) },
                                modifier = Modifier.fillMaxWidth(),
                                label = {
                                    Text(scenario.title)
                                },
                            )
                        }
                    }
                }
            }
        }
    }
}
