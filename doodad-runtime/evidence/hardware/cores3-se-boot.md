# CoreS3 SE boot trace

Date: 2026-07-30

Port: `/dev/cu.usbmodem21101`

Target detected by M5GFX: `board_M5StackCoreS3SE`

This selected physical trace validates the desktop-first conformance runtime
on the connected board. It is not a long-duration power or performance claim.

## Observed boot facts

- ESP32-S3 revision 0.2, 240MHz
- 16MB SPI flash
- 8MB Quad PSRAM at 80MHz
- PSRAM boot memory test passed
- two 3MiB OTA firmware slots
- 0x9f0000-byte wear-levelled FAT package partition
- package filesystem mounted with 10,060KiB free of 10,060KiB
- no `/packages/active.wasm` present
- no microSD card present; timeout handled as an optional-source miss
- embedded recovery module size: 445 bytes
- WAMR module loaded and instantiated
- three-node, 151-byte AppSpec mounted
- resident guest reached steady state
- reported free 8-bit heap at steady state: 8,400,343 bytes
- initial active render: 7 frames, 25 flushes, 194,523 pixels
- settled display: zero frames and zero flushes in subsequent two-second
  reporting windows

## Relevant trace excerpt

```text
esp_psram: Found 8MB PSRAM device
esp_psram: SPI SRAM memory test OK
M5GFX: [Autodetect] board_M5StackCoreS3SE
[host] runtime pthread started
[host] WAMR ready (interpreter, stack=16384, heap=16384)
[host] onboard package storage: 10060 KiB free / 10060 KiB
[host] no activated onboard package at /packages/active.wasm
[host] microSD unavailable: ESP_ERR_TIMEOUT
[host] using embedded recovery app
[host] EMBEDDED app size: 445 bytes
[host] module loaded
[host] module instantiated
[guest] ui_mount: 151 bytes, 3 nodes
[host] app started; instance remains resident
[host] steady state; free heap: 8400343 bytes
[display] fps=0.0 frames=0 flushes=0 pixels=0
```
