# Full-shell MoQ media checkpoint — 2026-08-30

The physical Ultra now runs the native shell, resident WAMR recovery guest,
existing package filesystem and Wi-Fi while its new MoQ audio owner performs a
real microphone → reference decode/re-encode → speaker exchange. This is an
explicit private diagnostic using the internal media seam, not the public
Voice Orb/PTT or production live-agent conversation path.

The latest image is `e78688cae157f1187591f9835a941ef1c438d90d0aafa918fac11c86543cb56c`.
Only app0 was written. The runner verified the live board/security/OTA/partition
metadata, left the new firmware installed and required no restoration.

## Results, including failed attempts

| Run | Media result | Minimum internal RAM | Maximum observed flush | Maximum observed render |
| --- | --- | ---: | ---: | ---: |
| 1 | Passed | 95,040 B | 510,059 us | 1,172,034 us |
| 2 | Failed before capture: codec initialization race | Not an audio measurement | — | — |
| 3 | Passed; CPU1 worker affinity and endpoint configuration in PSRAM | 86,528 B | 8,404 us | 290,598 us |
| 4 | Passed; optional service control arena also in PSRAM | 114,792 B | 8,031 us | 291,299 us |

Run 1 established the integration and exposed both startup starvation and
insufficient internal headroom. Run 2 rejected a connection requested before
codec initialization finished. The API now queues a connection during that
initialization and rejects/drains grants if initialization fails. Run 3 reduced
the flush stall but still failed the 96 KiB memory floor. Run 4 meets that floor
in this short diagnostic; its minimum largest internal block was 81,920 bytes.

All passing exchanges have exactly 19,200 samples each way and correct terminal
group boundaries. Run 4 has zero concealed, late or pressure-discarded player
frames, zero microphone drops and 19,200 physically completed speaker samples.
It reports completion after DMA drains, then exercises idle cancel/disconnect
and remains quiet through a one-minute shell interval. This is not a cancel
while speaking, repeated-session race or long-duration leak test.

The independent PCM comparison found 38 one-LSB differences in run 4, RMS
0.04449 LSB, with exact lengths. The established limit is 8 LSB RMS plus a
stricter per-sample maximum of one LSB for this echo check. The result is not
bit-exact and does not establish acoustic speech quality. No calibration tones
were added in this shell test; the earlier acoustic diagnostic remains separate.

Free stack watermarks in run 4 were 43,532 audio / 7,248 network / 2,888 DNS bytes.
After endpoint cleanup, internal free RAM was 217,695 bytes; codecs and the
permanent audio owner remain allocated. No repeated-cycle leak attribution is
claimed. The connect request to both-direction catalog readiness took 4,476 ms;
this is not an isolated QUIC+SETUP measurement and does not meet/prove the planned
connection latency gate. WSS/bootstrap memory and active Voice Orb animation
load are absent. The 291 ms initial render also requires further UI work and
matched interaction measurements; no final UI budget is declared passed.

## Build and regression evidence

- `moq-elf-selection.json`: MoQ endpoint/capture symbols are linked; WebRTC peer
  open/send symbols are absent from the Ultra ELF.
- `legacy-build-result.json`: the extracted CoreS3 WebRTC selection builds and
  links, contains peer open/send and no MoQ endpoint. Image size 3,017,328 bytes,
  SHA `b34e09348a5878d9bb1297675684753f0b4f72f026011524ca79c3f4fa00fa31`.
  It was not flashed. An initial attempt used an obsolete private FATFS
  configuration and failed the existing long-filename guard; rebuilding with
  the current defaults' heap-based 255-character filenames passed.
- `library-host.log` and `library-audio.log`: current portable/core/service/
  endpoint tests, nine decode fixtures and twelve capture fixtures pass.
- `native-matrix.log`: all 18 real native interoperability/security cases pass,
  including expected certificate/hostname/ALPN rejections.
- `live-agent-tests.log`: 97 existing live-agent tests pass, with four warnings
  (three deprecations and an unawaited test coroutine). No host source changed;
  this does not validate a MoQ live-agent adapter, which is still missing.
- `storage-tests.log`: five existing firmware-storage checks pass.
- `verifier-tests.log`: five artifact-verifier tests pass, including rejection
  of missing/duplicate PCM, wrong identity/tail and out-of-tolerance samples.
  These are evidence-parser tests, not new audio-task race coverage.

`runN-serial.log` excludes raw PCM, device identifiers and network credentials.
Analysis files bind the private raw artifacts by hash. The snapshot binds the
latest firmware to 102 source hashes plus binary/map/ELF hashes. The physical
peer uses the unmodified pinned Rust MoQ/audio APIs at
`eb5776e21eeaecba8e844be53c821895c178bcaf`; its listening/auth wrapper is the local
public-token test harness. GitHub CI execution is not claimed.

## Remaining scope

Production HTTPS/WSS bootstrap, scoped credentials, trusted-time policy,
refresh/backoff and the bounded Rust/Python live-agent bridge are incomplete.
The normal MoQ build therefore refuses anonymous legacy control discovery.
The default Ultra profile still disables voice; the private CMake opt-in selects
this diagnostic. No anonymous fallback or TLS-validation bypass was added.

Complete the public control/response binding flow, real STT/model/tools/TTS,
PTT/Voice Orb, guest/owner switching, optical/touch/wake validation, installed
packages, interruption/reconnect races, long responses, impairment and sustained
acceptance gates before declaring a WebRTC replacement ready. Build-time legacy
DTLS/dependency conditionalization remains incomplete despite absent peer symbols
in the MoQ ELF. See `docs/moq-shell-media-seam.md` and the complete implementation
plan; this checkpoint does not close the goal.
