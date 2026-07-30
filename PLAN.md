# Doodad Runtime — First Vertical Slice Plan

Build the first vertical slice of an experimental voice-first, dynamically extensible watch platform on an M5Stack CoreS3 SE connected to my Apple Silicon Mac.

Work directly in a new local project named `doodad-runtime`. Implement the project, build it, and help me flash it onto the attached device. Do not merely describe how I could implement it.

## Product vision

I am exploring a watch-like personal computer whose operating interface begins with voice.

The eventual device will connect to a server running the Codex app-server JSON streaming protocol. The user should be able to speak commands such as:

* “Create a calorie tracker.”
* “Add the lunch I just described.”
* “Change this app so the weekly goal is shown first.”
* “Make me a simple scorekeeper.”
* “Add a server integration for my fantasy football league.”

The server can generate, compile, test and publish new applications. The device can then download, install, update and execute those applications without replacing the trusted base firmware every time.

The device should have two distinct software layers:

1. A relatively stable, trusted native firmware shell.
2. Small, dynamically installed application packages containing declarative UI, optional WebAssembly logic and assets.

The native firmware owns:

* Boot and recovery
* Display and touchscreen
* Voice capture and audio playback
* Wi-Fi, Bluetooth and server connectivity
* Authentication
* Application installation and verification
* WebAssembly execution
* Declarative UI rendering
* Local shared data
* Power management
* OTA updates to the base firmware
* Resource limits and application lifecycle

Downloaded applications do not receive raw hardware access. They use a versioned, capability-based host API exposed by the firmware.

Applications should eventually be able to share data through a device-owned data service. They must not directly open or modify a common database file. Instead, they call typed host capabilities such as:

* `data_get`
* `data_put`
* `data_query`
* `data_subscribe`

This lets the native shell enforce schemas, namespaces, migrations, permissions and quotas while still allowing useful cross-application data. For example, voice can add a meal to shared data and both a calorie tracker and a weekly health summary can use it.

## Voice-driven base UI

The native shell will eventually provide a small system UI with states such as:

* Idle
* Listening
* Transcribing
* Thinking
* Streaming a response
* Running an action
* Installing or updating an app
* Error and recovery

Voice is the primary operating interface, while touch is a compact complementary interface.

The server interprets voice requests and may:

* Answer conversationally
* Navigate to an existing app
* Read or update shared data
* Invoke an application action
* Generate a new app
* Generate a new version of an existing app
* Add or invoke a server-side integration

The native shell should always retain control over navigation, permissions, application lifecycle and system status.

Do not implement voice in this milestone. This context is here to ensure the first implementation establishes the correct architectural seam.

## Long-term application model

An installed app package will eventually resemble:

```text
app-package/
├── manifest.json
├── app.wasm
├── ui.json
├── assets/
└── signature
```

The manifest will eventually identify:

* Application ID and version
* Minimum host ABI version
* Requested capabilities
* Data schemas or namespaces
* Entry points
* Resource limits
* Package checksum and signature

Most UI should be declarative and rendered natively. WebAssembly is for local state machines, event handling, transformations and business logic—not direct display drawing, audio processing, TLS or unrestricted filesystem access.

The future lifecycle is:

1. The server generates an app.
2. Rust application logic is compiled to WebAssembly.
3. The server validates and signs the package.
4. The device downloads it.
5. The device checks its signature, ABI version and capabilities.
6. The device installs it atomically.
7. The native shell instantiates the Wasm module with strict limits.
8. Events are delivered to the app.
9. The app requests UI and data operations through host capabilities.
10. A failed app is stopped without taking down the base firmware.

For this first milestone, do not build this packaging system. Establish the foundation it will use.

## First milestone: “Hello from Wasm”

Create a small ESP-IDF firmware with these properties:

* Target: M5Stack CoreS3 SE / ESP32-S3.
* Development host: Apple Silicon Mac.
* Framework: ESP-IDF.
* Device integration: M5Unified/M5GFX using their supported ESP-IDF integration.
* Wasm runtime: the official Espressif `wasm-micro-runtime` component, version 2.4.x or the latest compatible stable 2.x version.
* Wasm execution mode: interpreter.
* Guest language: Rust.
* Guest target: a freestanding WebAssembly target without browser or JavaScript assumptions.
* Guest environment: no WASI unless it is genuinely necessary. Prefer a minimal custom ABI.
* Guest module: embedded into the firmware image at build time.
* Host API: exactly one meaningful guest capability, `display_text`.
* App entry point: one explicit exported function such as `app_start`.
* The host invokes `app_start`.
* The Rust guest calls `display_text("Hello from Wasm")`.
* The native host safely validates and copies the guest string before rendering it.

Use official documentation and current source code to confirm all integration details before implementing them. WAMR is changing GitHub organizations around July 2026, so follow the official Espressif component registry rather than relying on an assumed repository URL.

Pin compatible versions so this project remains reproducible.

## Required final behavior

On boot, the native firmware should:

1. Initialize serial logging.
2. Initialize the CoreS3 SE using M5Unified/M5GFX.
3. Draw a simple native system shell.
4. Initialize WAMR.
5. Locate the Wasm bytes embedded in flash.
6. Validate and load the module.
7. Instantiate it with explicit stack and heap limits.
8. Find the exported `app_start` function.
9. Invoke `app_start`.
10. Receive the guest’s `display_text` call.
11. Safely copy the string out of guest linear memory.
12. Render it within the native shell.
13. Continue running without rebooting.

The final display should contain native chrome and guest content:

* Native title: `DOODAD`
* Native status indicator: `WASM RUNNING`
* Guest-provided central content: `Hello from Wasm`
* Native footer: `HOST ABI v1`
* Native source label: `EMBEDDED`

Use a restrained dark interface with a clearly legible central message. Do not spend significant time polishing graphics.

The native shell must draw the title, status and footer itself. Only the central `Hello from Wasm` string comes from the guest. This distinction is important because it models the future relationship between the operating shell and downloaded applications.

## Error behavior

Do not leave a black screen or silently reboot on failure.

If any stage fails, log a precise serial error and display a concise native error screen identifying the failed stage, such as:

* `DISPLAY INIT FAILED`
* `WAMR INIT FAILED`
* `MODULE LOAD FAILED`
* `MODULE INSTANTIATE FAILED`
* `APP_START NOT FOUND`
* `GUEST TRAP`
* `INVALID GUEST STRING`

Include WAMR exception text in serial output when available.

Set sensible bounds on the imported string. For example, reject zero-length strings if inappropriate and reject strings larger than a small fixed maximum. Do not trust a guest pointer or length without validating it against guest linear memory.

## Project structure

Use a structure approximately like:

```text
doodad-runtime/
├── README.md
├── scripts/
│   ├── bootstrap-macos.sh
│   ├── build-guest.sh
│   ├── build-firmware.sh
│   └── flash.sh
├── sdk/
│   └── rust/
│       └── doodad-sdk/
├── apps/
│   └── hello/
│       ├── Cargo.toml
│       └── src/
├── firmware/
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── main/
│       ├── CMakeLists.txt
│       ├── idf_component.yml
│       ├── embedded/
│       │   └── hello.wasm
│       └── src/
└── docs/
    └── architecture.md
```

Adjust this if ESP-IDF conventions require a slightly different layout, but preserve the separation among:

* Native firmware
* Rust guest applications
* Shared guest SDK
* Build tooling

The firmware must not know how the module was produced. It should receive a byte buffer through a small loader abstraction so that later milestones can supply the same bytes from embedded flash, microSD or an HTTP download.

A conceptual interface such as this is enough:

```cpp
struct AppImage {
    const uint8_t* data;
    size_t size;
    const char* source;
};

bool run_app(const AppImage& image);
```

Do not overengineer the loader or invent a complete application manager yet.

## Rust guest SDK

Create a very small `doodad-sdk` Rust crate that owns the guest side of the ABI.

The hello application should depend on this SDK rather than declaring raw imports throughout the application.

The application-facing API should be approximately:

```rust
use doodad_sdk::display_text;

#[no_mangle]
pub extern "C" fn app_start() {
    display_text("Hello from Wasm");
}
```

Use whatever modern Rust attributes and unsafe declarations are required by the selected Rust edition.

The underlying ABI should pass an explicit pointer and byte length. Do not depend on a null-terminated string crossing the boundary.

Document:

* Imported module name
* Imported function name
* Parameter types
* Guest memory ownership
* String encoding
* Maximum accepted length
* Host ABI version

Use UTF-8 for text.

## Build workflow

I want a credible development loop, not a pile of manual copy steps.

Provide scripts or similarly simple commands that:

1. Build the Rust guest in release mode.
2. Copy the resulting `.wasm` to the firmware’s embedded asset location.
3. Build the ESP-IDF firmware.
4. Detect or accept the CoreS3 serial port.
5. Flash and open the serial monitor.

The first build may require explicit setup, but subsequent iteration should be straightforward.

Aim for a workflow resembling:

```bash
./scripts/build-guest.sh
./scripts/build-firmware.sh
./scripts/flash.sh
```

or one top-level command that composes them.

Do not require Docker for this milestone.

Do not install Homebrew itself or alter shell startup files without asking. If required tools are missing, inspect the system first and give me the smallest explicit installation step. Prefer officially supported Apple Silicon toolchains.

The README must include exact setup instructions for:

* ESP-IDF
* Rust
* The selected Rust Wasm target
* Any required Wasm inspection or optimization tools
* Building
* Flashing
* Monitoring logs
* Entering CoreS3 SE download mode
* Troubleshooting a missing serial port
* Restoring or reflashing after a failed build

Do not introduce optional tooling such as `wasm-opt` unless it materially benefits this milestone. If included, make it optional or install it deliberately and document why.

## Serial logging

Emit milestone-oriented logs such as:

```text
[host] boot
[host] display ready
[host] WAMR ready
[host] embedded app size: 1234 bytes
[host] module loaded
[host] module instantiated
[host] invoking app_start
[guest] display_text: 15 bytes
[host] app completed successfully
```

Include actual stack, heap and module sizes where useful.

## Architecture document

Write `docs/architecture.md` explaining:

* Why the native shell is trusted and changes relatively slowly
* Why apps are WebAssembly modules
* Why device hardware is not directly exposed to apps
* Why UI remains natively rendered and declarative
* Why shared data is accessed through host capabilities
* How the loader will later support embedded, microSD and HTTP sources
* How app signatures, capability permissions and resource limits will eventually work
* What this first milestone validates
* What it explicitly does not validate

Keep it concrete and tied to this project.

## Acceptance criteria

This milestone is complete only when:

* The native firmware builds successfully for ESP32-S3.
* The Rust guest builds successfully to a `.wasm` module.
* The firmware embeds the produced module rather than a hand-authored byte array.
* WAMR is obtained through the official Espressif component mechanism.
* The firmware initializes the CoreS3 SE display.
* The firmware loads and instantiates the embedded module.
* The firmware calls the guest’s exported `app_start`.
* The guest invokes the host’s `display_text` import.
* The host safely validates the guest pointer and length.
* The physical device displays `Hello from Wasm`.
* The physical device remains stable for at least several minutes.
* Serial output shows the complete successful lifecycle.
* Changing the string in the Rust guest, rebuilding and reflashing changes the displayed guest content.
* The README makes the complete process repeatable.

Do not claim the physical-device acceptance criteria have passed unless we actually flash and observe the device. Stop and ask me to perform a physical action when necessary.

## Explicit non-goals

Do not implement these yet:

* Voice capture
* Speech recognition
* Codex connectivity
* Wi-Fi provisioning
* HTTP application downloads
* microSD application loading
* Application signatures
* Multiple simultaneous apps
* A shared database
* Declarative JSON UI
* Touch event delivery
* OTA firmware updates
* WASI filesystem access
* AOT compilation
* ESP-Brookesia adoption
* A full application manifest
* A general-purpose operating system

Do not quietly expand the scope into any of these.

## Planned next milestones

Preserve an obvious path to these later steps, but do not implement them now:

### Milestone 2: microSD source

Load the exact same `hello.wasm` through the same `AppImage`/loader interface from microSD. The embedded app remains the recovery fallback.

### Milestone 3: Wi-Fi update

Download a versioned module over HTTPS, validate its checksum, store it atomically and run it after reboot. Retain the last-known-good app.

### Milestone 4: events

Add a small event ABI and deliver native touch events to the app. The app exports something like:

```text
app_event(pointer, length)
```

### Milestone 5: declarative UI

Replace `display_text` with a host capability that accepts a bounded declarative UI document. The native shell validates and renders it.

### Milestone 6: shared data

Add a device-owned data service exposed through versioned typed capabilities.

### Milestone 7: voice shell

Add native voice state management and stream audio and responses through the server.

## Implementation approach

Proceed incrementally:

1. Inspect the current directory and available tools.
2. Verify the connected USB device and likely serial port.
3. Confirm current official integration methods and compatible versions.
4. Create the repository structure.
5. Build the minimal Rust guest.
6. Inspect its imports and exports.
7. Build a minimal WAMR host without display dependencies if that helps isolate integration.
8. Integrate M5Unified/M5GFX.
9. Build the complete firmware.
10. Flash the device with my participation when physical button interaction is required.
11. Inspect serial logs.
12. Fix failures rather than papering over them.
13. Verify the physical display.
14. Finish the documentation.

Favor the smallest working implementation that cleanly establishes the host/guest boundary. Do not invent abstractions unrelated to the next two milestones.

At the end, report:

* What was built
* Exact versions selected
* The final project structure
* Commands to build and flash
* Observed serial output
* Whether each acceptance criterion passed
* Known limitations
* The most sensible next milestone
