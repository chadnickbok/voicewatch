# MoQ replacement implementation progress

Updated 2026-08-30. Objective remains the complete
[implementation plan](moq-webrtc-replacement-plan.md), including all nine review
findings, operational protocol/audio, security, host service, and the full Ultra
shell. This checkpoint does not satisfy the library-candidate or product gates.

## Implemented foundation

Changes are in the existing `libs/moq-esp32` checkout, based on `f827391`.
At the foundation checkpoint no VoiceWatch application code had changed. The Ultra memory probe, verified
network bring-up and 30-minute synthetic transport soak have completed. Both
soak endpoints report 90,000 streams and 180 resets per direction, with bounded
heap and teardown release. This does not establish audio or full-shell readiness.
The operational engine, owned media service and task endpoint now pass native
bidirectional interoperability against the pinned unmodified Rust endpoint.
The task endpoint also ran on the Ultra: both catalogs and eight synthetic Hang
frames per direction passed, with owned cleanup. This is an operational runtime
checkpoint; its original synthetic packets did not prove codec interoperability.

The next checkpoint adds real Opus receive playout: OpusHead pre-skip, separate
terminal/discontinuity events, frame ordinals, bounded jitter/PLC and cancellation
identity checks. Nine unmodified pinned Rust Producer/Consumer fixtures match
the C player's exact sample lengths and PCM. On the Ultra, the pinned Espressif
decoder passed all nine fixtures three times and PLC; worst measured decode was
2.6 ms per 20 ms packet. This standalone image uses no network or I2S. Capture
encoding was still pending at that checkpoint. Evidence is in the
[audio-player checkpoint](implementation-evidence/2026-08-30-audio-player/README.md).

The preceding checkpoint implements capture encoding, empty reset groups and
atomic terminal tails. A real-Opus network exchange passes on both the native
endpoint and physical Ultra: 537 synthetic input samples reach the unmodified
Rust Consumer; the reference Producer's 537 response samples match exactly in
the C player. This establishes the combined codec/network path, not real
microphone/speaker operation, production authentication or full-shell readiness.
See the [capture and network evidence](implementation-evidence/2026-08-30-audio-capture/README.md).

**User direction: future tests leave our new firmware installed. Default-firmware
preservation and restoration are not requirements.** The old live runner restored
once at the end of the completed soak. Its replacement no longer requires a
backup/full-flash baseline comparison and validates live board/security/layout
metadata before app0 writes. Eight offline runner checks pass; the replacement
runner has now flashed and tested the operational endpoint on hardware. The
passing combined physical audio/MoQ echo image is preserved as a checkpoint. The latest installed firmware is the full-shell MoQ diagnostic described below.

## Physical Ultra audio and MoQ echo checkpoint

The optional Ultra BSP now implements verified PMIC speaker power, PDM capture,
bounded PCM ownership and I2S playback/cancellation. The current combined image
passes local acoustic tone/cancel/restart checks followed by a live microphone
→ reference Opus decode/re-encode over MoQ → Ultra decoder/speaker exchange.
It transfers 19,200 samples each way with no microphone drops. Speaker completion
is measured after DMA drains, and stale-owner handoffs are rejected.

Two 10 ms DMA buffers bound already-submitted playback; invalidation clears the
480-sample pending FIFO. The final run's stop fence took 633 microseconds. The
reference comparison has 43 samples differing by one 16-bit LSB (RMS 0.0473),
with exact sample counts. This is within the established codec tolerance, not
bit-exact or speech-quality evidence. The earlier echo run had 39 one-LSB
startup differences. Both raw recordings and credential-bearing images stay private.

The combined network exchange took 6,397 ms, including connection/record/play
work, and reached 97,900 bytes minimum internal free RAM. Free stack watermarks
were 41,160 audio / 7,400 network / 2,868 DNS bytes. These numbers exclude display,
Wasm and the full shell. No charging settings or non-app0 flash were changed.
The passing audio checkpoint image SHA-256 is
`5ad5e674f59fa13011efb526be42afbe0a5622caf62ec97b6512fc8f5e72d7c8`.

See [physical audio evidence](implementation-evidence/2026-08-30-ultra-audio-io/README.md).
All 18 native interoperability/security matrix cases still pass with the new
reference echo crate linked. Production auth/host, PTT, display/touch/buttons/
haptics, actual speech, sustained operational audio and full-shell acceptance
remain open. This is a bounded explicitly selected diagnostic, not automatic
runtime capture or the complete VoiceWatch replacement.

## Ultra UI and full-shell integration in progress

A separate optional UI BSP now owns CO5300 QSPI display, verified CST9220
410x502 touch, XL9555 expander, DRV2605L haptics, short power-key events and
battery reads. It shares I2C with audio; serialized PMIC read-modify-write
protects independent display/speaker rail bits. It does not alter charging or
upload peripheral firmware. The second physical startup probe passes all
205,820 pixel transfers and repeated touch-register polling with zero bus errors.
Touch/button presses and optical/haptic behavior have not yet been observed.
The first run failed an IDF zero-length SPI buffer check, now corrected.

VoiceWatch now has an Ultra board target and full 410x502 LVGL display. Two
physical shell runs passed one-minute heartbeats: first offline, then with Wi-Fi
and the fixed LVGL heap placed in PSRAM. WAMR started the embedded recovery app,
the native Home rendered, and existing `ffat` storage mounted without formatting.
There were no installed apps in this test profile and personal-install keys were
not provisioned, so this does not establish installed-app or delivery parity.

The first image exposed LVGL's 128 KiB pool occupying internal SRAM. Its supported
large-array annotation now places the same fixed pool in external BSS; the ELF
confirms `work_mem_int` at `0x3c2542e8`, size `0x20000`. No managed source or heap
size changed. The Wi-Fi baseline has 220,123 bytes free internal RAM, minimum
183,992 and largest block 147,456; PSRAM free is 7,912,024. These exclude MoQ,
Opus and WSS. Maximum observed flush was 6,417 us; initial rendering reached
187,893 us. Those are baseline observations, not final UI latency acceptance.

The historical Wi-Fi baseline shell image SHA-256 is
`469861f8d4cf5e827592dd4ec773e5398c084d3fe57599214c3ed39bd23b7371`.
That baseline kept microphone/speaker off and legacy WebRTC disabled. The
subsequent checkpoint below connects the MoQ audio owner; production
control/host are still missing. Network recovery
never erases NVS automatically. The Ultra table matches inspected live metadata;
only app0 was written. Build/flash/monitor scripts now accept `t-watch-ultra`,
with app-only flashing and a heartbeat check. See the
[full-shell baseline evidence](implementation-evidence/2026-08-30-ultra-shell/README.md).

## MoQ audio integrated into the full shell

The internal media seam now selects WebRTC or MoQ at build time while the shared
service retains control, capture/request/guest correlation, actions, NVS and
package behavior. The Ultra implementation owns its own 64 KiB PSRAM audio task
and the library endpoint, using the existing board's timestamped/fenced audio
API. It does not open a second board. Native cancellation invalidates media
before waiting for the control queue; playback completion follows DMA drain.

The latest physical diagnostic passes 19,200 microphone samples through the
pinned reference decoder/re-encoder and plays all 19,200 response samples with
zero microphone drops and zero concealed, late or pressure-discarded player
frames. Independent PCM verification found 38 one-LSB differences (RMS 0.04449),
within the established tolerance. WAMR, native Home, Wi-Fi and existing storage
remain active, followed by a one-minute quiet interval and joined endpoint
cleanup. This exercises the internal media API, not public Voice Orb/PTT control.

The first run passed audio but exposed a 510 ms flush and only 95,040 bytes
minimum internal free RAM. The second failed before capture due to codec startup
ordering. The third fixed startup and TLS/UI core contention but still reached
only 86,528 bytes internal free. With the task-only endpoint/configuration and
optional service arena in PSRAM, the fourth run reaches 114,792 bytes minimum
internal free, minimum largest block 81,920 and maximum flush 8,031 us. Worker
stacks and DMA remain internal. Initial rendering still reached 291,299 us;
WSS/bootstrap and active voice UI stress are absent, so final UI/resource gates
remain open. The readiness interval was 4,476 ms, not a passing connection-latency
result. Free stacks were 43,532 audio / 7,248 network / 2,888 DNS bytes.

The latest installed image is
`e78688cae157f1187591f9835a941ef1c438d90d0aafa918fac11c86543cb56c`.
It is an explicitly selected USB-provisioned public-token diagnostic. Production
MoQ currently fails closed instead of using anonymous legacy mDNS/WebSocket
control. The default Ultra profile still disables voice. No restored firmware,
NVS/package erase or certificate-validation bypass was required.

The MoQ ELF contains no peer open/send symbols. A separate CoreS3 WebRTC image
still builds with those symbols and no MoQ endpoint; it was not flashed. All 18
native interoperability/security cases, core/audio regressions, 97 existing
live-agent tests, five storage checks and five artifact-verifier tests pass.
The live-agent suite retains four warnings; no new host adapter is implied.
See [full evidence and failed runs](implementation-evidence/2026-08-30-shell-moq/README.md)
and [media ownership details](moq-shell-media-seam.md).

Authenticated bootstrap, credential/time/reconnect policy, the Rust/Python host
bridge and public response bindings are next. Real voice turns, interruption,
PTT, full UI/package parity and all sustained/impairment gates remain incomplete.

## Capture and network checkpoint (historical)


- The bounded capture arena retains at most 320 partial PCM samples and two
  pending packets. Fragmented input, backpressure, retry identity, explicit
  discontinuity, terminal flush and cancellation are implemented and tested.
- The pinned Espressif VOIP encoder's 104-sample lookahead at 16 kHz is verified
  against a queried reference encoder and independently decoded target packets.
  OpusHead advertises 312 samples at 48 kHz. Encoder bitstreams need not match.
- The real exchange exposed normal catalog rendition removal interrupting
  queued response audio. CATALOG_UNAVAILABLE now prevents new receives while
  allowing an existing bound range and its PCM to drain. Format changes and
  source replacement still invalidate old media. A capture sink failure also
  now prevents a later finish call from reporting success.
- All 18 native QUIC/security/interoperability cases pass, including the new
  bidirectional codec exchange. macOS normal and Linux normal/ASan/UBSan core,
  endpoint, capture and player suites pass. Both Rust fixture sets regenerate
  byte for byte. The system libopus binary itself is not instrumented.
- On the Ultra, the same exchange runs on a dedicated 64 KiB PSRAM audio task,
  separate from the network owner. Both peers pass. Measured free stack was
  41,624 audio / 7,404 network / 2,876 DNS bytes. Internal minimum was 106,164
  bytes; PSRAM returned to 8,386,076 bytes after joined cleanup. Internal free
  heap retained an unattributed 300-byte difference. The exchange took 4,362 ms,
  which is not a handshake or conversational latency acceptance result.
- This remains a short synthetic-PCM exchange with no I2S, PMIC, display or UI.
  Real microphone/speaker integration, long responses, repeated sessions,
  acoustic verification and full-shell resource headroom remain unproven.

## Audio receive checkpoint (historical)

- Catalog selection now accepts the reference producer's minimal OpusHead,
  retains its 48 kHz pre-skip and rejects unsupported decoder configurations.
  Same-track decoder-format changes invalidate receive operations and held
  leases. CATALOG/MEDIA_READY copy the selected format to the consumer.
- FRAME includes the frame ordinal. AUDIO_END identifies timestamp-only Hang
  terminal markers; DISCONTINUITY identifies empty groups. These are distinct
  from dropped/late-packet GAP events, including a terminal marker followed by
  tail packets whose timestamps precede the marker's timestamp.
- The optional receive player copies at most ten Opus packets, reserves six
  boundary slots, prebuffers 60 ms, reorders groups/frames, trims pre-skip and
  terminal padding, preserves timestamp pauses and resets codec epochs. It
  limits consecutive PLC to three frames, then uses silence; owner stalls over
  200 ms skip stale audio. The audio owner still must use the service's commit
  fence and the eventual board driver's DMA cancellation contract.
- macOS normal and Linux normal/ASan/UBSan suites pass, including nine fixtures
  exported by the unmodified pinned Rust Producer/Consumer and regenerated
  byte-for-byte. Native QUIC's 17-case matrix passes after these core changes.
  macOS ASan was stopped after a sampled pre-main sanitizer/dyld startup deadlock;
  it is not reported as passing. Linux uses an uninstrumented system libopus;
  the portable player and core are instrumented.
- On the Ultra, 27 fixture runs produced exact PCM and lengths, and a PLC call
  passed. The first run exposed the Espressif wrapper's rejection of a null PLC
  input; a valid zero-length input fixes it. Worst decode was 2,600 us over 82
  calls. PSRAM before/after was 8,386,192 bytes; internal free heap changed from
  374,499 to 374,479 bytes (20-byte residual, not attributed). Main-task stack
  free watermark was 7,336 bytes. These standalone figures exclude Wi-Fi,
  QUIC/TLS, I2S and display load and do not establish full-shell headroom.
- Espressif encoder metadata reports no codec description (`spec_info_len=0`).
  Capture work must establish and verify lookahead, then implement empty-group
  resets and terminal-tail publication. No microphone-to-host or host-to-speaker
  audio is claimed yet. Production authentication/reconnect, host integration,
  full Ultra BSP/shell and all final product gates remain open.

## Original finding status

| Finding | Current implementation and evidence | Remaining gate |
| --- | --- | --- |
| R1 active-path link failure | One shared wolfSSL feature list; ngtcp2's client ticket API retained, unused server TLS excluded, no session cache added. Fresh ESP-IDF 5.5.5 populated-config build links and passes ELF symbol checks. CI separately builds empty and public-dummy configurations. Physical verified QUIC/SETUP now passes after the target AES correction below. | Actual GitHub CI execution; intended deployment authentication and latency |
| R2 peer bidi replies | Peer-open callback allocates send halves; local opens reserve advertised peer bidi capacity. Actual-adapter tests reply on the same peer stream. Real TRACK/SUBSCRIBE/catalog exchanges pass natively and on Ultra. | Retain regression coverage in the final audio/product image |
| R3 stream-credit exhaustion | Bounded peer registry returns credit exactly once only for observed remote opens. Tests simulate 100,000 uni plus 100,000 bidi lifetimes and reject duplicate/local credit returns. | Real native and 30-minute Ultra synthetic turnover pass; final operational/audio image must be revalidated |
| R4 blocked-stream starvation | Scheduler skips blocked streams, preserves ACK-owned buffers, prioritizes control and subscriber track/group priority, and rotates ties. SUBSCRIBE_UPDATE changes queued/backend-owned unsent groups. Owned media deadlines and abandon/reset are implemented. | Real loss/congestion and final audio latency tests |
| R5 receive datagram mismatch | Advertises 1,350-byte receive maximum; reads one extra byte and drops oversized input, including truncated prefixes. Loopback UDP and bounded receive-drain tests pass. | Real peer packet/probe cases on lwIP |
| R6 callback reentrancy | Deferred adapter events and the owned service isolate media/UI callers. The task endpoint exclusively owns QUIC and joins its DNS/network workers before freeing memory. Native producer/consumer concurrency and Linux sanitizers pass; target cleanup returns PSRAM to baseline. | Broader real-QUIC failure/reconnect races, decoder/DMA ownership in the audio layer |
| R7 missing operational client | Engine, catalog negotiation, native error translation, owned TX/RX leases, explicit media range binding and task endpoint are implemented. Real native and Ultra synthetic exchanges pass against unmodified pinned Rust APIs. DNS is off the owner; copied authorization/time leases expire, stale DNS results are discarded, and close rejects old attempt IDs. Opus receive playout now passes host and standalone target fixtures. | Production bootstrap/scope/time policy, credential renewal/backoff, capture/tail publication, device audio, production host endpoint and full VoiceWatch integration |
| R8 duplicate-start corruption | Validate session transition before clearing client state. Tests preserve partial SETUP, blocked FIN, and partial peer input across invalid starts. Original source fails the new regression. | Closed at portable-core regression level |
| R9 optional-bound overflow | Reject present values >= varint maximum before adding one. Tests cover legal/illegal start and end values, unchanged output on rejection, and irrelevant absent values. Original source fails the new regression. | Closed at portable-core regression level |

Additional changes:

- The active handshake example has a 2 MiB factory application partition. A
  fresh build exposed that the full image exceeds the old default 1 MiB
  partition. This is an example-only partition map, not an Ultra shell map.
- Both host suites use separate sanitizer binaries. `make test sanitize`
  cannot accidentally reuse the normal binary as its sanitizer result.
- README/backend documentation now distinguishes implemented primitives from
  unverified or missing capabilities. The root/core license remains unselected;
  the existing backend GPLv3 decision is recorded without assigning a license
  to unrelated source files.

## Verification at this checkpoint

- Portable core: 300,621 assertions passed on macOS and Linux. Approximately
  300,000 assertions are varint roundtrips, not distinct end-to-end scenarios.
- Original `client.c` and `lite05_control.c` each fail their new regression when
  substituted independently; corrected sources pass.
- Actual-adapter source: deterministic tests pass on macOS and Linux, including
  20 repeated macOS runs. ngtcp2 behavior is mocked; real loopback UDP exercises
  oversized datagram handling. This is not 200,000 real QUIC streams.
- Both suites passed Linux GCC AddressSanitizer/UndefinedBehaviorSanitizer,
  with separately compiled instrumented executables and normal leak detection.
- Fresh-config ESP32-S3 active handshake build passed, with real ngtcp2 and
  wolfSSL and public dummy credentials. The verifier confirms actual TLS and
  handshake symbols in the linked ELF. No certificate check was disabled.
- `git diff --check` passed. GitHub workflow execution is not claimed.

Evidence: [snapshot and artifact hashes](implementation-evidence/2026-08-30-foundation/snapshot.json),
[Linux test/sanitizer output](implementation-evidence/2026-08-30-foundation/linux-regressions.log),
[build result](implementation-evidence/2026-08-30-foundation/build-result.txt).

## Added oracle and native interoperability evidence

The new [oracle/native evidence](implementation-evidence/2026-08-30-oracle-native/README.md)
records source, dependency, executable, and firmware hashes. These results extend
the earlier foundation evidence; they do not close the operational-client or
product gates.

- The executable Rust oracle uses `moq-dev/moq` at
  `eb5776e21eeaecba8e844be53c821895c178bcaf`, with visibility-only instrumentation
  for its private codec modules. Its 74 cases include 62 Rust-to-C decodes,
  56 independently C-encoded cases decoded by Rust, nine invalid C cases,
  585 truncated inputs, and 68 incremental fragmentation cases.
- Added bounded SUBSCRIBE_UPDATE and GOAWAY codecs. Incoming GOAWAY URIs are
  untrusted data, not authority to redirect or forward credentials.
- The oracle executes upstream Hang catalog and `moq-flate` compressed-frame
  code, and cross-checks raw DEFLATE/dictionary framing against Python zlib.
  Embedded catalog parsing/decompression was missing at that checkpoint; the
  catalog checkpoint below adds and tests it.
- A reference-seeded C mutation harness passed 31,806 deterministic inputs under
  Linux ASan/UBSan. This is a bounded regression run, not exhaustive fuzzing.
- Real native tests compile the actual adapter/core, pinned ngtcp2, and wolfSSL.
  Each positive connection carries 10,000 uniquely numbered uni streams in
  **each direction** and 65 peer bidi replies. DNS, IPv4, and IPv6 cases pass.
  macOS ran the 12-case matrix three times; Linux ran it once with the whole C
  dependency chain instrumented by ASan/UBSan. Counts must not be presented as
  a single 90,000-stream connection or a 30-minute audio soak.
- Unknown CA, wrong DNS/IPv4/IPv6 identities, expired certificates, ALPN mismatch,
  and numeric-looking DNS SANs all reject before application connection/data.
  The matrix asserts the rejection diagnostic so timeouts cannot pass.
- The actual C client completes SETUP/path exchange against an **unmodified**
  pinned `moq-net::Server`. Its public `jwt` path fixture is not an authentication
  test. There are no publish/subscribe origins in this case.

The live tests identified and corrected three additional interoperability or
verification problems that deterministic mocks had not covered:

1. wolfSSL's default eight-byte ticket nonce rejects Rustls's normal
   post-handshake tickets. Both profiles now use fixed 255-byte nonce storage,
   without enabling session caches, persisted tickets, resumption, or early data.
2. The pinned native MoQ wrapper assumes QUIC DATAGRAM capability. The adapter
   now advertises it and supports bounded deferred receive callbacks. The
   stream-only profile explicitly discards/counts datagrams. A separate byte
   arena prevents them from consuming stream receive storage; they never return
   stream flow-control credit. No datagram-audio publisher was added.
3. wolfSSL 5.8.2's textual IP-name matching mishandles compressed IPv6 and can
   accept numeric-looking DNS SANs. Numeric endpoints now require exact binary
   iPAddress SAN matching before the connected callback, with canonical text for
   the underlying name check. SNI is used only for DNS hosts. Peer ALPN/TLS close
   alerts are also preserved instead of being replaced by a generic local error.

The updated populated ESP32-S3 handshake firmware links and passes the ELF
symbol verifier with these changes. The native CMake TLS configuration differs
from ESP-IDF; host results do not establish target memory use or target TLS
behavior. New CI jobs reproduce the oracle, mutation tests, real sanitized
transport matrix, and separate Ultra probe build; actual GitHub execution is
not claimed.

## Ultra feasibility preparation

Read-only ROM/stub interrogation identifies the connected device as ESP32-S3
revision 0.2, with 16 MiB external flash. The manufacturer's Ultra source is
pinned at `38e6f8dee3ba78b340512af9a013365ef248a7d0` in
`libs/moq-esp32/boards/twatch_ultra/hardware.lock.json`. It specifies external
quad PSRAM; the older S3 examples remain octal and must not be used as an Ultra
board profile.

`examples/twatch_ultra_probe` is a separate quad-PSRAM image. It validates memory
sizes, exercises two patterns over a 4 MiB PSRAM allocation, and measures
internal/PSRAM heap around a verifying TLS context allocation. It deliberately
does not initialize NVS, Wi-Fi, audio, display, touch, GPIO expansion, or power
rails. This is preparation for bring-up, not an Ultra BSP or shell.

The full 16 MiB backup was saved privately and verified against the device's
flash digest. The existing image uses app0 at 0x10000, app1 at 0x410000, NVS,
OTA data, FFAT, and coredump partitions; secure boot and flash encryption are
disabled. Only 0x60000 bytes of the existing app0 region were temporarily
replaced for the probe. The partition table and user-data regions were not
changed. The saved app bytes were restored, and a full 16 MiB digest check
confirmed that all original flash contents match again. The device is left in
ROM download mode, matching its initial state.

The [physical result](implementation-evidence/2026-08-30-oracle-native/ultra-hardware-result.json)
confirms 8 MiB quad PSRAM, a two-pattern 4 MiB memory test in 927,903 microseconds,
and a verifying wolfSSL/ngtcp2 TLS-context allocation. Free internal heap was
376,547 bytes before allocation, 373,575 during, and 376,547 after freeing; the
minimum observed was 341,544 bytes. These figures are for the tiny probe, not
Wi-Fi, a QUIC connection, audio, or the full shell. The core struct is 8,704
bytes on this target. No target network/TLS handshake or audio is claimed.

USB hard reset initially left the chip in download mode; the successful run
used the supported watchdog reset after clearing the force-download latch,
with ESP-IDF monitor's safe RTS/DTR ordering when USB re-enumerated. Earlier
unobserved attempts were restored and verified as well. The private backup
contains device/user data and must not enter Git or published evidence.

## Physical network bring-up and operational-runtime foundation

Evidence is in [the Ultra network checkpoint](implementation-evidence/2026-08-30-ultra-network/README.md).
This work made no changes to the VoiceWatch application or the Ultra audio/UI BSP.

- Added a separate quad-PSRAM network workload with Wi-Fi/PHY persistence
  disabled. Its Wi-Fi credentials, ephemeral CA, USB-provisioned test timestamp,
  firmware and raw logs stay in private storage. The public SETUP path fixture
  is still not production authentication or a production trusted-time policy.
- The first physical QUIC attempt failed authentication of the Initial packet.
  Independent OpenSSL-generated known-answer tests isolated bad nonempty
  AES-GCM/ECB results in the pinned wolfSSL accelerated ESP32-S3 profile; HKDF
  and empty GCM passed. Selecting supported software AES consistently in the
  shared feature list fixes the target vectors, including repeated/in-place
  encryption/decryption and altered-tag rejection. Verification remains enabled.
- Two short physical runs then passed verified TLS/ALPN, unmodified reference
  SETUP, and 500 uniquely numbered synthetic streams per direction. The extended
  run additionally passed one reset per direction, three ephemeral bidi replies
  and two ping replies over a persistent bidi control stream. Each attempt
  restored the saved app bytes and verified all 16 MiB against the baseline.
- Short-run handshakes took approximately 3.4–3.8 seconds, so the proposed
  connection-latency gate is **not met**. The extended short run observed a
  147,928-byte minimum internal heap and 3,788-byte main-task stack watermark.
  This is Wi-Fi/transport only, without audio, LVGL, WAMR or product control.
- The 90,000-group-per-direction, 30-minute workload passed on both endpoints,
  with 180 resets per direction, 182 bidi replies and 181 persistent-control
  round trips. Minimum internal heap was 149,024 bytes; teardown free internal
  heap was 267,123 bytes and free PSRAM 8,386,076 bytes. Sanitized final results
  and firmware/ELF hashes are in `long-soak.*` under the network evidence. The
  historical runner restored once; no future restoration gate remains.
- Added an optional extended backend close callback that preserves receive and
  transmit error flags/codes. The old single code could not distinguish MoQ
  CANCEL (zero) from clean closure. Tests cover zero-code resets and both halves;
  legacy callbacks remain available. This change postdates the completed soak's hardware
  binary, so that binary's memory figures do not cover the additional metadata.
- Added the bounded internal dispatcher and owned outbound queue. Host tests
  cover pinned-reference control messages, fragmented/interleaved streams,
  interrupted/duplicate SETUP, 10,000 group retirements, partial writes/FIN,
  control reservation, deadlines, reset backpressure and cancellation/ACK races.
  Linux ASan/UBSan passes. These components now feed the operational engine described below. The
  cross-task application service remains pending.
- Fresh public-dummy Ultra transport builds link the active TLS path. A CI job
  reproduces that configuration without private credentials. The native 12-case
  QUIC/security/SETUP matrix also passes after the close-callback change. Actual
  GitHub CI execution is not claimed.

## Operational engine checkpoint

Verified logs, source/object hashes and limitations are in the
[endpoint-engine evidence](implementation-evidence/2026-08-30-endpoint-engine/README.md).

`components/esp_moq/include/esp_moq/engine.h` and `src/engine.c` implement the
single-owner endpoint. Public behavior and limits are documented in the library's
`docs/endpoint-engine.md`. One broadcast/four tracks, discovery, track queries,
subscriptions, START/END/DROP, serving caps, retained publication groups and owned
receive assembly are operational. The core host arena is 23,408 bytes, with
203,328 bytes of media/cache storage and 67,808 bytes of TX storage accounted
separately. These figures exclude QUIC/TLS, application service, audio and UI.

The native C endpoint and public unmodified pinned `moq-net` APIs exchange eight
timestamped synthetic frames each way, an empty group and clean END. A separate
acknowledgement is published only after Rust validates the C publication. The
13-case native matrix and nine additional engine runs pass for DNS, IPv4 and IPv6.
Linux host sanitizers and Clang static analysis also pass. Host regression covers
partial writes, media before START, whole trailing groups after control closure,
zero-code cancellation, invalid roles and deadlines. A 10,000-position run with
1,000 separate DROP ranges does not accumulate history; 1,000 peer-subscription
lifetimes retain ID-reuse protection. None of this is Opus/audio interoperability.

Remaining engine hardening includes priority propagation, impairment/reconnect
stress, broader range fuzzing and explicit application media-epoch binding. The
internal callback still executes on the owner task and is borrowed until return;
it must not be passed directly to a UI or audio task. At this earlier checkpoint,
catalogs and the owned cross-task service were next, followed by real audio,
production auth/host service and full Ultra integration. The engine compiles for
ESP-IDF but has not run on the Ultra; the transport example does not invoke it.

## Catalog and Hang framing checkpoint

Added bounded audio catalog parsing/serialization, rendition selection, RFC 7396
merge patches and group-scoped raw DEFLATE. Both `catalog.json` and
`catalog.json.z` interoperate with the pinned reference. The vendored zlib 1.3.2
core is unmodified, licensed and checked by a 20-file hash manifest; its wrapper
uses fixed caller-owned arenas, without hot-path heap allocation. Public limits,
unsupported rendition policy and ownership are in the library's
`docs/hang-catalog.md`.

The oracle now runs 76 cases, including 66 catalog-sequence frames through the
actual pinned `moq-json` snapshot encoder/decoder. Rust decodes C-serialized and
C-compressed catalogs, and C reconstructs full JSON through peer patches while
preserving unknown fields. The native endpoint exchanges both catalog tracks,
three incoming snapshot/delta frames per track, and eight Hang timestamped
synthetic frames per direction. Matching container and outer timestamps are
checked; encoded payloads still are not real Opus audio.

Host regressions cover full-size frames, 5,120 incompressible DEFLATE frames with
window wrap/reset, decompression overflow, duplicate/escaped JSON keys,
Unicode, depth/field/node/merged-size limits, patch rollback, unsupported
renditions and per-byte snapshot mutations. The engine frame/cache limits were
separated to accept a bounded 4 KiB catalog plus compression/frame overhead:
4,112-byte frames, 4,140-byte encoded cached groups. Current host media/cache
storage is 204,608 bytes; the older engine evidence records its earlier size.
Catalog decoder storage is 95,256 bytes on macOS arm64 and 95,264 on Linux arm64,
including the inflater. These remain host measurements, not full-shell memory
budgets. Catalogs/engine compile for ESP-IDF but the transport-only watch example
does not invoke them.

One early native run terminated while receiving catalogs, with the Rust peer
reporting `dropped`; repeated subsequent tests did not reproduce it. This is
not classified as fixed and remains part of the lifecycle/reset hardening gate.
The earlier missing-track failure was a test-peer lifetime bug: its broadcast
holds weak track references, so completed catalog producers must stay alive.
The peer also maps application stream errors through the WebTransport range
for raw QUIC. The next service work must translate that pinned boundary while
preserving original diagnostics, including zero-code cancellation.

Evidence and exact verification counts are recorded in
[the catalog evidence](implementation-evidence/2026-08-30-hang-catalog/README.md).
No firmware was flashed or restored for this catalog checkpoint.

## Owned service and operational Ultra endpoint checkpoint

This subsequent checkpoint implements `esp_moq/service.h`, `esp_moq/native.h`
and the optional `esp_moq_endpoint` component. The portable service copies Opus
packet submissions, negotiates both catalogs, leases received packets and binds
capture/request/owner identity to explicit connection/group ranges. Cancel and
nonblocking PCM commit are linearized; held bytes remain stable until release.
Dedicated terminal slots survive queue pressure. Source restart cancels the old
catalog/media operation and resets decompression without accepting its late data.
An interval tracker and rolling progress deadline support a simulated five-minute
response; this is not physical five-minute Opus playback evidence.

The task endpoint copies configuration and authorization, separates DNS from the
single QUIC owner and rejects stale resolver completions. Explicit close rejects
old attempt IDs, invalidates media immediately and schedules bounded backend
cleanup. Destroy waits for live workers and consumer leases, then joins before
freeing. Platform DNS can outlast its logical deadline; the one bounded resolver
slot stays owned until the platform returns. There are no detached retry jobs.
Authorization/trusted-time leases and UTC rollback checks are implemented, but
the authenticated bootstrap that issues those leases is still missing.

Subscriber priorities now propagate from SUBSCRIBE/SUBSCRIBE_UPDATE into the
owned TX queue and adapter, including already copied unsent data. Control takes
precedence; equal priorities rotate and blocked streams are skipped. Abandoning
a publication removes its cache range and resets in-flight jobs without closing
the persistent track or affecting later groups. Native application errors are
mapped using the pinned WebTransport boundary; actual peer cancellation and
plain-catalog fallback pass.

Validation of the current implementation:

- macOS portable/driver tests pass; Linux GCC normal and ASan/UBSan suites pass
  for the portable service, actual threaded endpoint and actual adapter source.
  The driver fault test controls DNS/UTC and stubs the adapter; it is not TLS
  evidence. It covers DNS timeout/stale completion, copied configuration,
  close/reconnect, authorization expiry and UTC rollback. Expiry retains an
  error terminal outcome rather than masquerading as user cancellation.
- The final native 17-case matrix passed three times (51 cases), using the real
  ngtcp2/wolfSSL backend and pinned unmodified Rust endpoint. New service and
  task-endpoint modes use independent producer/consumer threads and validate
  both catalog formats, eight uploaded frames, receive range [5,13), both
  operation ends and zero outstanding leases. The public token is only a path
  fixture. Existing certificate/ALPN rejection cases also passed.
- The active public ESP-IDF build links the task-endpoint path. Enabling actual
  linkage exposed unavailable `nanosleep`; the target path now uses FreeRTOS
  delay. It is no longer sufficient to compile then discard the unused wrapper.
- Clang static analysis of the endpoint/service/TX source reports no diagnostics.
  Eight offline app-only runner checks pass, including abort/assert detection
  and no restoration after failures.

### Physical Ultra result

The connected ESP32-S3 Ultra ran the full service/task stack against the real
Rust peer. Both endpoints passed: catalogs negotiated, eight synthetic Hang
frames per direction were verified, and the receiver accepted [5,13). The peer
released the final response frame only after validating both C catalogs and all
uploaded frames. The complete short exchange took **3,884 ms**; this is not a
500 ms handshake or voice-latency success claim.

| Measurement | Bytes |
| --- | ---: |
| Internal RAM before endpoint creation | 266,739 |
| Minimum free internal RAM | 107,440 |
| Internal RAM at successful exchange | 172,383 |
| Internal RAM after joined destruction | 266,511 |
| PSRAM before creation / after destruction | 8,386,076 / 8,386,076 |
| PSRAM at successful exchange | 7,776,796 |
| Network / DNS stack watermarks | 7,392 / 2,868 |

RX/TX queue high-water marks were 7/1. Destruction returned success. The remaining
228-byte internal difference is not attributed yet; repeated create/destroy
measurement is still required before calling the endpoint leak-free on target.
No microphone, decoder, speaker or UI was active, so these values do not prove
full-shell headroom.

Two failed physical attempts preceded this pass and are retained privately:
service initialization rejected allocator alignment, then ESP-IDF's missing
pthread configuration cleared the saved output and caused an invalid restore.
The endpoint now explicitly requests `max_align_t` arena alignment and restores
the default pthread configuration when the getter reports NOT_FOUND. The final
image passed after those fixes; failed images were not restored to factory.

The app-only runner validated live identity/security/partition/OTA metadata and
wrote app0 only. **The passing new endpoint firmware remains installed.** No
factory backup, baseline comparison or restoration was performed. Private Wi-Fi
headers, images and full serial logs remain outside the repository. Sanitized
evidence and source hashes are under
[owned endpoint evidence](implementation-evidence/2026-08-30-owned-endpoint/README.md).

### Next requirements, still open

Codec/capture/playout, bounded jitter/PLC, decoder/DMA cancellation and a bounded
real reference audio echo now pass. Still implement authenticated
bootstrap, scope/time policy, credential refresh/reconnect and the production
Rust/Python host boundary. Then integrate the Ultra BSP and complete VoiceWatch
shell, preserving control/actions/packages and verifying real voice turns. The
90,000 operational groups per direction, 1,000 PTT turns, eight-hour idle,
impairment/reconnect and full-shell gates remain open. The earlier 90,000-stream
raw transport soak is not a substitute for those gates.

## Next work, without reducing the original objective

1. Integrate the verified Ultra audio BSP and capture/player owner with explicit
   PTT and the complete VoiceWatch shell. Bring up display/touch/buttons/haptics.
2. Extend the real-Opus exchange to long responses, repeated captures, source
   changes, network impairment and cancellation. The short codec exchange and
   earlier raw transport soak do not satisfy these operational audio gates.
3. Complete trusted time, authenticated bootstrap, scoped credentials, renewal,
   reconnection and the production host endpoint. The test token remains public
   test data, not production authorization. Target handshake latency remains open.
4. Bring up the Ultra audio/display/touch/power BSP and integrate the complete
   VoiceWatch shell, preserving PTT ownership, long responses, trusted actions
   and package delivery. Future device tests leave our firmware installed.
5. Run the full hardening/rollout matrix on the final release image and measure
   full-shell resource/latency headroom before making MoQ the default.

Useful local commands:

```sh
make -C libs/moq-esp32/tests/host test
make -C libs/moq-esp32/tests/adapter test
python3 libs/moq-esp32/tools/moq_reference_oracle/check.py
python3 libs/moq-esp32/tests/interop/build.py
python3 libs/moq-esp32/tests/interop/run.py --client libs/moq-esp32/tests/interop/build/client
# Use Linux for sanitizer validation; native --sanitize instruments dependencies too.
```

Active cross-build artifacts are under `/tmp/voicewatch-moq-active-build` and
`/tmp/moq-ultra-probe-build`; native artifacts are under `/tmp/moq-native-repro`.
Full hardware, operational relay, audio, and shell readiness remain unproven.
