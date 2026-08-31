# Stream-credit dormancy and full-shell regression — 2026-08-31

This checkpoint addresses the remaining retry behavior in R4. It does not close
all nine findings, the application-group impairment matrix, or product release.
The full implementation plan and its original acceptance gates remain in force.

## Reproduced defect and correction

With a real Quinn peer, a 3,072-byte old stream exhausts its 1,024-byte receive
window while connection credit remains available. The adapter previously
skipped it only within one send pass, retrying it on later polls without new
credit. Both resume and reset fixtures recorded **401 unnecessary blocked
writes during a 250 ms unread hold**. Fresh traffic still completed; this is a
reproduced retry defect, not evidence that the original adapter starved it.

The adapter now remembers stream-level blockage across polls. Only ngtcp2's
matching `extend_max_stream_data` callback makes that send half eligible again.
ACKs, timers, and credit for another stream do not wake it. Reset still retires
the stream normally; stopped/retired halves ignore late credit. Submitted bytes
retain their existing ACK/close ownership. Stream, payload and packet limits,
media deadlines, congestion control and audio buffering are unchanged.

New diagnostics distinguish block episodes, matching wakes and the most recent
blocked stream. The endpoint snapshots these on its owner and the Ultra emits
numbers at existing diagnostic boundaries. They are not audio-loss counters.

## Evidence and scope

- [Before results](before-results.json) preserve both failures and hashes of the
  instrumented before binary/logs. The exact before source snapshot was not
  captured, so no source hash is claimed. The peer's old success message covered
  its traffic checks; the client and harness correctly failed idle suppression.
- [Native results](native-results.json) contain 44 peer cases across two ordinary
  iterations and 22 under UBSan, plus two allocator self-tests per suite. All
  six blocked cases pass: 128 fresh streams each way and 128 control exchanges
  finish while the old stream is unread, zero retries occur during the hold,
  and a 129th stream completes after old-stream retirement. Resume delivers all
  3,072 bytes; reset reports code 77. TLS live blocks/bytes return to zero.
- [Native verification](native-verification.json) records passing portable
  host/audio/adapter tests, including 200,000 adapter stream lifetimes, 384 Python
  tests and 46 native/firmware protocol checks. ASan remains unpassed.
- [Source snapshot](source-snapshot.json) binds the dirty source, real peer,
  native/UBSan clients, full-shell image/ELF and active configuration by hashes.

The blocked fixtures use **synthetic raw QUIC payloads**, not Hang audio or
MoQ application groups. They prove real stream-credit scheduling and cleanup,
not deliberately blocked on-device audio, calibrated latency or Wi-Fi quality.
Normal raw-stream turnover additionally covers 60,000 streams per direction
across the ordinary matrix and 30,000 per direction under UBSan.

## Physical full-shell check

[Firmware evidence](firmware.json) records flash46's app0-only installation and
successful unchanged shell heartbeat. The image is 3,426,192 bytes in the
4 MiB app partition; live identity, security, partition and OTA selection checks
preceded the write. Other partitions were not written.

[Hardware results](hardware-results.json) preserve p115: the provider bench
passes text output, background speech, three zero-error microphone turns and
idle reconnect. All five identity-bound WSS completion receipts require exact
sample totals and follow speaker DMA drainage. Five final serial completion
markers independently match the expected sample counts in order: **181,538
samples**. The host starts and shuts down cleanly, with no firmware fault marker.

**Serial statistics coverage is incomplete.** Only four of the five detailed
`playout samples` summaries appear; a fragment remains in the third response's
diagnostic block. The original summary-match field stays false. The separate
five-marker match is true; it is not a substitute for missing quality counters.
The four available summaries contain three concealed chunks, four late chunks
and one silence chunk, and additional fault snapshots exist for the response
whose summary is absent. No complete-run quality total or audibility verdict is
claimed. This run was not repeated to replace the observation.

Ten transport snapshots report zero flow blocks/wakes, so this physical run does
not exercise the deliberate native block condition. TLS peak is 49,414 bytes,
with no tracked denial/system failure; ngtcp2 peak is 30,344 bytes. These do not
close the full device-memory gate. Permanent enrollment is revision 169 with
fresh readiness verified, and persistent service processes remain unchanged.

## Remaining acceptance

Occasional concealment and late packets are expected possibilities on Wi-Fi;
the same audio interval may count in both. Zero PLC is not a product acceptance
prerequisite. Intelligibility, full response/tail delivery, bounded latency and
recovery matter. Earlier strict diagnostic and critical-phrase failures remain
failed; later successes do not replace them.

Physical application-group blocking, full UI/package and sleep/wake interaction,
calibrated latency, complete SRAM/headroom stress, original endurance/reference
cases, ASan/CI and the remaining speech impairment failures are still open.
No reference pin, production host or default transport changes; no commit/push.
No default-firmware preservation or restoration is required.
