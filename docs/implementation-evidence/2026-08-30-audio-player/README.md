# Opus receive player checkpoint — 2026-08-30

This checkpoint adds reference-compatible Opus metadata and audio boundaries,
a bounded receive player, and its pinned Espressif decoder adapter. It does not
complete the library candidate or VoiceWatch replacement gates.

## Evidence

- `host-audio.log`: normal macOS/libopus 1.6.1 tests and byte-for-byte
  regeneration of nine fixtures using the unmodified pinned Rust Producer and
  Consumer. Both sample counts and PCM match; worst observed RMS error is zero.
- `linux-tests-sanitizers.log`: normal and ASan/UBSan core, endpoint, mocked
  actual-adapter, and audio tests. Linux system libopus 1.3.1 itself is not
  instrumented. `linux-service-final.log` repeats normal/sanitized service tests
  after adding the same-track format-change and held-lease regression.
- `native-results.jsonl`: all 17 real QUIC matrix rows complete as expected,
  including eight explicit rejection cases. A rejection row's `pass=false`
  describes the rejected connection, not failure of the matrix. The harness
  exits zero only when every expected success/rejection matches.
- `ultra-audio-serial.log` and `ultra-result.json`: app0-only physical Ultra
  run, nine fixture cases repeated three times, all with exact PCM and sample
  counts, plus PLC. 82 decode calls, worst 2,600 us. This image initializes no
  Wi-Fi, I2S, display, touch or PMIC. No microphone/speaker audio is established.
- `ultra-first-plc-failure.log`: the initial target run passed all 27 normal
  cases but rejected PLC because the Espressif wrapper validates a non-null
  input pointer before its recovery flag. The corrected adapter supplies a
  valid pointer and zero length. Only the corrected image is currently installed.
- `ultra-build.log`: successful corrected target image build. `runner-tests.log`
  records the eight offline live-layout safety checks. No default-firmware
  preservation or restoration is required, and none was performed.
- `clang-analysis.log`: static analysis of the portable player emitted no
  diagnostics. `macos-sanitizer-startup.txt` documents a sampled sanitizer/dyld
  initialization deadlock before main; that run was stopped, not called a pass.

The fixture exporter lives in `tests/interop/peer/src/bin/audio_fixture.rs` and
uses `moq-audio`/`moq-mux` at `eb5776e21eeaecba8e844be53c821895c178bcaf` without
upstream source modifications. `tests/audio/fixtures.json` contains actual
catalog descriptions, wire packets/markers and decoded reference PCM. The
host tests also cover explicit codec discontinuities, deliberate pauses,
reordering, three-frame PLC limits, bounded pressure, stale clock catch-up and
replacement-owner rejection. The physical run does not repeat every host
failure/pressure scenario and is not an acoustic quality or network-loss test.

## Measured scope and remaining work

Target PSRAM free before/after cleanup was 8,386,192 bytes. Internal free heap
changed from 374,499 to 374,479 bytes: a 20-byte residual remains unattributed.
Main-task free-stack watermark was 7,336 bytes. Standalone measurements exclude
network/display/audio-peripheral resource peaks and do not prove combined
headroom or leak-free repeated session teardown.

The target encoder's `get_info` returns no codec-specific description. Exact
encoder lookahead and capture tail publication must be established before
bidirectional audio integration. The service still only exposes normal packet
submission; producer discontinuities and grouped terminal tails need an API.
Production host pacing, security/renewal/reconnect, audio owner/DMA cancellation,
the Ultra BSP and full VoiceWatch shell remain outstanding.

`snapshot.json` records relevant source, artifact and evidence hashes. The
on-device image hash matches `ultra-result.json`. Prior endpoint/transport
evidence remains historical; do not treat these codec fixtures as a replacement
for real network/audio or final product acceptance tests.

## Reproduction

From the library root, normal host checks are:

```sh
make -C tests/host -j4 test
make -C tests/audio test oracle
```

Use Linux with a C compiler, Python 3, pkg-config and libopus-dev for the
sanitizer runs (`make -C tests/host sanitize`, `make -C tests/audio sanitize`).
The target image and safe app-only flash procedure are documented in
`examples/twatch_ultra_audio/README.md`. Native adapter build and matrix commands
remain in `tests/interop/README.md`.
