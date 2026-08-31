# Endpoint engine checkpoint

These results exercise the new portable endpoint engine, not Hang/Opus audio or
the full VoiceWatch product. Source/object/executable hashes and exact arena
sizes are in `checkpoint.json`.

- `linux-host-sanitizers.log`: actual core, dispatcher, TX and engine sources,
  normal binaries plus separate GCC ASan/UBSan binaries. Engine tests include
  fragmented control/media, owned data, serving-cap updates, early media/END,
  trailing whole groups after control closure, cancellation and roles. A
  10,000-position receive run has 9,000 frame groups and 1,000 separate DROP
  intervals; 1,000 peer subscription lifetimes preserve ID-reuse rejection.
  The older 300,621-check count is mostly varint rounds, not distinct scenarios.
- `native-matrix.jsonl`: 13 cases on macOS using actual ngtcp2/wolfSSL, including
  raw turnover, certificate/identity/ALPN negative cases, unmodified MoQ SETUP
  and operational pub/sub. Negative connection results are intentionally false;
  the harness requires the expected rejection diagnostics.
- `native-engine-repeat.jsonl`: three additional fresh engine exchanges for each
  of DNS, IPv4 and IPv6. The unmodified pinned Rust endpoint and C engine each
  deliver eight timestamped synthetic frames, an empty group and END. Rust
  publishes an acknowledgement only after validating the C publication; C
  requires that acknowledgement and both clean receive endings. The fixture
  path/token is public test data and is not an authorization test.
- `clang-analysis.log`: zero diagnostics from Clang static analysis of the
  engine. An empty log is expected; this does not prove absence of defects.
- `ultra-public-build.log`: ESP-IDF compiles the engine and the public-dummy Ultra
  transport image links. The transport example does not call the engine, so its
  code can be discarded from the final image. The compiled engine object hash
  is recorded separately. This is not on-device engine evidence.

The independent [physical transport soak](../2026-08-30-ultra-network/long-soak.json)
also finished successfully. That firmware predates the engine and the extended
close metadata; its heap results cannot be applied to this complete runtime.

Remaining gates include protocol hardening/priority propagation, catalog/audio
handling, cross-task ownership and media epochs, authenticated recovery, host
integration and the full Ultra shell. No GitHub workflow execution is claimed.
