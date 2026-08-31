# Terminal-reader adoption — 2026-08-31

The user approved adopting the reviewed patch and committing/pushing the work.
The Rust host now selects `moq-net` and unchanged `kio` from the library's
`third_party/moq-rust`, based on upstream
`eb5776e21eeaecba8e844be53c821895c178bcaf`. The exact patch is
`14d48121c8039da548d82086122d0e80e3e2cc0734a46b87cee2dce45f521239`.
Source hashes, both upstream licenses, locked dependencies and CI checks are
included. A fresh committed source archive resolves the adopted host graph.

This is an explicitly permitted downstream patch, not an upstream release or
unchanged-reference conformance. The original peer/oracle remains independent,
and its known terminal failure remains recorded. The patch changes receive
ownership and completion, not wire formats, authentication or watch-side memory.
An [upstream proposal](../../moq-terminal-reader-upstream-proposal.md) is prepared
but has not been submitted.

## Ten-minute physical session

The normal optimized host binary, SHA-256
`c1ad00fec8f364ce1968a1e81f231f96620e513b4b6d83490d82efcbf5c9255e`,
passes a **600,002 ms** authenticated session on unchanged normal flash49:

- Three spaced real microphone → STT → read tool/model → TTS → speaker turns,
  with zero word errors on the fixed six-word fixture in every completed turn.
- All three playback completion receipts, totaling **127,079 speaker samples**.
- One cancelled capture whose delayed real STT final is rejected during the
  replacement capture; no cancelled transcript reaches downstream processing.
- Four same-session credential renewals and monitored idle between turns.
- A deliberate fresh-grant reconnect afterward, without microphone activation.
- Normal service shutdown, no firmware fault and no microphone PCM saved.

Total bench duration including startup/reconnect/shutdown was 620,671 ms.
The Mac played a fixed generated fixture near the watch; output volume was
restored. This includes monitored idle, not ten minutes of continuous speech.
Controls came from the host test driver, so physical button/app/sleep-wake
acceptance remains separate. No packet impairment, long endurance or firmware
restoration was run or added as a gate.

Minimum observed internal free RAM was **84,604 bytes (82.6 KiB)**. The existing
provisional 96 KiB resource target remains unmet; no allocation failure occurred.
Adopting this Rust fix does not resolve or hide that separate budget issue.

## Deployment

The same tested binary and current Python runtime are installed under the
existing supervised MoQ service. Its ports, database, certificate/root trust and
device key mapping are preserved. Startup verifies the binary hash, and a fresh
watch session was observed with no capture event. The previous native/config
generation remains available for deliberate rollback. Legacy WebRTC service PID
2759 remained alive and was not changed.

Temporary bench enrollment was replaced with the permanent profile at revision
**187**. There was no firmware flash or factory restoration. Both upstream Rust
licenses now accompany the deployed generation. Deployment/process receipts are
numeric and credential-free; private profiles and logs remain outside Git.

## Verification

- 39 patched-host tests and all-feature Clippy pass.
- 853 moq-net plus 61 kio unit tests pass from the vendored workspace.
- The adopted peer passes 176 integration cases with the native UBSan client:
  22 ordinary cases, 100 engine exchanges, 27 delayed-reader cases and 27
  lifecycle cases, plus constructor/TLS self-checks.
- 434 Python tests pass (four existing warnings); the rebuilt native/firmware/
  supervisor integration lane passes 62 tests.
- Library host CI passes at commit `5a76f48`, including the full native
  **ASan/UBSan** lane and adopted-receiver lifecycle matrix. This closes the
  prior lack of a working Linux sanitizer executor for that committed code.

The first firmware CI run at `5a76f48` separately exposed unresolved TLS
allocator hooks in crypto-only smoke and memory-probe links. The complete Ultra
transport/audio jobs passed. The correction puts the allocator object in the
wolfSSL archive, matching the component that requires its ABI, instead of
relying on a transport object to force the linker to pull it in. This is an
ESP-IDF link correction; it does not change the tested Rust host binary or the
installed watch image. Its follow-up build receipts are recorded separately.
The correction is committed as `3793523`; local smoke, memory-probe and full
public-shell builds pass. The shell image is 3,363,648 bytes, leaving 830,656
bytes in its app partition, and passes the release-image inspector. None of
these public test images was flashed. Follow-up library CI is running at this
checkpoint; the earlier successful host run remains identified by its exact
commit above.

Physical button/touch/app/sleep-wake observation, the internal-memory budget and
library root/core distribution licensing remain open product work. Initial
acceptance does not require an upstream merge, impaired-network matrix or an
eight-hour run. Existing failed/historical evidence is retained.
