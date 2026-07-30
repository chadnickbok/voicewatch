# Wear token synchronization

`sync.py` extracts the core theme vocabulary from the frozen AndroidX Wear
Compose Material 3 source. It records every discovered Kotlin token file and
its Git blob ID, then byte-verifies the eight core files it mechanically
extracts.

Refresh intentionally requires network access:

```sh
python3 tools/token_sync/sync.py --refresh
```

Normal builds and CI use the offline deterministic check:

```sh
python3 tools/token_sync/sync.py --check
```

The normalized JSON is the canonical generator input. The checked-in C++
header is generated from it and must not be edited by hand. To regenerate only
that header without network access, use `--generate`.
