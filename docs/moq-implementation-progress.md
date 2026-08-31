# MoQ replacement implementation progress

Updated 2026-08-31. Objective remains the complete
[implementation plan](moq-webrtc-replacement-plan.md), including all nine review
findings, operational protocol/audio, security, host service, and the full Ultra
shell. This checkpoint does not satisfy all library-candidate or product gates.

## Current direction: resumed; initial acceptance excludes induced impairment

Latest: the user-approved terminal-reader fix is now selected by the Rust host
and deployed to the permanent MoQ service. A ten-minute physical provider session
passes three exact-fixture turns, cancellation isolation, four renewals and
reconnect. Flash49 remains installed; permanent enrollment is revision 187.
The committed library's Linux host CI passes, including full native ASan/UBSan.
See [adoption and deployment evidence](implementation-evidence/2026-08-31-terminal-adoption/README.md).
The earlier unchanged-reference rule is explicitly amended to permit this exact
patch. Physical controls/apps/sleep-wake, the measured 82.6 KiB internal RAM
minimum, and root/core licensing remain separate open work.

On 2026-08-31 the user resumed work to verify physical controls/apps/sleep-wake
and finish interoperability and release checks. Induced impaired-network tests
are outside initial replacement acceptance. Their historical failures remain
recorded as deferred hardening, not passes. Normal-network voice, security,
lifecycle and release requirements remain; ten minutes is sufficient for now.
The pause described below is historical and no longer prevents this work.

### Previous checkpoint: normal firmware and physical signed app delivery

Flash49 runs the normal full shell with personal-package installation enabled.
An enrolled-host HTTPS trust fix passes a physical signed Timer installation and
duplicate-offer check, without opening the microphone. Permanent enrollment is
revision 185. Physical navigation/PTT/sleep-wake are still unobserved; the prior
ten-minute idle window recorded no input and is not a physical acceptance pass.

Fresh host/native checks and a public complete-shell build pass. The terminal
reference candidate passes the current interoperability matrix and lifecycle
suite; the unchanged reference still reproduces its terminal receive race.
A full-shell CI workflow and release-image inspector are prepared, not remotely
executed. Current ASan, root/core licensing and the 84.7 KiB measured internal
RAM minimum remain release/resource limitations. See the
[initial acceptance results and hands-on checklist](implementation-evidence/2026-08-31-initial-acceptance/README.md).

## Previous direction: paused; ten minutes is sufficient for now

On 2026-08-31 the user explicitly removed long endurance testing as a replacement
gate. Ten minutes of successful operation is sufficient for now; eight hours
and 1,000 repeated cycles are optional future hardening. The acceptance plan is
updated accordingly, with a [longer-test outline](moq-optional-endurance-tests.md).
Earlier statements below describing long endurance as mandatory are historical
and superseded. The completed 30-minute transport test remains useful evidence.

The p122 eight-hour run was interrupted at the user's request, with normal
temporary-host cleanup and permanent-enrollment recovery. Its incomplete result
is not an endurance pass or evidence of a product failure. Further tests and
implementation are paused. A newly drafted echo-cycle helper is not integrated
or validated and will not run automatically.
Permanent enrollment recovery is verified at revision 183; flash48 and the
existing host services are unchanged. See the [interruption receipt](implementation-evidence/2026-08-31-idle-soak/interrupted-run.json).

## Previous checkpoint: idle/reconnect smoke passes; long run subsequently stopped

Flash48 adds a read-only owner status command under the optional synthetic-test
firmware flag. The corrected p121 smoke test completes 120,039 ms of idle,
one planned reconnect in 6,265 ms, and a final reconnect in 6,262 ms. Both idle
sessions renew twice; snapshots show no active microphone, speaker or retained
media pools and pass the memory/stack floors. The initial p120 host logger
failure is preserved and covered by a reproducing regression. Free internal
RAM is 16 bytes lower after the planned reconnect; attribution and cumulative
recovery remain open. Permanent readiness is verified at revision 181.

The eight-hour p122 run reached authenticated idle readiness on the unchanged
archived image before being stopped at the user's request. It requested only
read-only USB status, used normal certificate checks and short credential leases,
and recorded no microphone PCM. No firmware or persistent service was changed.
The long-duration gate is now superseded; the default transport is unchanged.

Latest verification passes 407 Python tests (four warnings), including 23 idle
tests, 46 native/firmware contract checks and both firmware build modes. See
[idle/reconnect evidence](implementation-evidence/2026-08-31-idle-soak/README.md).

A separate read-only completion audit now checks serial observations, timing,
renewal/reconnect evidence and allocator accounting independently of the runner's
pass flag. It preserves failed p120 and passes p121's operational checks, while
retaining the unexplained 16-byte internal-heap difference and withholding a
cumulative leak-free verdict. Its 42 focused checks and 426-test full Python
suite pass (four warnings). This checker was added after p122 started; it does
not change the running firmware, host or harness. A Linux sanitizer probe never
started and was cancelled without restarting Docker or unrelated services;
ASan remains unpassed.

## Previous checkpoint: 30-minute operational duplex soak passes

Flash47 runs the complete Ultra shell with an optional USB-started synthetic
workload, retaining ordinary authenticated bootstrap, scoped sessions, catalogs,
WSS and credential renewal. The private host uses a separate diagnostic build;
ordinary host/firmware builds exclude this path. Both directions exchange valid
20 ms Opus silence at 50 groups/s with exact payload/timestamp/count checks and
one final receipt group. No microphone, speaker or providers are started.

p117 passes 500 groups each way in 10,033 ms; p118 passes 3,000 in 60,049 ms,
including three same-session renewals. Both reconnect successfully, have zero
send backpressure/final receive leases and pass tracked allocation gates.
The subsequent p119 run passes **90,000 groups in each direction over 30 minutes**
with 82 same-session renewals, exact receipt groups and a 6,274 ms fresh-grant
reconnect. Permanent readiness is verified at revision 177. See
[stream-soak evidence](implementation-evidence/2026-08-31-stream-soak/README.md).

Verification passes 39 all-feature and 31 default Rust tests, 384 Python tests,
46 native/firmware contract checks, Clippy and both firmware build modes. app0
only was flashed; no factory restoration, persistent-host update, reference or
default switch, commit or push occurs. Speech impairment failures, physical
interaction/UI/package, complete resource/latency, endurance/reference/ASan/CI
and rollout gates remain open. This is still not a verified WebRTC replacement.

During p119, internal free RAM spans 164,535–164,635 bytes and returns to its
starting value; native-host workload RSS stays within 19,136–19,264 KiB. All
60 snapshots have zero receive leases. The post-test new connection has 32 fewer
free internal bytes than the pre-test idle baseline; the cumulative reconnect
audit remains open, and no blanket leak-free result is claimed.

The next idle/reconnect harness and read-only USB status command were prepared
while p119 ran on its unchanged image. Their subsequent flash48/smoke validation
is recorded above; no eight-hour result is claimed.
The idle fixture extends its private certificate lifetime from six to ten hours
without changing certificate verification or credential lease limits.

## Previous checkpoint: delayed Hang group passes on the full Ultra shell

A separate diagnostic host holds one standard encoded group for 257,756 µs
while 12 newer groups are published. On unchanged flash46, the watch receives
a fresh 200,000 µs media timestamp before the old 140,000 µs timestamp; the
affected response's maximum frame-arrival gap is 58,021 µs. Later audio therefore
continues during the hold. One concealed chunk and one late chunk are counted,
with zero pressure or fallback silence. All three real-provider voice turns
have zero word errors and exact watch/serial completion totals: 124,658 samples.
Startup, shutdown and idle reconnect pass. See
[delayed-group evidence](implementation-evidence/2026-08-31-group-delay/README.md).

The fixture is excluded from normal builds, which reject its config field.
It retains one bounded packet and explicitly aborts it on early end/cancel or
owner destruction. Native tests also prove control/fresh-group progress during
the hold and cancellation before replacement. Final Rust suites pass 35 tests
with the feature and 30 without it; 384 Python and 46 native/firmware checks pass.
Executed and subsequently rebuilt diagnostic binaries are recorded separately.

Permanent enrollment is revision 171 with fresh readiness verified. No firmware
write/restoration, persistent-host update, default/reference switch or commit
occurs. This adds a physical delayed application-group case; it does not close
physical byte-credit blocking, all speech impairment cells, full UI/package,
latency/memory/endurance/reference/ASan/CI gates. Prior failures and incomplete
statistics remain preserved. The complete replacement goal is still active.

## Previous checkpoint: blocked streams wait for credit; updated Ultra shell checked

A real Quinn peer reproduces repeated retries of a stream with exhausted byte
credit: 401 retries during a 250 ms unread hold. The adapter now keeps that
stream dormant until its matching ngtcp2 credit callback. Fresh media/control
continue; reset retains normal ownership and late credit cannot revive a
retired send half. No media deadlines, buffering or pool limits change.

Resume/reset cases pass across two native matrix iterations and one UBSan
iteration. All 44 ordinary and 22 UBSan peer cases pass, along with allocator,
portable host/audio/adapter, 384 Python and 46 native/firmware protocol checks.
This is real raw-QUIC stream blocking, not MoQ application-group or physical
blocked-audio proof. See [stream-blocking evidence](implementation-evidence/2026-08-31-blocked-stream/README.md).

Flash46 passes the full-shell heartbeat and p115's real-provider text/background
output, three zero-error voice turns and idle reconnect. Five watch completion
receipts and five final serial markers match all 181,538 samples. Only four
detailed playout summaries were captured, so full quality-counter coverage is
explicitly incomplete; the original summary mismatch remains recorded. Ten
transport snapshots show no flow block, and TLS/ngtcp2 bounds remain intact.
Permanent enrollment is revision 169 with fresh readiness verified.

Occasional Wi-Fi concealment remains a diagnostic, not an automatic product
failure. Earlier critical-phrase failures, remaining application-group/physical
interaction, latency, complete device-memory, endurance/reference and ASan/CI
gates remain open. No production-host rollout, reference/default switch,
commit/push or firmware restoration occurs. The complete goal remains active.

## Previous checkpoint: wolfSSL heap requests bounded and tested on the Ultra

The actual wolfSSL build now uses a shared 256 KiB request cap, covering TLS and
null-hint crypto allocations separately from ngtcp2's 128 KiB per-connection
cap. Headers, retained capacity and simultaneous realloc storage are charged.
Allocation/lock failure is handled without an unlimited fallback; thread-safe
snapshots distinguish unavailable data from zero use. Verification, hostname/IP,
ALPN and authorization policy remain unchanged.

Real-backend tests cover 128 constructor allocation sites, 43 smaller budgets,
25 clean restarts, concurrent allocation, and constructor/handshake exhaustion.
Two native-suite iterations and one fully instrumented UBSan iteration pass,
including security rejection, stream turnover and Opus exchange. The C host,
adapter/audio, 384 Python and 46 native/firmware-protocol checks also pass.

Flash45 runs the full shell and passes eight real-provider responses: text,
background speech and six zero-error microphone turns. The impaired run uses
5% loss, 120 ms added RTT and an 800 ms outage; capture recovers in 1,000 ms.
All speaker totals match, service lifecycles/reconnects pass, and TLS peak stays
at 49,415 bytes with no tracked denial/system failure. See
[TLS allocation and hardware evidence](implementation-evidence/2026-08-31-tls-memory/README.md).
An earlier 60-second flash observation timeout is retained; the final image
passes the unchanged heartbeat marker with enough time for its post-startup
interval. Permanent enrollment is revision 167 with fresh readiness verified.

This closes an unbounded active-provider allocation path, **not** the full
device-memory or release gate. mbedTLS, system bookkeeping, stacks, full UI
stress/headroom, physical interactions, remaining impairment/reference cases,
latency, ASan/CI and endurance remain open. Earlier speech failures remain failed.
No production-host rollout, reference-pin update, default switch or commit
occurs; the complete replacement goal remains active. No firmware restoration
is required.

## Previous checkpoint: packet faults exercised; cancelled-response recovery fixed

Separate 80 ms packet-reordering and duplicate-packet cases each pass three
zero-error real-provider turns on the Ultra. A combined 250 ms reorder/duplicate
case exposes a host defect: a pacing-cancelled response leaves the conversation
Speaking. The sink now discards unheard output, ends its speaking state and
retires only that provider turn before returning Ready. Stale drains cannot
reset replacements; durable announcements remain pending.

A controlled 350 ms media-pump stall takes the actual cancellation path, adds no
unheard history, and then passes three fresh same-session turns plus reconnect.
The subsequent combined-packet repeat completes its response and returns Ready,
but fails the frozen critical-phrase speech check. **The combined case and full
speech impairment gate remain unpassed.** Both failed runs are preserved. Two
additional bounded acoustic diagnostics do not reproduce or explain the earlier
p101 error, so they do not replace its failed result. No buffer, pacing, loss or
speech criterion changes.

See [packet and recovery evidence](implementation-evidence/2026-08-31-packet-order/README.md).
The full Python suite passes 384 tests; 46 native/firmware-protocol checks also
pass. Permanent enrollment is revision 164 with fresh readiness verified;
flash43 and all persistent service processes remain unchanged. No firmware
restoration, host rollout, reference-pin update, default switch or commit occurs.
Application-group/blocked-stream, physical full-shell, security/allocation,
latency, reference, sanitizer/CI and endurance gates remain open. The complete
replacement goal is still active.

## Previous checkpoint: twelve impairment cells run; one speech failure remains

The corrected full-service bench now covers all twelve configured combinations
of 0/1/3/5% packet loss and 30/60/120 ms added RTT on flash43. Eleven cells pass
three zero-error voice turns each. The 3%/60 ms cell fails its first turn: one
transcription error changes the frozen critical target phrase. Its watch read,
TTS response, exact speaker receipt and service shutdown still complete. The
failed cell is retained without retrying it into a pass; four untested cells
then run separately. **The impairment matrix has not passed.**

Across all cells, 34 responses finish with matching host/firmware sample totals;
33 speech turns satisfy the fixture policy. Every host starts and shuts down
cleanly, and all eleven passing cells verify idle reconnect. Concealment and
brief fallback silence under loss remain diagnostics rather than automatic
software-defect verdicts. The failed capture has seven lost groups and 140 ms
of PLC; passing captures can have larger losses, so the failure's cause is not
isolated by counters alone. No queue, buffer or speech threshold is relaxed.

See [the complete matrix and failure evidence](implementation-evidence/2026-08-31-provider-matrix/README.md).
The Python suite remains green. Permanent enrollment is revision 156 with fresh
readiness verified; firmware and persistent services remain unchanged. The
speech failure, remaining reorder/blocked-stream tests, physical product,
security/allocation, latency, reference, sanitizer/CI and endurance gates remain
open. No rollout, reference-pin update, default switch or firmware restoration
occurs, and the complete replacement goal remains active.

## Previous checkpoint: real provider pacing passes; service startup cleanup fixed

Flash43 and contiguous host pacing now pass nine complete real-provider voice
turns plus text/background speech in final runs p91–p93. All nine fixed-phrase
transcripts have zero word errors; eleven firmware speaker totals match host
receipts, and output-only speech/reconnect do not start capture. Delayed TTS from
a cancelled turn is rejected during the replacement capture. With 5% loss,
120 ms added RTT and an 800 ms outage, capture recovers on the same session in
973 ms. Its 22 PLC and 22 late chunks are retained diagnostics, with no fallback
silence; this is not a downlink audibility or complete impairment-matrix verdict.

An attempted matrix expansion exposes a host startup/cleanup blind spot. Initial
provider runs reach listening sockets without verified full service startup;
p90 later hangs after writing its provider result. Two regressions reproduce
discovery and transport resource leaks on startup failure. Startup now shares
the cleanup scope and uses owned asynchronous discovery. The private bench
disables advertisement and requires full service-ready/shutdown evidence rather
than socket readiness. Final p91–p93 pass those checks and exit cleanly. Historical
runs retain their narrower verdicts and the failed matrix attempt.

The complete Python suite passes 369 tests; real-native and firmware-protocol
checks also pass. See [provider and lifecycle evidence](implementation-evidence/2026-08-31-provider-pacing/README.md).
Permanent enrollment is revision 143 with fresh readiness observed. Flash43 and
both persistent hosts remain in place, with no firmware restoration, host rollout,
reference-pin change or default switch. The rest of the matrix, physical full-shell,
security/allocation, latency, reference, sanitizer/CI and endurance gates remain
open; the full replacement goal is not complete.

## Previous checkpoint: contiguous pacing fixed; three long-playback losses remain

Interpretation clarified after the user's Wi-Fi question: the three PLC and late
counts fail the bench's strict zero-concealment diagnostic, but do not by
themselves prove a transport defect or a product-readiness failure. The same
frames can contribute to both counters. Three 20 ms concealed chunks across
roughly 30,000 frames is about 0.01%; audibility still needs assessment, and the
quiet marker fixture does not establish speech quality. No injected loss is not
the same as a loss-free Wi-Fi path. Preserve the recorded diagnostic failure;
judge replacement readiness by response integrity, speech quality, bounded
latency, recovery and the complete original acceptance matrix. Do not make
eliminating every wireless concealment event the prerequisite for further
integration work.

The player now retains bounded first-failure timing snapshots and emits final
endpoint timing/allocator evidence after speaker drainage. A two-minute repeat
shows a responsive audio task and stable host pacing, but delivery gaps up to
95.789 ms starve the 60 ms prebuffer. The library can select 60..80 ms only before
playback starts; the Ultra uses the plan's 80 ms upper target. Its ten-packet/
200 ms queue, stale-media bound, PLC limit and cancellation are unchanged.
Real-Opus regression coverage reproduces the observed burst: 60 ms conceals,
while 80 ms preserves exact PCM. An 80 ms physical repeat still fails once.

That repeat also exposes a host pacing mismatch: scheduler slips permanently
move later deadlines even though native MoQ timestamps remain contiguous. MoQ
now retains the response clock and catches up at no more than two packets per
20 ms. More than 200 ms of debt cancels only that response. Every replacement
gets a fresh clock fenced from retired awaits. WebRTC pacing is unchanged.

Flash43/r8 completes all 9,599,877 samples over 600 seconds and 27 renewals. The
30,000 host packets span 599,981.998 ms, about 2 ms from nominal despite scheduling
slips. **Clean long playback still fails:** three PLC chunks and three late
packets, zero pressure/fallback silence. Delivery gaps still reach 107.010 ms
while audio steps remain below 9.737 ms. Native-send/adapter-receive timing must
localize the remaining stalls; no audio threshold or buffer limit is raised to
claim a pass. The final ngtcp2 peak is 30,344 bytes with no allocation denial;
this does not establish TLS/total-device bounds.

242 combined Python/firmware-protocol tests, six real-native cases, C host/audio
tests and player UBSan pass. macOS ASan stalls before main; the local Linux
container never starts. Neither attempt is a sanitizer pass. See
[the timing and pacing evidence](implementation-evidence/2026-08-31-playout-clock/README.md).
Flash43 remains installed; permanent enrollment is reapplied at revision 135.
No persistent host deployment/restart, reference pin update or default switch
occurs. Physical product, impairment/security/allocation, latency, soak,
release/CI and unchanged-reference gates remain open.

## Previous checkpoint: live renewal survives 600 seconds; long clean audio still fails

The library, native endpoint, Python host and firmware now support renewal of an
existing authenticated session. A one-use enrollment-key proof binds device and
session; native acknowledgment and signed fresh-time verification precede watch
acceptance. Deadlines advance atomically without changing scope, media ownership,
UTC or microphone state. Expired, replaced or revoked sessions cannot be revived.
Rejection and replay tests, C/C++/Python byte-contract tests, 29 Rust tests, all
18 ordinary native cases, six product/native integration cases and the full
Ultra build pass. Full sanitizers and remote CI remain outstanding.

Flash41 passes the shell heartbeat. Short r1 completes two renewals without
capture or reconnect. Long r3 retains the same session through 27 renewals and
plays the exact 9,599,877-sample terminal boundary in 600,186 ms. **It fails clean
long-playback acceptance**: 91 PLC chunks, 111 late-frame counts and 20 fallback-
silence chunks require investigation. The sample receipt alone does not prove
lossless audio. No threshold or buffering policy was relaxed. Startup-only heap
snapshots also do not prove the long-run allocation peak.

The physical bad-time-proof test r5 passes: after native renewal acknowledgment,
the firmware rejects at the expected verification site with zero accepted
renewals and no microphone capture. Failed harness attempts r2 and r4 remain in
[the renewal evidence](implementation-evidence/2026-08-31-session-renewal/README.md).
Permanent enrollment is revision 131; flash41 remains installed. Neither persistent
host is redeployed or restarted, and no firmware restoration occurs. The running
permanent host retains its older behavior pending coordinated rollout.

All remaining full-plan reference interoperability, long clean/impaired speech,
security/allocation, physical UI/apps/package behavior, latency, soak, sanitizer,
CI, distribution and default-cutover gates remain open. WebRTC is still the default.

## Previous checkpoint: queue age uses current time; final impaired run passes

The service previously stamped newly queued audio with its network owner's last
tick. A deterministic 210 ms owner pause reproduces fresh Opus and terminal-tail
packets being incorrectly discarded. Queue insertion and age checks now use a
required monotonic clock callback, supplied by the endpoint. RX lease ages also
advance without owner steps. The 200 ms age policy and queue capacities are unchanged.
Host/adapter tests, service-only UBSan and 18 native interop/security cases pass.

New wall-time phase snapshots show long loops inside backend TX/RX work, without
proving whether execution or task preemption dominates. A five-millisecond packet
budget reduced the recorded gaps but failed impaired and clean speech checks;
it is removed and preserved as a rejected experiment, not adopted as a fix.

Final flash40 retains only the queue-clock correction and diagnostics. p85 passes
three complete turns with zero word errors under 5% loss, 120 ms added RTT and an
800 ms outage, recovering in 655 ms. Clean p86 also passes three zero-error turns
without lost/late capture groups or concealment. Maximum owner gaps still reach
about 160 ms; these short runs do not close the complete impairment or scheduling
gates. See [the clock, timing and experiment evidence](implementation-evidence/2026-08-31-owner-scheduling/README.md).

Flash40 remains installed after an app0-only write and shell heartbeat. Permanent
enrollment is revision 125; both persistent services retain their processes.
No firmware restoration, production-host update, reference-pin change or default
switch occurs. All broader protocol, security/allocation, physical shell/app,
latency, long-response, soak, sanitizer, CI and release requirements remain open.

## Previous checkpoint: DROP reports no longer stall contiguous media

The engine now admits fresh contiguous media in the same poll as a DROP batch,
and while that control write is blocked or partial, after START is fully submitted.
It preserves owned control bytes, serving caps, required gap reports, cancellation
and END/FIN ordering. A strengthened regression fails before the fix; host,
actual-adapter, 18 native interop/security cases and engine-only UBSan checks pass.
The full fresh sanitizer gate remains open. Pool sizes and deadline policies do not change.

Flash37 is installed app0-only and passes the full-shell heartbeat. Physical p78
exposes a host capture-idle timeout that incorrectly closes the whole session.
That branch now aborts only the capture; no-audio and stalled-audio actor regressions
fail before the correction and pass after it, while malformed media stays fatal.
All 28 native host tests pass. The modified host is tested in private benches only.

Clean p79 and p81 each pass three full provider turns with zero word errors.
p81 loses one initial reset group without audio concealment. Impaired p80 recovers
from the intentional outage and completes two turns, but its third exceeds the
loss budget and aborts. Its eleven-group gap, 203 ms maximum network-owner poll gap
and thirteen stale capture frames are the next investigation, not acceptance.

Permanent service enrollment is revision 119, both persistent service processes
remain running, and the new firmware remains installed. No firmware restoration
or production-host update occurs. See [the admission and idle-recovery evidence](implementation-evidence/2026-08-31-control-media-admission/README.md).
All remaining full-plan impairment, reference, security/allocation, physical shell,
latency, soak, sanitizer and release gates remain open.

## Previous checkpoint: submitted FIN no longer blocks fresh audio admission

The adapter now lets fully submitted FIN streams use the existing retirement
reserve. They retain their IDs and ACK-owned bytes until backend closure; merely
queued FIN remains active. A regression fails before this change and passes after
it, including total-capacity enforcement and the reserved peer reply slots.
Pool sizes, QUIC heap cap and the 200 ms media/loss budgets are unchanged.

Host and actual-adapter tests, 18 native interop/security cases and the full Ultra
build pass. Flash36 is installed through the app0-only runner and reaches the
shell heartbeat. Fresh sanitizer checks remain unverified: Docker cannot start
even an empty container, and macOS ASan stalls in initialization before `main`.

The new impaired baseline p75 fails with two word errors. Updated firmware p76
completes two impaired provider turns with zero word errors, but its third loses
the critical phrase and fails. This is not impaired-speech acceptance. Clean p77
passes three provider turns with zero word errors and no capture loss/concealment.
Permanent service enrollment is advanced to revision 114; no firmware restoration
or production-host update occurs. See [the admission and speech evidence](implementation-evidence/2026-08-31-fin-retirement-admission/README.md).

The full impairment/speech, sanitizer, unchanged-reference, security/allocation,
physical shell/app, latency and release gates remain open.

## Previous checkpoint: subscription updates and real-QUIC lifecycle corrected

The candidate now fixes both follow-up defects: successfully forwarded floor
changes withdraw obsolete holes and cancel lower active readers, while quiet
subscription aggregation watches preference changes as well as departure. START
and END retain their original meaning, and later latency changes cannot extend
the fixed FIN drain deadline. These changes supersede the separate WIP patch.

A fresh preparation from the maintained patch passes 853 reference unit tests
and all 172 integration cases: 18 ordinary native cases, 100 engine exchanges with
four concurrent peers, 27 delayed-reader runs, and 27 real-QUIC lifecycle runs.
The lifecycle fixtures exercise late FIN, active DROP, known/unknown resets,
floor changes including active-reader cancellation, cap changes, fixed FIN
deadlines, and cancellation/replacement. Every case also completes an unrelated
track on the same connection. See [the new evidence](implementation-evidence/2026-08-31-terminal-subscription-updates/README.md).

The unchanged reference still loses the delayed tail; candidate success does not
close that gate. Broader impairment, source replacement, backward seeks, subscriber
combinations, and fault churn remain unverified. No upstream submission, pin change,
host rollout or device access occurred. All remaining allocation/security, speech,
physical shell/app, latency and release-soak requirements in the full plan remain.

## Previous validated checkpoint: isolated terminal receive candidate

A maintained opt-in patch now corrects the reference receiver's premature END/FIN
completion. It retains subscription ownership until group readers settle and the
promised range resolves, with bounded interval/reader tracking, explicit DROP,
cancellation cleanup and a fixed drain deadline after FIN. END alone can still
park a capped subscription. Ordered and arrival readers preserve the real final
boundary when trailing groups were explicitly dropped. Cancelled datagrams are
rejected before model mutation.

A fresh copy prepared from the patch passes all 18 ordinary native cases, 100
engine exchanges with four concurrent peers, and 27 delayed-reader cases. The
reference unit suite passes 849 tests, including 100,000-group bounded bookkeeping,
exhaustion, timeout, cancellation, dropped-tail and establishment regressions.
The unchanged pinned peer still fails the same delayed-reader test. These results
validate the isolated candidate, not unmodified-reference conformance.

Preparation and validation tools preserve the baseline lock and source. No upstream
PR, pin change, production host rollout or device action is performed. Broader
real-transport DROP/reset, impairment, source-change and subscription-update tests
remain necessary before adoption; datagram loss handling remains conservative.
All physical, speech, TLS/native allocation, latency and release gates remain
open. See [the candidate evidence](implementation-evidence/2026-08-31-terminal-drain-candidate/README.md).

## Previous checkpoint: terminal loss localized to reference reader scheduling

A fresh 100 serial engine exchanges pass, but a 40-connection concurrent loopback
run reproduces terminal loss while the sender reports all 11 groups retired with
no expiry, failure, cancellation or cache drop. The reference returns only groups
3–8. The missing sender diagnostics from the previous failure are now captured.

A maintained diagnostic delays one accepted reference UNI reader by 200 ms,
without delaying QUIC ACKs or changing bytes. Eight of nine selected-reader cases
fail. The final reproduction drops a not-yet-read stream and misses audio group
7 even though the sender has retired every group. The reference marks the model
complete and tears down subscription ownership before all its group readers run.

Normal tests keep the raw pinned reference transport; the scheduling wrapper is
explicit diagnostic code only. Its missing-group result exits nonzero and is not
accepted as a passing negative test. A receiver-side drain/ownership correction
is still required; arbitrary sender delays and relaxed checks are not introduced.
No reference revision, watch firmware, enrollment or deployed host is changed.
See [the diagnostic evidence](implementation-evidence/2026-08-31-terminal-reader-race/README.md).

## Previous checkpoint: bounded QUIC heap, catalog fallback and remaining tail race

The 128 KiB per-connection ngtcp2 allocation cap is now running in the complete
Ultra firmware (flash35). Startup and five clean microphone/tone round trips
sample a 30,344-byte peak; three round trips with 5% UDP loss and 120 ms added RTT
sample 34,216 bytes. No budget denial or platform allocation failure is observed.
Both audio runs pass cancellation/replacement, forced reconnect, fresh-grant
renewal, exact capture/playout sample counts and zero playback queue overflow.
Microphone PCM is counted/discarded. The impaired run uses synthetic tones and
has late/concealed packets; it does not close the impaired-speech quality gate.

The plain-catalog matrix failure was a stale test expectation: a recoverable
stream reset no longer emits a global service error. Tests now require exactly
one actual compressed-to-plain retry (zero for normal catalogs), using a service
counter. A new regression also exposed reuse of the compressed decoder after
switching tracks; fallback now resets its snapshot state. Raw and mapped errors,
an existing compressed snapshot, and a subsequent plain-track failure are tested.

A separate intermittent engine failure exposed END overtaking older media
under newest-group priority. The pinned receiver can report completion when it
sees END and the highest group, even while earlier groups are in flight. The C
publisher now delays END until all outstanding groups retire or are abandoned
with DROP. Media scheduling/deadlines remain unchanged. A regression test holds
an older group after the newest retires and requires END to remain withheld.
The reference source is unmodified and all exact group/payload checks remain.
However, an additional engine repeat fails after 15 passes: the reference reports
only groups 5–8 before completion. Delaying END does not fully resolve the race;
the terminal-delivery interoperability gate remains open. Transport retirement
alone cannot be treated as proof of application consumption. Added sender
retirement diagnostics pass 30 subsequent repeats; that does not erase the failure.

All 54 cases in three complete native interoperability runs pass, but the later
repeat failure prevents a claim of stable terminal interoperability. The actual
adapter and seven host suites pass normally and under Linux ASan/UBSan, including
real ngtcp2 constructor failure sweeps. The latter prove allocator cleanup,
not TLS/network failure injection. The firmware build and app0-only flash pass;
NVS, bootloader, OTA metadata and package/user storage are not written.

Complete device telemetry samples show minimum internal free heap of 101,052
bytes and a minimum largest internal block of 62,464 bytes across these runs.
These short samples do not establish all workload/soak headroom. The ngtcp2 cap
excludes TLS/crypto-provider allocation, system allocator metadata, sockets,
fixed adapter pools and the native Rust host. Those allocation gates, impaired
speech, the full impairment matrix, physical buttons/touch/apps, long responses,
latency and release soaks remain open. WebRTC remains the configured default.

The watch is re-enrolled at revision 110 to the persistent supervised MoQ host,
reconnects without capture, and retains flash35. Both host service processes are
unchanged. Firmware restoration is not required. Source/build hashes, test
scope and numeric evidence are in
[the checkpoint evidence](implementation-evidence/2026-08-31-quic-heap-and-catalog/README.md).

## Previous checkpoint: paired host supervision and local deployment

The explicit Mac MoQ installer now deploys a pinned release endpoint alongside
the Python service, under its own launchd label and data directory. Python starts
HTTPS/WSS/private IPC before native QUIC starts; inherited readiness/lifetime
channels coordinate the two processes. Either child dying retires the pair,
including old grants, and parent death is detectable without relying on PID
files. Native children do not inherit provider or personal signing keys.

The local service is installed with a separate maintained-development trust
profile and higher-revision watch enrollment. The existing WebRTC service and
default selection remain unchanged. Physical fault injection kills the deployed
native endpoint: both old children exit, launchd starts a fresh pair, and the
Ultra reaches full media readiness 13,569 ms after the kill, or approximately
6,917 ms after the pair reports listening. No capture starts. This is session
recovery evidence, not a new physical PTT/provider-turn acceptance result.

Reinstall testing also exposed asynchronous launchd shutdown: a stopped job may
still hold its profile lock. Installation now waits at most 40 seconds before
replacing Python files, and refuses to bypass a live lock. Binary hashes, private
config generations and codec notices are checked/copied; temporary IPC paths are
unique per invocation. The local leaf certificate expires September 30, 2026;
renewal remains explicit and the CA signing key is not deployed.

Verification passes 342 Python tests, eight native integration cases, 28 Rust
tests in debug and release, Clippy and the optimized endpoint build. The new full-process tests check
crashes of either child, bounded startup/shutdown, parent SIGKILL, and rejection
of old WSS/MoQ grants after restart. Installed-app/UI interaction, impaired speech,
long responses across renewal, native allocation limits and release soaks remain
open. See [host supervision](moq-supervised-host.md) and the
[deployment evidence](implementation-evidence/2026-08-31-supervised-host/README.md).

## Previous checkpoint: media admission and QUIC pacing

Two scheduling regressions are fixed: obsolete media is retired before fresh
TX admission, and ngtcp2's pacing timer advances once per bounded packet batch
instead of after each packet. Tests reproduce both old failures and cover
blocked reset/control work, byte and packet limits, retained datagrams and
exactly-once retirement. Host and adapter tests, Linux ASan/UBSan and firmware
builds pass; the existing storage and media/loss deadlines are unchanged.

The resulting flash34 firmware is installed on the Ultra. p74 passes three
complete provider turns without induced impairment: zero word errors, no
capture or playback loss/concealment, fresh watch reads and idle reconnect.
However, p72 and p73 still fail at 5% loss plus 120 ms added RTT and an 800 ms
uplink blackout. p72 passes one turn then exceeds the next capture's loss
budget; p73 completes playback but exceeds the speech-error gate. No impaired
speech improvement is claimed. Full replacement acceptance remains open; see
the [scheduling and hardware evidence](implementation-evidence/2026-08-31-pacing-batch/README.md).

## Previous checkpoint: capture PLC amplification and a measured speech gate

A no-loss control separates capture quality from the earlier impaired run's
full-scale PCM. p67 recognizes the fixture but times out during the model
response. The provider bench now applies a fixed six-word edit-distance gate:
zero word errors without induced impairment; at most one under impairment,
while preserving the ordered phrase “next exercise set.” p68 passes three full
provider turns with zero word errors and no concealment. This replaces the old
single-keyword acceptance check without altering provider input or responses.

A synthetic test of the actual native decoder reproduces excessive amplification
after packet loss: clean peak 1,009 versus impaired peak 32,766. Switching the
capture wrapper from the reference's transpiled decoder to a pinned, statically
built C libopus decoder fixes that regression (impaired peak 1,081), retaining
exact 20 ms PLC and the existing loss limits. The reference encoder, media wire
and watch firmware are unchanged. Source/dependency hashes and license notices
are recorded, and native tests, Clippy and the endpoint build pass.

p70 physically confirms normal signal levels under 5% loss, 120 ms added RTT
and an 800 ms uplink blackout, but still fails speech acceptance: WER 0.5 and
14.6% concealed capture PCM. p71 passes three complete clean provider turns
with zero word errors, exact speaker receipts, fresh watch reads and idle
reconnect. Its first empty reset group is lost without PCM loss; that protocol
loss is still recorded. The decoder defect is fixed; impaired speech, transport
pressure and the full replacement gates remain open. See the
[PLC and speech-quality evidence](implementation-evidence/2026-08-31-capture-plc-quality/README.md).

## Previous checkpoint: stream reset isolation and recovery scheduling

t65 reproduces the disconnect with zero local control deadlines and zero control
TX expiries. Inspection finds that `esp_moq_service_retire` emitted a global
SERVICE_ERROR for every reset, including an expired audio group. The previous
catalog-timeout attribution was incorrect: the event did not identify a catalog
operation. The service now routes retirement through the engine's operation
handling. Receive-group loss yields GAP, outgoing groups retire their TX job,
and catalog/discovery or receive-control failures remain visible at their proper
scope. This error-scope fix does not increase queues or audio loss/storage limits.

Targeted regressions fail before the correction and pass afterward, including
raw and mapped errors, late TX reset acknowledgements, continued audio, and
single terminal outcomes for failed controls. Normal C host suites and Linux
normal/ASan/UBSan adapter and host suites pass. Flash29 builds, writes only app0
and boots the full shell. Follow-up flash32 described below is now installed.

t66 passes 5% loss, 120 ms added RTT and an 800 ms uplink blackout. The failed
capture aborts while preserving the session; fresh audio resumes 1,193 ms after
restoration. Three four-second captures and replies pass, followed by response
replacement and fresh-grant lease renewal. Four 16,037-sample tones have zero
playback pressure and silence, with 3/1/2/2 concealed packets. These are counted
microphone captures and synthetic replies, not physical PTT or calibrated speech
quality evidence.

Provider p63 preserves the session and aborts the interrupted turn without an
STT commit. Its next capture loses the first 20 groups and aborts as well; zero
provider turns complete. No watch media-service failure occurs. That failure
is not waived: high-loss speech recovery remains open. New adapter counters
distinguish local stream-slot pressure, peer stream-credit pressure and payload
block pressure without changing any pool limit.

p64 measures local slot pressure with up to nine stopped send halves occupying
slots. A bounded retirement reserve adds 16 metadata slots (896 bytes on the
host ABI) while preserving the active limit, peer reply reserve, 64 payload
blocks and ACK ownership. p65 still fails on a later 21-group gap. Another
regression reproduces fresh-media starvation behind one DROP notice per owner
poll. The engine now batches those standard messages into one existing owned
control write, preserving every notice and backend backpressure handling.

Flash32 contains both scheduling changes and boots the full shell. p66 recovers
fresh audio in 710 ms and completes its next capture, one STT commit, the required
watch-state read, and a 50,832-sample response with zero playback pressure and
silence. However STT does not recognize the spoken fixture, so the provider
bench fails before completing its three required turns. The completed capture
contains 9,600 concealed samples out of 59,680 (16.1%). Capture loss and speech
quality remain unaccepted; neither successful playback nor exact counts waive
that failure. The final source passes normal and Linux ASan/UBSan adapter/host
suites, targeted before/after regressions, and the firmware build. Mac audio is
restored; firmware stays installed. See the
[stream-reset evidence](implementation-evidence/2026-08-31-stream-reset-isolation/README.md).
The full replacement plan remains open and WebRTC remains the configured default.

## Previous checkpoint: idle capture demand and timeout diagnostics

The native actor now polls its idle audio subscription before reporting media
readiness, then maintains that demand without decoding or forwarding microphone
PCM. A regression against the pinned scoped origin reproduces the old missing
idle demand. Active and idle subscriptions now request the same 200 ms recovery
budget; this transmission-budget change remains experimental and has not passed
hardware acceptance. Decoder concealment and storage bounds are unchanged.

Follow-up provider and native benches with 5% loss, 120 ms added RTT and an
800 ms uplink blackout fail. t64 records timeout code 3 escalated into a media
service failure. Its initial attribution to a catalog subscription was incorrect;
the stream-reset investigation above identifies the unscoped error path.
New engine counters distinguish
local control deadlines from queued control-transmission expiry; bounded native
close diagnostics exclude arbitrary peer/backend text.

Flash28 builds, writes only app0 and boots the full shell. It contains the new
timeout counters, but no subsequent impairment run has exercised those counters
at this checkpoint. Current verification passes 26 Rust tests and Clippy with
warnings denied, plus all seven normal C host suites. Earlier native integration
and firmware-parser checks pass 32 cases; sanitizers have not been rerun for the
new counter changes. See the
[idle-demand checkpoint](implementation-evidence/2026-08-31-idle-capture-demand/README.md).
The full replacement gates remain open and WebRTC remains the configured default.

## Previous checkpoint: capture failure isolation and stopped-stream recovery

Native capture loss beyond the unchanged 200 ms concealment budget now aborts
only that turn. Identity-bound failure receipts cancel queued PCM, STT and reply
work without retiring the authenticated session. A fresh PTT identity is required;
capture never restarts automatically. Watch failure receipts include identity
and start ID, preventing delayed cancellation from retiring a newer turn. Fresh
identities also clear unfinished provider audio if an old failure callback was
superseded. Firmware and host must be updated together.

t61 passes an 800 ms uplink blackout followed by three capture/tone cycles on the
same session; fresh audio resumes 231 ms after restoration, with zero playback
pressure or silence. Provider p58 passes the blackout, three complete voice/tool/
speech turns and reconnect, with 721 ms capture recovery.

The harder p59 run identifies a separate watch bug: a write to a peer-stopped
stream is misclassified as connection-fatal `INVALID_STATE`. The adapter now
reports a terminal stream outcome and TX cancels only the affected job, retaining
backend buffer ownership until ACK/close. Regressions fail before and pass after
the correction. Flash26 is installed and boots the full shell.

p60, with the fix, 5% loss, 120 ms added RTT and an 800 ms blackout, recovers in
923 ms and completes two provider turns. Its third capture exceeds the loss
budget and aborts without another STT commit or watch endpoint failure. The
three-turn bench still fails; high-loss quality/reliability acceptance and the
original p50 fault remain open. See the
[capture-failure evidence](implementation-evidence/2026-08-31-capture-failure-isolation/README.md).
Verification passes 305 Python tests (four warnings), 24 Rust tests and Clippy,
six native integration cases, 26 firmware-parser cases, the firmware build,
and normal plus Linux ASan/UBSan adapter/host suites. WebRTC remains the
configured default and the full replacement plan is not complete.

## Previous checkpoint: response subscription readiness

Follow-up diagnostics reproduce the capture failure in p56 at 5% loss and
120 ms added RTT: eleven consecutive unavailable audio groups exceed the
unchanged ten-frame concealment budget. Proxy forwarding timers are at most
3.326 ms late, while the event loop's maximum measured lag is 54.349 ms; the
source of the missing groups is still under investigation. p55 separately fails
the required fresh watch-state read. Neither provider failure is resolved.

The native-only bench now supports a generated reply after every capture.
t59 and t60 pass ten 1.2-second and twenty 4-second capture/reply cycles,
respectively, under the same impairment parameters, with zero playback pressure
or silence. These are host-triggered synthetic replies, not physical PTT or
acoustic echo tests; microphone PCM is counted and discarded. The new proxy
timing counters distinguish requested network delay from forwarding-timer delay.
Checkpoint verification passes all 300 Python tests (four warnings), 23 Rust
tests and Clippy with warnings denied. The hardware/build evidence below records
the earlier readiness-fix validation separately.

Physical diagnostics reproduce playback queue pressure caused by subscription
startup: paced host audio accumulates before TRACK/SUBSCRIBE completes, then
arrives in a burst. t56 receives its first eleven packets in 6.7 ms, before any
playout. Its two exact-count tones nevertheless contain 38 and 30 silence chunks.

The native endpoint now primes only a standard empty reset group during response
preparation; the watch acknowledges binding after `MEDIA_READY` initializes its
player. Paced PCM starts after that acknowledgement. This avoids a readiness
deadlock in the pinned Lite05 protocol and keeps PCM gated by watch authorization.
The ten-packet queue, 60 ms prebuffer, codec and 200 ms loss bounds are unchanged.
Host endpoint and firmware require coordinated updates. Flash23 is installed.

t57 (3% loss, 60 ms added RTT) plays two exact tones without pressure, silence or
concealment. t58 (5% loss, 120 ms added RTT) passes an explicit zero-pressure gate;
its two tones have no silence and three concealed packets each. Both pass
cancel/replace and forced/expiry reconnect checks. Provider baseline p53 passes
text, background speech and three complete voice/tool/speech turns with five
played history entries and no concealment, silence or pressure.

The impaired provider run p54 does not pass: after one complete speech turn with
zero pressure/silence, its second capture exceeds the existing concealment budget.
The native failure is `capture loss budget`. This is distinct from the unresolved
p50 watch transport fault; neither failure is waived. Prior intermittent missing
fresh watch-state reads also remain open. Exact counts and zero queue pressure
do not substitute for a calibrated speech-quality threshold or the full matrix.

Verification passes 23 Rust tests, Clippy, 299 Python tests (four warnings), six
native integration cases, 26 firmware-parser cases and the firmware build. See
the [response-readiness evidence](implementation-evidence/2026-08-31-response-readiness/README.md).
Mac audio is restored and new firmware stays installed; firmware restoration is
not required. The complete acceptance plan remains open and WebRTC is still the
configured default.

## Previous checkpoint: capture burst recovery and transport diagnostics

The host capture worker now handles a queued burst behind a missing group without
prematurely rejecting the capture. A deterministic regression passes with exact
12,807-sample output, one lost group and 320 PLC samples; the existing 32-handle
storage and 200 ms per-gap concealment bounds remain. Flash21 records numeric
adapter failure details so the original intermittent transport error can be
attributed if it recurs. That error remains unresolved.

With 3% loss and 60 ms added RTT, t54 passes 20 short captures and t55 passes three
10-second captures, each followed by tone playback, cancellation/replacement,
forced reconnect and lease-expiry reconnect. These are not complete PTT/echo
cycle counts. Playback pressure remains nonzero, so exact sample counts do not
satisfy the speech-quality gate. Provider runs p51 and p52 fail the required fresh
watch-state read on the third turn; p52 also exposes high playback pressure at
5% loss and 120 ms added RTT. Neither provider failure is waived.

Verification passes 22 Rust tests, Clippy, 299 Python tests, six native integration
and 26 firmware-parser cases, the firmware build, and normal plus Linux
ASan/UBSan adapter/host tests. See the
[capture-burst evidence](implementation-evidence/2026-08-31-capture-burst/README.md).
The new firmware stays installed; restoration is not required. All remaining
replacement acceptance gates remain open and WebRTC is still the default.

## Previous checkpoint: authenticated watch playback timelines

A deterministic real-Opus regression reproduces the 320-sample shortfall:
dropping the first packet reduces a 1,607-sample response to 1,287 samples when
playback anchors to the first arrival. The native endpoint now supplies the
encoder epoch in authenticated `playback.begin`; the player preserves that start
and conceals bounded missing audio. The audio owner also receives the exact end
sample count, allowing at most 200 ms of lost-tail recovery and rejecting a
conflicting Hang terminal marker. Host completion still requires the exact number
of samples actually handed to the speaker and drained from DMA.

The corrected app0 firmware (flash20) boots the full shell. Physical p48 passes
text/background output, three voice turns and reconnect without induced loss.
p49 passes three voice turns and reconnect with 1% loss and 30 ms added RTT,
despite six concealed/late playback frames. Together these runs record 360,640
microphone samples and 318,301 played samples, with exact host/watch totals.
The seeded rerun is not a byte-for-byte replay of the earlier network schedule.

p50 at 3% loss and 60 ms added RTT fails during the first capture, before STT
commit. The watch logs a transport error (12), one expired publisher group and
no microphone drops; the host then observes catalog withdrawal. The causal
transport failure remains unresolved. The 5%/120 ms case is deferred until that
failure is understood. These results do not satisfy the complete loss/RTT or
speech-quality matrix, and WebRTC remains the configured default.

Verification passes 299 Python tests (four existing warnings), six native
integration cases, 26 firmware-parser cases, 21 Rust tests, Clippy, the ESP-IDF
build, and normal plus Linux ASan/UBSan audio tests. Audio loss tests compare PCM
against independent direct Opus PLC calls, in addition to exact sample counts.
See the [playback-timeline evidence](implementation-evidence/2026-08-31-playback-timeline/README.md).
The new firmware remains installed; restoration is not required. All other
library/product acceptance gates remain open as recorded in the full plan.

## Previous checkpoint: bounded host capture loss recovery

The native capture worker now bounds reordering and incomplete-group waits to
200 ms, advances Opus by exactly one 20 ms packet for declared loss, ignores late
abandoned groups, and uses the authenticated end boundary to recover a bounded
lost tail. Malformed complete groups and excessive gaps still fail. Recovery
counters are validated by the Python adapter before STT commit. A seeded,
single-watch UDP impairment fixture exercises encrypted media without changing
the authenticated control path.

This checkpoint passes 293 Python tests (four warnings), six native integration
cases, 21 Rust tests, the native build and Clippy with warnings denied. Physical
run p45 exposed a closed-schema mismatch for the new counters, which was fixed.
The corrected loss-free p46 run passes text/background output, three voice turns
and reconnect without changing firmware.

The first impaired run, p47 (1% loss, 30 ms added RTT), does **not** pass. Host
capture recovery conceals one missing 320-sample packet and ignores its late
arrival; two complete voice turns succeed. The third response produces 47,201
host samples but only 46,881 watch playback samples, so completion is correctly
rejected. The suspected initial playback-timeline loss remains unconfirmed and
unfixed. The full loss/RTT/speech-quality matrix remains open; this commit is a
work-in-progress checkpoint. Raw provider/serial logs and credentials stay
private, and ambient microphone PCM is not persisted. No firmware restoration
is required.

## Previous checkpoint: physical output-only speech and durable delivery

The new output-context firmware is installed on the Ultra (app0 only) and boots
the shell. Text speech and idle background announcements pass before any
microphone capture. A second run holds real background TTS, cancels it, and
releases the stale frames during a fresh watch context. The announcement stays
pending, the production idle loop retries it, and only the retry reaches played
history. Both runs then complete three microphone/tool/speech turns and a
fresh-session reconnect without rearming the microphone.

This exposed and fixed standalone TTS history ordering: the history-flush marker
must wait behind held words and the speaker receipt. MoQ durable announcements
and question focus now wait for acknowledged playback too. Cancelled transport
drains report unsuccessful playout. Tests cover these boundaries and pending
notifications across restart. Delivery remains at least once across a crash
between audible playback and the database acknowledgement.

The checkpoint passes 284 Python tests and six native integration cases. The two
successful physical runs deliver nine responses, including three output-only
responses, with 364,000 microphone samples and 346,136 played samples; firmware
and host totals agree. A startup path-length failure and the earlier history
failure remain recorded in the [output-context evidence](implementation-evidence/2026-08-31-output-contexts/README.md).

The new firmware remains installed; no restoration is required. WebRTC remains
the configured default. Controlled-loss recovery, continuous long responses and
lease renewal, allocation limits, broader security, deployment, physical full-shell
controls/apps/sleep-wake, latency and endurance remain open acceptance gates.

## Previous checkpoint: output-only response contexts (before hardware validation)

Text and idle background speech now request a watch-issued response context
before entering the output pipeline. The control protocol correlates requests,
ready/busy receipts and cancellation. The native endpoint authorizes playback
without starting a capture reader; the firmware audio owner acknowledges the
context without activating the microphone. These contexts share the watch's
monotonic identity namespace with captures. Native IPC retains the legacy
`capture_id` field for media correlation; an output-only context does not create
a microphone capture or synthetic input audio.

The current checkpoint passes 267 Python tests (four warnings), six native
integration cases and sixteen Rust tests. The native output-only case checks
537 output samples against the reference codec and zero input samples. The
ESP-IDF firmware build passes. This response-context firmware has not been
flashed or validated on the Ultra; conversation-level text/background tests and
physical output-only acceptance remain unfinished. WebRTC remains the default,
and the full replacement goal remains open. No firmware restoration is required.

## Previous checkpoint: model, tool and TTS capture ownership

The previously staged provider adapters are now integrated into the MoQ path.
Capture ownership follows aggregated model context, tool runners/results, TTS
contexts, audio delivery and played assistant history. Cancelled work is checked
again across asynchronous waits; the MoQ writer also rejects an action that was
cancelled while queued. Already-issued actions may have completed, and accepted
durable jobs survive foreground cancellation.

Synthetic reproductions exposed both a stale tool invocation and late TTS audio
write. A separate TTS receive race could overwrite replacement word alignment;
alignment now belongs to each TTS context. The full suite has 261 passing Python
tests and five passing native integration cases.

Six physical provider runs pass, covering eighteen complete fresh tool/speech
turns and six new-session reconnects. They include delayed real tool callbacks
and delayed real TTS start/audio released after cancellation during replacement
captures. The final TTS run also verifies one played assistant-history message
per fresh turn. One intermediate repeat failed because the model omitted a new
watch-state read; the failure remains recorded, and the freshness instruction
was corrected before subsequent passing runs. See
[provider capture evidence](implementation-evidence/2026-08-31-provider-capture-ownership/README.md).

No firmware was flashed. WebRTC remains the default. Text/background response
authorization, controlled-loss recovery, long responses/lease renewal, allocation
limits, broader security, deployment, physical full-shell controls and endurance
remain acceptance work; these results do not close those gates.

## Previous checkpoint: STT capture ownership and cancellation

A reproduced cancellation bug allowed a delayed STT final to reach routing
after its capture was cancelled and a new capture had started. The MoQ STT
adapter now binds provider item IDs from commit acknowledgements to the
originating capture, preserves that identity on queued transcript frames, and
checks it again after asynchronous callbacks. Cancellation invalidates the
capture before waiting for device stop. WebRTC retains its existing STT path.

Only one commit can await acknowledgement; a missing or inconsistent receipt
retires the STT socket before another commit can be associated with it. Normal
turns retain the existing provider connections, model pipeline and history.
The STT resampler now has per-capture history, flushes the final samples before
commit, and never clears buffered samples merely because scheduling pauses.

Three complete provider turns pass on the Ultra. Three further physical runs hold
a real STT final in memory, cancel that capture, and release the old event
during a new capture. Each rejects the stale final and completes three fresh
tool/TTS/playback turns plus a new-session reconnect. No transcript is injected
or saved, and no firmware is flashed. The checkpoint has 234 passing Python
tests; see [STT capture evidence](implementation-evidence/2026-08-31-stt-capture-ownership/README.md).

This establishes the tested STT cancellation boundary, not complete provider
generation isolation. Cancellation after model/tool work has already started,
TTS generation ownership, text/background response contexts, transport loss
recovery and the remaining release gates still require work.

## Previous checkpoint: bounded publisher catch-up and repeated capture

Publisher diagnostics now distinguish cache-drop ranges, enqueued TX expiry,
failure/cancellation and backend submission/retirement. A deterministic test
reproduces a scheduling shortfall after a 160 ms network-owner pause: eight
20 ms groups are ready, but the old four-group poll cap cannot drain them.
Publisher polling now fills its existing bounded TX slots while retaining the
control reserve, backpressure behavior, queue sizes and live-media deadlines.
The 60 ms and 160 ms delayed-poll tests both pass.

The updated full-shell image passes three complete physical provider turns and
fresh-session reconnect: 181,600 microphone samples and 124,659 played samples.
A separate test completes 100 one-second captures with the listening UI active,
1,600,000 samples and no microphone drops/discards, publisher cache drops or
TX expiries/failures. It then passes exact playback, cancellation/replacement
and lease reconnect (5,371 ms). This is 100 captures, not 100 complete echo
cycles, and does not satisfy the 1,000-cycle or eight-hour acceptance gates.

Seven C host programs pass both normally and with ASan/UBSan in an isolated,
network-disabled Linux container. The macOS ASan attempt stalled in sanitizer
initialization before main and was terminated; it is not counted as a pass.
The adapter/audio suites, 209 Python tests and five native integration cases
also pass. See [publisher catch-up evidence](implementation-evidence/2026-08-31-publisher-catchup/README.md).

The earlier intermittent missing-group run is not conclusively attributed to
this scheduling defect: the new counters did not observe another failure.
Controlled-loss recovery, broader repetition and the remaining provider,
security, allocation, deployment and physical full-shell gates remain open.

## Previous checkpoint: physical provider turns and filtering correction

The full microphone → STT → model/read-only exercise tool → TTS → Ultra speaker
path now passes with the real providers. An in-memory comparison confirms the
microphone captures the generated fixture at the expected speed. The received
signal has little energy above 1 kHz; the existing provider `near_field` filter
misrecognizes it. Turning that filter off recognizes all six fixture keywords
and completes `get_next_set`, playback and a fresh session reconnect.

MoQ explicit push-to-talk now defaults to no provider noise filtering; WebRTC
retains its existing `near_field` setting. `DOODAD_STT_NOISE_REDUCTION` permits
an explicit `off`, `near_field` or `far_field` profile. A physical run without
any bench/configuration override passes with 60,480 microphone samples and
41,149 speaker samples. A subsequent two-turn run passes both tool calls and
separate playback receipts, totaling 121,120 microphone and 81,088 speaker
samples, then reconnects without recording. The installed image is unchanged.

Repeated operation is not yet reliable: an earlier two-turn attempt passed its
first turn but lost capture groups during the second. Native capture expired
waiting for group 261 with group 288 already buffered; firmware reported no
microphone drops and a maximum network-owner poll gap of 162 ms. This remains
an open transport/scheduling failure, not a fixed issue or a passing loss test.
The bench now requires fresh STT, tool and playback evidence for every turn.

209 Python tests pass, including unchanged WebRTC filtering, explicit MoQ
profiles and synthetic acoustic-analysis checks. No captured microphone PCM is
written to disk. See the [physical provider evidence](implementation-evidence/2026-08-31-provider-turns/README.md).
Physical PTT/navigation, provider generation isolation, transport loss recovery,
deployment, long responses and the remaining release gates are still open.

## Previous checkpoint: audio catch-up and discontinuity timelines

The real-provider terminal failure is now identified: resetting Opus discarded
lookahead, while the watch reported accepted-input samples as received PCM.
The audio owner also processed at most one 10 ms microphone chunk per RTOS tick,
preventing recovery after an encoding/network delay. It now drains up to four
chunks per cycle; a spoken capture transfers 60,320 samples with no microphone
drops and a validated STT commit. The transcript is still empty.

Capture completion now reports a separate timeline count. The native decoder
preserves bounded gaps across codec resets with silence and rejects excessive
or regressing timestamps. Ten consecutive three-second captures pass with the
listening shell active (480,000 samples, no drops), followed by exact playback,
cancellation/replacement and lease reconnection. Missing-group PLC remains open.
The previously omitted WebRTC 8x microphone gain has also been restored in the
MoQ input path. With priority 6 for the audio owner and the first 20 ms silenced
for microphone wake-up, the latest provider run captures 60,000 samples without
drops or clipping and plays 20,573 synthesized samples. Its six-character STT
result does not recognize the exercise fixture and does not invoke the required
tool, so provider-turn acceptance still fails. Speech recognition is the next
physical gate; the host-driven fixture has not proven conversational readiness.

Sixteen Rust tests, the C audio/host/adapter/touch suites, 199 Python tests and
five native integration cases pass. This does not complete the provider turn
or release gates. See the [capture timeline evidence](implementation-evidence/2026-08-31-capture-timeline/README.md).

## Previous checkpoint: capture ordering and publisher throughput

Native capture handles bounded group reordering, and the C publisher sends
bounded catch-up batches after delayed network polls. The physical listening
shell now passes three-second capture and a separate run of 20 consecutive
one-second captures (320,000 samples), followed by exact-tail playback,
cancellation/replacement and lease-expiry reconnection. An owner-revision race
that could erase pending starts and an invalid touch-report palm interpretation
have also been corrected. All seven C host programs, thirteen Rust tests, the
new touch decoder test and 199 Python tests pass; the Ultra application builds.

MoQ STT now commits on validated PTT completion instead of local VAD pauses.
The physical path still returns empty transcripts and sometimes retires near
capture completion; a synthetic-only probe confirms the same STT service can
transcribe the fixture. Acoustic capture quality and terminal diagnostics are
the immediate next work. A complete real provider voice turn, full lifecycle
isolation, production deployment and all release gates remain unproven. See the
[checkpoint evidence](implementation-evidence/2026-08-31-capture-ordering/README.md).
Older sections below retain their historical measurements and image identities.

## Implemented foundation

Changes are in the existing `libs/moq-esp32` checkout, based on `f827391`.
At the foundation checkpoint no VoiceWatch application code had changed. The Ultra memory probe, verified
network bring-up and 30-minute synthetic transport soak have completed. Both
soak endpoints report 90,000 streams and 180 resets per direction, with bounded
heap and teardown release. This does not establish audio or full-shell readiness.
The operational engine, owned media service and task endpoint now pass native
bidirectional interoperability against the pinned unmodified Rust endpoint.
The task endpoint also ran on the Ultra: both catalogs and eight synthetic Hang
frames per direction passed, with owned cleanup. This is an operational runtime
checkpoint; its original synthetic packets did not prove codec interoperability.

The next checkpoint adds real Opus receive playout: OpusHead pre-skip, separate
terminal/discontinuity events, frame ordinals, bounded jitter/PLC and cancellation
identity checks. Nine unmodified pinned Rust Producer/Consumer fixtures match
the C player's exact sample lengths and PCM. On the Ultra, the pinned Espressif
decoder passed all nine fixtures three times and PLC; worst measured decode was
2.6 ms per 20 ms packet. This standalone image uses no network or I2S. Capture
encoding was still pending at that checkpoint. Evidence is in the
[audio-player checkpoint](implementation-evidence/2026-08-30-audio-player/README.md).

The preceding checkpoint implements capture encoding, empty reset groups and
atomic terminal tails. A real-Opus network exchange passes on both the native
endpoint and physical Ultra: 537 synthetic input samples reach the unmodified
Rust Consumer; the reference Producer's 537 response samples match exactly in
the C player. This establishes the combined codec/network path, not real
microphone/speaker operation, production authentication or full-shell readiness.
See the [capture and network evidence](implementation-evidence/2026-08-30-audio-capture/README.md).

**User direction: future tests leave our new firmware installed. Default-firmware
preservation and restoration are not requirements.** The old live runner restored
once at the end of the completed soak. Its replacement no longer requires a
backup/full-flash baseline comparison and validates live board/security/layout
metadata before app0 writes. Eight offline runner checks pass; the replacement
runner has now flashed and tested the operational endpoint on hardware. The
passing combined physical audio/MoQ echo image is preserved as a checkpoint. The latest installed firmware is the authenticated full-shell MoQ image described below.

## Physical Ultra audio and MoQ echo checkpoint

The optional Ultra BSP now implements verified PMIC speaker power, PDM capture,
bounded PCM ownership and I2S playback/cancellation. The current combined image
passes local acoustic tone/cancel/restart checks followed by a live microphone
→ reference Opus decode/re-encode over MoQ → Ultra decoder/speaker exchange.
It transfers 19,200 samples each way with no microphone drops. Speaker completion
is measured after DMA drains, and stale-owner handoffs are rejected.

Two 10 ms DMA buffers bound already-submitted playback; invalidation clears the
480-sample pending FIFO. The final run's stop fence took 633 microseconds. The
reference comparison has 43 samples differing by one 16-bit LSB (RMS 0.0473),
with exact sample counts. This is within the established codec tolerance, not
bit-exact or speech-quality evidence. The earlier echo run had 39 one-LSB
startup differences. Both raw recordings and credential-bearing images stay private.

The combined network exchange took 6,397 ms, including connection/record/play
work, and reached 97,900 bytes minimum internal free RAM. Free stack watermarks
were 41,160 audio / 7,400 network / 2,868 DNS bytes. These numbers exclude display,
Wasm and the full shell. No charging settings or non-app0 flash were changed.
The passing audio checkpoint image SHA-256 is
`5ad5e674f59fa13011efb526be42afbe0a5622caf62ec97b6512fc8f5e72d7c8`.

See [physical audio evidence](implementation-evidence/2026-08-30-ultra-audio-io/README.md).
All 18 native interoperability/security matrix cases still pass with the new
reference echo crate linked. Production auth/host, PTT, display/touch/buttons/
haptics, actual speech, sustained operational audio and full-shell acceptance
remain open. This is a bounded explicitly selected diagnostic, not automatic
runtime capture or the complete VoiceWatch replacement.

## Ultra UI and full-shell integration in progress

A separate optional UI BSP now owns CO5300 QSPI display, verified CST9220
410x502 touch, XL9555 expander, DRV2605L haptics, short power-key events and
battery reads. It shares I2C with audio; serialized PMIC read-modify-write
protects independent display/speaker rail bits. It does not alter charging or
upload peripheral firmware. The second physical startup probe passes all
205,820 pixel transfers and repeated touch-register polling with zero bus errors.
Touch/button presses and optical/haptic behavior have not yet been observed.
The first run failed an IDF zero-length SPI buffer check, now corrected.

VoiceWatch now has an Ultra board target and full 410x502 LVGL display. Two
physical shell runs passed one-minute heartbeats: first offline, then with Wi-Fi
and the fixed LVGL heap placed in PSRAM. WAMR started the embedded recovery app,
the native Home rendered, and existing `ffat` storage mounted without formatting.
There were no installed apps in this test profile and personal-install keys were
not provisioned, so this does not establish installed-app or delivery parity.

The first image exposed LVGL's 128 KiB pool occupying internal SRAM. Its supported
large-array annotation now places the same fixed pool in external BSS; the ELF
confirms `work_mem_int` at `0x3c2542e8`, size `0x20000`. No managed source or heap
size changed. The Wi-Fi baseline has 220,123 bytes free internal RAM, minimum
183,992 and largest block 147,456; PSRAM free is 7,912,024. These exclude MoQ,
Opus and WSS. Maximum observed flush was 6,417 us; initial rendering reached
187,893 us. Those are baseline observations, not final UI latency acceptance.

The historical Wi-Fi baseline shell image SHA-256 is
`469861f8d4cf5e827592dd4ec773e5398c084d3fe57599214c3ed39bd23b7371`.
That baseline kept microphone/speaker off and legacy WebRTC disabled. The
subsequent checkpoint below connects the MoQ audio owner; production
control/host are still missing. Network recovery
never erases NVS automatically. The Ultra table matches inspected live metadata;
only app0 was written. Build/flash/monitor scripts now accept `t-watch-ultra`,
with app-only flashing and a heartbeat check. See the
[full-shell baseline evidence](implementation-evidence/2026-08-30-ultra-shell/README.md).

## MoQ audio integrated into the full shell

The internal media seam now selects WebRTC or MoQ at build time while the shared
service retains control, capture/request/guest correlation, actions, NVS and
package behavior. The Ultra implementation owns its own 64 KiB PSRAM audio task
and the library endpoint, using the existing board's timestamped/fenced audio
API. It does not open a second board. Native cancellation invalidates media
before waiting for the control queue; playback completion follows DMA drain.

The latest physical diagnostic passes 19,200 microphone samples through the
pinned reference decoder/re-encoder and plays all 19,200 response samples with
zero microphone drops and zero concealed, late or pressure-discarded player
frames. Independent PCM verification found 38 one-LSB differences (RMS 0.04449),
within the established tolerance. WAMR, native Home, Wi-Fi and existing storage
remain active, followed by a one-minute quiet interval and joined endpoint
cleanup. This exercises the internal media API, not public Voice Orb/PTT control.

The first run passed audio but exposed a 510 ms flush and only 95,040 bytes
minimum internal free RAM. The second failed before capture due to codec startup
ordering. The third fixed startup and TLS/UI core contention but still reached
only 86,528 bytes internal free. With the task-only endpoint/configuration and
optional service arena in PSRAM, the fourth run reaches 114,792 bytes minimum
internal free, minimum largest block 81,920 and maximum flush 8,031 us. Worker
stacks and DMA remain internal. Initial rendering still reached 291,299 us;
WSS/bootstrap and active voice UI stress are absent, so final UI/resource gates
remain open. The readiness interval was 4,476 ms, not a passing connection-latency
result. Free stacks were 43,532 audio / 7,248 network / 2,888 DNS bytes.

The latest installed image is
`e78688cae157f1187591f9835a941ef1c438d90d0aafa918fac11c86543cb56c`.
It is an explicitly selected USB-provisioned public-token diagnostic. Production
MoQ currently fails closed instead of using anonymous legacy mDNS/WebSocket
control. The default Ultra profile still disables voice. No restored firmware,
NVS/package erase or certificate-validation bypass was required.

The MoQ ELF contains no peer open/send symbols. A separate CoreS3 WebRTC image
still builds with those symbols and no MoQ endpoint; it was not flashed. All 18
native interoperability/security cases, core/audio regressions, 97 existing
live-agent tests, five storage checks and five artifact-verifier tests pass.
The live-agent suite retains four warnings; no new host adapter is implied.
See [full evidence and failed runs](implementation-evidence/2026-08-30-shell-moq/README.md)
and [media ownership details](moq-shell-media-seam.md).

Authenticated bootstrap, credential/time/reconnect policy, the Rust/Python host
bridge and public response bindings are next. Real voice turns, interruption,
PTT, full UI/package parity and all sustained/impairment gates remain incomplete.

## Host audio, authentication and private IPC boundary

The next host checkpoint extracts PCM spooling/resampling/pacing and shared
control/action futures from aiortc. WebRTC is now an explicit optional extra,
with lazy imports; the existing service installer retains it. Exact final PCM
chunks and generation cancellation are available without RTP or its local
playout-delay heuristic.

Python now implements an HTTPS-only nonce/HMAC bootstrap, independent one-use
control/media grants, identity-bound WSS activation, monotonic/UTC lease checks
and per-device replacement. An owner-private bounded Unix IPC listener redeems
media grants only after WSS is live, checks session/sequence on every packet,
and revokes both channels on expiry, replay, malformed input or disconnection.
The suite passes **142 tests**, including 45 new real TLS/WSS, real Unix-socket,
security, capacity and exact-tail/cancellation cases. Four pre-existing product
warnings remain. The first IPC test attempt exposed macOS path-length limits;
tests now use short private paths and the server rejects oversized paths explicitly.

This Python boundary checkpoint alone is not a running MoQ live-agent endpoint.
The subsequent Rust checkpoint below adds the native media process. Application
capture/response binding, watch bootstrap/trusted
time, renewal/backoff, supervisor packaging and real voice-turn acceptance are
still open. Normal `serve` remains legacy WebRTC. No actual enrollment file,
provider key, deployed service or watch firmware changed in this host-only work.
See [host boundary contracts](moq-host-boundary.md) and
[verification evidence](implementation-evidence/2026-08-30-host-boundary/README.md).

## Native Rust host checkpoint

The library now includes a native `moq-lite-05` TLS/QUIC endpoint, private IPC
authorization, separate scoped origins and standard Hang/Opus capture and response
workers. Seven Rust tests pass; formatting and Clippy checks are clean. Repeated
responses preserve increasing media timestamps and exact sample tails, including
after cancellation. The separate native integration lane exercises real Python
HTTPS/WSS/IPC and QUIC with synthetic bidirectional audio, invalid/replayed tokens
and revocation teardown.

At this checkpoint the integration control driver was a test fixture and normal
`serve` still used WebRTC. The following checkpoint adds `MoqSession`; physical
watch bootstrap/control binding, provider turns and deployment remain open.
Upstream model/cache allocation limits also need hardening; an origin eviction
budget is not a hard memory cap. No service or watch firmware was changed.
See [the native checkpoint and limitations](moq-host-boundary.md#native-endpoint-checkpoint).

## Product adapter and service selection checkpoint

`MoqSession` and `MoqTransportServer` now connect authenticated WSS/native IPC to
the existing conversation callbacks, shared action futures and HTTPS artifact
routes. `serve --transport moq --moq-config ...` selects this adapter explicitly;
the default and current deployment remain WebRTC. Start/capture/response IDs,
bounded queues, native capture completion and watch playback receipts prevent
early STT termination or treating an empty host spool as speaker completion.

Tests cover real product-adapter QUIC audio and pre-bind cancellation, WSS
replay/identity/replacement, exact tails, stale receipts, action cleanup and
PTT cancellation overtaking an application callback. An old playback wait cannot
detach a newly bound utterance. Final counts and source hashes are in
[product session evidence](implementation-evidence/2026-08-30-moq-product-session/README.md).

Firmware control/bootstrap, provider turns and generation isolation, text-only/
background response ownership, time/credential policy, deployment, native
memory/loss hardening and full-shell release gates remain open. No service was
deployed, actual credentials read or firmware changed. See
[configuration and the firmware-facing contract](moq-product-session.md).

## Authenticated firmware bootstrap checkpoint (build-only, historical)

The Ultra firmware now implements physical USB enrollment into its own NVS
namespace, nonce-bound authenticated time, verified HTTPS bootstrap and WSS
control bound to the issued session. Capture/playback handlers validate start,
owner, response, range and sample identities. Lease expiry, profile changes and
control failures retire the media session; reconnection obtains fresh grants.
The host has an optional, separate time-proof listener with no capability routes.

Checkpoint verification: **191 default Python tests, 26 native C++ protocol
tests, five native integration tests and seven Rust tests pass**. Four Python
warnings remain. Native endpoint binaries/examples and the full Ultra firmware
build successfully. The image is 3,416,848 bytes, within app0's 4 MiB limit;
SHA-256 is `872524f1790ba155faa58e6908b50aec8beab70a41856905e85a3e19a2a99b20`.
The firmware image, private configuration and raw build log stay outside Git.

This new image has **not been flashed**. USB enrollment, authenticated startup,
reconnection and full control/audio behavior still require hardware validation.
The installed diagnostic image is unchanged. Provider turns, deployment,
credential lifecycle, native memory/loss hardening and the full release matrix
remain open. Firmware restoration is not required for future tests.

## Authenticated full-shell hardware checkpoint

The connected Ultra now runs the authenticated full-shell image. USB enrollment,
nonce-bound time, certificate-verified HTTPS/WSS and native MoQ readiness pass
against the real Python product adapter and Rust endpoint. No static enrollment
key is compiled into the image. Certificate date checks are enabled and enforced
by a build guard. Expired, not-yet-valid, wrong-hostname and untrusted certificates
were rejected on hardware after time-proof issuance, without an authenticated
session or microphone capture.

The final audio run received 19,200 microphone samples and completed a
16,037-sample tone with zero reported concealment, late frames or pressure drops.
It then cancelled an active response and completed a replacement on the same
captured turn. Scoped cancellation now preserves that completed context while
fencing old media/DMA work; general cancellation still invalidates it. Forced
reconnect took 6,521 ms, and lease expiry led to a third fresh session without
recording. These are individual measurements, not p95/soak acceptance.

Both Ultra and legacy CoreS3 builds pass. The default Python suite has 191
passing tests, the explicit C++ parser lane 26, and Rust seven. Five native
integration cases passed ten consecutive runs after fixing their rejection
probe's close race and adding an independent issuer-denial assertion. See
[hardware evidence, failures and hashes](implementation-evidence/2026-08-30-ultra-authenticated-session/README.md)
and [enrollment/run instructions](moq-ultra-enrollment.md).

The bench hosts are stopped; the watch retains the new firmware and private test
profile and waits for a reachable host. The deployed service is unchanged.
Provider turns/generation isolation, text/background responses, proactive renewal
and continuous long speech, full app/UI behavior, native allocation/loss hardening,
connection latency, deployment and release soaks remain open. No restoration is
required, and this checkpoint does not declare the full replacement complete.

## Provider-session checkpoint — 2026-08-31 (in progress)

Provider callbacks now bind to their originating watch session. Reconnect retires
the old pipeline and creates a replacement while preserving conversation history
and durable job state. Retired callbacks cannot send audio/actions to the new
session. The sink also rejects mismatched TTS contexts, and an old playout drain
cannot clear a replacement's pending text. Five regression tests cover these
ownership paths; the default Python suite now passes 196 tests with four warnings.
This does not yet establish complete generation isolation within one session.

The new `moq_provider_bench.py` runs the real service/provider path with temporary
credentials, an isolated database, read-only tools and host-initiated capture on
the physical Ultra. Two attempts authenticated and received microphone PCM, but
both disconnected before final transcription or playback. Fixed-category native
diagnostics identified `capture gap/range` in the second attempt. The media
ordering/loss path remains unresolved; no successful end-to-end provider turn is
claimed. Private provider logs, enrollment profiles and audio stay outside Git.

The installed firmware and deployed service are unchanged by these attempts.
Physical-button acceptance, the remaining release gates above and a successful
STT/model/tool/TTS turn remain outstanding. Firmware restoration is not required.

## Capture and network checkpoint (historical)


- The bounded capture arena retains at most 320 partial PCM samples and two
  pending packets. Fragmented input, backpressure, retry identity, explicit
  discontinuity, terminal flush and cancellation are implemented and tested.
- The pinned Espressif VOIP encoder's 104-sample lookahead at 16 kHz is verified
  against a queried reference encoder and independently decoded target packets.
  OpusHead advertises 312 samples at 48 kHz. Encoder bitstreams need not match.
- The real exchange exposed normal catalog rendition removal interrupting
  queued response audio. CATALOG_UNAVAILABLE now prevents new receives while
  allowing an existing bound range and its PCM to drain. Format changes and
  source replacement still invalidate old media. A capture sink failure also
  now prevents a later finish call from reporting success.
- All 18 native QUIC/security/interoperability cases pass, including the new
  bidirectional codec exchange. macOS normal and Linux normal/ASan/UBSan core,
  endpoint, capture and player suites pass. Both Rust fixture sets regenerate
  byte for byte. The system libopus binary itself is not instrumented.
- On the Ultra, the same exchange runs on a dedicated 64 KiB PSRAM audio task,
  separate from the network owner. Both peers pass. Measured free stack was
  41,624 audio / 7,404 network / 2,876 DNS bytes. Internal minimum was 106,164
  bytes; PSRAM returned to 8,386,076 bytes after joined cleanup. Internal free
  heap retained an unattributed 300-byte difference. The exchange took 4,362 ms,
  which is not a handshake or conversational latency acceptance result.
- This remains a short synthetic-PCM exchange with no I2S, PMIC, display or UI.
  Real microphone/speaker integration, long responses, repeated sessions,
  acoustic verification and full-shell resource headroom remain unproven.

## Audio receive checkpoint (historical)

- Catalog selection now accepts the reference producer's minimal OpusHead,
  retains its 48 kHz pre-skip and rejects unsupported decoder configurations.
  Same-track decoder-format changes invalidate receive operations and held
  leases. CATALOG/MEDIA_READY copy the selected format to the consumer.
- FRAME includes the frame ordinal. AUDIO_END identifies timestamp-only Hang
  terminal markers; DISCONTINUITY identifies empty groups. These are distinct
  from dropped/late-packet GAP events, including a terminal marker followed by
  tail packets whose timestamps precede the marker's timestamp.
- The optional receive player copies at most ten Opus packets, reserves six
  boundary slots, prebuffers 60 ms, reorders groups/frames, trims pre-skip and
  terminal padding, preserves timestamp pauses and resets codec epochs. It
  limits consecutive PLC to three frames, then uses silence; owner stalls over
  200 ms skip stale audio. The audio owner still must use the service's commit
  fence and the eventual board driver's DMA cancellation contract.
- macOS normal and Linux normal/ASan/UBSan suites pass, including nine fixtures
  exported by the unmodified pinned Rust Producer/Consumer and regenerated
  byte-for-byte. Native QUIC's 17-case matrix passes after these core changes.
  macOS ASan was stopped after a sampled pre-main sanitizer/dyld startup deadlock;
  it is not reported as passing. Linux uses an uninstrumented system libopus;
  the portable player and core are instrumented.
- On the Ultra, 27 fixture runs produced exact PCM and lengths, and a PLC call
  passed. The first run exposed the Espressif wrapper's rejection of a null PLC
  input; a valid zero-length input fixes it. Worst decode was 2,600 us over 82
  calls. PSRAM before/after was 8,386,192 bytes; internal free heap changed from
  374,499 to 374,479 bytes (20-byte residual, not attributed). Main-task stack
  free watermark was 7,336 bytes. These standalone figures exclude Wi-Fi,
  QUIC/TLS, I2S and display load and do not establish full-shell headroom.
- Espressif encoder metadata reports no codec description (`spec_info_len=0`).
  Capture work must establish and verify lookahead, then implement empty-group
  resets and terminal-tail publication. No microphone-to-host or host-to-speaker
  audio is claimed yet. Production authentication/reconnect, host integration,
  full Ultra BSP/shell and all final product gates remain open.

## Original finding status

Updated against the later hardware and native evidence on 2026-08-31. These are
finding-level implementation states, not a claim that the final release image
has passed every acceptance gate. The older foundation narrative below retains
its historical limits; bootstrap, renewal and physical voice are no longer
missing implementations.

| Finding | Current implementation and evidence | Remaining gate |
| --- | --- | --- |
| R1 active-path link failure | Shared wolfSSL features and the required client-ticket API link with a populated ESP-IDF configuration. Ordinary and diagnostic full-shell images build, and verified TLS/QUIC plus authenticated bootstrap run on the Ultra. CI definitions cover empty/public-dummy configurations; definitions are not evidence of an executed CI job. | Final clean-checkout candidate/CI execution and measured connection latency |
| R2 peer bidi replies | Peer-open callback allocates send halves; local opens reserve advertised peer bidi capacity. Actual-adapter tests reply on the same peer stream. Real TRACK/SUBSCRIBE/catalog exchanges pass natively and on Ultra. | Retain regression coverage in the final audio/product image |
| R3 stream-credit exhaustion | Bounded peer registry returns credit once for observed remote opens. Adapter tests cover 200,000 lifetimes; earlier real raw-QUIC turnover passes. Full-shell p119 completes 90,000 operational groups each way over 30 minutes, with exact receipts, 82 renewals and no final receive leases. | Retain final-candidate coverage; longer reconnect/endurance auditing is optional |
| R4 blocked-stream starvation | Priority/fairness/deadline scheduling retains ACK-owned buffers. The subsequent credit-dormancy fix eliminates 401 redundant retries during a real blocked-stream hold; resume/reset, fresh streams and control progress pass. A delayed Hang group also permits later audio on the physical shell. | Deliberately byte-credit-blocked physical media, remaining impairment cells and calibrated audio latency |
| R5 receive datagram mismatch | Advertises 1,350-byte receive maximum; reads one extra byte and drops oversized input, including truncated prefixes. Loopback UDP and bounded receive-drain tests pass. | Real peer packet/probe cases on lwIP |
| R6 callback reentrancy | Deferred events and task ownership isolate QUIC, media and UI. DNS/network workers join before destruction; scoped audio cancellation and DMA-drain completion have native and physical coverage. Historical portable Linux sanitizers pass, and current real-backend UBSan passes; full-backend ASan remains unpassed. | Extended lifecycle/cycle/idle races, full physical ownership transitions and the outstanding ASan lane |
| R7 missing operational client | Operational pub/sub, catalog/Hang, scoped leases, capture/Opus/playout, Ultra BSP and the full shell are implemented. USB enrollment, signed time, verified HTTPS/WSS/QUIC, directional authorization and same-session renewal run through the Rust/Python host. Real STT/model/tool/TTS turns and complete speaker receipts pass; p116 adds three zero-word-error turns. | Remaining speech failures, unchanged-reference interoperability, complete physical navigation/PTT/package/sleep-wake behavior, resource/latency/endurance gates and final rollout |
| R8 duplicate-start corruption | Validate session transition before clearing client state. Tests preserve partial SETUP, blocked FIN, and partial peer input across invalid starts. Original source fails the new regression. | Closed at portable-core regression level |
| R9 optional-bound overflow | Reject present values >= varint maximum before adding one. Tests cover legal/illegal start and end values, unchanged output on rejection, and irrelevant absent values. Original source fails the new regression. | Closed at portable-core regression level |

Earlier foundation changes retained for provenance:

- The active handshake example has a 2 MiB factory application partition. A
  fresh build exposed that the full image exceeds the old default 1 MiB
  partition. This is an example-only partition map, not an Ultra shell map.
- Both host suites use separate sanitizer binaries. `make test sanitize`
  cannot accidentally reuse the normal binary as its sanitizer result.
- README/backend documentation now distinguishes implemented primitives from
  unverified or missing capabilities. The root/core license remains unselected;
  the existing backend GPLv3 decision is recorded without assigning a license
  to unrelated source files.

## Historical foundation verification

- Portable core: 300,621 assertions passed on macOS and Linux. Approximately
  300,000 assertions are varint roundtrips, not distinct end-to-end scenarios.
- Original `client.c` and `lite05_control.c` each fail their new regression when
  substituted independently; corrected sources pass.
- Actual-adapter source: deterministic tests pass on macOS and Linux, including
  20 repeated macOS runs. ngtcp2 behavior is mocked; real loopback UDP exercises
  oversized datagram handling. This is not 200,000 real QUIC streams.
- Both suites passed Linux GCC AddressSanitizer/UndefinedBehaviorSanitizer,
  with separately compiled instrumented executables and normal leak detection.
- Fresh-config ESP32-S3 active handshake build passed, with real ngtcp2 and
  wolfSSL and public dummy credentials. The verifier confirms actual TLS and
  handshake symbols in the linked ELF. No certificate check was disabled.
- `git diff --check` passed. GitHub workflow execution is not claimed.

Evidence: [snapshot and artifact hashes](implementation-evidence/2026-08-30-foundation/snapshot.json),
[Linux test/sanitizer output](implementation-evidence/2026-08-30-foundation/linux-regressions.log),
[build result](implementation-evidence/2026-08-30-foundation/build-result.txt).

## Added oracle and native interoperability evidence

The new [oracle/native evidence](implementation-evidence/2026-08-30-oracle-native/README.md)
records source, dependency, executable, and firmware hashes. These results extend
the earlier foundation evidence; they do not close the operational-client or
product gates.

- The executable Rust oracle uses `moq-dev/moq` at
  `eb5776e21eeaecba8e844be53c821895c178bcaf`, with visibility-only instrumentation
  for its private codec modules. Its 74 cases include 62 Rust-to-C decodes,
  56 independently C-encoded cases decoded by Rust, nine invalid C cases,
  585 truncated inputs, and 68 incremental fragmentation cases.
- Added bounded SUBSCRIBE_UPDATE and GOAWAY codecs. Incoming GOAWAY URIs are
  untrusted data, not authority to redirect or forward credentials.
- The oracle executes upstream Hang catalog and `moq-flate` compressed-frame
  code, and cross-checks raw DEFLATE/dictionary framing against Python zlib.
  Embedded catalog parsing/decompression was missing at that checkpoint; the
  catalog checkpoint below adds and tests it.
- A reference-seeded C mutation harness passed 31,806 deterministic inputs under
  Linux ASan/UBSan. This is a bounded regression run, not exhaustive fuzzing.
- Real native tests compile the actual adapter/core, pinned ngtcp2, and wolfSSL.
  Each positive connection carries 10,000 uniquely numbered uni streams in
  **each direction** and 65 peer bidi replies. DNS, IPv4, and IPv6 cases pass.
  macOS ran the 12-case matrix three times; Linux ran it once with the whole C
  dependency chain instrumented by ASan/UBSan. Counts must not be presented as
  a single 90,000-stream connection or a 30-minute audio soak.
- Unknown CA, wrong DNS/IPv4/IPv6 identities, expired certificates, ALPN mismatch,
  and numeric-looking DNS SANs all reject before application connection/data.
  The matrix asserts the rejection diagnostic so timeouts cannot pass.
- The actual C client completes SETUP/path exchange against an **unmodified**
  pinned `moq-net::Server`. Its public `jwt` path fixture is not an authentication
  test. There are no publish/subscribe origins in this case.

The live tests identified and corrected three additional interoperability or
verification problems that deterministic mocks had not covered:

1. wolfSSL's default eight-byte ticket nonce rejects Rustls's normal
   post-handshake tickets. Both profiles now use fixed 255-byte nonce storage,
   without enabling session caches, persisted tickets, resumption, or early data.
2. The pinned native MoQ wrapper assumes QUIC DATAGRAM capability. The adapter
   now advertises it and supports bounded deferred receive callbacks. The
   stream-only profile explicitly discards/counts datagrams. A separate byte
   arena prevents them from consuming stream receive storage; they never return
   stream flow-control credit. No datagram-audio publisher was added.
3. wolfSSL 5.8.2's textual IP-name matching mishandles compressed IPv6 and can
   accept numeric-looking DNS SANs. Numeric endpoints now require exact binary
   iPAddress SAN matching before the connected callback, with canonical text for
   the underlying name check. SNI is used only for DNS hosts. Peer ALPN/TLS close
   alerts are also preserved instead of being replaced by a generic local error.

The updated populated ESP32-S3 handshake firmware links and passes the ELF
symbol verifier with these changes. The native CMake TLS configuration differs
from ESP-IDF; host results do not establish target memory use or target TLS
behavior. New CI jobs reproduce the oracle, mutation tests, real sanitized
transport matrix, and separate Ultra probe build; actual GitHub execution is
not claimed.

## Ultra feasibility preparation

Read-only ROM/stub interrogation identifies the connected device as ESP32-S3
revision 0.2, with 16 MiB external flash. The manufacturer's Ultra source is
pinned at `38e6f8dee3ba78b340512af9a013365ef248a7d0` in
`libs/moq-esp32/boards/twatch_ultra/hardware.lock.json`. It specifies external
quad PSRAM; the older S3 examples remain octal and must not be used as an Ultra
board profile.

`examples/twatch_ultra_probe` is a separate quad-PSRAM image. It validates memory
sizes, exercises two patterns over a 4 MiB PSRAM allocation, and measures
internal/PSRAM heap around a verifying TLS context allocation. It deliberately
does not initialize NVS, Wi-Fi, audio, display, touch, GPIO expansion, or power
rails. This is preparation for bring-up, not an Ultra BSP or shell.

The full 16 MiB backup was saved privately and verified against the device's
flash digest. The existing image uses app0 at 0x10000, app1 at 0x410000, NVS,
OTA data, FFAT, and coredump partitions; secure boot and flash encryption are
disabled. Only 0x60000 bytes of the existing app0 region were temporarily
replaced for the probe. The partition table and user-data regions were not
changed. The saved app bytes were restored, and a full 16 MiB digest check
confirmed that all original flash contents match again. The device is left in
ROM download mode, matching its initial state.

The [physical result](implementation-evidence/2026-08-30-oracle-native/ultra-hardware-result.json)
confirms 8 MiB quad PSRAM, a two-pattern 4 MiB memory test in 927,903 microseconds,
and a verifying wolfSSL/ngtcp2 TLS-context allocation. Free internal heap was
376,547 bytes before allocation, 373,575 during, and 376,547 after freeing; the
minimum observed was 341,544 bytes. These figures are for the tiny probe, not
Wi-Fi, a QUIC connection, audio, or the full shell. The core struct is 8,704
bytes on this target. No target network/TLS handshake or audio is claimed.

USB hard reset initially left the chip in download mode; the successful run
used the supported watchdog reset after clearing the force-download latch,
with ESP-IDF monitor's safe RTS/DTR ordering when USB re-enumerated. Earlier
unobserved attempts were restored and verified as well. The private backup
contains device/user data and must not enter Git or published evidence.

## Physical network bring-up and operational-runtime foundation

Evidence is in [the Ultra network checkpoint](implementation-evidence/2026-08-30-ultra-network/README.md).
This work made no changes to the VoiceWatch application or the Ultra audio/UI BSP.

- Added a separate quad-PSRAM network workload with Wi-Fi/PHY persistence
  disabled. Its Wi-Fi credentials, ephemeral CA, USB-provisioned test timestamp,
  firmware and raw logs stay in private storage. The public SETUP path fixture
  is still not production authentication or a production trusted-time policy.
- The first physical QUIC attempt failed authentication of the Initial packet.
  Independent OpenSSL-generated known-answer tests isolated bad nonempty
  AES-GCM/ECB results in the pinned wolfSSL accelerated ESP32-S3 profile; HKDF
  and empty GCM passed. Selecting supported software AES consistently in the
  shared feature list fixes the target vectors, including repeated/in-place
  encryption/decryption and altered-tag rejection. Verification remains enabled.
- Two short physical runs then passed verified TLS/ALPN, unmodified reference
  SETUP, and 500 uniquely numbered synthetic streams per direction. The extended
  run additionally passed one reset per direction, three ephemeral bidi replies
  and two ping replies over a persistent bidi control stream. Each attempt
  restored the saved app bytes and verified all 16 MiB against the baseline.
- Short-run handshakes took approximately 3.4–3.8 seconds, so the proposed
  connection-latency gate is **not met**. The extended short run observed a
  147,928-byte minimum internal heap and 3,788-byte main-task stack watermark.
  This is Wi-Fi/transport only, without audio, LVGL, WAMR or product control.
- The 90,000-group-per-direction, 30-minute workload passed on both endpoints,
  with 180 resets per direction, 182 bidi replies and 181 persistent-control
  round trips. Minimum internal heap was 149,024 bytes; teardown free internal
  heap was 267,123 bytes and free PSRAM 8,386,076 bytes. Sanitized final results
  and firmware/ELF hashes are in `long-soak.*` under the network evidence. The
  historical runner restored once; no future restoration gate remains.
- Added an optional extended backend close callback that preserves receive and
  transmit error flags/codes. The old single code could not distinguish MoQ
  CANCEL (zero) from clean closure. Tests cover zero-code resets and both halves;
  legacy callbacks remain available. This change postdates the completed soak's hardware
  binary, so that binary's memory figures do not cover the additional metadata.
- Added the bounded internal dispatcher and owned outbound queue. Host tests
  cover pinned-reference control messages, fragmented/interleaved streams,
  interrupted/duplicate SETUP, 10,000 group retirements, partial writes/FIN,
  control reservation, deadlines, reset backpressure and cancellation/ACK races.
  Linux ASan/UBSan passes. These components now feed the operational engine described below. The
  cross-task application service remains pending.
- Fresh public-dummy Ultra transport builds link the active TLS path. A CI job
  reproduces that configuration without private credentials. The native 12-case
  QUIC/security/SETUP matrix also passes after the close-callback change. Actual
  GitHub CI execution is not claimed.

## Operational engine checkpoint

Verified logs, source/object hashes and limitations are in the
[endpoint-engine evidence](implementation-evidence/2026-08-30-endpoint-engine/README.md).

`components/esp_moq/include/esp_moq/engine.h` and `src/engine.c` implement the
single-owner endpoint. Public behavior and limits are documented in the library's
`docs/endpoint-engine.md`. One broadcast/four tracks, discovery, track queries,
subscriptions, START/END/DROP, serving caps, retained publication groups and owned
receive assembly are operational. The core host arena is 23,408 bytes, with
203,328 bytes of media/cache storage and 67,808 bytes of TX storage accounted
separately. These figures exclude QUIC/TLS, application service, audio and UI.

The native C endpoint and public unmodified pinned `moq-net` APIs exchange eight
timestamped synthetic frames each way, an empty group and clean END. A separate
acknowledgement is published only after Rust validates the C publication. The
13-case native matrix and nine additional engine runs pass for DNS, IPv4 and IPv6.
Linux host sanitizers and Clang static analysis also pass. Host regression covers
partial writes, media before START, whole trailing groups after control closure,
zero-code cancellation, invalid roles and deadlines. A 10,000-position run with
1,000 separate DROP ranges does not accumulate history; 1,000 peer-subscription
lifetimes retain ID-reuse protection. None of this is Opus/audio interoperability.

Remaining engine hardening includes priority propagation, impairment/reconnect
stress, broader range fuzzing and explicit application media-epoch binding. The
internal callback still executes on the owner task and is borrowed until return;
it must not be passed directly to a UI or audio task. At this earlier checkpoint,
catalogs and the owned cross-task service were next, followed by real audio,
production auth/host service and full Ultra integration. The engine compiles for
ESP-IDF but has not run on the Ultra; the transport example does not invoke it.

## Catalog and Hang framing checkpoint

Added bounded audio catalog parsing/serialization, rendition selection, RFC 7396
merge patches and group-scoped raw DEFLATE. Both `catalog.json` and
`catalog.json.z` interoperate with the pinned reference. The vendored zlib 1.3.2
core is unmodified, licensed and checked by a 20-file hash manifest; its wrapper
uses fixed caller-owned arenas, without hot-path heap allocation. Public limits,
unsupported rendition policy and ownership are in the library's
`docs/hang-catalog.md`.

The oracle now runs 76 cases, including 66 catalog-sequence frames through the
actual pinned `moq-json` snapshot encoder/decoder. Rust decodes C-serialized and
C-compressed catalogs, and C reconstructs full JSON through peer patches while
preserving unknown fields. The native endpoint exchanges both catalog tracks,
three incoming snapshot/delta frames per track, and eight Hang timestamped
synthetic frames per direction. Matching container and outer timestamps are
checked; encoded payloads still are not real Opus audio.

Host regressions cover full-size frames, 5,120 incompressible DEFLATE frames with
window wrap/reset, decompression overflow, duplicate/escaped JSON keys,
Unicode, depth/field/node/merged-size limits, patch rollback, unsupported
renditions and per-byte snapshot mutations. The engine frame/cache limits were
separated to accept a bounded 4 KiB catalog plus compression/frame overhead:
4,112-byte frames, 4,140-byte encoded cached groups. Current host media/cache
storage is 204,608 bytes; the older engine evidence records its earlier size.
Catalog decoder storage is 95,256 bytes on macOS arm64 and 95,264 on Linux arm64,
including the inflater. These remain host measurements, not full-shell memory
budgets. Catalogs/engine compile for ESP-IDF but the transport-only watch example
does not invoke them.

One early native run terminated while receiving catalogs, with the Rust peer
reporting `dropped`; repeated subsequent tests did not reproduce it. This is
not classified as fixed and remains part of the lifecycle/reset hardening gate.
The earlier missing-track failure was a test-peer lifetime bug: its broadcast
holds weak track references, so completed catalog producers must stay alive.
The peer also maps application stream errors through the WebTransport range
for raw QUIC. The next service work must translate that pinned boundary while
preserving original diagnostics, including zero-code cancellation.

Evidence and exact verification counts are recorded in
[the catalog evidence](implementation-evidence/2026-08-30-hang-catalog/README.md).
No firmware was flashed or restored for this catalog checkpoint.

## Owned service and operational Ultra endpoint checkpoint

This subsequent checkpoint implements `esp_moq/service.h`, `esp_moq/native.h`
and the optional `esp_moq_endpoint` component. The portable service copies Opus
packet submissions, negotiates both catalogs, leases received packets and binds
capture/request/owner identity to explicit connection/group ranges. Cancel and
nonblocking PCM commit are linearized; held bytes remain stable until release.
Dedicated terminal slots survive queue pressure. Source restart cancels the old
catalog/media operation and resets decompression without accepting its late data.
An interval tracker and rolling progress deadline support a simulated five-minute
response; this is not physical five-minute Opus playback evidence.

The task endpoint copies configuration and authorization, separates DNS from the
single QUIC owner and rejects stale resolver completions. Explicit close rejects
old attempt IDs, invalidates media immediately and schedules bounded backend
cleanup. Destroy waits for live workers and consumer leases, then joins before
freeing. Platform DNS can outlast its logical deadline; the one bounded resolver
slot stays owned until the platform returns. There are no detached retry jobs.
Authorization/trusted-time leases and UTC rollback checks are implemented, but
the authenticated bootstrap that issues those leases is still missing.

Subscriber priorities now propagate from SUBSCRIBE/SUBSCRIBE_UPDATE into the
owned TX queue and adapter, including already copied unsent data. Control takes
precedence; equal priorities rotate and blocked streams are skipped. Abandoning
a publication removes its cache range and resets in-flight jobs without closing
the persistent track or affecting later groups. Native application errors are
mapped using the pinned WebTransport boundary; actual peer cancellation and
plain-catalog fallback pass.

Validation of the current implementation:

- macOS portable/driver tests pass; Linux GCC normal and ASan/UBSan suites pass
  for the portable service, actual threaded endpoint and actual adapter source.
  The driver fault test controls DNS/UTC and stubs the adapter; it is not TLS
  evidence. It covers DNS timeout/stale completion, copied configuration,
  close/reconnect, authorization expiry and UTC rollback. Expiry retains an
  error terminal outcome rather than masquerading as user cancellation.
- The final native 17-case matrix passed three times (51 cases), using the real
  ngtcp2/wolfSSL backend and pinned unmodified Rust endpoint. New service and
  task-endpoint modes use independent producer/consumer threads and validate
  both catalog formats, eight uploaded frames, receive range [5,13), both
  operation ends and zero outstanding leases. The public token is only a path
  fixture. Existing certificate/ALPN rejection cases also passed.
- The active public ESP-IDF build links the task-endpoint path. Enabling actual
  linkage exposed unavailable `nanosleep`; the target path now uses FreeRTOS
  delay. It is no longer sufficient to compile then discard the unused wrapper.
- Clang static analysis of the endpoint/service/TX source reports no diagnostics.
  Eight offline app-only runner checks pass, including abort/assert detection
  and no restoration after failures.

### Physical Ultra result

The connected ESP32-S3 Ultra ran the full service/task stack against the real
Rust peer. Both endpoints passed: catalogs negotiated, eight synthetic Hang
frames per direction were verified, and the receiver accepted [5,13). The peer
released the final response frame only after validating both C catalogs and all
uploaded frames. The complete short exchange took **3,884 ms**; this is not a
500 ms handshake or voice-latency success claim.

| Measurement | Bytes |
| --- | ---: |
| Internal RAM before endpoint creation | 266,739 |
| Minimum free internal RAM | 107,440 |
| Internal RAM at successful exchange | 172,383 |
| Internal RAM after joined destruction | 266,511 |
| PSRAM before creation / after destruction | 8,386,076 / 8,386,076 |
| PSRAM at successful exchange | 7,776,796 |
| Network / DNS stack watermarks | 7,392 / 2,868 |

RX/TX queue high-water marks were 7/1. Destruction returned success. The remaining
228-byte internal difference is not attributed yet; repeated create/destroy
measurement is still required before calling the endpoint leak-free on target.
No microphone, decoder, speaker or UI was active, so these values do not prove
full-shell headroom.

Two failed physical attempts preceded this pass and are retained privately:
service initialization rejected allocator alignment, then ESP-IDF's missing
pthread configuration cleared the saved output and caused an invalid restore.
The endpoint now explicitly requests `max_align_t` arena alignment and restores
the default pthread configuration when the getter reports NOT_FOUND. The final
image passed after those fixes; failed images were not restored to factory.

The app-only runner validated live identity/security/partition/OTA metadata and
wrote app0 only. **The passing new endpoint firmware remains installed.** No
factory backup, baseline comparison or restoration was performed. Private Wi-Fi
headers, images and full serial logs remain outside the repository. Sanitized
evidence and source hashes are under
[owned endpoint evidence](implementation-evidence/2026-08-30-owned-endpoint/README.md).

### Next requirements, still open

Codec/capture/playout, bounded jitter/PLC, decoder/DMA cancellation and a bounded
real reference audio echo now pass. Still implement authenticated
bootstrap, scope/time policy, credential refresh/reconnect and the production
Rust/Python host boundary. Then integrate the Ultra BSP and complete VoiceWatch
shell, preserving control/actions/packages and verifying real voice turns. The
90,000 operational groups per direction, 1,000 PTT turns, eight-hour idle,
impairment/reconnect and full-shell gates remain open. The earlier 90,000-stream
raw transport soak is not a substitute for those gates.

## Next work, without reducing the original objective

1. Integrate the verified Ultra audio BSP and capture/player owner with explicit
   PTT and the complete VoiceWatch shell. Bring up display/touch/buttons/haptics.
2. Extend the real-Opus exchange to long responses, repeated captures, source
   changes, network impairment and cancellation. The short codec exchange and
   earlier raw transport soak do not satisfy these operational audio gates.
3. Complete trusted time, authenticated bootstrap, scoped credentials, renewal,
   reconnection and the production host endpoint. The test token remains public
   test data, not production authorization. Target handshake latency remains open.
4. Bring up the Ultra audio/display/touch/power BSP and integrate the complete
   VoiceWatch shell, preserving PTT ownership, long responses, trusted actions
   and package delivery. Future device tests leave our firmware installed.
5. Run the full hardening/rollout matrix on the final release image and measure
   full-shell resource/latency headroom before making MoQ the default.

Useful local commands:

```sh
make -C libs/moq-esp32/tests/host test
make -C libs/moq-esp32/tests/adapter test
python3 libs/moq-esp32/tools/moq_reference_oracle/check.py
python3 libs/moq-esp32/tests/interop/build.py
python3 libs/moq-esp32/tests/interop/run.py --client libs/moq-esp32/tests/interop/build/client
# Use Linux for sanitizer validation; native --sanitize instruments dependencies too.
```

Active cross-build artifacts are under `/tmp/voicewatch-moq-active-build` and
`/tmp/moq-ultra-probe-build`; native artifacts are under `/tmp/moq-native-repro`.
Full hardware, operational relay, audio, and shell readiness remain unproven.
