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

## Create the reference emulators

The setup script installs the official arm64 image on Apple Silicon (x86_64 on
Intel) and creates:

- `doodad_wear7_small_round`
- `doodad_wear7_large_round`
- `doodad_wear61_small_round`

It never overwrites an existing AVD:

```bash
./scripts/setup-reference-avds.sh
emulator -avd doodad_wear7_small_round
```

The system images are large and are therefore not downloaded by ordinary build
or test commands. The API 36.1 emulator is a compatibility lane. Its renderer
has a documented issue with some dashed arcs and Tile circular progress
indicators, so those differences must not become LVGL requirements.

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
