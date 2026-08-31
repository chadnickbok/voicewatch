# Ultra UI and full-shell baseline — 2026-08-30

The physical Ultra now boots the full portrait native shell and resident WAMR
recovery app. This is the shell baseline **before MoQ voice integration**. The
previous physical MoQ echo remains a separate checkpoint, not evidence that
voice already works inside this shell. Production auth/control/host, PTT,
installed-app parity and full acceptance remain incomplete.

## Evidence and scope

- `ui-probe-*`: verified CST9220 at 410×502, DRV2605L identity, battery reads,
  XL9555 readback and 205,820 CO5300 pixel transfers, with zero polling errors.
  No touch/button presses were observed. Transfers are not optical proof of
  color order or tactile confirmation. The first failed run found an IDF
  zero-payload SPI pointer requirement; the corrected second run passed.
- `offline-*`: initial full-shell boot, existing package filesystem mount,
  recovery Wasm startup, native Home render and one-minute heartbeat. This
  exposed LVGL's 128 KiB fixed pool consuming internal SRAM.
- `wifi-psram-*`: successful one-minute repeat with Wi-Fi and the fixed LVGL
  pool moved to PSRAM through its supported large-array annotation. No managed
  dependency was edited. Device identifiers and Wi-Fi logs are excluded.
- `lvgl-pool-placement.json`: the linker map and inspected ELF place the pool
  at `0x3c2542e8`, size `0x20000`, in external BSS.
- `storage-tests.log`: five existing firmware-storage regressions pass. The
  build/flash/monitor scripts also pass shell syntax checks; a full scripted
  rebuild was not used for these images (the documented isolated IDF build was).
- `snapshot.json`: source/artifact hashes. Raw logs, Wi-Fi configuration and
  credential-bearing firmware remain private. GitHub CI execution is not claimed.

| Measurement | Initial offline shell | Wi-Fi shell / LVGL in PSRAM |
| --- | ---: | ---: |
| Free internal RAM at stable Home | 129,655 | 220,123 |
| Minimum internal free | 93,524 | 183,992 |
| Largest internal block | 51,200 | 147,456 |
| Free PSRAM | 8,069,836 | 7,912,024 |
| Maximum observed flush, us | 6,102 | 6,417 |
| Maximum observed initial render, us | 276,319 | 187,893 |

These are baseline observations, not matched benchmarks or voice-active budgets.
Wi-Fi was absent in the first run. No Opus, QUIC endpoint or WSS allocation is
present in either shell image. Idle RAM stabilizes during these short runs;
they do not prove repeated-session leak freedom or eight-hour idle behavior.

Both runs mounted the existing `ffat` partition without formatting. The profile
had zero installed apps and no provisioned personal-install key, so only the
embedded recovery guest was exercised. NVS initialization now fails without
erasing when explicit recovery is needed. The live table was inspected and
matched in the Ultra build; the runner verified identity/security/OTA metadata
and wrote app0 only. No partition/NVS/package erase or restoration occurred.

The latest installed image is
`469861f8d4cf5e827592dd4ec773e5398c084d3fe57599214c3ed39bd23b7371`.
It keeps microphone/speaker off and WebRTC disabled. Next is the production
MoQ media seam and authenticated host/control lifecycle, with measured combined
resource use and explicit PTT. Optical/touch/wake/guest-surface validation and
all long-response, impairment, cancellation and sustained gates remain open.
