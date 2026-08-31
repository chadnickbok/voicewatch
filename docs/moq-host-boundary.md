# MoQ host audio, authorization and IPC boundary

Status: implemented Python boundary, native Rust endpoint and explicit MoQ
product adapter, 2026-08-30. Default `serve` still selects WebRTC; the new
`--transport moq --moq-config ...` mode is a development checkpoint. No enrolled
device, running service or firmware was changed. Firmware bootstrap/control,
live provider turns and release gates remain open. The sections below record
earlier checkpoints; see [the current product session contract](moq-product-session.md)
for the adapter and its limits.

## Shared audio and product control

`audio.PcmSpool` now owns bounded PCM16k spooling, streaming soxr resampling,
20 ms pacing and generation-based interruption without importing aiortc or PyAV.
The default spool is 600 seconds of samples, plus bounded per-frame Python object
metadata. It is a host response spool, not the watch jitter buffer. Capacity
failure is explicit; a resampler overflow retires the generation rather than
allowing a retry with already-consumed source samples.

A single reader calls `read(generation)`. Final chunks contain their exact sample
count; the MoQ encoder must flush its own lookahead and carry the Hang terminal
boundary. `None` means the **local** spool drained. It is not a watch speaker
completion receipt. Cancellation invalidates a held packet even across a pacing
await; a new utterance has a new generation. Encoding and cross-channel media
range binding remain the native worker/application adapter's responsibility.

`transport_webrtc.DownlinkAudioTrack` is now a small adapter over that spool. Only
it pads the last RTP frame to 320 samples and keeps the historical 350 ms local
playout-tail heuristic. Neither behavior is inherited by a future MoQ adapter.
`session.ControlSession` owns shared control envelopes and action futures;
`DownlinkUtteranceBinding` still prevents TTS callbacks from moving into a
replacement session. `transport.py` preserves lazy compatibility imports.

aiortc is an optional `webrtc` installation extra and a development dependency.
The current service installer explicitly installs that extra, preserving the
existing deployed mode. Importing the neutral modules or CLI does not load the
legacy adapter. Default `serve` is still WebRTC; explicit MoQ mode now wires the
worker through `MoqSession`, with separate configuration and remaining physical/
provider acceptance requirements.

## Enrollment and grant issuance

An owner-only regular JSON file outside the repository maps device IDs to distinct
32-byte random enrollment keys represented by 64 lowercase hexadecimal characters.
`load_device_keys` rejects symlinks, group/other permissions, duplicate IDs or keys,
invalid IDs, and files larger than 64 KiB. There is no default key. Provisioning
these keys into watch NVS and implementing rotation are still pending.

`MoqBootstrap.start` requires a TLS server context allowing TLS 1.2 or newer,
disables access logging, and serves these routes. All reject plaintext connections,
query strings, and attempts to confer TLS trust through proxy headers. Request
bodies are limited to 4 KiB with exact field sets; responses are `no-store`.

1. `POST /v1/moq/challenge`, body `{"device_id":"..."}`, returns a random
   256-bit base64url challenge for an enrolled device. A challenge is valid for
   30 seconds by default; a new challenge replaces only that device's previous
   unredeemed challenge.
2. `POST /v1/moq/bootstrap` supplies `device_id`, `challenge` and a lowercase hex
   HMAC-SHA256 `proof`. The signed bytes are the ASCII concatenation
   `voicewatch-moq-bootstrap-v1\0` + device ID + `\0` + challenge.
   A matching nonce is consumed even when the supplied proof fails. A successful
   nonce cannot be replayed.
3. The result contains independent one-use control/media tokens, a random
   session ID, media host/port, control path, UTC expiry and monotonic lease.
   Watch publication is scoped to `voicewatch/<device>/<session>/watch`, and watch
   subscription to `voicewatch/<device>/<session>/agent`. These are watch-relative
   directions; the host's directions are inverse.
4. `GET /v1/moq/control` upgrades to WSS using `Authorization: Bearer <control_token>`.
   Query credentials are forbidden. The handler receives the server's session ID
   and owner object, not an identity asserted by a watch hello. The application
   must validate hello/control identity against `registry.identity(session, owner)`.
5. Only after WSS activation may the Rust endpoint redeem the media token over
   private IPC. The proposed native SETUP path is `/voicewatch/v1?token=<token>`.
   The native endpoint now enforces this grammar and uses separate scoped origins.
   Actual QUIC invalid-token/replay tests pass; adversarial cross-scope network
   coverage remains required in addition to the direct origin-model tests.

Tokens are opaque random capabilities, not an invented JWT dialect. They apply
only to VoiceWatch's own issuer/endpoint. The watch stores no relay signing key;
this does not claim compatibility with arbitrary external relay authorization.
The registry stores token hashes, never emits them in exception text or reprs,
and has a fixed cap of 256 retained challenges/grants by default. Full capacity
rejects new work; it never evicts live grants to satisfy an unauthenticated request.

Activation of a replacement control session revokes previous grants for that
device only. Closing either WSS or IPC revokes both channels. Default attachment
must complete within 30 seconds; a live grant expires after at most 300 seconds
(configurable up to 900). Registry checks use monotonic expiry and UTC expiry;
UTC rollback over one second or monotonic rollback clears grants and denies new
work until the host clock is corrected. Restart clears all grants. The WSS/IPC
liveness watchers check every 100 ms; application callbacks have bounded cleanup.
Refresh, client backoff/jitter, warm-idle policy and root/key rotation remain open.

The watch must already have a trusted root and defensible certificate-validation
time before HTTPS. Neither SNTP nor the bootstrap response's UTC value solves
cold-start authenticated time. The physical diagnostic's USB-provisioned time
remains separate; a production enrollment/time policy is still required.

## Local IPC v1

`MoqBridgeServer` binds an owner-private Unix socket directory, with socket mode
0600 and a path of at most 100 encoded bytes for macOS portability. Startup never
unlinks an existing path. Cleanup removes only the socket inode it created.
At most eight peer tasks run (configurable 1–32), with 8 KiB stream reader limits.
The supervisor must provision the directory and handle a genuinely stale socket;
no supervisor or native process has been installed by this checkpoint.

Each packet is a big-endian unsigned 32-bit JSON length, that many UTF-8 JSON
bytes, then exactly `pcm_bytes` bytes of PCM16k little-endian mono. Limits are
4,096 JSON bytes and 640 PCM bytes (20 ms). JSON requires integer `v:1` and a
bounded `type`; duplicate keys, nonfinite numbers, odd/oversized PCM, invalid UTF-8
and truncated frames are rejected. Only `capture.pcm` and `playback.pcm` carry PCM.
Headers/errors/reprs never print raw audio or tokens.

The first packet is exactly `v`, `type:"attach"`, `token`, `pcm_bytes:0` within
three seconds. Redemption requires a live WSS grant and permits one IPC owner.
The reply is `type:"authorized"` with session/device/scopes and the remaining
lease in milliseconds. Later packets require the exact session ID and strictly
increasing integer `seq` starting at 1 in each direction. Failed authentication,
wrong sessions, sequence replay, malformed frames, lease loss, EOF and stalls
close the connection and revoke the associated control grant.

Active reads have a 30-second deadline, requiring the future worker's idle
heartbeat at a shorter interval. Writes drain within two seconds and reject
concurrent writers instead of building an unbounded waiter queue. Application
callbacks execute serially with two-second deadlines. A single application actor
must own output scheduling, cancellation priority and heartbeats. The listener
does not start capture, decode unbound audio or report media readiness by itself.

The remaining application messages must bind capture/request/owner identities,
response generations and first/end group ranges. The host must wait for the
watch's matching `playback.bound` before sending response PCM, and matching
`playback.finished` after the Hang tail before completing a turn. Those semantics,
the Rust Opus worker, retained-track lifecycle and actual native grant enforcement
are **not implemented by this framing layer**.

## Verification and next integration

The current suite passes 142 tests: 97 prior product/transport tests and 45 new
boundary cases. New evidence includes real TLS/WSS with generated test certificates,
real Unix sockets, permission/path/connection limits, replay and clock faults,
whole-utterance resampling, exact tails and cancellation while paced PCM is held.
The HMAC byte contract also matches an independently computed OpenSSL vector.
No external model/TTS requests or ambient microphone recording were used.
A separate clean installation without development dependencies also imports the
neutral modules, CLI and existing conversation module with neither aiortc nor
PyAV installed; this verifies imports, not a live provider turn.

The subsequent native endpoint checkpoint implements scoped origins, standard
Hang Opus workers and a bounded IPC actor. The following checkpoint adds
`MoqSession`. Next implement the firmware control/bootstrap path, then run the complete
STT/model/tools/TTS turn on the Ultra. The Python boundary tests alone do not
prove those remaining end-to-end requirements. See the full replacement plan and
[host boundary evidence](implementation-evidence/2026-08-30-host-boundary/README.md).

## Native endpoint checkpoint

`libs/moq-esp32/server/voice_agent` now contains the pinned Rust TLS/QUIC endpoint,
private IPC grant redemption, separate scoped origins and persistent Hang Opus
capture/response tracks. Seven Rust tests pass, including exact reference codec
equivalence across short tails and repeated responses, cancellation, strict
framing and grant validation. Rust formatting and Clippy checks pass.

The separate `tests/moq_native_integration.py` lane exercises real Python
HTTPS/WSS/IPC and native QUIC with generated test credentials and synthetic
audio. Its original three cases cover exact 537-sample audio, invalid/replayed
tokens and WSS revocation using a fixture control driver. Two subsequent cases
use the real `MoqSession` adapter; watch receipts remain emulated and provider
turns untested. The earlier evidence directory remains the historical Python
checkpoint, not evidence for this subsequent native implementation.

The native actor waits for an authenticated capture end and matching terminal
media/sample count. Response media waits for `playback.bound`, and encoded tail
completion is distinct from watch speaker completion. Timestamps increase across
responses and cancellation. Loss recovery, network cross-scope adversarial
coverage, endpoint-to-watch tests and long operational soaks remain required.
Upstream origin pool sizes are eviction budgets, not hard allocation caps;
upstream group/frame caches can allocate before application checks. Hard process
memory bounds and announcement limits remain open. See the library's
`server/voice_agent/README.md` for commands, configuration and limitations.
