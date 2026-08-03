#include "weather_provider.hpp"

#include "sdkconfig.h"

#if defined(CONFIG_DOODAD_WEATHER_NETWORK_PROVIDER) && \
    CONFIG_DOODAD_WEATHER_NETWORK_PROVIDER

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_service.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "weather_provider_model.hpp"

namespace {

constexpr char kTag[] = "weather-provider";
constexpr char kForecastEndpoint[] = "https://api.open-meteo.com/v1/forecast";
constexpr char kCacheNamespace[] = "doodad_weather";
constexpr char kCacheKey[] = "snapshot";
constexpr std::uint32_t kCacheMagic = 0x44575448;  // DWTH
constexpr std::uint16_t kCacheVersion = 1;
constexpr std::size_t kMaximumResponseBytes = 16 * 1024;
constexpr std::uint32_t kWorkerStackBytes = 8 * 1024;

struct CacheEnvelope {
    std::uint32_t magic = kCacheMagic;
    std::uint16_t version = kCacheVersion;
    std::uint16_t size = sizeof(CacheEnvelope);
    std::int64_t saved_epoch_seconds = 0;
    WeatherProviderResult result{};
};

struct HttpBuffer {
    char* bytes = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
    bool overflow = false;
};

struct LocalDateTime {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    std::uint16_t minute = 0;
    std::uint8_t weekday = 0;
};

QueueHandle_t g_result_queue = nullptr;
portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
bool g_busy = false;
bool g_nvs_ready = false;

template <std::size_t Size>
void copy_text(char (&destination)[Size], const char* source) {
    if (source == nullptr) return;
    std::strncpy(destination, source, Size - 1);
    destination[Size - 1] = '\0';
}

const char* condition_name(std::uint8_t condition) {
    constexpr std::array<const char*, 16> names{
        "Clear", "Clear", "Partly cloudy", "Partly cloudy",
        "Cloudy", "Overcast", "Fog", "Drizzle", "Rain",
        "Heavy rain", "Thunderstorms", "Snow", "Sleet", "Windy",
        "Hot", "Unknown",
    };
    return names[std::min<std::size_t>(condition, names.size() - 1)];
}

std::int32_t rounded_temperature(std::int32_t tenths) {
    return tenths >= 0 ? (tenths + 5) / 10 : (tenths - 5) / 10;
}

void describe_result(WeatherProviderResult& result) {
    copy_text(result.condition, condition_name(result.snapshot.current.condition));
    const auto& current = result.snapshot.current;
    if (current.has_high && current.has_low && current.has_feels_like) {
        std::snprintf(
            result.detail,
            sizeof(result.detail),
            "High %ld - Low %ld - Feels %ld",
            static_cast<long>(rounded_temperature(current.high_tenths)),
            static_cast<long>(rounded_temperature(current.low_tenths)),
            static_cast<long>(rounded_temperature(current.feels_like_tenths)));
    } else {
        copy_text(result.detail, result.condition);
    }
}

bool ensure_nvs() {
    if (g_nvs_ready) return true;
    const auto status = nvs_flash_init();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "NVS initialization failed: %s", esp_err_to_name(status));
        return false;
    }
    g_nvs_ready = true;
    return true;
}

esp_err_t http_event(esp_http_client_event_t* event) {
    if (event == nullptr || event->user_data == nullptr) return ESP_OK;
    auto& buffer = *static_cast<HttpBuffer*>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    const auto incoming = static_cast<std::size_t>(event->data_len);
    if (incoming > buffer.capacity - buffer.size) {
        buffer.overflow = true;
        return ESP_FAIL;
    }
    std::memcpy(buffer.bytes + buffer.size, event->data, incoming);
    buffer.size += incoming;
    buffer.bytes[buffer.size] = '\0';
    return ESP_OK;
}

bool fetch_json(HttpBuffer& buffer) {
    std::array<char, 2048> url{};
    constexpr const char* current =
        "temperature_2m,relative_humidity_2m,apparent_temperature,"
        "precipitation_probability,weather_code,wind_speed_10m,"
        "wind_direction_10m,is_day";
    constexpr const char* hourly =
        "temperature_2m,precipitation_probability,weather_code,uv_index";
    constexpr const char* minutely =
        "precipitation_probability,precipitation";
    constexpr const char* daily =
        "weather_code,temperature_2m_max,temperature_2m_min,"
        "precipitation_probability_max,sunrise,sunset";
#if defined(CONFIG_DOODAD_WEATHER_UNITS_IMPERIAL)
    constexpr const char* units =
        "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch";
#else
    constexpr const char* units = "";
#endif
    const auto written = std::snprintf(
        url.data(),
        url.size(),
        "%s?latitude=%s&longitude=%s&timezone=%s&forecast_hours=7&"
        "forecast_minutely_15=5&forecast_days=4&current=%s&hourly=%s&"
        "minutely_15=%s&daily=%s%s",
        kForecastEndpoint,
        CONFIG_DOODAD_WEATHER_LATITUDE,
        CONFIG_DOODAD_WEATHER_LONGITUDE,
        CONFIG_DOODAD_WEATHER_TIMEZONE,
        current,
        hourly,
        minutely,
        daily,
        units);
    if (written <= 0 || static_cast<std::size_t>(written) >= url.size()) {
        return false;
    }

    esp_http_client_config_t configuration{};
    configuration.url = url.data();
    configuration.event_handler = http_event;
    configuration.user_data = &buffer;
    configuration.timeout_ms = 12'000;
    // The generated request line is about 600 bytes. Leave enough room for
    // ESP-IDF to append its first header batch in the same transmit buffer.
    configuration.buffer_size_tx = 1024;
    configuration.crt_bundle_attach = esp_crt_bundle_attach;
    configuration.user_agent = "DoodadWeather/1.0";
    auto client = esp_http_client_init(&configuration);
    if (client == nullptr) return false;
    const auto status = esp_http_client_perform(client);
    const auto response_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status != ESP_OK || response_code != 200 || buffer.overflow ||
        buffer.size == 0) {
        ESP_LOGW(
            kTag,
            "forecast request failed: transport=%s http=%d bytes=%u overflow=%d",
            esp_err_to_name(status),
            response_code,
            static_cast<unsigned>(buffer.size),
            buffer.overflow);
        return false;
    }
    return true;
}

const cJSON* member(const cJSON* object, const char* name) {
    if (!cJSON_IsObject(object)) return nullptr;
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool finite_number(const cJSON* value, double& output) {
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)) {
        return false;
    }
    output = value->valuedouble;
    return true;
}

bool bounded_integer(
    const cJSON* value,
    int minimum,
    int maximum,
    int& output) {
    double number = 0;
    if (!finite_number(value, number)) return false;
    const auto rounded = static_cast<long>(std::lround(number));
    if (rounded < minimum || rounded > maximum) return false;
    output = static_cast<int>(rounded);
    return true;
}

bool tenths(const cJSON* value, std::int32_t& output) {
    double number = 0;
    if (!finite_number(value, number)) return false;
    const auto rounded = std::llround(number * 10.0);
    if (rounded < std::numeric_limits<std::int32_t>::min() ||
        rounded > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    output = static_cast<std::int32_t>(rounded);
    return true;
}

const cJSON* array(const cJSON* section, const char* name, int minimum_size) {
    const auto* value = member(section, name);
    return cJSON_IsArray(value) && cJSON_GetArraySize(value) >= minimum_size
        ? value
        : nullptr;
}

const cJSON* item(const cJSON* values, int index) {
    return values == nullptr ? nullptr : cJSON_GetArrayItem(values, index);
}

bool local_datetime(const cJSON* value, LocalDateTime& output) {
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    const auto count = std::sscanf(
        value->valuestring,
        "%d-%u-%uT%u:%u",
        &year,
        &month,
        &day,
        &hour,
        &minute);
    if (count != 5 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59) {
        return false;
    }
    output.year = year;
    output.month = month;
    output.day = day;
    output.minute = static_cast<std::uint16_t>(hour * 60 + minute);
    output.weekday = doodad::weather::weekday(year, month, day);
    return true;
}

bool local_date(const cJSON* value, LocalDateTime& output) {
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (std::sscanf(value->valuestring, "%d-%u-%u", &year, &month, &day) != 3 ||
        month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    output.year = year;
    output.month = month;
    output.day = day;
    output.weekday = doodad::weather::weekday(year, month, day);
    return true;
}

bool same_date(const LocalDateTime& left, const LocalDateTime& right) {
    return left.year == right.year && left.month == right.month &&
           left.day == right.day;
}

std::uint64_t fnv_revision(const char* bytes, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint8_t>(bytes[index]);
        hash *= 1099511628211ULL;
    }
    return hash == 0 ? 1 : hash;
}

bool normalize(
    const char* json,
    std::size_t json_size,
    WeatherProviderResult& result) {
    auto* root = cJSON_ParseWithLength(json, json_size);
    if (root == nullptr) return false;
    const auto cleanup = [&]() { cJSON_Delete(root); };
    const auto* current = member(root, "current");
    const auto* hourly = member(root, "hourly");
    const auto* minutely = member(root, "minutely_15");
    const auto* daily = member(root, "daily");
    if (!cJSON_IsObject(current) || !cJSON_IsObject(hourly) ||
        !cJSON_IsObject(minutely) || !cJSON_IsObject(daily)) {
        cleanup();
        return false;
    }

    const auto* hourly_times = array(hourly, "time", 7);
    const auto* hourly_temperatures = array(hourly, "temperature_2m", 7);
    const auto* hourly_probabilities =
        array(hourly, "precipitation_probability", 7);
    const auto* hourly_codes = array(hourly, "weather_code", 7);
    const auto* hourly_uv = array(hourly, "uv_index", 1);
    const auto* day_times = array(daily, "time", 4);
    const auto* day_highs = array(daily, "temperature_2m_max", 4);
    const auto* day_lows = array(daily, "temperature_2m_min", 4);
    const auto* day_probabilities =
        array(daily, "precipitation_probability_max", 4);
    const auto* day_codes = array(daily, "weather_code", 4);
    const auto* sunrises = array(daily, "sunrise", 4);
    const auto* sunsets = array(daily, "sunset", 4);
    const auto* minute_probabilities =
        array(minutely, "precipitation_probability", 5);
    const auto* minute_amounts = array(minutely, "precipitation", 5);
    if (hourly_times == nullptr || hourly_temperatures == nullptr ||
        hourly_probabilities == nullptr || hourly_codes == nullptr ||
        hourly_uv == nullptr || day_times == nullptr || day_highs == nullptr ||
        day_lows == nullptr || day_probabilities == nullptr ||
        day_codes == nullptr || sunrises == nullptr || sunsets == nullptr ||
        minute_probabilities == nullptr || minute_amounts == nullptr) {
        cleanup();
        return false;
    }

    result = {};
    copy_text(result.location, CONFIG_DOODAD_WEATHER_LOCATION_NAME);
    if (result.location[0] == '\0') copy_text(result.location, "Local weather");
    auto& snapshot = result.snapshot;
    snapshot.location = result.location;
    snapshot.units =
#if defined(CONFIG_DOODAD_WEATHER_UNITS_IMPERIAL)
        1;
#else
        0;
#endif

    LocalDateTime current_time{};
    std::int32_t current_temperature = 0;
    std::int32_t wind_speed = 0;
    int current_code = 0;
    int current_is_day = 0;
    if (!local_datetime(member(current, "time"), current_time) ||
        !tenths(member(current, "temperature_2m"), current_temperature) ||
        !tenths(member(current, "wind_speed_10m"), wind_speed) ||
        wind_speed < 0 || wind_speed > std::numeric_limits<std::uint16_t>::max() ||
        !bounded_integer(member(current, "weather_code"), 0, 99, current_code) ||
        !bounded_integer(member(current, "is_day"), 0, 1, current_is_day)) {
        cleanup();
        return false;
    }
    snapshot.local_weekday = current_time.weekday;
    snapshot.local_minute = current_time.minute;
    snapshot.current.temperature_tenths = current_temperature;
    snapshot.current.wind_speed_tenths = static_cast<std::uint16_t>(wind_speed);
    snapshot.current.has_wind_speed = 1;
    snapshot.current.condition = doodad::weather::map_wmo_condition(
        current_code,
        current_is_day != 0,
        current_temperature,
        snapshot.units == 1,
        snapshot.current.wind_speed_tenths);

    if (!tenths(
            member(current, "apparent_temperature"),
            snapshot.current.feels_like_tenths)) {
        cleanup();
        return false;
    }
    snapshot.current.has_feels_like = 1;
    int integer = 0;
    if (bounded_integer(member(current, "relative_humidity_2m"), 0, 100, integer)) {
        snapshot.current.humidity_percent = static_cast<std::uint8_t>(integer);
        snapshot.current.has_humidity = 1;
    }
    if (bounded_integer(
            member(current, "precipitation_probability"), 0, 100, integer)) {
        snapshot.current.precipitation_percent = static_cast<std::uint8_t>(integer);
        snapshot.current.has_precipitation = 1;
    }
    if (!bounded_integer(
            member(current, "wind_direction_10m"), 0, 360, integer)) {
        cleanup();
        return false;
    }
    snapshot.current.wind_direction_degrees =
        static_cast<std::uint16_t>(integer % 360);
    snapshot.current.has_wind_direction = 1;
    std::int32_t uv_tenths = 0;
    if (tenths(item(hourly_uv, 0), uv_tenths) && uv_tenths >= 0 &&
        uv_tenths <= std::numeric_limits<std::uint16_t>::max()) {
        snapshot.current.uv_index_tenths = static_cast<std::uint16_t>(uv_tenths);
        snapshot.current.has_uv_index = 1;
    }

    std::array<LocalDateTime, 4> dates{};
    std::array<std::uint16_t, 4> sunrise_minutes{};
    std::array<std::uint16_t, 4> sunset_minutes{};
    snapshot.day_count = 4;
    for (int index = 0; index < 4; ++index) {
        LocalDateTime sunrise{};
        LocalDateTime sunset{};
        std::int32_t high = 0;
        std::int32_t low = 0;
        int code = 0;
        int probability = 0;
        if (!local_date(item(day_times, index), dates[index]) ||
            !local_datetime(item(sunrises, index), sunrise) ||
            !local_datetime(item(sunsets, index), sunset) ||
            !tenths(item(day_highs, index), high) ||
            !tenths(item(day_lows, index), low) ||
            !bounded_integer(item(day_codes, index), 0, 99, code) ||
            !bounded_integer(
                item(day_probabilities, index), 0, 100, probability)) {
            cleanup();
            return false;
        }
        sunrise_minutes[index] = sunrise.minute;
        sunset_minutes[index] = sunset.minute;
        auto& day = snapshot.days[index];
        day.weekday = dates[index].weekday;
        day.high_tenths = high;
        day.low_tenths = low;
        day.precipitation_percent = static_cast<std::uint8_t>(probability);
        day.has_precipitation = 1;
        day.condition = doodad::weather::map_wmo_condition(
            code,
            true,
            high,
            snapshot.units == 1,
            snapshot.current.wind_speed_tenths);
    }
    snapshot.current.high_tenths = snapshot.days[0].high_tenths;
    snapshot.current.low_tenths = snapshot.days[0].low_tenths;
    snapshot.current.has_high = 1;
    snapshot.current.has_low = 1;
    snapshot.current.sunrise_local_minute = sunrise_minutes[0];
    snapshot.current.sunset_local_minute = sunset_minutes[0];
    snapshot.current.has_sunrise = 1;
    snapshot.current.has_sunset = 1;

    snapshot.hour_count = 7;
    for (int index = 0; index < 7; ++index) {
        LocalDateTime local{};
        std::int32_t temperature = 0;
        int probability = 0;
        int code = 0;
        if (!local_datetime(item(hourly_times, index), local) ||
            !tenths(item(hourly_temperatures, index), temperature) ||
            !bounded_integer(
                item(hourly_probabilities, index), 0, 100, probability) ||
            !bounded_integer(item(hourly_codes, index), 0, 99, code)) {
            cleanup();
            return false;
        }
        std::uint16_t sunrise = 6 * 60;
        std::uint16_t sunset = 19 * 60;
        for (std::size_t day = 0; day < dates.size(); ++day) {
            if (same_date(local, dates[day])) {
                sunrise = sunrise_minutes[day];
                sunset = sunset_minutes[day];
                break;
            }
        }
        auto& hour = snapshot.hours[index];
        hour.local_minute = local.minute;
        hour.temperature_tenths = temperature;
        hour.precipitation_percent = static_cast<std::uint8_t>(probability);
        hour.has_precipitation = 1;
        hour.condition = doodad::weather::map_wmo_condition(
            code,
            local.minute >= sunrise && local.minute < sunset,
            temperature,
            snapshot.units == 1,
            snapshot.current.wind_speed_tenths);
    }

    std::array<std::uint8_t, 5> probabilities{};
    std::array<float, 5> amounts{};
    for (int index = 0; index < 5; ++index) {
        int probability = 0;
        double amount = 0;
        if (!bounded_integer(
                item(minute_probabilities, index), 0, 100, probability) ||
            !finite_number(item(minute_amounts, index), amount) || amount < 0) {
            cleanup();
            return false;
        }
        probabilities[index] = static_cast<std::uint8_t>(probability);
        amounts[index] = static_cast<float>(amount);
    }
    const auto precipitation =
        doodad::weather::resample_fifteen_to_five(probabilities);
    std::copy(
        precipitation.begin(), precipitation.end(), snapshot.precipitation);
    const auto window = doodad::weather::rain_window(precipitation, amounts);
    snapshot.minutes_until_rain = window.minutes_until_rain;
    snapshot.rain_duration_minutes = window.duration_minutes;
    snapshot.data_revision = fnv_revision(json, json_size);
    snapshot.cache_age_minutes = 0;
    result.freshness = 0;
    describe_result(result);
    cleanup();
    return true;
}

std::int64_t current_epoch() {
    const auto now = std::time(nullptr);
    // Reject unset RTC values rather than inventing an enormous cache age.
    return now >= 1'700'000'000 ? static_cast<std::int64_t>(now) : 0;
}

bool snapshot_encodes(WeatherProviderResult& result) {
    result.snapshot.location = result.location;
    std::array<std::uint8_t, 512> encoded{};
    return m3e_encode_weather_provider_event_v2(
               &result.snapshot,
               1,
               result.freshness,
               0,
               encoded.data(),
               encoded.size()) != 0;
}

bool save_cache(const WeatherProviderResult& source) {
    if (!ensure_nvs()) return false;
    CacheEnvelope envelope{};
    envelope.saved_epoch_seconds = current_epoch();
    envelope.result = source;
    envelope.result.snapshot.location = nullptr;
    nvs_handle_t handle = 0;
    auto status = nvs_open(kCacheNamespace, NVS_READWRITE, &handle);
    if (status == ESP_OK) {
        status = nvs_set_blob(handle, kCacheKey, &envelope, sizeof(envelope));
    }
    if (status == ESP_OK) status = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    if (status != ESP_OK) {
        ESP_LOGW(kTag, "cache save failed: %s", esp_err_to_name(status));
        return false;
    }
    return true;
}

bool load_cache(WeatherProviderResult& result) {
    if (!ensure_nvs()) return false;
    CacheEnvelope envelope{};
    std::size_t size = sizeof(envelope);
    nvs_handle_t handle = 0;
    auto status = nvs_open(kCacheNamespace, NVS_READONLY, &handle);
    if (status == ESP_OK) {
        status = nvs_get_blob(handle, kCacheKey, &envelope, &size);
    }
    if (handle != 0) nvs_close(handle);
    if (status != ESP_OK || size != sizeof(envelope) ||
        envelope.magic != kCacheMagic || envelope.version != kCacheVersion ||
        envelope.size != sizeof(envelope) ||
        std::memchr(envelope.result.location, '\0',
                    sizeof(envelope.result.location)) == nullptr) {
        return false;
    }
    result = envelope.result;
    result.freshness = 2;
    const auto now = current_epoch();
    result.snapshot.cache_age_minutes =
        now > 0 && envelope.saved_epoch_seconds > 0 &&
                now >= envelope.saved_epoch_seconds
            ? static_cast<std::uint64_t>(
                  (now - envelope.saved_epoch_seconds) / 60)
            : 0;
    describe_result(result);
    return snapshot_encodes(result);
}

void build_error_result(WeatherProviderResult& result) {
    result = {};
    copy_text(result.location, CONFIG_DOODAD_WEATHER_LOCATION_NAME);
    if (result.location[0] == '\0') copy_text(result.location, "Local weather");
    result.snapshot.location = result.location;
    result.snapshot.current.condition = 15;
    result.snapshot.hour_count = M3E_WEATHER_MAX_HOURS;
    for (std::size_t index = 0; index < result.snapshot.hour_count; ++index) {
        result.snapshot.hours[index].local_minute =
            static_cast<std::uint16_t>(index * 60);
        result.snapshot.hours[index].condition = 15;
    }
    result.snapshot.day_count = M3E_WEATHER_MAX_DAYS;
    for (std::size_t index = 0; index < result.snapshot.day_count; ++index) {
        result.snapshot.days[index].weekday = static_cast<std::uint8_t>(index);
        result.snapshot.days[index].condition = 15;
    }
    result.snapshot.minutes_until_rain = -1;
    result.snapshot.units =
#if defined(CONFIG_DOODAD_WEATHER_UNITS_IMPERIAL)
        1;
#else
        0;
#endif
    result.snapshot.data_revision = 1;
    result.freshness = 3;
    copy_text(result.condition, "Weather unavailable");
    copy_text(result.detail, "Connect Wi-Fi and retry");
}

void provider_worker(void*) {
    WeatherProviderResult result{};
    bool fresh = false;
    if (ensure_nvs() && network_service_connect()) {
        network_service_sync_time();
        HttpBuffer buffer{};
        buffer.capacity = kMaximumResponseBytes;
        buffer.bytes = static_cast<char*>(heap_caps_malloc(
            buffer.capacity + 1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer.bytes == nullptr) {
            buffer.bytes = static_cast<char*>(heap_caps_malloc(
                buffer.capacity + 1,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (buffer.bytes != nullptr) {
            buffer.bytes[0] = '\0';
            fresh = fetch_json(buffer) &&
                    normalize(buffer.bytes, buffer.size, result) &&
                    snapshot_encodes(result);
            heap_caps_free(buffer.bytes);
        }
    }
    if (fresh) {
        save_cache(result);
        ESP_LOGI(kTag, "fresh forecast ready for %s", result.location);
    } else if (load_cache(result)) {
        ESP_LOGW(
            kTag,
            "using offline cache for %s (%llu minutes old)",
            result.location,
            static_cast<unsigned long long>(result.snapshot.cache_age_minutes));
    } else {
        build_error_result(result);
        ESP_LOGW(kTag, "forecast and last-good cache unavailable");
    }
    result.snapshot.location = result.location;
    xQueueOverwrite(g_result_queue, &result);
    taskENTER_CRITICAL(&g_state_lock);
    g_busy = false;
    taskEXIT_CRITICAL(&g_state_lock);
    vTaskDelete(nullptr);
}

}  // namespace

bool weather_provider_init() {
    if (g_result_queue != nullptr) return true;
    g_result_queue = xQueueCreate(1, sizeof(WeatherProviderResult));
    if (g_result_queue == nullptr) {
        ESP_LOGE(kTag, "result queue allocation failed");
        return false;
    }
    ESP_LOGI(kTag, "ESP-IDF network provider enabled");
    return true;
}

bool weather_provider_request() {
    if (g_result_queue == nullptr && !weather_provider_init()) return false;
    taskENTER_CRITICAL(&g_state_lock);
    if (g_busy) {
        taskEXIT_CRITICAL(&g_state_lock);
        return false;
    }
    g_busy = true;
    taskEXIT_CRITICAL(&g_state_lock);
    if (xTaskCreate(
            provider_worker,
            "weather-provider",
            kWorkerStackBytes,
            nullptr,
            4,
            nullptr) != pdPASS) {
        taskENTER_CRITICAL(&g_state_lock);
        g_busy = false;
        taskEXIT_CRITICAL(&g_state_lock);
        return false;
    }
    return true;
}

bool weather_provider_poll(WeatherProviderResult& result) {
    if (g_result_queue == nullptr ||
        xQueueReceive(g_result_queue, &result, 0) != pdTRUE) {
        return false;
    }
    result.snapshot.location = result.location;
    return true;
}

bool weather_provider_busy() {
    taskENTER_CRITICAL(&g_state_lock);
    const auto busy = g_busy;
    taskEXIT_CRITICAL(&g_state_lock);
    return busy;
}

#else

bool weather_provider_init() { return true; }
bool weather_provider_request() { return false; }
bool weather_provider_poll(WeatherProviderResult&) { return false; }
bool weather_provider_busy() { return false; }

#endif
