# Host boundary checkpoint — 2026-08-30

`doodad-runtime/scripts/test-live-agent.sh` passes **142 tests**, including 45 new
boundary cases and all 97 prior tests. `snapshot.json` records the source hashes
and sanitized test-log hash. Four pre-existing product warnings remain: three
deprecations and a Pipecat test coroutine warning. CI execution is not claimed.

Implemented source includes transport-neutral PCM spooling, exact sample tails,
shared control/action futures, optional/lazy WebRTC imports, owner-only enrollment
loading, HTTPS nonce/HMAC issuance, WSS-bound one-use grants, and bounded private
Unix IPC with session/sequence validation and linked lifetime revocation.

Tests establish:

- Real verified TLS/WSS using generated, temporary test certificates; unknown CA,
  plaintext, forwarded-TLS spoofing and query credentials rejected.
- Wrong keys, nonce/token replay, direction token confusion, per-device replacement,
  stale-owner cleanup, expiry and UTC/monotonic faults rejected by the issuer.
- Real private Unix socket authentication, exact PCM transfer, matching identity,
  session/sequence replay rejection, EOF/expiry/control-loss revocation and peer caps.
- Permission/path checks, refusal to overwrite an existing live socket, binary
  frame fragmentation, invalid/ambiguous/oversized metadata, partial EOF, and
  bounded writer stalls/concurrency rejection.
- Streaming resampling with exact unpadded tails, cancellation while a paced frame
  is held, generation retirement on overflow, preserved action futures and legacy
  long-response/pacing behavior.
- Neutral imports succeed with aiortc and av imports explicitly unavailable.
  The enrollment proof matches an independent OpenSSL HMAC test vector.
- A separate clean `uv sync --locked --no-dev` installation contains neither
  aiortc nor PyAV and imports the neutral modules, CLI and existing
  `LiveConversation` module successfully. No live provider instance was started.
  See `base-install.json`.

The first IPC run failed because pytest's long macOS temporary paths exceed
`sun_path`. The tests use short private directories now; the server explicitly
rejects overly long paths. Inspection also found asyncio's path-based Unix bind
could delete a socket; the implementation binds a socket itself and passes its
handle to asyncio. No permissions or path checks were bypassed.

This does **not** establish a real Rust-to-Python media exchange, native QUIC scope
enforcement, authenticated watch enrollment/time, the production MoQ session,
STT/model/tools/TTS, or physical PTT/playback. The normal service still selects
WebRTC. No service was deployed or restarted, no actual enrollment/provider
credentials were read or installed, and the watch was not flashed or recorded.

See [the detailed contract](../../moq-host-boundary.md) for the implementation
boundary and required next work. The full replacement goal remains open.
