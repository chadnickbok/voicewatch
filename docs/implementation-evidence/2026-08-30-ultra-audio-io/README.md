# Physical Ultra audio and MoQ echo — 2026-08-30

The optional Ultra BSP and reference echo crate now pass together on the
connected watch. This is a short calibration diagnostic, not PTT, speech
quality, production security, relay deployment or full-shell acceptance.

The combined image first captures 1.2 seconds of local acoustic calibration:
1 kHz, 2 kHz, cancel, stale-owner rejection and silent restart. It then captures
19,200 microphone samples, uploads 61 Opus packets through MoQ, receives the
reference decoder/re-encoder's 61 paced packets and plays 19,200 samples.
The reference uses unmodified pinned public audio/session APIs. It is a direct
reference connection using a public test path, not a production relay test.

- `ultra-serial-sanitized.log`, `ultra-result.json`, `peer-result.log` record
  both device and host passes. No raw microphone/decoded PCM is included.
- `acoustic-analysis.json` verifies both frequencies, clipping and silence
  following cancellation/restart. Mic drops and callback faults are zero;
  320 queued samples were cancelled and the measured stop fence was 633 us.
- `echo-analysis.json` verifies exact sample counts and codec conformance.
  43 startup samples differ by one signed-16-bit LSB, RMS 0.0473; all other
  samples match exactly. The verifier requires maximum one-LSB disagreement
  and the existing eight-LSB RMS ceiling. It explicitly reports non-bit-exact
  output. A preceding run had 39 one-LSB differences; its initial strict byte
  comparison failure is retained privately, not hidden or called a pass.
- `native-results.jsonl`: all 18 existing real QUIC matrix cases give expected
  outcomes, including eight intended security rejections. This rerun uses the
  new Rust dependency graph and the previously built unchanged native C client.
- `runner-tests.log`: eight offline app-only flash safety tests pass.
- `snapshot.json` binds source and artifact hashes to this checkpoint. Later
  board/full-shell work must be validated separately. Actual GitHub CI
  execution is not claimed; a compile-only board IO job was added.

The speaker uses a 480-sample FIFO and two 10 ms DMA buffers; invalidation
rejects handoffs and clears pending PCM, while stop disables/zeros DMA before
restart. Completed stats count actual DMA completions. The extra FIFO half-frame
joins the shortened pre-skip output to the next packet without inserted silence.
Microphone capture retains four 10 ms chunks with cadence-derived timestamps.

The combined echo phase took 6,397 ms including connection/capture/playback,
not a conversational latency measurement. Minimum internal free RAM was 97,900
bytes. At success, internal free was 163,055 and PSRAM free 7,601,072 bytes;
free stack watermarks were 41,160 audio / 7,400 network / 2,868 DNS. Overall
cleanup and heap integrity passed. This run does not log after-cleanup heap
totals, and neither it nor the earlier raw-stream soak proves leak-free repeated
audio sessions. Display/Wasm/full-shell memory remains to be measured.

The app-only runner verified live board/security/partition metadata and wrote
app0 only. No charging setup, package/NVS erase, partition table change or
firmware restoration occurred. The new installed firmware hash is
`5ad5e674f59fa13011efb526be42afbe0a5622caf62ec97b6512fc8f5e72d7c8`.
Private headers, firmware, raw logs and ambient WAVs remain outside Git.

Reproduction is documented in the library's Ultra transport README (`--echo`)
and `server/echo_oracle/README.md`. Remaining work includes the complete Ultra
BSP and shell, explicit PTT, production authentication/host, real speech,
long responses, sustained operational audio and impairment/reconnect gates.
