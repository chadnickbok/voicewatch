# Bounded QUIC heap, catalog fallback and terminal ordering

This checkpoint validates the default 128 KiB per-connection ngtcp2 allocation
budget in the full Ultra firmware, repairs catalog fallback, and improves END
ordering. A later repeated engine test still fails; terminal interoperability
and the complete WebRTC replacement remain unaccepted.

## Changes and regression evidence

The allocator charges aligned headers and transient old-plus-new realloc
storage, preserves old allocations on failed growth, and distinguishes budget
denials from system allocation failures. Endpoint snapshots expose numeric
live/peak bytes and allocation counters to the firmware. Scope excludes system
allocator bookkeeping, fixed adapter pools, socket buffers, wolfSSL/crypto
storage and native Rust host allocation. A retained closed-endpoint snapshot is
not a post-destruction leak measurement.

The plain-catalog test expected a generic stream error removed by the earlier
reset-isolation fix. It now checks the service's actual fallback counter: exactly
one for a plain-only peer, zero otherwise. A new service regression then exposed
a real decoder bug: switching to plain after an applied compressed snapshot
retained the old compression/frame state. Fallback now resets that state. The
regression fails before the reset fix and passes afterward, for both raw and
mapped not-found/unsupported errors. Failure of the plain track remains visible
and does not cause an endless retry.

An intermittent engine failure delivered only the final empty group before the
reference receiver stopped. The pinned model declares completion when END and
the highest sequence are present; it does not wait for every earlier sequence.
The C publisher had sent END before all outstanding media retired. It now waits
for retirement or abandonment, with DROP queued before END. A regression holds
an older group while retiring the newest and verifies no END bytes appear until
the older group retires. The pre-fix assertion fails. Normal newest-group
priority, bounded deadlines, wire encoding and the pinned dependency are unchanged.
The reference test receiver still validates every expected group exactly once
using `recv_group()`; no payload/count checks were removed.

The END change is not a complete fix: an additional engine-only run passed 15
times, then failed with only groups 5–8 (three frames and the empty final group).
The failure remains authoritative even though 30 further runs pass after adding
numeric sender-retirement diagnostics. Those passing runs report all 11 groups
(two catalogs plus nine audio groups) submitted and retired without expiry,
cancellation, failure or cache drop. The failure did not recur in that diagnostic
run, so its sender-retirement state is not yet known. Investigating reference
group ingestion versus END/FIN processing remains the next interoperability task.

## Verification

- The production adapter and all seven host suites pass normally and under
  Linux ASan/UBSan, with sanitizer errors configured to halt execution.
- Real pinned ngtcp2 construction tests fail each of seven allocation points and
  sweep 77 insufficient budgets. Every unwind/deletion leaves zero live bytes
  and blocks. The constructor peak is 19,556 bytes on macOS and 19,668 under the
  Linux sanitizer build. This test exercises no TLS or networking callbacks.
- Three complete native interoperability runs pass all 54 cases. Each run
  includes 10,000 raw streams per direction and 65 peer bidi replies separately
  for hostname, IPv4 and IPv6, plus audio, SETUP, engine, service, endpoint,
  plain-catalog and certificate/ALPN rejection cases. Expected TLS rejection
  cases have `client.pass=false` but `case_pass=true` in the normalized results.
- The complete ESP-IDF 5.5.5 Ultra firmware builds and is written only to app0
  after chip, security and partition/OTA-selection checks. No firmware restore,
  NVS erase or package/user-storage write is performed.

See [verification.json](verification.json) for scopes and private-log hashes,
[native-matrix.json](native-matrix.json) for each native result, and
[source-snapshot.json](source-snapshot.json) for source/build binding.
The subsequent failure and diagnostic repeats are recorded separately in
[tail-repeat.json](tail-repeat.json); the earlier 54 passing cases do not prove
stable terminal delivery. The diagnostic rebuild did not change device firmware.

## Physical results

The flashed image is flash35, SHA-256
`a686b4ab18b6d0e6cf8f89784357b57fd7f1ac1dda06e9ea2267b7c9d4829b02`.

| Physical workload | Completed round trips | Sampled ngtcp2 peak | Capture samples | Outcome |
| --- | ---: | ---: | ---: | --- |
| Startup on persistent service | No capture requested | 30,344 B | 0 | Connected |
| Clean network, five 1.2 s captures and generated replies | 5 | 30,344 B | 96,000 | Pass |
| 5% configured UDP loss, 120 ms added RTT, three captures/replies | 3 | 34,216 B | 57,600 | Pass |

All allocator samples stay within 131,072 bytes with zero budget denials and
system failures. Every completed generated reply has exactly 16,037 samples and
zero playback queue overflow. Clean replies have no late/concealed packets.
The impaired run has late packets and concealment; successful synthetic tones
are not speech-quality acceptance. Each run also verifies cancellation with a
fresh replacement response, forced reconnect and fresh-grant lease renewal.
Forced reconnect takes 5,342 ms clean and 6,093 ms impaired. These are single
observations, not latency percentiles or the required sub-500 ms handshake gate.

Only complete contiguous display-memory records are summarized; truncated or
interleaved serial records cannot contribute partial numeric values. Across the
audio runs the reported internal heap low-water is 101,052 bytes, minimum sampled
free internal memory is 136,007 bytes, and minimum sampled largest internal block
is 62,464 bytes. Minimum sampled PSRAM free is 7,003,108 bytes. These short tests
do not prove worst-case device headroom or allocator cleanup across release soaks.

The first bench launch used a 104-byte IPC socket pathname and failed validation
before installing enrollment or capturing audio. A short private output path
resolved the harness invocation issue. The clean run preceded the bench's new
explicit heap gate, so its counters were independently extracted afterward.
The impaired run includes the explicit `--max-quic-heap-bytes 131072` gate.
Both harness hashes and the preflight failure are retained in
[hardware.json](hardware.json).

After the isolated benches, the watch is re-enrolled to the persistent service
with revision 110 and reaches media readiness without capture. It retains the
new firmware. The MoQ supervisor and legacy WebRTC service retain their original
PIDs throughout; no host deployment or default-transport switch occurs.

## Remaining acceptance work

The remaining terminal-delivery race, TLS/crypto and native-host hard allocation limits, broad allocation-exhaustion
recovery, impaired speech accuracy, the complete loss/RTT/burst/reorder matrix,
long responses across renewal, physical PTT/touch/navigation/apps, package and
sleep/wake parity, frame and connection latency budgets, 1,000-turn/eight-hour
soaks, broader physical security negatives, licensing/distribution and remote CI
remain open under the full replacement plan. No milestone is redefined here.

Private keys, enrollment identities, provider credentials, raw serial/native
logs, transcripts, databases and firmware binaries are not published. Microphone
PCM is counted and discarded; the speaker fixtures are generated tones.
