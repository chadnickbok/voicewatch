package dev.doodad.reference

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.CompositionLocalProvider
import androidx.wear.compose.foundation.LocalAmbientModeManager
import androidx.wear.compose.foundation.rememberAmbientModeManager
import dev.doodad.reference.model.SceneSnapshotRepository
import dev.doodad.reference.ui.AppSpecReferenceRenderer
import dev.doodad.reference.ui.ReferenceGeometryProfile
import dev.doodad.reference.ui.ReferenceLabApp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val initialScene = intent.getStringExtra(EXTRA_SCENE)
        val snapshotAsset = intent.getStringExtra(EXTRA_SNAPSHOT_ASSET)
        val runtimeSnapshot =
            snapshotAsset?.let {
                SceneSnapshotRepository(assets).load(it)
            }
        val runtimeProfile =
            intent
                .getStringExtra(EXTRA_PROFILE)
                ?.let(ReferenceGeometryProfile::fromId)
                ?: ReferenceGeometryProfile.WatchSquare240
        setContent {
            val ambientModeManager = rememberAmbientModeManager()
            CompositionLocalProvider(LocalAmbientModeManager provides ambientModeManager) {
                if (runtimeSnapshot == null) {
                    ReferenceLabApp(initialScene = initialScene)
                } else {
                    AppSpecReferenceRenderer(
                        snapshot = runtimeSnapshot.snapshot,
                        profile = runtimeProfile,
                    )
                }
            }
        }
    }

    companion object {
        const val EXTRA_SCENE = "oracle_scene"
        const val EXTRA_SNAPSHOT_ASSET = "parallax_snapshot_asset"
        const val EXTRA_PROFILE = "parallax_profile"
    }
}
