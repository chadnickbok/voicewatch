# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T20:39:57.511147+00:00`  
Port: `/dev/cu.usbmodem21101`  
Duration: `112.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 53 |
| Frames / flushes | 864 / 3577 |
| Flushed pixels | 2,565,679 |
| Lifetime frames / flushes | 959 / 3982 |
| Lifetime flushed pixels | 2,873,855 |
| Mean / maximum FPS | 8.111 / 48.5 |
| Mean / maximum render | 8,755.4 / 102,293 us |
| Mean / maximum flush | 567.2 / 9,033 us |
| Lifetime mean / maximum render | 74,154.0 / 102,293 us |
| Lifetime mean / maximum flush | 5,568.0 / 9,033 us |
| LVGL objects | 13–37 |
| Internal free / historical minimum | 19,539 / 19,084 B |
| Internal largest block floor | 11,264 B |
| PSRAM free / historical minimum | 7,740,092 / 7,732,324 B |
| PSRAM largest block floor | 7,733,248 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
