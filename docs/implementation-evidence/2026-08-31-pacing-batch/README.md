# Media admission and QUIC pacing checkpoint

Two reproduced scheduling defects are corrected. This checkpoint is not
acceptance of the full WebRTC replacement.

## Changes and regression evidence

Expired media previously occupied TX jobs until the end of the engine poll,
delaying fresh admission and required DROP notices by another owner iteration.
The engine now prunes obsolete media before scheduling new work. This bounded
pass performs resets and retirement callbacks only; it does not open streams,
write payloads, submit FINs or expire controls. Blocked resets retain ownership,
and late acknowledgements cannot duplicate terminal events. The strengthened
engine regression fails before the change and passes after it, including a
blocked control stream.

The ngtcp2 adapter previously advanced the pacing timer after every packet,
prematurely closing a multi-packet allowance. It now commits the timer once per
bounded batch, respecting ngtcp2's byte quantum and the existing 16-packet cap.
A retained datagram counts toward the next batch's limits. The regression
reproduces the old one-packet behavior and verifies batching, byte limits,
packet limits and socket backpressure. The 64 by 256-byte payload pool, 200 ms
live-media deadline and 200 ms capture-loss budget are unchanged.

Normal host and adapter tests, Linux ASan/UBSan host and adapter tests, and both
ESP-IDF firmware builds pass. The adapter suite includes 200,000 stream
lifetimes. Raw validation logs remain private; hashes are in
[verification.json](verification.json). Remote CI has not been verified.

## Physical results

Flash33 contains media pruning; flash34 adds pacing batching. Both app0-only
installations reached steady state. Flash34 remains installed. Firmware
restoration is not required. The native endpoint retains the corrected C Opus
decoder from the [preceding checkpoint](../2026-08-31-capture-plc-quality/README.md).

| Run | Configuration | Result |
| --- | --- | --- |
| p72 | Flash33; 5% bidirectional loss, 120 ms added RTT, 800 ms uplink blackout | FAIL. Intentional failed capture aborts without STT commit; recovery 788 ms. One full provider turn passes with zero word errors and 8,584 concealed samples out of 60,320. The second capture exceeds its loss budget; the third is not attempted. |
| p73 | Flash34; same impairment settings | FAIL. Recovery 695 ms. The next capture has 10,560 concealed samples out of 60,480 and two word errors, exceeding the fixed one-error gate. Playback completes, but no full provider turn is accepted. Remaining turns are not attempted. |
| p74 | Flash34; no induced impairment | PASS. Three complete provider turns, each with zero word errors, a fresh watch read and completed playback; idle provider reconnect also passes. Capture has zero concealed, lost or late groups; playback has zero concealment, lateness, pressure or silence. |

p74 captures contain 52,160, 60,960 and 60,480 samples; speaker receipts contain
43,570, 38,728 and 42,360 samples. Total run time is 53,563 ms. The Mac's output
volume and mute setting were restored and independently checked afterward.
Only numeric counters and fixed fixture scores are published in
[hardware-results.json](hardware-results.json); no ambient microphone PCM,
provider transcripts, credentials or raw bench databases are included.

The impaired runs use seed 52, but different packet timelines prevent treating
them as identical loss traces. These results do not demonstrate an improvement
in impaired speech quality from either scheduling fix. Hard-network speech
reliability and the wider impairment matrix remain open, as do full physical
shell acceptance, resource limits, long-duration tests and deployment gates.

## Source binding

[prune-source-snapshot.json](prune-source-snapshot.json) and
[batch-source-snapshot.json](batch-source-snapshot.json) bind the tested source
files, native endpoint and firmware to their hashes. The batch snapshot also
records the ELF hash and linked-symbol checks. Installation metadata is in
[prune-firmware.json](prune-firmware.json) and
[batch-firmware.json](batch-firmware.json). Historical snapshots intentionally
retain the source hashes from their respective builds.
