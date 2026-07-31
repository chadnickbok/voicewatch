package dev.doodad.reference

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.CompositionLocalProvider
import androidx.wear.compose.foundation.LocalAmbientModeManager
import androidx.wear.compose.foundation.rememberAmbientModeManager
import dev.doodad.reference.ui.ReferenceLabApp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val initialScene = intent.getStringExtra(EXTRA_SCENE)
        setContent {
            val ambientModeManager = rememberAmbientModeManager()
            CompositionLocalProvider(LocalAmbientModeManager provides ambientModeManager) {
                ReferenceLabApp(initialScene = initialScene)
            }
        }
    }

    companion object {
        const val EXTRA_SCENE = "oracle_scene"
    }
}
