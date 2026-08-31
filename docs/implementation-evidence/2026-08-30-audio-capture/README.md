# Capture and real-Opus network checkpoint — 2026-08-30

The portable capture pipeline, pinned target encoder and atomic MoQ publication
boundaries are implemented. A real-Opus exchange passes on the connected Ultra
against the unmodified pinned Rust audio Consumer and Producer. This remains
synthetic PCM with no microphone/speaker, production authentication or shell.
Neither library-candidate nor product acceptance is complete.

## Implemented behavior

- Fixed existing VoiceWatch profile: 16 kHz mono, 20 ms, 24 kb/s VOIP, complexity
  0, VBR on, FEC/DTX off. The caller-owned capture arena is 3,384 bytes; it buffers
  at most 320 partial PCM samples and two encoded packets.
- Fragmented PCM uses capture timestamps; write accepts everything or nothing.
  Pump preserves identical packet bytes across WOULD_BLOCK. Explicit epoch
  changes reset the encoder and publish an empty group before subsequent audio.
- Finish drains partial input and codec delay. The endpoint marker and up to
  two final packets form one atomic service submission/group. An expired tail
  reports failure, and a failed capture sink cannot later report finish success.
- Catalog removal now represents unavailable discovery, rather than cancelling
  a bound range. This fixes loss of queued terminal audio when the upstream
  Producer finishes and removes its rendition. New receives wait for a usable
  catalog; format changes and source restarts still invalidate old media.

## Evidence

- `host-core.log`, `host-audio.log`: strict host compilation and tests, including
  capture fragmentation, retries, atomic tail pressure/deadline failure, held
  leases across catalog removal, and the prior ownership/format regressions.
  Both Rust fixture sets regenerate byte for byte using locked dependencies.
- `linux-tests-sanitizers.log`: normal and ASan/UBSan core/endpoint/player/capture
  suites pass. The linked system libopus itself is not instrumented. An earlier
  staging attempt omitted the oracle JSON prerequisite; it failed before tests
  and is not used as evidence. The corrected staging run is retained here.
- `native-results.jsonl`: all 18 real QUIC cases complete with expected outcomes,
  including the eight intended certificate/ALPN rejection rows. The new
  `moq-audio` case sends fragmented capture through the actual owned endpoint.
  The unmodified reference Consumer emits 537 PCM samples in chunks [216,320,1],
  equal to direct reference decoding of the uploaded packets. It sends a
  reference Producer fixture back; the C player checks all 537 output samples.
- `ultra-network-serial.log`, `ultra-network-result.json`, `ultra-peer.log`:
  the same test passes using physical Espressif codecs and Wi-Fi. The peer
  publishes its response catalog only after installing upload subscriptions,
  and sends response audio only after validating the complete upload. The
  catalog owner remains alive across audio finish, using upstream public APIs.
- `target-encoder-serial.log`, `target-encoder-analysis.json`: 40 physical target
  packets across 12 synthetic cases independently decoded on the host. All
  reference alignment lags are zero. Long impulse/tone/noise signals peak at
  exactly 104 samples of delay in both target and queried reference streams.
  This establishes pre-skip 312 at 48 kHz, not speech quality or equal encoder
  bytes (29 packets differ). Maximum observed target encoding time was 6,845 us.
  These diagnostics were collected before tightening the standalone probe's
  final return guard; the independent verifier checks every case explicitly.
  The current combined network image separately verifies the configured delay
  and terminal lengths through the actual capture/service/Consumer path.
- `runner-tests.log`: offline safety tests for app0-only flashing. No firmware
  preservation or restoration is required or performed. The current combined
  network/capture/player image remains installed, SHA-256
  `1058c73d4709b40f7c9df84c1f297d688817c7688c07196ae97414700fdac129`.

The initial 16 KiB standalone encoder task overflowed/corrupted memory. The
diagnostic was moved to a guarded 64 KiB task; this is consistent with the
existing VoiceWatch audio task's stack budget. The combined test uses a 64 KiB
PSRAM audio stack and independent internal network/DNS stacks. No internal
codec structs or unofficial lookahead accessors are used.

## Combined physical measurements

| Measurement | Bytes |
| --- | ---: |
| Internal free before audio/endpoint creation | 266,739 |
| Minimum free internal RAM | 106,164 |
| Internal free at successful exchange | 171,075 |
| Internal free after joined cleanup | 266,439 |
| PSRAM before / after cleanup | 8,386,076 / 8,386,076 |
| PSRAM at successful exchange | 7,643,172 |
| Audio / network / DNS stack free watermarks | 41,624 / 7,404 / 2,876 |

The exchange took 4,362 ms including connection setup; this is not a handshake
or end-to-end voice latency acceptance result. RX and TX high-water marks were
2 packets each. Heap integrity passed. The 300-byte internal residual is not
attributed and repeated session teardown remains required before a leak-free
claim. There was no I2S, display, touch, Wasm, AI service or full-shell load.

## Remaining scope and reproduction

Next is the Ultra microphone/speaker BSP, explicit PTT ownership and bounded DMA
cancellation, then real echo and long responses. Production bootstrap/auth,
scope isolation, renewal/reconnect, the Rust/Python host boundary, complete
VoiceWatch shell and final sustained/impairment/latency gates remain open.

From the library root, run `make -C tests/host test`, `make -C tests/audio test
oracle`, then `tests/interop/build.py` and `tests/interop/run.py`. Linux sanitizer
commands are `make -C tests/host sanitize` and `make -C tests/audio sanitize`.
The physical mode is documented in `examples/twatch_ultra_transport/README.md`:
peer `hardware-audio`, private header `--audio`, and a 120-second app-only runner.
Private headers, full logs and credential-bearing firmware stay outside Git.

`snapshot.json` hashes current source and final artifacts/evidence. Historical
receive-only and raw-transport snapshots are preserved as separate checkpoints.
