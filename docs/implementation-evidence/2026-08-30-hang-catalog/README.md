# Hang catalog implementation evidence

This checkpoint implements and tests bounded audio catalog JSON, snapshot/merge
patches, rendition selection, raw DEFLATE and catalog/Hang framing over native
QUIC. It does not complete the WebRTC replacement or full Ultra shell objective.
See `snapshot.json` for source and executable/firmware hashes.

Verified against the final source snapshot:

- macOS host regressions pass, including catalog bounds, Unicode/duplicate keys,
  merge rollback, unsupported renditions, 5,120 full-size random compressed
  frames, dictionary reset/wrap and per-byte snapshot mutations.
- Linux GCC normal and ASan/UBSan suites pass. The vendored zlib code is also
  sanitizer-instrumented. Sanitizers are separate executables, not normal-test
  binaries accidentally reused.
- Clang analysis of catalog, JSON, compression and engine sources produces no
  diagnostics. `static-analysis.log` is intentionally empty.
- The pinned oracle reproduces 76 saved cases and provenance: 66 Rust-to-C and
  60 C-to-Rust wire/frame cases, 10 invalid C cases, 585 truncated inputs,
  68 incremental fragmentation cases and 66 catalog sequence frames.
- The 13-case native QUIC/security/SETUP/engine matrix passes. The engine case
  exchanges both catalog formats, three incoming snapshot/delta frames per
  track, eight Hang-framed synthetic payloads per direction, an empty group,
  clean subscription ends and a peer acknowledgement that requires successful
  C publication validation.
- The public-config Ultra ESP-IDF build passes. It compiles the new source but
  does not invoke the catalog/engine APIs; unused code may be discarded at link.
  Its example partition layout is not an authorization to flash over the live
  Ultra partition table. No firmware was flashed or restored in this checkpoint.

All 75 final-artifact native repetitions pass: 25 each for DNS, IPv4 and IPv6.
Results are in `native-repeats.jsonl`; each row is a new
connection and certificate. The native peer uses public unmodified pinned
`moq-net`, Hang and `moq-json` APIs. The low-level fixture oracle separately uses
the previously documented visibility-only modifications for private codecs.
Those two provenance claims must not be confused.

One earlier catalog exchange ended with a peer `dropped` error. Subsequent
repetition runs did not reproduce it, but its cause is not classified as fixed.
A separate initial missing-track failure was traced to test-peer object lifetime:
the broadcast indexes tracks weakly, so finished catalog producers now stay alive.
The pinned peer also translates application stream errors through WebTransport's
error range even in raw QUIC mode. The forthcoming service must translate that
boundary while preserving wire diagnostics and cancellation code presence.

The payload bytes are synthetic, not valid audio test material. No Opus quality,
PTT, jitter/PLC, cross-task cancellation, production authentication, relay fan-out,
host service, screen/touch/audio BSP or full-shell readiness is established.
Host arena sizes are documented in the library's `docs/hang-catalog.md` and
`docs/endpoint-engine.md`; they exclude the rest of the firmware.
