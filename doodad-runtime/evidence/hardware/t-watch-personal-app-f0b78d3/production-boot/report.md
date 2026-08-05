# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T22:54:17.832067+00:00`  
Port: `/dev/cu.usbmodem22301`  
Duration: `12.0s`  
Firmware: `firmware/build/t-watch-s3/doodad_runtime.bin` (2,824,160 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 5 |
| Frames / flushes | 4 / 14 |
| Flushed pixels | 116,655 |
| Lifetime frames / flushes | 4 / 14 |
| Lifetime flushed pixels | 116,655 |
| Mean / maximum FPS | 0.400 / 2.0 |
| Mean / maximum render | 12,979.4 / 86,697 us |
| Mean / maximum flush | 1,131.8 / 7,079 us |
| Lifetime mean / maximum render | 64,897.0 / 86,697 us |
| Lifetime mean / maximum flush | 5,659.0 / 7,079 us |
| LVGL objects | 37–37 |
| Internal free / historical minimum | 22,499 / 22,460 B |
| Internal largest block floor | 14,336 B |
| PSRAM free / historical minimum | 7,934,284 / 7,925,048 B |
| PSRAM largest block floor | 7,864,320 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
