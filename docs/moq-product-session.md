# MoQ product session checkpoint — 2026-08-30

Latest update (2026-08-31): the reviewed terminal-reader patch is adopted and
deployed in the Rust host. A ten-minute normal-network physical provider session
passes three turns, cancellation, renewal and reconnect. Normal flash49 is
unchanged; permanent enrollment is revision 187. See the
[adoption evidence](implementation-evidence/2026-08-31-terminal-adoption/README.md)
for exact coverage and remaining physical/resource limitations.

Current acceptance direction (2026-08-31): work has resumed on physical
controls/apps/sleep-wake, interoperability and release checks. Ten minutes is
sufficient; longer endurance and induced impaired-network testing are deferred
and do not gate initial replacement. Historical pause/failure notes below are
retained without relabelling incomplete or failed tests as passes.

Latest installed state: normal full-shell **flash49**, personal app installation
enabled, permanent enrollment **185**. Signed private-CA HTTPS installation of
the test Timer and duplicate-offer handling pass on the watch. Physical app
interaction, button/touch behavior and sleep/wake still need observation.
The current reference candidate passes but is not adopted; unchanged-reference,
resource and release limitations are documented in the
[initial acceptance checkpoint](implementation-evidence/2026-08-31-initial-acceptance/README.md).

The live-agent service now has an explicit MoQ adapter connected to the existing
conversation callbacks. This is a development mode, not the accepted replacement
release. Host-initiated physical provider turns now pass through microphone,
STT, model/tools, TTS and speaker receipt, including two turns in one session
and a fresh session reconnect. Another repeated-turn run still fails at the
capture reorder deadline. A separate supervised local MoQ deployment now has
startup/restart coverage; full physical interaction remains open. See the
[current progress](moq-implementation-progress.md).
The installed
Ultra now runs the authenticated full-shell firmware; a private local host bench
has verified startup, audio, response replacement and reconnection.

The subsequent firmware checkpoint implements USB enrollment, authenticated time,
HTTPS bootstrap and session-bound WSS control. It has been flashed and tested on
the Ultra; see [hardware evidence](implementation-evidence/2026-08-30-ultra-authenticated-session/README.md)
and [enrollment/run instructions](moq-ultra-enrollment.md) for the exact scope.

The 2026-08-31 TLS checkpoint adds a 256 KiB aggregate wolfSSL request cap,
including null-hint crypto allocations, alongside the separate ngtcp2 cap.
Flash45 passes text/background output and clean/impaired physical provider
turns with valid TLS snapshots and no allocation failure. This does not cover
mbedTLS HTTPS/WSS, system/stack/UI memory or the complete product acceptance
gate. See [allocation evidence](implementation-evidence/2026-08-31-tls-memory/README.md).

## Service selection

The subsequent [stream-credit checkpoint](implementation-evidence/2026-08-31-blocked-stream/README.md)
fixes unnecessary retries while a QUIC stream lacks byte credit. Flash46 passes
five real-provider responses and reconnect, with exact completion receipts and
final serial sample markers. One detailed serial statistics block is incomplete;
complete quality counters and deliberately blocked on-device media are not
claimed. Permanent enrollment is revision 169; host processes are unchanged.

The later [delayed-group test](implementation-evidence/2026-08-31-group-delay/README.md)
holds one Hang group for about 258 ms while fresh groups continue. The same
flash46 image completes three real-provider turns with exact sample receipts,
one concealed/late chunk and no queue-pressure drop or fallback silence.
Permanent enrollment is now revision 171; firmware and persistent hosts are
unchanged. This verifies one application-group delay case, not the full release
matrix or physical QUIC byte-credit blocking.

The [operational duplex soak](implementation-evidence/2026-08-31-stream-soak/README.md)
adds a USB-started test workload to the full flash47 shell. Short 500/3,000-group
exchanges pass with renewal, exact totals and permanent-host recovery. p119 then
passes 90,000 groups each way over 30 minutes with 82 renewals and fresh-grant
reconnect. The test opens neither microphone nor speaker and does not establish
full product readiness. Permanent enrollment is verified at revision 177;
persistent hosts remain unchanged.

The [idle/reconnect checkpoint](implementation-evidence/2026-08-31-idle-soak/README.md)
adds read-only owner status on flash48. Corrected smoke run p121 passes two
minutes, renewal, planned/final reconnect and permanent-host recovery at
revision 181. The independent audit retains an unexplained 16-byte internal-heap
difference; cumulative recovery is not yet verified. The eight-hour p122 run
was stopped at the user's request. Ten minutes is sufficient for current
acceptance; longer endurance and 1,000-cycle testing are optional follow-up,
not replacement gates. Further work is paused. No microphone or speaker was
opened, no firmware was restored, and persistent hosts are unchanged. See the
[optional longer-test outline](moq-optional-endurance-tests.md).

WebRTC remains the default and its existing service is preserved. MoQ selection is explicit:

```sh
cd doodad-runtime/services/live-agent
uv sync --locked --no-dev
uv run --locked --no-dev doodad-live-agent serve --transport moq \
  --moq-config /private/voicewatch/host.json --port 8766
```

The existing provider configuration is still required to run conversations.
Missing or contradictory transport/config options are rejected. There is no
anonymous, plaintext or WebRTC fallback. Imports of the MoQ service, CLI and
conversation code pass in the separate installation without aiortc or PyAV;
this is not a live provider test.

The owner-private JSON file requires these fields (illustrative paths/host):

```json
{
  "certificate": "/private/voicewatch/server-chain.pem",
  "private_key": "/private/voicewatch/server-key.pem",
  "device_keys": "/private/voicewatch/devices.json",
  "ipc_socket": "/private/voicewatch/media.sock",
  "public_host": "voicewatch.example.test",
  "media_port": 4443
}
```

An optional `time_port` enables a separate HTTP listener exposing only
`POST /v1/moq/time`. Fresh device nonces and domain-separated HMAC proofs bind
the returned time to the enrolled key; this listener carries no session tokens,
bootstrap, control or artifact routes. Firmware verifies the proof and bounded
round-trip time before validating the HTTPS/WSS certificate chain.

Config/key files must be owner-only regular files, at most 16 KiB, without
symlink following. Enrollment retains its separate 64 KiB/256-device checks.
FIFOs fail without blocking; encrypted keys fail without prompting on stdin.
The certificate chain is a regular file of at most 64 KiB. The IPC parent must
already exist with owner-only permissions. Errors do not echo configuration values.

Start the native `server/voice_agent` process after Python creates the IPC socket,
using its separate private config. Both processes must agree on socket, endpoint,
certificate identity and trust. See the library README for native commands.
Alternatively use the [paired supervisor and explicit Mac installer](moq-supervised-host.md).
Automatic enrollment remains open; the default installer still selects WebRTC.
HTTPS artifact routes are mounted
on the same application, with announcements using the configured public host.
MoQ mDNS explicitly advertises TLS/control/bootstrap routes; discovery is not trust.

## WSS application contract

Every message has exactly `v`, `type`, `seq`, `session_id`, `device_id`, `payload`.
Version is integer 1; session/device match the redeemed grant. Sequences are
strictly consecutive positive integers below 2^53, separately per direction.
Duplicate/nonfinite JSON and unknown transport commands fail closed. Messages
are limited to 16 KiB without compression.

`hello.payload` supplies the same device, bounded board name, capabilities object
and `transport:"moq"`. `welcome` advertises native MoQ/Hang Opus. `welcome.ack`,
`peer.created` and `watch.state` may precede media readiness. Application readiness
requires runtime identification, watch `peer.ready` and native `media.ready`
after catalog/subscription validation. None starts recording.

### Live authorization renewal

`hello.capabilities.moq_renewal_v1:true` opts into same-session renewal. At half
the current lease the host sends `session.challenge` with exactly one `nonce`
(64 lowercase hexadecimal characters). One challenge is retained per live
grant for at most ten seconds. Challenge issuance does not change any deadline.
The watch returns `session.renew` with `nonce` and a lowercase SHA-256 HMAC proof
over the NUL-separated strings `voicewatch-moq-renew-v1`, device ID, session ID,
and nonce, using its enrolled key. A matching bad proof consumes that challenge.
An expired, revoked, replaced, unattached or differently owned grant cannot renew.

The host extends the same directional grant and sends `session.renew` on its
owner-private IPC, with integer `renewal_revision`, `lease_ms`, and
`expires_unix_ms`. The native reader checks consecutive revisions, pre-expiry
arrival, the 900-second maximum, strictly increasing deadlines and absolute UTC
expiry before updating its monotonic timer. It acknowledges `session.renewed`
with only `renewal_revision`; it never changes origins or media state.

Only then does the host send WSS `session.renewed` with exactly `nonce`, integer
`revision`, `lease_seconds`, `expires_unix`, and `time` (the existing signed time
proof, bound to this nonce). The watch requires the pending nonce, next revision,
a round trip of at most three seconds, matching enrollment revision, still-live
old deadlines, a fresh valid time MAC within two seconds of UTC, and increasing
bounded deadlines. It changes no UTC value. Its media endpoint updates atomically
before control accepts the new lease. Finally, the watch acknowledges
`session.renewed` with only integer `revision`. The host counts completion only
after that receipt. Missing native/watch acknowledgment fails within three seconds.

Neither SETUP nor one-use attachment tokens are reused. No capture, cancellation,
response generation or scope changes as a side effect. Renewal failure retires
the session; fresh bootstrap remains necessary for reconnect, changed identity,
scope, roots or enrollment. Legacy peers that omit the capability continue to
expire and reconnect. These application/IPC messages do not alter standard Hang
audio or the pinned MoQ reference protocol.

### Capture and PTT

The watch owns local guest/microphone authorization. Host `capture.start` includes
`duration_ms` (1–30,000) and a monotonic decimal-string `start_id`. The watch must
echo that ID in `capture.started`. Local guest capture uses `start_id:"0"`
(omission currently means zero). Stop/cancel before a start receipt applies only
to that start ID, never to a replacement guest's microphone.

`capture.started` supplies decimal-string `capture_id`, `request_id`, `owner_token`,
`first_group`, and the applicable `start_id`. Capture IDs increase within the
session; request and owner may be zero. Native `capture.begin` binds that identity
and first group. `capture.stopped` supplies the same identity, first group,
exclusive `end_group`, and exact `samples` count. Decimal strings preserve u64s.

Only current native `capture.pcm` enters the application queue. `capture.ended`
must match the authenticated range, expected count and received PCM. The product
`capture.stopped` event follows that PCM; the service commits STT at that validated
boundary, not on early PTT release or a local VAD pause. WebRTC keeps its existing
silence-padding/VAD behavior. MoQ defaults to no provider noise filter because
`near_field` suppresses the Ultra acoustic fixture; the explicit
`DOODAD_STT_NOISE_REDUCTION` option selects `off`, `near_field` or `far_field`.

The explicit-capture STT adapter binds the provider's acknowledged `item_id` to
that capture before admitting interim/final transcripts. One pending commit and
one current item keep correlation state bounded; the prior-item chain detects
inconsistent acknowledgements. Unacknowledged or retired items cannot acquire
the identity of a replacement capture. A five-second configuration/commit wait
or failed write retires the socket and affected capture. A new provider socket
does not replay the old capture. Normal turns keep the existing STT/model/TTS
connections and conversation history.

Internal capture identities accompany queued PCM, boundaries and transcripts.
Cancellation invalidates them before waiting for device stop, and routing
rechecks them after asynchronous callbacks. The VHQ STT resampler resets at each
capture start and flushes its exact remaining duration before a valid commit;
cancelled history cannot bleed into the next capture.

The MoQ provider pipeline now preserves this identity through user aggregation,
model requests and history frames, tool runners/results, and registered TTS
contexts. The writer rechecks action ownership immediately before sending a
queued command. An action already issued may have completed; cancellation does
not undo accepted durable work. New watch-state requests require fresh reads.
TTS partial-word alignment is stored per context and checked after audio waits;
retired output cannot recreate an audio context or commit unheard text to history.
The sink rechecks ownership across capture-stop and playback-drain waits. These
boundaries have synthetic tests and physical delayed-tool/TTS fault runs; they
do not replace the broader loss, long-response and full-shell acceptance gates.

Cancellation retires pending start IDs, capture ownership, queued old PCM and
response generation. A delayed start receipt is cancelled without opening a
decoder. Intent epochs prevent a suspended, cancelled listen callback from
rearming capture. Pending starts have a three-second deadline; active captures
retain native/Python deadlines and a 31-second sample cap.

Native `capture.failed` carries the capture/request/owner identity when live
loss exceeds the unchanged 200 ms concealment budget. Python retires that
capture, discards its queued PCM and response work, sends `capture.cancel` to
the watch, and reports application `capture.failed` so STT cannot commit partial
audio. Control and media sessions remain authorized for a fresh PTT attempt;
capture never restarts automatically. Malformed media, invalid boundaries,
authentication failure and IPC/transport faults still fail closed.

Watch `capture.failed` now includes capture/request/owner identity and `start_id`,
including failures before capture starts. Python matches either the active
capture or the pending start. A late cancellation receipt, stale native PCM/end,
or queued application failure cannot retire a newer capture. This receipt change
requires coordinated firmware and host updates; uncorrelated failure payloads
are rejected. Cancelled response tasks create their audio-pump coroutine only
after starting, so immediate cancellation does not leak an unawaited coroutine.
The conversation receives the authenticated capture identity too: a fresh start
clears an unfinished provider buffer even when the old failure callback has been
superseded. Duplicate starts for the same identity remain idempotent.

### Response binding and speaker completion

Text and idle background speech use output-only authorization. Host
`context.request` contains a nonzero monotonic decimal `context_request_id` and
`kind` (`text` or `background`). After its audio owner accepts the context, the
watch returns `context.ready` with that request/kind, a watch-issued `context_id`,
and neutral `request_id:"0"`/`owner_token:"0"`. An active microphone or unfinished
response yields `context.rejected` with `reason:"busy"`. Requests time out after
three seconds. `context.cancel` names only its request; a late receipt cannot
cancel a newer context or capture.

Context IDs share the monotonic watch identity namespace with capture IDs.
Native IPC `context.begin` maps the ID to its legacy `capture_id` correlation
field, but starts no capture reader, decoder, microphone, synthetic PCM or STT
commit. Text waits for authorization before entering the model pipeline. Idle
background output uses the same path and retries pending attention when busy.

The shared begin/enqueue/end API preserves the exact-tail PCM spool. MoQ uses
a fresh contiguous pacing clock for each response, anchored on the first PCM
read after media readiness. Small scheduler slips do not reanchor subsequent
deadlines: catch-up is limited to one packet per 10 ms. More than 200 ms of
pacing debt cancels that response through `playback.cancel` on both channels,
without revoking the authenticated session. Retired awaits retain their old
pacer object and cannot shift the next response's clock. WebRTC retains its
existing wall-time pacing mode. Native
response startup waits for capture validation or an acknowledged output-only
context. Native `playback.prepared`
supplies the response ID, first group and `pts_us`, the exact start timestamp of
the encoder's response epoch. WSS `playback.begin` carries
capture/request/owner/response IDs, that group and timestamp; no PCM reaches the encoder
before matching watch `playback.bound`.

Native preparation publishes only a standard empty codec-reset group. The pinned
Lite05 publisher needs this first group before it sends `SUBSCRIBE_START`.
The watch emits `playback.bound` after `MEDIA_READY` initializes the player, not
when a receive request is merely queued. Only then does paced PCM begin. This
prevents subscription negotiation from accumulating an audio burst at startup.
Cancelling an unbound preparation leaves only an empty reset group; group IDs
are never reused. The native endpoint and firmware must be updated together.

The player binds time zero to this authenticated timestamp, not to the first
packet that happens to arrive. Missing initial packets therefore retain their
duration. The authenticated `playback.end` sample count reaches the audio owner
and bounds missing-tail concealment to 200 ms. A Hang terminal marker must agree
with that count. Playback waits for both the media boundary and the control
boundary before draining; oversized loss fails. Host/native/firmware versions
must be updated together for this required timestamp field.

Watch receipts include those IDs, `first_group`, `samples`, boolean `cancelled`
and integer `error`. Successful binding has zero samples, false cancellation and
zero error. `playback.started` follows binding. Native `playback.encoded` supplies
the exact exclusive end group and sample count; the host checks both and sends
`playback.end`. Watch `playback.finished` also supplies that end/count and is
accepted only after the end command was sent. Firmware must emit it after
decoder/DMA completion, not merely after receiving media.

Spool drain and native encoding never satisfy the application playback wait.
Cancelled/replaced waits return an unsuccessful result rather than successful
playout. Standalone TTS history-flush frames wait behind their words and the
speaker receipt, preventing premature empty-history commits. MoQ background
announcements remain durably pending, and questions remain unfocused, until the
owning response plays. Cancellation or process loss before acknowledgement
permits retry; this is at-least-once delivery, not proof against repetition if
the process dies after audible playback but before the database acknowledgement.
Binding, encoded tail and watch receipt have bounded waits. Cancellation drops
pending output without flushing, sends `playback.cancel` to both channels and
releases the old wait. Native playback cancellation preserves the completed
capture for a later, higher response ID. Stale receipts cannot activate/finish a
replacement. Shared binding cleanup also cannot detach a newer utterance when
an old playback wait returns.

A failed transport drain also ends the sink's speaking lifecycle without
committing unheard text or taking the successful natural-pause path. The owned
MoQ failure callback fences the failed provider turn, queues interruption and
returns Ready; generation and turn checks prevent stale recovery from resetting
a replacement. Durable announcements remain pending. A controlled physical
pacing stall plus three fresh turns verifies this recovery, while the combined
packet-fault speech case remains failed; see
[packet/recovery evidence](implementation-evidence/2026-08-31-packet-order/README.md).

## Scheduling, ownership and limits

One 32-item writer owns IPC/WSS output. One response pump holds at most one PCM
packet awaiting a write receipt; stale queued response items are skipped. A
separate 64-item application queue cannot block IPC framing or liveness. Overflow
retires the session instead of silently dropping audio. Native headers remain
4,096 bytes, PCM 640 bytes, writes two seconds, pending actions at most 32.
Application/startup callbacks have deadlines. The spool/native response limit
is 600 seconds; every authorization lease must stay live through authenticated
renewal, or playback is retired at expiry without a grace period.

Closing/replacing sessions revokes both channels, cancels tasks/action futures
and preserves the replacement's mapping. These bounds do not fix upstream
native cache/model allocation, provider buffering or process-wide resource limits.
An origin pool remains an eviction budget, not a hard allocation cap.

## Evidence and remaining work

See [product session evidence](implementation-evidence/2026-08-30-moq-product-session/README.md)
for final test counts and source hashes. The native lane uses real TLS/WSS, Unix
IPC and QUIC. Two product-adapter cases verify exact bidirectional 537-sample
reference PCM and cancellation before binding with delayed stale receipts. Three
earlier cases cover native exchange, invalid/replayed tokens and WSS revocation.
Audio and watch receipts are synthetic, not proof of physical speaker completion.

Required next work:

- Expanded physical lifecycle/security coverage of the implemented bootstrap,
  enrollment, control and capture/playback bindings. Basic full-shell startup,
  exact-tail speaker completion, response replacement, certificate rejection and
  fresh-grant reconnection now pass the private hardware bench.
- Provider-turn ownership across reconnection, rapid PTT, owner/app changes,
  background speech and long responses. Native tests do not prove STT/model/tool/
  TTS generation isolation or response history correctness.
- Text-only `conversation.text` and speech before any captured turn need an
  explicit watch-owned response context. The current MoQ adapter rejects these
  unsupported commands rather than inventing a microphone capture.
- Credential refresh/backoff, key/root rotation, supervisor packaging, native
  memory/loss hardening, actual Ultra turns and the full-shell release matrix.
  A 600-second spool does not prove continuous speech through lease expiry.

The full goal remains open. Future tests do not require firmware restoration;
the existing app-only flashing policy remains unchanged.
