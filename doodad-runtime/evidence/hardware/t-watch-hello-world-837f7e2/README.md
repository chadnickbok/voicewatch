# T-Watch Hello World voice-build proof

Completed on 2026-08-05 with T-Watch S3
`t-watch-s3-a0f262e11e18` (`a0:f2:62:e1:1e:18`). The watch runs firmware
`074f34a`; the pickup-ready service and Hello World build contract are commit
`837f7e2`.

## Production generation

- Exact brief: `Build me a hello world app`
- Durable job: `job_14119d0a99cd449db15f3c9ec1300f28`
- Final state: `ready_for_review`
- Product question: none
- App: `dev.doodad.generated-hello-world` version `0.1.0`
- Identity: curated `generic` icon and theme seed `#2563EB`
- Capability set: `ui.mount`
- Interaction: toggle the visible greeting with one semantic default-size button
- Verification gates: schema, plan, semantics, permissions, build, check,
  test, Wasm inspect, conformance, and simulator render

The independent verifier rejected two intermediate generations and allowed the
bounded worker to repair them. Only the final ten-gate artifact reached the
outer packager.

## Signed delivery and hardware result

- Payload SHA-256:
  `c509daeb80a2c0abdb12099fff2759adc958851f1aedbe7edef0cb39ee420d3a`
- Bundle SHA-256:
  `86550aa4829eab01cf2586523c4daa6334a4b12ac35f3a174134a205859f6ada`
- Signed bundle bytes: `1453`
- Owner/key labels: `local.nick` / `personal-v1`

The live T-Watch serial stream reported the matching bundle announcement and
installation:

```text
voice-service: app.ready accepted bundle=86550aa4829e bytes=1453
package-service: installed dev.doodad.generated-hello-world 0.1.0 payload=c509daeb80a2 bundle=86550aa4829e
doodad: [visual] device_id=t-watch-s3-a0f262e11e18 scene=app-ready
voice-service: peer state=7
```

The user then tapped **Launch now**, observed the generated Hello World UI, and
tapped its greeting button successfully.

## Persistence capture

After that successful launch, the bounded `launch-reboot` capture hard-reset
the watch and recorded:

- package storage mounted with `apps=1` and 10032 KiB free;
- normal module load, instantiation, and UI mount;
- replay of the same immutable bundle announcement without a second install;
- voice peer state 7; and
- no guest failure or rollback.

See [launch-reboot/report.md](launch-reboot/report.md),
[launch-reboot/telemetry.json](launch-reboot/telemetry.json), and the raw
[launch-reboot/serial.log](launch-reboot/serial.log).

No signing key or provider credential is included in this evidence.
