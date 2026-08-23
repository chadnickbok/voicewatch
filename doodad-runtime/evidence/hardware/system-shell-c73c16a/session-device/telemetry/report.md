# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T20:32:48.524597+00:00`  
Port: `/dev/cu.usbmodem21101`  
Duration: `115.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 19 |
| Frames / flushes | 7 / 26 |
| Flushed pixels | 231,855 |
| Lifetime frames / flushes | 7 / 26 |
| Lifetime flushed pixels | 231,855 |
| Mean / maximum FPS | 0.184 / 2.5 |
| Mean / maximum render | 6,306.4 / 88,263 us |
| Mean / maximum flush | 596.4 / 7,098 us |
| Lifetime mean / maximum render | 55,954.0 / 88,263 us |
| Lifetime mean / maximum flush | 5,451.0 / 7,098 us |
| LVGL objects | 13–37 |
| Internal free / historical minimum | 28,575 / 27,468 B |
| Internal largest block floor | 16,384 B |
| PSRAM free / historical minimum | 7,924,656 / 7,913,300 B |
| PSRAM largest block floor | 7,864,320 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
