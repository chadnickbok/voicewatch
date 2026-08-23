# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T20:43:17.335200+00:00`  
Port: `/dev/cu.usbmodem21101`  
Duration: `106.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 50 |
| Frames / flushes | 866 / 3578 |
| Flushed pixels | 2,611,551 |
| Lifetime frames / flushes | 961 / 3974 |
| Lifetime flushed pixels | 2,861,535 |
| Mean / maximum FPS | 8.616 / 48.8 |
| Mean / maximum render | 11,362.4 / 103,880 us |
| Mean / maximum flush | 719.0 / 6,679 us |
| Lifetime mean / maximum render | 74,843.0 / 103,880 us |
| Lifetime mean / maximum flush | 5,571.0 / 6,679 us |
| LVGL objects | 13–37 |
| Internal free / historical minimum | 19,927 / 19,084 B |
| Internal largest block floor | 11,264 B |
| PSRAM free / historical minimum | 7,796,108 / 7,732,716 B |
| PSRAM largest block floor | 7,733,248 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
