"""Open-Meteo adapter for the bounded Doodad Weather provider-v2 payload.

Networking, geocoding, units, time-zone interpretation, and persistence are
host concerns. The Wasm guest receives only the normalized snapshot described
by ``contracts/weather-snapshot-v2.cddl``.
"""

from __future__ import annotations

import hashlib
import json
import math
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Callable

from tools.weather_snapshot.generate import encode, payload


GEOCODING_ENDPOINT = "https://geocoding-api.open-meteo.com/v1/search"
FORECAST_ENDPOINT = "https://api.open-meteo.com/v1/forecast"
CACHE_SCHEMA = "doodad.weather-cache.v1"
USER_AGENT = "DoodadWeatherReference/1.0"


class WeatherProviderError(RuntimeError):
    """A remote, normalization, or cache error safe to show at the host edge."""


@dataclass(frozen=True)
class ResolvedLocation:
    name: str
    latitude: float
    longitude: float
    timezone: str
    country_code: str = ""
    admin1: str = ""

    @property
    def display_name(self) -> str:
        # A watch needs the recognizable locality, not a verbose postal label.
        return bounded_utf8(self.name, 48)


@dataclass(frozen=True)
class ProviderResult:
    source: dict[str, Any]
    freshness: int
    from_cache: bool
    fetched_at: datetime
    error: str | None = None

    @property
    def cbor(self) -> bytes:
        encoded = encode(payload(self.source))
        if len(encoded) > 512:
            raise WeatherProviderError(
                f"normalized Weather payload is {len(encoded)} bytes"
            )
        return encoded


JsonGetter = Callable[[str, float], dict[str, Any]]


def bounded_utf8(value: str, maximum: int) -> str:
    data = value.strip().encode("utf-8")
    if not data:
        raise WeatherProviderError("display location is empty")
    if len(data) <= maximum:
        return data.decode("utf-8")
    shortened = data[:maximum]
    while shortened:
        try:
            return shortened.decode("utf-8").rstrip()
        except UnicodeDecodeError:
            shortened = shortened[:-1]
    raise WeatherProviderError("display location has no bounded UTF-8 prefix")


def _default_get_json(url: str, timeout: float) -> dict[str, Any]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            if response.status != 200:
                raise WeatherProviderError(
                    f"Open-Meteo returned HTTP {response.status}"
                )
            decoded = json.load(response)
    except (OSError, ValueError) as error:
        raise WeatherProviderError(f"Open-Meteo request failed: {error}") from error
    if not isinstance(decoded, dict):
        raise WeatherProviderError("Open-Meteo response is not an object")
    if decoded.get("error"):
        raise WeatherProviderError(str(decoded.get("reason", "Open-Meteo error")))
    return decoded


class OpenMeteoClient:
    def __init__(
        self,
        *,
        get_json: JsonGetter = _default_get_json,
        timeout_seconds: float = 10.0,
    ) -> None:
        self._get_json = get_json
        self._timeout = timeout_seconds

    def resolve(self, query: str) -> ResolvedLocation:
        query = query.strip()
        if len(query) < 2:
            raise WeatherProviderError("city query must contain at least 2 characters")
        url = GEOCODING_ENDPOINT + "?" + urllib.parse.urlencode(
            {"name": query, "count": 5, "language": "en", "format": "json"}
        )
        body = self._get_json(url, self._timeout)
        results = body.get("results")
        if not isinstance(results, list) or not results:
            raise WeatherProviderError(f"no location matched {query!r}")
        candidate = results[0]
        if not isinstance(candidate, dict):
            raise WeatherProviderError("geocoding result is malformed")
        try:
            return ResolvedLocation(
                name=str(candidate["name"]),
                latitude=float(candidate["latitude"]),
                longitude=float(candidate["longitude"]),
                timezone=str(candidate["timezone"]),
                country_code=str(candidate.get("country_code", "")),
                admin1=str(candidate.get("admin1", "")),
            )
        except (KeyError, TypeError, ValueError) as error:
            raise WeatherProviderError("geocoding result is incomplete") from error

    def forecast(
        self,
        location: ResolvedLocation,
        *,
        units: str,
    ) -> dict[str, Any]:
        if units not in {"metric", "imperial"}:
            raise ValueError("units must be 'metric' or 'imperial'")
        parameters = {
            "latitude": f"{location.latitude:.6f}",
            "longitude": f"{location.longitude:.6f}",
            "timezone": location.timezone or "auto",
            "forecast_hours": 7,
            "forecast_minutely_15": 5,
            "forecast_days": 4,
            "current": (
                "temperature_2m,relative_humidity_2m,apparent_temperature,"
                "precipitation_probability,weather_code,wind_speed_10m,"
                "wind_direction_10m,is_day"
            ),
            "hourly": (
                "temperature_2m,precipitation_probability,weather_code,uv_index"
            ),
            "minutely_15": "precipitation_probability,precipitation",
            "daily": (
                "weather_code,temperature_2m_max,temperature_2m_min,"
                "precipitation_probability_max,sunrise,sunset"
            ),
        }
        if units == "imperial":
            parameters.update(
                {
                    "temperature_unit": "fahrenheit",
                    "wind_speed_unit": "mph",
                    "precipitation_unit": "inch",
                }
            )
        url = FORECAST_ENDPOINT + "?" + urllib.parse.urlencode(parameters)
        return self._get_json(url, self._timeout)


def _number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise WeatherProviderError(f"{field} is not numeric")
    result = float(value)
    if not math.isfinite(result):
        raise WeatherProviderError(f"{field} is not finite")
    return result


def _integer(value: Any, minimum: int, maximum: int, field: str) -> int:
    result = round(_number(value, field))
    if not minimum <= result <= maximum:
        raise WeatherProviderError(f"{field} is outside {minimum}..{maximum}")
    return result


def _series(section: dict[str, Any], name: str, minimum: int) -> list[Any]:
    value = section.get(name)
    if not isinstance(value, list) or len(value) < minimum:
        raise WeatherProviderError(f"{name} must contain at least {minimum} values")
    return value


def _local_time(value: Any, field: str) -> datetime:
    if not isinstance(value, str):
        raise WeatherProviderError(f"{field} is not an ISO local timestamp")
    try:
        result = datetime.fromisoformat(value)
    except ValueError as error:
        raise WeatherProviderError(f"{field} is not an ISO local timestamp") from error
    return result


def _minute(value: Any, field: str) -> int:
    parsed = _local_time(value, field)
    return parsed.hour * 60 + parsed.minute


def _tenths(value: Any, field: str) -> int:
    return round(_number(value, field) * 10)


def _optional_percent(value: Any, field: str) -> int | None:
    if value is None:
        return None
    return _integer(value, 0, 100, field)


def wmo_condition(
    code: int,
    *,
    is_day: bool,
    temperature_tenths: int,
    units: str,
    wind_speed_tenths: int,
) -> int:
    """Map WMO interpretation codes onto Doodad's bounded icon vocabulary."""
    hot_threshold = 1000 if units == "imperial" else 380
    if temperature_tenths >= hot_threshold and code <= 3:
        return 14
    if wind_speed_tenths >= (400 if units == "imperial" else 640) and code <= 3:
        return 13
    if code == 0:
        return 0 if is_day else 1
    if code in {1, 2}:
        return 2 if is_day else 3
    if code == 3:
        return 5
    if code in {45, 48}:
        return 6
    if code in {51, 53, 55}:
        return 7
    if code in {56, 57, 66, 67}:
        return 12
    if code in {61, 63, 80, 81}:
        return 8
    if code in {65, 82}:
        return 9
    if code in {71, 73, 75, 77, 85, 86}:
        return 11
    if code in {95, 96, 99}:
        return 10
    return 15


def _resample_fifteen_to_five(values: list[Any]) -> list[int]:
    if len(values) < 5:
        raise WeatherProviderError("15-minute precipitation needs 5 values")
    points = [_integer(value, 0, 100, "minutely precipitation") for value in values[:5]]
    output: list[int] = []
    for index in range(4):
        start = points[index]
        end = points[index + 1]
        output.extend(
            round(start + (end - start) * fraction / 3)
            for fraction in range(3)
        )
    output.append(points[-1])
    return output


def _rain_window(probabilities: list[int], amounts: list[Any]) -> tuple[int, int]:
    # Amount data is 15-minute accumulation. Expand it across the three 5-minute
    # buckets represented by that model step and combine it with probability.
    expanded_amounts: list[float] = []
    for value in amounts[:4]:
        expanded_amounts.extend([_number(value, "minutely precipitation")] * 3)
    expanded_amounts.append(
        _number(amounts[4], "minutely precipitation") if len(amounts) >= 5 else 0.0
    )
    raining = [
        amount > 0.01 or probability >= 60
        for amount, probability in zip(expanded_amounts, probabilities)
    ]
    try:
        first = raining.index(True)
    except ValueError:
        return -1, 0
    final = first
    while final + 1 < len(raining) and raining[final + 1]:
        final += 1
    return first * 5, (final - first + 1) * 5


def normalize_forecast(
    body: dict[str, Any],
    location: ResolvedLocation,
    *,
    units: str,
    fetched_at: datetime,
) -> dict[str, Any]:
    if units not in {"metric", "imperial"}:
        raise ValueError("units must be 'metric' or 'imperial'")
    try:
        current = body["current"]
        hourly = body["hourly"]
        minutely = body["minutely_15"]
        daily = body["daily"]
    except KeyError as error:
        raise WeatherProviderError(f"forecast omitted {error.args[0]}") from error
    if not all(isinstance(value, dict) for value in (current, hourly, minutely, daily)):
        raise WeatherProviderError("forecast sections must be objects")

    current_time = _local_time(current.get("time"), "current.time")
    current_temperature = _tenths(current.get("temperature_2m"), "current.temperature")
    wind_speed = _tenths(current.get("wind_speed_10m"), "current.wind_speed")
    is_day = bool(_integer(current.get("is_day"), 0, 1, "current.is_day"))
    current_wmo = _integer(current.get("weather_code"), 0, 99, "current.weather_code")

    hourly_times = _series(hourly, "time", 7)
    hourly_temperatures = _series(hourly, "temperature_2m", 7)
    hourly_probabilities = _series(hourly, "precipitation_probability", 7)
    hourly_codes = _series(hourly, "weather_code", 7)
    hourly_uv = _series(hourly, "uv_index", 1)
    day_times = _series(daily, "time", 4)
    day_highs = _series(daily, "temperature_2m_max", 4)
    day_lows = _series(daily, "temperature_2m_min", 4)
    day_probabilities = _series(daily, "precipitation_probability_max", 4)
    day_codes = _series(daily, "weather_code", 4)
    sunrises = _series(daily, "sunrise", 4)
    sunsets = _series(daily, "sunset", 4)
    minute_probabilities = _series(minutely, "precipitation_probability", 5)
    minute_amounts = _series(minutely, "precipitation", 5)

    rain = _resample_fifteen_to_five(minute_probabilities)
    minutes_until_rain, rain_duration = _rain_window(rain, minute_amounts)

    daylight_by_date: dict[str, tuple[int, int]] = {}
    for index in range(4):
        date = _local_time(day_times[index], f"daily.time[{index}]")
        daylight_by_date[date.date().isoformat()] = (
            _minute(sunrises[index], f"daily.sunrise[{index}]"),
            _minute(sunsets[index], f"daily.sunset[{index}]"),
        )

    hours: list[dict[str, Any]] = []
    for index in range(7):
        local = _local_time(hourly_times[index], f"hourly.time[{index}]")
        temperature = _tenths(
            hourly_temperatures[index], f"hourly.temperature[{index}]"
        )
        code = _integer(hourly_codes[index], 0, 99, f"hourly.code[{index}]")
        local_minute = local.hour * 60 + local.minute
        sunrise, sunset = daylight_by_date.get(
            local.date().isoformat(), (6 * 60, 19 * 60)
        )
        daylight = sunrise <= local_minute < sunset
        hours.append(
            {
                "local_minute": local_minute,
                "temperature_tenths": temperature,
                "precipitation_percent": _optional_percent(
                    hourly_probabilities[index], f"hourly.precipitation[{index}]"
                ),
                "condition": wmo_condition(
                    code,
                    is_day=daylight,
                    temperature_tenths=temperature,
                    units=units,
                    wind_speed_tenths=wind_speed,
                ),
            }
        )

    days: list[dict[str, Any]] = []
    for index in range(4):
        date = _local_time(day_times[index], f"daily.time[{index}]")
        high = _tenths(day_highs[index], f"daily.high[{index}]")
        low = _tenths(day_lows[index], f"daily.low[{index}]")
        code = _integer(day_codes[index], 0, 99, f"daily.code[{index}]")
        days.append(
            {
                "weekday": date.isoweekday() % 7,
                "low_tenths": low,
                "high_tenths": high,
                "precipitation_percent": _optional_percent(
                    day_probabilities[index], f"daily.precipitation[{index}]"
                ),
                "condition": wmo_condition(
                    code,
                    is_day=True,
                    temperature_tenths=high,
                    units=units,
                    wind_speed_tenths=wind_speed,
                ),
            }
        )

    canonical_response = json.dumps(
        body, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    revision = int.from_bytes(hashlib.sha256(canonical_response).digest()[:8], "big")
    source: dict[str, Any] = {
        "freshness": 0,
        "location": location.display_name,
        "local_weekday": current_time.isoweekday() % 7,
        "local_minute": current_time.hour * 60 + current_time.minute,
        "current": {
            "temperature_tenths": current_temperature,
            "feels_like_tenths": _tenths(
                current.get("apparent_temperature"), "current.apparent_temperature"
            ),
            "condition": wmo_condition(
                current_wmo,
                is_day=is_day,
                temperature_tenths=current_temperature,
                units=units,
                wind_speed_tenths=wind_speed,
            ),
            "high_tenths": days[0]["high_tenths"],
            "low_tenths": days[0]["low_tenths"],
            "precipitation_percent": _optional_percent(
                current.get("precipitation_probability"),
                "current.precipitation_probability",
            ),
            "humidity_percent": _optional_percent(
                current.get("relative_humidity_2m"), "current.humidity"
            ),
            "wind_speed_tenths": wind_speed,
            "wind_direction_degrees": _integer(
                current.get("wind_direction_10m"), 0, 360, "current.wind_direction"
            ) % 360,
            "uv_index_tenths": max(0, _tenths(hourly_uv[0], "hourly.uv_index[0]")),
            "sunrise_local_minute": _minute(sunrises[0], "daily.sunrise[0]"),
            "sunset_local_minute": _minute(sunsets[0], "daily.sunset[0]"),
        },
        "hours": hours,
        "days": days,
        "precipitation": rain,
        "minutes_until_rain": minutes_until_rain,
        "rain_duration_minutes": rain_duration,
        "units": 1 if units == "imperial" else 0,
        "data_revision": revision,
        "cache_age_minutes": 0,
        "source_timestamp": fetched_at.astimezone(UTC).isoformat(),
        "source_timezone": str(body.get("timezone", location.timezone)),
    }
    # The extra source metadata is useful in replay JSON but intentionally not
    # encoded into the bounded Wasm payload.
    payload(source)
    return source


class WeatherCache:
    def __init__(self, path: Path) -> None:
        self.path = path

    def save(self, source: dict[str, Any], fetched_at: datetime) -> None:
        payload(source)
        document = {
            "schema": CACHE_SCHEMA,
            "fetched_at": fetched_at.astimezone(UTC).isoformat(),
            "source": source,
        }
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(self.path.suffix + ".tmp")
        temporary.write_text(
            json.dumps(document, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        temporary.replace(self.path)

    def load(self, *, now: datetime, freshness: int) -> ProviderResult:
        try:
            document = json.loads(self.path.read_text(encoding="utf-8"))
            if document.get("schema") != CACHE_SCHEMA:
                raise WeatherProviderError("Weather cache schema is unsupported")
            fetched_at = datetime.fromisoformat(document["fetched_at"])
            source = dict(document["source"])
        except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            raise WeatherProviderError(f"Weather cache is unavailable: {error}") from error
        if fetched_at.tzinfo is None:
            raise WeatherProviderError("Weather cache timestamp has no timezone")
        age_seconds = max(0.0, (now.astimezone(UTC) - fetched_at.astimezone(UTC)).total_seconds())
        source["freshness"] = freshness
        source["cache_age_minutes"] = int(age_seconds // 60)
        payload(source)
        return ProviderResult(
            source=source,
            freshness=freshness,
            from_cache=True,
            fetched_at=fetched_at,
        )


class WeatherProvider:
    def __init__(self, client: OpenMeteoClient, cache: WeatherCache) -> None:
        self.client = client
        self.cache = cache

    def refresh(
        self,
        *,
        city: str,
        units: str,
        now: datetime | None = None,
    ) -> ProviderResult:
        observed = (now or datetime.now(UTC)).astimezone(UTC)
        try:
            location = self.client.resolve(city)
            body = self.client.forecast(location, units=units)
            source = normalize_forecast(
                body, location, units=units, fetched_at=observed
            )
            self.cache.save(source, observed)
            return ProviderResult(source, 0, False, observed)
        except WeatherProviderError as error:
            try:
                cached = self.cache.load(now=observed, freshness=2)
            except WeatherProviderError:
                raise error
            return ProviderResult(
                source=cached.source,
                freshness=2,
                from_cache=True,
                fetched_at=cached.fetched_at,
                error=str(error),
            )

    def cached(
        self,
        *,
        now: datetime | None = None,
        stale_after_minutes: int = 15,
        offline: bool = False,
    ) -> ProviderResult:
        observed = (now or datetime.now(UTC)).astimezone(UTC)
        preliminary = self.cache.load(now=observed, freshness=0)
        age = int(preliminary.source["cache_age_minutes"])
        freshness = 2 if offline else (1 if age > stale_after_minutes else 0)
        return self.cache.load(now=observed, freshness=freshness)
