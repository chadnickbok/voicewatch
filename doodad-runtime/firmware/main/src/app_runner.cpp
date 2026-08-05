#include "app_runner.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "bh_platform.h"
#include "display.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "m3e/appspec/command_batch.hpp"
#include "m3e/appspec/wire.hpp"
#include "m3e/services/exact_scheduler_c.h"
#include "m3e/services/provider_event_c.h"
#include "m3e/os/surface_registry.hpp"
#include "m3e/state/store.hpp"
#include "wasm_export.h"
#include "weather_provider.hpp"
#include "voice_service.hpp"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::uint32_t kWasmStackBytes = 16 * 1024;
constexpr std::uint32_t kWasmHeapBytes = 16 * 1024;
constexpr std::uint32_t kExecEnvStackBytes = 8 * 1024;
constexpr std::size_t kEventQueueDepth = 16;
constexpr std::uint32_t kMaximumTimerDurationMs =
    7 * 24 * 60 * 60 * 1000;
constexpr char kEmbeddedAppId[] = "system.embedded";

struct GuestEvent {
    std::uint64_t ui_owner_epoch;
    std::uint8_t schema;
    std::array<char, 65> app_id;
    std::array<char, 65> screen_id;
    std::array<char, 65> node_id;
    std::array<char, 65> action_id;
    m3e::appspec::EventKind kind;
    std::uint64_t timestamp_monotonic_ms;
    m3e::appspec::EventValueKind value_kind;
    std::int32_t integer_value;
    bool boolean_value;
    std::array<char, 65> text_value;
};

bool g_runtime_ready = false;
bool g_semantic_mount_called = false;
bool g_invalid_guest_message = false;
std::uint8_t* g_module_bytes = nullptr;
wasm_module_t g_module = nullptr;
wasm_module_inst_t g_instance = nullptr;
wasm_exec_env_t g_execution_environment = nullptr;
wasm_function_inst_t g_handle_event = nullptr;
wasm_function_inst_t g_handle_provider_event = nullptr;
QueueHandle_t g_event_queue = nullptr;
m3e::state::Store* g_app_store = nullptr;
m3e_exact_scheduler_handle g_scheduler = nullptr;
std::uint64_t g_provider_revision = 0;
std::uint64_t g_last_timer_publish_ms = 0;
std::uint64_t g_timer_surface_revision = 0;
std::uint64_t g_weather_request_id = 0;
std::uint64_t g_weather_surface_revision = 0;
bool g_weather_request_pending = false;
std::uint64_t g_weather_pending_owner_token = 0;
std::uint64_t g_weather_inflight_owner_token = 0;
[[maybe_unused]] std::uint8_t g_weather_cycle = 0;
AppRuntimeIdentity g_current_identity{};
bool g_has_current_identity = false;
bool g_guest_healthy = false;
std::uint64_t g_provider_owner_sequence = 0;
std::uint64_t g_current_provider_owner_token = 0;
AppRuntimeFailure g_pending_failure{};
bool g_failure_pending = false;
std::uint64_t g_failure_sequence = 0;
portMUX_TYPE g_ui_owner_lock = portMUX_INITIALIZER_UNLOCKED;
const char* g_active_ui_owner_pointer = nullptr;
std::uint64_t g_active_ui_owner_epoch = 0;
std::uint64_t g_ui_owner_epoch_sequence = 0;
std::atomic<bool> g_allow_legacy_event_alias{false};

void log_exception(wasm_module_inst_t module_instance);
std::uint64_t scenario_now_ms();

bool copy_identifier(
    std::array<char, 65>& destination,
    const char* source) {
    if (source == nullptr) return false;
    const auto length = std::strlen(source);
    if (length == 0 || length >= destination.size()) return false;
    std::memcpy(destination.data(), source, length + 1);
    return true;
}

bool copy_identity_text(
    char* destination,
    std::size_t capacity,
    const char* source,
    bool allow_empty) {
    if (destination == nullptr || capacity == 0 || source == nullptr) {
        return false;
    }
    const auto length = std::strlen(source);
    if ((!allow_empty && length == 0) || length >= capacity) {
        return false;
    }
    std::memcpy(destination, source, length + 1);
    return true;
}

bool identity_for_image(
    const AppImage& image,
    AppRuntimeIdentity& identity) {
    identity = {};
    const auto* app_id = image.app_id == nullptr
        ? kEmbeddedAppId
        : image.app_id;
    const auto* semantic_version =
        image.semantic_version == nullptr ? "" : image.semantic_version;
    const auto* generation =
        image.generation == nullptr ? "" : image.generation;
    return copy_identity_text(
               identity.app_id,
               sizeof(identity.app_id),
               app_id,
               false) &&
        copy_identity_text(
               identity.semantic_version,
               sizeof(identity.semantic_version),
               semantic_version,
               true) &&
        copy_identity_text(
               identity.generation,
               sizeof(identity.generation),
               generation,
               true);
}

void invalidate_active_ui_owner() {
    portENTER_CRITICAL(&g_ui_owner_lock);
    g_active_ui_owner_pointer = nullptr;
    g_active_ui_owner_epoch = 0;
    portEXIT_CRITICAL(&g_ui_owner_lock);
}

bool activate_ui_owner(
    const char* document_app_id,
    std::uint64_t& epoch) {
    epoch = 0;
    if (document_app_id == nullptr) return false;
    portENTER_CRITICAL(&g_ui_owner_lock);
    if (g_ui_owner_epoch_sequence !=
        std::numeric_limits<std::uint64_t>::max()) {
        epoch = ++g_ui_owner_epoch_sequence;
        g_active_ui_owner_pointer = document_app_id;
        g_active_ui_owner_epoch = epoch;
    }
    portEXIT_CRITICAL(&g_ui_owner_lock);
    return epoch != 0;
}

void invalidate_ui_owner_if(const char* document_app_id) {
    if (document_app_id == nullptr) return;
    portENTER_CRITICAL(&g_ui_owner_lock);
    if (g_active_ui_owner_pointer == document_app_id) {
        g_active_ui_owner_pointer = nullptr;
        g_active_ui_owner_epoch = 0;
    }
    portEXIT_CRITICAL(&g_ui_owner_lock);
}

bool snapshot_ui_owner(
    const m3e::appspec::UiEvent& event,
    bool trusted_legacy_alias,
    const char*& pointer,
    std::uint64_t& epoch) {
    pointer = nullptr;
    epoch = 0;
    portENTER_CRITICAL(&g_ui_owner_lock);
    const auto* active_pointer = g_active_ui_owner_pointer;
    const auto active_epoch = g_active_ui_owner_epoch;
    const bool matches = active_pointer != nullptr && active_epoch != 0 &&
        event.app_id != nullptr &&
        (event.app_id == active_pointer ||
         (trusted_legacy_alias &&
          g_allow_legacy_event_alias.load(std::memory_order_relaxed) &&
          std::strcmp(event.app_id, active_pointer) == 0));
    if (matches) {
        pointer = active_pointer;
        epoch = active_epoch;
    }
    portEXIT_CRITICAL(&g_ui_owner_lock);
    return matches;
}

bool ui_owner_still_active(
    const char* pointer,
    std::uint64_t epoch) {
    portENTER_CRITICAL(&g_ui_owner_lock);
    const bool active = pointer != nullptr && epoch != 0 &&
        g_active_ui_owner_pointer == pointer &&
        g_active_ui_owner_epoch == epoch;
    portEXIT_CRITICAL(&g_ui_owner_lock);
    return active;
}

void clear_current_identity() {
    invalidate_active_ui_owner();
    g_allow_legacy_event_alias.store(false, std::memory_order_release);
    g_current_identity = {};
    g_has_current_identity = false;
    g_guest_healthy = false;
    g_current_provider_owner_token = 0;
}

bool advance_provider_owner() {
    if (g_provider_owner_sequence ==
        std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    g_current_provider_owner_token = ++g_provider_owner_sequence;
    return true;
}

bool guest_provider_event_targets_current(std::uint64_t owner_token) {
    return owner_token != 0 &&
        owner_token == g_current_provider_owner_token;
}

bool voice_event_targets_current(std::uint64_t owner_token) {
    // Owner zero belongs to the trusted native voice surface, not whichever
    // guest happens to be resident when the event queue is drained.
    return guest_provider_event_targets_current(owner_token);
}

void clear_pending_failure() {
    g_pending_failure = {};
    g_failure_pending = false;
}

void latch_runtime_failure(AppRuntimeFailureKind kind) {
    if (!g_has_current_identity || !g_guest_healthy ||
        kind == AppRuntimeFailureKind::none) {
        return;
    }
    if (!g_failure_pending) {
        if (g_failure_sequence !=
            std::numeric_limits<std::uint64_t>::max()) {
            ++g_failure_sequence;
        }
        g_pending_failure.sequence = g_failure_sequence;
        g_pending_failure.kind = kind;
        g_pending_failure.identity = g_current_identity;
        g_failure_pending = true;
    }
    // Once a handler has failed, do not enter that WAMR instance again. The
    // runtime manager remains alive and can replace it with the prior
    // generation after consuming the bounded failure record.
    g_guest_healthy = false;
}

const char* current_scheduler_owner() {
    return g_has_current_identity
        ? g_current_identity.app_id
        : kEmbeddedAppId;
}

bool queued_event_targets_current_ui(const GuestEvent& event) {
    portENTER_CRITICAL(&g_ui_owner_lock);
    const bool active = g_active_ui_owner_pointer != nullptr &&
        event.ui_owner_epoch != 0 &&
        event.ui_owner_epoch == g_active_ui_owner_epoch;
    portEXIT_CRITICAL(&g_ui_owner_lock);
    return active;
}

template <std::size_t Size>
void set_text(
    std::array<char, Size>& destination,
    const char* source) {
    if (source == nullptr) return;
    std::strncpy(destination.data(), source, destination.size() - 1);
    destination.back() = '\0';
}

bool publish_timer_surfaces(
    const m3e_schedule_record& record,
    std::uint64_t now) {
    if (record.state != 1 && record.state != 2) return true;
    if (g_timer_surface_revision ==
        std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto remaining =
        record.state == 1 && record.deadline_scenario_ms > now
            ? record.deadline_scenario_ms - now
            : 0;
    const auto seconds = (remaining + 999) / 1'000;
    char duration[32]{};
    std::snprintf(
        duration,
        sizeof(duration),
        "%llu:%02llu",
        static_cast<unsigned long long>(seconds / 60),
        static_cast<unsigned long long>(seconds % 60));

    m3e::os::DomainSurfaceSnapshot snapshot{};
    set_text(snapshot.app_id, record.owner_app_id);
    snapshot.domain_revision = ++g_timer_surface_revision;
    snapshot.observed_at_ms = now;
    snapshot.declared_mask =
        m3e::os::surface_bit(m3e::os::SurfaceKind::app) |
        m3e::os::surface_bit(m3e::os::SurfaceKind::glance) |
        m3e::os::surface_bit(
            m3e::os::SurfaceKind::complication) |
        m3e::os::surface_bit(
            m3e::os::SurfaceKind::notification) |
        m3e::os::surface_bit(m3e::os::SurfaceKind::ongoing) |
        m3e::os::surface_bit(m3e::os::SurfaceKind::voice);
    for (auto& projection : snapshot.projections) {
        projection.revision = snapshot.domain_revision;
    }

    const bool firing = record.state == 2;
    auto& app = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::app)];
    app.active = true;
    set_text(app.primary, firing ? "TIME'S UP" : duration);
    set_text(
        app.secondary,
        firing ? "Timer complete" : "Running in background");

    auto& glance = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::glance)];
    glance.active = true;
    set_text(glance.primary, firing ? "Timer complete" : duration);
    set_text(glance.secondary, "Exact scheduler");
    set_text(
        glance.action_id,
        firing ? "timer.dismiss" : "timer.cancel");

    auto& complication = snapshot.projections[
        static_cast<std::size_t>(
            m3e::os::SurfaceKind::complication)];
    complication.active = true;
    set_text(
        complication.primary, firing ? "Done" : duration);

    auto& notification = snapshot.projections[
        static_cast<std::size_t>(
            m3e::os::SurfaceKind::notification)];
    notification.active = firing;
    if (firing) {
        set_text(notification.primary, "Timer complete");
        set_text(notification.secondary, "Your timer is ready");
        set_text(notification.action_id, "timer.dismiss");
    }

    auto& ongoing = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::ongoing)];
    ongoing.active = !firing;
    if (!firing) {
        set_text(ongoing.primary, "Timer");
        set_text(ongoing.secondary, duration);
        set_text(ongoing.action_id, "timer.cancel");
    }

    auto& voice = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::voice)];
    voice.active = true;
    set_text(
        voice.primary,
        firing ? "Dismiss my timer" : "Cancel my timer");
    set_text(
        voice.action_id,
        firing ? "timer.dismiss" : "timer.cancel");
    return display_publish_surfaces(snapshot);
}

bool publish_weather_surfaces(
    std::int32_t temperature_tenths,
    const char* condition,
    const char* detail,
    std::uint8_t freshness,
    std::uint64_t now) {
    if (freshness > 3 ||
        g_weather_surface_revision ==
            std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto rounded = temperature_tenths >= 0
        ? (temperature_tenths + 5) / 10
        : (temperature_tenths - 5) / 10;
    char temperature[24]{};
    std::snprintf(
        temperature,
        sizeof(temperature),
        "%ld°",
        static_cast<long>(rounded));

    m3e::os::DomainSurfaceSnapshot snapshot{};
    set_text(snapshot.app_id, "dev.doodad.weather");
    snapshot.domain_revision = ++g_weather_surface_revision;
    snapshot.observed_at_ms = now;
    snapshot.freshness =
        static_cast<m3e::os::Freshness>(freshness);
    snapshot.declared_mask =
        m3e::os::surface_bit(m3e::os::SurfaceKind::app) |
        m3e::os::surface_bit(m3e::os::SurfaceKind::glance) |
        m3e::os::surface_bit(
            m3e::os::SurfaceKind::complication) |
        m3e::os::surface_bit(
            m3e::os::SurfaceKind::notification) |
        m3e::os::surface_bit(m3e::os::SurfaceKind::voice);
    for (auto& projection : snapshot.projections) {
        if ((snapshot.declared_mask &
             (1U << (&projection - snapshot.projections.data()))) != 0) {
            projection.revision = snapshot.domain_revision;
        }
    }
    auto& app = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::app)];
    app.active = true;
    set_text(app.primary, temperature);
    set_text(app.secondary, condition);
    auto& glance = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::glance)];
    glance.active = true;
    set_text(glance.primary, temperature);
    set_text(glance.secondary, condition);
    set_text(glance.action_id, "weather.refresh");
    auto& complication = snapshot.projections[
        static_cast<std::size_t>(
            m3e::os::SurfaceKind::complication)];
    complication.active = true;
    set_text(complication.primary, temperature);
    set_text(complication.secondary, condition);
    auto& voice = snapshot.projections[
        static_cast<std::size_t>(m3e::os::SurfaceKind::voice)];
    voice.active = true;
    set_text(voice.primary, "Refresh the weather");
    set_text(voice.secondary, detail);
    set_text(voice.action_id, "weather.refresh");
    return display_publish_surfaces(snapshot);
}

void release_app() {
    // A request accepted but not yet handed to a provider belongs to the
    // outgoing instance. In-flight work keeps its token until its completion
    // is polled, at which point the new instance will discard it.
    g_weather_request_pending = false;
    g_weather_pending_owner_token = 0;
    if (g_current_provider_owner_token != 0 &&
        !voice_service_release_owner(
            g_current_provider_owner_token)) {
        ESP_LOGW(kTag, "[host] voice owner release queue full");
    }
    voice_service_set_current_guest_owner(0);
    g_handle_event = nullptr;
    g_handle_provider_event = nullptr;
    g_guest_healthy = false;
    if (g_execution_environment != nullptr) {
        wasm_runtime_destroy_exec_env(g_execution_environment);
        g_execution_environment = nullptr;
    }
    if (g_instance != nullptr) {
        wasm_runtime_deinstantiate(g_instance);
        g_instance = nullptr;
    }
    if (g_module != nullptr) {
        wasm_runtime_unload(g_module);
        g_module = nullptr;
    }
    std::free(g_module_bytes);
    g_module_bytes = nullptr;
    delete g_app_store;
    g_app_store = nullptr;
    clear_current_identity();
}

bool invoke_guest_handler(
    wasm_function_inst_t handler,
    const std::uint8_t* encoded,
    std::size_t encoded_size,
    const char* label) {
    if (g_instance == nullptr ||
        g_execution_environment == nullptr ||
        handler == nullptr ||
        encoded == nullptr ||
        encoded_size == 0 ||
        encoded_size > 1024) {
        ESP_LOGW(kTag, "[host] guest handler unavailable: %s", label);
        return false;
    }
    void* guest_native = nullptr;
    const auto guest_pointer = wasm_runtime_module_malloc(
        g_instance, encoded_size, &guest_native);
    if (guest_pointer == 0 || guest_native == nullptr ||
        guest_pointer > std::numeric_limits<std::uint32_t>::max()) {
        ESP_LOGE(kTag, "[host] guest event allocation failed");
        return false;
    }
    std::memcpy(guest_native, encoded, encoded_size);
    std::uint32_t arguments[2]{
        static_cast<std::uint32_t>(guest_pointer),
        static_cast<std::uint32_t>(encoded_size),
    };
    const bool called = wasm_runtime_call_wasm(
        g_execution_environment, handler, 2, arguments);
    wasm_runtime_module_free(g_instance, guest_pointer);
    if (!called) {
        log_exception(g_instance);
        ESP_LOGE(kTag, "[host] guest %s trapped", label);
        return false;
    }
    std::uint64_t packed_result = 0;
    std::memcpy(&packed_result, arguments, sizeof(packed_result));
    if (packed_result == 0) {
        // The guest may have navigated by mounting a new bounded AppSpec.
        return true;
    }
    const auto result_pointer =
        static_cast<std::uint32_t>(packed_result >> 32U);
    const auto result_length =
        static_cast<std::uint32_t>(packed_result);
    if (result_pointer == 0 || result_length == 0 ||
        result_length > m3e::appspec::kMaximumCommandBatchBytes) {
        ESP_LOGE(kTag, "[host] invalid guest CommandBatch slice");
        return false;
    }
    if (!wasm_runtime_validate_app_addr(
            g_instance, result_pointer, result_length)) {
        ESP_LOGE(kTag, "[host] CommandBatch pointer outside guest memory");
        return false;
    }
    const auto* result_bytes = static_cast<const std::uint8_t*>(
        wasm_runtime_addr_app_to_native(
            g_instance, result_pointer));
    if (result_bytes == nullptr) {
        ESP_LOGE(kTag, "[host] CommandBatch address translation failed");
        return false;
    }
    auto* batch =
        new (std::nothrow) m3e::appspec::CommandBatch{};
    if (batch == nullptr) {
        ESP_LOGE(kTag, "[host] CommandBatch allocation failed");
        return false;
    }
    const auto decoded =
        m3e::appspec::decode_command_batch_canonical_cbor(
            result_bytes, result_length, *batch);
    if (!decoded.ok()) {
        ESP_LOGE(
            kTag,
            "[host] CommandBatch rejected: %s byte=%u command=%u",
            m3e::appspec::command_error_name(decoded.error),
            static_cast<unsigned>(decoded.byte_offset),
            static_cast<unsigned>(decoded.command_index));
        delete batch;
        return false;
    }
    if (batch->domain == m3e::appspec::CommandDomain::ui) {
        if (!display_apply_command_batch(batch)) {
            ESP_LOGE(kTag, "[host] UI rejected CommandBatch");
            return false;
        }
    } else {
        if (g_app_store == nullptr) {
            g_app_store =
                new (std::nothrow) m3e::state::Store{};
            if (g_app_store == nullptr) {
                delete batch;
                ESP_LOGE(kTag, "[host] state Store allocation failed");
                return false;
            }
        }
        const auto applied =
            m3e::appspec::apply_state_command_batch(
                *batch, *g_app_store);
        delete batch;
        if (!applied.ok()) {
            ESP_LOGE(
                kTag,
                "[host] state CommandBatch rejected: %s command=%u",
                m3e::appspec::command_error_name(applied.error),
                static_cast<unsigned>(applied.command_index));
            return false;
        }
    }
    return true;
}

bool dispatch_event(const GuestEvent& event) {
    m3e::appspec::EventValue value{};
    switch (event.value_kind) {
        case m3e::appspec::EventValueKind::none:
            break;
        case m3e::appspec::EventValueKind::integer:
            value = m3e::appspec::EventValue::integer(
                event.integer_value);
            break;
        case m3e::appspec::EventValueKind::boolean:
            value = m3e::appspec::EventValue::boolean(
                event.boolean_value);
            break;
        case m3e::appspec::EventValueKind::text:
            value = m3e::appspec::EventValue::text(
                event.text_value.data());
            break;
    }
    const m3e::appspec::UiEvent envelope{
        event.schema,
        event.app_id.data(),
        event.screen_id.data(),
        event.node_id.data(),
        event.action_id.data(),
        event.kind,
        event.timestamp_monotonic_ms,
        value,
    };
    std::array<std::uint8_t, 512> encoded{};
    const auto encoded_size =
        m3e::appspec::encode_event_canonical_cbor(
            envelope, encoded.data(), encoded.size());
    if (encoded_size == 0) {
        ESP_LOGE(kTag, "[host] failed to encode semantic UI event");
        return false;
    }
    return invoke_guest_handler(
        g_handle_event,
        encoded.data(),
        encoded_size,
        "handle_event");
}

bool deliver_weather_snapshot(
    m3e_weather_snapshot_v2& snapshot,
    std::uint8_t freshness,
    std::int32_t temperature,
    const char* condition,
    const char* detail) {
    if (g_handle_provider_event == nullptr ||
        g_provider_revision ==
            std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto now = scenario_now_ms();
    std::array<std::uint8_t, 512> encoded{};
    const auto encoded_size =
        m3e_encode_weather_provider_event_v2(
            &snapshot,
            ++g_provider_revision,
            freshness,
            now,
            encoded.data(),
            encoded.size());
    return encoded_size != 0 &&
           invoke_guest_handler(
               g_handle_provider_event,
               encoded.data(),
               encoded_size,
               "handle_provider_event") &&
           publish_weather_surfaces(
               temperature,
               condition,
               detail,
               freshness,
               now);
}

bool deliver_weather_provider() {
#if defined(CONFIG_DOODAD_WEATHER_NETWORK_PROVIDER) && \
    CONFIG_DOODAD_WEATHER_NETWORK_PROVIDER
    WeatherProviderResult delivery{};
    if (weather_provider_poll(delivery)) {
        const auto owner_token = g_weather_inflight_owner_token;
        g_weather_inflight_owner_token = 0;
        if (!guest_provider_event_targets_current(owner_token)) {
            ESP_LOGW(kTag, "[host] dropped stale weather completion");
            return true;
        }
        auto snapshot = delivery.snapshot;
        snapshot.location = delivery.location;
        return deliver_weather_snapshot(
            snapshot,
            delivery.freshness,
            snapshot.current.temperature_tenths,
            delivery.condition,
            delivery.detail);
    }
    if (g_weather_request_pending) {
        const auto owner_token = g_weather_pending_owner_token;
        g_weather_request_pending = false;
        g_weather_pending_owner_token = 0;
        if (!guest_provider_event_targets_current(owner_token)) {
            ESP_LOGW(kTag, "[host] discarded stale weather request");
            return true;
        }
        if (!weather_provider_request()) return false;
        g_weather_inflight_owner_token = owner_token;
    }
    return true;
#else
    if (!g_weather_request_pending) return true;
    const auto owner_token = g_weather_pending_owner_token;
    g_weather_request_pending = false;
    g_weather_pending_owner_token = 0;
    if (!guest_provider_event_targets_current(owner_token)) {
        ESP_LOGW(kTag, "[host] discarded stale weather request");
        return true;
    }
    if (g_handle_provider_event == nullptr ||
        g_provider_revision ==
            std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    g_weather_cycle =
        static_cast<std::uint8_t>((g_weather_cycle + 1) % 3);

    m3e_weather_snapshot_v2 snapshot{};
    snapshot.location = "San Francisco";
    snapshot.local_weekday = 6;
    snapshot.local_minute = 609;
    snapshot.current.temperature_tenths = 620;
    snapshot.current.feels_like_tenths = 590;
    snapshot.current.high_tenths = 670;
    snapshot.current.low_tenths = 540;
    snapshot.current.condition = 2;
    snapshot.current.precipitation_percent = 0;
    snapshot.current.humidity_percent = 49;
    snapshot.current.wind_speed_tenths = 80;
    snapshot.current.wind_direction_degrees = 270;
    snapshot.current.uv_index_tenths = 30;
    snapshot.current.sunrise_local_minute = 372;
    snapshot.current.sunset_local_minute = 1205;
    snapshot.current.has_feels_like = 1;
    snapshot.current.has_high = 1;
    snapshot.current.has_low = 1;
    snapshot.current.has_precipitation = 1;
    snapshot.current.has_humidity = 1;
    snapshot.current.has_wind_speed = 1;
    snapshot.current.has_wind_direction = 1;
    snapshot.current.has_uv_index = 1;
    snapshot.current.has_sunrise = 1;
    snapshot.current.has_sunset = 1;
    snapshot.hour_count = 7;
    constexpr std::array<std::int32_t, 7> temperatures{
        620, 630, 650, 660, 670, 660, 640};
    constexpr std::array<std::uint8_t, 7> conditions{2, 2, 0, 0, 0, 2, 2};
    for (std::size_t index = 0; index < snapshot.hour_count; ++index) {
        snapshot.hours[index].local_minute =
            static_cast<std::uint16_t>(609 + index * 60);
        snapshot.hours[index].temperature_tenths = temperatures[index];
        snapshot.hours[index].precipitation_percent = 0;
        snapshot.hours[index].condition = conditions[index];
        snapshot.hours[index].has_precipitation = 1;
    }
    snapshot.day_count = 4;
    constexpr std::array<std::uint8_t, 4> day_conditions{2, 2, 8, 2};
    constexpr std::array<std::uint8_t, 4> day_precipitation{0, 5, 30, 10};
    constexpr std::array<std::int32_t, 4> day_lows{540, 530, 510, 520};
    constexpr std::array<std::int32_t, 4> day_highs{670, 650, 630, 640};
    for (std::size_t index = 0; index < snapshot.day_count; ++index) {
        snapshot.days[index].weekday =
            static_cast<std::uint8_t>((6 + index) % 7);
        snapshot.days[index].low_tenths = day_lows[index];
        snapshot.days[index].high_tenths = day_highs[index];
        snapshot.days[index].precipitation_percent = day_precipitation[index];
        snapshot.days[index].condition = day_conditions[index];
        snapshot.days[index].has_precipitation = 1;
    }
    snapshot.minutes_until_rain = -1;
    snapshot.rain_duration_minutes = 0;
    snapshot.units = 1;
    snapshot.data_revision = 1;
    snapshot.cache_age_minutes = 12;
    std::uint8_t freshness = 1;
    if (g_weather_cycle == 2) {
        snapshot.cache_age_minutes = 18;
        freshness = 2;
    } else if (g_weather_cycle == 0) {
        snapshot.current.temperature_tenths = 610;
        snapshot.current.high_tenths = 660;
        snapshot.current.low_tenths = 530;
        snapshot.hours[0].temperature_tenths = 610;
        snapshot.days[0].low_tenths = 530;
        snapshot.days[0].high_tenths = 660;
        snapshot.data_revision = 2;
        snapshot.cache_age_minutes = 0;
        freshness = 0;
    }
    return deliver_weather_snapshot(
        snapshot,
        freshness,
        snapshot.current.temperature_tenths,
        "Partly cloudy",
        "High 67 - Low 54 - Feels 59");
#endif
}

bool deliver_voice_provider() {
    VoiceEvent delivery{};
    while (voice_service_poll(delivery)) {
        if (delivery.owner_token == 0) {
            // Native voice state is rendered directly by voice_service and
            // display. It is intentionally consumed here without entering
            // an unrelated resident Wasm guest.
            continue;
        }
        if (!voice_event_targets_current(delivery.owner_token)) {
            ESP_LOGW(kTag, "[host] dropped stale voice completion");
            continue;
        }
        if (g_handle_provider_event == nullptr) continue;
        if (g_provider_revision ==
            std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        std::array<std::uint8_t, 512> encoded{};
        const auto encoded_size = m3e_encode_voice_provider_event(
            static_cast<std::uint8_t>(delivery.kind),
            delivery.request_id,
            delivery.elapsed_ms,
            delivery.encoded_frames,
            delivery.dropped_frames,
            delivery.text.data(),
            ++g_provider_revision,
            scenario_now_ms(),
            encoded.data(),
            encoded.size());
        if (encoded_size == 0 ||
            !invoke_guest_handler(
                g_handle_provider_event,
                encoded.data(),
                encoded_size,
                "handle_provider_event")) {
            return false;
        }
    }
    return true;
}

std::int32_t reject_guest_appspec(
    wasm_module_inst_t module_instance,
    const char* reason) {
    ESP_LOGE(kTag, "[host] invalid guest AppSpec: %s", reason);
    g_invalid_guest_message = true;
    display_error("INVALID APPSPEC");
    wasm_runtime_set_exception(module_instance, "INVALID APPSPEC");
    return 0;
}

std::int32_t host_ui_mount(
    wasm_exec_env_t execution_environment,
    std::uint32_t guest_pointer,
    std::uint32_t guest_length) {
    wasm_module_inst_t module_instance =
        wasm_runtime_get_module_inst(execution_environment);

    if (guest_length == 0 ||
        guest_length > m3e::appspec::kMaximumWireBytes) {
        return reject_guest_appspec(
            module_instance, "length outside 1..4096 bytes");
    }
    if (!wasm_runtime_validate_app_addr(
            module_instance, guest_pointer, guest_length)) {
        return reject_guest_appspec(
            module_instance, "pointer outside guest linear memory");
    }

    const auto* guest_bytes = static_cast<const std::uint8_t*>(
        wasm_runtime_addr_app_to_native(module_instance, guest_pointer));
    if (guest_bytes == nullptr) {
        return reject_guest_appspec(
            module_instance, "guest address translation failed");
    }
    auto* document =
        new (std::nothrow) m3e::appspec::WireDocument{};
    if (document == nullptr) {
        return reject_guest_appspec(
            module_instance, "host document allocation failed");
    }
    const auto decoded = m3e::appspec::decode_canonical_cbor(
        guest_bytes, guest_length, *document);
    if (!decoded.ok()) {
        ESP_LOGE(
            kTag,
            "[host] AppSpec decode failed: %s byte=%u node=%u",
            m3e::appspec::wire_error_name(decoded.error),
            static_cast<unsigned>(decoded.byte_offset),
            static_cast<unsigned>(decoded.node_index));
        delete document;
        return reject_guest_appspec(
            module_instance, "canonical CBOR validation failed");
    }
    const auto node_count = document->node_count;
    const auto* document_app_id =
        document->string_at(document->app_id_offset);
    std::uint64_t ui_owner_epoch = 0;
    if (!activate_ui_owner(document_app_id, ui_owner_epoch)) {
        delete document;
        return reject_guest_appspec(
            module_instance, "UI ownership sequence exhausted");
    }
    if (!display_mount_appspec(document)) {
        invalidate_ui_owner_if(document_app_id);
        return reject_guest_appspec(
            module_instance, "UI renderer rejected mount");
    }
    ESP_LOGI(
        kTag,
        "[guest] ui_mount: %u bytes, %u nodes",
        static_cast<unsigned>(guest_length),
        static_cast<unsigned>(node_count));
    g_semantic_mount_called = true;
    return 1;
}

std::uint64_t scenario_now_ms() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

bool copy_guest_service_id(
    wasm_exec_env_t execution_environment,
    std::uint32_t guest_pointer,
    std::uint32_t guest_length,
    std::array<char, 49>& output) {
    if (guest_length == 0 || guest_length >= output.size()) return false;
    auto instance =
        wasm_runtime_get_module_inst(execution_environment);
    if (!wasm_runtime_validate_app_addr(
            instance, guest_pointer, guest_length)) {
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(
        wasm_runtime_addr_app_to_native(instance, guest_pointer));
    if (bytes == nullptr) return false;
    for (std::uint32_t index = 0; index < guest_length; ++index) {
        const auto byte = bytes[index];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '-' || byte == '_')) {
            return false;
        }
    }
    std::memcpy(output.data(), bytes, guest_length);
    output[guest_length] = '\0';
    return true;
}

std::uint64_t host_timer_schedule_after(
    wasm_exec_env_t execution_environment,
    std::uint32_t guest_pointer,
    std::uint32_t guest_length,
    std::uint32_t duration_ms) {
    std::array<char, 49> id{};
    if (g_scheduler == nullptr ||
        duration_ms == 0 ||
        duration_ms > kMaximumTimerDurationMs ||
        !copy_guest_service_id(
            execution_environment,
            guest_pointer,
            guest_length,
            id)) {
        return 0;
    }
    return m3e_exact_scheduler_schedule_after_for_app(
        g_scheduler,
        current_scheduler_owner(),
        id.data(),
        duration_ms,
        scenario_now_ms());
}

std::int32_t host_timer_cancel(
    wasm_exec_env_t execution_environment,
    std::uint32_t guest_pointer,
    std::uint32_t guest_length) {
    std::array<char, 49> id{};
    if (g_scheduler == nullptr ||
        !copy_guest_service_id(
            execution_environment,
            guest_pointer,
            guest_length,
            id)) {
        return 0;
    }
    return m3e_exact_scheduler_cancel_for_app(
        g_scheduler, current_scheduler_owner(), id.data());
}

std::int32_t host_timer_acknowledge(
    wasm_exec_env_t execution_environment,
    std::uint32_t guest_pointer,
    std::uint32_t guest_length) {
    std::array<char, 49> id{};
    if (g_scheduler == nullptr ||
        !copy_guest_service_id(
            execution_environment,
            guest_pointer,
            guest_length,
            id)) {
        return 0;
    }
    return m3e_exact_scheduler_acknowledge_for_app(
        g_scheduler, current_scheduler_owner(), id.data());
}

std::uint64_t host_provider_request(
    wasm_exec_env_t execution_environment,
    std::uint32_t provider_pointer,
    std::uint32_t provider_length,
    std::uint32_t operation_pointer,
    std::uint32_t operation_length,
    std::uint32_t payload_pointer,
    std::uint32_t payload_length) {
    std::array<char, 49> provider{};
    std::array<char, 49> operation{};
    auto instance =
        wasm_runtime_get_module_inst(execution_environment);
    if (g_weather_request_id ==
            std::numeric_limits<std::uint64_t>::max() ||
        !copy_guest_service_id(
            execution_environment,
            provider_pointer,
            provider_length,
            provider) ||
        !copy_guest_service_id(
            execution_environment,
            operation_pointer,
            operation_length,
            operation) ||
        payload_length > 512 ||
        (payload_length != 0 &&
         !wasm_runtime_validate_app_addr(
             instance, payload_pointer, payload_length))) {
        return 0;
    }
    const auto weather =
        std::strcmp(provider.data(), "weather") == 0 &&
        std::strcmp(operation.data(), "refresh") == 0;
    const auto fixture =
        std::strcmp(provider.data(), "fixture") == 0;
    if ((!weather && !fixture) ||
        (weather &&
         (g_weather_request_pending || weather_provider_busy()))) {
        return 0;
    }
    if (weather) {
        g_weather_request_pending = true;
        g_weather_pending_owner_token =
            g_current_provider_owner_token;
    }
    return ++g_weather_request_id;
}

#define DEFINE_BOUND_PROVIDER_REQUEST(                                \
    function_name, prefix_one, prefix_two)                            \
    std::uint64_t function_name(                                      \
        wasm_exec_env_t execution_environment,                        \
        std::uint32_t operation_pointer,                              \
        std::uint32_t operation_length,                               \
        std::uint32_t payload_pointer,                                \
        std::uint32_t payload_length) {                               \
        std::array<char, 49> operation{};                             \
        auto instance = wasm_runtime_get_module_inst(                 \
            execution_environment);                                  \
        if (g_weather_request_id ==                                   \
                std::numeric_limits<std::uint64_t>::max() ||          \
            !copy_guest_service_id(                                   \
                execution_environment, operation_pointer,            \
                operation_length, operation) ||                       \
            payload_length > 512 ||                                   \
            (payload_length != 0 &&                                   \
             !wasm_runtime_validate_app_addr(                         \
                 instance, payload_pointer, payload_length))) {       \
            return 0;                                                 \
        }                                                             \
        const auto prefix_one_ok =                                    \
            std::strncmp(                                             \
                operation.data(), prefix_one,                         \
                std::strlen(prefix_one)) == 0;                        \
        const auto prefix_two_ok =                                    \
            prefix_two[0] != '\0' &&                                  \
            std::strncmp(                                             \
                operation.data(), prefix_two,                         \
                std::strlen(prefix_two)) == 0;                        \
        if (!prefix_one_ok && !prefix_two_ok) return 0;                \
        return ++g_weather_request_id;                                \
    }

DEFINE_BOUND_PROVIDER_REQUEST(
    host_calendar_request, "calendar.", "")

std::uint64_t host_audio_request(
    wasm_exec_env_t execution_environment,
    std::uint32_t operation_pointer,
    std::uint32_t operation_length,
    std::uint32_t payload_pointer,
    std::uint32_t payload_length) {
    std::array<char, 49> operation{};
    auto instance = wasm_runtime_get_module_inst(execution_environment);
    if (g_weather_request_id ==
            std::numeric_limits<std::uint64_t>::max() ||
        !copy_guest_service_id(
            execution_environment,
            operation_pointer,
            operation_length,
            operation) ||
        payload_length > 512 ||
        (payload_length != 0 &&
         !wasm_runtime_validate_app_addr(
             instance, payload_pointer, payload_length)) ||
        std::strncmp(operation.data(), "voice-notes.", 12) != 0) {
        return 0;
    }
    const auto request_id = ++g_weather_request_id;
    return voice_service_request(
               operation.data(),
               request_id,
               8'000,
               g_current_provider_owner_token)
        ? request_id : 0;
}

DEFINE_BOUND_PROVIDER_REQUEST(
    host_medication_request, "medication.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_sensor_request, "sensor.", "sensor-recorder.")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_sleep_request, "sleep.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_media_request, "media.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_navigation_request, "navigation.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_transit_request, "transit.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_home_request, "home.", "smart-home.")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_sports_request, "sports.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_wallet_request, "wallet.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_remote_request, "remote.", "remote-control.")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_workout_request, "workout.", "complete_set")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_game_request, "snake.", "")

#undef DEFINE_BOUND_PROVIDER_REQUEST

NativeSymbol g_native_symbols[] = {
    {
        .symbol = "ui_mount",
        .func_ptr = reinterpret_cast<void*>(host_ui_mount),
        .signature = "(ii)i",
        .attachment = nullptr,
    },
    {
        .symbol = "timer_schedule_after",
        .func_ptr =
            reinterpret_cast<void*>(host_timer_schedule_after),
        .signature = "(iii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "timer_cancel",
        .func_ptr = reinterpret_cast<void*>(host_timer_cancel),
        .signature = "(ii)i",
        .attachment = nullptr,
    },
    {
        .symbol = "timer_acknowledge",
        .func_ptr =
            reinterpret_cast<void*>(host_timer_acknowledge),
        .signature = "(ii)i",
        .attachment = nullptr,
    },
    {
        .symbol = "provider_request",
        .func_ptr =
            reinterpret_cast<void*>(host_provider_request),
        .signature = "(iiiiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "calendar_request",
        .func_ptr =
            reinterpret_cast<void*>(host_calendar_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "audio_request",
        .func_ptr = reinterpret_cast<void*>(host_audio_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "medication_request",
        .func_ptr =
            reinterpret_cast<void*>(host_medication_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "sensor_request",
        .func_ptr = reinterpret_cast<void*>(host_sensor_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "sleep_request",
        .func_ptr = reinterpret_cast<void*>(host_sleep_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "media_request",
        .func_ptr = reinterpret_cast<void*>(host_media_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "navigation_request",
        .func_ptr =
            reinterpret_cast<void*>(host_navigation_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "transit_request",
        .func_ptr = reinterpret_cast<void*>(host_transit_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "home_request",
        .func_ptr = reinterpret_cast<void*>(host_home_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "sports_request",
        .func_ptr = reinterpret_cast<void*>(host_sports_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "wallet_request",
        .func_ptr = reinterpret_cast<void*>(host_wallet_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "remote_request",
        .func_ptr = reinterpret_cast<void*>(host_remote_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "workout_request",
        .func_ptr =
            reinterpret_cast<void*>(host_workout_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
    {
        .symbol = "game_request",
        .func_ptr = reinterpret_cast<void*>(host_game_request),
        .signature = "(iiii)I",
        .attachment = nullptr,
    },
};

void log_exception(wasm_module_inst_t module_instance) {
    const char* exception = wasm_runtime_get_exception(module_instance);
    if (exception != nullptr) {
        ESP_LOGE(kTag, "[host] WAMR exception: %s", exception);
    }
}

void* wamr_psram_malloc(unsigned int size) {
    auto* allocation = heap_caps_aligned_alloc(
        8,
        size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (allocation == nullptr) {
        allocation = heap_caps_aligned_alloc(
            8,
            size,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return allocation;
}

void wamr_psram_free(void* allocation) {
    heap_caps_free(allocation);
}

}  // namespace

bool app_runtime_init() {
    g_event_queue = xQueueCreate(kEventQueueDepth, sizeof(GuestEvent));
    if (g_event_queue == nullptr) {
        ESP_LOGE(kTag, "[host] semantic event queue allocation failed");
        display_error("WAMR INIT FAILED");
        return false;
    }
    g_scheduler = m3e_exact_scheduler_create();
    if (g_scheduler == nullptr) {
        ESP_LOGE(kTag, "[host] exact scheduler allocation failed");
        display_error("SCHEDULER INIT FAILED");
        return false;
    }
    if (!weather_provider_init()) {
        ESP_LOGE(kTag, "[host] weather provider initialization failed");
        display_error("PROVIDER INIT FAILED");
        return false;
    }
    if (!voice_service_init()) {
        ESP_LOGE(kTag, "[host] voice service initialization failed");
        display_error("VOICE INIT FAILED");
        return false;
    }
    RuntimeInitArgs arguments{};
    arguments.mem_alloc_type = Alloc_With_Allocator;
    arguments.mem_alloc_option.allocator.malloc_func =
        reinterpret_cast<void*>(wamr_psram_malloc);
    // ESP-IDF's aligned os_realloc corrupts WAMR's loader control stack when
    // it grows. A null realloc makes WAMR use its safe allocate/copy/free
    // fallback while retaining the aligned platform allocator.
    arguments.mem_alloc_option.allocator.realloc_func = nullptr;
    arguments.mem_alloc_option.allocator.free_func =
        reinterpret_cast<void*>(wamr_psram_free);

    if (!wasm_runtime_full_init(&arguments)) {
        ESP_LOGE(kTag, "[host] WAMR init failed");
        display_error("WAMR INIT FAILED");
        return false;
    }
    if (!wasm_runtime_register_natives(
            "doodad", g_native_symbols,
            sizeof(g_native_symbols) / sizeof(g_native_symbols[0]))) {
        ESP_LOGE(kTag, "[host] native ABI registration failed");
        display_error("WAMR INIT FAILED");
        wasm_runtime_destroy();
        return false;
    }

    g_runtime_ready = true;
    ESP_LOGI(kTag, "[host] WAMR ready (interpreter, stack=%u, heap=%u)",
             static_cast<unsigned>(kWasmStackBytes),
             static_cast<unsigned>(kWasmHeapBytes));
    return true;
}

bool run_app(const AppImage& image) {
    if (!g_runtime_ready || image.data == nullptr || image.size == 0) {
        ESP_LOGE(kTag, "[host] invalid app image");
        display_error("MODULE LOAD FAILED");
        return false;
    }
    AppRuntimeIdentity candidate_identity{};
    if (!identity_for_image(image, candidate_identity)) {
        ESP_LOGE(kTag, "[host] invalid app generation identity");
        display_error("MODULE LOAD FAILED");
        return false;
    }

    char error_buffer[192]{};
    const auto* source = image.source == nullptr ? "UNKNOWN" : image.source;
    ESP_LOGI(kTag, "[host] %s app size: %u bytes", source,
             static_cast<unsigned>(image.size));

    // A switch establishes a new failure epoch. Any report not consumed for
    // the outgoing instance must not be mistaken for the incoming generation.
    clear_pending_failure();
    release_app();
    g_module_bytes = static_cast<std::uint8_t*>(std::malloc(image.size));
    if (g_module_bytes == nullptr) {
        ESP_LOGE(kTag, "[host] module buffer allocation failed: %u bytes",
                 static_cast<unsigned>(image.size));
        display_error("MODULE LOAD FAILED");
        return false;
    }
    std::memcpy(g_module_bytes, image.data, image.size);

    g_module = wasm_runtime_load(
        g_module_bytes,
        static_cast<std::uint32_t>(image.size),
        error_buffer,
        sizeof(error_buffer));
    if (g_module == nullptr) {
        ESP_LOGE(kTag, "[host] module load failed: %s", error_buffer);
        display_error("MODULE LOAD FAILED");
        release_app();
        return false;
    }
    ESP_LOGI(kTag, "[host] module loaded");

    g_instance = wasm_runtime_instantiate(
        g_module,
        kWasmStackBytes,
        kWasmHeapBytes,
        error_buffer,
        sizeof(error_buffer));
    if (g_instance == nullptr) {
        ESP_LOGE(kTag, "[host] module instantiate failed: %s", error_buffer);
        display_error("MODULE INSTANTIATE FAILED");
        release_app();
        return false;
    }
    ESP_LOGI(kTag, "[host] module instantiated");

    wasm_function_inst_t app_start =
        wasm_runtime_lookup_function(g_instance, "app_start");
    if (app_start == nullptr) {
        ESP_LOGE(kTag, "[host] app_start export not found");
        display_error("APP_START NOT FOUND");
        release_app();
        return false;
    }

    g_execution_environment =
        wasm_runtime_create_exec_env(g_instance, kExecEnvStackBytes);
    if (g_execution_environment == nullptr) {
        ESP_LOGE(kTag, "[host] execution environment allocation failed");
        display_error("MODULE INSTANTIATE FAILED");
        release_app();
        return false;
    }
    g_handle_event =
        wasm_runtime_lookup_function(g_instance, "handle_event");
    g_handle_provider_event =
        wasm_runtime_lookup_function(
            g_instance, "handle_provider_event");
    xQueueReset(g_event_queue);

    if (!advance_provider_owner()) {
        ESP_LOGE(kTag, "[host] provider ownership sequence exhausted");
        display_error("MODULE LOAD FAILED");
        release_app();
        return false;
    }
    voice_service_set_current_guest_owner(
        g_current_provider_owner_token);
    g_current_identity = candidate_identity;
    g_has_current_identity = true;
    g_guest_healthy = true;
    g_allow_legacy_event_alias.store(
        image.app_id == nullptr, std::memory_order_release);
    display_shell("WASM RUNNING", source);
    ESP_LOGI(kTag, "[host] invoking app_start");
    g_semantic_mount_called = false;
    g_invalid_guest_message = false;
    const bool call_succeeded =
        wasm_runtime_call_wasm(g_execution_environment, app_start, 0, nullptr);

    bool succeeded = call_succeeded && g_semantic_mount_called;
    if (!call_succeeded) {
        log_exception(g_instance);
        if (!g_invalid_guest_message) {
            display_error("GUEST TRAP");
        }
    } else if (!g_semantic_mount_called) {
        ESP_LOGE(kTag, "[host] app returned without mounting AppSpec");
        display_error("GUEST TRAP");
        succeeded = false;
    } else {
        ESP_LOGI(kTag, "[host] app started; instance remains resident");
    }

    if (!succeeded) {
        release_app();
    }
    return succeeded;
}

bool app_runtime_current_identity(AppRuntimeIdentity& identity) {
    identity = {};
    if (!g_has_current_identity) return false;
    identity = g_current_identity;
    return true;
}

bool app_runtime_poll_failure(AppRuntimeFailure& failure) {
    failure = {};
    if (!g_failure_pending) return false;
    failure = g_pending_failure;
    clear_pending_failure();
    return true;
}

void app_runtime_invalidate_ui_mount(const char* document_app_id) {
    invalidate_ui_owner_if(document_app_id);
}

namespace {

bool post_ui_event(
    const m3e::appspec::UiEvent& event,
    bool trusted_legacy_alias) {
    if (g_event_queue == nullptr) return false;
    const char* ui_owner_pointer = nullptr;
    std::uint64_t ui_owner_epoch = 0;
    if (!snapshot_ui_owner(
            event,
            trusted_legacy_alias,
            ui_owner_pointer,
            ui_owner_epoch)) {
        // A live switch can leave the outgoing LVGL tree visible until the
        // new mount command is processed. Treat its callbacks as consumed,
        // but never queue or attribute them to the incoming generation.
        ESP_LOGW(kTag, "[host] dropped stale semantic UI event");
        return true;
    }
    // Origin validation deliberately precedes semantic validation: callbacks
    // from a retired document can contain freed string pointers, which must
    // never be dereferenced merely to decide that they are stale.
    if (!m3e::appspec::event_is_valid(event)) return false;
    GuestEvent copy{};
    copy.ui_owner_epoch = ui_owner_epoch;
    copy.schema = event.schema;
    copy.kind = event.kind;
    copy.timestamp_monotonic_ms = event.timestamp_monotonic_ms;
    copy.value_kind = event.value.kind;
    copy.integer_value = event.value.integer_value;
    copy.boolean_value = event.value.boolean_value;
    if (!copy_identifier(copy.app_id, event.app_id) ||
        !copy_identifier(copy.screen_id, event.screen_id) ||
        !copy_identifier(copy.node_id, event.node_id) ||
        !copy_identifier(copy.action_id, event.action_id)) {
        return false;
    }
    if (event.value.kind == m3e::appspec::EventValueKind::text) {
        const auto length = std::strlen(event.value.text_value);
        if (length == 0 || length >= copy.text_value.size()) {
            return false;
        }
        std::memcpy(
            copy.text_value.data(),
            event.value.text_value,
            length + 1);
    }
    // The UI task can retire a document while another producer is copying a
    // direct event. Recheck the coherent pointer/epoch pair before enqueueing;
    // queued delivery later compares the monotonic epoch only, so allocator
    // address reuse can never grant an old event to a new mount.
    if (!ui_owner_still_active(ui_owner_pointer, ui_owner_epoch)) {
        ESP_LOGW(kTag, "[host] dropped semantic UI event during remount");
        return true;
    }
    if (xQueueSend(g_event_queue, &copy, 0) != pdTRUE) {
        ESP_LOGE(kTag, "[host] semantic event queue overflow");
        return false;
    }
    return true;
}

}  // namespace

bool app_post_ui_event(const m3e::appspec::UiEvent& event) {
    return post_ui_event(event, false);
}

bool app_post_embedded_ui_event(const m3e::appspec::UiEvent& event) {
    return post_ui_event(event, true);
}

void app_runtime_update(std::uint32_t maximum_wait_ms) {
    if (g_event_queue == nullptr || !g_has_current_identity ||
        !g_guest_healthy) {
        return;
    }
    GuestEvent event{};
    if (xQueueReceive(
            g_event_queue,
            &event,
            pdMS_TO_TICKS(maximum_wait_ms)) == pdTRUE) {
        if (!queued_event_targets_current_ui(event)) {
            ESP_LOGW(kTag, "[host] discarded queued stale UI event");
        } else if (!dispatch_event(event)) {
            ESP_LOGE(kTag, "[host] semantic UI event delivery failed");
            latch_runtime_failure(AppRuntimeFailureKind::ui_event);
            return;
        }
        while (xQueueReceive(g_event_queue, &event, 0) == pdTRUE) {
            if (!queued_event_targets_current_ui(event)) {
                ESP_LOGW(
                    kTag,
                    "[host] discarded queued stale UI event");
                continue;
            }
            if (!dispatch_event(event)) {
                ESP_LOGE(
                    kTag,
                    "[host] semantic UI event delivery failed");
                latch_runtime_failure(AppRuntimeFailureKind::ui_event);
                return;
            }
        }
    }
    if (!deliver_weather_provider()) {
        ESP_LOGE(kTag, "[host] weather provider delivery failed");
        latch_runtime_failure(AppRuntimeFailureKind::provider_event);
        return;
    }
    if (!deliver_voice_provider()) {
        ESP_LOGE(kTag, "[host] voice provider delivery failed");
        latch_runtime_failure(AppRuntimeFailureKind::provider_event);
        return;
    }

    if (g_scheduler == nullptr) return;
    const auto now = scenario_now_ms();
    m3e_due_delivery due[8]{};
    const auto due_count = m3e_exact_scheduler_poll(
        g_scheduler, now, due, 8);
    if (due_count == 0 &&
        now - g_last_timer_publish_ms < 1'000) {
        return;
    }
    g_last_timer_publish_ms = now;
    if (g_handle_provider_event == nullptr) return;

    m3e_schedule_record records[8]{};
    const auto count = m3e_exact_scheduler_records_for_app(
        g_scheduler,
        current_scheduler_owner(),
        records,
        8,
        now);
    for (std::size_t index = 0; index < count; ++index) {
        if (records[index].state != 1 &&
            records[index].state != 2) {
            continue;
        }
        if (g_provider_revision ==
            std::numeric_limits<std::uint64_t>::max()) {
            ESP_LOGE(kTag, "[host] provider revision overflow");
            latch_runtime_failure(AppRuntimeFailureKind::timer_event);
            return;
        }
        std::array<std::uint8_t, 256> encoded{};
        const auto encoded_size =
            m3e_encode_timer_provider_event(
                &records[index],
                ++g_provider_revision,
                now,
                encoded.data(),
                encoded.size());
        if (encoded_size == 0 ||
            !invoke_guest_handler(
                g_handle_provider_event,
                encoded.data(),
                encoded_size,
                "handle_provider_event")) {
            ESP_LOGE(
                kTag,
                "[host] timer provider delivery failed");
            latch_runtime_failure(AppRuntimeFailureKind::timer_event);
            return;
        }
        if (!publish_timer_surfaces(records[index], now)) {
            ESP_LOGE(
                kTag,
                "[host] timer surface publication failed");
            latch_runtime_failure(AppRuntimeFailureKind::timer_event);
            return;
        }
    }
}
