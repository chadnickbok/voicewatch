# MoQ product session evidence — 2026-08-30

This local checkpoint adds `MoqSession`, `MoqTransportServer`, private service
configuration and explicit `serve --transport moq` selection. It wires existing
conversation callbacks, actions and HTTPS artifact routes to the native worker.
Base commits are VoiceWatch `b0935ef` and library `d9bb2ae`; these changes are
uncommitted at the time of this evidence run. `snapshot.json` binds the source
and debug binary hashes at that checkpoint; it does not cover the subsequent
firmware bootstrap and authenticated-time changes.

Verification:

- **182 Python tests pass**, including the 142 prior tests and 40 new session/
  service cases. Four existing product warnings remain; see `python-tests.log`.
- **Five native integration tests pass**, using actual verified TLS/WSS, Unix IPC
  and native QUIC. The two new product-adapter cases exercise exact bidirectional
  537-sample reference PCM, watch binding, encoder/end receipts and cancellation
  before binding with a stale receipt. The three prior cases cover native audio,
  invalid/replayed tokens and WSS revocation closing QUIC. See `native-integration.log`.
- **Seven Rust tests pass**; exact reference tails, persistent response epochs,
  cancellation, scoped origin models, IPC and grant validation. See `rust-tests.log`.
- `cargo fmt --check`, `cargo clippy --locked --all-targets -- -D warnings`, and
  `cargo build --locked --examples --bins` pass.
- The separate existing base environment imports the new MoQ service, config,
  CLI and `LiveConversation` while aiortc/PyAV are absent. No provider was started.

The new Python checks include capture PCM before final STT padding, rejection
of invented playback completion, cancellation during paced PCM, stale generation
cleanup, old playback waits returning after replacement, private config/FIFO
rejection, real WSS identity/replay/duplicate rejection, same-device replacement,
HTTPS route mounting and shared action future cleanup. PTT tests cover release
before the start receipt and cancellation overtaking a suspended application
callback without rearming the microphone.

All audio, enrollment keys and certificates in these tests are synthetic fixtures.
Actual watch playback receipts are **emulated**; the native reference consumer
checks decoded audio independently, but this is not physical DMA completion.
No actual provider/enrollment file was loaded, service deployed or watch flashed.
No ambient PCM, private configuration, key or firmware image is included here.
Workspace paths in logs are normalized to `/workspace/voicewatch`.

This does not close firmware bootstrap/control, provider-turn generation/ownership,
text/background response context, lease renewal, native loss/memory hardening,
deployment, full-shell or sustained acceptance gates. See the
[current contract and remaining work](../../moq-product-session.md).
