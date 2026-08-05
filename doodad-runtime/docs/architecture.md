# Doodad runtime architecture

## Trust boundary

The native ESP-IDF firmware is the trusted shell. It boots the device,
initializes M5Unified, owns the screen and microSD hardware, selects application
bytes, configures WAMR, enforces ABI bounds, verifies personal bundles, owns the
installed-app registry, and retains the recovery path. It changes relatively
slowly because a defect here can affect the whole device.

Applications are smaller WebAssembly modules. Wasm gives the shell a
well-defined linear-memory boundary and explicit imports instead of linking
untrusted app logic directly into the firmware. A failed module can be
deinstantiated while the native shell remains in control.

Apps never receive display, SPI, microSD, network, audio, or filesystem handles.
They can only call registered, versioned capabilities. The first semantic
capability is `ui.mount`.

Personal-app v0 adds a deliberately local trust profile, not an app-store trust
model. One explicit `owner_id`, signer-key label, and 32-byte HMAC key are
configured on both the Mac service and watch firmware. Codex never receives the
key: an outer host packager uses it only after the independent verifier has
accepted the generated artifact. Possession of that local key represents the
watch owner's trust. Published apps, publisher identity, revocation, and
per-install capability grants require a separate later profile.

This v0 profile has no in-place owner or key migration. Changing `owner_id`,
the signer-key label, or the HMAC key requires erasing the package partition,
rebuilding the firmware profile, and repackaging and reinstalling the desired
apps. Re-authenticating different envelope bytes under an existing
`(app_id, semantic_version, payload_sha256)` triple is an identity conflict,
not key rotation.

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

The guest exports `app_start() -> ()`. It has no WASI imports. Native loading
status and error screens remain outside guest control, but they are transient:
a successful AppSpec mount replaces the shell chrome and owns the complete
240×240 app surface. AppSpecs do not receive a synthesized title bar and
initial scenes do not repeat the launcher/app name. Interactive guests also export
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

The device follows the same one-engine rule. Many personal apps may be
installed, but only one WAMR guest is resident. **Launch now** or an installed
launcher selection asks the runtime actor to deinstantiate the current guest,
load the selected immutable generation, instantiate it, and mount its AppSpec;
the native ESP-IDF shell, display task, Voice service, package registry, and
other host services remain resident. App switching therefore does not reboot
the watch or replace its base firmware.

Runtime identity is copied into the guest instance as app ID, semantic version,
and payload generation hash. Host-owned exact-scheduler records are keyed by
app ID plus timer ID, and only the current app's due records are delivered to
its guest. A trap or failed UI/provider/timer handler is attributed to that
generation and latches a bounded failure for the runtime manager rather than
being delivered to whichever app runs next.

## Package contract v1

`manifest.json` names the app and version, pins host ABI v1, lists requested
capabilities, and points to `app.wasm` plus optional `ui.json`. `doodad build`
rejects unknown manifest fields, undeclared or unknown imports, missing exports,
incorrect memory bounds, oversized modules, and invalid declarative UI. It
stages only normalized filenames beneath `target/doodad/<app-id>`.

The current contract files are source-controlled in `contracts/`. JSON Schema
files make the document shapes portable; the CLI also performs semantic checks
such as matching Wasm imports to declared capabilities.

The source/staging package is not the device delivery envelope. After all
independent gates pass, the personal packager reads the verified `manifest.json`
and `app.wasm`, then produces DDB1:

```text
"DDB1" | metadata_length:u32be | payload_length:u32be
       | canonical UTF-8 JSON metadata | raw app.wasm
       | HMAC-SHA256("Doodad Personal Bundle v1\0" || all prior bytes)
```

Metadata binds the personal owner and signer-key label to app ID, display name,
semantic version, host ABI, payload size, and payload SHA-256. The stable
generation identity is `(app_id, semantic_version, payload_sha256)`; the whole
envelope has a separate `bundle_sha256` used as its immutable HTTP object key.
Firmware maps the exact triple to a lowercase storage key by hashing a
canonical sequence of three 32-bit-length-prefixed fields. Exact envelope
replays are idempotent, while different signed metadata or a different bundle
digest under an existing triple is rejected as an identity conflict.
The v0 bundle carries one Wasm payload. Capabilities remain verifier/build
metadata, not an on-device personal-app permission prompt.

## Loader seam

The WAMR runner accepts bytes plus copied generation identity:

```cpp
struct AppImage {
    const uint8_t* data;
    size_t size;
    const char* source;
    const char* app_id;
    const char* semantic_version;
    const char* generation;
};
```

The package service, legacy onboard loader, microSD loader, and linker-embedded
recovery image all present this interface. The runner has no knowledge of how
the bytes were transported or stored. WAMR 2.4's interpreter loader mutates its
input during parsing, so the runner makes one bounded mutable RAM copy before
loading any source. Embedded flash is never cast into writable memory.

CoreS3 SE's LCD and microSD slot share SPI2 on SCLK GPIO 36, MOSI GPIO 37, and
MISO GPIO 35. LCD chip select is GPIO 3 and card chip select is GPIO 4. M5GFX
initializes the shared bus; the SD loader attaches its device to that existing
ESP-IDF bus, reads the module, removes the SD device, and then executes the
copied bytes.

At ordinary boot the trusted Home surface remains in charge; installed personal
apps are entered through the launcher. `/packages/active.wasm` remains a bare
Wasm compatibility fallback for the older boot milestone. It is not a registry
pointer, is not written by personal installation, and does not mean that a
package is currently running. A legacy onboard or microSD load failure still
falls through to the embedded recovery guest.

## Personal delivery, installation, and rollback

The live-agent WebSocket and artifact HTTP endpoint share one port. Once a
verified Codex job has a durable DDB1 artifact, `app.ready` carries its bounded
identity, sizes, hashes, and `/apps/<bundle_sha256>` URL. Reconnect announces
the same digest again, and a 30-second same-session retry heals a dropped
offer without adding an acknowledgement RPC; both host and device treat the
immutable digest idempotently. HTTP is only the byte transport. DDB1 HMAC
verification and the
announced whole-bundle hash are the authority even when v0 uses plain HTTP on a
trusted local network.

The firmware network callback only validates and queues the offer. A separate
installer task streams it to `/packages/incoming/<bundle_sha256>.part`, checks
the announced byte length and whole-bundle SHA-256, then strictly parses and
authenticates DDB1. Owner ID, signer-key label, supported host ABI, canonical
metadata, HMAC, payload length, and payload SHA-256 must all agree before bytes
are promoted beneath `/packages/apps/<app-id>/...`.

The personal v0 runtime accepts app IDs through 64 ASCII bytes and Wasm through
1 MiB. The latter is a runnable limit, not just an envelope-parser limit, so a
bundle accepted by the packager is not knowingly too large for the device
loader. Package capacity remains byte-limited even though the registry has 32
identity slots.

The signed reverse-domain manifest `app_id` is the package, generation, timer,
and runtime-owner identity. AppSpec's shorter `app_id` remains a guest-local UI
event namespace (such as `timer`); it is not used as package authority. Stale
UI delivery is isolated with a generation-specific mounted-document token.

`/packages/registry.ddr` is a deterministic checksummed DDR3 registry tied to
one owner. It supports multiple app IDs and retains at most the current and
previous immutable generation for each. The resident generation is separate,
volatile state: installing advances current/previous but does not launch.

FatFs rename does not replace an existing destination, so registry commits do
not assume POSIX rename semantics. The host writes and syncs
`registry.ddr.part`, moves the valid current file to `registry.ddr.bak`, then
promotes the part file. At boot it selects a valid checksummed final or backup,
repairs an interrupted promotion, and removes stale part/backup files. Incoming
bundles and extracted Wasm likewise use `.part` or `.installing` paths until
verification completes. These content-addressed names require heap-backed
FatFs long filenames with `CONFIG_FATFS_MAX_LFN=255`; the firmware build
migrates existing ignored board sdkconfigs to those settings in place so local
Wi-Fi and personal-profile values survive the LFN change.

On first use, the host enables formatting only after reading the raw package
partition and confirming that it is entirely erased (`0xFF`). Any non-erased
mount failure is preserved rather than reformatted.

On **Launch now**, the runtime loads the selected generation and starts it in
the single WAMR slot. If load, instantiation, `app_start`, initial AppSpec mount,
or a later UI/provider/timer handler fails detectably, the manager attributes
that failure to the copied generation identity. When a previous generation
exists, it atomically restores that registry generation, quarantines the failed
generation, and reloads the previous guest. Each app has a persisted,
non-evicting quarantine set for up to eight exact
`(app_id, semantic_version, payload_sha256)` failure identities; launch
selection and reinstall reject every recorded tuple. If that bounded set is
full, a new failure cannot displace an older record. The registry instead
persists a terminal block for the whole app: current and previous become
unlaunchable, all reinstall/replay is rejected, and the app is omitted from the
launcher until the owner performs the documented destructive profile reset. If
a newer install became registry current without being launched, a failure in
the still-resident older guest instead persists quarantine for that exact tuple and loads the
distinct current generation without swapping the slots. Request, pre/post-load,
and running-state selection all reject quarantined tuples. This is bounded
process-level recovery; power loss, watchdog reset, and every possible infinite
loop are not yet a claimed crash detector.

## Future published-app policy

The personal profile intentionally has no app-store PKI or on-device capability
grant screen. Generated apps still pass the existing independent manifest,
import, permission, resource, semantic, simulator, and scenario gates, but a
matching owner HMAC means “this local user trusts this personal app.”

If published distribution becomes a product requirement, add a distinct
asymmetric publisher/store profile with identity lifecycle, review, revocation,
capability grants, migrations, and policy UI rather than stretching the shared
personal key into that role. Shared data will likewise stay native and be
reached through typed calls such as
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

The personal-app implementation adds deterministic coverage for:

- byte-compatible host/device DDB1 parsing, canonical metadata, hashes, owner
  matching, HMAC authentication, and tamper rejection;
- immutable artifact HTTP delivery and reconnect-safe `app.ready` publication;
- crash-safe multi-app current/previous registry transitions and idempotent
  repeated offers;
- live single-guest replacement, generation-attributed detectable failures,
  and prior-generation rollback; and
- `(app_id, timer_id)` scheduler isolation across guest switches.

Primary executable evidence is in the
[host bundle tests](../services/live-agent/tests/test_personal_bundle.py),
[delivery tests](../services/live-agent/tests/test_app_delivery.py),
[device bundle/registry tests](../tests/m3e/personal_packages_test.cpp), and
[scheduler ownership tests](../tests/m3e/exact_scheduler_test.cpp).

## Remaining validation

Voice/audio transport and touch-to-guest event delivery now have working
CoreS3 implementations and deterministic test lanes. They still require the
hardening and T-Watch qualification described in the
[roadmap](roadmap.md).

The personal-app loop still needs its documented physical CoreS3 run: real
`app.ready` signaling, HTTP download, install UI, **Launch now**, launcher
re-entry, Voice non-preemption, version update, exact-generation quarantine,
detectable-failure rollback, and timer isolation, with serial and screen
evidence. No such physical completion is claimed here.

Beyond that gate, remaining product boundaries include real shared data and
migrations, published-app/store trust if required, OTA base-firmware updates,
broader crash/hang detection, production recovery policy, and any justified AOT
path. See [Personal app installation](personal-app-installation.md) for the v0
contract and manual validation procedure.
