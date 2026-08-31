# Physical provider loss/delay matrix

Recorded 2026-08-31 against the source snapshot in this directory and the
previously verified flash43 full-shell image. **All twelve configured cells ran;
eleven passed and one failed the frozen critical-phrase speech check.** This is
not a passing impairment gate or completion of the WebRTC replacement plan.

## Method and lifecycle

Each fresh private host uses the real STT/model/read-tool/TTS pipeline, the native
Rust endpoint, authenticated WSS/QUIC, and the physical Ultra microphone and
speaker. The Mac plays the fixed six-word synthetic fixture at volume 60, then
restores volume/mute. Every cell requests three sequential captures in one
authenticated session, followed by an idle reconnect. A failed provider turn
ends that cell; it is not retried until it happens to pass.

The bounded UDP fixture applies seeded independent loss in both directions and
half the configured delay each way. Seed is 44 throughout. WSS is unaffected.
The table reports **added RTT**, not measured total RTT: the existing Wi-Fi/LAN
path remains present. Observed packet loss can differ from the configured
probability, especially in a cell stopped after its first turn. Exact sent,
dropped, forwarded and fixture-pressure counts are retained in the JSON.

The same firmware, host/native sources, 80 ms startup prebuffer, 200 ms bounds,
and speech policy are used throughout. The only bench tightening since the
previous checkpoint is rejecting component shutdown-timeout events even if a
final `shutdown.completed` event appears. Each result requires full service
startup, a live service owner before shutdown, and completed shutdown. Private
hosts use explicit enrollment endpoints with mDNS advertisement disabled.

## Results

PLC/late/silence are **downlink chunk counters**, not separate loss totals or
an acoustic quality score. A concealed chunk can also arrive late.

| Run | Loss | Added RTT | Speech result | Played responses | PLC / late / silence |
| --- | --- | --- | --- | --- | --- |
| p94 | 0% | 30 ms | PASS, 3 zero-error turns | 3 | 0 / 0 / 0 |
| p95 | 0% | 60 ms | PASS, 3 zero-error turns | 3 | 0 / 0 / 0 |
| p96 | 0% | 120 ms | PASS, 3 zero-error turns | 3 | 0 / 0 / 0 |
| p97 | 1% | 30 ms | PASS, 3 zero-error turns | 3 | 10 / 14 / 4 |
| p98 | 1% | 60 ms | PASS, 3 zero-error turns | 3 | 7 / 6 / 0 |
| p99 | 1% | 120 ms | PASS, 3 zero-error turns | 3 | 7 / 7 / 0 |
| p100 | 3% | 30 ms | PASS, 3 zero-error turns | 3 | 5 / 5 / 0 |
| p101 | 3% | 60 ms | FAIL, first turn changes the critical phrase | 1 | 5 / 5 / 0 |
| p102 | 3% | 120 ms | PASS, 3 zero-error turns | 3 | 13 / 13 / 0 |
| p103 | 5% | 30 ms | PASS, 3 zero-error turns | 3 | 14 / 14 / 0 |
| p104 | 5% | 60 ms | PASS, 3 zero-error turns | 3 | 12 / 11 / 0 |
| p105 | 5% | 120 ms | PASS, 3 zero-error turns | 3 | 22 / 22 / 0 |

There are **33 accepted voice turns and one failed speech turn**, with **34
complete responses totalling 1,435,383 played samples**. Every firmware speaker
sample total matches its host receipt in order, including the response from the
failed speech turn. All twelve hosts reach full startup and clean shutdown,
with no component timeout. All eleven passing cells verify a fresh reconnect
without activating the microphone. p101 stops before that reconnect check.
Every independent bench process exits normally; p101 returns status 1 for its
failed assertion. No cell records a host pacing overrun or firmware crash marker.

The first matrix driver stops at p101. After inspecting its failure and verifying
cleanup, the remaining four previously untested cells run once. p101 is not
rerun or overwritten. The later driver exits successfully as an orchestrator;
that does **not** change the aggregate matrix failure.

## What p101 establishes—and what it does not

Its one admitted completion contains one word error among six words. The allowed
impaired error count is one, but the required contiguous target phrase is absent,
so policy v1 correctly fails it. The fresh watch read and real TTS response still
complete, as does the exact 39,939-sample speaker receipt. The result is a speech
quality failure, not a missing-response-tail or shutdown failure.

The host receives 58,240 microphone samples, including 2,240 samples of PLC
(140 ms), with seven lost and three late groups. Numeric microphone levels are
comparable with earlier runs. Publisher snapshots record two cache drops, eight
expiries and a maximum network-owner wall-time gap of 201,067 us. The proxy has
no queue pressure; its actual uplink drop fraction is 22/453 packets in this
short cell. These observations do not prove that Wi-Fi, the proxy, host
recognition or a particular firmware scheduling path caused the word error.

Passing p99 records a slightly larger owner gap, and p103 reaches 236,943 us.
Several passing captures contain more PLC than p101, including 5,120 samples in
p105's first turn. Neither a gap counter nor concealed-sample total alone predicts
this fixture's recognition result. No buffer, loss budget or speech criterion is
changed in response to the failure. Further investigation should localize loss
relative to the speech fixture and establish repeatability, while preserving this
failed result. Ordinary bounded network recovery remains expected behavior.

## Resources, provenance and retained scope

The largest recorded ngtcp2 allocation peak is 42,192 bytes against the existing
131,072-byte limit, with no allocation denials/system failures. This excludes TLS
and total-device memory, and is not full-shell stress acceptance. The matrix
also does not measure physical speaker audibility, general intelligibility,
calibrated latency or the reliability distribution across networks/seeds.

The full Python suite remains green; exact counts and private-log hashes are in
`verification.json`. No new firmware, native binary or production-host deployment
is made. Public evidence contains whitelisted numeric counters, fixed labels and
source/log hashes. Provider logs, enrollment credentials, databases and synthetic
audio remain private. Ambient microphone PCM is never persisted.

Permanent enrollment is reapplied at revision **156**, with a fresh ready event
afterward. The original MoQ supervisor/child and legacy service processes remain
running. No default-firmware backup or restoration is performed or required.

The matrix remains failed until the speech-quality issue is understood and the
required acceptance is established. Reordering, duplicates, delayed old groups,
blocked-stream cases, expanded speech/downlink quality, physical controls/apps/
package delivery/sleep-wake, security/TLS allocation, latency, operational stream
turnover, 1,000 cycles, eight-hour soak, unchanged-reference compatibility,
sanitizers/CI, clean release and default cutover remain open. WebRTC stays the
configured default and the original full goal remains active.
