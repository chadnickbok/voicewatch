# Receiver delivery timing and contiguous response pacing

The previous 600-second renewal run completed its exact speaker receipt but
failed clean-audio acceptance. This checkpoint investigates that failure without
changing the zero-PLC/late/pressure/fallback-silence gate. It is not completion of
the library-candidate or product migration plan. No supervised host rollout,
reference pin change, default switch or firmware restoration is performed.

## Diagnosis

Wi-Fi modem sleep was already disabled in the shared station initialization.
Flash42 adds eight retained numeric snapshots at the first player degradations,
plus maximum frame-delivery/audio-step intervals. They are emitted only after
speaker drainage, without per-packet logging or retained PCM. End-of-playback
endpoint timing and ngtcp2 allocation snapshots now follow the sample receipt.
The bench waits for that final snapshot; startup-only heap evidence cannot stand
in for a completed long-response observation. Host read timing and its first
eight intervals of at least 30 ms are retained without packet contents.

r6 uses the unchanged 60 ms prebuffer and old host pacing. It completes 1,919,877
samples in 120,176 ms through five renewals, but fails with seven PLC chunks and
seven late-packet counts. No fallback silence or queue overflow occurs. Host PCM
intervals peak at 26.313 ms with no reanchor; 6,000 packets span 119,981.691 ms.
The watch's audio steps remain within 10.195 ms, but delivered-frame gaps reach
95.789 ms. The first empty-queue concealment follows 66 ms without a frame. This
localizes the immediate player starvation to frame delivery, rather than a
stalled audio owner or accumulating host PCM pacing drift in that run. It does
not distinguish native sender, QUIC processing and wireless network delay.

The plan allows a 60..80 ms startup target. A new library API selects that range
only after begin and before media is queued. Default remains 60 ms; every new
begin resets it. Rejection is non-mutating, active clocks cannot be moved, and
the ten-packet/200 ms queue, staleness bound, PLC limit and cancellation are
unchanged. Flash43 selects 80 ms for the Ultra. A real-Opus regression translates
the first measured burst into a short deterministic fixture: 60 ms conceals and
drops late audio, while 80 ms yields byte-for-byte reference PCM and exact tail.
The fixture models observed delivery timing, not an unobserved network cause.

r7 shows that prebuffer tuning alone is insufficient. It completes the same
sample count in 120,216 ms through five renewals but still fails with one PLC
chunk and one late packet. Its maximum frame gap is 90.115 ms; audio-owner steps
remain within 8.299 ms. A host scheduling slip at 82.695 seconds permanently
reanchors the old pacer. Its 6,000 PCM packets span 119,996.081 ms instead of
119,980 ms. The native producer uses contiguous sample timestamps, so this extra
wall time consumes receiver headroom instead of advancing the media timeline.
The first player fault occurs later, at 105.144 seconds. This identifies a real
pacing mismatch; it does not prove the slip alone caused that physical fault.

## Pacing correction

MoQ now requests a contiguous per-response pacer. It keeps the original sample
deadlines across small scheduler slips and catches up at no more than one packet
per 10 ms, rather than emitting an unbounded burst or moving every subsequent
deadline. Falling more than 200 ms behind cancels the current response through
the normal WSS/IPC media cancellation path, while preserving its authenticated
session. A replacement response owns a fresh pacer object anchored on its first
read after media readiness. An old pacing await cannot modify that clock.
The existing WebRTC pacer and its wall-time reanchoring behavior are unchanged.

Tests exercise all 30,000 packets of a simulated ten-minute response with
repeated 15 ms scheduler slips, contiguous timestamps, bounded packet spacing
and no accumulated drift. Other tests cover oversized pacing debt, cancellation
while awaiting a deadline, old/new clock isolation, and response-local overrun
cancellation followed by usable replacement media without session revocation.

## Verification limits

The corrected physical r8 completes all 9,599,877 samples in 600,206 ms through
27 renewals on the same session, with zero microphone samples. Its 30,000 host
PCM packets span 599,981.998 ms, approximately two milliseconds beyond the
599,980 ms nominal first-to-last interval, despite a maximum individual interval
of 68.363 ms. No host reanchor, pacing overrun or session fault occurs. The new
pacer therefore avoids accumulated scheduling drift in this physical run.

**Clean long playback still fails:** three PLC chunks and three late-packet
counts, with zero pressure and fallback-silence chunks. Two concealed chunks
occur near 216.4 seconds, following up to 99.870 ms without a frame while the
audio task continues stepping approximately every millisecond. Another occurs
near 343.9 seconds, close to the observed host scheduling slip. Maximum delivered
frame gap is 107.010 ms; maximum audio-step gap is 9.737 ms. This run does not
prove clean playback, nor does it isolate the native/network/QUIC cause of the
remaining delivery gap. The earlier 600-second run had 91 PLC chunks, but these
are separate uncontrolled network runs, not a calibrated causal A/B comparison.

The final endpoint snapshot is present: ngtcp2 allocation peak 30,344 bytes,
24,092 bytes live at completion, 131,072-byte cap, zero denials and zero platform
allocation failures. This is the endpoint allocator's lifetime peak, not a TLS
or total-device heap bound. Neither clean-audio thresholds nor the maximum
startup/queue targets are raised further to pass this result. The next diagnostic
needs correlated native-send and adapter-receive timings around the remaining
delivery gaps; a larger buffer alone would conceal the uncertainty.

All 242 combined Python/firmware-protocol tests and six real-native integration
cases pass after the pacing change. The 68 targeted pacing/session/legacy tests
overlap that total and must not be added to it.

The full C host and real-Opus audio suites pass. The player also passes UBSan.
The combined ASan/UBSan player executable stalled before `main`; a sampled stack
shows recursive allocation during Apple ASan shadow-memory initialization and
its static spin lock. That specific live process was stopped after inspection.
This is neither a passing ASan test nor an application-code failure. A local
Linux attempt found a cached sanitizer image, but Docker left its environment
probe in `created` state with PID zero and no start/error timestamps. It never
executed even the compiler-version probe. Only that container and its pending
client were removed; the daemon and other containers were not restarted. Full
Linux sanitizers and remote CI remain open. The dynamically linked system Opus library
is not itself rebuilt with sanitizers by the local player command.

Both flashes are app0-only and pass the full-shell heartbeat. The benches use
generated quiet marker/silence PCM and never open the microphone. Exact speaker
receipts and player counters are not an acoustic quality measurement or a count
of hardware DMA underruns. Physical PTT/navigation/apps/packages/sleep-wake,
complete impairment and security matrices, allocation enforcement, latency,
reference interoperability, long soaks, release and default-cutover gates remain
separate requirements.

Permanent enrollment is reapplied at revision 135 with unchanged identity, roots,
key and endpoint, followed by a fresh permanent-service ready event. Flash43
remains installed. The persistent MoQ and legacy processes retain PIDs 45731 and
2759; neither is restarted or redeployed. Their running code is still the prior
deployment, so these pacing changes require a later coordinated host rollout.

See [hardware results](hardware-results.json), [firmware](firmware.json),
[verification](verification.json) and [source snapshot](source-snapshot.json).
