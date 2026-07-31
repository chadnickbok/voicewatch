package dev.doodad.reference.model

import android.content.res.AssetManager
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject

@Serializable
data class ScenarioIndex(
    @SerialName("schema_version") val schemaVersion: Int,
    val scenarios: List<String>,
)

@Serializable
data class ReferenceScenario(
    @SerialName("schema_version") val schemaVersion: Int,
    val id: String,
    val scene: String,
    val title: String,
    val appspec: String? = null,
    @SerialName("lifecycle_scenario") val lifecycleScenario: String? = null,
    val data: JsonObject,
    @SerialName("ui_state") val uiState: JsonObject,
    val theme: ThemeSpec,
    @SerialName("render_profiles") val renderProfiles: List<String>,
    @SerialName("font_scale") val fontScale: Float,
    val interaction: InteractionSpec,
    @SerialName("expected_semantics") val expectedSemantics: SemanticNode,
)

@Serializable
data class ThemeSpec(
    @SerialName("color_scheme") val colorScheme: String,
    val typography: String,
    val shapes: String,
    @SerialName("motion_scheme") val motionScheme: String,
    @SerialName("dynamic_color") val dynamicColor: Boolean,
    val ambient: Boolean,
    @SerialName("reduced_motion") val reducedMotion: Boolean,
)

@Serializable
data class InteractionSpec(
    val state: String,
    val target: String? = null,
    @SerialName("animation_fraction") val animationFraction: Float,
)

@Serializable
data class SemanticNode(
    val id: String,
    val role: String,
    val label: String,
    val value: String? = null,
    @SerialName("state_description") val stateDescription: String? = null,
    val enabled: Boolean? = null,
    val selected: Boolean? = null,
    val checked: Boolean? = null,
    val children: List<SemanticNode> = emptyList(),
) {
    fun flatten(): List<SemanticNode> = listOf(this) + children.flatMap(SemanticNode::flatten)
}

class ScenarioRepository(
    private val assets: AssetManager,
    private val json: Json =
        Json {
            ignoreUnknownKeys = false
            explicitNulls = false
        },
) {
    fun loadAll(): List<ReferenceScenario> {
        val index = decode<ScenarioIndex>("index.json")
        check(index.schemaVersion == 1) { "Unsupported scenario index version" }
        check(index.scenarios.isNotEmpty()) { "Scenario index is empty" }
        check(index.scenarios.distinct().size == index.scenarios.size) {
            "Scenario index contains duplicates"
        }
        return index.scenarios.map { filename ->
            check(filename.endsWith(".json") && '/' !in filename && '\\' !in filename) {
                "Unsafe scenario filename: $filename"
            }
            decode<ReferenceScenario>(filename).also { scenario ->
                check(scenario.schemaVersion == 1) {
                    "Unsupported scenario version in $filename"
                }
            }
        }
    }

    private inline fun <reified T> decode(filename: String): T =
        assets.open(filename).bufferedReader(Charsets.UTF_8).use { reader ->
            json.decodeFromString<T>(reader.readText())
        }
}
