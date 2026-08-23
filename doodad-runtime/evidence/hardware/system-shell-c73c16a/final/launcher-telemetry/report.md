# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T20:46:03.740818+00:00`  
Port: `/dev/cu.usbmodem21101`  
Duration: `38.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 18 |
| Frames / flushes | 6 / 20 |
| Flushed pixels | 174,255 |
| Lifetime frames / flushes | 6 / 20 |
| Lifetime flushed pixels | 174,255 |
| Mean / maximum FPS | 0.167 / 2.0 |
| Mean / maximum render | 7,389.7 / 92,782 us |
| Mean / maximum flush | 606.1 / 6,441 us |
| Lifetime mean / maximum render | 68,406.0 / 92,782 us |
| Lifetime mean / maximum flush | 5,281.0 / 6,441 us |
| LVGL objects | 13–37 |
| Internal free / historical minimum | 28,575 / 27,732 B |
| Internal largest block floor | 20,480 B |
| PSRAM free / historical minimum | 7,924,640 / 7,913,124 B |
| PSRAM largest block floor | 7,864,320 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
