# Physical provider turns and STT filtering checkpoint

Recorded 2026-08-31. The complete host-initiated physical microphone → MoQ →
STT → model/read-only tool → TTS → MoQ → watch speaker path now has passing
evidence. This is not release acceptance: one repeated-turn run fails, physical
PTT/navigation are not exercised, and the remaining plan gates stay open.

## Diagnosis and change

The previous six-character transcription failure was reproducible with the
provider's `near_field` noise reduction. `p22` adds bounded, in-memory signal
analysis without writing microphone audio. The captured signal correlates with
the generated speech fixture at the expected duration: waveform correlation
0.37685 and 10 ms RMS-envelope correlation 0.69465. Gross duration scaling gives
worse envelope matches. This supports reception of the fixture, not a general
speech-quality or intelligibility threshold.

Only about 0.8% of received signal energy is above 1 kHz, compared with about
19.5% in the synthetic source. The attenuation exists before provider filtering;
these measurements do not isolate whether it comes from room/device placement,
microphone acoustics or the capture/codec path. That acoustic-quality question
remains open. The provider's filter is nevertheless a separate demonstrated
failure condition: `p23` uses the identical firmware and fixed spoken fixture,
with only a bench override disabling provider noise reduction, and passes all
six expected fixture keyword checks plus the required exercise tool.

The production MoQ explicit-PTT profile now defaults to no provider noise
filtering. WebRTC retains its existing `near_field` default. An explicit
`DOODAD_STT_NOISE_REDUCTION` value selects `off`, `near_field` or `far_field`;
invalid values fail before opening the STT service. No injected transcript,
forced tool result, authentication relaxation or replacement AI pipeline is
used. `p24` verifies the new default with no bench/environment override.

## Physical results

All runs use the existing installed `flash16-audio-priority` image, SHA-256
`c5f0f22d89e55f3a21fd8e8aaea1d8b8f02a224d6c940f3efd9291b09f2c04ff`.
No firmware was written in this checkpoint. The private bench provisions its
own enrollment profile and isolated database; it exposes only `get_next_set`
and `get_task_status`, without personal-app delivery or mutating tools.

| Run | Filter/profile | Microphone samples | Speaker samples | Result |
| --- | --- | ---: | ---: | --- |
| p22 | Previous `near_field` default | 60,640 | 20,573 | Fail: six characters, fixture not recognized, no exercise tool |
| p23 | Bench override `off` | 60,160 | 39,939 | Pass: 33 characters, fixture/tool/playback, fresh reconnect |
| p24 | New MoQ default, no override | 60,480 | 41,149 | Pass: fixture/tool/playback, fresh reconnect |
| p25 | New default, two requested turns | 82,456 total | 41,149 first turn | Fail: first turn passes, second capture misses groups |
| p26 | New default, two requested turns | 121,120 total | 81,088 total | Pass: two separate tool calls/playback receipts, fresh reconnect |
| p27 | New default, provider configuration observed | 60,320 | 43,570 | Pass: provider confirms no noise reduction, fixture/tool/playback/reconnect |

`p26` captures 60,800 and 60,320 samples, returns 33 transcription characters
per turn, and plays 41,149 and 39,939 samples under response IDs 1 and 2. Each
capture has zero dropped microphone chunks, zero discarded input and one
initial codec reset. Playback completion is the actual watch DMA-drain receipt.
Reconnect obtains a new session and remains microphone-idle for three seconds.

`p27` also records the provider's acknowledged session configuration:
`stt_effective_noise_reduction` is null. This confirms that omitting the filter
through the pinned Pipecat settings leaves it disabled on the actual provider.

`p25` is retained as a failure, not explained away by `p26`. Native capture
expires with `next=261`, `last=288`, and 20 buffered groups. Firmware failure
diagnostics report `encoded=113 accepted=303 sent=298 stale=0 queued=5 high=8
mic_drops=0 poll_gap_ms=162`. Accepted/sent are service counters and do not prove
network delivery; the server's outgoing loss counter does not measure all
watch-to-server loss. The next transport task is to distinguish publisher
cache eviction, transmit expiry and network delay, then fix/recover bounded
loss without weakening the 200 ms live-media budget or hiding zero-impairment
failures. No induced network-loss matrix was run here.

## Bench and privacy contract

The provider bench can repeat 1–3 turns with `--capture-rounds`. Every turn
requires new STT completion, an exercise-related fixture result, the real
`get_next_set` completion and a newer response's speaker receipt. Prior-turn
success cannot satisfy the next turn. These are host-driven capture requests,
not evidence of a physical button/touch PTT action.

`--acoustic-analysis` holds at most 31 seconds of PCM in memory and returns
only numeric aggregate signal measurements. The capture buffer is cleared at
shutdown. It writes no microphone waveform, spectrum or transcript. The known
synthetic fixture is “Please read my next exercise set.”; its 16 kHz mono WAV
contains 28,549 samples, SHA-256
`fc208e74c8553988df8c0540dc0d70f3d0a733980b6f08f3313a8f2ee414479c`.
Only that generated source may be saved by the bench. Native/serial/provider
raw logs and credentials remain private; public stage records retain fixed
names, numeric counts and keyword booleans only. Mac output volume/mute was
restored after every fixture and checked at the end.

The early acoustic reports in p22–p24 used 10 ms window indices for envelope
offsets. Public copies explicitly name these `offset_windows_10ms` and include
`envelope_window_samples: 160`; waveform offsets are PCM samples. The current
analysis implementation reports every offset in PCM samples, with a regression
test for that unit conversion. No audio-derived values were recomputed from a
saved recording.

## Verification and remaining gates

209 Python tests and five native integration cases pass; the Python suite has
four existing warnings. New tests inspect the actual
STT construction boundary to preserve WebRTC filtering and verify explicit MoQ
profiles; synthetic diagnostics tests check known delay/gain, silence, unit
conversion and input bounds. Firmware/library code is unchanged from the
previous tested image and committed submodule revision.

Complete provider turns are now demonstrated. Reliable repetition, cancellation
at every provider phase, capture/response generation isolation, proactive lease
renewal, long responses, hard allocation limits, loss recovery, deployment,
physical PTT/full-shell/app behavior and the endurance/security matrices remain
required. The goal is active; this checkpoint does not authorize the default
transport switch or establish a library release candidate.
