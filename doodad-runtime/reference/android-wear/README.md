# Wear Reference Lab

`wear-reference-lab` is Doodad's executable Material 3 Expressive oracle. It
renders deterministic semantic scenarios with Google's Wear Compose
implementation and records screenshots plus accessibility semantics for
comparison with the LVGL renderer.

It is a behavioral and stylistic oracle for the square watch, not a geometry
oracle. Round Wear profiles and `watch_square_240` intentionally share semantic
state, color roles, typography roles, shape and motion vocabulary while keeping
separate geometry.

## Pinned stack

- Wear Compose Material 3, Foundation, Navigation and UI Tooling: `1.6.2`
- Compose BOM: `2026.06.00`
- Android Gradle Plugin: `9.1.1`
- Gradle: `9.3.1`
- Kotlin: `2.3.20`
- compile/target SDK: Android API `37.0` / `37`
- Roborazzi: `1.59.0`
- Robolectric: `4.16.1`

The Material pin matches AndroidX commit
`f65727cc5cc63d05724c0edb55900bc8790b14e8`. Stable `1.6.2` defines Doodad v1;
the `1.7.0-alpha07` line is observed but does not change the oracle.

Official references:

- <https://developer.android.com/jetpack/androidx/releases/wear-compose>
- <https://developer.android.com/training/wearables/versions/7/setup>
- <https://developer.android.com/training/wearables/compose/screen-size>
- <https://github.com/android/wear-os-samples/tree/main/ComposeStarter>

## Source layout

```text
reference/
├── display-profiles.json
├── scenarios/
│   ├── index.json
│   └── *.json
└── android-wear/
    ├── app/
    │   ├── src/main/       # Catalog and Compose oracle renderers
    │   └── src/test/       # Roborazzi + semantic contract tests
    └── scripts/            # AVD setup, install, capture, record and diff
```

The scenarios are renderer-neutral. Their schema is
`contracts/reference-render-scenario-v1.schema.json`; dependency-free Python
validation lives in `tools/doodad_cli/reference_scenario.py`.

## Build and test

Android Studio's bundled JDK is selected automatically on macOS:

```bash
./scripts/build.sh
./gradlew :app:testDebugUnitTest
./gradlew :app:verifyRoborazziDebug
```

Record intentionally changed host goldens with:

```bash
./gradlew :app:recordRoborazziDebug
```

There are 30 initial goldens: ten scenes across small-round, large-round and
240px-square profiles. Every capture also asserts all declared semantic node
IDs and accessible labels. Actual Compose semantics trees are emitted under
`app/build/reports/reference-semantics/`.

Host-side goldens use Robolectric Native Graphics for deterministic regression
coverage. They do not replace captures from the API 37 Wear runtime.

## Render resolved Doodad scenes

`AppSpecReferenceRenderer` accepts the renderer-neutral `SceneSnapshot v1`
produced by Project Parallax. It validates the complete scene, selects one of
six generic structural patterns, dispatches all public AppSpec kinds through
an explicit registry, and never branches on app ID.

The normal entry point is the repository-level command, which captures all
twenty aligned Compose/LVGL pairs in one Compose test process:

```bash
cd ../..
./doodad perfect-render \
  --suite all-20 \
  --profile watch_square_240 \
  --output target/parallax/perfect-render-20
```

For a direct batch capture, pass a strict JSON array of absolute snapshot and
PNG output paths:

```json
[
  {
    "snapshot": "/absolute/scene-snapshot.json",
    "output": "/absolute/captures/timer.png"
  }
]
```

```bash
./gradlew :app:testDebugUnitTest \
  --tests dev.doodad.reference.SceneSnapshotBatchCaptureTest \
  -Pparallax.manifest=/absolute/batch.json \
  -Pparallax.rendererBuildSha256=<64-lowercase-hex>
```

For `timer.png`, the task also emits `timer.rgb888`,
`timer.rgb888.json`, and `timer.node-evidence.json`. Captures are exactly
240×240, use packed top-to-bottom RGB888, pin `en-US`, UTC, density 1.25,
font scale 1.0, and restore process locale/time-zone state after the run.

## Create the reference emulators

The setup script installs the official arm64 image on Apple Silicon (x86_64 on
Intel) and creates:

- `Wear_OS_Square` — primary API 37 runtime oracle, configured to the product
  contract at 240×240, 200 dpi / density 1.25, and 192×192dp after applying
  the runtime display override;
- `doodad_wear7_small_round`
- `doodad_wear7_large_round`
- `doodad_wear61_small_round`

It never overwrites an existing AVD:

```bash
./scripts/setup-reference-avds.sh
emulator -avd Wear_OS_Square
./scripts/configure-square-runtime.sh --serial emulator-5554
```

The system images are large and are therefore not downloaded by ordinary build
or test commands. The API 36.1 emulator is a compatibility lane. Its renderer
has a documented issue with some dashed arcs and Tile circular progress
indicators, so those differences must not become LVGL requirements.

The square API 37 AVD is the primary runtime authority for Parallax app-screen
comparisons. The round AVDs remain adaptive-design and system-surface
references. The host lane and the square AVD deliberately share the
`watch_square_240` geometry, so runtime images are never stretched to compare
with the product framebuffer.

The API 37 `wearos_square` skin currently starts with a 360×360 physical
framebuffer even when its AVD configuration requests 240×240. The configuration
script applies Android's supported `wm size 240x240` and `wm density 200`
overrides. The Parallax suite capture command applies and verifies those
overrides automatically after every boot.

## Capture the real runtime

Install the debug app on a running emulator:

```bash
./scripts/install-debug.sh --serial emulator-5554
```

Capture one resting screenshot and Android accessibility tree:

```bash
./scripts/capture-scene.sh \
    --serial emulator-5554 \
    --output captures/wear7-small \
    timer-running
```

Capture every indexed scene:

```bash
./scripts/capture-all.sh \
    --serial emulator-5554 \
    --output captures/wear7-small
```

Capture the exact twenty AppSpec snapshots used by Project Parallax on the
240×240 square API 37 runtime:

```bash
./scripts/capture-parallax-suite.sh \
    --serial emulator-5554 \
    --output ../../target/parallax/runtime-wear-square-240
```

The command establishes and then fails closed unless the device reports API 37,
a 240×240 display override, and Android density 200. It installs the reference
app, selects each content-addressed `SceneSnapshot` asset, and preserves a
native screenshot, accessibility XML, device fingerprint, SDK package
revisions, installed APK hash, snapshot hash, and capture manifest.

Compare those runtime captures with the exact same host-rendered snapshots:

```bash
python3 ./scripts/compare-parallax-runtime.py \
    --runtime ../../target/parallax/runtime-wear-square-240 \
    --host ../../target/parallax/perfect-render-20 \
    --output ../../target/parallax/runtime-wear-square-240/host-runtime-comparison.json
```

The comparator rejects different snapshot hashes and compares native 240×240
frames after the same canonical RGB565 quantization used by the LVGL report.

Record a normal-speed interaction session:

```bash
./scripts/record-scene.sh --serial emulator-5554 timer-running 15
```

The filenames are deterministic. Commit only reviewed oracle captures; local
`captures/` output is ignored.

## Compare with LVGL

The LVGL catalog currently emits BMP images. ImageMagick can normalize a
Compose PNG and LVGL BMP to the product viewport and produce a difference,
50/50 overlay, boundary images and RMSE report:

```bash
./scripts/compare-oracle.sh \
    app/src/test/screenshots/calculator-keypad_watch_square_240.png \
    ../../target/catalog/calculator.bmp \
    captures/diffs/calculator
```

RMSE is diagnostic, not the conformance definition. Review hierarchy, relative
sizing, emphasis, roles, state communication, motion, legibility and touch
targets independently.

## Initial catalog

1. Transforming list
2. Hero metric with arc progress
3. Two-button expressive group
4. Running timer with edge action
5. Calculator keypad
6. Workout set entry
7. Calorie dashboard
8. Confirmation dialog
9. Theme switcher
10. Ambient-mode live activity

Tiles remain a separate renderer. A future `tile` module should use
`androidx.wear.protolayout:protolayout-material3:1.4.1` and consume the same
semantic scenarios without routing Tile layout through Compose.
