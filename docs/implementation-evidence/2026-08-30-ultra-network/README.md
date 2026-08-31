# Ultra network and protocol-runtime checkpoint

This directory contains sanitized evidence, not a release-readiness claim.
Every completed temporary physical run restored app0 and verified the complete
16 MiB against the private backup. No microphone, speaker, display, touch or
power control was initialized. The full VoiceWatch shell is still untouched.

## Physical reproductions and short runs

| Files | Result |
| --- | --- |
| `initial-authentication-failure.*`, `initial-peer.log` | Peer rejected authentication of the first QUIC Initial; no verified target connection |
| `accelerated-aes-failure.*` | Independent vectors fail nonempty AES-GCM and AES-ECB under the accelerated profile; HKDF/empty GCM pass |
| `software-aes-short.*`, `software-aes-peer.log` | Software AES passes known answers and altered-tag rejection; verified TLS/ALPN and reference SETUP; 500 raw streams each way |
| `reset-control-short.*`, `reset-control-peer.log` | Same, with a reset each way, three ephemeral bidi replies and two replies on a control stream held open across the workload |

`*.json` records private firmware hashes and full-restoration success. Serial
logs retain only known test-tag lines; identifiers and other raw boot/Wi-Fi
logs are excluded. No backup, private configuration header, real firmware,
private ELF or raw credential-bearing build artifact belongs in this directory.

The public crypto vector generator uses cryptography/OpenSSL and reproduces the
checked bytes exactly. The ESP32 test uses the pinned wolfSSL APIs underlying
ngtcp2, including reused and in-place GCM contexts, direct GCM, ECB header
protection, HKDF and invalid authentication tags. No verification was disabled.

The short-run handshakes take approximately 3.4–3.8 seconds and miss the proposed
500 ms target. Minimum internal heap on the extended short run is 147,928 bytes;
the stack watermark is 3,788 bytes. These do not establish full-shell headroom.

The physical binaries predate the subsequent optional extended-close callback,
which adds metadata to deferred events. Current-source host/native tests and
public-dummy cross-builds cover that addition; physical release gates will need
the final candidate image.

The completed `long-soak.*` evidence records 90,000 synthetic streams each way,
180 resets each way, 182 ephemeral bidi replies and 181 persistent-control round
trips. Target elapsed time was 1,803.668 seconds; peer workload time was 1,800.022
seconds. Both processes exited successfully and independently report pass.
Minimum internal heap was 149,024 bytes and stack watermark 3,788 bytes.
After adapter/context teardown, free internal heap was 267,123 bytes and free
PSRAM 8,386,076 bytes. This workload still excludes audio, UI and shell resources.
The old runner restored once at exit; per user direction, restoration is not an
acceptance gate and future tests leave the new firmware installed.

## Host verification

`dispatcher-linux-sanitizers.log` runs the actual portable core, dispatcher and
TX queue under GCC ASan/UBSan, with separate normal and instrumented binaries.
The 300,621 original assertions remain dominated by varint round trips. New
dispatcher tests use the checked pinned Rust fixtures and cover classification,
fragmentation, interleaving, errors, bounds and 10,000 group retirements. New TX
tests cover copying, partial writes, FIN, scheduling, deadlines and cancellation.

`adapter-close-linux-sanitizers.log` tests the actual adapter source with mocked
ngtcp2/TLS behavior (real loopback UDP where applicable), including zero-code
cancellation versus clean close and preservation of both half-stream errors.
Its 200,000 simulated lifetimes are not physical QUIC stream counts.

Native matrix and build records are listed in `checkpoint.json`. Negative
security cases intentionally show `pass: false` for the connection attempt;
the matrix passes only if they reject for the expected diagnostic. No operational
pub/sub, Hang catalog/audio, production authorization, reconnecting service or
full-shell interoperability is claimed by these tests.
