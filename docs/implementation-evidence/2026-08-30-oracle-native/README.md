# Oracle and native transport checkpoint

These results accompany `../../moq-implementation-progress.md`. `snapshot.json`
records exact source and ESP32 build hashes. The native JSONL files are complete
matrices: 36 macOS cases (three repetitions), and 12 Linux cases with the full
C dependency chain instrumented using ASan/UBSan. Negative cases intentionally
report `pass:false` and `connected:false`; the runner validates rejection and
its diagnostic, rather than treating that field as the matrix result.

Every positive raw-QUIC connection carries 10,000 eight-byte streams per
direction and 65 peer bidi replies. This is not an audio workload or a 30-minute
single-connection soak. MoQ interoperability here means SETUP/path exchange
with the unmodified pinned server only. The public `jwt` path string is not
an authentication test. No physical handshake, audio, or full shell is claimed.

The oracle records separate fixture/provenance checks and honest exclusions.
The Linux portable/adapter regression log includes real instrumented binaries
and 31,806 deterministic mutation inputs. About 300,000 portable assertions
are varint roundtrips, not separate end-to-end scenarios.

The ESP32 ELF verifier confirms the active TLS/handshake symbols; these
artifacts use public dummy build configuration and were not flashed. Native
TLS and ESP-IDF TLS profiles differ, as recorded in the provenance files.
Actual GitHub CI execution has not been performed.


The separate Ultra memory probe was flashed into only the existing app0 region
(0x10000, sector-aligned span 0x60000), leaving bootloader/partitions/NVS/user-data
regions untouched. `ultra-probe-serial.log` and `ultra-hardware-result.json` record
the physical memory/TLS-context result. The original region was then restored;
the full 16 MiB flash digest matched the private backup before and after the
experiment. This is no network or audio result. The backup stays outside Git.
