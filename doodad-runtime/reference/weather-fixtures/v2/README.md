# Weather snapshot v2 fixtures

These deterministic fixtures exercise the bounded `weather.snapshot.v2`
provider payload described by `contracts/weather-snapshot-v2.cddl`.

- `baseline`: the approved Current Conditions concept data.
- `rain`: the imminent-rain screen and nonzero minutely chart.
- `extreme`: three-digit temperature and high-UV layout pressure.
- `stale`: retained last-good data with an explicit stale age.
- `error`: retained last-good data while the envelope reports an error.

Run `python3 tools/weather_snapshot/generate.py` after editing a source. The
generated manifest records payload sizes and hashes; every payload must remain
at or below the provider envelope's 512-byte limit.
