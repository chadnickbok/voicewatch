# Queue clocks, owner timing and a rejected packet-budget experiment

Flash40 fixes a demonstrated media queue-age defect and adds bounded numeric
timing diagnostics. It passes one three-turn impaired provider run and one clean
run. This does not establish full impairment acceptance or complete the WebRTC
replacement plan. A five-millisecond packet-budget experiment failed physical
checks and is **removed from the current library and installed firmware**.

## Correct queue age across owner pauses

The service stamped incoming publication packets with `s->now`, the last network
owner tick. A producer still running during an owner pause therefore backdated
new audio. The regression queues one packet, advances producer time by 210 ms
without stepping the owner, queues fresh audio, then resumes the owner. The old
packet must expire while the fresh packet survives. The former implementation
discards both and fails the regression. A second case covers the atomic two-packet
terminal tail, which could otherwise fail a healthy publication as stale.

Service configuration now requires a nonblocking monotonic-millisecond clock
callback and optional context in the same clock domain as `service_step`.
The endpoint supplies `esp_timer`; the standalone native caller and host fixtures
are updated. The callback runs under the service mutex and must not reenter it.
This is an API configuration requirement for direct service callers, not an
optional fallback to the previous owner-clock behavior.

Queue insertion and expiry checks use current time. RX queue and held-lease ages
also advance when the network owner is paused; additional tests verify expiry
without an owner step and continued ownership of expired leased bytes until release.
No media PTS is treated as wall time. The 200 ms queue policy, stale-tail failure,
eight-frame TX queue, sixteen RX slots, cache and transport deadlines are unchanged.
No hot-path allocation is added. Current macOS arm64 service arenas are 29,552
bytes core and 580,608 bytes media; clock configuration adds sixteen core bytes.

Host and actual-adapter tests pass, as do all 18 ordinary native interoperability/
security cases and the allocator self-test. Service UBSan checks pass with the
unchanged zlib objects uninstrumented. Full fresh ASan/UBSan remains outstanding;
this partial verification does not replace it. The ordinary native matrix does
not close the separate unchanged-reference delayed-tail defect.

## What the timing diagnostics establish

The endpoint retains the slowest ready-state loop's wall-time breakdown: service,
transport and other work. Associated transport counters cover socket wait, RX
including its callbacks, TX pumps, and callbacks outside RX. The snapshot includes
the board's monotonic timestamp, resets per attempt, and is logged only at existing
diagnostic boundaries. No per-packet logging or audio retention is introduced.

The arithmetic is checked in the exported results: service + transport + other
equals the loop gap, and the transport subphases fit within transport duration.
These measurements include task descheduling and are **not CPU-time attribution**.
The counters add 32 bytes to the fixed adapter (30,808 bytes on macOS arm64), without
growing its stream or payload pools.

The timing-only flash38/p82 run passes three impaired provider turns but records
a 156,928 us maximum loop gap. In that loop, TX accounts for 100,164 us and RX
for 39,735 us. This identifies the phases in which time elapses, but does not
separate crypto work from preemption by other tasks on the core.

## Rejected five-millisecond experiment

Flash39 combined the queue-clock correction with elapsed-time checks between RX
and TX packet operations. Synthetic expensive-packet tests proved early yield,
continued packet progress and preserved ACK ownership; native tests also passed.
Physical acceptance did not:

- p83, with impairment: the first capture loses 70 groups and conceals
  22,080/58,560 samples. Five word errors and a missing critical phrase fail the
  speech gate. No full turn is accepted, despite a lower 74,808 us maximum loop gap.
- p84, clean: one full turn passes; the second capture has 19 lost groups and one
  word error, failing the zero-error clean gate. Its maximum loop gap is 73,363 us.

The experiment is removed. Its exact code/test patch is retained as
[rejected-five-ms-experiment.patch](rejected-five-ms-experiment.patch), which
passes `git apply --check` from the current library checkout. It is evidence,
not an enabled feature or recommended integration patch. The associated sources,
binary hashes and native results remain in [experiment-source-snapshot.json](experiment-source-snapshot.json)
and [owner-budget-native-matrix.json](owner-budget-native-matrix.json).

The measurements do not isolate causality: physical packet timelines differ
even with the same seed, and flash39 also contained the clock correction.
The final clock-only firmware passes the subsequent checks. These results justify
withholding the five-millisecond proposal; they do not prove a universal timing
threshold. CPU/time-bounded scheduling with adequate throughput remains open.

## Final physical checks

| Run | Firmware and network | Result |
| --- | --- | --- |
| p82 | Flash38, timing only; 5% loss, 120 ms added RTT, seed 52, 800 ms uplink outage | PASS: three full turns, zero word errors; outage recovery 836 ms. |
| p83 | Flash39, clock + five-ms experiment; same impairment | FAIL: no full turn, 70 lost groups, five word errors. |
| p84 | Flash39; no induced impairment | FAIL: one full turn, second turn has one word error. |
| p85 | Flash40, clock + diagnostics, established packet limits; same impairment | PASS: three full turns, zero word errors; outage recovery 655 ms, same-session capture recovery and provider reconnect. |
| p86 | Flash40; no induced impairment | PASS: three full turns, zero word errors, no lost/late capture groups or concealed samples, and provider reconnect. |

p85 reports 5, 7 and 13 lost groups, with 1,600/58,400, 2,240/58,880 and
4,160/58,240 samples concealed. Successful recognition does not mean lossless
audio. Its maximum owner gap remains 159,513 us and the sampled ngtcp2 heap peaks
at 48,416/131,072 bytes. p86's maximum owner gap is 161,101 us despite clean audio.
No timing limit or full device-memory guarantee is inferred from these samples.
All full turns require the real provider pipeline, a fresh read-only watch tool,
and exact speaker receipts. No speech threshold, codec setting, reorder wait or
concealment budget was relaxed after failure.

Flash40 is a 3,429,680-byte full Ultra shell image, SHA-256
`c32c54d20029ea75d4ba1d016b268dbcd844cdfccf5663ba502c382e4d27b78f`.
All three writes verify identity, chip/security state, flash geometry, partition
layout and OTA selection; write only app0; and pass the shell heartbeat. The
final firmware remains installed. No NVS, bootloader, OTA metadata, package or
user-data erase or firmware restoration is performed.

The benches restore Mac volume/mute and retain no ambient PCM. Raw logs, credentials
and binaries stay private. Permanent enrollment is reapplied at revision 125 with
the same identity, roots and key, followed by a fresh permanent-service ready event.
Both persistent Mac services keep their existing processes. No production host
deployment, compatibility-pin change or reference-candidate rollout occurs.

See [hardware-results.json](hardware-results.json), [firmware.json](firmware.json),
[verification.json](verification.json), [source-snapshot.json](source-snapshot.json),
and [queue-clock-native-matrix.json](queue-clock-native-matrix.json). Prior checkpoints
and failed outcomes are preserved.

## Remaining full-plan gates

The next work must establish repeatability and complete the loss/RTT/burst/reorder/
duplication/flow-control matrix, while investigating backend execution versus
task-preemption time without sacrificing packet throughput. One passing fixed
impaired case is not full impaired-speech acceptance. Unchanged-reference terminal
interop, TLS/native allocation bounds, security negatives, 600-second responses,
proactive renewal, physical PTT/navigation/installed-app/package/sleep-wake behavior,
latency, 1,000 interactions, eight-hour soak, fresh sanitizers, remote CI and
distribution review remain open. WebRTC is still the configured default.
