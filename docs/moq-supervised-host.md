# Supervised local MoQ host

The explicit MoQ deployment uses one launchd job to own the Python live-agent
and a pinned native endpoint binary. It runs separately from the legacy WebRTC
job; it does not change the default firmware or service transport. This is local
development deployment, not completion of the WebRTC replacement acceptance gates.

## Build and configuration

Build the native endpoint from `libs/moq-esp32/server/voice_agent` with the pinned
Rust toolchain and bundled C Opus configuration:

```sh
cargo test --locked
cargo clippy --locked --all-targets -- -D warnings
cargo build --locked --release --bin voicewatch-moq-endpoint
```

Provision a private host JSON file using the
[MoQ host configuration](moq-product-session.md#service-selection). For a watch
without trusted RTC time, set its dedicated `time_port`. Use a maintained local
CA or an appropriate deployment certificate whose SAN matches the advertised
host. Do not deploy the six-hour hardware-bench certificates. Keep the CA signing
key separate from the runtime; only the server chain, leaf key and device-key
map are copied into deployment. Watch enrollment still uses the explicit
[USB provisioning procedure](moq-ultra-enrollment.md).

Create an owner-only supervisor JSON file with exactly these fields:

```json
{
  "host_config": "/private/voicewatch/host.json",
  "endpoint_binary": "/path/to/moq-esp32/server/voice_agent/target/release/voicewatch-moq-endpoint",
  "endpoint_sha256": "REPLACE_WITH_THE_64_LOWERCASE_HEX_DIGITS_OF_THE_BUILT_BINARY",
  "port": 8766,
  "database": "/private/voicewatch/moq/agent-control.sqlite3",
  "trace": "/private/voicewatch/moq/live-agent-latency.jsonl"
}
```

The endpoint digest is checked before startup and again after copying. The
executable must be a regular owner-owned executable, not a symlink or writable
by group/others. Configuration errors do not echo keys, filenames or arbitrary
JSON values. The host's configured `ipc_socket` is replaced with a fresh short
private socket path at runtime, and both children receive matching generated
configuration. No preexisting socket is unlinked to force startup.

Use distinct control/time ports and separate database/trace paths from another
running service. The control and time listeners use TCP; media uses UDP. The
local development installation uses TCP 8766/8767 and UDP 4443. Its host address
must remain reachable; an IP address change requires a matching certificate and
watch profile update. No automatic DNS or trust fallback is installed.

## Install and operate

From the containing repository, with the existing private provider environment
files and live-agent development environment available:

```sh
doodad-runtime/scripts/live-agent-service.sh --moq install /private/voicewatch/supervisor.json
doodad-runtime/scripts/live-agent-service.sh --moq status
doodad-runtime/scripts/live-agent-service.sh --moq restart
doodad-runtime/scripts/live-agent-service.sh --moq stop
```

The installer uses `uv sync --locked --no-dev`; aiortc and PyAV are not required.
It copies the native binary, its current Opus notices, server trust and enrollment
map into a new immutable configuration generation, then atomically replaces the
supervisor profile. Previous native/config generations remain for deliberate
rollback; there is no automatic rollback or garbage collection. These notices
do not replace the full third-party license/distribution audit.

MoQ has its own `dev.doodad.live-agent.moq` label, runtime/data beneath
`~/Library/Application Support/Doodad/moq`, and logs beneath
`~/Library/Logs/Doodad/moq`. Private files use owner-only permissions. Existing
WebRTC service files, database and label are not replaced. Reinstallation stops
the MoQ job and waits at most 40 seconds for its profile lock before replacing
Python runtime files. If it does not stop, installation fails without bypassing
the lock. The installer does not erase watch data or flash firmware.

`--moq uninstall` unloads and removes only the launchd entry. It preserves data,
configuration generations and private credentials. Use the explicit WebRTC
commands without `--moq` to manage that separate service.

## Lifetime and failure behavior

Python signals readiness only after its HTTPS/WSS/IPC listeners and discovery
registration start. The supervisor then starts native QUIC, which signals that
its UDP endpoint is bound. These signals mean the pair is listening; actual
watch media readiness is separately recorded as `moq.session_ready` after
identity, watch media and native media all agree. `moq.capture_started` records
an authorized capture transition without logging its PCM or identity.

Either child's exit, even exit code zero, retires the whole pair. Inherited Unix
socket lifetime channels also close when the supervisor is killed, so children
can detect parent death without trusting PID reuse or a stale PID file. The
native process receives no provider, personal signing or SMTP environment keys.
Descriptors are noninheritable after child startup. Shutdown sends SIGTERM and
allows 30 seconds before killing a stuck child; native QUIC also handles SIGINT
and lifetime-channel EOF. Old WSS and media grants are not carried into the next
Python process, and reconnect does not authorize microphone capture.

launchd restarts failed pairs with a ten-second throttle. Startup readiness has
a 60-second bound per child. This is not a CPU-hang watchdog after readiness;
application/session deadlines and the remaining soak tests still matter.
Normal shutdown removes its own temporary directory. A SIGKILL can leave a small
owner-private directory under `/tmp/vw-moq-*`; subsequent starts never reuse or
scan-delete it. Remove such directories only after verifying their owners have
stopped. They contain generated path configuration, not copied enrollment keys.

## Local development trust and remaining acceptance

The recorded local installation uses a 365-day private root and 30-day leaf.
The private root key is retained outside the deployed runtime for deliberate
leaf renewal. Renew the leaf before expiry, preserve its issuing root, verify
SAN/date checks, update the private source host profile and reinstall the MoQ
job. Root replacement or device-key rotation requires a new higher-revision
watch enrollment. Automatic certificate issuance/rotation is not implemented.

The current evidence covers actual process startup, child death, fresh grants,
native TLS/SETUP denial of stale grants, and physical watch reconnection. It
does not establish a physical button-driven provider turn on this deployed
release, complete app/UI parity, impaired-network speech quality, hard process
memory bounds, long-response renewal or release soaks. See
[implementation progress](moq-implementation-progress.md) for those gates.
