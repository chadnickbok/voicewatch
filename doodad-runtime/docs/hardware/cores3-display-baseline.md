# CoreS3 SE display baseline

Date: 2026-07-30  
Board: M5Stack CoreS3 SE, ESP32-S3 revision 0.2  
Transport: USB Serial/JTAG at `/dev/cu.usbmodem21101`  
Panel: 320×240 physical, centered 240×240 watch viewport with 40px side gutters  
Color: RGB565  
Workload: continuously invalidated 240×240 Material catalog stress scene

## Selected development configuration

- CPU: 240MHz
- display SPI write clock: 80MHz requested and reported
- LVGL refresh period: 8ms
- FreeRTOS tick: 1000Hz
- UI service cadence: 2ms
- two internal-DMA strip buffers, 240×40 pixels each
- asynchronous `pushImageDMA`; `lv_display_flush_ready()` is issued only after
  `dmaBusy()` clears

The selected configuration sustains 27.8 FPS under the deliberately pessimistic
full-screen workload. A normal watch screen should invalidate substantially less
area.

## Measurement matrix

| Candidate | Buffers | SPI | Refresh/tick | CPU | Sustained FPS | Avg flush | Notes |
|---|---:|---:|---:|---:|---:|---:|---|
| A | 2×24 rows | 40MHz | 33ms/100Hz | 160MHz | 16.7 | 2.54ms | initial baseline |
| B | 2×40 rows | 40MHz | 8ms/100Hz | 160MHz | 19.8 | 4.08ms | fewer flushes |
| C | 2×40 rows | 80MHz | 8ms/100Hz | 160MHz | 24.5 | 2.16ms | stable 80MHz bus |
| D | 2×40 rows | 80MHz | 8ms/1000Hz | 160MHz | 25.6 | 2.16ms | 2ms UI cadence |
| E | 2×40 rows | 80MHz | 8ms/1000Hz | 240MHz | 27.8 | 2.15ms | selected |

Candidate E steady-state windows reported 56 frames and 336 flushes per roughly
2.017 seconds, or six strips per full frame. Average render duration was about
31.26ms, maximum render duration about 31.29ms, and the DMA state was idle at
each telemetry boundary. No overlapping-transfer warning was emitted. Free heap
after starting WAMR and the stress story was about 99.3KB.

## Interpretation

The display bus is no longer the only limiting factor: raising the CPU clock
improved the same scene while per-strip transfer time remained nearly constant.
The next performance work should reduce full-screen rendering and invalidation,
not merely raise SPI speed again. Component stories must report dirty area so
normal UI paths can be judged independently from this stress case.

This report covers the attached CoreS3 SE only. It is not evidence for a
T-Watch S3 configuration. A clean webcam artifact and touch/wake checks remain
part of the attached-hardware validation lane.

## Native component and queue validation

The later `m3e_lvgl` component build was flashed successfully. It boots WAMR on
a worker pthread, copies guest display requests into a 16-entry FreeRTOS UI
queue, and performs every LVGL mutation plus `display_update()` on the main UI
task. Serial output showed the guest text command and component-catalog command
arrive in order, no off-task rendering error, no queue overflow, and no DMA
overlap. Free heap after the resident guest, queue, and component catalog was
about 95.8KB.

The final flashed build also registers an LVGL pointer input backed by
`M5.Touch`. Physical 320px x-coordinates are translated through the 40px
viewport origin; touches in either host-owned gutter are released rather than
delivered to the app. Telemetry now reports touch press edges. The adapter
boots cleanly (`touch_presses=0` while unattended), but an actual press/drag
sequence still requires physical actuation and is not claimed here.

The static component screen painted six initial frames/21 strips and then
reported zero frames, flushes, and pixels in each following two-second window.
That confirms the catalog does not continually invalidate a stationary screen.

CleanCam retained the approved manual UVC settings (exposure 16 in 100µs units,
gain 58, automatic exposure off), but the Logitech stream stopped delivering
frames after competing AVFoundation/ffmpeg clients opened it. A USB
re-enumeration request succeeded without restoring frame delivery. No new
webcam artifact is claimed; the device result above is based on flash
verification and serial telemetry until the camera is physically power-cycled.

## Semantic AppSpec and event actor validation

The raw `display_text` proof was subsequently removed. The exact flashed
firmware now accepts only `doodad.ui_mount(pointer, length) -> i32`, with a
4096-byte canonical-CBOR ceiling. On the ESP32-S3 it decoded and mounted the
151-byte/three-node interactive Hello AppSpec, then delivered the
`say_hello` action through the bounded UI-to-runtime queue to the guest's
`handle_event` export. The guest returned a 56-byte canonical CommandBatch,
which the host copied, validated, and applied to the existing title and button
objects. Serial evidence:

```text
[guest] ui_mount: 151 bytes, 3 nodes
[host] app started; instance remains resident
[host] delivered action=say_hello node=hello.action commands=2
```

The desktop harness produces the event from an actual LVGL `CLICKED` callback.
The unattended hardware fixture injects the same already-validated semantic
envelope into the actor queue after boot, so it verifies the CoreS3
host→WAMR→semantic event path without claiming a physical touch that did not
occur. This verifies an in-place native patch rather than a second mount. The
live touch adapter remains attached to the rendered button for a future
physical press check.

The final build keeps the 128-entry state Store lazy and app-scoped. Immediate
steady-state telemetry reported 66,664 bytes free while the UI command was
still queued; the 60-second heartbeat reported 72,332 bytes after the owned
batch was applied and released. There were no queue overflows, guest traps,
DMA overlaps, or stationary redraws. After adding the broader component
framework and catalog, the exact flashed application image was `0xdeeb0`
bytes (913,072 bytes), leaving `0x21150` bytes (13%) in the 1MiB application
partition. The recovery-app runtime path retained the same 66,664-byte
immediate steady-state heap measurement because unused component code consumes
flash rather than live heap.
