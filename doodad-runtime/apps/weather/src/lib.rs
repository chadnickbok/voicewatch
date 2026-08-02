#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    CommandEncodeError, EventValue, Freshness, UiCommandBuffer, WeatherCondition, WeatherCurrentV2,
    WeatherDayV2, WeatherHourV2, WeatherProviderPayloadV2, WeatherUnits,
    decode_provider_event, decode_ui_event,
    decode_weather_provider_payload_v2, mount_appspec, pack_result,
    request_provider,
};

const APPSPEC: &[u8] = include_bytes!("../appspec.cbor");
const HOURLY: &[u8] = include_bytes!("../screens/hourly.cbor");
const DAILY: &[u8] = include_bytes!("../screens/daily.cbor");
const DETAILS: &[u8] = include_bytes!("../screens/details.cbor");
const RAIN: &[u8] = include_bytes!("../screens/rain.cbor");

struct Runtime {
    temperature: [u8; 16],
    status: [u8; 64],
    scratch: [u8; 64],
    commands: UiCommandBuffer<2048>,
    showing_rain: bool,
    page: u8,
    weather: WeatherState,
}

#[derive(Clone, Copy)]
struct WeatherState {
    valid: bool,
    freshness: Freshness,
    location: [u8; 64],
    location_length: u8,
    local_weekday: u8,
    current: WeatherCurrentV2,
    hours: [WeatherHourV2; 7],
    hour_count: u8,
    days: [WeatherDayV2; 4],
    day_count: u8,
    precipitation: [u8; 13],
    minutes_until_rain: Option<u16>,
    rain_duration_minutes: u16,
    units: WeatherUnits,
    data_revision: u64,
    cache_age_minutes: u64,
}

const EMPTY_HOUR: WeatherHourV2 = WeatherHourV2 {
    local_minute: 0,
    temperature_tenths: 0,
    precipitation_percent: None,
    condition: WeatherCondition::Unknown,
};
const EMPTY_DAY: WeatherDayV2 = WeatherDayV2 {
    weekday: 0,
    low_tenths: 0,
    high_tenths: 0,
    precipitation_percent: None,
    condition: WeatherCondition::Unknown,
};
const EMPTY_CURRENT: WeatherCurrentV2 = WeatherCurrentV2 {
    temperature_tenths: 0,
    feels_like_tenths: None,
    condition: WeatherCondition::Unknown,
    high_tenths: None,
    low_tenths: None,
    precipitation_percent: None,
    humidity_percent: None,
    wind_speed_tenths: None,
    wind_direction_degrees: None,
    uv_index_tenths: None,
    sunrise_local_minute: None,
    sunset_local_minute: None,
};

impl WeatherState {
    const fn empty() -> Self {
        Self {
            valid: false,
            freshness: Freshness::Current,
            location: [0; 64],
            location_length: 0,
            local_weekday: 0,
            current: EMPTY_CURRENT,
            hours: [EMPTY_HOUR; 7],
            hour_count: 0,
            days: [EMPTY_DAY; 4],
            day_count: 0,
            precipitation: [0; 13],
            minutes_until_rain: None,
            rain_duration_minutes: 0,
            units: WeatherUnits::Imperial,
            data_revision: 0,
            cache_age_minutes: 0,
        }
    }

    fn replace(&mut self, freshness: Freshness, source: WeatherProviderPayloadV2<'_>) {
        let location = source.location.as_bytes();
        let length = core::cmp::min(location.len(), self.location.len());
        self.location[..length].copy_from_slice(&location[..length]);
        self.location_length = length as u8;
        self.valid = true;
        self.freshness = freshness;
        self.local_weekday = source.local_weekday;
        self.current = source.current;
        self.hours = source.hours;
        self.hour_count = source.hour_count;
        self.days = source.days;
        self.day_count = source.day_count;
        self.precipitation = source.precipitation;
        self.minutes_until_rain = source.minutes_until_rain;
        self.rain_duration_minutes = source.rain_duration_minutes;
        self.units = source.units;
        self.data_revision = source.data_revision;
        self.cache_age_minutes = source.cache_age_minutes;
    }

    fn location(&self) -> &str {
        unsafe {
            core::str::from_utf8_unchecked(
                &self.location[..self.location_length as usize],
            )
        }
    }
}

impl Runtime {
    const fn new() -> Self {
        Self {
            temperature: [0; 16],
            status: [0; 64],
            scratch: [0; 64],
            commands: UiCommandBuffer::new(),
            showing_rain: false,
            page: 0,
            weather: WeatherState::empty(),
        }
    }

    fn mount_current(&mut self) -> u64 {
        self.page = 0;
        let _ = mount_appspec(APPSPEC);
        self.render_current()
    }

    fn mount_hourly(&mut self) -> u64 {
        self.page = 1;
        let _ = mount_appspec(HOURLY);
        self.render_hourly()
    }

    fn mount_daily(&mut self) -> u64 {
        self.page = 2;
        let _ = mount_appspec(DAILY);
        self.render_daily()
    }

    fn mount_details(&mut self) -> u64 {
        self.page = 3;
        let _ = mount_appspec(DETAILS);
        self.render_details()
    }

    fn mount_rain(&mut self) -> u64 {
        self.page = 4;
        let _ = mount_appspec(RAIN);
        self.render_rain()
    }

    fn render_active(&mut self) -> u64 {
        match self.page {
            0 => self.render_current(),
            1 => self.render_hourly(),
            2 => self.render_daily(),
            3 => self.render_details(),
            4 => self.render_rain(),
            _ => 0,
        }
    }

    fn navigate(&mut self, delta: i32) -> u64 {
        if delta == 0 {
            return 0;
        }
        match (self.page, delta.signum()) {
            (0, 1) => {
                if self.showing_rain {
                    self.mount_rain()
                } else {
                    self.mount_hourly()
                }
            }
            (1 | 4, -1) => self.mount_current(),
            (1 | 4, 1) => self.mount_daily(),
            (2, -1) => {
                if self.showing_rain {
                    self.mount_rain()
                } else {
                    self.mount_hourly()
                }
            }
            (2, 1) => self.mount_details(),
            (3, -1) => self.mount_daily(),
            _ => 0,
        }
    }

    fn loading(&mut self) -> u64 {
        if self.commands.begin(2).is_err()
            || self
                .commands
                .set_primary_text("weather.status", "Updating...")
                .is_err()
            || self
                .commands
                .set_primary_text("weather.symbol", "Updating weather")
                .is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }

    fn render_current(&mut self) -> u64 {
        let weather = self.weather;
        if !weather.valid {
            return 0;
        }
        let temperature = format_temperature(
            weather.current.temperature_tenths,
            &mut self.temperature,
        );
        let status = format_status(
            weather.freshness,
            weather.data_revision,
            weather.cache_age_minutes,
            &mut self.status,
        );
        if self.commands.begin(12).is_err()
            || self.commands.set_primary_text(
                "weather.location", weather.location()).is_err()
            || self
                .commands
                .set_primary_text("weather.summary", temperature)
                .is_err()
            || self
                .commands
                .set_primary_text(
                    "weather.symbol",
                    condition_label(weather.current.condition),
                )
                .is_err()
            || set_optional_temperature(
                &mut self.commands,
                "weather.high",
                "H ",
                weather.current.high_tenths,
                &mut self.scratch,
            ).is_err()
            || set_optional_temperature(
                &mut self.commands,
                "weather.low",
                "L ",
                weather.current.low_tenths,
                &mut self.scratch,
            ).is_err()
            || set_optional_temperature(
                &mut self.commands,
                "weather.feels",
                "",
                weather.current.feels_like_tenths,
                &mut self.scratch,
            ).is_err()
            || self
                .commands
                .set_primary_text("weather.status", status)
                .is_err()
            || self.commands.set_icon(
                "weather.condition-icon",
                condition_icon(weather.current.condition),
            ).is_err()
            || set_temperature_semantic(
                &mut self.commands,
                "weather.summary",
                weather.current.temperature_tenths,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_semantic_value(
                "weather.symbol",
                condition_label(weather.current.condition),
            ).is_err()
            || self.commands.set_semantic_label(
                "weather.condition-icon",
                condition_label(weather.current.condition),
            ).is_err()
            || set_status_semantic(
                &mut self.commands,
                "weather.status",
                weather.freshness,
                weather.cache_age_minutes,
                &mut self.scratch,
            ).is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }

    fn render_hourly(&mut self) -> u64 {
        let weather = self.weather;
        if !weather.valid || weather.hour_count < 4 {
            return 0;
        }
        let status = format_status(
            weather.freshness,
            weather.data_revision,
            weather.cache_age_minutes,
            &mut self.status,
        );
        let mut rain = [0_u16; 4];
        for (index, value) in rain.iter_mut().enumerate() {
            *value = weather.hours[index].precipitation_percent.unwrap_or(0) as u16;
        }
        if self.commands.begin(23).is_err()
            || set_now_temperature(
                &mut self.commands,
                "weather.hourly-now",
                weather.current.temperature_tenths,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_primary_text(
                "weather.hourly-condition",
                condition_label(weather.current.condition),
            ).is_err()
            || self.commands.set_primary_text(
                "weather.hourly-status", status).is_err()
            || self.commands.set_chart_samples(
                "weather.rain-chart", &rain).is_err()
            || self.commands.set_icon(
                "weather.hourly-condition-icon",
                condition_icon(weather.current.condition),
            ).is_err()
            || set_temperature_semantic(
                &mut self.commands,
                "weather.hourly-now",
                weather.current.temperature_tenths,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_semantic_label(
                "weather.hourly-condition-icon",
                condition_label(weather.current.condition),
            ).is_err()
            || set_status_semantic(
                &mut self.commands,
                "weather.hourly-status",
                weather.freshness,
                weather.cache_age_minutes,
                &mut self.scratch,
            ).is_err()
        {
            return 0;
        }
        for (index, target) in [
            "weather.hour-now-temp",
            "weather.hour-10-temp",
            "weather.hour-11-temp",
            "weather.hour-12-temp",
        ].iter().enumerate() {
            let text = format_temperature(
                weather.hours[index].temperature_tenths,
                &mut self.scratch,
            );
            if self.commands.set_primary_text(target, text).is_err() {
                return 0;
            }
        }
        for (index, target) in [
            "weather.hour-10-label",
            "weather.hour-11-label",
            "weather.hour-12-label",
        ].iter().enumerate() {
            let text = format_hour_label(
                weather.hours[index + 1].local_minute,
                &mut self.scratch,
            );
            if self.commands.set_primary_text(target, text).is_err() {
                return 0;
            }
        }
        for (index, target) in [
            "weather.hour-now-icon",
            "weather.hour-10-icon",
            "weather.hour-11-icon",
            "weather.hour-12-icon",
        ].iter().enumerate() {
            if self.commands.set_icon(
                target,
                condition_icon(weather.hours[index].condition),
            ).is_err() || self.commands.set_semantic_label(
                target,
                condition_label(weather.hours[index].condition),
            ).is_err() {
                return 0;
            }
        }
        finish(&self.commands)
    }

    fn render_daily(&mut self) -> u64 {
        let weather = self.weather;
        if !weather.valid || weather.day_count < 4 {
            return 0;
        }
        let status = format_status(
            weather.freshness,
            weather.data_revision,
            weather.cache_age_minutes,
            &mut self.status,
        );
        if self.commands.begin(23).is_err()
            || self.commands.set_primary_text(
                "weather.daily-location", weather.location()).is_err()
            || self.commands.set_primary_text(
                "weather.daily-status", status).is_err()
            || set_status_semantic(
                &mut self.commands,
                "weather.daily-status",
                weather.freshness,
                weather.cache_age_minutes,
                &mut self.scratch,
            ).is_err()
        {
            return 0;
        }
        let suffixes = ["today", "mon", "tue", "wed"];
        for (index, suffix) in suffixes.iter().enumerate() {
            let day = weather.days[index];
            let label = if index == 0 { "TODAY" } else { weekday_label(day.weekday) };
            let mut target = [0_u8; 40];
            let label_target = target_id("weather.day-", suffix, "-label", &mut target);
            if self.commands.set_primary_text(label_target, label).is_err() {
                return 0;
            }
            let low_target = target_id("weather.day-", suffix, "-low", &mut target);
            let low = format_temperature(day.low_tenths, &mut self.scratch);
            if self.commands.set_primary_text(low_target, low).is_err() {
                return 0;
            }
            let high_target = target_id("weather.day-", suffix, "-high", &mut target);
            let high = format_temperature(day.high_tenths, &mut self.scratch);
            if self.commands.set_primary_text(high_target, high).is_err() {
                return 0;
            }
            let icon_target = target_id("weather.day-", suffix, "-icon", &mut target);
            if self.commands.set_icon(
                icon_target,
                condition_icon(day.condition),
            ).is_err() || self.commands.set_semantic_label(
                icon_target,
                condition_label(day.condition),
            ).is_err() {
                return 0;
            }
        }
        finish(&self.commands)
    }

    fn render_details(&mut self) -> u64 {
        let weather = self.weather;
        if !weather.valid {
            return 0;
        }
        let status = format_status(
            weather.freshness,
            weather.data_revision,
            weather.cache_age_minutes,
            &mut self.status,
        );
        if self.commands.begin(17).is_err()
            || set_temperature(
                &mut self.commands,
                "weather.details-temperature",
                weather.current.temperature_tenths,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_primary_text(
                "weather.details-condition",
                condition_label(weather.current.condition),
            ).is_err()
            || self.commands.set_primary_text(
                "weather.details-status", status).is_err()
            || set_percent(
                &mut self.commands,
                "weather.humidity-value",
                weather.current.humidity_percent,
                &mut self.scratch,
            ).is_err()
            || set_tenths_number(
                &mut self.commands,
                "weather.wind-value",
                weather.current.wind_speed_tenths,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_primary_text(
                "weather.wind-unit",
                if weather.units == WeatherUnits::Imperial { "mph" } else { "km/h" },
            ).is_err()
            || set_tenths_number(
                &mut self.commands,
                "weather.uv-value",
                weather.current.uv_index_tenths,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_primary_text(
                "weather.uv-unit",
                uv_label(weather.current.uv_index_tenths),
            ).is_err()
            || set_time(
                &mut self.commands,
                "weather.sunrise-value",
                weather.current.sunrise_local_minute,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_icon(
                "weather.details-condition-icon",
                condition_icon(weather.current.condition),
            ).is_err()
            || self.commands.set_semantic_label(
                "weather.details-condition-icon",
                condition_label(weather.current.condition),
            ).is_err()
            || set_temperature_semantic(
                &mut self.commands,
                "weather.details-temperature",
                weather.current.temperature_tenths,
                &mut self.scratch,
            ).is_err()
            || set_status_semantic(
                &mut self.commands,
                "weather.details-status",
                weather.freshness,
                weather.cache_age_minutes,
                &mut self.scratch,
            ).is_err()
            || set_percent_semantic(
                &mut self.commands,
                "weather.humidity-value",
                weather.current.humidity_percent,
                &mut self.scratch,
            ).is_err()
            || set_wind_semantic(
                &mut self.commands,
                "weather.wind-value",
                weather.current.wind_speed_tenths,
                weather.units,
                &mut self.scratch,
            ).is_err()
            || set_uv_semantic(
                &mut self.commands,
                "weather.uv-value",
                weather.current.uv_index_tenths,
                &mut self.scratch,
            ).is_err()
            || set_time_semantic(
                &mut self.commands,
                "weather.sunrise-value",
                "Sunrise at ",
                weather.current.sunrise_local_minute,
                &mut self.scratch,
            ).is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }

    fn render_rain(&mut self) -> u64 {
        let weather = self.weather;
        let minutes = weather.minutes_until_rain.unwrap_or(20);
        if self.commands.begin(6).is_err()
            || set_rain_title(
                &mut self.commands, minutes, &mut self.scratch).is_err()
            || set_rain_duration(
                &mut self.commands,
                weather.rain_duration_minutes,
                &mut self.scratch,
            ).is_err()
            || set_percent(
                &mut self.commands,
                "weather.rain-probability-value",
                weather.current.precipitation_percent,
                &mut self.scratch,
            ).is_err()
            || self.commands.set_chart_samples(
                "weather.rain-bars",
                &weather.precipitation.map(u16::from),
            ).is_err()
            || set_rain_title_semantic(
                &mut self.commands,
                minutes,
                &mut self.scratch,
            ).is_err()
            || set_percent_semantic(
                &mut self.commands,
                "weather.rain-probability-value",
                weather.current.precipitation_percent,
                &mut self.scratch,
            ).is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);
unsafe impl Sync for SharedRuntime {}
static RUNTIME: SharedRuntime =
    SharedRuntime(UnsafeCell::new(Runtime::new()));

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(APPSPEC);
    let _ = request_provider("weather", "refresh", &[]);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(
    pointer: *const u8,
    length: u32,
) -> u64 {
    if pointer.is_null() || length == 0 || length > 512 {
        return 0;
    }
    let bytes =
        unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_ui_event(bytes) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    match event.action_id {
        "weather.retry" => {
            if request_provider("weather", "refresh", &[]).is_err() {
                0
            } else if runtime.showing_rain {
                0
            } else {
                runtime.loading()
            }
        }
        "weather.hourly" => {
            runtime.mount_hourly()
        }
        "weather.daily" => {
            runtime.mount_daily()
        }
        "weather.details" => {
            runtime.mount_details()
        }
        "weather.rain-preview" => {
            runtime.showing_rain = true;
            let _ = mount_appspec(RAIN);
            runtime.page = 4;
            if runtime.weather.minutes_until_rain.is_some() {
                runtime.render_rain()
            } else {
                0
            }
        }
        "weather.rain-details" => {
            runtime.mount_details()
        }
        "weather.page-changed" => {
            if let EventValue::Integer(delta) = event.value {
                runtime.navigate(delta)
            } else {
                0
            }
        }
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_provider_event(
    pointer: *const u8,
    length: u32,
) -> u64 {
    if pointer.is_null() || length == 0 || length > 1024 {
        return 0;
    }
    let bytes =
        unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_provider_event(bytes) {
        Ok(value)
            if value.provider_id == "weather"
                && value.event_id == "weather.snapshot.v2" =>
        {
            value
        }
        _ => return 0,
    };
    let weather = match decode_weather_provider_payload_v2(event.payload) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    runtime.weather.replace(event.freshness, weather);
    if weather.minutes_until_rain.is_some() {
        runtime.showing_rain = true;
        return runtime.mount_rain();
    }
    if runtime.showing_rain {
        runtime.showing_rain = false;
        if runtime.page == 4 {
            return runtime.mount_current();
        }
    }
    runtime.render_active()
}

fn condition_label(condition: WeatherCondition) -> &'static str {
    match condition {
        WeatherCondition::ClearDay | WeatherCondition::ClearNight => "Clear",
        WeatherCondition::PartlyCloudyDay |
        WeatherCondition::PartlyCloudyNight => "Partly cloudy",
        WeatherCondition::Cloudy => "Cloudy",
        WeatherCondition::Overcast => "Overcast",
        WeatherCondition::Fog => "Foggy",
        WeatherCondition::Drizzle => "Drizzle",
        WeatherCondition::Rain => "Rain",
        WeatherCondition::HeavyRain => "Heavy rain",
        WeatherCondition::Thunderstorm => "Thunderstorms",
        WeatherCondition::Snow => "Snow",
        WeatherCondition::Sleet => "Sleet",
        WeatherCondition::Wind => "Windy",
        WeatherCondition::Hot => "Hot",
        WeatherCondition::Unknown => "Unavailable",
    }
}

fn condition_icon(condition: WeatherCondition) -> &'static str {
    match condition {
        WeatherCondition::ClearDay => "condition_clear_day",
        WeatherCondition::ClearNight => "condition_clear_night",
        WeatherCondition::PartlyCloudyDay => "condition_partly_cloudy_day",
        WeatherCondition::PartlyCloudyNight => "condition_partly_cloudy_night",
        WeatherCondition::Cloudy => "condition_cloudy",
        WeatherCondition::Overcast => "condition_overcast",
        WeatherCondition::Fog => "condition_fog",
        WeatherCondition::Drizzle => "condition_drizzle",
        WeatherCondition::Rain => "condition_rain",
        WeatherCondition::HeavyRain => "condition_heavy_rain",
        WeatherCondition::Thunderstorm => "condition_thunderstorm",
        WeatherCondition::Snow => "condition_snow",
        WeatherCondition::Sleet => "condition_sleet",
        WeatherCondition::Wind => "condition_wind",
        WeatherCondition::Hot => "condition_hot",
        WeatherCondition::Unknown => "condition_unknown",
    }
}

fn set_temperature<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: i32,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    commands.set_primary_text(target, format_temperature(value, output))
}

fn set_optional_temperature<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    prefix: &str,
    value: Option<i32>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = copy(prefix.as_bytes(), output, 0);
    if let Some(value) = value {
        let rounded = if value >= 0 { (value + 5) / 10 } else { (value - 5) / 10 };
        if rounded < 0 {
            output[cursor] = b'-';
            cursor += 1;
        }
        cursor = write_unsigned(rounded.unsigned_abs() as u64, output, cursor);
        output[cursor] = 0xc2;
        output[cursor + 1] = 0xb0;
        cursor += 2;
    } else {
        cursor = copy(b"--", output, cursor);
    }
    let text = unsafe { core::str::from_utf8_unchecked(&output[..cursor]) };
    commands.set_primary_text(target, text)
}

fn set_now_temperature<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: i32,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let cursor = copy(b"Now ", output, 0);
    let rounded = if value >= 0 { (value + 5) / 10 } else { (value - 5) / 10 };
    let mut end = cursor;
    if rounded < 0 {
        output[end] = b'-';
        end += 1;
    }
    end = write_unsigned(rounded.unsigned_abs() as u64, output, end);
    output[end] = 0xc2;
    output[end + 1] = 0xb0;
    end += 2;
    commands.set_primary_text(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..end]) },
    )
}

fn set_percent<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: Option<u8>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = 0;
    if let Some(value) = value {
        cursor = write_unsigned(value as u64, output, cursor);
        output[cursor] = b'%';
        cursor += 1;
    } else {
        cursor = copy(b"--", output, cursor);
    }
    commands.set_primary_text(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_tenths_number<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: Option<u16>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let cursor = if let Some(value) = value {
        write_unsigned(((value as u64) + 5) / 10, output, 0)
    } else {
        copy(b"--", output, 0)
    };
    commands.set_primary_text(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_time<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: Option<u16>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = 0;
    if let Some(minutes) = value {
        let mut hour = (minutes / 60) % 24;
        if hour == 0 { hour = 12; } else if hour > 12 { hour -= 12; }
        cursor = write_unsigned(hour as u64, output, cursor);
        output[cursor] = b':';
        cursor += 1;
        let minute = minutes % 60;
        if minute < 10 {
            output[cursor] = b'0';
            cursor += 1;
        }
        cursor = write_unsigned(minute as u64, output, cursor);
    } else {
        cursor = copy(b"--:--", output, cursor);
    }
    commands.set_primary_text(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn format_hour_label<'a>(minutes: u16, output: &'a mut [u8]) -> &'a str {
    let mut hour = (minutes / 60) % 24;
    if hour == 0 {
        hour = 12;
    } else if hour > 12 {
        hour -= 12;
    }
    let cursor = write_unsigned(hour as u64, output, 0);
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

fn set_temperature_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: i32,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let rounded = if value >= 0 { (value + 5) / 10 } else { (value - 5) / 10 };
    let mut cursor = 0;
    if rounded < 0 {
        output[cursor] = b'-';
        cursor += 1;
    }
    cursor = write_unsigned(rounded.unsigned_abs() as u64, output, cursor);
    cursor = copy(
        if rounded == 1 || rounded == -1 {
            b" degree".as_slice()
        } else {
            b" degrees".as_slice()
        },
        output,
        cursor,
    );
    commands.set_semantic_value(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_status_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    freshness: Freshness,
    age: u64,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let cursor = match freshness {
        Freshness::Current => copy(b"Weather updated now", output, 0),
        Freshness::Stale => {
            let mut cursor = copy(b"Weather updated ", output, 0);
            cursor = write_unsigned(age, output, cursor);
            copy(b" minutes ago", output, cursor)
        }
        Freshness::Offline => copy(b"Weather unavailable offline", output, 0),
        Freshness::Error => copy(b"Weather unavailable, retry", output, 0),
    };
    commands.set_semantic_label(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_percent_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: Option<u8>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let cursor = if let Some(value) = value {
        let cursor = write_unsigned(value as u64, output, 0);
        copy(b" percent", output, cursor)
    } else {
        copy(b"Unavailable", output, 0)
    };
    commands.set_semantic_label(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_wind_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: Option<u16>,
    units: WeatherUnits,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let cursor = if let Some(value) = value {
        let cursor = write_unsigned(((value as u64) + 5) / 10, output, 0);
        copy(
            if units == WeatherUnits::Imperial {
                b" miles per hour".as_slice()
            } else {
                b" kilometers per hour".as_slice()
            },
            output,
            cursor,
        )
    } else {
        copy(b"Wind unavailable", output, 0)
    };
    commands.set_semantic_label(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_uv_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    value: Option<u16>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let cursor = if let Some(value) = value {
        let rounded = ((value as u64) + 5) / 10;
        let mut cursor = copy(b"UV index ", output, 0);
        cursor = write_unsigned(rounded, output, cursor);
        cursor = copy(b", ", output, cursor);
        copy(uv_label(Some(value)).as_bytes(), output, cursor)
    } else {
        copy(b"UV index unavailable", output, 0)
    };
    commands.set_semantic_label(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_time_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    target: &str,
    prefix: &str,
    value: Option<u16>,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = copy(prefix.as_bytes(), output, 0);
    if let Some(minutes) = value {
        let mut hour = (minutes / 60) % 24;
        if hour == 0 { hour = 12; } else if hour > 12 { hour -= 12; }
        cursor = write_unsigned(hour as u64, output, cursor);
        output[cursor] = b':';
        cursor += 1;
        let minute = minutes % 60;
        if minute < 10 {
            output[cursor] = b'0';
            cursor += 1;
        }
        cursor = write_unsigned(minute as u64, output, cursor);
    } else {
        cursor = copy(b"unavailable", output, cursor);
    }
    commands.set_semantic_label(
        target,
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_rain_title<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    minutes: u16,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = copy(b"Rain in\n", output, 0);
    cursor = write_unsigned(minutes as u64, output, cursor);
    cursor = copy(b" min", output, cursor);
    commands.set_primary_text(
        "weather.rain-title",
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_rain_duration<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    minutes: u16,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = copy(b"Light rain for ", output, 0);
    cursor = write_unsigned(minutes as u64, output, cursor);
    cursor = copy(b" min", output, cursor);
    commands.set_primary_text(
        "weather.rain-duration",
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn set_rain_title_semantic<const N: usize>(
    commands: &mut UiCommandBuffer<N>,
    minutes: u16,
    output: &mut [u8],
) -> Result<(), CommandEncodeError> {
    let mut cursor = copy(b"Rain in ", output, 0);
    cursor = write_unsigned(minutes as u64, output, cursor);
    cursor = copy(
        if minutes == 1 {
            b" minute".as_slice()
        } else {
            b" minutes".as_slice()
        },
        output,
        cursor,
    );
    commands.set_semantic_label(
        "weather.rain-title",
        unsafe { core::str::from_utf8_unchecked(&output[..cursor]) },
    )
}

fn uv_label(value: Option<u16>) -> &'static str {
    match value.map(|value| (value + 5) / 10) {
        Some(0..=2) => "Low",
        Some(3..=5) => "Moderate",
        Some(6..=7) => "High",
        Some(8..=10) => "Very high",
        Some(_) => "Extreme",
        None => "--",
    }
}

fn weekday_label(value: u8) -> &'static str {
    ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
        .get(value as usize)
        .copied()
        .unwrap_or("---")
}

fn target_id<'a>(
    prefix: &str,
    middle: &str,
    suffix: &str,
    output: &'a mut [u8],
) -> &'a str {
    let mut cursor = copy(prefix.as_bytes(), output, 0);
    cursor = copy(middle.as_bytes(), output, cursor);
    cursor = copy(suffix.as_bytes(), output, cursor);
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

fn finish<const N: usize>(commands: &UiCommandBuffer<N>) -> u64 {
    match commands.finish() {
        Ok(bytes) => pack_result(bytes),
        Err(_) => 0,
    }
}

fn format_temperature<'a>(value: i32, output: &'a mut [u8]) -> &'a str {
    let rounded = if value >= 0 {
        (value + 5) / 10
    } else {
        (value - 5) / 10
    };
    let mut cursor = 0;
    if rounded < 0 {
        output[cursor] = b'-';
        cursor += 1;
    }
    cursor = write_unsigned(rounded.unsigned_abs() as u64, output, cursor);
    output[cursor] = 0xc2;
    output[cursor + 1] = 0xb0;
    cursor += 2;
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

fn format_status(
    freshness: Freshness,
    revision: u64,
    age: u64,
    output: &mut [u8; 64],
) -> &str {
    let (prefix, number, suffix, include_number) = match freshness {
        Freshness::Current => ("Now", revision, "", false),
        Freshness::Stale => ("", age, "m", true),
        Freshness::Offline => ("Offline", age, "", false),
        Freshness::Error => ("Retry", revision, "", false),
    };
    let mut cursor = copy(prefix.as_bytes(), output, 0);
    if include_number {
        cursor = write_unsigned(number, output, cursor);
        cursor = copy(suffix.as_bytes(), output, cursor);
    }
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

fn copy(source: &[u8], output: &mut [u8], offset: usize) -> usize {
    let end = offset + source.len();
    output[offset..end].copy_from_slice(source);
    end
}

fn write_unsigned(
    value: u64,
    output: &mut [u8],
    offset: usize,
) -> usize {
    let mut reverse = [0_u8; 20];
    let mut digits = 0;
    let mut remaining = value;
    loop {
        reverse[digits] = b'0' + (remaining % 10) as u8;
        digits += 1;
        remaining /= 10;
        if remaining == 0 {
            break;
        }
    }
    let mut cursor = offset;
    while digits > 0 {
        digits -= 1;
        output[cursor] = reverse[digits];
        cursor += 1;
    }
    cursor
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
