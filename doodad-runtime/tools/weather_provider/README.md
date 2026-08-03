# Host-owned Weather provider

This package converts real Open-Meteo geocoding and forecast responses into
the bounded `weather.snapshot.v2` payload consumed by the Weather Wasm guest.
The guest never receives network, location, cache, or credential access.

Implemented host responsibilities:

- city-name geocoding with a manually configured fallback city;
- explicit metric/imperial API requests;
- local-time and weekday normalization using the resolved IANA timezone;
- WMO-to-Doodad condition mapping, including day/night variants;
- current, seven-hour, four-day, UV, sunrise/sunset, and precipitation data;
- 15-minute probability resampling into the bounded 13×5-minute chart;
- atomic persisted last-good cache with current, stale, and offline delivery;
- canonical CBOR encoding through the same provider-v2 schema as firmware;
- optional delivery to the real native host, Weather Wasm guest, and LVGL
  renderer for live-data screenshots and semantic snapshots.

Run a live four-route simulator capture:

```sh
python3 tools/fetch_weather.py \
  --city "San Francisco" \
  --units imperial \
  --render \
  --output target/weather-provider/live-sf
```

Replay the persisted last-good snapshot without network access:

```sh
python3 tools/fetch_weather.py \
  --offline \
  --render \
  --cache target/weather-provider/cache.json \
  --output target/weather-provider/offline-sf
```

`snapshot.json` is the human-reviewable normalized record, `snapshot.cbor` is
the exact Wasm payload, and the route PNG/scene/semantics files prove delivery
through the production guest and LVGL renderer. Live outputs remain under
`target/`; checked visual goldens continue to use deterministic fixtures.

The adapter follows the official [Open-Meteo Forecast API](https://open-meteo.com/en/docs)
and [Geocoding API](https://open-meteo.com/en/docs/geocoding-api). The matching
ESP-IDF Wi-Fi/HTTPS/NVS adapter lives in `firmware/main/src/weather_provider.cpp`;
this Python adapter remains the executable desktop/reference host.
