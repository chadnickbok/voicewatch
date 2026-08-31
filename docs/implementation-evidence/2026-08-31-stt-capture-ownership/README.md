# STT capture ownership and cancellation

Recorded 2026-08-31. This checkpoint fixes a reproduced stale-transcript routing
bug and validates the STT cancellation boundary with synthetic provider events
and the physical Ultra. Complete model/tool/TTS generation isolation and the
remaining WebRTC-replacement release gates are still open.

## Failure and implementation

At Voicewatch `bed685a`, a final transcript from an abandoned capture reached
focus routing after cancellation and a new capture. The router only checked
whether the watch session was retired. Clearing the provider's input buffer
could not invalidate a transcription already being processed.

The MoQ-specific `CaptureRealtimeSTTService` now binds the item ID from the
provider's commit acknowledgement to the capture that issued that commit.
Transcription completion order is not assumed: the provider documents using
item IDs to associate results with committed input. See the official
[transcription event guide](https://developers.openai.com/api/docs/guides/realtime-transcription#handle-transcript-events)
and [commit acknowledgement schema](https://developers.openai.com/api/reference/resources/realtime/server-events#input_audio_buffer.committed).

Only one commit can await acknowledgement. Cancellation leaves that pending
receipt associated with its original, invalidated capture; it cannot become
the next capture's receipt. The previous-item chain detects inconsistent
acknowledgements, and five-second configuration/commit waits fail closed by
retiring the affected capture and socket. One pending receipt, one current
item and one prior item ID bound the correlation records; they hold no audio
or transcript. Unknown, cancelled and duplicate completed items are discarded.

Capture identities are stamped on PCM and boundaries when they enter the
pipeline, and on provider transcripts only after item-ID validation. The router
checks that identity before and after asynchronous callbacks. Control
cancellation invalidates it before awaiting device stop; a new listen intent
invalidates the previous turn before its new capture receipt arrives. Socket
loss retires the old capture, and reconnection does not replay its queued PCM.

The STT resampler also needed a capture boundary. Its stream previously retained
history and did not flush its final samples at commit. The MoQ adapter retains
VHQ conversion but resets history on each capture and flushes the remaining
samples before a valid commit. Scheduling pauses do not clear buffered samples.
Synthetic tests check exact converted durations, chunked versus whole input
within the two-LSB tolerance of independent int16 conversion, and no old signal
in the next silent capture. WebRTC retains its existing STT/VAD behavior.

Normal captures do not recreate provider connections, model context or the
conversation pipeline. This is an extension of the pinned Pipecat 1.7.0 STT
service; the receive dispatcher consumes acknowledgements that the base service
otherwise only logs. No provider package or managed dependency was edited.

## Physical results

All runs use the already-installed flash18 full-shell image, SHA-256
`9126220122365874058091c140e33d6c1a5954dabe125b8c1b89c5166155e308`.
No firmware was flashed, and preserving/restoring default firmware is not
required. Capture is host-driven, not a physical PTT-button acceptance test.

| Run | Scenario | Microphone samples | Played samples | Result |
| --- | --- | ---: | ---: | --- |
| p30 | Three complete provider turns | 181,440 | 125,869 | Pass, including fresh-session reconnect |
| p31 | Cancel one delayed STT final, then three fresh turns | 241,120 including 60,800 cancelled | 128,289 | Pass; exactly one stale completion rejected |
| p32 | Repeat after moving cancellation ahead of device-stop wait | 239,680 including 58,560 cancelled | 121,027 | Pass; exactly one stale completion rejected |
| p33 | Final code, including interim-callback recheck | 240,640 including 60,320 cancelled | 127,079 | Pass; exactly one stale completion rejected |

The cancellation bench speaks the existing generated fixture into the watch's
microphone and holds one actual provider completion event in memory. It then
cancels the capture, starts a replacement capture and delivers the held event
to the same STT adapter. The old event produces no admitted final, tool call or
playback. Each subsequent turn requires a fresh 33-character STT result, the
fixture check, real `get_next_set` completion and a newer response's speaker
receipt. No transcript, tool result or TTS response is fabricated.

`p32` completes three fresh captures of 60,640, 60,640 and 59,840 samples, with
37,518, 39,939 and 43,570 samples played under response IDs 1, 2 and 3. It records
four commits, three admitted completions, one rejected completion, zero provider
errors and a successful fresh-session reconnect. Total duration is 58,053 ms.
The final `p33` repeat takes 59,342 ms and passes the same checks. Independent
pipeline traces for all four runs contain exactly three routed finals, three
read-tool completions and three TTS starts, confirming that the rejected event
did not create an extra downstream turn. Mac output volume/mute is restored by
each bench run and was checked again after the last run.

## Verification and privacy

234 Python tests pass with four existing warnings. Five native integration
cases also pass. The new checks exercise delayed acknowledgements on either
side of cancellation, reordered and duplicate finals, stale queued frames,
cancellation during resampling and transcript callbacks, malformed/missing
acknowledgements, failed writes, configuration timeout, and actual JSON receive
dispatch after reconnection. Legacy STT filtering and cancellation ordering
remain covered. The original routing failure is recorded in
`regression-before.txt`.

Public artifacts contain only fixed stage names, numeric counters, fixture
booleans and sanitized results. Captured PCM is never written. The delayed
provider event is held only in memory and cleared after delivery or cleanup.
Actual transcripts, raw serial/provider logs, credentials, enrollment profiles,
databases and firmware images remain outside the repository. Source/evidence
hashes and the base revisions are in `source-snapshot.json`.

## Remaining acceptance work

This checkpoint prevents the tested abandoned STT turn from initiating new work.
It does not establish cancellation safety once a model request or tool call
already exists, nor complete TTS generation/history isolation. Those downstream
boundaries need request/context identities and tests of delayed callbacks.
Text/background speech still needs a watch-owned response context.

The earlier intermittent capture-group failure remains unattributed; controlled
network loss/recovery, hard allocation limits, proactive lease renewal and long
responses, deployed-service restart, physical controls/apps/sleep-wake, security
coverage and endurance gates remain required. WebRTC is still the default.
