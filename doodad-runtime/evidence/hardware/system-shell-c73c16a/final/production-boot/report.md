# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T20:49:54.874498+00:00`  
Port: `/dev/cu.usbmodem21101`  
Duration: `28.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 13 |
| Frames / flushes | 4 / 14 |
| Flushed pixels | 116,655 |
| Lifetime frames / flushes | 4 / 14 |
| Lifetime flushed pixels | 116,655 |
| Mean / maximum FPS | 0.154 / 2.0 |
| Mean / maximum render | 4,020.3 / 91,159 us |
| Mean / maximum flush | 393.0 / 7,020 us |
| Lifetime mean / maximum render | 52,264.0 / 91,159 us |
| Lifetime mean / maximum flush | 5,109.0 / 7,020 us |
| LVGL objects | 37–37 |
| Internal free / historical minimum | 28,595 / 28,396 B |
| Internal largest block floor | 20,480 B |
| PSRAM free / historical minimum | 7,924,692 / 7,913,180 B |
| PSRAM largest block floor | 7,864,320 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
