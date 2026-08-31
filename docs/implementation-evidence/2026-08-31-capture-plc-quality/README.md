# Capture PLC amplification and speech-quality gate

Work after VoiceWatch `13fe8a3` and moq-esp32 `0f71710`. This checkpoint fixes
a reproduced native-decoder defect and establishes a numeric speech-fixture
gate. It does not complete the WebRTC replacement or accept impaired speech.

## Baseline and acceptance policy

p67 runs the installed flash32 firmware without induced network impairment.
It receives 60,160 microphone samples with zero concealment, lost groups or late
groups. STT recognizes all six fixture keywords, but the model response times
out and no complete provider turn passes. The model timeout's cause remains
unproven. The baseline microphone RMS is 500.14 and peak 6,478. Numeric waveform
and envelope correlations are 0.37947 and 0.70316. Only about 0.8% of captured
energy is above 1 kHz, versus about 19.5% in the reference. Those spectral
measurements do not isolate the acoustic/codec frequency-response cause.

The old provider bench accepted any transcript containing exercise, workout or
set. That could accept severe word loss. Policy v1 instead measures word-level
Levenshtein distance from **“Please read my next exercise set.”** Case and
punctuation are ignored. Zero word errors are required without induced
impairment. With impairment, at most one of the six words may be wrong
(unrounded WER <= 1/6), and the contiguous ordered phrase **“next exercise set”**
must survive. This is a deliberately narrow request-preservation gate based on
the six-word baseline, not a general speech-intelligibility metric.

The policy was implemented before p68/p69 and is not relaxed after failures.
Every admitted completion of a turn must pass; results reset for each new
capture. Missing, oversized and non-text responses fail. The scorer holds no
audio, returns no transcript, and bounds input to 4,096 characters / 256 words.
Only numeric scores and fixed keyword booleans enter evidence. The real STT
input, model context and provider responses are not replaced or edited.

p68 establishes the measured baseline with the new policy: three complete
provider turns, each zero WER, fresh `get_next_set`, exact watch speaker receipt,
and one admitted history message. A fresh authenticated session remains
microphone-idle after reconnect. All three captures and responses have zero
loss/concealment. The generated 16 kHz mono source is 28,549 samples, SHA-256
`fc208e74c8553988df8c0540dc0d70f3d0a733980b6f08f3313a8f2ee414479c`.

## Reproduced codec defect and correction

p69 repeats the 5% loss / 120 ms added RTT / 800 ms uplink blackout case with
unchanged firmware. Its next capture completes with 8,000 concealed samples
of 59,840 (13.4%). Received PCM again reaches 32,766, with aggregate RMS 12,695.
STT has one word error but loses the required target phrase, so it fails.

A deterministic host test removes acoustics, microphone gain, QUIC, providers
and hardware from the diagnosis. It encodes a four-second, 220 Hz tone at 0.03
full-scale amplitude using the pinned reference encoder, then feeds the same
packets to clean and loss-impaired instances of the actual capture decoder.
The schedule includes isolated drops and an eight-packet burst, invoking 7,040
samples of real PLC. With `unsafe-libopus = 0.2.0`, the clean peak is 1,009 but
the impaired peak reaches 32,766. The regression fails. This demonstrates
amplification in the decoder loss path, without establishing a particular
source line in the transpiled dependency as the root cause.

The native capture wrapper now uses the C decoder bundled in the exactly pinned
`audiopus_sys = 0.2.2` crate. It still requests exactly 320 samples per decode/PLC
call, preserves epoch pre-skip, and invokes real concealment. It does not add
an amplitude limiter, replace PLC with silence, enable FEC, or increase any
transport/audio loss budget. The regression now passes: clean peak 1,009,
impaired peak 1,081. Existing clean reference-PCM equivalence, timestamps,
lost-tail and cancellation tests pass as well.

The supported build statically compiles the crate's bundled C source and rejects
external library-path overrides. Local Cargo configuration bypasses pkg-config;
a build guard rejects callers that bypass that configuration. The archive's
Cargo.lock checksum, canonical bundled-source manifest hash and license hashes
are recorded in `codec-dependency.json`. The endpoint has no dynamic libopus
dependency. License notices are included under the endpoint's `licenses/`.
The reference encoder remains unmodified and still requires unsafe-libopus
transitively. No relay patch, private framing or watch firmware change is used.

## Physical results

All runs use flash32, SHA-256
`26fa1f7377b7479e1f779befaf7aa51f013c0a50ea29f53fcc2c990677df8484`.
Only p70/p71 use the corrected native decoder. Their endpoint binary hash is in
`source-snapshot.json`. No firmware flash or restoration was performed. Private
bench enrollment, isolated database and read-only model tools remain in use.

| Run | Profile | Result |
| --- | --- | --- |
| p67 | No induced impairment, previous decoder | Speech keywords pass; model-response timeout; zero complete provider turns. |
| p68 | No induced impairment, previous decoder | Three complete turns pass with zero word errors, no capture or playout concealment, and idle reconnect. |
| p69 | 5% / 120 ms added RTT / 800 ms outage, previous decoder | Session survives aborted capture; recovery 752 ms; next capture completes but target phrase fails. PCM reaches full scale. |
| p70 | Same impairment settings, C decoder | Recovery 756 ms; peak normalizes to 7,646 and RMS to 440.55. Speech still fails: three word errors (WER 0.5), with 8,640 concealed samples of 59,200 (14.6%). |
| p71 | No induced impairment, C decoder | Three complete turns pass with zero word errors, fresh watch reads, exact playback and idle reconnect. No PCM concealment; one initial empty reset group is lost. |

The same seed (52) does not make different network runs byte-identical. The
120 ms is added RTT, not measured total RTT. Comparing p69/p70 alone cannot
attribute all differences to the decoder; the deterministic signal regression
is the controlled causal evidence. p70 confirms normalized physical signal
levels but **does not prove improved speech accuracy or accept capture loss**.
No complete provider turn passes p69/p70's full gates; remaining requested
turns are not attempted after the first failure.

p71 captures 59,840 / 60,480 / 59,840 samples and plays 43,570 / 44,780 / 41,149
samples. Every response has zero concealment, late packets, pressure and silence.
The first capture loses sequence 0, the empty initial reset, before receiving
sequence 1; it loses no PCM and has zero concealment. This is retained as an
unresolved no-induced-impairment protocol loss, not described as a loss-free
run. Its microphone RMS is 501.51 and peak 7,348. Reconnect stays microphone-idle.

Mac volume/mute is restored after each fixture (final volume 0, muted). Captured
audio is counted/discarded, with optional bounded in-memory numeric analysis;
no ambient microphone waveform or transcript is added to the repository.

## Verification and remaining work

`verification.json` records current test scopes and log hashes. The maintained
signal regression fails on the old backend and passes on the C backend. Native
tests cover clean reference equivalence as well as recovery, and the new build
policy checks accept bundled builds and reject missing configuration/external
library overrides. CI now has a dedicated endpoint test/Clippy/build lane;
adding that lane is not a claim that remote CI has run.

Next, reduce the measured capture abandonment/stream-credit pressure without
weakening the 200 ms live-media policy, and re-run the unchanged speech gate.
The first reset loss, p67 model timeout and older p50 cause remain unresolved.
The complete loss/RTT/reorder/flow-control matrix, measured resource/allocation
bounds, long responses, physical PTT/navigation/apps/package lifecycle, secure
deployment, latency, 1,000 cycles and eight-hour soak remain required by the
full replacement plan. WebRTC remains the configured default.
