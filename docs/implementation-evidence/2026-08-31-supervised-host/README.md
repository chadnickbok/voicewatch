# Paired host supervision and local deployment

This checkpoint implements and installs the explicit Mac MoQ service pair.
It does not accept the complete WebRTC replacement. The existing WebRTC service
and default transport remain unchanged; no watch firmware was written.

## Implemented behavior

One launchd job owns Python HTTPS/WSS/providers and the native QUIC endpoint.
The native child starts after Python's listeners and discovery registration are
ready. Both children have inherited readiness/lifetime channels; failure of
either retires the pair, and supervisor death closes the lifetime channels.
The native child does not inherit provider, SMTP or personal signing keys.
Shutdown has a 30-second bound before killing a stuck child. launchd provides
restart with a ten-second throttle; this is not an after-readiness CPU watchdog.

Each invocation uses a fresh short owner-private IPC directory. It never
unlinks an existing socket to force a bind. Configurations and the release
binary are validated, and deployment copies its own codec notices, server chain,
leaf key and device enrollment map. The root signing key is not deployed.
The installer uses a separate label/runtime/data tree and `uv --no-dev`.
The installed environment has neither aiortc nor PyAV.

The first running-service reinstall exposed launchd's asynchronous shutdown:
the old supervisor still held its profile lock when preparation began. That
installation failed closed. The corrected installer waits at most 40 seconds
for the profile lock before replacing Python runtime files, and refuses to
bypass the lock. A deterministic delayed-release test covers the race. The
final running-service reinstall succeeds. Redundant kill/restart immediately
after `bootstrap` was also removed from the MoQ install/start path.

## Process and native validation

- 342 Python tests pass, with four existing warnings. New cases exercise real
  child startup, either-child death, first-child death while the second starts,
  bounded startup/shutdown, supervisor SIGKILL, environment/descriptor isolation,
  duplicate-instance exclusion, private configuration and atomic packaging.
- Eight native integration cases pass. Two launch the actual Python main and
  native endpoint, kill one child, start a fresh pair and verify old WSS grants
  receive HTTP 403 and old media grants are rejected over verified native QUIC.
  A new device proof and control grant succeed afterward. They send no hello or
  capture request, so they do not exercise provider turns or microphone PCM.
- All 28 Rust tests pass in both debug and release profiles. Clippy with warnings
  denied, debug binaries/examples and the optimized endpoint build pass.
- Shell syntax and actual `plutil` array arguments with spaces/quotes pass.
  The final installed scripts/modules match seven relevant source files, and
  the private deployment generation manifest verifies every copied artifact.

No sanitizer run or remote CI success is claimed for this checkpoint. Log
hashes, counts and scope are in [verification.json](verification.json).

## Physical deployment and recovery

The local release endpoint is 10,998,736 bytes, SHA-256
`71031dd3a7fd107b47de45aa8ec7be2f1d666142b80277e4502ae29de3740e94`.
The Ultra retains flash34 from the preceding pacing checkpoint. Its enrollment
is deliberately advanced to the persistent local service profile. That profile
uses a 365-day private root and a 30-day leaf expiring
`2026-09-30T12:51:25Z`; it does not reuse the six-hour test PKI.

The actual deployed native endpoint was killed with SIGKILL. The supervisor and
both original children exited. launchd started a new pair, which reported
listening after 6,652 ms; the watch reached full application media readiness
after 13,569 ms (approximately 6,917 ms later). No capture started. The separate
WebRTC service kept the same process throughout. These are sampled wall-clock
intervals from one fault injection, not latency percentiles or an eight-hour soak.

After the final reinstall, three complete media-ready transitions have been
observed across service incarnations and zero capture starts. The MoQ service
is left running and the watch remains enrolled to it. See
[deployment-result.json](deployment-result.json),
[native-crash-recovery.json](native-crash-recovery.json) and
[provisioning-summary.json](provisioning-summary.json). The counters do not
claim a new physical PTT/STT/tool/TTS turn on this deployed release.

## Binding and limitations

[source-snapshot.json](source-snapshot.json) records the uncommitted checkpoint
against the pushed parent/library revisions, relevant source hashes, pinned
reference and optimized endpoint hash. Raw logs, transcripts, provider keys,
device identities, enrollment keys, signing keys, databases and binaries remain
private. No ambient microphone PCM is persisted by these tests.

Automatic certificate rotation, hard native allocation limits, impaired speech,
the full loss/RTT matrix, long response/renewal cases, physical button/touch and
installed-app parity, frame-budget acceptance and long-duration release soaks
remain open. No default switch or full-product acceptance is implied. The
[host runbook](../../moq-supervised-host.md) covers explicit installation and
trust renewal. Firmware restoration is not required.
