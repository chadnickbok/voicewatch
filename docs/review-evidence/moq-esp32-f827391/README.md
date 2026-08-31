# Review evidence — moq-esp32 f827391

These artifacts support `../../moq-integration-readiness.md`. No production source was changed and no firmware was flashed.

- `configured-build-result.txt`: active handshake linker errors from ESP-IDF 5.5.5, using nonempty dummy JWT/SSID/password. The empty configuration builds because the handshake is optimized away. Full local build output is in `/tmp/voicewatch-moq-review-f827391/`.
- `backend_repros.c`: exact relevant functions extracted from the reviewed adapter, with mocked ngtcp2/socket boundaries to isolate two adapter defects. This intentionally confirms existing failures; it is not a production regression suite or network interop test.
- `backend-repros.txt`: observed results from the plain host compilation.
- `core_review_repros.c` and `core-repros.txt`: independently identified and rerun portable-core failures using the actual library and existing mock transport: rejected duplicate start discards pending SETUP, and `UINT64_MAX` optional group bounds wrap to absent.

Reproduce the adapter observations from the VoiceWatch root:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I libs/moq-esp32/components/esp_moq/include \
  -I libs/moq-esp32/components/esp_moq_transport_ngtcp2/include \
  docs/review-evidence/moq-esp32-f827391/backend_repros.c \
  -o /tmp/moq-backend-repros
/tmp/moq-backend-repros
```

Expected observations are a rejected reply on peer bidirectional stream 1 and repeated attempts on blocked stream 2 without attempting writable stream 6. A sanitizer build of this isolated harness stalled on the review host; it was terminated and is not counted as passing.

Reproduce the core observations from the VoiceWatch root:

```sh
cc -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Wshadow \
  -Wstrict-prototypes -pedantic \
  -I libs/moq-esp32 -I libs/moq-esp32/components/esp_moq/include \
  libs/moq-esp32/components/esp_moq/src/*.c \
  docs/review-evidence/moq-esp32-f827391/core_review_repros.c \
  -o /tmp/moq-core-review-repros
/tmp/moq-core-review-repros
```
