# Doodad Runtime

This project implements the first two vertical slices of the Doodad watch
runtime on an M5Stack CoreS3 SE:

1. A trusted ESP-IDF shell embeds and runs a Rust WebAssembly guest.
2. The same guest can be loaded from microSD, with the embedded image retained
   as the recovery fallback.

The guest exports `app_start` and imports one capability:
`doodad.display_text(i32 pointer, i32 length)`. The host bounds-checks the
length, validates the guest address and UTF-8, copies the bytes into native
memory, and only then renders them.

## Pinned versions

- ESP-IDF 5.5.5
- M5Unified 0.2.19
- M5GFX 0.2.26 (resolved by M5Unified)
- Espressif wasm-micro-runtime 2.4.0 revision 1
- Rust 1.95.0
- Rust target `wasm32-unknown-unknown`

The guest linker is constrained to a 16 KiB stack, one initial 64 KiB linear
memory page, and a two-page maximum. The build inspector enforces those limits.

ESP-IDF managed-component checksums are recorded in
`firmware/dependencies.lock` after the first successful configure.

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
`doodad.display_text`, verifies its `app_start` and `memory` exports, and copies
the resulting bytes to `firmware/main/embedded/hello.wasm`.

Build the complete ESP32-S3 firmware:

```bash
./scripts/build-firmware.sh
```

The firmware build always rebuilds the guest first, so the embedded module
cannot silently drift from the Rust source.

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
[host] embedded app size: ... bytes
[host] module loaded
[host] module instantiated
[host] invoking app_start
[guest] display_text: 15 bytes
[host] app completed successfully
[host] steady state; free heap: ... bytes
```

With no prepared card, the screen source label is `EMBEDDED`. It should show:

```text
DOODAD                         WASM RUNNING

              Hello from Wasm

HOST ABI v1                         EMBEDDED
```

## Run the microSD milestone

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
├── apps/hello/                  Rust guest
├── sdk/rust/doodad-sdk/         Guest-side ABI wrapper
├── firmware/                    ESP-IDF shell
│   └── main/
│       ├── embedded/hello.wasm  Generated embedded guest
│       ├── include/
│       └── src/
├── scripts/                     Build, flash, inspect, and SD install tools
└── docs/architecture.md
```

See [docs/architecture.md](docs/architecture.md) for the trust boundary and
milestone design.
