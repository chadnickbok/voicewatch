# Ultra enrollment and authenticated full-shell testing

This is the development enrollment path for the full Ultra firmware. The host
issues separate one-use WSS and native MoQ capabilities after a device proof.
No production capability or enrollment key is compiled into the firmware.

## Firmware requirements

Build the Ultra with MoQ uplink/autoconnect selected and the USB Serial/JTAG
console. `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` is mandatory; the bootstrap source
rejects builds without certificate date verification. The Ultra defaults enable
it. Trust is established before HTTPS, using a fresh 256-bit nonce and a
domain-separated HMAC time proof from the configured time listener. Unauthenticated
SNTP does not overwrite this clock in a MoQ build.

The enrollment worker installs the USB serial driver before enabling nonblocking
reads. Its internal stack permits NVS writes. A profile must contain a parseable
CA bundle and a higher revision than the installed profile. Installation writes
only the `moq_enroll` NVS namespace. Profile changes invalidate the old clock and
session and allow a new authentication attempt after terminal failures.

Profiles are not encrypted in NVS on the current development device. This path
does not claim protection against a physical flash/debug attacker. Secure boot,
flash encryption and irreversible fuse changes are not performed by these tools.

## Physical enrollment tool

Run `doodad-runtime/tools/moq_enroll.py` with the ESP-IDF Python environment,
which supplies pyserial. `info --port PORT --output PRIVATE_FILE` writes device
identity and profile revision privately. `install --port PORT --profile
PRIVATE_PROFILE --output PRIVATE_RESULT` checks the connected identity and
revision before transmitting the profile. Output files must not already exist.

The profile is an owner-only regular JSON file containing exactly `v` (1),
`revision` (positive u32), `device_id`, `host`, `control_port`, `time_port`,
`roots_pem`, and `key_hex` (a unique nonzero 32-byte key in lowercase hex).
The host enrollment file maps that device identity to the same key; do not reuse
keys across devices. Neither keys nor full profiles belong on command lines,
in source control, in static firmware headers, or in terminal output.

The tool does not echo incoming serial data. Its optional `monitor` command
writes raw serial to a private output file. The USB wire protocol is bounded to
8 KiB per line and rejects incomplete commands after five seconds.

## Hardware bench

Run `doodad-runtime/services/live-agent/tools/moq_ultra_bench.py` with the
live-agent Python environment. Required arguments are `--output` (a fresh private
directory), `--port`, `--host` (the Mac's reachable LAN IPv4 address), and
`--idf-python` (the Python executable used by the USB tool).

The bench generates temporary private certificates and a fresh per-device key,
starts the real Python transport and Rust QUIC endpoint on local free ports,
and installs a higher profile revision over USB. It leaves the profile installed
when it stops; its test host is then unavailable. Run a new bench or deliberately
provision the permanent service profile to reconnect. Certificates expire after
six hours. Do not use this temporary PKI as a deployment procedure.

Default mode checks startup without recording, forced WSS replacement, distinct
fresh grants, and automatic reconnection after a 45-second lease. `--audio`
explicitly captures 1.2 seconds from the watch microphone, counts and discards the
received PCM, then plays a synthetic 16,037-sample tone. Completion requires the
watch's matching end/sample receipt; host spool completion is insufficient.
It also cancels a following response and completes a replacement against the
same captured turn, without another microphone capture.
No provider is contacted, and no ambient PCM is written to disk.

`--certificate-fault expired|not_yet_valid|hostname|untrusted` tests firmware
certificate rejection with no audio. A pass requires issuance of the fresh time
proof, an actual X.509 rejection reported by firmware, no session establishment,
and no retry during the post-rejection observation interval. It cannot pass just
because an unreachable watch failed to connect. These cases exercise the HTTPS
bootstrap verifier; they are not substitutes for native QUIC certificate/ALPN
negative tests or long-duration retry/rotation tests.

Build the native endpoint binaries before running the bench. The output contains
private provisioning/configuration, raw serial/native logs and a summary. Only
sanitized summary/metrics should be copied into repository evidence.

## Flashing and remaining scope

Use `libs/moq-esp32/tools/run_ultra_transport.py` with the full-shell image and
success marker `[host] steady state; free heap:`. It validates the connected
chip, security state and partition/OTA layout, then writes app0 only. It does not
erase NVS, change the bootloader/partition table, or restore default firmware.

This bench verifies media through the production transport adapter while running
the actual shell. It does not exercise STT/model/tools/TTS, personal-app delivery,
all navigation/lifecycle cases, impaired networks, long responses across renewal,
full-shell memory stress, or release soaks. See the full replacement plan and
implementation progress for those remaining gates.
