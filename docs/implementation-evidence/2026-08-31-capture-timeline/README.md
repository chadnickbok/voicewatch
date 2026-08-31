# Audio owner scheduling and discontinuity timeline checkpoint

Recorded 2026-08-31. This continues the capture-ordering checkpoint and does not
establish full provider-turn or release acceptance. New source is uncommitted;
`source-snapshot.json` identifies its base revisions and file hashes.

## Root cause and correction

`p18` identifies the previously generic terminal failure: the native decoder
completed 46,192 samples while firmware reported 50,560 accepted input samples.
The 4,368-sample difference equals 42 Opus lookahead intervals of 104 samples.
Microphone discontinuities reset the codec and discard this history; accepted
input is therefore not an accurate count of received PCM after resets.

The firmware audio task also read only one 10 ms DMA chunk per RTOS tick. It
could not catch up after encoding/network work delayed a tick. The owner now
drains at most four chunks per cycle, matching the bounded board queue while
retaining command and playback work on each cycle. No queue sizes, transport
deadlines, TLS validation or authorization checks were relaxed.

`flash13-audio-catchup` contains the scheduling fix. `p19` then transfers 60,320
samples with zero dropped microphone chunks, zero discarded buffered samples,
and only the initial codec reset. The capture validates and STT commits, but the
provider returns an empty transcript. This is capture success, not voice-turn
acceptance.

For future discontinuities, the encoder now reports a separate final timeline
sample count from the first published packet to the logical end. Native decoding
preserves gaps across explicit reset groups with at most 200 ms of silence,
counts concealed samples, and rejects larger or regressing gaps. It never calls
the silence recovered speech. Missing network groups still fail at the existing
reorder deadline; network-loss PLC and longer loss/stress matrices remain open.

Tests cover partial input plus abandoned lookahead, exact final tails, timestamp
bounds, and the actual native capture worker's PCM/terminal sequence. Normal
loss-free fixtures remain byte-equivalent to the pinned reference decoder.

## Physical validation

`flash14-timeline` includes the timeline correction. `audio11-timeline` passes
ten consecutive three-second captures with the listening shell active:
480,000 microphone samples, no drops, 16,037 speaker samples, cancellation and
response replacement, and fresh-grant reconnection. Reconnect takes 5,250 ms,
still above the plan's connection target. This does not substitute for the
1,000-cycle or endurance gates.

`flash12-audio-catchup` copied the previous image while the build was pending.
Its hash matches `flash11-control-site`; it is recorded for flash provenance
only and is not used as evidence for either correction. Subsequent images were
copied after confirmed build completion and their hashes differ.

## Speech-input work

The existing WebRTC configuration applies 8x microphone gain. The MoQ raw-board
capture path omitted it; gain is now applied once when acquiring a new chunk,
never again during backpressure retries. The Ultra board compatibility wrapper
also now honors its configured microphone gain.

`flash15-input-gain` and `p20` reach STT, a model response, TTS and an exact
20,573-sample speaker receipt. However, the transcript contains only six
characters and the required exercise tool is not invoked, so the run fails.
The louder input causes 21 dropped microphone chunks; the new timeline logic
keeps the session valid with 59,520 timeline samples versus 56,160 accepted
input samples. This validates discontinuity handling on hardware but is not
acceptable capture quality. The bench now also requires an exercise-related
fixture transcript, recording only a boolean and character count.

The manufacturer schematic and pinned microphone initializer were inspected:
clock GPIO17, data GPIO18, the microphone's direct VDD3V3 supply, and 16 kHz mono
PDM configuration agree with this BSP. No pin, power-rail or charging changes
were made. Startup transients and physical speech quality still need validation.

The next firmware gives the bounded audio owner priority 6 above the network
worker's priority 5 and replaces the first 320 samples with silence, retaining
their timeline positions. The [T3902 manufacturer datasheet, table 5](https://www.mouser.com/datasheet/2/400/DS_000357_T3902_v1_0-1864560.pdf)
specifies up to 20 ms wake-up time. No microphone activity is enabled before
authorized capture, and the board's raw capture API remains unchanged.

`flash16-audio-priority` is now installed, SHA-256
`c5f0f22d89e55f3a21fd8e8aaea1d8b8f02a224d6c940f3efd9291b09f2c04ff`.
`p21` captures 60,000 samples with zero drops/discards, only the initial reset,
peak 5,867 and zero clipped samples in its measured 500 ms windows. STT commits
after capture validation and returns six characters, but `fixture_recognized`
is false. Model/TTS output reaches a 20,573-sample watch speaker receipt; the
exercise tool is not invoked. The provider bench therefore fails. Its later
native IPC EOF follows bench shutdown and is not a new capture failure.

The next physical gate is intelligible recognition of the generated exercise
request and the matching read-only tool invocation, then a fresh provider
session/reconnect. Acoustic fixture verification and microphone signal quality
remain open; successful transport or audible synthesis cannot satisfy them.

## Verification and limits

Sixteen Rust tests, the C audio suites, seven C host programs, the real-adapter
fault suite (including 200,000 stream lifetimes), the touch decoder test, 199
Python tests and five native integration cases pass. The Python suite retains
four pre-existing dependency/runtime warnings. The full Ultra image builds and
app0-only flashes boot to shell steady state.

Public artifacts contain numeric/fixed diagnostics only. Credentials, firmware
images, raw serial/provider logs and microphone recordings are excluded. Tests
leave the new firmware installed; default firmware restoration is not required.
The complete STT/model/tool/TTS turn and the implementation plan's remaining
security, lifecycle, allocation, deployment, UI and endurance gates remain open.
