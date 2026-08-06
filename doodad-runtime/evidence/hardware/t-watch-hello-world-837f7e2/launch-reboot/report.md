# CoreS3 Weather hardware telemetry

Captured: `2026-08-06T03:24:23.700747+00:00`  
Port: `/dev/cu.usbmodem22301`  
Duration: `25.0s`  
Firmware: `firmware/build/t-watch-s3/doodad_runtime.bin` (2,824,160 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 11 |
| Frames / flushes | 6 / 20 |
| Flushed pixels | 174,255 |
| Lifetime frames / flushes | 6 / 20 |
| Lifetime flushed pixels | 174,255 |
| Mean / maximum FPS | 0.273 / 2.0 |
| Mean / maximum render | 13,861.5 / 99,371 us |
| Mean / maximum flush | 1,102.0 / 7,488 us |
| Lifetime mean / maximum render | 64,672.0 / 99,371 us |
| Lifetime mean / maximum flush | 5,876.0 / 7,488 us |
| LVGL objects | 37–39 |
| Internal free / historical minimum | 22,499 / 22,460 B |
| Internal largest block floor | 14,336 B |
| PSRAM free / historical minimum | 7,933,496 / 7,924,496 B |
| PSRAM largest block floor | 7,864,320 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
