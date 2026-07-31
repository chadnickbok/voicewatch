# Doodad Runtime

This project implements the first three vertical slices of the Doodad watch
runtime on an M5Stack CoreS3 SE:

1. A trusted ESP-IDF shell embeds and runs a Rust WebAssembly guest.
2. Packages load from a 9.94 MiB onboard wear-levelled partition first, with
   optional microSD and the embedded recovery image as fallbacks.
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
- an instrumented CoreS3 asynchronous-DMA display path.

The active UI/runtime milestone is specified in the
[20-app and OS conformance-suite plan](docs/20-app-conformance-suite.md).
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
for the evidence and geometry policy.

All 20 decisive flows are executable and emit checked-in semantic/resource
evidence. Cross-app replacement stress, display-sleep service behavior,
surface-revision consistency, and selected physical CoreS3 traces are covered.
Production networking, flash-backed scheduler journals, long-running
audio/sensor services, and power budgets are subsequent milestones.

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
[host] WAMR ready (interpreter, stack=16384, heap=16384)
[host] onboard package storage: 10060 KiB free / 10060 KiB
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

An SD card is not required for current development. The firmware now formats
and mounts the onboard `packages` partition and checks
`/packages/active.wasm` first. Until the package activation command is added,
an empty onboard store simply falls through to optional microSD and then the
embedded recovery guest.

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
also the runtime recovery path when a microSD application fails.

## Layout

```text
doodad-runtime/
├── apps/                        Wasm hello app and semantic reference specs
├── components/m3e_lvgl/        Material tokens, runtime, and LVGL components
├── contracts/                   Versioned package, UI, and ABI contracts
├── sdk/rust/doodad-sdk/         Guest-side ABI wrapper
├── firmware/                    ESP-IDF shell
│   └── main/
│       ├── embedded/hello.wasm  Generated embedded guest
│       ├── include/
│       └── src/
├── tools/native-host/           Desktop WAMR + headless LVGL host
├── tools/doodad_cli/            Package and simulator CLI
├── tools/token_sync/            Pinned upstream token extraction/generation
├── ui/                          LVGL shell shared by desktop and firmware
├── doodad                       Development command
├── scripts/                     Build, flash, inspect, and SD install tools
└── docs/architecture.md
```

See [docs/architecture.md](docs/architecture.md) for the trust boundary and
milestone design.
