# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T20:29:20.919287+00:00`  
Port: `/dev/cu.usbmodem21101`  
Duration: `125.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 19 |
| Frames / flushes | 6 / 20 |
| Flushed pixels | 174,255 |
| Lifetime frames / flushes | 6 / 20 |
| Lifetime flushed pixels | 174,255 |
| Mean / maximum FPS | 0.158 / 2.0 |
| Mean / maximum render | 5,954.8 / 90,099 us |
| Mean / maximum flush | 569.3 / 7,088 us |
| Lifetime mean / maximum render | 54,255.0 / 90,099 us |
| Lifetime mean / maximum flush | 5,290.0 / 7,088 us |
| LVGL objects | 13–37 |
| Internal free / historical minimum | 30,323 / 28,496 B |
| Internal largest block floor | 20,480 B |
| PSRAM free / historical minimum | 8,003,736 / 7,998,580 B |
| PSRAM largest block floor | 7,995,392 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
