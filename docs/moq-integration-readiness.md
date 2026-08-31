# moq-esp32 integration readiness for VoiceWatch

Reviewed 2026-08-30. Library revision: `f827391` (merged `main`). VoiceWatch's application firmware and service live under `doodad-runtime/`.

## Verdict

**Do not replace VoiceWatch's WebRTC transport with this revision.** The library is a useful early implementation of wire codecs, parsers, session bookkeeping, and a QUIC adapter. It is not yet an operational bidirectional MoQ/Hang audio client.

There are two separate reasons:

1. Essential publication, subscription, catalog, audio, and reconnect integration is missing.
2. Existing transport code has defects that block a usable firmware build and sustained multi-stream operation.

It is suitable for continued library development and host-side conformance work. Hardware handshake testing becomes reasonable after fixing the configured build and board/time prerequisites. It is not ready for a VoiceWatch media cutover or a claim of authenticated relay interoperability.

This review made no changes to production source and flashed no firmware. The ngtcp2 submodules and ignored build dependencies were initialized to verify the actual ESP-IDF build. Review evidence is under `docs/review-evidence/moq-esp32-f827391/`. The companion `moq-esp32-technical-review.md` contains the independent subagent's architecture and portable-core review.

## What exists today

| Area | Actual implementation | Readiness |
| --- | --- | --- |
| Compatibility pin | Exact moq-dev revision `eb5776e21eeaecba8e844be53c821895c178bcaf`, ALPN `moq-lite-05`, Hang legacy/Opus | Good foundation; does not itself prove interoperability |
| Wire primitives | Bounded readers/writers, varints, signed deltas, UTF-8/path checks | Host-testable |
| MoQ messages | SETUP, announcement, track, subscription, group/frame codecs | Useful building blocks, not complete network operations |
| Incremental receive | TRACK, SUBSCRIBE response, GROUP parsing | Caller must route streams, own buffers, and dispatch events |
| Session state | Setup lifecycle and bounded subscription bookkeeping | Not a complete live pub/sub client |
| Live client driver | Sends local SETUP, handles partial writes/FIN, receives peer SETUP | Stops at negotiation |
| QUIC backend | UDP/ngtcp2/wolfSSL, TLS configuration, flow-control byte updates, retained TX buffers | Real code; build and lifecycle blockers below |
| Hang | Legacy timestamp-plus-payload encode/decode | No catalog publisher/parser or rendition selection |
| Audio | No Opus/jitter/FEC/PLC implementation in `esp_moq_audio` | Missing; VoiceWatch can supply/reuse some of this |
| Watch support | Ultra directory and voice example are README placeholders | Missing |
| Server and oracle | Descriptions of intended programs | No runnable echo peer, AI adapter, or fixture generator |
| Evidence | Portable tests, smoke and handshake source | No checked-in live-relay, audio, loss, or hardware-soak results |

The root README's statement that no production protocol code exists is stale after the merge. Conversely, its claim that concurrent publication/subscription is supported is not demonstrated by the current client API. Code is the authority for this assessment.

## Blocking findings

### R1 — The configured handshake firmware fails to link

**Severity: blocks the first real firmware build. Evidence: reproduced.**

Building `examples/esp32s3_relay_handshake` with ESP-IDF 5.5.5 and its default empty JWT succeeds and emits a `0x30da0`-byte application. However, `main/main.c:146` immediately returns for an empty compile-time JWT. Compiler optimization and linker garbage collection remove the unused handshake path.

Rebuilding with nonempty, clearly invalid test strings for the JWT, SSID, and password activates that path and fails with:

```text
undefined reference to `TlsSessionCacheGetAndRdLock'
undefined reference to `TlsSessionCacheUnlockRow'
collect2: error: ld returned 1 exit status
```

The adapter's `CMakeLists.txt:85` enables `HAVE_SESSION_TICKET`. The pinned managed wolfSSL configuration defines `NO_SESSION_CACHE` in `include/user_settings.h:371`; the cache definitions in `src/ssl_sess.c` are conditional on that setting, while the linked ticket code references them. The current combined configuration is not link-complete.

**Required correction:** choose a consistent wolfSSL session-ticket/cache configuration, rebuild the active handshake path, and add that build to CI using dummy nonempty configuration. A small explicit cache or a deliberate prototype without ticket support should be evaluated; do not resolve this by disabling certificate validation. The review did not modify the configuration to claim a fix.

Evidence: `review-evidence/moq-esp32-f827391/configured-build-result.txt`. Full local log: `/tmp/voicewatch-moq-review-f827391/full-build.log`.

### R2 — Incoming bidirectional streams cannot be answered

**Severity: blocks serving TRACK/SUBSCRIBE and publication. Evidence: source trace and isolated reproduction.**

`esp_moq_ngtcp2.c:578` allocates a TX record only for locally opened streams. `receive_stream_data_cb` does not register remotely opened bidirectional streams. `transport_write` at lines 617–619 requires that TX record and rejects an incoming stream with `ESP_MOQ_ERR_INVALID_STATE`.

MoQ control requests arrive on bidirectional streams; a publisher must answer on the reverse direction of the same stream. Opening a separate local stream is not an equivalent protocol response.

An isolated harness using the adapter's extracted functions delivered data on server-initiated stream 1, then attempted a reply. Result: `INVALID_STATE`, zero accepted bytes.

**Required correction:** register peer bidirectional streams with bounded TX state, distinguish them from receive-only streams, and preserve both half-stream lifetimes. Test incoming TRACK and SUBSCRIBE through actual QUIC, including resets and capacity exhaustion.

### R3 — Normal receive-stream closure does not replenish stream credit

**Severity: blocks sustained downlink. Evidence: source trace against pinned ngtcp2.**

The adapter advertises 16 remote unidirectional streams and four remote bidirectional streams (`esp_moq_ngtcp2.c:741` and `:916`). Its `stream_closed_cb` at line 312 releases local TX bookkeeping but never calls `ngtcp2_conn_extend_max_streams_uni` or `ngtcp2_conn_extend_max_streams_bidi`. Neither function is called elsewhere in the adapter.

Normal ngtcp2 stream closure does not replenish these limits automatically. This was checked in the pinned implementation's `lib/ngtcp2_conn.c`, including `ngtcp2_conn_close_stream`, rather than inferred from the number of active records. The upstream [stream-credit API documentation](https://nghttp2.org/ngtcp2/ngtcp2_conn_extend_max_streams_uni.html) explains the application responsibility; its exception for streams closed before normal opening does not solve ordinary completed media streams.

At one group per 20 ms audio packet, 16 total unidirectional streams represent at most about 320 ms of groups, and SETUP/catalog traffic consumes some of the same initial allowance. This is a consequence of the configured limits, not a measured hardware stall time.

**Required correction:** return credit for retired peer streams while enforcing a cap on concurrently active streams. Verify at least 90,000 audio groups per direction over 30 minutes, plus control/catalog traffic, with no exhaustion or memory growth.

### R4 — One blocked stream can starve every newer stream

**Severity: blocks latency isolation and reliable cancellation. Evidence: source trace and isolated reproduction.**

`next_sendable_stream` at line 388 always scans from the first slot. `pump_output` at lines 488–492 returns from the entire pump on `STREAM_DATA_BLOCKED`, `STREAM_NOT_FOUND`, or `STREAM_SHUT_WR`. It neither moves to another writable stream nor retires stale send work.

The isolated harness held stream 2 flow-control-blocked and left stream 6 writable. Across four pump calls, all four attempts targeted stream 2; stream 6 was never attempted. A reset stream with queued unsent blocks can trigger the same scheduling problem until cleanup occurs.

This defeats a central reason to use separate audio group streams: an old blocked group must not prevent current media or control from progressing. The [ngtcp2 write API](https://nghttp2.org/ngtcp2/ngtcp2_conn_writev_stream.html) allows progress on other streams after these stream-specific conditions.

**Required correction:** skip blocked streams within a bounded scheduling pass, retire reset/closed TX work safely, provide appropriate media/control priorities, and always allow connection-level output. Test delayed old groups, reset-before-send, and backpressure under loss.

### R5 — The advertised receive datagram limit exceeds the actual buffer

**Severity: interoperability and packet-loss defect. Evidence: source trace.**

The UDP receiver uses a 1,350-byte stack buffer (`esp_moq_ngtcp2.c:525`). QUIC initialization caps **outgoing** packet size, but does not set `parameters.max_udp_payload_size`. The pinned ngtcp2 default for that advertised receive limit is 65,527 bytes.

A conforming peer can therefore send UDP payloads larger than this adapter can preserve. `recv` does not obtain the discarded remainder of a UDP datagram on the next call. Larger packets or path-MTU probes can be truncated and dropped or cause a connection failure; disabling the watch's own PMTU discovery does not constrain its peer.

**Required correction:** advertise the actual supported receive size, handle truncation explicitly, and test legal datagrams above 1,350 bytes and PMTU probing. Do not confuse Opus packet size, frame size, and UDP datagram size.

### R6 — Callback dispatch is not safe for ordinary request/reply use

**Severity: integration contract defect. Evidence: source trace; not reproduced as a hardware crash.**

The connected callback is deferred, but normal `stream_data` and `stream_closed` callbacks run from inside ngtcp2 processing (`esp_moq_ngtcp2.c:255` and `:335`). Calling the adapter's `write`, `finish`, or `reset` from such a callback invokes `pump_output`, which calls `ngtcp2_conn_writev_stream` recursively. Upstream explicitly forbids calling that writer from callbacks.

The present SETUP receiver mostly parses data, so this does not establish that every handshake will crash. It is a trap for the next necessary feature: replying to incoming MoQ control messages. Decoding Opus, updating LVGL, doing NVS writes, or running slow callbacks on this path would also delay QUIC processing.

**Required correction:** queue application events and outbound work and drain them after ngtcp2 returns. Document buffer ownership, copy requirements, callback task affinity, and which methods may be called from callbacks. Bound the queue and define an explicit overflow policy.

### R7 — A negotiated session does not yet transport voice

**Severity: missing functionality, not just a defect. Evidence: public API and call graph.**

`include/esp_moq/client.h` defines only `TX_NONE` and `TX_SETUP`; the public client implements init/start/connected/flush/peer-setup. The standalone codecs and subscription table are not wired into a network dispatcher. The handshake example routes every stream to the SETUP parser and closes immediately after readiness.

Missing runtime responsibilities include:

- Discovery/announcement handling and broadcast lifetime.
- Incoming request parsing and responses on peer streams.
- Outbound TRACK/SUBSCRIBE, their response state, and updates/termination.
- Association of stream IDs with parser instances and subscription generations.
- Publishing groups/frames with bounded ownership and cancellation.
- Hang catalog generation/parsing, plain and compressed catalog support, and rendition selection.
- Audio receive assembly, timestamp mapping, jitter scheduling, stale-group disposal, and loss recovery.
- Capture/utterance boundaries, response completion, and prevention of stale playback after interruption.
- Reconnect/backoff, token refresh, re-publication/re-subscription, and operation deadlines.

Adding these in VoiceWatch itself would turn the application into a second implementation of the library. Complete them in `esp_moq` behind a narrow media/session API first.

### R8 — A rejected duplicate start destroys pending handshake work

**Severity: lifecycle correctness. Evidence: independently reproduced against the actual core.**

`client.c:65` clears the pending transmission and peer SETUP bookkeeping before `session_start` validates whether starting is legal. If the initial SETUP is waiting for stream credit, a second `client_start` returns `INVALID_STATE` but has already erased that queued transmission. A subsequent `flush` returns success without sending SETUP; the session remains negotiating.

**Required correction:** validate state before mutation, or make repeated start explicitly idempotent. An error return must leave the active session and pending work intact. Include duplicate connect/start, failed reconnect, and cancellation races in lifecycle tests.

### R9 — An out-of-range subscription boundary silently becomes absent

**Severity: public encoder input validation. Evidence: independently reproduced against the actual core.**

`lite05_control.c:473` and `:483` reject an optional group value only when it equals the maximum QUIC varint. For `UINT64_MAX`, adding one wraps to zero; the encoder succeeds and the decoder interprets the boundary as absent. This is an invalid caller-input case, not a demonstrated remote exploit, but it violates the intended strict bounds and changes subscription semantics silently.

**Required correction:** reject every present value at or above the representable optional-value limit before addition. Test both start and end bounds at the limit and at `UINT64_MAX`.

## Security, memory, and hardware readiness

### Security has the right primitives but no demonstrated end-to-end gate

Positive findings: the adapter requires PEM roots, configures TLS 1.3 and peer verification, checks hostname, sends SNI, and checks the negotiated ALPN. There is no exposed silent insecure fallback. TX buffers are retained until acknowledgement, which is necessary for QUIC retransmission lifetime. The example avoids printing its JWT.

Remaining issues:

- The handshake example connects Wi-Fi and immediately starts TLS without synchronizing time or checking for a plausible wall clock. A cold boot can fail certificate-date validation. Integrate a bounded time-establishment policy; retain validation.
- A single bundled ISRG Root X1 is an explicit trust choice, not a maintained general trust store. Actual relay certificate chains must be verified.
- Static firmware JWT configuration is suitable only for a controlled experiment. There is no bootstrap, credential renewal, scope test, or token-expiry recovery.
- No certificate-negative, hostname-negative, ALPN-negative, scope-isolation, or malicious-peer tests exercise this backend.
- `create` shallow-copies pointer-valued configuration. Host, ALPN, CA, callbacks, and user context must outlive the adapter; this lifetime needs an explicit contract or owned copies.

The backend manifest declares `GPL-3.0-or-later` and its design document accepts wolfSSL's GPLv3 terms; the root still says a license has not been selected. Record a consistent project/distribution licensing decision before release. This is a dependency-readiness observation, not a legal assessment of a particular distribution.

### Bounded protocol objects do not establish a bounded product heap

The portable tests enforce roughly 9 KB for the client, 4.2 KB per control receiver, and 256 bytes for a group receiver. The backend adds 64 retained 256-byte TX payload blocks, 16 TX stream records, a 4,352-byte deferred input buffer, packet buffers, and metadata. Those are sensible explicit limits.

However, ngtcp2 is constructed with its default allocator and wolfSSL also allocates internally. A fixed TX pool does not prove that handshake, retransmission, incoming reassembly, or connection churn is allocation-free or fits internal SRAM. The initial connection receive window is 256 KiB. No adapter allocation budget, heap-capability policy, or high-water telemetry enforces the design's whole-system target.

The two design documents also specify different free-internal-memory targets (80 versus 96 KiB). Existing VoiceWatch hardware evidence shows much tighter internal-memory margins on the older watch. Removing esp_peer may recover memory, but there is no measurement showing that QUIC plus the full LVGL/WAMR shell fits. Do not run both complete media stacks simultaneously just to obtain a feature flag.

DNS resolution in `start` is synchronous; receive draining loops until the socket becomes empty. Neither has a dedicated scheduling budget. Idle timeout is configured, but no keepalive/resumption/reconnect manager is implemented. These details matter for a responsive watch shell and power behavior.

### The supplied examples are not Ultra board bring-up

Both ESP32-S3 examples select octal PSRAM. LilyGO documents the Ultra as using external quad PSRAM, with a 410×502 CO5300 AMOLED, CST9217 touch, GPIO expansion, PDM microphone, and a separate speaker amplifier. They need a verified Ultra profile before flashing. See the manufacturer's [Ultra hardware definition](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-watch-ultra.md).

VoiceWatch's current production BSP is `board_twatch_s3.cpp`, with an ST7789 240×240 panel, a different pin/power map, and octal PSRAM configuration. Its simulator already has an Ultra surface profile, but that does not implement the physical board. Neither this library nor the existing firmware currently supplies the required Ultra BSP. Preserve the existing native board abstraction and add an explicit Ultra target; do not reuse the older watch's PMIC or pin configuration.

## What VoiceWatch needs from the replacement

The existing firmware's `voice_service.cpp` combines three responsibilities. Only one is predominantly WebRTC:

| Responsibility | Existing implementation | Migration implication |
| --- | --- | --- |
| Media | `esp_peer`, Opus encode/decode, RTP/DTLS/SRTP, bounded playback, microphone/speaker handoff | Replace the transport while retaining or explicitly replacing audio behavior |
| Application control | WebSocket hello, capture requests/status, actions/results, `agent.state`, transcripts, app delivery | Must continue independently of the media replacement |
| Trusted lifecycle | Owner tokens, capture correlation, playback generations, cancellation, LVGL queue, NVS action journal | Preserve semantics and tests |

The firmware already uses 16 kHz mono, 20 ms/320-sample Opus at 24 kbps. FEC and DTX are disabled, encoder complexity is zero, and the receive code has a 12-frame PCM queue with a three-frame prebuffer. This is a reusable starting audio profile. The proposed library's complexity/DTX/FEC settings are different and should not change during the initial transport comparison without measurement.

`esp_peer` provides additional network jitter handling: 100 ms cache timeout and 2 KiB receive cache. A raw MoQ byte callback is not an equivalent replacement. Preserve pacing, cancellation, clock mapping, and the distinction between late media and lost media. A QUIC group FIN is not automatically the user's end-of-utterance signal.

The live-agent service currently uses `aiortc` and `WatchSession` in `services/live-agent/src/doodad_agent/transport.py`. It presents PCM to the conversation pipeline, retains a long-response audio spool, paces downlink, isolates device identities, and suppresses output from replaced sessions. A MoQ server endpoint must preserve those interfaces and semantics; changing firmware alone cannot work.

For the first migration, retain the bounded application control channel, upgraded to authenticated WSS where needed, while moving audio to native MoQ/Hang. WebSocket control is not itself WebRTC. If a later design also moves application control into MoQ tracks, define explicit ordering, idempotency, session identity, and replay rules: action results and install approvals cannot use lossy/latest-only media semantics. Audio must keep standard Hang framing rather than acquiring private application headers.

No change to the STT/model/TTS providers is inherently required. Replace the watch-facing media adapter, not the durable job ledger, tool boundary, or application delivery system.

## Validation performed and limits

| Check | Result | What it establishes |
| --- | --- | --- |
| Repository and API inspection | Merged revision reviewed | Actual implemented scope, not README promises |
| Portable-core strict host suite | 300,480 checks passed | Wire/parser/session behavior covered by the current suite; not 300,480 independent interoperability scenarios |
| Additional portable-core defect tests | Both reproduced | Duplicate-start data loss and optional-bound overflow, using the real core implementation |
| Isolated backend source harness | Both defect reproductions pass | Incoming bidi response rejection and first-stream starvation in adapter logic |
| Empty-config ESP-IDF 5.5.5 build | Succeeds | Component compilation and a small early-return image |
| Nonempty dummy-config ESP-IDF build | Fails at link | Active authenticated-handshake path is not buildable as configured |
| Existing VoiceWatch focused Python tests | 50 passed | Baseline transport/conversation/control/app-delivery tests remain available for migration |
| Live relay, certificate negatives, real audio, watch flashing | Not run | No interoperability or hardware success is claimed |

The 50 Python tests cover `test_transport.py`, `test_conversation.py`, `test_contracts.py`, `test_controller.py`, and `test_app_delivery.py`. They emitted existing deprecation warnings and one coroutine-not-awaited warning; a passing result is not a clean runtime-soak result.

The source harness compiles extracted adapter functions with mocked ngtcp2/socket boundaries. It is not a substitute for a network test. Its plain build passed; a separate AddressSanitizer attempt stalled on this host and was terminated, so no sanitizer pass is claimed for that harness. Portable-core sanitizer results are recorded separately in the companion review.

The subagent established that 300,000 of the host-suite checks come from 100,000 varint round trips. Additional core reproduction output and the test source are retained as `core-repros.txt` and `core_review_repros.c` in the evidence directory. No generated Rust oracle or cross-implementation fixture suite is present yet.

## Gates before integration

1. **Buildable transport:** make the nonempty-config example link; establish correct Ultra memory settings and time initialization. Add active-path builds to CI.
2. **Correct QUIC lifecycle:** fix peer-stream responses, stream-credit renewal, blocked/reset scheduling, receive datagram limits, and callback dispatch. Add focused adapter tests and verified TLS negative tests.
3. **Operational MoQ/Hang client:** wire the existing codecs into real publisher/subscriber state machines, catalog handling, typed audio callbacks, deadlines, shutdown, and reconnect. Define ownership and hard memory budgets.
4. **Independent interoperability:** build the pinned Rust oracle and generate fixtures from it. Demonstrate watch publication consumed by an unmodified reference peer and reference publication consumed by the watch. SETUP alone does not satisfy this gate.
5. **Audio and lifecycle parity:** keep the existing PCM profile initially; demonstrate paced full responses, clear/end/drain behavior, interruption, late-packet rejection, timestamp continuity, and capture boundaries through the reference relay.
6. **Full-shell hardware fit:** add the Ultra BSP and run LVGL, WAMR, Wi-Fi, QUIC, control, audio, and package delivery together. Record internal heap/minimum/largest block, PSRAM, stacks, frame timing, and underruns.
7. **Cutover evidence:** sustain 50 groups/second each direction for 30 minutes, then repeated PTT/reconnect/idle runs and controlled loss. Test two devices for isolation and verify that cancellation and app-owner changes cannot replay stale audio or actions. Keep WebRTC rollback until these pass.

The immediate engineering task is to complete and validate the library, starting with the active-path build and QUIC adapter defects. A production VoiceWatch migration estimate would be premature until those gates establish that the transport behaves correctly on the Ultra within the full-shell memory budget.
