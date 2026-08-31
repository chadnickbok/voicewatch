# Idle capture demand and catalog timeout checkpoint

Work after VoiceWatch `3855d05` and moq-esp32 `21e3750`. This is an investigation
checkpoint, not acceptance of the WebRTC replacement.

## Changes and scope

The pinned scoped origin returns a spliced audio track. Awaiting subscription
resolves track information, but upstream demand starts only when the group cursor
is polled. The native actor now primes that cursor before `media.ready` and keeps
polling it while idle. It does not decode or forward idle microphone PCM;
authorized captures retain independent bounded cursors. A real-actor regression
fails before the fix with missing idle demand.

Both idle and active subscriptions request 200 ms maximum latency instead of two
seconds, matching the existing capture recovery wait. This is an experimental
transmission policy: it has not passed the impaired hardware gate. It does not
enlarge concealment, sample limits or the 32-handle capture storage bound.

Native diagnostics classify backend failures into fixed categories and numeric
codes, and report typed QUIC close metadata. Arbitrary peer/backend reason text
is excluded, with a regression checking that boundary. Watch diagnostics expose
the failing service event and distinguish local control deadlines from control
TX expiry. C regressions exercise both timeout sources separately and prevent
duplicate deadline counts.

## Physical evidence

Provider p61/p62 and native-only t62/t63/t64 all fail with 5% seeded loss,
120 ms added RTT, seed 52 and an 800 ms uplink blackout. Requested added RTT is
not total measured RTT. These runs do not establish capture recovery or speech
quality acceptance.

t64, on flash27, records `service event failure type=9 result=12 code=3`, followed
by `media failure result=12`. The service's catalog operation termination path
reports that timeout and the watch then retires the media session. Zero endpoint
failure counters do not imply that the media service stayed healthy. This
narrows the failure to catalog subscription termination, but does not distinguish
a local control deadline, queued TX expiry or a peer termination. The original
p50 failure is not proven to have the same cause.

The t64 bench fails after 23,012 ms with zero completed captures or round trips.
It counts and discards 4,376 microphone samples. Two sessions become ready,
including the expected forced reconnect, which takes 6,319 ms.

Flash28 includes the new control-timeout counters. Its firmware SHA-256 is
`b476cb85de34965b2ee356bce387edf834deb9afe7f7d4029a2d486c0125ab17`.
The app0-only write starts at offset 65,536 and spans 3,428,352 bytes; the full
shell reaches its steady-state marker. No subsequent impairment run has tested
these counters at this checkpoint. Firmware stays installed; restoration is
not required.

## Verification and remaining work

Checkpoint verification passes 26 Rust tests across all targets, Clippy with
warnings denied, and all seven normal C host suites. Earlier checks during this
investigation pass six native integration cases and 26 firmware-parser cases;
the final firmware build also passes. ASan/UBSan were not rerun for these latest
counter changes; the preceding checkpoint's sanitizer results still apply only
to its recorded source revision.

Next, reproduce the catalog timeout with the new counters and fix its confirmed
cause before accepting the transmission-budget change. The complete impairment
and speech-quality matrix, full physical shell interaction, allocation limits,
deployment/default switch, latency and endurance gates remain open. WebRTC is
still the default.

This evidence contains only reviewed counters and the firmware hash. Credentials,
profiles, transcripts, raw hardware/provider logs and microphone PCM are not
included. No ambient microphone PCM was persisted by these benches.
