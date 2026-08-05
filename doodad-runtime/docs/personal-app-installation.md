# Personal app installation v0

| Field | Value |
|---|---|
| Status | Implemented in code; physical CoreS3 completion evidence pending |
| Last reconciled | 2026-08-04 |
| Trust profile | One local user and one shared 32-byte HMAC key |
| Transport | `app.ready` WebSocket announcement plus same-port HTTP download |
| Runtime | Multiple installed apps, one live WAMR guest |

This is the small personal-app loop, not an app-store design. Codex generates
source in its isolated workspace. A separate deterministic verifier decides
whether the result passes Doodad's schema, build, ABI, semantic, permission,
timer, and simulator gates. Only then does an outer packager with access to the
local user's key produce the device artifact. The packager signs bytes from one
snapshot only when its complete tree hash still matches the verifier's result;
mutable workspace changes require verification again.

## Contract at a glance

One DDB1 artifact contains a fixed header, canonical UTF-8 JSON metadata, raw
`app.wasm`, and an HMAC-SHA256 tag. The signed metadata binds:

- `owner_id` and `signer_key_id`;
- `app_id`, display name, semantic version, curated icon, and theme seed;
- supported host ABI; and
- payload byte length and SHA-256.

The generation identity is `(app_id, semantic_version, payload_sha256)`. The
whole envelope has its own `bundle_sha256`, which is the immutable artifact
store and HTTP URL key. The live-agent sends a bounded `app.ready` message over
the existing control WebSocket and serves the bytes at
`/apps/<bundle_sha256>` on the same host and port.

On the watch, a generation directory is named by the lowercase SHA-256 of a
canonical sequence containing those three exact identity fields, each prefixed
by its 32-bit big-endian byte length. Signed display metadata and the whole
bundle hash do not create a second generation. An exact envelope replay is a
no-op; a different signed envelope that reuses the same identity triple is a
conflict and fails closed.

The watch downloads to a `.part` file and verifies the announced length and
whole-bundle hash, exact DDB1 encoding, configured owner and key ID, host ABI,
HMAC, and payload hash before it changes the registry. A reconnect repeats the
announcement immediately, and the host retries it every 30 seconds on a live
session so a transient queue or fetch failure heals without an acknowledgement
protocol. An already known bundle digest is a no-op.

Installation makes the new generation `current` and retains the former current
as `previous`. It does not launch automatically. **Launch now** or a launcher
selection replaces the one resident WAMR guest while the native OS UI, Voice,
networking, package manager, and base firmware keep running.

Installation completion is notification-only while the trusted Voice overlay
is open. The watch preserves the listening/speaking interaction, emits the
completion haptic, and defers the **APP READY** surface until Voice is dismissed
normally; downloading or installing an app never preempts the active Voice UI.

Detectable load/start/mount or UI/provider/timer-handler failure is attributed
to the exact running `(app_id, semantic_version, payload_sha256)` identity. When
a previous generation exists, the registry restores it and the runtime reloads
it. If installation advanced `current` while the older guest was still
resident, failure instead quarantines that exact non-current generation and
launches the distinct safe current generation without swapping registry slots.
The bounded, non-evicting quarantine set is persisted before recovery; direct
or queued launch and reinstall reject every recorded failed tuple. It retains
up to eight identities per app. A ninth distinct failure persists a terminal
app-level block rather than forgetting an older failure: neither retained slot
can launch, all reinstall/replay is rejected, and the app is omitted from the
launcher until destructive profile reset. Timers are keyed by
`(owner_app_id, timer_id)`, so switching apps cannot route one app's timer event
into another guest.

## Configure one personal identity

Generate one 32-byte key and keep it out of source control:

```sh
openssl rand -hex 32
```

Choose a stable local owner ID and signer-key label. Export the same values for
the Mac service; the key must be exactly 64 hexadecimal characters:

```sh
export DOODAD_PERSONAL_OWNER_ID="local.nick"
export DOODAD_PERSONAL_SIGNER_KEY_ID="personal-v1"
export DOODAD_PERSONAL_HMAC_KEY_HEX="<64 hex characters>"
```

From `doodad-runtime`, open the CoreS3 firmware configuration:

```sh
source ./scripts/env.sh
DOODAD_RUNTIME_DIR="$(pwd)"
idf.py -C "${DOODAD_RUNTIME_DIR}/firmware" \
  -B "${DOODAD_RUNTIME_DIR}/firmware/build/cores3" \
  -D SDKCONFIG="${DOODAD_RUNTIME_DIR}/firmware/sdkconfig.cores3" \
  -D SDKCONFIG_DEFAULTS="${DOODAD_RUNTIME_DIR}/firmware/sdkconfig.defaults;${DOODAD_RUNTIME_DIR}/firmware/boards/cores3/sdkconfig.defaults" \
  menuconfig
```

Under **Doodad personal apps**, leave installation enabled and enter the same:

- Personal app owner ID;
- Personal signer key ID;
- Personal HMAC key; and
- host ABI `1`.

`firmware/sdkconfig.cores3` is ignored by Git, but it and the built firmware
contain the shared key. Treat build artifacts accordingly. Keep the owner ID
stable once the registry contains apps; an existing registry is deliberately
rejected for a different owner.

There is no in-place owner or key-rotation workflow in v0. If `owner_id`, the
signer-key label, or the HMAC key changes, treat it as a destructive profile
migration: choose a new signer label for new key material, erase the package
partition, rebuild the firmware, then repackage and reinstall every desired
personal app. Do not reuse one signer label for different key material. Old
retained generations fail launch-time revalidation, and repackaging different
envelope bytes under the same `(app_id, semantic_version, payload_sha256)` is an
identity conflict rather than an in-place re-sign operation.

Personal package paths exceed FAT 8.3 names. The tracked defaults require
heap-backed long-filename support and `CONFIG_FATFS_MAX_LFN=255` on both boards.
`build-firmware.sh` migrates those FatFs settings in an existing ignored board
sdkconfig in place, preserving its local Wi-Fi and personal-profile values.

The first boot after a full partition erase may log `initializing erased
package partition`. Formatting is enabled only after a raw scan confirms every
byte is `0xFF`. A non-erased partition that fails to mount is never reformatted;
the package service stays unavailable while the native shell and recovery path
remain usable.

The owner-bound registry is checksummed DDR3. DDR2 has no migration reader:
the signed-identity rollout is a clean break and requires erasing the package
partition once. Because FatFs rename cannot
overwrite a destination, an update syncs `registry.ddr.part`, preserves the
last valid registry as `registry.ddr.bak`, and then promotes the new final. Boot
repairs an interrupted promotion from the valid final or backup and removes
stale transaction files.

The build script can apply the exported profile directly to the ignored board
configuration without printing it. For the T-Watch clean break, erase only the
package partition, then build, flash, and monitor:

```sh
./scripts/erase-package-partition.sh /dev/cu.usbmodem21101
./scripts/build-firmware.sh --board t-watch-s3
./scripts/flash.sh --board t-watch-s3 --port /dev/cu.usbmodem21101 --no-monitor
./scripts/monitor.sh /dev/cu.usbmodem21101
```

In another terminal, preserve the exported personal values and start the
service:

```sh
./scripts/run-live-agent.sh check-config
./scripts/run-live-agent.sh serve
```

The artifact store defaults to
`~/Library/Application Support/Doodad/personal-apps`. Override it with
`DOODAD_PERSONAL_ARTIFACT_ROOT` if needed; it must remain outside mutable Codex
workspaces.

## Manual CoreS3 validation

No completed physical run is asserted by this document. Record the serial log,
the final watch screens, app IDs/versions, and the first 12 characters of the
payload and bundle hashes while performing this gate.

1. Boot with the configured firmware and start the service. The service should
   print `Personal app delivery enabled.` The watch should log a package-store
   mount with `apps=<count>` and must not log `personal installs disabled`.
   On a factory-erased first boot, one `initializing erased package partition`
   line is expected; it should not recur once the initialized filesystem exists.

2. Start a voice turn and ask, “Build me a hydration tracker with a blue
   water-drop identity.” A complete brief should ask no question; an ambiguous
   request may ask at most one focused enum question. Continue an unrelated conversation while the durable job
   runs; app generation must not block Voice. Before delivery completes, keep
   or reopen Voice in a listening or speaking state.

3. Wait for the independent verifier and outer packager. The service latency
   JSONL should contain `"kind":"app.ready"` with a bundle hash. The watch
   should log lines matching:

   ```text
   app.ready accepted bundle=<12 hex> bytes=<n>
   installed <app-id> <version> payload=<12 hex> bundle=<12 hex>
   ```

   An HTTP/announced-length mismatch must show **App download failed**. A
   bundle-hash, owner, key-ID, HMAC, ABI, canonical-metadata, or payload mismatch
   must show **App verification failed**. Neither may add the app to the
   launcher. If the valid install completes while Voice is open, the Voice
   surface must remain in place; only the completion haptic should occur.

4. Dismiss Voice normally. Only then confirm the trusted screen reads **APP
   READY**, shows the generated app name and `Installed <version>`, and offers
   **Launch now** and **Later**. Installation must not have silently launched
   the guest or displaced the active Voice interaction.

5. Tap **Launch now**. The generated UI should replace the prior guest surface
   without a new `[host] boot` line. Successful serial output includes the
   ordinary WAMR load/instantiate/start messages followed by:

   ```text
   [packages] running <app-id> <version> <12 hex> without reboot
   ```

   Log water once. The app package, icon, and theme persist; hydration data is
   deliberately session-scoped until the SDK gains host-owned guest storage.

6. Press Button B to return Home, then Button B again to open **APPS**. Confirm
   the installed launcher lists the app name and version; tap it and confirm the
   same generation launches again without rebooting.

7. Build or package a higher semantic version with the same app ID. Confirm the
   launcher shows the new current version and the prior generation remains the
   rollback candidate. Replayed `app.ready` messages for either retained digest
   must not flip current and previous.

8. For the rollback gate, use a deliberately instrumented new generation that
   passes startup but traps in a chosen semantic event handler. Trigger only
   that event. Expected output includes `guest failure`, then:

   ```text
   [packages] rolling back <app-id> from <new hash> to <previous hash>
   [packages] running <app-id> <previous version> <previous hash> without reboot
   ```

   Confirm the watch shows **RECOVERED**, identifies the restored version, and
   still provides **Launch now**. Home and Voice must remain available. If no
   previous generation exists, the native shell should report the crash and
   retain the embedded recovery path instead of rebooting.

   Replay `app.ready` for the failed bundle. It must not produce a new
   `installed` line or **APP READY** surface, change the restored current
   generation, or make the failed tuple appear as a launcher choice. The
   deterministic package tests separately exercise direct launch/reinstall
   rejection for that exact `(app_id, semantic_version, payload_sha256)`. A
   repaired build must change the payload hash or semantic version, creating a
   new generation triple.

9. As a timer-isolation check, install two distinct app IDs that use the same
   timer ID. Schedule the first, switch to the second, and verify the due event
   is never delivered to the second guest. Reopen the first app and confirm its
   owned timer state is still attributed to it.

## Deliberate v0 limits

- The shared HMAC key authenticates one trusted local user. It is not publisher
  identity, store review, delegation, transparency, or revocation.
- There is no personal-app capability approval UI. Existing independent
  verifier/import policy still applies, but the owner implicitly trusts an app
  that reaches this packaging step.
- HTTP transport may be plaintext on the local development network. The
  authenticated bundle and announced hashes protect artifact integrity; HTTP
  itself is not the trust boundary.
- Automatic rollback covers failures the host can detect and attribute. It does
  not yet promise recovery from every hang, watchdog reset, power loss, or
  native firmware defect.
- Each app retains up to eight one-way quarantine records. On the ninth distinct
  detected bad generation, a durable terminal block removes that whole app from
  the launcher and rejects launch/reinstall until the package profile is reset;
  an older known-bad identity is never evicted to make room.
- App IDs are bounded to 64 ASCII bytes and runnable Wasm payloads to 1 MiB.
  The device registry can index at most 32 app IDs, but actual capacity is
  byte-limited by the package partition and two retained generations per app;
  32 maximum-sized apps are not promised. It does not provide uninstall, store
  updates, or long-term version archives.
- Factory-erased storage may be initialized once after an all-`0xFF` scan.
  Non-erased filesystem errors never trigger automatic reformatting.
- Package storage requires heap-backed FatFs long filenames with a maximum of
  255 bytes; the build script enforces and migrates those settings.
- `/packages/active.wasm` is a legacy bare-Wasm boot fallback. It is not the
  current-generation pointer and is never written by the personal installer.

Published apps should get a separate asymmetric trust profile only when a real
distribution/store requirement exists.
