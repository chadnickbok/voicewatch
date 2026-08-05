# Doodad Runtime

This project implements a trusted, voice-first Doodad watch runtime for CoreS3
SE, with shared board/build support for T-Watch S3. The current overall
sequence is maintained in the [Doodad roadmap](docs/roadmap.md).

The runtime foundation includes:

1. A trusted ESP-IDF shell embeds and runs a Rust WebAssembly guest.
2. Owner-bound personal apps install into a multi-app registry on the 9.94 MiB
   onboard wear-levelled partition; optional legacy bare-Wasm and embedded
   recovery paths remain available.
3. A package-first desktop simulator builds the guest, validates its manifest
   and ABI, runs it in WAMR, and renders declarative UI through LVGL.

It now also contains a substantial Material 3 Expressive framework slice:

- a reproducible Wear Material 3 1.6.2 token extraction pipeline;
- RGB888 and quantized RGB565 themes with a shared live style registry;
- fixed-point 192dp display profiles;
- native LVGL action, card, list, selection, progress, picker, pager,
  navigation, dialog, voice, and system-status component families;
- an eight-object virtualized transforming list with keyed anchor preservation;
- semantic state, motion, haptic, and accessibility foundations;
- a bounded native keyed reconciler with atomic patch validation;
- semantic AppSpec v1 plus calories, calculator, workout, and voice fixtures;
- deterministic 240×240 RGB565 catalog goldens;
- a visually verified CoreS3 40MHz synchronous strip-display path.

The [20-app and OS conformance suite](docs/20-app-conformance-suite.md) is the
permanent UI/runtime executable specification, and
[Project Parallax](docs/project-parallax.md) tracks dual-renderer fidelity.
Phase 5 of the
[live foreground agent and durable jobs plan](docs/live-agent-vertical-slice.md)
routes production builds through a real Codex worker and an independent
verifier. The personal-app Phase 6 path packages successful output outside the
Codex workspace, announces it to the watch, installs it, and offers live
launch. The active qualification gate and next product slice are described in
the roadmap.
The suite now includes 20 separate interactive Wasm packages, deterministic
scenario and cross-surface contracts, a trusted Home/Voice/App Manager shell,
CoreS3 system-shortcut wiring, dual 3 MiB firmware slots, and 9.94 MiB of
onboard package storage. Run the complete package lane with:

```bash
./scripts/test-conformance-suite.sh
```

Run or inspect one deterministic lifecycle scenario with:

```bash
./doodad conformance fixtures/scenarios/timer-reboot.scenario.json --trace
```

The pinned Wear Compose implementation is executable as a dual-renderer oracle
under [`reference/android-wear`](reference/android-wear/README.md). It consumes
ten renderer-neutral scenarios, records 30 small-round, large-round, and
240-square host goldens, asserts live Compose semantics, and includes API 37
emulator capture plus Compose-to-LVGL overlay tooling. Run its independent lane
with:

```bash
./scripts/test-reference-oracle.sh
```

See
[`docs/dual-renderer-conformance.md`](docs/dual-renderer-conformance.md)
for the evidence and geometry policy. The execute-once, render-twice roadmap
and tracked implementation checklist live in
[`Project Parallax`](docs/project-parallax.md).

Generate the aligned initial-state comparison for all twenty applications
with:

```bash
./doodad perfect-render \
  --suite all-20 \
  --profile watch_square_240 \
  --output target/parallax/perfect-render-20
```

This executes the real package corpus once, replays accepted scenes through
the production LVGL renderer, renders the same `SceneSnapshot` through Wear
Compose Material 3, and writes raw captures, normalized evidence, overlays,
metrics, a contact sheet, and static HTML. The first measured result and
remediation order are in the
[Project Parallax comparison report](docs/project-parallax-comparison-report.md).

All 20 decisive flows are executable and emit checked-in semantic/resource
evidence. Cross-app replacement stress, display-sleep service behavior,
surface-revision consistency, selected physical CoreS3 traces, and the duplex
Opus live-agent path and bounded Codex rest-timer generation are covered.
The deterministic personal-app packaging/install path is implemented, but its
physical CoreS3 gate is not yet recorded. General backing data,
published-app/store trust, remaining production providers, long-running
audio/sensor services, power budgets, and physical T-Watch qualification
remain.

The guest exports `app_start` and imports one capability:
`doodad.ui_mount(i32 pointer, i32 length) -> i32`. Its payload is canonical
CBOR AppSpec v1—not text or an LVGL tree. The host bounds-checks guest memory,
decodes into fixed-capacity native storage, validates canonical encoding,
UTF-8, IDs, semantics, tree shape, and resource quotas, and only then queues
the document to the UI task.

Interactive guests also export
`handle_event(i32 pointer, i32 length) -> i64`. The result is a borrowed,
packed pointer/length for a canonical CommandBatch. The host copies at most
4096 bytes, rejects mixed UI/state batches, validates the entire batch before
commit, and patches mounted LVGL components in place.

## Pinned versions

- ESP-IDF 5.5.5
- M5Unified 0.2.19
- M5GFX 0.2.26 (resolved by M5Unified)
- LVGL 9.5.0
- Espressif wasm-micro-runtime 2.4.0 revision 1
- Rust 1.95.0
- Rust target `wasm32-unknown-unknown`

The guest linker is constrained to a 16 KiB stack, one initial 64 KiB linear
memory page, and a two-page maximum. The build inspector enforces those limits.

ESP-IDF managed-component checksums are recorded in
`firmware/dependencies.lock` after the first successful configure.

## Develop an app

From `doodad-runtime`, check the local toolchain and start the simulator:

```bash
./doodad doctor
./doodad dev apps/hello
```

`doodad dev` opens a browser preview, watches the app, SDK, and contracts, and
rebuilds on change. Each successful reload creates a fresh resident WAMR
instance and renders the package with LVGL. A failed reload reports the
contract, build, or runtime error while preserving the last good frame.

The simulated app surface is always exactly 240×240. The CoreS3's physical
320×240 screen displays that same square surface centered between two
host-owned 40-pixel background gutters. Apps never receive a widescreen layout.

Other package commands are:

```bash
./doodad build apps/hello
./doodad check apps/hello
./doodad test apps/hello
./doodad inspect target/doodad/dev.doodad.hello/app.wasm
```

Material catalog and semantic AppSpec commands are:

```bash
./doodad catalog --story components --output target/catalog/components.bmp
./doodad catalog --story calories --output target/catalog/calories.bmp
./doodad catalog --story calculator --output target/catalog/calculator.bmp
./doodad catalog --story workout --output target/catalog/workout.bmp
./doodad catalog --story navigation --output target/catalog/navigation.bmp
./doodad catalog --story system --output target/catalog/system.bmp
./doodad catalog --story transforming-list --output target/catalog/transforming-list.bmp
./doodad catalog --story expressive-depth --output target/catalog/expressive-depth.bmp
./doodad catalog --story mockup-hydration --output target/mockups/hydration.bmp
./doodad catalog --story mockup-focus --output target/mockups/focus.bmp
./doodad catalog --story mockup-travel --output target/mockups/travel.bmp
./doodad catalog --story mockup-music --output target/mockups/music.bmp
./doodad appspec apps/calories/appspec.json --validate-only
./doodad appspec apps/calories/appspec.json --output target/appspec/calories.bmp
python3 tools/token_sync/sync.py --check
```

AppSpec intentionally exposes semantic components, tones, sizes, spacing, and
events. It has no raw colors, radii, coordinates, LVGL names, animation
curves, or generic style object. See [docs/appspec-v1.md](docs/appspec-v1.md).
Privileged host-service events and cross-surface publication are specified in
[docs/provider-contracts.md](docs/provider-contracts.md).

The staged package is written to `target/doodad/<app-id>/`. `check` executes
`app_start` in the native WAMR host; `test` additionally verifies that LVGL
produced a non-empty 240×240 frame. See
[docs/simulator.md](docs/simulator.md) for the package and UI contracts.

## Install a generated personal app

After the independent Codex verifier succeeds, the live-agent's outer
packager creates an owner-bound DDB1 bundle with canonical metadata, raw
`app.wasm`, and an HMAC-SHA256 tag. The same live-agent port sends an
`app.ready` control message over WebSocket and serves the immutable bundle over
HTTP. The watch downloads to temporary storage, verifies the announced bundle
hash, owner, signer key ID, host ABI, HMAC, and payload hash, and only then
advances the installed-app registry.

Installation and launch are separate: the trusted shell shows **APP READY**
with **Launch now** and **Later** actions. Home's launcher lists installed
apps, while the native shell keeps exactly one WAMR guest resident and can
replace that guest without rebooting. Each app retains `current` and
`previous` generations; a detectable startup or guest-handler failure in the
new generation reloads the prior generation when one exists and records the
exact failed `(app_id, semantic_version, payload_sha256)` tuple in a persisted,
non-evicting quarantine set of up to eight entries per app. Launch and reinstall
reject every recorded tuple. A ninth distinct failure persists a terminal block
for that whole app, removes it from the launcher, and requires a destructive
profile reset rather than forgetting an older failure. An
`app.ready` completion never displaces the trusted Voice
overlay; **APP READY** is deferred until Voice closes normally. Host-owned
timers are keyed by app identity so switching guests cannot deliver one app's
timer event to another.

This v0 profile intentionally trusts the local user who holds the shared key.
Published apps, store publisher identity, revocation, and on-device capability
approval are later policy. See
[Personal app installation](docs/personal-app-installation.md) for the exact
contract, configuration, limitations, and manual CoreS3 validation flow.

## Apple Silicon setup

The host needs Git, CMake, Ninja, Python 3, Rustup, and Rust 1.95.0. Homebrew
itself is not installed or modified by this project. From this directory:

```bash
./scripts/bootstrap-macos.sh
```

That script clones the official ESP-IDF 5.5.5 tag to
`~/.espressif/frameworks/esp-idf-v5.5.5`, installs only the ESP32-S3 tools in
Espressif's normal `~/.espressif` location, and installs the freestanding Rust
target. It does not alter shell startup files.

If ESP-IDF already lives elsewhere, skip its installation and export
`IDF_PATH` before running the other scripts.

No WASI SDK, JavaScript runtime, `wasm-opt`, or Docker installation is needed.
The dependency-free `scripts/inspect-wasm.py` verifies the guest's import and
exports during every build.

## Build

Build and inspect the guest:

```bash
./scripts/build-guest.sh
```

This creates the release Wasm module, verifies that its only import is
`doodad.ui_mount`, verifies the `app_start() -> ()` and
`handle_event(i32, i32) -> i64` signatures plus the bounded `memory` export,
and copies the resulting bytes to `firmware/main/embedded/hello.wasm`.

Build the complete ESP32-S3 firmware:

```bash
./scripts/build-firmware.sh
```

The firmware build always rebuilds the guest first, so the embedded module
cannot silently drift from the Rust source.

Run the complete local verification lane:

```bash
./scripts/test-all.sh
```

It checks deterministic token generation, native C++ tests, semantic contract
tests, all RGB565 catalog goldens, reference AppSpecs, WAMR execution, and the
ESP-IDF firmware build.

## Flash and monitor

With the CoreS3 SE connected over its main USB-C port:

```bash
./scripts/flash.sh
```

The script selects the first `/dev/cu.usbmodem*`, flashes, and starts the serial
monitor. Select an exact port when more than one device is attached:

```bash
./scripts/flash.sh --port /dev/cu.usbmodem21101
```

Exit the ESP-IDF monitor with `Control-]`. To flash without opening it:

```bash
./scripts/flash.sh --port /dev/cu.usbmodem21101 --no-monitor
```

To reopen logs later:

```bash
./scripts/monitor.sh /dev/cu.usbmodem21101
```

The normal successful lifecycle includes:

```text
[host] boot
[host] display ready
[package-service] package storage mounted: ... KiB free / ... KiB apps=...
[host] WAMR ready (interpreter, stack=16384, heap=16384)
[host] no legacy onboard package at /packages/active.wasm
[host] using embedded recovery app
[host] EMBEDDED app size: ... bytes
[host] module loaded
[host] module instantiated
[host] invoking app_start
[guest] ui_mount: 151 bytes, 3 nodes
[host] app started; instance remains resident
[host] delivered action=say_hello node=hello.action commands=2
[host] steady state; free heap: ... bytes
```

The connected CoreS3 SE has also been physically traced through this lifecycle:
16MB flash, both 3MiB OTA slots, the 9.94MiB package partition, 8MB Quad PSRAM
with a passing memory test, no-SD recovery fallback, WAMR instantiation, and
AppSpec mount all completed successfully.

With no prepared card, the embedded semantic app should show:

```text
       40px gutter | 240×240 app surface | 40px gutter
                            Hello world
```

## Run the microSD milestone

An SD card is not required for current development. Personal installs use the
onboard registry described above. A factory-erased package partition is
initialized once only after an all-`0xFF` check; a non-erased mount failure is
never reformatted and its bytes remain available for diagnosis/recovery. The
older `/packages/active.wasm` bare-module path is retained only as a legacy boot
fallback; it is not the installed-app registry, does not name the currently
running guest, and is never updated by the DDB1 installer. An unavailable
legacy module falls through to optional microSD and then the embedded recovery
guest.

First build the guest. Put the CoreS3 SE's card in a Mac card reader, identify
its mounted volume, and copy the exact built module:

```bash
./scripts/build-guest.sh
./scripts/install-sd-app.sh /Volumes/YOUR_SD_CARD
```

The installer uses `cmp` and prints both SHA-256 hashes. Eject the card cleanly,
insert it into the powered-off CoreS3 SE, and boot. The loader looks for:

```text
/doodad/hello.wasm
```

When it succeeds, the screen source label is `MICROSD`. If the card, filesystem,
file, or Wasm module is unavailable or invalid, the shell logs the failure and
runs the embedded recovery image instead.

The card must use a FAT filesystem supported by ESP-IDF FatFs. This milestone
does not format cards automatically.

## Change the app

Edit the string in `apps/hello/src/lib.rs`, then rebuild and flash:

```bash
./scripts/build-firmware.sh
./scripts/flash.sh
```

To stage another package in the embedded recovery slot and keep it visible for
real-hardware conformance capture:

```bash
./scripts/build-firmware.sh --app timer --show-app
./scripts/flash.sh --port /dev/cu.usbmodem21101 --no-monitor
```

With the fixed camera fixture in place, capture the full 20-app hardware lane:

```bash
./scripts/capture-hardware-suite.sh --port /dev/cu.usbmodem21101
```

The command saves desktop references, raw camera frames, normalized 240×240
hardware crops, color-corrected hardware images, and side-by-side comparisons
under `target/hardware-gallery/apps`. Use `--app timer` for one package or
`--start-at calendar` to resume an interrupted sweep. Comparisons always use
the corrected output. The standard path uses exposure 16, gain 58, fixed
white balance and focus, and no blur.

To flash the label-free RGB565 calibration target, capture it with the approved
camera settings, and fit a capture-correction matrix:

```bash
./scripts/capture-color-bars.sh --port /dev/cu.usbmodem21101
```

The 240×240 target has a white viewport-registration frame around standard
full-range bars followed by grayscale, red, green, and blue ramps. Results are
written under
`target/hardware-gallery/color-bars`: the deterministic reference, raw camera
frame, normalized crop, corrected preview, three-way comparison, per-patch CSV,
and a JSON affine sRGB correction matrix with clipping/crushing diagnostics.
Once the bars are already on screen, use `--capture-only` with different
`--exposure` and `--gain` values to iterate without reflashing.

The color target and app gallery use the same checked-in sharp profile:
exposure 16, gain 58, white balance 4000 K, and an explicit autofocus settle.
This is necessary because a correction fitted at different acquisition
settings is not transferable. The attached-hardware result is recorded in
[`evidence/hardware/cores3-color-calibration.md`](evidence/hardware/cores3-color-calibration.md).

Both capture commands default to `--moire-sigma 0`: no Gaussian blur, denoise,
or unsharp pass is applied. Raw and uncorrected normalized captures are always
retained. See
[`docs/hardware/capture-standard.md`](docs/hardware/capture-standard.md) for
the mandatory sharp-then-color-correct evidence contract and profile
regeneration rules.

For microSD, rerun `install-sd-app.sh` after rebuilding.

## Download mode and recovery

The ESP32-S3 USB Serial/JTAG interface normally enters the bootloader
automatically. If flashing cannot connect:

1. Keep USB-C connected.
2. Hold the CoreS3 SE reset button until download mode begins, then release it.
3. Retry `./scripts/flash.sh --port /dev/cu.usbmodem...`.

If the serial port is missing:

1. Use a known data-capable USB-C cable and connect directly rather than
   through an unpowered hub.
2. Check `ls /dev/cu.usbmodem*`.
3. Disconnect and reconnect USB, then press reset once.
4. Close any serial monitor already holding the port.
5. Hold reset to enter download mode and check again.

A broken application cannot prevent reflashing through the ROM bootloader.
Enter download mode as above and rerun `scripts/flash.sh`. The embedded guest is
the final runtime recovery path when neither an installed/legacy generation nor
a microSD application can run. Personal-app switching does not reboot or replace
the native shell, and the registry preserves the prior generation for bounded
automatic rollback.

## Layout

```text
doodad-runtime/
├── apps/                        Wasm hello app and semantic reference specs
├── components/m3e_lvgl/        Material tokens, runtime, and LVGL components
├── contracts/                   Versioned package, UI, and ABI contracts
├── sdk/rust/doodad-sdk/         Guest-side ABI wrapper
├── services/live-agent/         Foreground conversation and durable jobs
├── firmware/                    ESP-IDF shell
│   └── main/
│       ├── embedded/hello.wasm  Generated embedded guest
│       ├── include/
│       └── src/
├── tools/native-host/           Desktop WAMR + headless LVGL host
├── tools/doodad_cli/            Package and simulator CLI
├── tools/token_sync/            Pinned upstream token extraction/generation
├── ui/                          LVGL shell shared by desktop and firmware
├── docs/roadmap.md              Current overall project sequence
├── docs/personal-app-installation.md
│                                Personal bundle/install/launch contract
├── doodad                       Development command
├── scripts/                     Build, flash, inspect, and SD install tools
└── docs/architecture.md
```

See [docs/architecture.md](docs/architecture.md) for the trust boundary and
runtime design.
