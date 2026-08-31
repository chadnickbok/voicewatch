# Initial replacement acceptance — 2026-08-31

The user resumed physical controls/apps/sleep-wake and interoperability/release
work, explicitly excluding induced network impairment. Ten minutes remains
sufficient. No endurance, loss/reorder/outage or factory-firmware restoration
gate is introduced here. This checkpoint is **not full replacement acceptance**.

## Watch and application delivery

Flash49 is the normal complete Ultra shell, built with the existing personal
owner/signing profile retrieved privately from Keychain. Synthetic commands are
excluded. Live identity matched permanent enrollment revision 183 before the
runner verified ESP32-S3, security state, 16 MiB flash, partition layout and OTA
selection. Only app0 was written; NVS, bootloader, partition table, OTA metadata
and package storage were preserved. The one-minute shell heartbeat passed.
The firmware remains installed. No credential-bearing image is published here.

The preceding flash48 observation lasted 600,075 ms, with 117 status snapshots,
no firmware faults, no active microphone/speaker snapshots and **zero physical
input events**. It is an idle check, not a ten-minute voice or physical UI pass.

The previous private sdkconfig had no personal-package owner or key, so the
installer was disabled. The product build now uses the existing profile.
The installer also needed the enrolled host's private CA: it previously used
only the public root bundle. The fix copies public roots under the enrollment
lock for an exact `https://host:port/apps/<sha256>` route, disables redirects,
retains hostname/time verification and checks enrollment revision throughout
download. The bundle's existing hash, HMAC and owner checks remain in force.

The physical package test passed on flash49:

- Authenticated MoQ/WSS session with the ordinary optimized native endpoint.
- One HTTPS request, verified signed bundle, durable package-registry commit.
- Duplicate `app.ready` causes no second download.
- No microphone samples, capture events or firmware fault.
- Temporary bench services stopped; permanent enrollment reapplied at revision
  **185**, followed by a separate permanent-host readiness observation.

The fixture is the repository Timer, already launched through the native WAMR
shell, installed under a new ID `dev.voicewatch.acceptance` with display name
**MoQ Test Timer**. It does not replace another app. This is a trusted repository
fixture, not evidence that a new generated app passed the generation pipeline.
Physical launch, interaction, timer completion and uninstall remain unobserved.

The first package harness stopped before enrollment because its Unix socket path
exceeded the supported length. The corrected run uses a separate owner-private
short IPC directory. Both receipts are retained; no firmware failure is inferred
from that harness error.

With installation enabled, minimum free internal RAM is **86,756 bytes
(84.7 KiB)**, below the provisional 96 KiB target. The observed idle free value is
about 151,855 bytes; sampled largest blocks remain above 32 KiB. The installer
adds a 12 KiB internal stack because it writes flash. There was no allocation
failure, but this is not full UI/voice/installation stress or resource acceptance.
Do not lower the budget or move a flash-writing task's stack to PSRAM just to
make the result pass.

## Interoperability

Fresh unchanged-reference testing passes the native matrix (constructor/TLS
self-checks and 22 cases) and 20 additional normal engine exchanges. The reference
oracle checks both C directions, invalid inputs, truncation and fragmentation.

The existing terminal receive defect still reproduces with a **software reader
scheduling delay**, while QUIC ACKs and the network continue normally. This is
separate from induced network impairment. The unchanged pinned subscriber can
finish its control stream before reading a promised group; the missing group and
missing terminal receipts remain a failure.

The opt-in terminal-drain candidate passes the current 22-case matrix, 100
connection exchanges, 27 delayed-reader cases and 27 lifecycle cases. This is
176 integration cases, plus constructor/TLS self-checks. Candidate files match
the patch manifest. Its prior 853 unit-test result is historical, not rerun here.
The first fresh candidate run used an older prepared harness which lacked the
new blocked-stream modes; its matrix failure is retained. A new preparation
used the current harness, with the currently untracked `blocked.rs` explicitly
copied into that isolated source snapshot before a locked build.

No candidate patch was applied to the compatibility pin, ordinary host, firmware
or upstream repository. The plan explicitly requires an unmodified reference;
adopting the reviewed upstream patch would change that requirement. A candidate
pass does not close it. Candidate patch SHA-256 remains
`14d48121c8039da548d82086122d0e80e3e2cc0734a46b87cee2dce45f521239`.

## Release verification

| Check | Current result |
| --- | --- |
| Live-agent Python suite | 426 passed, four warnings retained |
| Explicit firmware/native/supervisor integration lane | 62 passed |
| Native LVGL/WAMR CTest | 18/18 passed |
| Shell/app/lifecycle Python selection | 9 passed, including Timer during display sleep |
| Rust endpoint, all features | 39 passed; Clippy with warnings denied passed |
| Core, adapter, touch and audio tests | Passed; synthetic host tests, not physical inputs |
| Current native QUIC/TLS with UBSan | Constructor/TLS self-checks and all 22 cases passed, halt-on-error enabled |
| Normal optimized native endpoint | Built; used by the physical package bench |
| Public complete-shell source export | Fresh dependency/build tree passed; no private sdkconfig used |
| Normal private product firmware | Built, inspected, app0 flashed and heartbeat passed |
| Build inspector | Positive public build plus four negative configuration checks passed |
| Current remote CI | Not run; existing successful CI covers the earlier committed library |
| Full native ASan | Not passed in this environment; minimal Linux Docker probe never started |
| Distribution license | Existing backend GPL decision retained; library root/core selection unresolved |

The discovery regression found during supervisor tests is fixed: a MoQ service's
mDNS instance name includes its host and control port, so a second service can
start beside the deployed instance. The two real crash/restart integration cases
now pass. The installed permanent host has not been updated or restarted.

The source export contains current dirty working files and pinned dependencies;
it is **not a clean committed release or a bit-for-bit reproducibility claim**.
Build and lock hashes are recorded. The new root workflow builds the full shell
with public dummy credentials and validates MoQ/TLS/package linkage, certificate
time checks, application size and absence of WebRTC/synthetic diagnostics.
Its commands passed locally, and YAML parsed; GitHub execution remains pending.
It uploads only compile-only artifacts, never private firmware or sdkconfig.

The Docker probe used one owned, network-disabled container. It stayed in the
created state with PID 0 and no start timestamp, then was removed. No daemon or
unrelated service was restarted. This is an environment limitation, not an ASan
pass or a sanitizer finding. A working Linux executor is needed for that check.

## Hands-on acceptance still required

On the installed flash49, spend up to ten minutes checking:

1. Open Apps, launch **MoQ Test Timer**, start/cancel it, return to the launcher
   and Home, then open Agents and return. Check layout, touch targets and state.
2. Use the voice button for one explicit voice turn; holding it should not
   repeatedly toggle capture. Cancel a second turn with the normal back/palm
   input. Check complete audible playback and the listening/speaking indicators.
3. Start a timer, use a short power-button press to blank the display, then wake
   it by touch. The wake touch must not activate an underlying control. Confirm
   the timer survives and voice works after wake. Repeat from Home once.
4. Confirm no unexpected recording, stuck speaker, missed/doubled input,
   unusable screen or firmware reset. Record any failure; do not infer physical
   acceptance from automated model tests or a serial heartbeat.

Current sleep is **display sleep**, not ESP32 deep sleep or suspend/resume. The
native tests verify timer service continuity while rendering is asleep; actual
panel blanking, wake suppression and physical GPIO/PMIC behavior need observation.

No microphone PCM is saved. Published receipts contain fixed labels, numeric
telemetry, source/build hashes and synthetic host fixtures only. Private profiles,
keys, raw watch logs, signed bundle and firmware remain owner-private outside Git.
