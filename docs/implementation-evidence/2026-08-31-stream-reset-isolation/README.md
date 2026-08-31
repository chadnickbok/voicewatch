# Stream reset isolation and impaired capture recovery

Work after VoiceWatch `a3671f7` and moq-esp32 `d589dc9`. This fixes a reproduced
service error-scope defect. It does not complete replacement acceptance.

## Corrected diagnosis and implementation

t65 repeats the failing native bench with flash28 at 5% loss, 120 ms added RTT,
seed 52 and an 800 ms uplink blackout. It records service error 12 / code 3,
but both local control timeout counters are zero. The earlier attribution of
t64 to a catalog timeout was incorrect: `esp_moq_service_retire` emitted the
same global error for **every** nonzero stream reset. In particular, retirement
of an expired audio group could make VoiceWatch close the whole session. The
event alone identifies neither a catalog operation nor the reset's initiator.

Retirement now goes through the engine's existing operation ownership. Partial
receive groups report GAP; outgoing groups retire their TX job, including late
acknowledgements of local reset/expiry. Catalog and discovery control errors
still report SERVICE_ERROR. A failed receive control reports its identity-bound
RECEIVE_END. Raw error codes remain in service statistics. No transport pool,
audio queue, concealment budget or capture storage limit is enlarged.

A regression fails before the fix when a partial audio group reset emits a
global error instead of GAP. Afterward both raw and mapped-error variants pass:
the next audio group completes its receive, a late outgoing expiry retirement
does not disrupt publication/catalog readiness, and new audio is accepted.
Separate cases ensure catalog, discovery and media-control failures each yield
one appropriate terminal outcome. Existing cancellation and malformed-data
coverage remains in the normal host suite.

## Hardware evidence

| Run | Firmware | Result |
| --- | --- | --- |
| t65 | Flash28 | Fail after 23,542 ms; no completed captures/replies. Global service error code 3; both control timeout counters zero. |
| t66 | Flash29 isolation fix | Pass: interrupted capture aborts, fresh audio resumes in 1,193 ms on the same session, three four-second captures/replies complete, response replacement and lease renewal pass. |
| p63 | Flash29, real provider pipeline | Fail: interrupted capture aborts without STT commit and the session survives, but the next capture loses its first 20 groups and aborts. Zero successful provider turns or STT commits. |
| p64 | Flash30 pressure diagnostics, real providers | Fail: recovery starts in 1,153 ms but a later 16-group gap aborts the next capture. Local opens are denied 42 times; peer-credit and payload-block denials are zero. Up to nine stopped local send halves occupy slots at denial. |
| p65 | Flash31 retirement reserve, real providers | Fail: recovery starts in 1,579 ms, but a later 21-group gap aborts capture. No STT commit or session failure. Local open denials 28, peer-credit denials 73, payload-block denials zero. |
| p66 | Flash32 batched DROP notices, real providers | Fail overall: recovery starts in 710 ms; capture, one STT commit, watch-state read and response playback complete, but the spoken fixture is not recognized. The three-turn gate does not pass. |

All six use the requested 5%/120 ms/800 ms impairment above. A seeded rerun is
not a byte-for-byte replay of packet timing. The 120 ms value is added delay,
not a claim about total observed RTT.

t66 counts/discards 196,696 microphone samples, including three complete
64,000-sample captures. Its four generated tones each contain 16,037 samples,
with 3/1/2/2 concealed and late packets, zero playback pressure and zero silence.
Forced reconnect takes 6,333 ms; three sessions reach readiness including expiry
renewal. This is neither physical-button PTT nor calibrated speech-quality or
endurance evidence.

p63 counts/discards 4,056 microphone samples. Its second capture reports
`next=87 available=107`: the missing prefix exceeds the existing loss bound.
The watch reports publisher-cache drops and transmit expiry, but no service or
endpoint failure. Its largest recorded capture poll gap is 147 ms. These
counters do not yet establish whether stream slots, peer credit, or payload
blocks prevent the new prefix from being sent. Mac fixture output settings are
restored to their prior state. No ambient microphone PCM is persisted.

Flash29 writes only app0 and reaches the full-shell steady-state marker. Its
hash and write span are in `firmware.json`; it remains installed until another
new-firmware test replaces it. Default firmware restoration is not required.

## Verification and next work

The isolation fix passes all seven normal C host suites and normal plus Linux
ASan/UBSan adapter/host suites, as well as the ESP-IDF 5.5.5 firmware build.
`verification.json` records log hashes and scope; `source-snapshot.json` binds
those results to source and firmware hashes. No Rust or Python production code
changes in this checkpoint.

Follow-up numeric adapter diagnostics distinguish denied local stream opens,
denied peer stream credit and exhausted payload blocks. They also record the
maximum stopped local send halves present at a local-pool denial. These are
attempt counts, not packet-loss rates or elapsed time. Pool capacities and ACK
ownership are unchanged. Their follow-up build/hardware results are recorded
separately from flash29.

p64 confirms local send-slot pressure during recovery, though those aggregate
counters do not attribute every missing group. The adapter now provides 32
total TX metadata slots while retaining the 16-slot active limit and four
default peer reply reservations. By default at most 12 local send halves are
active; at most 28 local halves in total, including stopped ones, can exist.
Stopped halves no longer consume the active quota, but still own their original
IDs and payload blocks until backend release. The 64 x 256-byte payload pool,
engine TX job count, audio buffers and deadlines are unchanged.

The old adapter fails the new epoch-opening regression. The revised adapter
passes it, including the hard total bound, reserved peer replies, retained byte
ownership and release of one slot without reusing IDs or touching another
stream's bytes. Sixteen extra 56-byte metadata entries cost 896 bytes on the
host ABI; additional live ngtcp2 allocations still need sustained physical
measurement. Normal and Linux ASan/UBSan adapter/host suites and the firmware
build pass. Flash31 boots the full shell; its hash and
boot free-heap count are in `retiring-firmware.json`. p65 does not pass provider
acceptance of this reserve; active-slot/credit pressure and capture loss remain.

The follow-up engine regression identifies an additional scheduling defect:
twelve expired group jobs create twelve required DROP notices, but the engine
sends only one per owner poll and defers fresh groups until they all drain.
The corrected engine serializes the pending standard DROP messages into one
bounded, owned control write. It clears notices only after enqueue acceptance,
preserves every individual sequence/error, and retains the write across backend
backpressure. No wire extension, extra queue storage or media deadline change is
introduced. The regression fails before and passes after batching, including a
blocked control write and verification of all twelve notices without duplicates.

The batching change also passes normal and Linux ASan/UBSan adapter/host suites
and the firmware build. Flash32 writes only app0, boots the full shell and is
now installed; see `drop-batch-firmware.json` and its source snapshot. p66
counts/discards 63,416 microphone samples across the interrupted and subsequent
capture. The interrupted capture is not committed; the subsequent capture
produces 59,680 samples, including 9,600 PLC/concealed samples (16.1%), with
30 lost groups and three late groups. It produces one STT completion and the
required watch-state read. The response
plays exactly 50,832 samples with ten concealed/late packets, zero pressure and
zero silence. STT fails the spoken-fixture recognition gate, so no full provider
turn is counted as passing and the remaining two turns are not attempted.
Fresh speech recovery is not accepted on the basis of these sample counts.

p66 still reports capture gaps (including a ten-group gap), 60 denied local
opens and 374 denied peer-credit opens; no payload-block denial is recorded.
These are attempt counts, not distinct lost packets. Input peak reaches 32,766
and RMS is 13,684.75, but these aggregate levels alone do not identify the
relative contributions of acoustic clipping and transport concealment to STT
failure. Mac fixture audio settings are restored. No ambient PCM is saved.

The next step is to separate acoustic/codec quality from remaining capture loss
using a defined synthetic speech reference and bounded in-memory measurements.
High-loss capture quality, the complete
impairment matrix, provider behavior, original p50 attribution, allocation caps,
full physical shell interaction, deployment/default switch, latency, long speech
and endurance gates remain open. WebRTC is still the default.

Only reviewed counters, source/binary hashes and test scope are published here.
Private profiles, credentials, provider transcripts and raw device logs remain
excluded.
