package dev.doodad.reference

import android.graphics.BitmapFactory
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.unit.Density
import com.github.takahirom.roborazzi.ExperimentalRoborazziApi
import com.github.takahirom.roborazzi.RoborazziOptions
import com.github.takahirom.roborazzi.RoborazziTaskType
import com.github.takahirom.roborazzi.captureScreenRoboImage
import dev.doodad.reference.model.SceneSnapshotRepository
import dev.doodad.reference.ui.AppSpecReferenceRenderer
import dev.doodad.reference.ui.ComposeNodeEvidenceCollector
import dev.doodad.reference.ui.DefaultComposeRendererBuildSha256
import dev.doodad.reference.ui.EvidenceCapturePhase
import dev.doodad.reference.ui.ReferenceGeometryProfile
import java.io.BufferedOutputStream
import java.io.File
import java.security.MessageDigest
import java.util.Locale
import java.util.TimeZone
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [33])
@RunWith(RobolectricTestRunner::class)
class SceneSnapshotBatchCaptureTest {
    @get:Rule
    val composeRule = createComposeRule()

    @OptIn(ExperimentalRoborazziApi::class)
    @Test
    fun captureSnapshotsAtSquare240() {
        val requests = captureRequests()
        assumeTrue(
            "Set -Pparallax.manifest or both single-capture properties",
            requests.isNotEmpty(),
        )
        withDeterministicJvmDefaults {
        val repository =
            SceneSnapshotRepository(
                RuntimeEnvironment.getApplication().assets,
            )
        val targets =
            requests.map { request ->
                require(request.fontScaleMilli == 1000 || request.fontScaleMilli == 1300) {
                    "Font scale must be 1000 or 1300"
                }
                val snapshotFile = File(request.snapshot).canonicalFile
                val outputFile = File(request.output).canonicalFile
                require(snapshotFile.isFile) {
                    "SceneSnapshot does not exist: $snapshotFile"
                }
                require(outputFile.extension.lowercase() == "png") {
                    "Capture output must end in .png: $outputFile"
                }
                outputFile.parentFile?.mkdirs()
                CaptureTarget(
                    outputFile = outputFile,
                    sha256 = snapshotFile.readBytes().sha256(),
                    snapshot = repository.decode(snapshotFile.readText()),
                    collector = ComposeNodeEvidenceCollector(),
                    capturePhase = request.capturePhase,
                    captureState = request.captureState,
                    fontScale = request.fontScaleMilli / 1000f,
                )
            }
        val profile = ReferenceGeometryProfile.WatchSquare240
        RuntimeEnvironment.setQualifiers(profile.qualifier())
        composeRule.mainClock.autoAdvance = true
        var activeTarget by mutableStateOf(targets.first())
        composeRule.setContent {
            CompositionLocalProvider(
                LocalDensity provides Density(1.25f, activeTarget.fontScale),
            ) {
                key(activeTarget.sha256, activeTarget.fontScale) {
                    AppSpecReferenceRenderer(
                        snapshot = activeTarget.snapshot,
                        profile = profile,
                        evidenceCollector = activeTarget.collector,
                    )
                }
            }
        }
        val rendererBuildSha256 =
            System.getProperty("parallax.rendererBuildSha256")
                ?: DefaultComposeRendererBuildSha256

        targets.forEachIndexed { index, target ->
            if (index > 0) {
                composeRule.runOnUiThread {
                    activeTarget = target
                }
            }
            composeRule.waitForIdle()
            target.snapshot.nodes
                .filter { it.visible }
                .forEach { node ->
                    composeRule
                        .onNodeWithTag(node.id, useUnmergedTree = true)
                        .assertContentDescriptionEquals(
                            node.semantics.label,
                        )
                }
            captureScreenRoboImage(
                filePath = target.outputFile.path,
                roborazziOptions =
                    RoborazziOptions(
                        taskType = RoborazziTaskType.Record,
                        recordOptions =
                            RoborazziOptions.RecordOptions(
                                applyDeviceCrop = true,
                            ),
                    ),
            )
            target.outputFile.writeRgb888(profile)
            val evidence =
                target.collector.build(
                    snapshot = target.snapshot,
                    snapshotSha256 = target.sha256,
                    profile = profile,
                    density = 1.25f,
                    capturePhase =
                        EvidenceCapturePhase(
                            id = target.capturePhase,
                            state = target.captureState,
                        ),
                    rendererBuildSha256 = rendererBuildSha256,
                )
            target.outputFile
                .resolveSibling(
                    target.outputFile.nameWithoutExtension +
                        ".node-evidence.json",
                )
                .writeText(evidence.toJson())
        }
        }
    }

    private fun captureRequests(): List<BatchCaptureRequest> {
        val manifest = System.getProperty("parallax.manifest")
        if (!manifest.isNullOrBlank()) {
            val file = File(manifest).canonicalFile
            require(file.isFile) { "Batch manifest does not exist: $file" }
            return BatchJson.decodeFromString(file.readText())
        }
        val snapshot = System.getProperty("parallax.snapshot")
        val output = System.getProperty("parallax.output")
        if (snapshot.isNullOrBlank() || output.isNullOrBlank()) {
            return emptyList()
        }
        return listOf(BatchCaptureRequest(snapshot, output))
    }
}

private inline fun withDeterministicJvmDefaults(block: () -> Unit) {
    val previousLocale = Locale.getDefault()
    val previousTimeZone = TimeZone.getDefault()
    try {
        Locale.setDefault(Locale.US)
        TimeZone.setDefault(TimeZone.getTimeZone("UTC"))
        block()
    } finally {
        Locale.setDefault(previousLocale)
        TimeZone.setDefault(previousTimeZone)
    }
}

@Serializable
private data class BatchCaptureRequest(
    val snapshot: String,
    val output: String,
    @kotlinx.serialization.SerialName("capture_phase")
    val capturePhase: String = "resting",
    @kotlinx.serialization.SerialName("capture_state")
    val captureState: String = "resting",
    @kotlinx.serialization.SerialName("font_scale_milli")
    val fontScaleMilli: Int = 1000,
)

private data class CaptureTarget(
    val outputFile: File,
    val sha256: String,
    val snapshot: dev.doodad.reference.model.SceneSnapshot,
    val collector: ComposeNodeEvidenceCollector,
    val capturePhase: String,
    val captureState: String,
    val fontScale: Float,
)

@Serializable
private data class Rgb888Metadata(
    @kotlinx.serialization.SerialName("schema_version")
    val schemaVersion: Int = 1,
    val width: Int,
    val height: Int,
    @kotlinx.serialization.SerialName("stride_bytes")
    val strideBytes: Int,
    @kotlinx.serialization.SerialName("pixel_format")
    val pixelFormat: String = "rgb888",
    @kotlinx.serialization.SerialName("byte_order")
    val byteOrder: String = "r_g_b",
    val bytes: Int,
)

private fun File.writeRgb888(profile: ReferenceGeometryProfile) {
    val bitmap =
        requireNotNull(BitmapFactory.decodeFile(path)) {
            "Could not decode captured PNG: $this"
        }
    require(
        bitmap.width == profile.physicalWidthPx &&
            bitmap.height == profile.physicalHeightPx,
    ) {
        "Expected ${profile.physicalWidthPx}x${profile.physicalHeightPx} " +
            "capture, got ${bitmap.width}x${bitmap.height}"
    }
    val rgbFile =
        resolveSibling(nameWithoutExtension + ".rgb888")
    val row = IntArray(bitmap.width)
    BufferedOutputStream(rgbFile.outputStream()).use { output ->
        repeat(bitmap.height) { y ->
            bitmap.getPixels(
                row,
                0,
                bitmap.width,
                0,
                y,
                bitmap.width,
                1,
            )
            row.forEach { pixel ->
                output.write((pixel shr 16) and 0xFF)
                output.write((pixel shr 8) and 0xFF)
                output.write(pixel and 0xFF)
            }
        }
    }
    val metadata =
        Rgb888Metadata(
            width = bitmap.width,
            height = bitmap.height,
            strideBytes = bitmap.width * 3,
            bytes = bitmap.width * bitmap.height * 3,
        )
    resolveSibling(nameWithoutExtension + ".rgb888.json")
        .writeText(BatchJson.encodeToString(metadata) + "\n")
    bitmap.recycle()
}

private fun ByteArray.sha256(): String =
    MessageDigest.getInstance("SHA-256")
        .digest(this)
        .joinToString("") { "%02x".format(it) }

private val BatchJson =
    Json {
        ignoreUnknownKeys = false
        encodeDefaults = true
        explicitNulls = false
        prettyPrint = true
    }
