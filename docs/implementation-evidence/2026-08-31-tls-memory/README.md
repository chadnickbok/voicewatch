# Bounded wolfSSL allocation

This checkpoint adds a separate, enforced heap-request cap for the actual
wolfSSL provider used by the MoQ Ultra firmware. It does not declare the full
WebRTC replacement, library candidate, or total-device memory gate complete.

## Implementation and scope

The production ngtcp2 allocator already has a per-connection 128 KiB cap, but
that excludes wolfSSL and crypto objects. Some backend crypto calls allocate
with a null heap hint, so simply passing a per-adapter heap to the TLS context
would leave part of the active path unbounded.

One compile-time `XMALLOC_USER` configuration now supplies the same callbacks
to wolfSSL and the adapter/backend. The **256 KiB aggregate wolfSSL request cap**
includes every heap hint used by these paths. It is shared across connections;
it is not a separate allowance for each endpoint or a reserved arena. The
ESP-IDF allocator retains the managed FreeRTOS profile's internal 8-bit memory
class. There is no unlimited or external-RAM fallback.

Aligned headers and retained capacity count toward the limit. Growing realloc
charges old and new storage simultaneously, and failure leaves the original
pointer/data unchanged. Shrinking retains the actual capacity charge. Counters
saturate and a process-lifetime mutex protects requests and accounting. Failure
to create the lazy ESP-IDF mutex rejects allocation without accessing unlocked
state. Snapshot availability is explicit, rather than reporting an unavailable
snapshot as zero usage. Host fault-control entry points are absent from firmware.

Endpoint and Ultra diagnostics distinguish the shared TLS counters from the
per-connection ngtcp2 counters. Endpoint values are last owner snapshots, not
post-destruction leak measurements. System allocator/RTOS bookkeeping, stacks,
sockets/Wi-Fi, mbedTLS HTTPS/WSS, codecs, UI and service arenas remain outside
these two request caps. The cap does not guarantee allocation success below it
or satisfy the plan's complete SRAM headroom and UI-load budgets.

No downloaded dependency is edited. The pinned TLS version, verified roots,
hostname/IP checks, ALPN, ticket policy, authorization and transport deadlines
remain unchanged. Compilation checks confirm the actual TLS, backend and
adapter translation units use the callbacks, and the populated firmware ELF
contains the allocator and handshake symbols without the test reset/fault API.

## Real-backend validation

The host harness compiles the production adapter against pinned wolfSSL and
ngtcp2. The TLS constructor test observes 128 successful allocation sites and a
43,046-byte peak. Injecting failure at each site causes 117 failed constructors
and 11 tolerated optional failures; every trial releases all tracked storage.
Forty-three smaller-budget trials and 25 clean reconstruction cycles also
release all tracked storage. These are constructor/initial-packet tests, not a
claim that every possible handshake allocation site has been fault-injected.

Allocator edge tests cover arithmetic overflow, alignment, failed growth/data
preservation, shrink accounting, shared null/non-null hints, concurrent callers,
and first-lock failure. Separate real-peer tests force a 1-byte constructor cap
and a 64 KiB handshake cap. Both fail without connection or application data,
and clean up to zero tracked bytes and blocks. The latter reaches certificate
processing and rejects promptly when the budget cannot satisfy an allocation.

Normal-cap cases still cover successful IPv4/IPv6/name-verified connections,
wrong roots/hostnames, expired certificates, wrong ALPN, numeric names that must
not match DNS SANs, MoQ setup, publication/subscription, compressed/plain catalog
paths and real Opus exchange. Each native test process now checks TLS accounting
after destruction, including endpoint and audio helper paths.

Two normal-suite iterations exercise 40 peer cases, including 60,000 completed
raw streams in each direction across the six valid raw-QUIC cases. A separately
instrumented UBSan build runs one full iteration and the allocator self-test
with halt-on-error enabled. UBSan does not replace ASan; the previously recorded
macOS ASan startup problem and the Linux CI/sanitizer gate remain open.

## Firmware observation

The first allocator image, flash44, boots into the complete shell and records
TLS usage, but its 60-second runner window misses the uptime-heartbeat marker.
The application begins its 60-second heartbeat interval after startup; the
window was insufficient. The failed observation is retained as failed, not
relabelled as a pass or treated as a stopped process before the runner exits.

Flash45 adds the first-lock failure guard and snapshot-validity flag. The same
heartbeat marker passes with a 120-second observation allowance: it appears at
65,388 ms after boot. The image is 3,425,872 bytes and is written only to the
existing app0 slot after live identity, layout, OTA-selection and security-state
checks. No bootloader, partition table, NVS, OTA metadata or user storage is
written. A startup snapshot reports 49,415 bytes of TLS peak, 16,611 live bytes,
and no allocation denial/system failure. That snapshot is not a stress verdict.

## Physical provider checks on flash45

Both private-host runs use the real STT/model/watch-tool/TTS path, physical Ultra
microphone and speaker, and the unchanged fixed-phrase scoring policy. Synthetic
fixture audio is played at volume 60, with Mac volume/mute restored afterwards.
Microphone PCM is counted and discarded, never persisted. The 5% loss case adds
120 ms RTT to the existing Wi-Fi path; it does not claim measured total RTT.

| Run | Case | Result | Complete speaker samples | PLC / late / silence |
| --- | --- | --- | --- | --- |
| p113 | Text and background output without microphone, then three voice turns | PASS; all voice transcripts have zero word errors | 181,540 across five responses | 3 / 3 / 0 |
| p114 | 5% loss, 120 ms added RTT, 800 ms capture outage, then three voice turns | PASS; failed capture has no STT commit; recovery in 1,000 ms; all transcripts zero-error | 129,499 across three responses | 29 / 29 / 0 |

All eight complete firmware speaker totals match their host receipts in order.
Both runs verify fresh idle reconnect and complete service startup/shutdown with
no component timeout. Nineteen TLS snapshots are valid, remain under the shared
262,144-byte cap, and record the same 49,415-byte lifetime peak with zero denials
or system allocation failures. The largest ngtcp2 snapshot is 47,808 bytes under
its 131,072-byte cap. No firmware crash marker is recorded.

These snapshots accumulate across the boot's connections and include an active
TLS connection; they do not assert zero live memory on the device after teardown.
The native tests separately prove tracked cleanup after destruction. Concealment
and late counts can describe the same chunks; nonzero counts alone do not fail
product readiness. Exact samples and successful recognition are not subjective
speaker-audibility measurements. Earlier speech failures are retained and are
not replaced by these two passing runs.

The portable C host, adapter and audio suites pass, as do 384 Python tests
(four warnings) and 46 explicit native/firmware-protocol tests. Permanent
enrollment is reapplied at revision 167 with a fresh permanent-host readiness
event. The MoQ supervisor/child and legacy host processes are unchanged. Flash45
remains installed; all source changes remain uncommitted. Verification counts,
exact image/source identities and private-log hashes are in `verification.json`.

## Remaining acceptance

The original speech failures, delayed application-group/blocked-stream cases,
physical PTT/UI/apps/packages/sleep-wake, calibrated latency, complete device
allocation/headroom, reference compatibility, ASan/CI, and original endurance
and release gates remain open. No persistent-host rollout, reference-pin update
or default-transport switch is part of this checkpoint. Firmware restoration is
not required. Raw logs, images, keys, enrollment and provider data remain private;
only numeric results, fixed labels and source/log hashes are published here.
