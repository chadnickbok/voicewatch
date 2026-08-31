# Plan: make moq-esp32 ready to replace VoiceWatch WebRTC

Created 2026-08-30 against library revision `f827391b839ef60d4d197bf0bc5fa135a0fa594e`.

Inputs: [integration-readiness review](moq-integration-readiness.md), [technical review](moq-esp32-technical-review.md), and [reproductions](review-evidence/moq-esp32-f827391/README.md). This document defines the complete scope and acceptance gates against the reviewed baseline. It is not a completion report: consult [implementation progress](moq-implementation-progress.md) for implemented changes, measured results, and remaining work.

The recorded foundation includes build/core/adapter corrections, an executable reference oracle, native QUIC and SETUP interoperability, and a physical Ultra memory probe. These results do not establish operational publication/subscription, audio, production authentication, or full-shell readiness. Preserve those results as regression coverage; close each remaining gate below without repeating completed work unnecessarily.

## 1. Objective and scope

Deliver a bounded, interoperable native MoQ/Hang client that carries bidirectional Opus audio between the T-Watch Ultra and the existing VoiceWatch live-agent, without regressing the trusted shell or conversation lifecycle.

Use two explicit acceptance milestones:

- **Library candidate:** the Ultra publishes and consumes standard Hang/Opus through an unmodified pinned reference relay and peer, passes security and sustained-stream tests, and exposes a usable application API. This permits integration work, not automatic production cutover.
- **VoiceWatch replacement:** the complete Ultra shell, Wasm apps, control channel, live-agent audio, cancellation, reconnect, and package delivery pass together. This permits making MoQ the default for that board.

Keep these decisions stable during the first migration:

| Decision | Initial implementation |
| --- | --- |
| Protocol | Native QUIC v1/TLS 1.3, ALPN `moq-lite-05`, pinned moq-dev commit already in the compatibility lock |
| Media | Standard Hang legacy framing, microsecond timestamps with a matching track timescale, both plain and compressed catalog tracks |
| Codec | Existing VoiceWatch baseline: 16 kHz mono, 20 ms/320 samples, Opus VOIP, 24 kbps, complexity 0, VBR on, DTX/FEC off |
| Reliability | Reliable streams per group, one normal 20 ms audio packet per group; bounded late-group abandonment |
| User interaction | Explicit push-to-talk and half-duplex acoustic operation; bidirectional network capability remains available |
| Control | Retain bounded, versioned application messages separately from media; authenticated WSS for the non-local deployment profile |
| AI pipeline | Retain existing PCM-facing STT/model/tools/TTS, durable jobs, response journal, and app delivery |
| Ownership | Protocol/session/media transport belongs in the library; device identity, capture authorization, UI, app ownership, and action policy remain in VoiceWatch |
| Rollback | Separate WebRTC and MoQ firmware configurations; do not allocate both complete transports on the watch at once |

Do not include video, generic IETF draft coverage, a new AI stack, simultaneous acoustic barge-in/AEC, datagram audio, or QUIC migration in the initial release. Classify unsupported protocol operations explicitly and handle them as required by the pinned reference; omission must not become silent acceptance.

## 2. Work order and dependencies

| Stage | Deliverable | Dependencies | Exit evidence |
| --- | --- | --- | --- |
| 1 | Buildable active handshake and corrected portable-core edge cases | Reviewed baseline | Nonempty-config firmware links; defect tests pass with corrected expectations |
| 2 | Correct, testable QUIC adapter | Stage 1 | Real adapter lifecycle, scheduling, truncation, callback, and cleanup tests |
| 3 | Reference oracle plus early Ultra transport feasibility | Oracle can start alongside 1–2; hardware needs 1–2 | Reproducible reference fixtures; secure handshake; sustained raw-QUIC stream turnover and measured memory |
| 4 | Operational bounded MoQ publisher/subscriber | Stages 2–3 | Bidirectional protocol exchanges and control lifecycles against the reference |
| 5 | Hang audio and device audio integration | Stage 4 and oracle; board work can start earlier | Catalog interoperability, paced audio, discontinuities, loss recovery, hardware echo |
| 6 | Secure reconnectable service and library release candidate | Stages 4–5; credential design starts in 1 | Auth/renewal/isolation and soak gates; reference peer and production host adapter |
| 7 | Full VoiceWatch Ultra integration | Library candidate; Ultra BSP and simulator work may proceed earlier | Full-shell voice and control parity, installed-app and recovery tests |
| 8 | Hardening, evidence, and controlled default switch | Stage 7 | Acceptance matrix passes on release binaries; rollback verified |

The highest-risk dependency is Stage 3: QUIC stream turnover and memory on the target hardware. Do not complete a large application integration before proving it. The oracle and Ultra BSP can progress independently, but protocol expansion must use the oracle and the board must be validated before flashing a full-shell image.

## 3. Stage 1 — repair the build and portable-core defects

### 3.1 Fix the real wolfSSL configuration (R1)

Files: `components/esp_moq_transport_ngtcp2/CMakeLists.txt`, its configuration headers, `examples/esp32s3_relay_handshake/`, and `.github/workflows/esp-idf-build.yml` inside the library.

1. Add a CI configuration with nonempty **dummy** JWT/SSID/password, ensuring the complete connection path is referenced and linked. Keep empty-config behavior as a separate diagnostic test.
2. Establish one authoritative wolfSSL configuration shared by the backend and managed component. Avoid modifying downloaded managed-component files by hand.
3. Resolve the `HAVE_SESSION_TICKET`/`NO_SESSION_CACHE` conflict. First establish a valid full-handshake configuration without resumption; enable a bounded, explicit ticket/cache profile only after its build and memory costs are known. If the pinned backend requires ticket symbols even without application resumption, provide the smallest supported consistent cache configuration.
4. Confirm the missing symbols are resolved and inspect the ELF/map for the actual adapter/TLS entry points. Do not use binary size alone as proof that the path linked.
5. Record full dependency revisions, compile definitions, binary/ELF/map hashes, static size, and compiler versions. Keep credentials and credential-bearing binaries out of published CI artifacts.

**Exit:** clean-checkout active-path builds pass on ESP-IDF 5.5.5. Certificate, hostname, and ALPN validation remain enabled. No undefined cache symbols, weak dummy implementations, or disabled validation are used to satisfy the linker.

### 3.2 Preserve state on rejected operations (R8)

Files: `components/esp_moq/src/client.c`, `session.c`, and host tests.

Validate `client_start` before clearing any state. Preserve the current public behavior of rejecting invalid starts, but make rejection non-mutating. Cover duplicate calls in CONNECTING, NEGOTIATING, READY, CLOSING, and FAILED, including a partially written SETUP and blocked FIN. Verify a rejected call leaves byte offsets, stream identity, and subsequent completion unchanged.

### 3.3 Reject overflowing optional bounds (R9)

File: `components/esp_moq/src/lite05_control.c`.

Validate a present optional value before adding its presence offset: values at or above the allowed maximum must fail. Test start and end independently at zero, largest legal value, first illegal value, QUIC varint maximum, and `UINT64_MAX`. Check both returned status and decoded semantics; ensure a rejected encoding does not appear accepted to the caller.

### 3.4 Make test claims precise

Promote the review reproductions into maintained regression tests. Their assertions must describe the **correct** behavior, rather than continue passing when the bug is present. Preserve the existing host suite and run ASan/UBSan in Linux CI with bounded job timeouts; the previous macOS sanitizer stall is neither a passing result nor proof of a code defect.

Update README status to distinguish codec/parser support, SETUP support, and operational publication/subscription. Resolve the root/backend license-document inconsistency and record the chosen distribution policy before release, without blocking unrelated technical fixes.

## 4. Stage 2 — make the QUIC adapter correct under load

### 4.1 Establish a testable boundary

Introduce a small platform seam for socket I/O, clock, and wakeups. Compile the **actual adapter implementation** into host tests where practical; do not rely indefinitely on copied/extracted functions from the review. Use deterministic mocks for rare states and a real pinned ngtcp2/TLS peer for integrated behavior.

One networking task owns each connection and its protocol state. Internal parser work may execute there, but public application callbacks must be delivered from an application event-consumer context. No UI, Opus decoding, filesystem, NVS, or blocking application work runs inside ngtcp2 callbacks.

### 4.2 Correct callback and buffer ownership first (R6)

- Split enqueueing outbound bytes from pumping the QUIC writer. `write`, FIN, reset, and close requests must not recursively invoke packet generation from ngtcp2 callbacks.
- Buffer deferred stream events in a bounded multi-stream structure; replace the initial single-stream-only staging assumption.
- Copy transient input before deferring it, or hold an explicitly owned fixed-pool buffer until released. Never retain a borrowed callback pointer.
- Defer FIN/reset/close events in order behind already accepted bytes for that stream. Add a connection-generation tag so events from a retired connection cannot reach its replacement.
- Define queue exhaustion: media may be abandoned according to a documented deadline policy, but essential control and close events must be reserved or trigger a visible bounded failure. Do not silently drop them.
- Ensure shutdown waits for in-flight internal callbacks and cancels application deliveries before freeing state. No user callback may occur after destroy completes.

**Tests:** callbacks immediately request reply/reset/close; nested ngtcp2 writes never occur. Simultaneous input on multiple streams is retained correctly; forced queue exhaustion cannot corrupt memory or resurrect a closed session.

### 4.3 Register peer streams and reclaim them correctly (R2, R3)

- Track initiator, direction, read/write half-state, reset state, and credit-return status.
- Register a send half for peer-initiated bidirectional streams, allowing a reply on that same stream. Reject writes to peer-initiated unidirectional streams.
- Reserve bounded resources before accepting/creating application stream work. Avoid consuming new QUIC stream IDs repeatedly when the local pool is already full.
- Return peer stream credit exactly once when a stream is retired, separately for uni/bidi streams and respecting ngtcp2's special auto-accounting cases. Never return local stream credit as though it were peer credit.
- Maintain a cap on **concurrent** streams while allowing the total lifetime stream count to increase. FIN on one half of a bidirectional stream does not retire its other half prematurely.
- Retain submitted bytes until ACK or the backend's definitive release event. Handle partial ACKs, reset races, and close without double-freeing or leaking pool blocks.

**Tests:** reply to incoming TRACK/SUBSCRIBE streams; complete far more streams than the initial 16/4 limits; FIN/reset ordering; peer STOP_SENDING; lost reset acknowledgements; repeated close callbacks; exhaustion followed by recovery. Instrument credit conservation and buffer accounting as invariants.

### 4.4 Schedule around blocked and obsolete work (R4)

- Iterate a bounded set of eligible streams fairly within each pump. A stream-level block must not end the whole pass.
- Record flow-control-blocked streams and retry when input can make them writable; avoid spinning on them.
- Retire or suppress unsent work for closed/reset send halves. Do not reinterpret `STREAM_NOT_FOUND` as indefinitely retryable work.
- Reserve capacity for control and connection-level packets. Apply media deadlines and newest-useful-group priority without starving control or ignoring QUIC congestion control.
- Bound packet count and CPU time per pump. Bound receive draining too, so continuous traffic cannot prevent timers, commands, or the shell from progressing.

**Tests:** block an old group while newer groups and control progress; delay ACKs; saturate the TX pool; cancel before/after partial send; exercise congestion and stream flow control separately. Successful enqueue is not reported as remote delivery.

### 4.5 Match receive limits to storage (R5)

- Set the advertised QUIC receive datagram maximum explicitly to the supported receive-buffer size, initially 1,350 bytes, and validate it against protocol minimums.
- Detect UDP truncation using supported lwIP receive semantics, or a tested platform fallback. Discard an oversized datagram intact rather than parsing a prefix as a complete packet.
- Distinguish outgoing MTU policy from incoming capability. Handle a peer's probes and larger datagrams without memory errors or accidental partial parsing.
- Treat transient socket errors separately from terminal failure. Test receive, write, pending-datagram retry, and connection-close behavior under `EAGAIN`.

**Exit:** all R2–R6 regression cases pass against the real adapter code, backed by selected real-QUIC tests. Teardown releases all owned resources and delivers one terminal application outcome.

## 5. Stage 3 — prove the reference contract and hardware feasibility

### 5.1 Build the executable reference oracle

Implement `tools/moq_reference_oracle/` against the exact moq-dev commit in `compatibility/moq-dev.lock.json`. Add a Rust lockfile and fixture-provenance manifest recording generator version, upstream SHA, inputs, and expected bytes/events.

Generate and verify SETUP, announcement, TRACK, SUBSCRIBE, supported updates/termination, groups, frame timestamps, unknown parameters, resets, and rejection cases. Include plain/compressed catalogs, normal Opus frames, empty-group discontinuities, and terminal flushing. Cross-check C-produced bytes through the Rust decoder and Rust-produced bytes through the C decoder; same-implementation roundtrips alone are insufficient.

Fragment each message at every byte boundary and test multiple messages in one input batch. Add parser fuzzing seeded by valid reference fixtures, with explicit time/allocation bounds. A small instrumented oracle may expose upstream serializers, but a separate unmodified reference peer must be used for interoperability acceptance.

Treat a reference mismatch as a compatibility failure. Do not patch the relay to accept a private format. Upstream-main drift checks may be advisory; pinned conformance must be required.

### 5.2 Run an early Ultra transport gate

Before every flash, verify board identity, port, security state and the intended partition layout. The user has explicitly authorized replacing the Ultra's default firmware: we are building new firmware from the ground up, and future tests leave it installed. Preserving or restoring the default firmware, taking another baseline backup, and comparing the whole flash against the factory image are not test requirements. Existing private backups may remain as optional historical artifacts. Never erase package/user data as incidental bring-up cleanup; deliberate layout changes belong in the new firmware's documented flash procedure.

Add a separate Ultra example profile with verified quad-PSRAM configuration and safe power behavior. The generic ESP32-S3 octal profile remains a different target. Establish a plausible wall clock before TLS, with an explicit trusted-time policy and bounded synchronization; never bypass certificate checks to get a handshake.

Use two distinct tests:

1. **Real relay handshake:** verify TLS/ALPN and native authenticated SETUP against the intended deployment and pinned-compatible relay. Include invalid certificate/hostname/ALPN cases against a controlled test endpoint.
2. **Raw-QUIC stress peer:** open/complete/reset streams at 50 per second in each direction for 30 minutes, plus long-lived bidirectional control streams. This isolates transport turnover before full MoQ exists. It is not called MoQ interoperability evidence.

Record internal free/minimum/largest block, PSRAM, task-stack watermarks, adapter pool use, backend allocations, loop latency, packet counts, RTT/loss, stream credits, and reset causes. Repeat failed/expired handshakes and reconnect cleanup.

**Exit:** valid active firmware, successful verified handshake, at least 90,000 synthetic streams per direction without credit exhaustion/leaks, and measured resource headroom sufficient to proceed. If QUIC cannot meet that gate, fix its allocator/configuration/architecture before expanding into VoiceWatch.

## 6. Stage 4 — complete the MoQ client and freeze the application contract

### 6.1 Implement the missing protocol runtime (R7)

Build on the existing portable codecs rather than replacing them. Add a bounded stream registry and dispatcher that validates stream initiator/direction/type, routes input to the correct incremental parser, and associates groups with subscription and connection incarnations.

Implement both sides of the selected role:

- Serve local broadcasts, announcement changes, track information, and incoming subscriptions.
- Discover authorized remote broadcasts, request tracks, subscribe, process START/END/DROP, and unsubscribe/update according to the pinned protocol.
- Create and finish outgoing groups/frames with partial-write support, priorities, and deadlines.
- Reject unsupported operations with the reference-defined error behavior; handle GOAWAY and terminal failures without leaving work live.
- On malformed input, distinguish a stream failure from a connection failure and centralize the resulting state transition.

Size local and peer subscription tables separately. The present four-entry table is not assumed to cover both directions, both catalog variants, media, and all control work. Publish a resource accounting table and explicit maximums derived from the implemented topology.

### 6.2 Proposed public API and guarantees

These are proposed capabilities, not names of existing implemented functions. Prefer opaque handles and typed events so the production application does not depend on public struct layout or QUIC stream IDs.

| Capability | Required contract |
| --- | --- |
| Create/configure | Validate bounds; own copies of mutable names, paths, and credentials; explicitly document lifetime of static CA data and callback context |
| Connect/session events | Asynchronous start; separate transport connected, MoQ session ready, publication available, and subscription/media ready |
| Publish/subscribe audio | Typed format and broadcast identity; complete catalog negotiation; explicit readiness/failure/ended events |
| Submit Opus frame | All-or-nothing application acceptance into a bounded owned pool; caller may reuse its input on success; `WOULD_BLOCK` means no acceptance |
| Receive frame | Complete validated timestamped encoded packet with owned/leased lifetime and connection/media generation; release explicitly |
| Discontinuity/end | Separate timeline reset, end of publication, and completion of local playback; use standard reference media semantics |
| Cancel | Invalidate the specified operation/generation, drop pending media, and stop/reset associated work; never affect a replacement operation |
| Close/destroy | Idempotent close, bounded cleanup, no callbacks after destruction, exactly one terminal outcome per operation |
| Statistics | Snapshot counters and high-water marks without exposing secrets or requiring UI work on the network task |

The portable engine should remain host-testable and single-owner. A thin ESP-IDF service wrapper owns its task, command queue, and application event queue. Public cross-task calls enqueue work; they do not access QUIC state directly. Tests can drive the engine deterministically through an internal step interface.

Use connection generations and non-reused subscription IDs. Define media epochs in the application-control contract, binding capture/response IDs to subscription incarnations and group boundaries. A fresh local counter alone cannot distinguish an old network frame on the same track: require explicit media-range binding and test cross-channel reordering before calling cancellation safe. Keep these identities out of proprietary Opus payload headers.

Define independent deadlines for DNS, handshake, SETUP, discovery, track response, subscription start, queued media, and close. Resolve DNS outside the time-critical owner loop and discard stale resolver completions by connection generation.

**Exit:** bidirectional MoQ operations pass generated fixtures and an unmodified reference peer. No production caller assembles protocol bytes or manually dispatches stream IDs. Public error and cancellation behavior is documented and tested.

## 7. Stage 5 — implement Hang audio and device playback

### 7.1 Catalog and framing correctness

Add catalog publication, bounded parsing, rendition selection, and live updates. Support both catalog tracks with the exact reference compression scope and raw-DEFLATE format. Cap compressed input, decompressed output, nesting, field counts, and names independently; handle invalid compression and unsupported renditions visibly.

Track separate limits for an Opus packet, Hang frame payload, MoQ frame, catalog, and UDP datagram. An Opus-packet cap does not include its Hang timestamp or outer transport framing. Start with a 1,275-byte single-frame Opus packet limit and a 4 KiB decoded catalog limit, validating all profile limits against reference fixtures.

Use a matching microsecond outer track timescale and Hang timestamp timebase. Preserve signed timestamp-delta handling; do not reuse the old esp_peer frame-sequence PTS convention. Generate monotonic media timestamps from capture cadence, not from the count of successfully transmitted frames, so drops do not compress time.

Implement empty-group discontinuities and terminal padding/flush exactly as the pinned reference specifies. Normal group FIN, end of a subscription, end of synthesis, and speaker drain are different events. Never feed an empty terminal marker to Opus as ordinary audio or treat every group FIN as an utterance boundary.

### 7.2 Audio scheduling and recovery

Keep the current Opus settings for the first comparison. Reuse the known ESP-IDF codec component through the optional audio layer; the transport core continues to accept encoded frames and has no dependency on microphone, speaker, or UI.

Implement bounded packet assembly, jitter scheduling, stale/duplicate group rejection, decoder reset on discontinuity, and PLC through the codec's supported recovery API. FEC and DTX can be separately selectable, tested improvements after baseline parity; do not silently enable them during the transport cutover.

Initial tuning targets: 60–80 ms playback prebuffer, maximum 200 ms of device playback buffering, and a 200 ms live-media staleness target. Separate the bounded device queue from the existing host response spool: a five-minute response must be paced over time, not loaded onto the watch or truncated to the jitter-buffer length.

Preserve intentional speech pauses and time gaps. Under congestion, count late-media abandonment and concealment explicitly; reliable retransmission does not mean expired audio should still play. Test warmup, final partial frame, long pauses, decoder errors, and an immediate new turn after cancellation.

### 7.3 Minimal Ultra audio example

Implement verified Ultra microphone, speaker, button, power, and haptic support behind a BSP. Reuse a common board layer between the library example and VoiceWatch where practical; do not maintain divergent pin/rail definitions. Keep charging and PMIC settings specific to the Ultra.

The device example must connect without capturing, begin capture only on an explicit PTT action, stop the microphone before playback, and cancel promptly. No automatic recording on reconnect. Add loopback/tone diagnostics independent of the network to isolate hardware faults.

**Exit:** reference-produced audio plays correctly on the Ultra; the reference decodes its publications; repeated PTT echo completes without stale audio or decoder state. Obtain a captured waveform and a reference decode/transcript for at least a defined speech fixture, not just packet counters.

## 8. Stage 6 — secure sessions, host endpoint, and library candidate

### 8.1 Authentication and recovery

Keep production credentials outside static firmware configuration. An authenticated HTTPS bootstrap supplies the relay location, authorized namespace, expiration, and short-lived token. The watch never stores a relay signing key. Pin and test the actual issuer/relay scope behavior rather than assuming an arbitrary JWT layout is accepted.

Bind device identity and session namespace to the authenticated control connection. Give watch and agent inverse publish/subscribe privileges, narrowly scoped to their directions. Test unauthorized cross-device discovery, publication, subscription, expired tokens, and server replacement. Do not fall back to anonymous access on failure.

Redact query strings and tokens from logs, errors, crash diagnostics, and telemetry. Define root rotation and trusted-time policy. Withhold connection when time cannot satisfy certificate validation; SNTP availability alone is not a claim of authenticated time. Test cold boot, bad RTC values, clock rollback, unknown CA, expired/not-yet-valid certificates, hostname mismatch, and ALPN mismatch.

Use bounded exponential reconnect backoff with jitter and an upper limit. Reset it only after a meaningful stable connection. Refresh credentials before expiry, reestablish publication/subscription deliberately, and clear stale audio on every replacement. Surface terminal auth errors separately from transient network loss.

Add configurable warm-idle keepalive and eventual close/power policy. TLS session resumption must be measured and bounded before enabling by default. Do not send microphone audio or replayable control actions as zero-RTT data.

### 8.2 Reference and production host peers

Implement `server/echo_oracle/` in Rust using the pinned reference libraries. Exercise decode/re-encode and recorded fixtures, not just opaque byte reflection. Preserve the unmodified relay as the interoperability boundary.

Add the production watch-facing MoQ endpoint using the same reference libraries. A small supervised Rust endpoint with bounded local IPC to Python is the preferred starting point; this process is the MoQ endpoint, not a network gateway translating proprietary watch traffic. Confirm binding availability before electing to replace it with an in-process Python binding.

Preserve a PCM16k interface to the existing conversation system. Isolate devices, captures, responses, and replacement sessions. Keep the long-response spool and pacing semantics from `DownlinkAudioTrack`, but extract them from its aiortc inheritance so both test implementations can share the behavior.

Define explicit methods for begin/enqueue/end/clear/wait-for-playback. `end` flushes the encoder once and commits a terminal media boundary. `clear` interrupts synthesis and pending output. Waiting for playback must account for watch playout, with bounded generation-bound completion/progress messages if needed; a drained server queue is not proof that the watch speaker finished. IPC disconnection must cancel the associated stream, not leave speech running.

**Library candidate gate:** repeatable two-way reference audio, secure reconnect, cancellation, controlled loss, and memory/stream-turnover evidence pass on the Ultra example. Publish a versioned API, compatibility manifest, build/run instructions, and limitations. VoiceWatch may then integrate against that pinned candidate.

## 9. Stage 7 — integrate the full VoiceWatch Ultra shell

### 9.1 Add and verify the Ultra board target

Add `DOODAD_BOARD_TWATCH_ULTRA`, a dedicated board source/profile, and build/flash/monitor support. Implement the verified AMOLED, touch, power/expander, PDM/I2S, battery, and haptic behavior through the existing `board.hpp` abstraction.

Validate the 410×502 shell against the existing simulator profile, including layout dimensions, flush alignment, color order, touch mapping, rotation, brightness, safe areas, sleep/wake, and PTT. Audit 240×240 guest surfaces separately; preserve the app ABI or define a deliberate transform rather than changing every guest implicitly.

Capture a shell-only baseline before media: Home/watch face, launcher, installed app, Agents, control center, Voice Orb, and recovery. Measure peak display memory and animation load, not only idle boot.

### 9.2 Replace the firmware media seam

Keep `voice_service.hpp` and its app/host ownership semantics stable. Extract media-specific work from the 2,500-line service into a small interface with one selected implementation per firmware build. Suggested internal files are `voice_media_transport.hpp`, `voice_media_webrtc.cpp`, and `voice_media_moq.cpp`; names are implementation choices, not new public ABI commitments.

The MoQ implementation must preserve capture IDs and request IDs, owner-token isolation, playback generations, explicit microphone authorization, and UI-queue delivery. Do not copy action/NVS/package behavior into the library or call LVGL from its worker.

Retain current application control messages while removing SDP/ICE from the MoQ path. Advertise the selected media transport explicitly during authenticated session setup; do not infer support from a connected control WebSocket. Readiness must reflect the required media direction being usable.

Make `esp_peer` and its WebRTC-only TLS settings conditional. Keep `esp_audio_codec`, WebSocket control, and other shared dependencies as needed. Measure the combined memory cost of wolfSSL QUIC and any retained mbedTLS HTTPS/WSS clients; do not assume removing WebRTC removes all other TLS use.

### 9.3 Switch the live-agent adapter without rewriting the product

Introduce a transport-neutral session interface around the existing `WatchSession` behavior. Preserve multi-device isolation, replacement-session cleanup, `DownlinkUtteranceBinding`, action-result futures, `agent.state`, response journaling, and personal-app delivery.

Make aiortc an optional WebRTC/test dependency, with conditional imports and no SDP/RTP requirement in the MoQ production path. Continue running existing transport/conversation/control/app-delivery tests and add equivalent MoQ contract tests. Update the supervised Mac service packaging to include the endpoint binary, trust/configuration, and restart behavior.

**Exit:** the complete physical shell performs a real voice turn through existing STT/model/tools/TTS, plays the entire response, cancels cleanly, reconnects without recording, and retains working navigation and installed-app behavior.

## 10. Stage 8 — hardening and acceptance matrix

These thresholds are proposed acceptance targets, not measurements. Freeze their exact harness definitions after Stage 3; changes require a documented engineering decision, not silent relaxation to get a pass.

| Area | Required test and pass condition |
| --- | --- |
| Builds | Clean checkout, pinned submodules/dependencies, real nonempty-config firmware; default and negative configuration tests; recorded release binary hashes |
| Protocol | Reproducible generated fixtures and both-direction reference decode; real standard relay/peer interoperability; no private framing or patched relay |
| Stream turnover | 30 minutes at 50 audio groups/s **each direction**, at least 90,000 groups/direction plus catalogs/control; use synthetic encoded traffic for simultaneous network load without requiring acoustic full duplex |
| Cycles/soak | 1,000 PTT/echo cycles plus eight-hour idle/reconnect run; no leaked slots, cumulative heap loss, watchdogs, crashes, or stale playback |
| Loss/reordering | 0%, 1%, 3%, 5% induced packet loss at 30/60/120 ms RTT; also burst loss, delayed old groups, reorder, duplicates, and one stream deliberately flow-control-blocked |
| Audio | Known speech/tone fixtures decode correctly; complete long response and final tail; expected timing and duration; impairment produces counted concealment/drops rather than stalled control or unbounded buffering |
| Latency | Warm ready-session PTT-to-first-published-frame p95 <100 ms; first complete usable received frame to speaker start p95 <250 ms; local cancel-to-silence target <100 ms |
| Connection latency | With Wi-Fi associated and credentials cached, initial QUIC+SETUP p95 target <500 ms; report media-ready separately and account for relay distance; first network provisioning measured separately |
| Recovery | After an injected network restoration, usable media within 10 seconds when the configured relay/auth services are reachable; terminal auth failures visible instead of endless retries |
| Security | Certificate/hostname/ALPN and identity/scope negative tests reject correctly; token expiry/rotation and clock faults; no credential logs or anonymous fallback |
| Memory | Start with 96 KiB minimum free internal SRAM and 32 KiB largest free internal block under full-shell stress; record every pool and stack high-water mark; re-budget explicitly if hardware feasibility disproves the target |
| Allocation | No general-heap allocation in capture/encode/application packet handoff after warmup; backend allocation/reassembly is instrumented, bounded, and fails gracefully; no blanket zero-allocation claim based only on the TX pool |
| UI | Compare shell-only and voice-active frame/flush timing and input latency; predeclare the accepted regression budget after baseline capture; no new long UI stalls under network load |
| Lifecycle | Cancel during partial send, decoder work, synthesis, and final drain; owner/app switch and replaced connection cannot receive old completions; disconnect never starts capture |
| Full product | Watch face/launcher/app/Agents/Voice Orb, trusted actions, package download/install/launch and rollback work during/after media sessions; persistent data survives firmware rollback |

At the impaired-network cells, reliability means bounded degradation and successful recovery, not an impossible promise of zero audible loss. Set a reference speech-quality threshold from measured baseline fixtures before accepting the audio candidate; packet counts alone are not an audio-quality metric. Under no induced impairment, no unexplained media loss, missing response tail, or underflow is acceptable after warmup.

Speech-fixture policy v1 is now frozen from the measured six-word baseline:
“Please read my next exercise set.” requires zero word errors without induced
impairment, or at most one word error with impairment while retaining the
contiguous ordered target “next exercise set.” Every admitted current-turn
completion must pass, alongside the existing fresh watch-state read, full
playback and lifecycle gates. See the [policy and before/after evidence](implementation-evidence/2026-08-31-capture-plc-quality/README.md).
This narrow fixture score does not establish general intelligibility, close
the full impairment matrix, or waive unexplained protocol loss. Later changes
to the policy require a recorded engineering decision before rerunning acceptance.

Measure both incremental library resources and the complete firmware peak. Replace the contradictory 80/96 KiB design targets with one documented budget after feasibility. Use bounded allocator hooks/slabs where the selected backends support them; otherwise instrument actual behavior and resolve any inability to enforce the intended limit before claiming a resource-bounded release.

Store sanitized serial logs, metrics, fixture hashes, packet captures when appropriate, waveform results, and screen captures with firmware/library/reference SHAs. Declare which tests used a synthetic peer, a local relay, the actual deployment relay, and physical hardware.

## 11. Defect closure and suggested change sets

| Review item | Fix location/work | Required closure test |
| --- | --- | --- |
| R1 configured link failure | Stage 1 TLS settings and active-path CI | Actual populated-config ELF links and contains handshake path |
| R2 peer bidi replies | Stage 2 stream registry/send halves | Reply on the original peer stream; control request/response interop |
| R3 credit exhaustion | Stage 2 per-stream retirement accounting | Concurrent cap preserved across 90,000+ remote groups |
| R4 blocked-stream starvation | Stage 2 eligible-stream scheduler | New media/control progresses while old stream blocks/resets |
| R5 receive mismatch | Stage 2 negotiated datagram bound and truncation handling | Oversize/probe input never parsed as a complete truncated datagram |
| R6 callback reentrancy | Stage 2 deferred operations/events | No ngtcp2 writer inside a callback; safe immediate reply/reset/close requests |
| R7 missing operational client | Stages 4–6 | Real catalog discovery and two-way audio through reference relay |
| R8 duplicate-start corruption | Stage 1 non-mutating error path | Partially sent/blocked SETUP survives rejected duplicate start |
| R9 optional-bound overflow | Stage 1 strict checked encoding | All illegal start/end bounds reject without semantic change |
| Readiness gaps | Stages 3, 5–8 | Time/auth, oracle, audio/BSP/server, resource limits, diagnostics and full-shell evidence |

Keep changes independently reviewable. A reasonable merge sequence is: (1) active build/config fix, (2) core state/range regressions, (3) adapter test seam and deferred callbacks, (4) stream lifetime/credits, (5) scheduling and datagram limits, (6) oracle/fixtures, (7) Ultra transport feasibility and metrics, (8) operational pub/sub client, (9) Hang catalog/framing, (10) audio/BSP/reference echo, (11) credential/reconnect lifecycle and host endpoint, (12) VoiceWatch media seam and Ultra shell, (13) hardening/default-switch evidence. Oracle and board changes may be developed earlier when independent, but their dependent gates still apply.

## 12. Rollout, estimates, and stop conditions

Pin the library candidate in VoiceWatch reproducibly, preferably using a Git submodule entry rather than relying on an untracked nested checkout. Keep lockfiles/configuration for both applications explicit. Retain a known-good WebRTC build for each board that already supports it. The Ultra is a new firmware target; retaining/restoring its default image is not required. Product update/rollback tests concern our own firmware releases and persistent data, not a return to the manufacturer's image. Do not assume the older S3 WebRTC image is valid Ultra firmware.

First enable MoQ only in the Ultra development build. After Stage 8 passes, switch that board's default and remove esp_peer from its linked image. Keep legacy WebRTC support for other boards separately until they pass their own gate. Retire the fallback only after an agreed field/soak period with recorded recovery performance, not merely after the first successful conversation.

Planning allowance for one engineer familiar with this codebase: roughly **8–12 engineer-weeks to a well-tested library candidate**, with **2–4 additional weeks for full VoiceWatch integration and cutover evidence**. These are broad allowances, not delivery commitments; existing audio reuse and parallel board/oracle work may shorten elapsed time, while QUIC resource or interoperability problems may expand it. Re-estimate after the active build and Stage 3 hardware gate, using measured results.

Stop expansion and address the underlying problem if the target cannot sustain stream turnover, the full-shell memory budget cannot be met, reference interoperability requires a private protocol, or certificate/authentication checks cannot remain enabled. Keep working on the candidate or explicitly revisit the transport architecture; do not conceal those failures behind a successful smoke build, gateway, reduced shell, or disabled validation.

The first implementation batch should be the active-path build, the two portable-core fixes, and the adapter test/deferred-callback seam. That provides a trustworthy build and test foundation for every later stage.
