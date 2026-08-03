from __future__ import annotations

import json
import tempfile
import unittest
import urllib.parse
from datetime import UTC, datetime, timedelta
from pathlib import Path

from tools.weather_provider.open_meteo import (
    OpenMeteoClient,
    ResolvedLocation,
    WeatherCache,
    WeatherProvider,
    WeatherProviderError,
    normalize_forecast,
    wmo_condition,
)


def forecast_body() -> dict:
    return {
        "timezone": "America/Los_Angeles",
        "current": {
            "time": "2026-08-01T10:09",
            "temperature_2m": 62.0,
            "relative_humidity_2m": 49,
            "apparent_temperature": 59.0,
            "precipitation_probability": 12,
            "weather_code": 2,
            "wind_speed_10m": 8.0,
            "wind_direction_10m": 270,
            "is_day": 1,
        },
        "hourly": {
            "time": [f"2026-08-01T{hour:02d}:00" for hour in range(10, 17)],
            "temperature_2m": [62, 63, 65, 66, 67, 66, 64],
            "precipitation_probability": [10, 20, 30, 40, 25, 10, 0],
            "weather_code": [2, 2, 0, 0, 1, 2, 3],
            "uv_index": [3.2, 4, 5, 4, 3, 2, 1],
        },
        "minutely_15": {
            "time": [
                "2026-08-01T10:15",
                "2026-08-01T10:30",
                "2026-08-01T10:45",
                "2026-08-01T11:00",
                "2026-08-01T11:15",
            ],
            "precipitation_probability": [0, 30, 90, 60, 0],
            "precipitation": [0, 0, 0.05, 0.1, 0],
        },
        "daily": {
            "time": ["2026-08-01", "2026-08-02", "2026-08-03", "2026-08-04"],
            "weather_code": [2, 1, 61, 3],
            "temperature_2m_max": [67, 65, 63, 64],
            "temperature_2m_min": [54, 53, 51, 52],
            "precipitation_probability_max": [20, 10, 70, 25],
            "sunrise": [
                "2026-08-01T06:12",
                "2026-08-02T06:13",
                "2026-08-03T06:14",
                "2026-08-04T06:15",
            ],
            "sunset": [
                "2026-08-01T20:05",
                "2026-08-02T20:04",
                "2026-08-03T20:03",
                "2026-08-04T20:02",
            ],
        },
    }


LOCATION = ResolvedLocation(
    name="San Francisco",
    latitude=37.7749,
    longitude=-122.4194,
    timezone="America/Los_Angeles",
    country_code="US",
    admin1="California",
)


class OpenMeteoProviderTests(unittest.TestCase):
    def test_client_resolves_and_requests_bounded_forecast_fields(self) -> None:
        urls: list[str] = []

        def get_json(url: str, timeout: float) -> dict:
            self.assertEqual(timeout, 3.0)
            urls.append(url)
            if "geocoding-api" in url:
                return {
                    "results": [
                        {
                            "name": "San Francisco",
                            "latitude": 37.7749,
                            "longitude": -122.4194,
                            "timezone": "America/Los_Angeles",
                            "country_code": "US",
                            "admin1": "California",
                        }
                    ]
                }
            return forecast_body()

        client = OpenMeteoClient(get_json=get_json, timeout_seconds=3.0)
        location = client.resolve("San Francisco")
        body = client.forecast(location, units="imperial")
        self.assertEqual(body["timezone"], "America/Los_Angeles")
        geocode_query = urllib.parse.parse_qs(urllib.parse.urlsplit(urls[0]).query)
        forecast_query = urllib.parse.parse_qs(urllib.parse.urlsplit(urls[1]).query)
        self.assertEqual(geocode_query["name"], ["San Francisco"])
        self.assertEqual(forecast_query["forecast_hours"], ["7"])
        self.assertEqual(forecast_query["forecast_days"], ["4"])
        self.assertEqual(forecast_query["temperature_unit"], ["fahrenheit"])
        self.assertIn("uv_index", forecast_query["hourly"][0])
        self.assertIn("sunrise", forecast_query["daily"][0])

    def test_normalizes_real_shape_into_bounded_provider_v2(self) -> None:
        fetched_at = datetime(2026, 8, 1, 17, 9, tzinfo=UTC)
        source = normalize_forecast(
            forecast_body(), LOCATION, units="imperial", fetched_at=fetched_at
        )
        self.assertEqual(source["location"], "San Francisco")
        self.assertEqual(source["local_weekday"], 6)
        self.assertEqual(source["local_minute"], 609)
        self.assertEqual(source["current"]["condition"], 2)
        self.assertEqual(source["current"]["temperature_tenths"], 620)
        self.assertEqual(source["current"]["uv_index_tenths"], 32)
        self.assertEqual(source["current"]["sunrise_local_minute"], 372)
        self.assertEqual(source["hours"][3]["temperature_tenths"], 660)
        self.assertEqual(source["days"][1]["weekday"], 0)
        self.assertEqual(source["days"][2]["condition"], 8)
        self.assertEqual(len(source["precipitation"]), 13)
        self.assertEqual(source["minutes_until_rain"], 25)
        self.assertGreater(source["rain_duration_minutes"], 0)
        self.assertEqual(source["units"], 1)

    def test_condition_mapping_preserves_day_night_and_extremes(self) -> None:
        self.assertEqual(
            wmo_condition(0, is_day=True, temperature_tenths=700, units="imperial", wind_speed_tenths=0),
            0,
        )
        self.assertEqual(
            wmo_condition(0, is_day=False, temperature_tenths=700, units="imperial", wind_speed_tenths=0),
            1,
        )
        self.assertEqual(
            wmo_condition(0, is_day=True, temperature_tenths=1040, units="imperial", wind_speed_tenths=0),
            14,
        )
        self.assertEqual(
            wmo_condition(99, is_day=True, temperature_tenths=700, units="imperial", wind_speed_tenths=0),
            10,
        )
        self.assertEqual(
            wmo_condition(67, is_day=True, temperature_tenths=320, units="metric", wind_speed_tenths=0),
            12,
        )
        self.assertEqual(
            wmo_condition(1, is_day=True, temperature_tenths=700, units="imperial", wind_speed_tenths=420),
            13,
        )

    def test_metric_values_and_timezone_local_clock_remain_host_normalized(self) -> None:
        body = forecast_body()
        body["timezone"] = "Europe/Paris"
        body["current"]["time"] = "2026-08-01T21:30"
        body["current"]["temperature_2m"] = 16.7
        body["current"]["apparent_temperature"] = 15.9
        body["current"]["wind_speed_10m"] = 12.4
        body["current"]["wind_direction_10m"] = 360
        body["current"]["weather_code"] = 0
        body["current"]["is_day"] = 0
        location = ResolvedLocation(
            "Paris", 48.8566, 2.3522, "Europe/Paris", "FR"
        )
        source = normalize_forecast(
            body,
            location,
            units="metric",
            fetched_at=datetime(2026, 8, 1, 19, 30, tzinfo=UTC),
        )
        self.assertEqual(source["units"], 0)
        self.assertEqual(source["local_minute"], 21 * 60 + 30)
        self.assertEqual(source["current"]["temperature_tenths"], 167)
        self.assertEqual(source["current"]["wind_speed_tenths"], 124)
        self.assertEqual(source["current"]["wind_direction_degrees"], 0)
        self.assertEqual(source["current"]["condition"], 1)

    def test_cache_age_stale_and_offline_freshness(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = WeatherCache(Path(temporary) / "weather.json")
            fetched = datetime(2026, 8, 1, 17, 9, tzinfo=UTC)
            source = normalize_forecast(
                forecast_body(), LOCATION, units="imperial", fetched_at=fetched
            )
            cache.save(source, fetched)
            provider = WeatherProvider(OpenMeteoClient(), cache)
            current = provider.cached(now=fetched + timedelta(minutes=4))
            stale = provider.cached(now=fetched + timedelta(minutes=35))
            offline = provider.cached(
                now=fetched + timedelta(minutes=9), offline=True
            )
            self.assertEqual((current.freshness, current.source["cache_age_minutes"]), (0, 4))
            self.assertEqual((stale.freshness, stale.source["cache_age_minutes"]), (1, 35))
            self.assertEqual((offline.freshness, offline.source["cache_age_minutes"]), (2, 9))

    def test_refresh_failure_preserves_last_good_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = WeatherCache(Path(temporary) / "weather.json")
            fetched = datetime(2026, 8, 1, 17, 9, tzinfo=UTC)
            source = normalize_forecast(
                forecast_body(), LOCATION, units="imperial", fetched_at=fetched
            )
            cache.save(source, fetched)

            def fail(_url: str, _timeout: float) -> dict:
                raise WeatherProviderError("simulated disconnect")

            provider = WeatherProvider(OpenMeteoClient(get_json=fail), cache)
            result = provider.refresh(
                city="San Francisco",
                units="imperial",
                now=fetched + timedelta(minutes=18),
            )
            self.assertTrue(result.from_cache)
            self.assertEqual(result.freshness, 2)
            self.assertEqual(result.source["cache_age_minutes"], 18)
            self.assertIn("simulated disconnect", result.error or "")
            self.assertLessEqual(len(result.cbor), 512)

    def test_cache_rejects_corrupt_document(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "weather.json"
            path.write_text(json.dumps({"schema": "wrong"}), encoding="utf-8")
            with self.assertRaisesRegex(WeatherProviderError, "schema"):
                WeatherCache(path).load(now=datetime.now(UTC), freshness=0)


if __name__ == "__main__":
    unittest.main()
