# Phase 6 CoreS3 hardware checkpoint

This checkpoint records the first physical render and bounded runtime telemetry
for the five-screen Weather implementation. It covers the **Current** route on
the CoreS3 at 240×240. It does not yet cover T-Watch S3, touch interaction,
power draw, or T-Watch S3 wrist ergonomics. The checked firmware does include
the new swipe route and entry-animation path; touch-driven frame telemetry is
still an open capture.

## Visual evidence

- `desktop-vs-cores3.png` — the exact LVGL desktop scene beside a calibrated
  photograph of the CoreS3 display.
- `weather-cores3-physical.png` — the corrected 240×240 physical crop.
- `camera-calibration-profile.json` — the fixture-local calibration used for
  the photograph.

The physical photograph confirms composition, clipping, legibility, and
successful asset/font rendering. It is **not colorimetric proof**: the camera
calibration reduced patch RMSE from 0.2358 to 0.1160, but five dark patches
were crushed and the photographed LCD retains cyan/magenta moiré. Renderer
goldens remain the color oracle.

## Runtime result

The accepted firmware is 1,706,976 bytes and leaves 46% of the 3 MiB app
partition free. `telemetry.json` and `telemetry-report.md` capture the stable
post-boot state:

| Metric | Result |
| --- | ---: |
| LVGL objects | 31 |
| Lifetime frames / flushes | 4 / 14 |
| Lifetime flushed pixels | 116,655 |
| Lifetime average / maximum render | 45.2 / 86.3 ms |
| Lifetime average / maximum flush | 3.66 / 4.21 ms |
| Internal free / historical minimum | 125,883 / 85,548 B |
| Internal largest-block floor | 49,152 B |
| PSRAM free / historical minimum | 8,173,072 / 8,165,772 B |

The display settles without idle redraws. Moving the two LVGL draw strips and
the private WAMR heap to PSRAM raised steady internal free memory from 26,107 B
to 125,883 B and the historical floor from 12,528 B to 85,548 B. A trial that
moved the runtime pthread stack into PSRAM was rejected after it triggered an
ESP32-S3 cache-safety assertion; the checked configuration keeps that stack in
internal RAM.

## Open hardware gates

- Exercise taps and swipes while capturing dirty-region and latency samples.
- Measure route-transition frame-time distribution; the current first-paint
  average is above the 33.3 ms target and needs profiling or a reduced-motion
  fallback.
- Repeat on T-Watch S3 and measure power, sunlight legibility, and wrist use.
