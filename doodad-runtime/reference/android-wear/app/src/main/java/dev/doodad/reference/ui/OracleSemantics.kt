package dev.doodad.reference.ui

import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.platform.testTag
import dev.doodad.reference.model.ReferenceScenario
import dev.doodad.reference.model.SemanticNode

internal fun ReferenceScenario.semanticNode(id: String): SemanticNode =
    expectedSemantics.flatten().firstOrNull { it.id == id }
        ?: error("Scenario ${this.id} does not declare semantic node $id")

internal fun Modifier.oracleNode(
    scenario: ReferenceScenario,
    id: String,
    mergeDescendants: Boolean = false,
): Modifier {
    val node = scenario.semanticNode(id)
    return testTag(id).semantics(mergeDescendants = mergeDescendants) {
        contentDescription = node.label
        node.stateDescription?.let { stateDescription = it }
    }
}
