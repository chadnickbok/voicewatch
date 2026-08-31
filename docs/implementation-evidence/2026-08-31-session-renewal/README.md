# Live authorization renewal and long playback

This checkpoint adds renewal of an already-running authenticated session to
avoid cutting a continuous response off at the default 300-second grant lease.
The configured default remains WebRTC. No running supervised host is redeployed.
This is not completion of the library-candidate or full-product acceptance gates.

## Authorization contract

At half lease, a renewal-capable watch receives a one-use 256-bit nonce over its
existing authenticated WSS connection. Its enrollment-key HMAC uses a distinct
signing domain and binds device ID, session ID and nonce. The host admits only
the same still-live control owner with attached media. Replaced/revoked/expired
sessions, wrong owners/keys/domains, replay and malformed proofs cannot extend
a lease. A bad matching proof consumes the nonce. A challenge alone grants nothing.

After verification, Python extends the existing directional grant. Private IPC
carries a consecutive renewal revision, the bounded lease and absolute UTC expiry.
The native reader consumes IPC transit time from that expiry, checks its old
monotonic deadline before updating, and acknowledges only an accepted update.
Python then sends the watch its new bounded expiry and a fresh signed time proof.
The watch verifies the pending nonce, next revision, enrollment revision, a
three-second exchange bound and time agreement within two seconds. It never
sets UTC during renewal. Both its control and library deadlines advance before
its completion receipt is sent. The host counts success only after that receipt.

The endpoint update and expiry test share a mutex. An owner paused beyond expiry
cannot be revived before it notices the timer. Failures are non-mutating; no
identity, scope, SETUP credential, catalog, media generation or queued audio
changes on success. No microphone operation is started. A genuine replacement
still invalidates media and needs fresh bootstrap and one-use attachment tokens.

The complete message and lifetime contract is documented in
[the product session contract](../../moq-product-session.md). This changes the
application control and local IPC contract, not standard MoQ/Hang framing or the
pinned reference. Older firmware omitting the capability expires and reconnects;
renewal requires the matching new Python and native implementations.

## Local validation

The maintained tests cover proof/domain/identity separation; nonce replay and
consumption; timeout, key/owner/replacement faults; old authorization and trusted
time expiry even before the owner polls; non-mutating rejection; preservation
of an active response; native-before-watch acknowledgment; and failure when the
native acknowledgment never arrives. C++ firmware parsing and HMAC are exercised
against Python-generated fixtures using actual ESP-IDF cJSON and OpenSSL.

All seven C host suites and the actual-adapter suite pass. The 181-test Python
MoQ suite and 154-test combined scoped/cross-language suite pass (these overlap;
do not add the counts). The six existing real-native product integration cases,
29 Rust tests, Clippy, and all 18 ordinary native adapter/interoperability/security
cases plus the allocator self-test pass. Full ASan/UBSan and remote
CI have not been rerun. Counts and private log hashes are recorded separately.

## Physical validation

Flash41 is the complete secure Ultra shell, written app0-only after identity,
chip/security, layout and OTA checks. The full-shell heartbeat passes. No NVS,
bootloader, OTA metadata, package or user-data erase occurs. Firmware restoration
is not required. The firmware and native endpoint hashes are recorded separately.

r1 passes two same-session renewals with a 45-second lease and zero microphone
samples. Its deliberate pre-test reconnect takes 5,513 ms; the two renewals do
not reconnect. r2's first long attempt is a harness error: the first progress log
passed a duplicate timestamp keyword at 15 seconds, raising TypeError and closing
the bench. It is retained as a failed run, not counted as transport rejection or
long-playback acceptance. The corrected logger uses a distinct playback timestamp.

The long fixture is generated, mostly quiet PCM: a 100 ms 500 Hz marker followed
by 900 ms intentional silence, paced through the normal host spool, codec,
standard Hang streams and physical speaker. Its 9,599,877 samples occupy just
under 600 seconds and end 123 samples short of a whole second, exercising a
non-frame-aligned terminal tail. Neither a provider nor microphone is used.
No ambient PCM, grant documents, keys, raw private logs or binaries are published.

r3 completes the 600-second response with 27 renewals on the same session,
zero microphone samples and the exact 9,599,877-sample speaker receipt after
600,186 ms. That proves continuous authorized playback and the terminal sample
count, but **the full clean-audio gate fails**: the player reports 91 PLC chunks,
111 late-frame counts and 20 fallback-silence chunks, with zero queue-pressure
events. Those are player counters, not an acoustic waveform or hardware-DMA
underrun measurement. No speech/latency/loss budget has been relaxed. The receiver
needs diagnosis of packet arrival versus its playout clock and owner scheduling;
this run does not establish which caused the losses or whether renewal contributes.

The two recorded ngtcp2 heap snapshots in r3 are startup snapshots, not final
long-response peaks. They must not be used to claim allocation headroom throughout
this run. Future long-run diagnostics need an end-of-response heap/timing snapshot
and bounded timestamps at the first quality regressions.

r4 deliberately corrupts a fresh renewal's signed time after a valid device proof.
The native acknowledgment arrives and no renewal is accepted, but the serial tail
misses the firmware rejection site before monitor shutdown, so the negative gate
fails to establish its required evidence. The repeat r5 adds a bounded 350 ms
serial drain before stopping that monitor. It passes: exactly one corrupted MAC,
one native renewal acknowledgment, firmware rejection at site 1260, zero accepted
renewals and zero microphone samples. The underlying positive bench correctly
fails in this negative case; the separate rejection result is the acceptance gate.
Both records remain unchanged.

Flash41 remains installed. Permanent enrollment is reapplied at revision 131
with the same device identity, roots and key. A fresh permanent-service ready
event is observed after enrollment. No persistent service
process is restarted, no production host deployment or reference pin changes,
and no firmware restoration occurs. The running permanent host is still the
previous deployed version; new renewal behavior is tested with private bench
hosts until a coordinated rollout.

See [hardware results](hardware-results.json), [verification](verification.json),
[firmware](firmware.json), [source snapshot](source-snapshot.json), and
[native matrix](native-matrix.json).

## Remaining acceptance

This work does not close unchanged-reference delayed-tail interoperability,
complete loss/RTT/burst/reordering/flow-control and speech-quality coverage,
backend/TLS allocation enforcement, root/key rotation rollout, physical PTT,
launcher/apps/package/sleep-wake acceptance, latency and UI budgets, 1,000-cycle
and eight-hour soaks, release builds/sanitizers/CI or default cutover. A synthetic
long response is not general provider speech quality or a full product release.
