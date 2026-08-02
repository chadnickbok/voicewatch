# CoreS3 Weather hardware telemetry

Captured: `2026-08-02T04:04:42.841748+00:00`
Port: `/dev/cu.usbmodem21101`
Duration: `8.0s`
Firmware: `firmware/build/doodad_runtime.bin` (1,706,976 bytes)

| Metric | Result |
| --- | ---: |
| Samples | 4 |
| Frames / flushes | 0 / 0 |
| Flushed pixels | 0 |
| Lifetime frames / flushes | 4 / 14 |
| Lifetime flushed pixels | 116,655 |
| Mean / maximum FPS | 0.000 / 0.0 |
| Mean / maximum render | 0.0 / 0 us |
| Mean / maximum flush | 0.0 / 0 us |
| Lifetime mean / maximum render | 45,189.0 / 86,340 us |
| Lifetime mean / maximum flush | 3,660.0 / 4,213 us |
| LVGL objects | 31–31 |
| Internal free / historical minimum | 125,883 / 85,548 B |
| Internal largest block floor | 49,152 B |
| PSRAM free / historical minimum | 8,173,072 / 8,165,772 B |
| PSRAM largest block floor | 8,126,464 B |
| Touch presses observed | 0 |
| Transfer mode | synchronous |

Idle windows are expected to report zero FPS after the screen has
settled. Render/flush maxima include first paint only when this report
was captured with a hardware reset.
