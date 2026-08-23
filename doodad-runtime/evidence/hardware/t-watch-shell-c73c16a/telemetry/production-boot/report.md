# CoreS3 Weather hardware telemetry

Captured: `2026-08-05T21:24:26.460659+00:00`  
Port: `/dev/cu.usbmodem22301`  
Duration: `20.0s`  
Firmware: `firmware/build/doodad_runtime.bin` (2,904,960 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 9 |
| Frames / flushes | 4 / 14 |
| Flushed pixels | 116,655 |
| Lifetime frames / flushes | 4 / 14 |
| Lifetime flushed pixels | 116,655 |
| Mean / maximum FPS | 0.222 / 2.0 |
| Mean / maximum render | 7,089.0 / 86,540 us |
| Mean / maximum flush | 600.7 / 6,613 us |
| Lifetime mean / maximum render | 63,801.0 / 86,540 us |
| Lifetime mean / maximum flush | 5,406.0 / 6,613 us |
| LVGL objects | 37–37 |
| Internal free / historical minimum | 7,875 / 3,668 B |
| Internal largest block floor | 7,680 B |
| PSRAM free / historical minimum | 8,051,720 / 8,040,628 B |
| PSRAM largest block floor | 7,995,392 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
