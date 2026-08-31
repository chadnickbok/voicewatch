# Optional longer MoQ testing

The user clarified on 2026-08-31 that **ten minutes of successful operation is
enough for now**. WebRTC was not required to pass an eight-hour test. Longer MoQ
endurance is therefore not an integration, replacement or default-switch gate.
The user subsequently resumed work on physical controls/apps/sleep-wake,
interoperability and release checks. Induced impaired-network testing is also
deferred beyond initial acceptance. Neither long endurance nor impairment
matrices should restart automatically as replacement gates.

Existing longer results remain valid evidence: the full-shell 30-minute duplex
test passed 90,000 groups in each direction. This is transport evidence; it
does not automatically prove ten minutes of voice interaction or full UI parity.
An interrupted idle run must not be relabelled as a completed ten-minute test.

If useful after initial adoption, a longer session could look like this:

1. Run the full shell for several hours, using normal certificate verification
   and short credential leases. Keep the device idle between brief voice turns.
2. Exercise occasional disconnect/reconnect, Wi-Fi interruptions, sleep/wake
   and foreground app changes. Confirm renewed sessions recover, old replies
   cannot play, and reconnect does not open the microphone.
3. Optionally repeat up to 1,000 capture/echo cycles. Record exact capture and
   playback completion, fresh operation identities and resource release between
   turns. Host-triggered cycles do not substitute for physical-button checks.
4. Sample internal RAM, largest free block, PSRAM, task stacks, media pools and
   host memory. Look for accumulating loss across comparable settled states;
   a small one-off free-heap difference is an investigation lead, not a leak
   diagnosis by itself.
5. Record speech quality, complete response tails, latency and recovery. Expect
   some concealment or late arrivals over Wi-Fi; do not add overlapping counters
   as independent packet losses or demand zero concealment as the success rule.

Keep logs private and export only fixed labels, numeric observations and
artifact hashes. Never persist ambient microphone PCM. Reapply permanent
enrollment after temporary bench credentials; do not restore factory firmware.
Any temporary sleep inhibitor ends with the test. Do not disturb other services.

The existing eight-hour harness and resource audit may be reused for this
optional work. Their historical `eight_hour_gate_complete` or cumulative audit
flags do not define current readiness. No long test should restart automatically
or delay adoption solely because its optional result is missing.
