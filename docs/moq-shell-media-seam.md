# VoiceWatch media seam and Ultra owner

Implementation checkpoint, 2026-08-30. This describes the internal media
integration, not a production-authenticated voice replacement. The complete
acceptance scope remains in `moq-webrtc-replacement-plan.md`.

## Product and media ownership

`voice_service.hpp` keeps its public request/event and guest-owner API. The
shared `voice_service.cpp` retains control envelopes, capture/request
correlations, transcript routing, trusted action dispatch, agent-state updates,
NVS journaling and personal-package offers. Those behaviors are not copied into
the library or either media implementation.

`voice_media_transport.hpp` selects one implementation at build time:

- `voice_media_webrtc.cpp` contains the extracted legacy peer, codecs, capture
  and playback. SDP/ICE travel through a copied signaling callback into the
  control queue. Existing boards retain this selection.
- `voice_media_moq.cpp` uses the library endpoint and audio service. It owns a
  64 KiB PSRAM audio task, initialized encoder/decoder, fixed capture/player
  arenas, sixteen command slots and sixteen copied lifecycle-event slots.
  Endpoint workers own DNS and QUIC. No codec, socket write, flash operation or
  LVGL call occurs inside an ngtcp2 callback.

The control task polls copied events; network/codec work cannot be delayed by
its potentially blocking WebSocket send. UI audio levels use the existing
display queue. The public native cancel request immediately invalidates MoQ
media before queuing its separate host notification.

## Explicit authorization and generation boundaries

`Session` must contain an authenticated control generation, host, trust roots,
setup path, directional broadcast names and monotonic authorization/time leases.
The endpoint verifies TLS against the real UTC clock as well. Configuration is
copied with bounded lengths; the temporary setup-path copy is wiped on release.
A welcome frame or successful WebSocket connection is insufficient authority.

**The production bootstrap provider is still missing.** Consequently, the normal
MoQ selection currently refuses the legacy anonymous mDNS/`ws://` discovery path.
It cannot start a production voice session. The default Ultra profile still has
voice disabled; an explicit private diagnostic enables and exercises MoQ.

Each session generation must increase. A cancellation revision invalidates
queued starts and both current service operations synchronously. The board
rejects new speaker handoffs immediately; the audio owner then stops DMA and
clears codec/PCM state. A new capture has a firmware-issued capture ID and
request/guest owner identity. Cancellation retires that identity so a delayed
response cannot enable playback afterward. Speaker owner numbers do not reset
when the underlying endpoint is replaced.

`Response` binds a session, response ID, capture/request/owner identity and MoQ
group range. It must match the current authorized turn; repeated/stale response
IDs and replacement-session mismatches are rejected. A successful queue return
only accepts a command. The peer must wait for `playback.bound` before sending;
its final `end_group` is exclusive. The production control parser and host
binding handshake are still to be connected and tested.

## Capture, playback and shutdown

The existing Ultra BSP handle is borrowed from `board_ultra.hpp`. The media
owner never opens a second board or closes the UI's shared I2C owner. Capture
starts only for an explicit command. Its nonblocking 10 ms chunks preserve the
sample clock; discontinuities reset the codec through a mandatory empty group.
One held microphone chunk survives temporary queue pressure. Capture finish
flushes lookahead and the logical tail once; completion reports the exact
publication range after the service's terminal outcome.

Receive packets are copied into the bounded player, with every service lease
released even when stale. PCM is copied into one pending chunk and submitted
through `esp_moq_service_media_commit` and the board's nonblocking speaker FIFO.
`playback.finished` follows the player's terminal drain **and** physical DMA
drain, with the response identity and completed sample count. Cancelled/error
completion is distinct from successful playback.

Endpoint close invalidates media, releases held leases and retries destruction
until DNS/network workers have joined. It does not force-delete workers inside
DNS or TLS. Warm codecs and the permanent audio-owner buffers remain allocated;
a transport disconnect is not a claim that all voice resources are freed.

## Resource and scheduling changes

The first combined physical run passed audio but reached only 95,040 bytes
minimum free internal RAM and a 510,059 us display flush. These fail the planned
96 KiB memory floor and show unacceptable startup starvation; the successful
media marker does not override either observation.

The follow-up implementation keeps the endpoint's task-only copied
configuration in PSRAM. The Ultra also explicitly opts into a PSRAM service
control arena after a second memory shortfall; the library default keeps that
arena internal. Internal worker stacks and board DMA are unchanged. Optional endpoint worker affinity lets this application
place TLS/DNS on CPU1, away from the priority-1 LVGL loop on CPU0. Audio also runs
on CPU1. The latest short exchange reports no concealment, late or pressure
frames; sustained contention and UI interaction still need measurement. No memory
or latency acceptance threshold is relaxed by these changes.

## Diagnostic scope

`DOODAD_MOQ_DIAGNOSTIC_CONFIG_DIR` is an explicit CMake-only opt-in that requires
the Ultra MoQ selection and a private `ultra_test_config.h`. It adds
`voice_moq_diagnostic.cpp`; normal firmware does not link it. The diagnostic uses
USB-provisioned test time, the public test-token fixture and the pinned Rust
reference echo endpoint. It records 1.2 seconds, plays the complete reference
response and checks cancellation/idle cleanup while the native shell, WAMR and
storage remain running. It exports PCM only into the private serial capture for
codec comparison. It does not drive the public Voice Orb/PTT workflow or a live
STT/model/tools/TTS conversation.

`tools/verify_moq_shell.py` checks sample lengths, identity, tail boundaries,
reference PCM tolerance and device completion. Memory and UI observations are
reported separately from the media-exchange result. Do not publish the private
configuration, image, raw serial or audio files. Flash using the inspected
app0-only runner; firmware restoration is not required.

## Work still required

Connect authenticated HTTPS/WSS bootstrap, scoped credentials and trusted-time
policy; implement the bounded Rust/Python host bridge and explicit media binding
messages; preserve existing conversation/action/package behavior through that
bridge. Then validate real PTT, owner/app switching, interruption during decode
and DMA, repeated session replacement, full responses, UI/input parity, resource
headroom with production TLS/control, impairment and all sustained gates. The
legacy dependency's build-time download/DTLS configuration still needs complete
conditionalization even though its peer symbols are absent from the MoQ ELF.

Latest physical measurements and every failed attempt are recorded in
[the shell MoQ checkpoint](implementation-evidence/2026-08-30-shell-moq/README.md).
