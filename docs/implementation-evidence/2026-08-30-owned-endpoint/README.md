# Owned service and Ultra operational endpoint evidence

This snapshot covers the portable owned media service, native error mapping,
subscription-priority propagation and task-owning endpoint, including its
physical Ultra synthetic exchange. See [implementation progress](../../moq-implementation-progress.md)
and [service contracts](../../../libs/moq-esp32/docs/owned-service.md).

- `snapshot.json` hashes source files, test artifacts and evidence. The private
  target image hash is cross-checked against the app-only runner's written image.
  Private binaries/configuration and raw logs are not copied here.
- `host-tests.log` and `linux-tests-sanitizers.log` contain final normal and
  Linux ASan/UBSan results. Endpoint fault tests use the actual threaded wrapper
  with controlled DNS/UTC and an adapter stub. Adapter tests compile the actual
  adapter source with mocked ngtcp2 calls and real loopback UDP.
- `native-results.jsonl` contains three complete 17-case matrices (51 cases).
  Real ngtcp2/wolfSSL and unmodified pinned Rust APIs are used. Negative security
  rows intentionally have `pass:false`; the harness verifies the expected TLS
  rejection and zero media, so those rows are passing rejection tests.
- `ultra-public-build.log` proves the active endpoint path links for ESP-IDF
  using public dummy inputs, not just that unused objects compile. This public
  image was not flashed. Its example partition table is not the live Ultra map.
- `ultra-operational-serial.log`, `ultra-peer-result.log` and `ultra-result.json`
  record the passing physical run. The app-only runner validated the existing
  layout and wrote app0 at 0x10000, leaving bootloader/NVS/OTA metadata and other
  partitions untouched. No firmware backup or restoration was required or run.
  The new image remains installed.
- `clang-analysis.log` is empty: analysis of endpoint/service/TX reported no
  diagnostics. `runner-tests.log` records eight offline checks.

The physical run negotiated catalogs and verified eight synthetic Hang frames
per direction with response group range [5,13). It completed in 3,884 ms.
Minimum free internal RAM was 107,440 bytes. Network/DNS stack watermarks were
7,392/2,868 bytes. After joined destruction PSRAM exactly matched the pre-create
8,386,076 bytes; internal RAM was 228 bytes below its initial value. That small
remaining difference is not yet attributed or proven stable across many cycles.

Two earlier target attempts failed before media exchange: arena alignment
validation, then invalid pthread configuration restoration. Both were corrected
before this passing run. Their images and logs remain in the private hardware
test directory; they are not erased from the test history. The host test token
and USB-provisioned time are fixtures, not production authentication.

Not established here: actual Opus/audio quality, authenticated bootstrap/scope
policy, secure reconnect/credential refresh, jitter/PLC, long operational MoQ
soaks under impairment, the production host service, or the full VoiceWatch
Ultra shell. The earlier raw transport soak is separate historical evidence.
