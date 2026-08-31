# T-Watch Ultra shell target

ESP-IDF 5.5.5, ESP32-S3, 16 MiB flash, 8 MiB **quad** PSRAM, portrait 410×502.
Board dependencies live in `libs/moq-esp32/boards/twatch_ultra/components` from
the VoiceWatch workspace root. Pins and register-source revisions are locked
there; the older T-Watch S3 driver is not used.

The full native shell, WAMR recovery app and authenticated MoQ voice path run
on the connected Ultra. HTTPS bootstrap, WSS control, native QUIC, credential
renewal and real provider voice turns have hardware evidence. The board defaults
still leave voice disabled; enabling voice selects MoQ for the Ultra. Microphone
capture never starts at boot. Physical controls/apps/sleep-wake and final release
acceptance remain separate from a successful shell build or heartbeat. See the
workspace's `docs/moq-implementation-progress.md` for current evidence and limits.

Build from `doodad-runtime/firmware` with an isolated configuration:

The standard scripts also accept `--board t-watch-ultra`: `build-firmware.sh`,
`flash.sh` and `monitor.sh`. Ultra flashing uses the app-only verification runner
and waits for a one-minute shell heartbeat; it never uses full-layout flashing.

```sh
idf.py -B "$BUILD_DIR" -D SDKCONFIG="$PRIVATE_DIR/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;boards/t-watch-ultra/sdkconfig.defaults' build
```

Wi-Fi credentials belong in a private configuration, never in these defaults.
The generated image then contains credentials and must stay outside Git.

For a compile-only active MoQ path with populated public dummy Wi-Fi and personal
package settings, append `boards/t-watch-ultra/sdkconfig.moq-ci.defaults` to
`SDKCONFIG_DEFAULTS`, using a fresh build directory and SDKCONFIG. This exercises
the configured application path without reading a developer's private sdkconfig.
Do not flash the dummy profile. Keep diagnostic/stream-soak options off for the
normal release build and inspect the ELF to confirm that MoQ, rather than
`esp_peer`, supplies media.

`python3 ../tools/verify_moq_release_build.py "$BUILD_DIR" --public-ci` checks
this public profile, required linkage, certificate-time checking, size and the
absence of synthetic diagnostics. It reads SDKCONFIG from the build directory
and emits only build hashes/status, never its credential values. The root
`.github/workflows/moq-ultra-release.yml` runs this complete-shell compile lane;
its artifacts are compile-only and must not be flashed as a personal watch.

The partition CSV matches the inspected connected board: app0/app1 are 4 MiB,
NVS stays at 0x9000, OTA metadata at 0xe000 and existing `ffat` at 0x810000.
Package storage uses that existing label and never formats non-erased storage
after a failed mount. NVS recovery also never erases automatically. Do not use
generic `idf.py flash` for an app-only bring-up run: use the library's
`tools/run_ultra_transport.py`, which verifies live identity/security/layout
and writes app0 only. A one-minute shell smoke check can use
`--success-marker '[host] uptime heartbeat; free heap:' --timeout 150`.
Tests leave the new firmware installed; restoration is not required.

LVGL uses the complete physical display for the existing portrait system shell.
The portable guest ABI still describes its original 240×240 surface; guest
layout/transform parity and all navigation surfaces require separate validation.
LVGL draw buffers use PSRAM and the board copies bounded RGB565 strips into a
6,560-byte internal DMA buffer. The fixed LVGL heap also uses PSRAM through its
supported `LV_ATTRIBUTE_LARGE_RAM_ARRAY`, applied without editing managed code.
The first baseline exposed a 128 KiB internal heap placement problem; verify
the corrected pool's ELF address and repeat hardware memory measurement.

The physical BSP validates CST9220 identity/resolution and DRV2605L identity,
polls touch/power/battery and debounces GPIO0. A fresh button press is emitted
once to the existing shell action API; it is not repeated while held. Sleep
brightness/wake-touch suppression is implemented, but physical touch mapping,
colors, wake behavior, tactile effects and explicit PTT still need observation.
The MoQ audio owner must use `board_ultra.hpp`'s borrowed board handle and the
timestamped/fenced BSP methods; it must not open a second I2C/I2S owner.

Media ownership, diagnostic scope and remaining integration work are documented
in the workspace root at `docs/moq-shell-media-seam.md`.
