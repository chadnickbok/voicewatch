# T-Watch personal-app provisioning proof

Captured on 2026-08-05 from T-Watch S3 `a0:f2:62:e1:1e:18` at
`/dev/cu.usbmodem22301`. The final clean firmware reports version `074f34a`.

## Provisioned profile

- owner: `local.nick`
- signer label: `personal-v1`
- signaling endpoint: `ws://192.168.1.95:8765/ws`
- HMAC key: generated locally and stored in macOS Keychain under service
  `voicewatch.doodad.personal.hmac`; the key is not present in this evidence
  or the repository

## Operations completed

1. Erased only the package partition at `0x610000` for `0x9f0000` bytes.
2. Built signed-profile T-Watch firmware.
3. Flashed bootloader, partition table, OTA data, and application image with
   hash verification.
4. Validated the live-agent configuration with personal delivery enabled.
5. Started the live agent and captured a hard-reset production boot.

The hardware run found and fixed two provisioning issues before the final
capture: esptool v4 requires `erase_region`, and sdkconfig refresh must restore
ignored local Wi-Fi/trust values. It also moved the large launcher catalog
buffers to PSRAM, leaving enough internal DMA memory for all five Wi-Fi RX
buffers and the 4096-byte audio reservation.

## Final proof

The accompanying production-boot capture shows:

- empty DDR3 package storage mounted with 10052 KiB free;
- all five configured static Wi-Fi RX buffers allocated;
- Wi-Fi station initialization and address assignment;
- 4096-byte audio DMA reservation with 20791 bytes still free;
- signaling discovery at the configured live agent;
- successful DTLS/SRTP handshake and voice peer state 7;
- at least 22460 bytes of minimum observed internal free memory.

See [production-boot/report.md](production-boot/report.md),
[production-boot/telemetry.json](production-boot/telemetry.json), and the raw
[production-boot/serial.log](production-boot/serial.log).

The final spoken hydration-tracker generation remains a manual interaction:
press Voice on the watch and say, “Build me a hydration tracker with a blue
water-drop identity.”
