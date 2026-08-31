# MoQ product session checkpoint — 2026-08-30

The live-agent service now has an explicit MoQ adapter connected to the existing
conversation callbacks. This is a development mode, not the accepted replacement
release. Host-initiated physical provider turns now pass through microphone,
STT, model/tools, TTS and speaker receipt, including two turns in one session
and a fresh session reconnect. Another repeated-turn run still fails at the
capture reorder deadline; deployed service restart and full physical interaction
remain open. See the [current progress](moq-implementation-progress.md).
The installed
Ultra now runs the authenticated full-shell firmware; a private local host bench
has verified startup, audio, response replacement and reconnection.

The subsequent firmware checkpoint implements USB enrollment, authenticated time,
HTTPS bootstrap and session-bound WSS control. It has been flashed and tested on
the Ultra; see [hardware evidence](implementation-evidence/2026-08-30-ultra-authenticated-session/README.md)
and [enrollment/run instructions](moq-ultra-enrollment.md) for the exact scope.

## Service selection

WebRTC remains the default and the existing deployed mode. MoQ selection is explicit:

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
Supervisor installation/restart policy and automatic enrollment remain open;
the current installer still deploys WebRTC. HTTPS artifact routes are mounted
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

The shared begin/enqueue/end API preserves the exact-tail PCM spool. Native
response startup waits for capture validation or an acknowledged output-only
context. Native `playback.prepared`
supplies the response ID, first group and `pts_us`, the exact start timestamp of
the encoder's response epoch. WSS `playback.begin` carries
capture/request/owner/response IDs, that group and timestamp; no PCM reaches the encoder
before matching watch `playback.bound`.

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

## Scheduling, ownership and limits

One 32-item writer owns IPC/WSS output. One response pump holds at most one PCM
packet awaiting a write receipt; stale queued response items are skipped. A
separate 64-item application queue cannot block IPC framing or liveness. Overflow
retires the session instead of silently dropping audio. Native headers remain
4,096 bytes, PCM 640 bytes, writes two seconds, pending actions at most 32.
Application/startup callbacks have deadlines. The spool/native response limit
is 600 seconds; a shorter authorization lease still takes precedence.

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
