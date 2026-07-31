# Media Remote

Deterministic interactive conformance package.

Screens:

- package launch screen
- NOW PLAYING: Midnight City
- PAUSED: 1:42
- CONNECTION LOST: Last at 1:42
- RECONCILED: Playing / 1:45

Every transition crosses the domain-scoped mocked media capability before mounting the next bounded AppSpec.

## Package artwork

Media is the first Doodad multimedia fixture. Its original 96×64 synthwave
artwork is encoded as a content-addressed `DIMG` package resource containing a
12-byte header followed by RGB565 little-endian pixels. The manifest declares
the exact hash, dimensions, encoded size, and decoded memory cost. Package
staging verifies all of those fields before copying the asset.

Wear Compose and LVGL decode the same bytes independently. The accepted
disconnect flow deliberately requests an unknown all-zero hash so both
renderers also exercise the deterministic missing-image fallback. Regenerate
and verify the asset with:

```sh
PYTHONPATH=tools python3 tools/generate_media_asset.py
PYTHONPATH=tools python3 tools/generate_media_asset.py --check
```
