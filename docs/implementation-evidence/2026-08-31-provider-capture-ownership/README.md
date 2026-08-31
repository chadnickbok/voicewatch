# Provider capture ownership

Recorded 2026-08-31 against Voicewatch base `a1a28d3` and unchanged moq-esp32
`127eda5`. This implements the downstream cancellation boundary that the
previous STT checkpoint explicitly left open. It does not complete the
WebRTC-replacement acceptance plan.

## Reproductions and changes

The baseline allowed an old model tool callback to execute after cancellation
and a replacement capture. A delayed TTS start could also reactivate the sink
and write audio. Synthetic reproductions each observe one unauthorized late
operation. The pinned ElevenLabs dispatcher has a further race: cancellation
while awaiting audio append can let the old event overwrite shared partial-word
alignment belonging to a replacement. These outcomes are recorded in
`regression-before.txt`.

The MoQ pipeline now uses the capture-aware user/assistant aggregators and
provider adapters. Input aggregation preserves the capture on model context;
model requests, parallel/sequential tool runners, result callbacks and TTS
contexts retain that origin. Queued response frames cannot acquire the identity
of whichever capture is current when they arrive. Model send/receive handling
retains the pinned cancellation/drain protocol, including response events already
read during cancellation. Retired provider cache entries cannot replace locally
admitted history on a new turn.

Tool admission and results are checked before and after asynchronous work.
The MoQ writer also rechecks a captured predicate before sending a queued action,
then rejects its pending future if it was cancelled. A write already started
may have completed on the watch; interrupted history explicitly records that
uncertainty. Accepted durable jobs are not undone by foreground cancellation.
Read-only queries may be repeated, but mutations require checking current state
before retrying. The foreground instruction now requires a fresh read when the
user asks for current watch/workout state.

TTS IDs map to their originating captures, with bounded ownership records.
The receiver uses the pinned pronunciation/alignment helpers while keeping each
context's partial word and time state separate. It checks ownership again after
audio append. Retired events cannot recreate audio contexts or modify replacement
alignment. The sink checks ownership across device-stop and playback-drain waits,
stamps derived bot-speaking frames, and commits text only after matching playout.
Focused confirmations and natural-pause speech carry their admitted origin too.

Normal turns keep their existing pipeline, provider connections and conversation
history. WebRTC retains its original provider services and aggregators. No
managed Pipecat package or firmware was modified for this checkpoint.

## Physical runs

The bench uses the real provider pipeline, read-only watch tools, the physical
Ultra microphone and speaker, and the installed flash18 full-shell image. These
are host-driven capture tests, not physical PTT-button acceptance. It writes only
its authorized enrollment namespace; it does not flash or restore firmware.

Tool faults hold a real watch read result before delivering its original guarded
callback. TTS faults hold one real start frame and one bounded provider audio
frame. Each fault cancels the capture, starts a replacement, and then releases
the held work. No transcript, tool result or synthesized response is fabricated.

| Run | Scenario and version distinction | Result |
| --- | --- | --- |
| p34 | Initial integrated pipeline, three normal turns | Pass; three tool/playback turns and reconnect |
| p35 | Delayed real tool result, then three fresh turns | Pass; stale callback rejected |
| p36 | Delayed real TTS start/audio, with writer and cache guards | Pass; old TTS output rejected |
| p37 | Tool repeat with additional context-frame and receive guards | Fail; stale callback rejected, but the next model reply omitted the required fresh read |
| p38 | TTS repeat with explicit spoken-history assertions | Pass; three fresh replies each added one played assistant message |
| p39 | Tool repeat after fresh-read instruction and per-context alignment | Pass; stale callback rejected, three fresh reads/playbacks/history additions and reconnect |
| p40 | Final code, including explicit origin scope for externally delivered live callbacks | Pass; delayed TTS frames rejected, three fresh reads/playbacks/history additions and reconnect |

The six successful runs complete eighteen fresh provider turns and play 743,107
samples. Final p40 records 250,880 microphone samples, including 67,680 in the
cancelled capture, and 118,606 played samples across its three fresh responses.
It completes in 60,740 ms with four STT commits, zero STT provider errors and a
fresh-session reconnect. Independent traces contain four finals/read-tool
completions but only three TTS starts, as expected for the held TTS fault.
Each successful response adds exactly one played assistant-history message.

The failed p37 remains in the evidence. Its fresh capture was transcribed and
produced speech, but no new read tool ran, so the bench correctly failed. There
was no provider/transport exception in that run. The freshness instruction was
tightened instead of relaxing the required tool check. These bounded runs do not
prove universal model compliance with that instruction.

See `hardware-results.json` for exact samples/durations, per-run stage files for
independent pipeline counts, and the numeric capture/publisher/native counters.
Earlier runs precede the final external-callback scope correction; final source
hashes and run distinctions are recorded in `source-snapshot.json`.

## Verification and limits

Tests cover stale tool admission/results, admitted durable work, external delayed
callback origin, queued action cancellation, response-cache retirement, model
send/receive cancellation, history and audio fences, TTS context recreation,
interleaved word alignment and cancellation during alignment delivery. The full
Python suite passes 261 tests with four existing warnings; five native integration
cases also pass. Results are recorded in `verification.json`.

Public evidence contains only fixed stage/tool names, numeric values and
booleans. Held provider data remains in memory and is released on completion or
cleanup. Ambient microphone PCM is never persisted. Raw logs, credentials,
enrollment profiles, databases and synthetic fixture audio remain private. The
bench restores Mac output volume/mute; default watch firmware restoration is
not required.

Remaining gates include text/background response authorization before a capture,
long-response/lease renewal, controlled packet-loss recovery, allocation limits,
adversarial security cases, deployed-service recovery, physical controls/apps/
sleep-wake, latency and endurance. This checkpoint does not justify switching
the board default from WebRTC to MoQ.
