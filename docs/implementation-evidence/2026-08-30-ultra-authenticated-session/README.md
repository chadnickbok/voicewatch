# Authenticated Ultra full-shell media checkpoint — 2026-08-30

The physical Ultra now runs the full shell with USB enrollment, authenticated
time, verified HTTPS/WSS control and native MoQ media through the real Python
product adapter and Rust endpoint. These tests use a private local hardware
bench, not live STT/model/tools/TTS or the deployed service.

## Verified hardware behavior

- Both final builds (MoQ Ultra and legacy WebRTC CoreS3) succeed. The app-only
  runner verified the connected board/layout and installed the full Ultra image;
  it reached shell steady state. No bootloader/partition/OTA/NVS erase or default
  firmware restoration occurred. Enrollment intentionally updates its own NVS
  namespace. Flash receipts are in `hardware-results.json`.
- `idle2`: three distinct authenticated sessions, zero microphone samples,
  forced reconnect in 6,544 ms and successful automatic lease renewal.
- `audio2` on the final image: three distinct sessions, forced reconnect in
  6,521 ms; 19,200 microphone samples received; a complete 16,037-sample tone;
  cancellation of a subsequent response and successful replacement on the same
  capture, without another microphone capture. Lease renewal also passed.
  Both completed tones reported zero concealment, late frames or pressure drops.
  Completion was a matching firmware receipt after the existing DMA-drain check.
- `expired3`, `notyet1`, `hostname1`, `untrusted1`: each issued one time proof,
  then received an explicit firmware certificate rejection; zero authenticated
  sessions, zero microphone samples, and no retry in the four-second observation
  after rejection. These use flash3; the subsequent flash4 changes only response
  cancellation handling. They test the bootstrap HTTPS verifier, not independent
  QUIC/WSS certificate faults or ALPN negative cases.
- Repeated enrollment changed the key/root/profile revision on the same device.
  A valid profile recovered from the prior terminal rejection. Keys and private
  PEM markers were absent from the collected serial/native/event/tool logs.

`snapshot.json` records the source and binary hashes. The latest installed image
is `5937c36ebd6e206d213a9e0ddedbef376cc385d682be4f70cd93d8009d2de7c4`.
The native endpoint binary was unchanged during hardware runs; the test-only
reference probe changed during the regression investigation below.

## Regressions and corrections

Default Python suite: **191 passed**, with four existing warnings. The explicit
C++ parser lane: **26 passed**. Rust: **seven passed**; formatting and Clippy pass.
The five real TLS/WSS/IPC/QUIC integration cases passed **ten consecutive runs**
after the probe correction; see `native-repeated.log`. This is 50 repeated cases,
not 50 distinct tests. The C++ lane tests protocol parsing, not the new threaded
audio cancellation state machine; physical response replacement covers that path.

Earlier failures are retained in `hardware-results.json`:

- `idle1`: USB reads timed out. IDF nonblocking VFS availability comes from the
  installed driver ring; the console had no driver. Installing it fixed enrollment.
- `expired1`: TLS rejected the certificate, but the firmware retried because IDF
  omitted verify flags with peer-certificate retention disabled. The firmware now
  also recognizes the X.509 verification error code and reports terminal rejection.
- `expired2`: the corrected firmware rejected once, but the bench missed an early
  serial time message while switching from enrollment to monitoring. The bench now
  requires observed issuer time-proof issuance plus explicit firmware certificate
  rejection. Lack of a connection by itself cannot satisfy the test.

Repeated native testing exposed a probe race: a reset SETUP stream can make the
reference driver close locally before Quinn observes the server's application
close. Keeping the driver unpolled was tested and failed because it also prevents
SETUP progress. The corrected fixture drives SETUP, requires connection closure,
and independently asserts an actual issuer denial and no unauthorized attachment
or PCM. Thus a locally initiated close alone cannot pass the rejection test.

Review during the run also found that response cancellation erased a completed
capture context. The new scoped cancellation fences old service/DMA work and
queued bindings, preserves only the matching completed capture at that revision,
and permits a later response. General cancellation, new capture and disconnect
still invalidate that context. Binding retries remain bounded while old service
slots retire. The physical `audio2` replacement case passes this corrected path.

## Limits and next gates

No provider was contacted or deployed service restarted. The bench processes
stop at the end of each run; the watch retains the new firmware and its latest
private test profile and waits for a reachable host. No ambient PCM, private
profile/key, certificate, firmware image or raw serial log is in this directory.
Public serial metrics are a whitelist of fixed status and numeric playout lines.

These few recovery samples are not p95 evidence. Initial QUIC/media readiness
still takes several seconds and does not meet the 500 ms connection target.
This tests reconnecting after lease expiry, not uninterrupted long responses or
proactive renewal. NVS profiles are not encrypted on this development device.
Full-shell navigation/app delivery, provider generation ownership, text/background
responses, resource/loss hardening, certificate/ALPN/scope matrices, memory/UI
stress and long soaks remain required by the full plan. No release gate is waived.
