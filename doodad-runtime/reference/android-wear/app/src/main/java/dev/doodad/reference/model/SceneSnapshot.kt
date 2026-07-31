package dev.doodad.reference.model

import android.content.res.AssetManager
import java.security.MessageDigest
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.decodeFromJsonElement

@Serializable
data class SceneSnapshot(
    @SerialName("schema_version") val schemaVersion: Int,
    @SerialName("app_id") val appId: String,
    @SerialName("screen_id") val screenId: String,
    val origin: String,
    val nodes: List<SceneNode>,
) {
    val root: SceneNode
        get() = nodes.first()

    fun childrenOf(parent: SceneNode): List<SceneNode> =
        nodes.filter { it.parentId == parent.id }
}

@Serializable
data class SceneNode(
    val id: String,
    @SerialName("parent_id") val parentId: String?,
    val kind: String,
    val depth: Int,
    @SerialName("child_count") val childCount: Int,
    val visible: Boolean,
    val enabled: Boolean,
    val props: SceneProps,
    val semantics: SceneSemantics,
    val actions: List<SceneAction>,
)

@Serializable
data class SceneProps(
    @SerialName("primary_text") val primaryText: String? = null,
    @SerialName("secondary_text") val secondaryText: String? = null,
    val variant: String? = null,
    val tone: String? = null,
    val size: String? = null,
    val gap: String? = null,
    val alignment: String? = null,
    val value: Int? = null,
    val minimum: Int? = null,
    val maximum: Int? = null,
    val step: Int? = null,
    val checked: Boolean? = null,
    val keys: List<String>? = null,
    @SerialName("key_columns") val keyColumns: Int? = null,
    val state: String? = null,
    val icon: String? = null,
    @SerialName("max_lines") val maxLines: Int? = null,
) {
    internal fun presentFields(): Set<String> =
        buildSet {
            if (primaryText != null) add("primary_text")
            if (secondaryText != null) add("secondary_text")
            if (variant != null) add("variant")
            if (tone != null) add("tone")
            if (this@SceneProps.size != null) add("size")
            if (gap != null) add("gap")
            if (alignment != null) add("alignment")
            if (value != null) add("value")
            if (minimum != null) add("minimum")
            if (maximum != null) add("maximum")
            if (step != null) add("step")
            if (checked != null) add("checked")
            if (keys != null) add("keys")
            if (keyColumns != null) add("key_columns")
            if (state != null) add("state")
            if (icon != null) add("icon")
            if (maxLines != null) add("max_lines")
        }
}

@Serializable
data class SceneSemantics(
    val role: String,
    val label: String,
    val value: String? = null,
    val hint: String? = null,
    @SerialName("state_description") val stateDescription: String? = null,
)

@Serializable
data class SceneAction(
    val kind: String,
    @SerialName("action_id") val actionId: String,
)

data class LoadedSceneSnapshot(
    val assetPath: String,
    val sha256: String,
    val snapshot: SceneSnapshot,
)

class SceneSnapshotRepository(
    private val assets: AssetManager,
    private val json: Json = strictSceneSnapshotJson(),
) {
    fun loadAll(): List<LoadedSceneSnapshot> =
        snapshotAssetPaths().map(::load)

    fun load(assetPath: String): LoadedSceneSnapshot {
        require(
            assetPath.matches(
                Regex(
                    "^[a-z][a-z0-9-]*/decisive/snapshots/" +
                        "[0-9a-f]{64}\\.json$",
                ),
            ),
        ) {
            "Unsafe SceneSnapshot asset path: $assetPath"
        }
        val bytes = assets.open(assetPath).use { it.readBytes() }
        val sha256 = bytes.sha256()
        check(assetPath.substringAfterLast('/').removeSuffix(".json") == sha256) {
            "SceneSnapshot asset hash does not match its filename: $assetPath"
        }
        return LoadedSceneSnapshot(
            assetPath = assetPath,
            sha256 = sha256,
            snapshot = decode(bytes.decodeToString()),
        )
    }

    fun decode(source: String): SceneSnapshot {
        val element = json.parseToJsonElement(source)
        rejectUnexpectedNulls(element)
        return json.decodeFromJsonElement<SceneSnapshot>(element).also(
            SceneSnapshotValidator::validate,
        )
    }

    fun snapshotAssetPaths(): List<String> =
        Doodad20AppSlugs
            .asSequence()
            .flatMap { app ->
                val directory = "$app/decisive/snapshots"
                assets.list(directory).orEmpty()
                    .asSequence()
                    .filter { it.matches(Regex("^[0-9a-f]{64}\\.json$")) }
                    .map { "$directory/$it" }
            }
            .sorted()
            .toList()

    private fun rejectUnexpectedNulls(
        element: JsonElement,
        path: String = "$",
    ) {
        when (element) {
            JsonNull ->
                check(path.endsWith(".parent_id")) {
                    "$path must not be null"
                }
            is JsonObject ->
                element.forEach { (key, value) ->
                    rejectUnexpectedNulls(value, "$path.$key")
                }
            is JsonArray ->
                element.forEachIndexed { index, value ->
                    rejectUnexpectedNulls(value, "$path[$index]")
                }
            else -> Unit
        }
    }
}

val Doodad20AppSlugs: List<String> =
    listOf(
        "timer",
        "weather",
        "notifications",
        "tasks",
        "calculator",
        "calendar",
        "workout",
        "calories",
        "voice-notes",
        "medication",
        "sensor-recorder",
        "sleep",
        "media",
        "navigation",
        "transit",
        "smart-home",
        "sports",
        "wallet",
        "remote-control",
        "snake",
    )

object SceneSnapshotValidator {
    private val identifier = Regex("^[a-z][a-z0-9_.-]{0,95}$")
    private val origins =
        setOf("guest_appspec", "trusted_surface", "hybrid_projection")
    private val roles =
        setOf(
            "screen",
            "heading",
            "text",
            "button",
            "toggle",
            "progress",
            "list",
            "list_item",
            "dialog",
            "slider",
            "group",
        )
    private val actionKinds =
        setOf(
            "tap",
            "long_press",
            "repeat",
            "value_changing",
            "value_committed",
            "checked_changed",
            "page_changed",
            "dismissed",
            "submit",
            "retry",
            "cancel",
        )
    private val propsByKind =
        mapOf(
            "screen" to PropertyRule(setOf("gap", "alignment")),
            "column" to PropertyRule(setOf("gap", "alignment")),
            "row" to PropertyRule(setOf("gap", "alignment")),
            "scroll" to PropertyRule(setOf("gap", "alignment")),
            "text" to
                PropertyRule(
                    setOf("primary_text", "variant", "alignment", "max_lines"),
                    setOf("primary_text", "variant", "alignment"),
                ),
            "button" to
                PropertyRule(
                    setOf("primary_text", "variant", "tone", "size", "icon"),
                    setOf("primary_text", "variant", "tone", "size"),
                ),
            "card" to
                PropertyRule(
                    setOf("primary_text", "secondary_text", "tone"),
                    setOf("primary_text", "secondary_text", "tone"),
                ),
            "progress" to
                PropertyRule(
                    setOf("primary_text", "value", "maximum", "variant", "tone"),
                    setOf("value", "maximum", "variant", "tone"),
                ),
            "stepper" to
                PropertyRule(
                    setOf(
                        "primary_text",
                        "secondary_text",
                        "value",
                        "minimum",
                        "maximum",
                        "step",
                    ),
                    setOf("primary_text", "value", "minimum", "maximum", "step"),
                ),
            "toggle" to
                PropertyRule(
                    setOf("primary_text", "tone", "checked"),
                    setOf("primary_text", "tone", "checked"),
                ),
            "keypad" to
                PropertyRule(
                    setOf("keys", "key_columns"),
                    setOf("keys", "key_columns"),
                ),
            "voice_orb" to
                PropertyRule(
                    setOf("primary_text", "secondary_text", "tone", "state"),
                    setOf("primary_text", "tone", "state"),
                ),
            "live_card" to
                PropertyRule(
                    setOf(
                        "primary_text",
                        "secondary_text",
                        "tone",
                        "value",
                        "maximum",
                    ),
                    setOf("primary_text", "secondary_text", "tone"),
                ),
        )

    fun validate(snapshot: SceneSnapshot) {
        check(snapshot.schemaVersion == 1) {
            "Unsupported SceneSnapshot schema version ${snapshot.schemaVersion}"
        }
        requireIdentifier(snapshot.appId, "app_id")
        requireIdentifier(snapshot.screenId, "screen_id")
        check(snapshot.origin in origins) {
            "Unsupported SceneSnapshot origin ${snapshot.origin}"
        }
        check(snapshot.nodes.size in 1..250) {
            "SceneSnapshot must contain between 1 and 250 nodes"
        }

        val seen = linkedSetOf<String>()
        val depths = mutableMapOf<String, Int>()
        snapshot.nodes.forEachIndexed { index, node ->
            validateNode(node, index, seen, depths)
            seen += node.id
            depths[node.id] = node.depth
        }
        val root = snapshot.nodes.first()
        check(root.kind == "screen" && root.parentId == null && root.depth == 0) {
            "The first SceneSnapshot node must be the root screen"
        }
        check(root.id == snapshot.screenId) {
            "screen_id must equal the root node id"
        }
        check(snapshot.nodes.count { it.parentId == null } == 1) {
            "SceneSnapshot must contain exactly one root"
        }
        snapshot.nodes.forEach { node ->
            val actual = snapshot.nodes.count { it.parentId == node.id }
            check(actual == node.childCount) {
                "${node.id}.child_count is $node.childCount but found $actual children"
            }
            if (node.kind !in setOf("screen", "column", "row", "scroll")) {
                check(node.childCount == 0) {
                    "Leaf component ${node.id} cannot have children"
                }
            }
        }
    }

    private fun validateNode(
        node: SceneNode,
        index: Int,
        seen: Set<String>,
        depths: Map<String, Int>,
    ) {
        requireIdentifier(node.id, "nodes[$index].id")
        check(node.id !in seen) { "Duplicate SceneSnapshot node id ${node.id}" }
        if (node.parentId == null) {
            check(index == 0) { "Only the first SceneSnapshot node may be a root" }
        } else {
            requireIdentifier(node.parentId, "nodes[$index].parent_id")
            check(node.parentId in seen) {
                "${node.id}.parent_id must precede the node"
            }
            val parent = requireNotNull(depths[node.parentId])
            check(node.depth == parent + 1) {
                "${node.id}.depth does not follow its parent"
            }
        }
        check(node.depth in 0..249 && node.childCount in 0..249) {
            "${node.id} has an invalid depth or child_count"
        }
        check(node.semantics.role in roles) {
            "${node.id} has unsupported semantic role ${node.semantics.role}"
        }
        check(node.semantics.label.length <= 128) {
            "${node.id} semantic label is too long"
        }
        listOfNotNull(
            node.semantics.value,
            node.semantics.hint,
            node.semantics.stateDescription,
        ).forEach {
            check(it.length <= 128) {
                "${node.id} semantic value is too long"
            }
        }
        check(node.actions.size <= 16) {
            "${node.id} has too many actions"
        }
        node.actions.forEach {
            check(it.kind in actionKinds) {
                "${node.id} has unsupported action kind ${it.kind}"
            }
            requireIdentifier(it.actionId, "${node.id}.action_id")
        }
        node.props.presentFields().forEach { field ->
            val text =
                when (field) {
                    "primary_text" -> node.props.primaryText
                    "secondary_text" -> node.props.secondaryText
                    "variant" -> node.props.variant
                    "tone" -> node.props.tone
                    "size" -> node.props.size
                    "gap" -> node.props.gap
                    "alignment" -> node.props.alignment
                    "state" -> node.props.state
                    "icon" -> node.props.icon
                    else -> null
                }
            if (text != null) {
                check(text.length <= 256) {
                    "${node.id}.$field is too long"
                }
            }
        }
        check(node.actions.distinct().size == node.actions.size) {
            "${node.id} contains duplicate actions"
        }
        check(node.actions.map { it.kind }.distinct().size == node.actions.size) {
            "${node.id} contains ambiguous actions of the same kind"
        }
        when (node.kind) {
            "button" ->
                check(node.actions.any { it.kind in setOf("tap", "long_press") }) {
                    "${node.id} button requires a tap or long_press action"
                }
            "stepper" ->
                check(
                    node.actions.any {
                        it.kind in setOf("value_committed", "value_changing")
                    },
                ) {
                    "${node.id} stepper requires a value action"
                }
            "toggle" ->
                check(node.actions.any { it.kind == "checked_changed" }) {
                    "${node.id} toggle requires a checked_changed action"
                }
            "keypad" ->
                check(node.actions.any { it.kind == "tap" }) {
                    "${node.id} keypad requires a tap action"
                }
        }
        validateProps(node)
    }

    private fun validateProps(node: SceneNode) {
        val rule =
            propsByKind[node.kind]
                ?: error("Unsupported SceneSnapshot component kind ${node.kind}")
        val present = node.props.presentFields()
        check(present.all { it in rule.allowed }) {
            "${node.id} has unsupported ${node.kind} properties: " +
                (present - rule.allowed).sorted().joinToString()
        }
        check(rule.required.all { it in present }) {
            "${node.id} is missing ${node.kind} properties: " +
                (rule.required - present).sorted().joinToString()
        }
        validateTokens(node)
    }

    private fun validateTokens(node: SceneNode) {
        node.props.gap?.let {
            check(it in setOf("none", "xs", "sm", "md", "lg")) {
                "${node.id} has unsupported gap $it"
            }
        }
        node.props.alignment?.let {
            check(it in setOf("start", "center", "end", "stretch")) {
                "${node.id} has unsupported alignment $it"
            }
        }
        node.props.tone?.let {
            check(it in setOf("primary", "secondary", "tertiary", "neutral", "error")) {
                "${node.id} has unsupported tone $it"
            }
        }
        node.props.size?.let {
            check(it in setOf("compact", "default", "large")) {
                "${node.id} has unsupported size $it"
            }
        }
        node.props.variant?.let {
            val allowed =
                when (node.kind) {
                    "text" ->
                        setOf(
                            "display",
                            "title",
                            "label",
                            "body",
                            "numeral",
                            "caption",
                        )
                    "button" -> setOf("filled", "tonal", "outlined", "text")
                    "progress" -> setOf("linear", "circular", "segmented")
                    else -> emptySet()
                }
            check(it in allowed) {
                "${node.id} has unsupported ${node.kind} variant $it"
            }
        }
        node.props.state?.let {
            check(it in setOf("idle", "listening", "thinking", "speaking", "error")) {
                "${node.id} has unsupported voice state $it"
            }
        }
        node.props.maxLines?.let {
            check(it in 1..4) { "${node.id}.max_lines must be between 1 and 4" }
        }
        node.props.keys?.let {
            check(it.size in 1..20 && it.all { key -> key.length in 1..4 }) {
                "${node.id} has invalid keypad keys"
            }
        }
        node.props.keyColumns?.let {
            check(it in 2..5) { "${node.id}.key_columns must be between 2 and 5" }
        }
        if (node.kind in setOf("progress", "live_card")) {
            val maximum = node.props.maximum
            val value = node.props.value
            if (maximum != null || value != null) {
                check(
                    maximum != null &&
                        maximum > 0 &&
                        value != null &&
                        value in 0..maximum
                ) {
                    "${node.id} requires a positive maximum with its value"
                }
            }
        }
        if (node.kind == "stepper") {
            val minimum = requireNotNull(node.props.minimum)
            val maximum = requireNotNull(node.props.maximum)
            val value = requireNotNull(node.props.value)
            val step = requireNotNull(node.props.step)
            check(step > 0 && maximum >= minimum && value in minimum..maximum) {
                "${node.id} has an invalid stepper range"
            }
        }
    }

    private fun requireIdentifier(value: String, field: String) {
        check(value.matches(identifier)) { "$field is not a valid identifier" }
    }

    private data class PropertyRule(
        val allowed: Set<String>,
        val required: Set<String> = emptySet(),
    )
}

fun strictSceneSnapshotJson(): Json =
    Json {
        ignoreUnknownKeys = false
        explicitNulls = false
        isLenient = false
        allowTrailingComma = false
        allowSpecialFloatingPointValues = false
    }

private fun ByteArray.sha256(): String =
    MessageDigest.getInstance("SHA-256")
        .digest(this)
        .joinToString("") { "%02x".format(it) }
