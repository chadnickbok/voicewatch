# Capture failure isolation and recovery

Work after VoiceWatch `01cfd20` and moq-esp32 `666f5a2`. The full replacement
plan remains open; these are targeted recovery results.

## Change

p56 exceeded the 200 ms concealment budget with eleven missing groups. Its
watch log also reports publisher-cache drops overlapping that span, but does
not identify the cause of every missing group. Rather than expand the audio
budget, the native actor now reports identity-bound `capture.failed` for loss
exhaustion. It stops that decoder and rejects late end/playback callbacks while
retaining the authenticated connection. Malformed media, authorization and
IPC/transport errors remain failures. A fresh capture requires a new identity.

Python retires queued PCM/response work, cancels watch capture and tells the
conversation to cancel without STT completion. Watch failure receipts include
capture/request/owner identity and `start_id`, including failure before capture
starts. Delayed failures cannot cancel a new turn. Firmware and host updates
must be coordinated; uncorrelated failure receipts are rejected.

Native and provider benches can inject a bounded uplink blackout independently
of seeded packet loss/RTT. The acceptance gate requires an explicit loss-budget
abort, no partial completion and fresh audio on the same session within ten
seconds of network restoration. Failed gates remain failed. Microphone PCM is
never persisted; native-only replies are generated tones, not acoustic echo.

## Hardware results

| Run | Configuration | Result |
| --- | --- | --- |
| t61 | Flash24; 800 ms uplink blackout, no other induced impairment | Pass. Capture aborts; fresh PCM arrives 231 ms after restoration on the same session. Three fresh 1.2-second captures and replies complete, plus cancellation/replacement and expiry reconnect. Four 16,037-sample tones have zero pressure, silence, concealment or late packets. |
| p57 | Flash24; same blackout through the real provider pipeline | Fail. Failed capture produces zero STT commits; conversation returns to ready. On the next capture the watch reports protocol failure / `INVALID_STATE` and disconnects. No complete provider turn passes. |
| p58 | Flash25 diagnostics; 800 ms blackout, no other induced impairment | Pass. No partial STT commit; fresh audio resumes 721 ms after restoration. Three complete provider turns and reconnect pass. The watch fault does not recur. |
| p59 | Flash25; blackout plus 5% loss, 120 ms added RTT, seed 52 | Fail. Capture abort succeeds but the next turn hits watch protocol `INVALID_STATE`. Diagnostics identify a write to peer-stopped stream 17. |
| p60 | Flash26 stopped-stream fix; same requested impairment as p59 | Fail overall: outage recovery takes 923 ms and two full provider turns complete, but the third capture exceeds the loss budget. It aborts without another STT commit or watch disconnect. No endpoint failure is logged. |

t61 drops 28 uplink datagrams during the blackout. The failed capture contributes
8,536 received samples; the three successful captures contribute 57,600. Its
native reader reports a missing prefix from group 28 to 60, which exceeds the
unchanged concealment budget. This is three host-driven synthetic round trips,
not physical-button PTT, the required 1,000 echo cycles, or speech-quality proof.

p57 observes a larger missing span, aborts without an STT commit and then fails
on the watch with endpoint failure 6, result 9. The host receives QUIC close code
4. That diagnostic identifies a watch protocol-layer failure, but not yet its
internal call site. This does not prove p50's earlier fault has the same cause.

## Stopped-stream correction

p59 narrows the watch fault to service step 3, adapter write site 11, detail
1026 on stream 17: connected adapter, existing stream, send half already stopped.
The adapter treated this as `INVALID_STATE`; the TX scheduler propagated it as
a connection failure even though other streams could continue.

An established peer-stopped send half now returns `NOT_FOUND` from write/FIN.
The scheduler cancels only its queued job, resets the remaining stream half and
emits one cancellation receipt. A reset reporting an already-retired stream is
also terminal success for cleanup. Backend-owned bytes remain held until ACK or
definitive close. Invalid connection state and other transport errors still
fail. Adapter and TX regressions fail before this correction and pass after it;
tests cover write/FIN/priority failure, independent progress and no duplicate
terminal receipt after eventual stream retirement.

p60's two responses contain 39,939 and 41,149 samples, each with five concealed
and five late packets, zero pressure and zero silence. The initial failed capture
and third-turn failure produce only two STT commits in total. Capture failures
occur at 13,161 and 38,097 ms; disconnection occurs at 39,678 ms during bench
cleanup, not as a watch endpoint failure. The required three successful turns
do not pass, and this result does not establish acceptable speech quality at
5% loss/120 ms added RTT. The p50 cause remains unproven.

Fresh authenticated capture identities now also reach the conversation/STT
boundary. A new capture clears unfinished provider audio even if the old failure
callback was superseded. The regression submits old PCM, skips the old failure
callback, starts a new identity and verifies clear/new-audio/one-commit ordering;
duplicate starts stay idempotent.

The initial isolation change passes 24 Rust tests and Clippy, 304 Python tests
(four warnings), six native integration cases, 26 firmware-parser cases and the
firmware build. The native actor regression exercises an eleven-group gap,
control liveness, stale callbacks, exact fresh capture and malformed-media
rejection. The final identity regression raises the Python count to 305 (four
warnings). Normal adapter and host suites and the corrected firmware build pass;
Linux normal and ASan/UBSan adapter/host suites also pass. Test counts, scope
limits and log hashes are recorded in `verification.json`.

Only whitelisted counters and source/binary hashes are included. Private keys,
tokens, profiles, transcripts, raw logs and microphone PCM remain excluded.
Firmware is written only to app0 and left installed; restoration is not needed.
Provider recovery, the original loss pattern, the full impairment/quality matrix,
allocation limits, deployment/default switch, full physical shell interaction,
latency and endurance acceptance remain open. WebRTC is still the default.
