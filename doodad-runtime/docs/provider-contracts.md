# Provider contracts

Doodad guests never own privileged service loops. Native host services own
exact scheduling, sensors, audio, connectivity, and other capabilities; a
package receives bounded versioned events and returns the same atomic
CommandBatch used for touch interactions.

## Provider event v1

The canonical wire shape is defined in
`contracts/provider-event-v1.cddl`. Every event includes:

- provider and event identifiers;
- a monotonically increasing provider revision;
- freshness (`current`, `stale`, `offline`, or `error`);
- the deterministic scenario time at which it was observed;
- a bounded provider-specific canonical CBOR payload.

The payload is a byte string so adding a provider does not widen the stable
host ABI. A package that declares a provider capability exports:

```text
handle_provider_event(pointer: i32, length: i32) -> i64
```

The result is the same borrowed guest CommandBatch slice returned by
`handle_event`. The host validates and copies both directions synchronously.

## Exact scheduler

Timer is the first complete provider path. The package imports the
capability-scoped `timer_schedule_after`, `timer_cancel`, and
`timer_acknowledge` functions. The trusted host:

- stores eight fixed-capacity schedule records;
- uses a monotonic scenario deadline, unaffected by wall-clock/timezone edits;
- journals all records for reboot restoration;
- transitions a deadline to `firing` once;
- emits `timer.changed` provider events;
- publishes app, glance, complication, notification, ongoing, and Voice
  projections atomically through the trusted surface registry.

Desktop time advances only when a test calls `NativeHost.advance_time`, so
tests run instantly and deterministically. Firmware uses ESP monotonic time
and publishes at most once per second plus the exact firing transition.

The scheduler journal and reboot behavior are tested natively. Persisting that
journal to CoreS3 NVS before real power loss remains a hardware-lane task.

## Cross-surface rule

`SurfaceRegistry` accepts one fixed-capacity `DomainSurfaceSnapshot` at a time.
Every declared projection must carry the authoritative domain revision. A
mismatched, partial, repeated, or regressing publication is rejected without
changing the prior state. The trusted Home shell derives its Live Card,
notification, and ongoing counts from this registry.

Quarantining a package immediately deactivates all of its host-owned
projections and blocks further publications until an explicit recovery. Every
suite package also has deterministic baseline, stale, and recovered snapshots
whose projection revisions move atomically from 1 to 2 to 3.

The conformance suite uses domain-scoped imports rather than one catch-all
fixture call. Calendar, audio, medication, sensor, sleep, media, navigation,
transit, smart-home, sports, wallet, remote-control, workout-store, and
game-clock packages each import a distinct capability function. The host also
validates a bounded domain-specific operation prefix before accepting a
request. This makes cross-domain access fail at both package permission
validation and the Wasm symbol boundary.

`fixture.interact` remains a clearly marked diagnostic-only capability for
host tests, but none of the 20 suite packages declares or imports it. Production
calendar, audio, sensor, media, location, transit, home, sports, wallet, and
remote protocols remain out of this UI conformance phase.
