# Doodad runtime architecture

## Trust boundary

The native ESP-IDF firmware is the trusted shell. It boots the device,
initializes M5Unified, owns the screen and microSD hardware, selects application
bytes, configures WAMR, enforces ABI bounds, and retains the recovery path. It
changes relatively slowly because a defect here can affect the whole device.

Applications are smaller WebAssembly modules. Wasm gives the shell a
well-defined linear-memory boundary and explicit imports instead of linking
untrusted app logic directly into the firmware. A failed module can be
deinstantiated while the native shell remains in control.

Apps never receive display, SPI, microSD, network, audio, or filesystem handles.
They can only call registered, versioned capabilities. The first semantic
capability is `ui.mount`.

## Host ABI v1

The imported module name is `doodad`, the imported function name is
`ui_mount`, and its WebAssembly signature is `(i32, i32) -> i32`.

The first parameter is a byte offset in the calling guest's linear memory. The
second is an explicit byte length. The guest retains ownership for the duration
of the synchronous call. The host accepts 1 through 4096 bytes, validates the
complete range with WAMR, and decodes canonical CBOR into host-owned bounded
AppSpec storage. Numeric key ordering, minimal integer encoding, definite
lengths, UTF-8, identifiers, component properties, semantics, parents, depth,
children, events, and quotas are checked before anything reaches LVGL.

The guest exports `app_start() -> ()`. It has no WASI imports. Native title,
status, footer, source label, and error screens remain outside guest control.
Interactive guests also export
`handle_event(i32 pointer, i32 length) -> i64`. LVGL callbacks copy semantic
events into a 16-entry UI-to-runtime queue. The runtime actor serializes each
event as canonical CBOR, allocates a bounded guest range, invokes the export
off the UI task, and frees the event range immediately. The 64-bit result
packs a borrowed guest pointer in its high 32 bits and a byte length in its
low 32 bits. The host validates the range, copies and decodes at most 4096
bytes as a canonical `CommandBatch`, and never retains guest memory.

A batch is UI-only or state-only; mixed batches are rejected. Every target,
property, value, quota, and required string capacity is checked before the
first LVGL mutation. State batches use the Store's staged transaction path.
UI batches mutate existing native components in place, preserving object
identity and event bindings. Guest code is never called reentrantly from an
LVGL callback.

## Native UI

The native shell renders stable chrome and a central guest-content region with
LVGL 9.5.0. The portable display contract is square and fixed at 240×240. On
the CoreS3's 320×240 panel the host centers the app surface at x=40; the two
40-pixel gutters are native background and never enter app layout.

The desktop simulator and firmware compile the same `ui/doodad_lvgl_ui.c`
shell. The simulator owns a headless RGB565 framebuffer; firmware flushes the
same logical coordinates through M5GFX at the physical x offset.

AppSpec v1 is the public contract for generated applications. It names semantic Material components,
stable node IDs, bindings, events, and semantics while prohibiting raw colors,
coordinates, radii, LVGL parts, callbacks, and arbitrary effects.

JSON is the authoring form. Both desktop preview and firmware consume the same
canonical CBOR compiler output, fixed-capacity decoder, native typed document,
Material component factory, and LVGL renderer. The `ui.json` v0 code remains
only for old package fixtures; it is not the generated-app or Wasm display
boundary. Apps receive no framebuffer or LVGL pointers.

## One Wasm engine

Both hosts compile the exact WAMR 2.4.0 revision 1 source resolved by the
ESP-IDF component manager. Desktop development does not use a browser Wasm
engine or a second compatibility implementation. It uses the fast interpreter,
the same disabled WASI/libc profile, and the same explicit native imports as
the device.

The app module, module instance, and execution environment remain resident
after `app_start` succeeds. On simulator reload, static build and contract
validation happen before the old instance is stopped. The host then starts one
fresh instance; if runtime startup fails, the browser retains the last good
frame with a stale-preview error. The resident lifetime supports serialized
semantic events and returned in-place command batches.

## Package contract v1

`manifest.json` names the app and version, pins host ABI v1, lists requested
capabilities, and points to `app.wasm` plus optional `ui.json`. `doodad build`
rejects unknown manifest fields, undeclared or unknown imports, missing exports,
incorrect memory bounds, oversized modules, and invalid declarative UI. It
stages only normalized filenames beneath `target/doodad/<app-id>`.

The current contract files are source-controlled in `contracts/`. JSON Schema
files make the document shapes portable; the CLI also performs semantic checks
such as matching Wasm imports to declared capabilities.

## Loader seam

The WAMR runner only accepts:

```cpp
struct AppImage {
    const uint8_t* data;
    size_t size;
    const char* source;
};
```

Milestone 1 creates an `AppImage` from linker symbols generated by ESP-IDF's
`EMBED_FILES`. Milestone 2 reads `/doodad/hello.wasm` into bounded native memory
and presents the same interface. The runner has no knowledge of how the bytes
were produced or stored. WAMR 2.4's interpreter loader mutates its input during
parsing, so the runner makes one bounded mutable RAM copy before loading either
source. Embedded flash is never cast into writable memory.

CoreS3 SE's LCD and microSD slot share SPI2 on SCLK GPIO 36, MOSI GPIO 37, and
MISO GPIO 35. LCD chip select is GPIO 3 and card chip select is GPIO 4. M5GFX
initializes the shared bus; the SD loader attaches its device to that existing
ESP-IDF bus, reads the module, removes the SD device, and then executes the
copied bytes.

The source priority is:

1. A readable microSD module that WAMR can load and execute.
2. The embedded module compiled into the firmware.

Therefore a missing card, missing file, read error, malformed module, missing
export, or guest trap does not remove the recovery app. A future HTTP loader can
produce another `AppImage` without changing the runner.

## Future package policy

A later application manager will verify package checksums and signatures before
making a new version active. Its manifest will declare a minimum host ABI,
requested capabilities, resource limits, and data namespaces. Installation will
be atomic and preserve a last-known-good package.

Capabilities will be granted per app rather than exposing devices. Shared data
will likewise stay native and be reached through typed calls such as
`data_get`, `data_put`, `data_query`, and `data_subscribe`. That lets the shell
enforce namespaces, schema migrations, quotas, and cross-app permission policy.

WAMR instances will continue to receive explicit stack and heap limits. These
milestones use 16 KiB for each plus an 8 KiB execution-environment stack and a
256 KiB maximum microSD module file.

## What milestones 1 through 3 validate

- A freestanding Rust guest can compile without WASI.
- ESP-IDF can embed the produced Wasm bytes reproducibly.
- WAMR interpreter mode can load, instantiate, and call the guest.
- A Rust guest can invoke one explicit host capability.
- The host can validate and copy guest linear memory before use.
- Native chrome and guest content remain visibly distinct.
- Embedded and microSD loaders can feed the same runner.
- The embedded image can recover from an unavailable or broken microSD app.
- The same pinned WAMR source and LVGL renderer build for macOS and ESP32-S3.
- App layout stays within a device-independent 240×240 square surface.
- A manifest, Wasm module, and declarative UI can be validated and staged as a
  package.
- A headless native host can execute and render a package for automated tests.
- The browser development loop can hot reload a fresh resident app instance.

## Remaining validation

These slices do not yet complete voice/audio transport, touch-to-guest event
delivery, Wi-Fi/HTTPS package delivery, signatures, atomic on-device package
installation, shared data, migrations, OTA base-firmware updates, or AOT.
The device still loads a bare Wasm module from microSD; package installation is
defined and exercised on desktop but is not yet the device loader.
