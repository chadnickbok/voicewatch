package dev.doodad.reference.ui

import androidx.compose.ui.geometry.Rect
import dev.doodad.reference.model.SceneAction
import dev.doodad.reference.model.SceneNode
import dev.doodad.reference.model.SceneSnapshot
import kotlin.math.roundToInt
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

@Serializable
data class ComposeNodeEvidence(
    @SerialName("schema_version") val schemaVersion: Int = 1,
    @SerialName("snapshot_sha256") val snapshotSha256: String,
    @SerialName("capture_phase") val capturePhase: EvidenceCapturePhase,
    val renderer: EvidenceRenderer,
    @SerialName("profile_id") val profileId: String,
    @SerialName("physical_width_px") val physicalWidthPx: Int,
    @SerialName("physical_height_px") val physicalHeightPx: Int,
    val nodes: List<EvidenceNode>,
) {
    fun toJson(): String =
        EvidenceJson.encodeToString(this) + "\n"
}

@Serializable
@OptIn(ExperimentalSerializationApi::class)
data class EvidenceCapturePhase(
    val id: String = "resting",
    val state: String = "resting",
    @SerialName("animation_fraction_milli")
    val animationFractionMilli: Int = 0,
    @EncodeDefault(EncodeDefault.Mode.NEVER)
    val target: String? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER)
    @SerialName("scroll_anchor")
    val scrollAnchor: String? = null,
)

@Serializable
data class EvidenceRenderer(
    val kind: String = "compose",
    val mode: String = "host",
    val version: String = "wear-compose-1.6.2",
    @SerialName("build_sha256")
    val buildSha256: String = DefaultComposeRendererBuildSha256,
)

@Serializable
@OptIn(ExperimentalSerializationApi::class)
data class EvidenceNode(
    val id: String,
    @SerialName("parent_id") val parentId: String?,
    val role: String,
    val label: String,
    val value: String,
    @SerialName("state_description") val stateDescription: String,
    val visible: Boolean,
    val enabled: Boolean,
    @EncodeDefault(EncodeDefault.Mode.NEVER)
    val checked: Boolean? = null,
    val actions: List<SceneAction>,
    @SerialName("bounds_px") val boundsPx: EvidenceBounds,
    @SerialName("bounds_dp_q8_8") val boundsDpQ8_8: EvidenceBounds,
    @SerialName("token_roles") val tokenRoles: Map<String, String>,
)

@Serializable
data class EvidenceBounds(
    val x: Int,
    val y: Int,
    val width: Int,
    val height: Int,
)

class ComposeNodeEvidenceCollector {
    private val boundsByNode = linkedMapOf<String, Rect>()

    internal fun record(
        nodeId: String,
        bounds: Rect,
    ) {
        boundsByNode[nodeId] = bounds
    }

    fun clear() {
        boundsByNode.clear()
    }

    fun build(
        snapshot: SceneSnapshot,
        snapshotSha256: String,
        profile: ReferenceGeometryProfile,
        density: Float,
        capturePhase: EvidenceCapturePhase = EvidenceCapturePhase(),
        rendererBuildSha256: String = DefaultComposeRendererBuildSha256,
    ): ComposeNodeEvidence {
        require(snapshotSha256.matches(Regex("^[0-9a-f]{64}$"))) {
            "snapshotSha256 must be a lowercase SHA-256 digest"
        }
        require(rendererBuildSha256.matches(Regex("^[0-9a-f]{64}$"))) {
            "rendererBuildSha256 must be a lowercase SHA-256 digest"
        }
        require(density > 0f) { "density must be positive" }

        return ComposeNodeEvidence(
            snapshotSha256 = snapshotSha256,
            capturePhase = capturePhase,
            renderer = EvidenceRenderer(buildSha256 = rendererBuildSha256),
            profileId = profile.id,
            physicalWidthPx = profile.physicalWidthPx,
            physicalHeightPx = profile.physicalHeightPx,
            nodes =
                snapshot.nodes.map { node ->
                    val bounds = boundsByNode[node.id]
                    EvidenceNode(
                        id = node.id,
                        parentId = node.parentId,
                        role = node.semantics.role,
                        label = node.semantics.label,
                        value = node.semantics.value.orEmpty(),
                        stateDescription =
                            node.semantics.stateDescription.orEmpty(),
                        visible = node.visible,
                        enabled = node.enabled,
                        checked = node.props.checked,
                        actions = node.actions,
                        boundsPx = bounds.toEvidenceBounds(1f),
                        boundsDpQ8_8 = bounds.toEvidenceBounds(256f / density),
                        tokenRoles = node.tokenRoles(),
                    )
                },
        )
    }
}

private fun Rect?.toEvidenceBounds(scale: Float): EvidenceBounds {
    if (this == null) {
        return EvidenceBounds(0, 0, 0, 0)
    }
    return EvidenceBounds(
        x = (left * scale).roundToInt(),
        y = (top * scale).roundToInt(),
        width = (width * scale).roundToInt().coerceAtLeast(0),
        height = (height * scale).roundToInt().coerceAtLeast(0),
    )
}

private fun SceneNode.tokenRoles(): Map<String, String> =
    when (kind) {
        "screen" -> mapOf("background" to "background")
        "column", "row", "scroll" -> mapOf("layout" to "surface")
        "text" -> mapOf("typography" to requireNotNull(props.variant))
        "button" ->
            mapOf(
                "container" to
                    if (props.variant in setOf("outlined", "text")) {
                        "transparent"
                    } else {
                        requireNotNull(props.tone)
                    },
                "content" to "on_surface",
            )
        "card", "live_card" ->
            mapOf(
                "container" to "surface_container",
                "content" to "on_surface",
            )
        "progress" ->
            mapOf("indicator" to requireNotNull(props.tone))
        "stepper" ->
            mapOf(
                "control" to "primary",
                "content" to "on_surface",
            )
        "toggle" ->
            mapOf("control" to requireNotNull(props.tone))
        "keypad" ->
            mapOf(
                "container" to "surface_container",
                "content" to "on_surface",
            )
        "voice_orb" ->
            mapOf("container" to requireNotNull(props.tone))
        else -> error("No normalized token-role mapping for $kind")
    }

private val EvidenceJson =
    Json {
        encodeDefaults = true
        explicitNulls = true
        prettyPrint = true
    }

const val DefaultComposeRendererBuildSha256: String =
    "b6cc78b1300f94f8008736b4b7a1b8f098864aa0a8e70fc8c44698d15aa58b56"
