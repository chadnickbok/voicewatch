#include "voice_service.hpp"
#include "voice_media_transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.hpp"
#include "cJSON.h"
#include "display.hpp"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"
#include "m3e/os/system_shell.h"
#include "network_service.hpp"
#include "nvs.h"
#include "package_service.hpp"
#include "sdkconfig.h"
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
#include "voice_moq_bootstrap.hpp"
#include "esp_random.h"
#endif

#ifdef DOODAD_MOQ_DIAGNOSTIC
void voice_moq_diagnostic_tick();
#endif

namespace {

constexpr char kTag[] = "voice-service";
constexpr std::size_t kMaximumSignalBytes = 16 * 1024;
constexpr std::uint32_t kFrameDurationMs = 20;
constexpr int kSignalingFragmentBytes = 256;
constexpr std::size_t kSdpChunkBytes = 64;
constexpr std::size_t kCommandQueueDepth = 6, kEventQueueDepth = 6;
constexpr std::size_t kCaptureCorrelationCount = 8;
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
constexpr std::size_t kVoiceTaskStackBytes = 16 * 1024;
#else
constexpr std::size_t kVoiceTaskStackBytes = 64 * 1024;
#endif

enum class CommandKind : std::uint8_t {
    connected,
    secure_message,
    welcome,
    local_description,
    local_candidate,
    remote_description,
    remote_candidate,
    start,
    stop,
    transcript,
    action,
    agent_state,
    activate,
    finish,
    cancel,
    release_owner,
};

struct Command {
    CommandKind kind;
    std::uint64_t request_id;
    std::uint64_t owner_token;
    std::uint64_t capture_id;
    std::uint32_t duration_ms;
    std::uint8_t* data;
    std::size_t size;
    std::uint64_t session = 0;
};

struct CaptureCorrelation {
    std::uint64_t capture_id = 0;
    std::uint64_t request_id = 0;
    std::uint64_t owner_token = 0;
};

namespace media = doodad::voice_media;
QueueHandle_t g_commands = nullptr;
QueueHandle_t g_events = nullptr;
TaskHandle_t g_task = nullptr;
StaticTask_t g_task_control{};
StackType_t* g_task_stack = nullptr;
esp_websocket_client_handle_t g_websocket = nullptr;
std::atomic<bool> g_websocket_connected{false};
std::atomic<bool> g_transport_reset_requested{false};
bool g_mdns_initialized = false;
std::uint64_t g_sequence = 0;
std::atomic<std::uint64_t> g_control_generation{0};
std::uint64_t g_active_request = 0, g_active_owner_token = 0, g_active_capture_id = 0;
std::uint64_t g_capture_sequence = 0;
std::array<CaptureCorrelation, kCaptureCorrelationCount> g_capture_correlations{};
std::size_t g_capture_correlation_cursor = 0;
portMUX_TYPE g_current_guest_owner_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint64_t g_current_guest_owner_token = 0;
std::uint32_t g_encoded_frames = 0, g_dropped_frames = 0, g_encoded_bytes = 0;
std::uint64_t g_first_group = 0, g_end_group = 0;
char* g_websocket_payload = nullptr;
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
namespace secure = doodad::moq_control;
secure::Grant* g_grant = nullptr;
std::uint64_t g_server_sequence=0, g_active_start=0, g_highest_start=0, g_capture_samples=0;
std::uint64_t g_highest_response=0, g_response_samples=0, g_next_connect=0;
media::Response g_response{};
bool g_welcomed=false, g_response_ending=false, g_capture_complete=false;
unsigned g_connect_failures=0;
std::atomic<std::uint32_t> g_denied_revision{0};
std::uint32_t g_connect_revision=0;
std::uint64_t g_ready_deadline=0;
std::size_t g_ws_received=0, g_ws_frame_received=0;
bool g_ws_fragmented=false;
bool secure_live() {
    const auto now=static_cast<std::uint64_t>(esp_timer_get_time()/1000);
    return g_grant && g_grant->session[0] && now<g_grant->until_ms && now<g_grant->trusted_until_ms &&
        g_grant->profile_revision==secure::profile_revision() && secure::clock_valid();
}
void retire_control() {
    media::cancel(); g_websocket_connected=false;
    g_transport_reset_requested.store(true,std::memory_order_release);
}
void parse_message(const char*,std::size_t);
#endif

constexpr std::uint32_t kAgentStateMagic = 0x44414731;
constexpr std::size_t kJournalCapacity = 8;

enum class AgentCapability : std::uint8_t {
    none,
    record_missed_set,
    log_food,
};

struct AgentJournalEntry {
    std::array<char, 65> idempotency_key{};
    AgentCapability capability = AgentCapability::none;
    std::uint32_t revision = 0;
    std::uint32_t item_id = 0;
};

struct AgentState {
    std::uint32_t magic = kAgentStateMagic;
    std::uint32_t revision = 1;
    std::uint32_t food_count = 0;
    bool set3_missed = false;
    std::uint8_t next_journal = 0;
    std::array<AgentJournalEntry, kJournalCapacity> journal{};
};

AgentState g_agent_state{};
ESP_EVENT_DEFINE_BASE(kAgentPersistEvent);
constexpr std::int32_t kAgentPersistEventWrite = 1;
TaskHandle_t g_agent_persist_waiter = nullptr;
volatile bool g_agent_persist_result = false;
bool g_agent_persist_ready = false;

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void free_command(Command& command) {
    if (command.data != nullptr) heap_caps_free(command.data);
    command.data = nullptr;
    command.size = 0;
}

std::uint64_t current_guest_owner_snapshot() {
    portENTER_CRITICAL(&g_current_guest_owner_lock);
    const auto owner_token = g_current_guest_owner_token;
    portEXIT_CRITICAL(&g_current_guest_owner_lock);
    return owner_token;
}

std::uint64_t next_capture_id() {
    if (g_capture_sequence == UINT64_MAX) return 0;
    return ++g_capture_sequence;
}

void remember_capture(const CaptureCorrelation& correlation) {
    g_capture_correlations[g_capture_correlation_cursor] = correlation;
    g_capture_correlation_cursor =
        (g_capture_correlation_cursor + 1) %
        g_capture_correlations.size();
}

bool consume_capture(
    std::uint64_t capture_id,
    std::uint64_t request_id,
    CaptureCorrelation& correlation) {
    correlation = {};
    if (capture_id == 0) return false;
    for (auto& candidate : g_capture_correlations) {
        if (candidate.capture_id != capture_id ||
            candidate.request_id != request_id) {
            continue;
        }
        correlation = candidate;
        candidate = {};
        return true;
    }
    return false;
}

void publish_owned(
    VoiceEventKind kind,
    const char* text,
    std::uint32_t elapsed_ms,
    std::uint64_t request_id,
    std::uint64_t owner_token) {
    if (g_events == nullptr) return;
    VoiceEvent event{};
    event.kind = kind;
    event.request_id = request_id;
    event.owner_token = owner_token;
    event.elapsed_ms = elapsed_ms;
    event.encoded_frames = g_encoded_frames;
    event.dropped_frames = g_dropped_frames;
    if (text != nullptr) {
        std::strncpy(event.text.data(), text, event.text.size() - 1);
    }
    if (xQueueSend(g_events, &event, 0) != pdTRUE) {
        ESP_LOGW(kTag, "voice event queue full");
    }
}

void publish(
    VoiceEventKind kind,
    const char* text = nullptr,
    std::uint32_t elapsed_ms = 0) {
    publish_owned(
        kind,
        text,
        elapsed_ms,
        g_active_request,
        g_active_owner_token);
}

bool enqueue(
    CommandKind kind,
    const char* data = nullptr,
    std::size_t size = 0,
    std::uint32_t duration_ms = 0,
    std::uint64_t request_id = 0,
    std::uint64_t owner_token = 0,
    TickType_t wait_ticks = 0,
    std::uint64_t capture_id = 0) {
    if (g_commands == nullptr || size > kMaximumSignalBytes) return false;
    Command command{
        kind,
        request_id,
        owner_token,
        capture_id,
        duration_ms,
        nullptr,
        size};
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    command.session=g_control_generation.load();
#endif
    if (size != 0) {
        command.data = static_cast<std::uint8_t*>(heap_caps_malloc(
            size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (command.data == nullptr) return false;
        std::memcpy(command.data, data, size);
        command.data[size] = 0;
    }
    if (xQueueSend(g_commands, &command, wait_ticks) != pdTRUE) {
        free_command(command);
        return false;
    }
    return true;
}

void add_envelope(cJSON* root, const char* type) {
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "type", type);
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    cJSON_AddStringToObject(root,"session_id",g_grant?g_grant->session:"");
#else
    cJSON_AddStringToObject(root, "session_id", "watch-uplink");
#endif
    cJSON_AddStringToObject(
        root, "device_id", doodad::board::identity().device_id);
    cJSON_AddNumberToObject(root, "seq", ++g_sequence);
}

bool send_json(cJSON* root) {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    if (!secure_live()) { cJSON_Delete(root); return false; }
#endif
    if (root == nullptr || !g_websocket_connected || g_websocket == nullptr) {
        cJSON_Delete(root);
        return false;
    }
    char* encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (encoded == nullptr) return false;
    const auto length = static_cast<int>(std::strlen(encoded));
    int sent = 0;
    if (length <= kSignalingFragmentBytes) {
        sent = esp_websocket_client_send_text(
            g_websocket, encoded, length, pdMS_TO_TICKS(1'000));
    } else {
        // Small WebSocket frames prevent signaling from competing with the
        // microphone's DMA reservation for a large contiguous Wi-Fi pbuf.
        int offset = 0;
        while (offset < length) {
            const auto fragment = std::min(
                kSignalingFragmentBytes, length - offset);
            const auto written = offset == 0
                ? esp_websocket_client_send_text_partial(
                    g_websocket,
                    encoded + offset,
                    fragment,
                    pdMS_TO_TICKS(1'000))
                : esp_websocket_client_send_cont_msg(
                    g_websocket,
                    encoded + offset,
                    fragment,
                    pdMS_TO_TICKS(1'000));
            if (written != fragment) {
                sent = written < 0 ? written : offset + written;
                break;
            }
            offset += written;
            sent = offset;
        }
        if (sent == length &&
            esp_websocket_client_send_fin(
                g_websocket, pdMS_TO_TICKS(1'000)) < 0) {
            sent = -1;
        }
    }
    if (sent != length) {
        ESP_LOGW(kTag, "signaling send failed sent=%d expected=%d", sent, length);
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
        retire_control();
#endif
    }
    cJSON_free(encoded);
    return sent == length;
}

void send_simple(const char* type) {
    auto* root = cJSON_CreateObject();
    add_envelope(root, type);
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    cJSON_AddObjectToObject(root,"payload");
#endif
    send_json(root);
}

void add_decimal_u64(
    cJSON* object,
    const char* name,
    std::uint64_t value) {
    char encoded[21]{};
    std::snprintf(
        encoded,
        sizeof(encoded),
        "%llu",
        static_cast<unsigned long long>(value));
    cJSON_AddStringToObject(object, name, encoded);
}

void send_capture_status(const char* type, std::uint32_t elapsed_ms) {
    auto* root = cJSON_CreateObject();
    add_envelope(root, type);
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddNumberToObject(payload, "elapsed_ms", elapsed_ms);
    cJSON_AddNumberToObject(payload, "encoded_frames", g_encoded_frames);
    cJSON_AddNumberToObject(payload, "dropped_frames", g_dropped_frames);
    cJSON_AddNumberToObject(payload, "encoded_bytes", g_encoded_bytes);
    // Decimal strings preserve the full 64-bit correlation across JSON
    // implementations that otherwise coerce numbers through IEEE-754.
    add_decimal_u64(payload, "capture_id", g_active_capture_id);
    add_decimal_u64(payload, "request_id", g_active_request);
    if (std::strcmp(media::name(), "moq") == 0) {
        add_decimal_u64(payload, "first_group", g_first_group);
        add_decimal_u64(payload, "end_group", g_end_group);
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
        add_decimal_u64(payload,"owner_token",g_active_owner_token);
        add_decimal_u64(payload,"start_id",g_active_start);
        add_decimal_u64(payload,"samples",g_capture_samples);
#endif
    }
    send_json(root);
}

void send_hello() {
    auto* root = cJSON_CreateObject();
    add_envelope(root, "hello");
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    const auto& identity = doodad::board::identity();
    cJSON_AddStringToObject(payload, "device_id", identity.device_id);
    cJSON_AddStringToObject(payload, "device", identity.board);
    cJSON_AddStringToObject(payload, "board", identity.board);
    cJSON_AddStringToObject(payload, "transport", media::name());
    cJSON_AddStringToObject(payload, "audio", media::codec_description());
    cJSON_AddNumberToObject(payload, "frame_ms", kFrameDurationMs);
    auto* capabilities = cJSON_AddObjectToObject(payload, "capabilities");
    cJSON_AddBoolToObject(capabilities, "touch", true);
    cJSON_AddBoolToObject(capabilities, "microphone", true);
    cJSON_AddBoolToObject(capabilities, "speaker", true);
    cJSON_AddBoolToObject(capabilities, "haptic",
        std::strcmp(identity.board, "t-watch-s3") == 0 ||
        std::strcmp(identity.board, "t-watch-ultra") == 0);
    cJSON_AddBoolToObject(capabilities, "microsd",
        doodad::board::has_microsd());
    send_json(root);
}

bool write_agent_state(const AgentState& state) {
    nvs_handle_t handle = 0;
    auto status = nvs_open("agent_ctl", NVS_READWRITE, &handle);
    if (status != ESP_OK) return false;
    status = nvs_set_blob(
        handle, "state", &state, sizeof(state));
    if (status == ESP_OK) status = nvs_commit(handle);
    nvs_close(handle);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "agent journal persist failed: %s", esp_err_to_name(status));
    }
    return status == ESP_OK;
}

void persist_agent_state_event(
    void*, esp_event_base_t, std::int32_t, void* event_data) {
    const auto* state = static_cast<const AgentState*>(event_data);
    g_agent_persist_result = state != nullptr && write_agent_state(*state);
    if (g_agent_persist_waiter != nullptr) {
        xTaskNotifyGive(g_agent_persist_waiter);
    }
}

bool save_agent_state() {
    // NVS temporarily disables the flash/PSRAM cache. The WebRTC worker needs
    // a large PSRAM stack, so perform only the flash transaction on ESP-IDF's
    // default event-loop task, whose stack is internal and cache-safe.
    if (!g_agent_persist_ready) return write_agent_state(g_agent_state);
    if (g_agent_persist_waiter != nullptr) return false;
    g_agent_persist_waiter = xTaskGetCurrentTaskHandle();
    g_agent_persist_result = false;
    const auto posted = esp_event_post(
        kAgentPersistEvent,
        kAgentPersistEventWrite,
        &g_agent_state,
        sizeof(g_agent_state),
        pdMS_TO_TICKS(1'000));
    if (posted != ESP_OK) {
        g_agent_persist_waiter = nullptr;
        ESP_LOGE(kTag, "agent journal dispatch failed: %s", esp_err_to_name(posted));
        return false;
    }
    const auto completed = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5'000));
    g_agent_persist_waiter = nullptr;
    if (completed == 0) {
        ESP_LOGE(kTag, "agent journal persist timed out");
        return false;
    }
    return g_agent_persist_result;
}

bool load_agent_state() {
    g_agent_state = AgentState{};
    nvs_handle_t handle = 0;
    const auto opened = nvs_open("agent_ctl", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return write_agent_state(g_agent_state);
    if (opened != ESP_OK) return false;
    AgentState restored{};
    std::size_t size = sizeof(restored);
    const auto status = nvs_get_blob(handle, "state", &restored, &size);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) return write_agent_state(g_agent_state);
    if (status != ESP_OK || size != sizeof(restored) ||
        restored.magic != kAgentStateMagic || restored.revision == 0 ||
        restored.next_journal >= kJournalCapacity) {
        ESP_LOGW(kTag, "agent journal invalid; resetting bounded state");
        return write_agent_state(g_agent_state);
    }
    g_agent_state = restored;
    ESP_LOGI(
        kTag,
        "agent journal restored revision=%u food=%u missed=%d",
        static_cast<unsigned>(g_agent_state.revision),
        static_cast<unsigned>(g_agent_state.food_count),
        g_agent_state.set3_missed);
    return true;
}

const AgentJournalEntry* find_journal(const char* key) {
    if (key == nullptr || key[0] == 0) return nullptr;
    for (const auto& entry : g_agent_state.journal) {
        if (entry.capability != AgentCapability::none &&
            std::strcmp(entry.idempotency_key.data(), key) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

void remember_action(
    const char* key,
    AgentCapability capability,
    std::uint32_t revision,
    std::uint32_t item_id) {
    auto& entry = g_agent_state.journal[g_agent_state.next_journal];
    entry = AgentJournalEntry{};
    std::strncpy(
        entry.idempotency_key.data(), key,
        entry.idempotency_key.size() - 1);
    entry.capability = capability;
    entry.revision = revision;
    entry.item_id = item_id;
    g_agent_state.next_journal = static_cast<std::uint8_t>(
        (g_agent_state.next_journal + 1) % kJournalCapacity);
}

void send_watch_snapshot() {
    auto* root = cJSON_CreateObject();
    add_envelope(root, "watch.state");
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddNumberToObject(payload, "schema_version", 1);
    cJSON_AddStringToObject(
        payload, "device_id", doodad::board::identity().device_id);
    cJSON_AddStringToObject(
        payload, "board", doodad::board::identity().board);
    cJSON_AddNumberToObject(payload, "revision", g_agent_state.revision);
    cJSON_AddStringToObject(payload, "foreground_app", "dev.doodad.workout");
    cJSON_AddStringToObject(payload, "route", "active_session");
    cJSON_AddStringToObject(payload, "selected_entity", "squat_set_3");
    cJSON_AddStringToObject(payload, "active_workout_id", "workout_819");
    auto* semantics = cJSON_AddArrayToObject(payload, "screen_semantics");
    auto* selected = cJSON_CreateObject();
    cJSON_AddStringToObject(selected, "id", "squat_set_3");
    cJSON_AddStringToObject(selected, "role", "row");
    cJSON_AddStringToObject(selected, "label", "Squat set 3");
    cJSON_AddStringToObject(selected, "value", "225 lb x 5");
    cJSON_AddStringToObject(selected, "state", "selected");
    cJSON_AddItemToArray(semantics, selected);
    cJSON_AddArrayToObject(payload, "pending_jobs");
    auto* domain = cJSON_AddObjectToObject(payload, "domain_state");
    auto* workout = cJSON_AddObjectToObject(domain, "workout");
    auto* sets = cJSON_AddArrayToObject(workout, "sets");
    for (int index = 3; index <= 4; ++index) {
        auto* set = cJSON_CreateObject();
        char id[20]{};
        std::snprintf(id, sizeof(id), "squat_set_%d", index);
        cJSON_AddStringToObject(set, "id", id);
        cJSON_AddStringToObject(set, "exercise", "Squat");
        cJSON_AddNumberToObject(set, "weight_lb", 225);
        cJSON_AddNumberToObject(set, "reps", 5);
        cJSON_AddStringToObject(
            set, "status",
            index == 3 && g_agent_state.set3_missed ? "missed" : "pending");
        cJSON_AddItemToArray(sets, set);
    }
    send_json(root);
}

void send_action_error(
    const char* request_id,
    const char* capability,
    const char* code,
    const char* message) {
    auto* root = cJSON_CreateObject();
    add_envelope(root, "action.result");
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(payload, "request_id", request_id);
    cJSON_AddStringToObject(payload, "capability", capability);
    cJSON_AddFalseToObject(payload, "ok");
    auto* error = cJSON_AddObjectToObject(payload, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddNumberToObject(error, "revision", g_agent_state.revision);
    send_json(root);
}

void send_action_success(
    const char* request_id,
    const char* capability,
    bool duplicate,
    cJSON* result) {
    auto* root = cJSON_CreateObject();
    add_envelope(root, "action.result");
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(payload, "request_id", request_id);
    cJSON_AddStringToObject(payload, "capability", capability);
    cJSON_AddTrueToObject(payload, "ok");
    cJSON_AddBoolToObject(payload, "duplicate", duplicate);
    cJSON_AddNumberToObject(payload, "revision", g_agent_state.revision);
    cJSON_AddItemToObject(payload, "result", result);
    send_json(root);
}

void handle_record_missed_set(
    const char* request_id,
    const char* idempotency_key,
    const cJSON* arguments) {
    const auto* prior = find_journal(idempotency_key);
    if (prior != nullptr) {
        if (prior->capability != AgentCapability::record_missed_set) {
            send_action_error(request_id, "record_missed_set", "idempotency_conflict",
                              "Idempotency key belongs to another capability.");
            return;
        }
        auto* result = cJSON_CreateObject();
        cJSON_AddTrueToObject(result, "committed");
        cJSON_AddStringToObject(result, "set_id", "squat_set_3");
        cJSON_AddStringToObject(result, "status", "missed");
        cJSON_AddNumberToObject(result, "revision", prior->revision);
        send_action_success(request_id, "record_missed_set", true, result);
        return;
    }
    const auto* revision = cJSON_GetObjectItemCaseSensitive(arguments, "expected_revision");
    const auto* workout = cJSON_GetObjectItemCaseSensitive(arguments, "workout_id");
    const auto* set = cJSON_GetObjectItemCaseSensitive(arguments, "set_id");
    if (!cJSON_IsNumber(revision) || !cJSON_IsString(workout) ||
        !cJSON_IsString(set) ||
        std::strcmp(workout->valuestring, "workout_819") != 0 ||
        std::strcmp(set->valuestring, "squat_set_3") != 0) {
        send_action_error(request_id, "record_missed_set", "ambiguous_reference",
                          "The selected workout set could not be resolved.");
        return;
    }
    if (static_cast<std::uint32_t>(revision->valuedouble) != g_agent_state.revision ||
        g_agent_state.set3_missed) {
        send_action_error(request_id, "record_missed_set", "stale_revision",
                          "Watch state changed before the action committed.");
        return;
    }
    const auto previous = g_agent_state;
    g_agent_state.set3_missed = true;
    ++g_agent_state.revision;
    remember_action(idempotency_key, AgentCapability::record_missed_set,
                    g_agent_state.revision, 3);
    if (!save_agent_state()) {
        g_agent_state = previous;
        send_action_error(request_id, "record_missed_set", "persistence_failed",
                          "The watch journal could not commit the action.");
        return;
    }
    auto* result = cJSON_CreateObject();
    cJSON_AddTrueToObject(result, "committed");
    cJSON_AddStringToObject(result, "set_id", "squat_set_3");
    cJSON_AddStringToObject(result, "status", "missed");
    cJSON_AddNumberToObject(result, "revision", g_agent_state.revision);
    send_action_success(request_id, "record_missed_set", false, result);
    send_watch_snapshot();
}

void handle_get_next_set(const char* request_id) {
    auto* result = cJSON_CreateObject();
    auto* next = cJSON_AddObjectToObject(result, "next_set");
    cJSON_AddStringToObject(next, "id", "squat_set_4");
    cJSON_AddStringToObject(next, "exercise", "Squat");
    cJSON_AddNumberToObject(next, "weight_lb", 225);
    cJSON_AddNumberToObject(next, "reps", 5);
    cJSON_AddStringToObject(next, "status", "pending");
    send_action_success(request_id, "get_next_set", false, result);
}

void handle_log_food(
    const char* request_id,
    const char* idempotency_key,
    const cJSON* arguments) {
    const auto* prior = find_journal(idempotency_key);
    if (prior != nullptr) {
        if (prior->capability != AgentCapability::log_food) {
            send_action_error(request_id, "log_food", "idempotency_conflict",
                              "Idempotency key belongs to another capability.");
            return;
        }
        auto* result = cJSON_CreateObject();
        char entry_id[24]{};
        std::snprintf(entry_id, sizeof(entry_id), "food_%u",
                      static_cast<unsigned>(prior->item_id));
        cJSON_AddTrueToObject(result, "committed");
        cJSON_AddTrueToObject(result, "provisional");
        cJSON_AddStringToObject(result, "entry_id", entry_id);
        cJSON_AddNumberToObject(result, "revision", prior->revision);
        send_action_success(request_id, "log_food", true, result);
        return;
    }
    const auto* description = cJSON_GetObjectItemCaseSensitive(arguments, "description");
    const auto* quantity = cJSON_GetObjectItemCaseSensitive(arguments, "quantity");
    const auto* unit = cJSON_GetObjectItemCaseSensitive(arguments, "unit");
    if (!cJSON_IsString(description) || description->valuestring[0] == 0 ||
        !cJSON_IsNumber(quantity) || quantity->valuedouble <= 0 ||
        !cJSON_IsString(unit) || unit->valuestring[0] == 0) {
        send_action_error(request_id, "log_food", "invalid_arguments",
                          "Food description, positive quantity, and unit are required.");
        return;
    }
    const auto previous = g_agent_state;
    ++g_agent_state.food_count;
    ++g_agent_state.revision;
    remember_action(idempotency_key, AgentCapability::log_food,
                    g_agent_state.revision, g_agent_state.food_count);
    if (!save_agent_state()) {
        g_agent_state = previous;
        send_action_error(request_id, "log_food", "persistence_failed",
                          "The watch journal could not commit the action.");
        return;
    }
    auto* result = cJSON_CreateObject();
    char entry_id[24]{};
    std::snprintf(entry_id, sizeof(entry_id), "food_%u",
                  static_cast<unsigned>(g_agent_state.food_count));
    cJSON_AddTrueToObject(result, "committed");
    cJSON_AddTrueToObject(result, "provisional");
    cJSON_AddStringToObject(result, "entry_id", entry_id);
    cJSON_AddStringToObject(result, "description", description->valuestring);
    cJSON_AddNumberToObject(result, "quantity", quantity->valuedouble);
    cJSON_AddStringToObject(result, "unit", unit->valuestring);
    cJSON_AddNumberToObject(result, "revision", g_agent_state.revision);
    send_action_success(request_id, "log_food", false, result);
    send_watch_snapshot();
}

void handle_action(const char* bytes, std::size_t size) {
    auto* payload = cJSON_ParseWithLength(bytes, size);
    if (payload == nullptr) return;
    const auto* request = cJSON_GetObjectItemCaseSensitive(payload, "request_id");
    const auto* capability = cJSON_GetObjectItemCaseSensitive(payload, "capability");
    const auto* key = cJSON_GetObjectItemCaseSensitive(payload, "idempotency_key");
    const auto* arguments = cJSON_GetObjectItemCaseSensitive(payload, "arguments");
    if (!cJSON_IsString(request) || !cJSON_IsString(capability) ||
        !cJSON_IsObject(arguments)) {
        cJSON_Delete(payload);
        return;
    }
    const char* idempotency_key = cJSON_IsString(key) ? key->valuestring : request->valuestring;
    if (std::strlen(idempotency_key) >= 65) {
        send_action_error(request->valuestring, capability->valuestring,
                          "invalid_idempotency_key", "Idempotency key is too long.");
    } else if (std::strcmp(capability->valuestring, "record_missed_set") == 0) {
        handle_record_missed_set(
            request->valuestring, idempotency_key, arguments);
    } else if (std::strcmp(capability->valuestring, "get_next_set") == 0) {
        handle_get_next_set(request->valuestring);
    } else if (std::strcmp(capability->valuestring, "log_food") == 0) {
        handle_log_food(request->valuestring, idempotency_key, arguments);
    } else {
        send_action_error(request->valuestring, capability->valuestring,
                          "unknown_capability", "Capability is not installed.");
    }
    cJSON_Delete(payload);
}

std::uint8_t voice_phase_value(const char* phase) {
    if (phase == nullptr || std::strcmp(phase, "idle") == 0) return 0;
    if (std::strcmp(phase, "listening") == 0) return 1;
    if (std::strcmp(phase, "thinking") == 0) return 2;
    if (std::strcmp(phase, "speaking") == 0) return 3;
    if (std::strcmp(phase, "clarifying") == 0) return 4;
    if (std::strcmp(phase, "ready") == 0) return 6;
    return 5;
}

void copy_agent_text(
    char* destination,
    std::size_t capacity,
    const cJSON* value) {
    if (destination == nullptr || capacity == 0) return;
    destination[0] = '\0';
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return;
    std::strncpy(destination, value->valuestring, capacity - 1);
    destination[capacity - 1] = '\0';
}

std::uint32_t agent_color_value(const cJSON* value) {
    if (!cJSON_IsString(value) || value->valuestring == nullptr ||
        value->valuestring[0] != '#' || std::strlen(value->valuestring) != 7) {
        return 0x7241ff;
    }
    char* end = nullptr;
    const auto color = std::strtoul(value->valuestring + 1, &end, 16);
    return end != nullptr && *end == '\0'
        ? static_cast<std::uint32_t>(color)
        : 0x7241ff;
}

std::uint8_t agent_icon_value(const cJSON* value) {
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
        return M3E_SYSTEM_SHELL_AGENT_ICON_MONITORING;
    }
    if (std::strcmp(value->valuestring, "app_builder") == 0) {
        return M3E_SYSTEM_SHELL_AGENT_ICON_APP_BUILDER;
    }
    if (std::strcmp(value->valuestring, "research") == 0) {
        return M3E_SYSTEM_SHELL_AGENT_ICON_RESEARCH;
    }
    if (std::strcmp(value->valuestring, "presentation") == 0) {
        return M3E_SYSTEM_SHELL_AGENT_ICON_PRESENTATION;
    }
    return M3E_SYSTEM_SHELL_AGENT_ICON_MONITORING;
}

void handle_agent_state(const char* bytes, std::size_t size) {
    auto* payload = cJSON_ParseWithLength(bytes, size);
    if (payload == nullptr) return;
    const auto* phase = cJSON_GetObjectItemCaseSensitive(payload, "voice_phase");
    const auto* display = cJSON_GetObjectItemCaseSensitive(payload, "display");
    const auto* transcript = cJSON_GetObjectItemCaseSensitive(display, "transcript");
    const auto* response = cJSON_GetObjectItemCaseSensitive(display, "response");
    const auto* background = cJSON_GetObjectItemCaseSensitive(payload, "background");
    const auto* running = cJSON_GetObjectItemCaseSensitive(background, "running_count");
    const auto* focused = cJSON_GetObjectItemCaseSensitive(background, "focused_question");
    const auto* review = cJSON_GetObjectItemCaseSensitive(background, "review_ready");
    const auto* completion = cJSON_GetObjectItemCaseSensitive(background, "completion_pending");
    const auto* status_changed = cJSON_GetObjectItemCaseSensitive(
        background, "status_changed");
    const auto* install = cJSON_GetObjectItemCaseSensitive(background, "install_state");
    std::array<DisplayAgentTask, kDisplayAgentTaskCapacity> tasks{};
    std::size_t task_count = 0;
    const auto* task_array = cJSON_GetObjectItemCaseSensitive(background, "tasks");
    if (cJSON_IsArray(task_array)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, task_array) {
            if (task_count >= tasks.size() || !cJSON_IsObject(item)) break;
            auto& task = tasks[task_count];
            copy_agent_text(
                task.task_id, sizeof(task.task_id),
                cJSON_GetObjectItemCaseSensitive(item, "job_id"));
            copy_agent_text(
                task.title, sizeof(task.title),
                cJSON_GetObjectItemCaseSensitive(item, "title"));
            copy_agent_text(
                task.status, sizeof(task.status),
                cJSON_GetObjectItemCaseSensitive(item, "status"));
            copy_agent_text(
                task.elapsed, sizeof(task.elapsed),
                cJSON_GetObjectItemCaseSensitive(item, "elapsed"));
            copy_agent_text(
                task.context_label, sizeof(task.context_label),
                cJSON_GetObjectItemCaseSensitive(item, "detail_label"));
            copy_agent_text(
                task.context, sizeof(task.context),
                cJSON_GetObjectItemCaseSensitive(item, "detail"));
            const auto* stages = cJSON_GetObjectItemCaseSensitive(item, "stages");
            for (std::size_t stage = 0; stage < 4; ++stage) {
                copy_agent_text(
                    task.stages[stage], sizeof(task.stages[stage]),
                    cJSON_IsArray(stages)
                        ? cJSON_GetArrayItem(stages, static_cast<int>(stage))
                        : nullptr);
            }
            const auto* completed = cJSON_GetObjectItemCaseSensitive(
                item, "completed_stages");
            const auto* active = cJSON_GetObjectItemCaseSensitive(
                item, "active_stage");
            const auto* progress = cJSON_GetObjectItemCaseSensitive(
                item, "progress");
            task.completed_stage_count = cJSON_IsNumber(completed)
                ? std::clamp(completed->valueint, 0, 4) : 0;
            task.active_stage = cJSON_IsNumber(active)
                ? std::clamp(active->valueint, 0, 3) : 0;
            task.progress_percent = cJSON_IsNumber(progress)
                ? std::clamp(progress->valueint, 0, 100) : 0;
            task.primary_color_rgb = agent_color_value(
                cJSON_GetObjectItemCaseSensitive(item, "color"));
            task.icon = agent_icon_value(
                cJSON_GetObjectItemCaseSensitive(item, "icon"));
            if (task.task_id[0] != '\0' && task.title[0] != '\0') {
                ++task_count;
            }
        }
    }
    display_publish_agent_state(
        voice_phase_value(cJSON_IsString(phase) ? phase->valuestring : "error"),
        cJSON_IsNumber(running) ? std::clamp(running->valueint, 0, 255) : 0,
        cJSON_IsTrue(focused), cJSON_IsTrue(review), cJSON_IsTrue(completion),
        cJSON_IsNumber(install) ? std::clamp(install->valueint, 0, 4) : 0,
        cJSON_IsString(transcript) ? transcript->valuestring : "",
        cJSON_IsString(response) ? response->valuestring : "",
        tasks.data(), task_count, cJSON_IsTrue(status_changed));
    cJSON_Delete(payload);
}

void send_local_peer_message(const Command& command) {
    if (command.data == nullptr || command.size == 0) return;
    const auto* message_text = reinterpret_cast<const char*>(command.data);
    if (command.kind == CommandKind::local_candidate) {
        auto* root = cJSON_CreateObject();
        add_envelope(root, "ice");
        auto* payload = cJSON_AddObjectToObject(root, "payload");
        cJSON_AddStringToObject(
            payload,
            "candidate",
            message_text);
        send_json(root);
    } else {
        const auto chunk_count =
            (command.size + kSdpChunkBytes - 1) /
            kSdpChunkBytes;
        bool all_sent = true;
        for (std::size_t index = 0; index < chunk_count; ++index) {
            const auto offset = index * kSdpChunkBytes;
            const auto chunk_size = std::min(
                kSdpChunkBytes,
                command.size - offset);
            char chunk[kSdpChunkBytes + 1]{};
            std::memcpy(chunk, message_text + offset, chunk_size);
            auto* root = cJSON_CreateObject();
            add_envelope(root, "sdp.chunk");
            auto* payload = cJSON_AddObjectToObject(root, "payload");
            cJSON_AddStringToObject(payload, "kind", "offer");
            cJSON_AddNumberToObject(payload, "index", index);
            cJSON_AddNumberToObject(payload, "count", chunk_count);
            cJSON_AddStringToObject(payload, "data", chunk);
            all_sent = send_json(root) && all_sent;
        }
        ESP_LOGI(
            kTag,
            "local SDP chunks=%u delivered=%d",
            static_cast<unsigned>(chunk_count),
            all_sent);
    }
}

void handle_command(Command& command) {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    if (command.session!=g_control_generation.load()) return;
#endif
    switch (command.kind) {
        case CommandKind::connected:
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            if (secure_live()) send_hello(); else retire_control();
#endif
            break;
        case CommandKind::secure_message:
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            if (secure_live() && command.data) parse_message(reinterpret_cast<const char*>(command.data),command.size);
            else retire_control();
#endif
            break;
        case CommandKind::welcome:
            send_simple("welcome.ack");
            send_watch_snapshot();
            media::disconnect();
            g_capture_correlations = {};
#if !CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            if (g_control_generation == UINT64_MAX) break;
            ++g_control_generation;
            if (media::connect(media::Session{.generation=g_control_generation})) {
                send_simple("peer.created");
            } else {
                publish(VoiceEventKind::error, "Voice transport failed");
            }
#else
            if (secure_live()) {
                media::Session session{}; session.generation=g_control_generation.load();
                session.host=g_grant->host; session.port=g_grant->port; session.roots_pem=g_grant->roots;
                session.setup_path=g_grant->setup; session.local_broadcast=g_grant->publish; session.remote_broadcast=g_grant->subscribe;
                session.authorization_valid_until_ms=g_grant->until_ms; session.trusted_time_valid_until_ms=g_grant->trusted_until_ms;
                if (media::connect(session)) send_simple("peer.created"); else retire_control();
            } else retire_control();
#endif
            break;
        case CommandKind::local_description:
        case CommandKind::local_candidate:
            send_local_peer_message(command);
            break;
        case CommandKind::remote_description:
        case CommandKind::remote_candidate:
            if (command.data != nullptr) {
                media::signal(command.kind == CommandKind::remote_candidate
                    ? media::Signal::candidate : media::Signal::description,
                    reinterpret_cast<const char*>(command.data), command.size);
            }
            break;
        case CommandKind::start: {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            // Local guest requests do not inherit a previous host start ID.
            g_active_start=0;
            g_capture_complete=false; g_response={};
#endif
            const auto capture_id = next_capture_id();
            if (!capture_id) break;
            const auto request_id = command.request_id == 0 && command.owner_token != 0
                ? capture_id : command.request_id;
            g_active_request = request_id;
            g_active_owner_token = command.owner_token;
            g_active_capture_id = capture_id;
            if (!g_websocket_connected || !media::capture_begin(
                    {capture_id, request_id, command.owner_token}, command.duration_ms)) {
                publish(VoiceEventKind::error, "Voice link is not ready");
                send_simple("capture.failed");
            }
            break;
        }
        case CommandKind::stop:
            if (!command.owner_token || command.owner_token == g_active_owner_token)
                media::capture_finish();
            break;
        case CommandKind::transcript: {
            CaptureCorrelation correlation{};
            if (!consume_capture(
                    command.capture_id,
                    command.request_id,
                    correlation)) {
                ESP_LOGW(kTag, "dropped uncorrelated transcript");
                break;
            }
            publish_owned(
                VoiceEventKind::transcript,
                reinterpret_cast<char*>(command.data),
                0,
                correlation.request_id,
                correlation.owner_token);
            break;
        }
        case CommandKind::action:
            if (command.data != nullptr) {
                handle_action(
                    reinterpret_cast<const char*>(command.data), command.size);
            }
            break;
        case CommandKind::agent_state:
            if (command.data != nullptr) {
                handle_agent_state(
                    reinterpret_cast<const char*>(command.data), command.size);
            }
            break;
        case CommandKind::activate:
            send_simple("listen.requested");
            break;
        case CommandKind::finish:
            send_simple("listen.finished");
            break;
        case CommandKind::cancel:
            media::cancel();
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            g_response={};
#endif
            send_simple("listen.cancelled");
            break;
        case CommandKind::release_owner:
            if (command.owner_token != 0 &&
                command.owner_token == g_active_owner_token) {
                // Keep the retired token active after stopping. Any delayed
                // transcript or lifecycle event is then still attributable
                // to the outgoing guest and cannot become owner-neutral.
                media::cancel();
            }
            break;
    }
}

bool parse_decimal_u64(
    const cJSON* value,
    bool allow_zero,
    std::uint64_t& output) {
    output = 0;
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
        return false;
    }
    const auto* text = value->valuestring;
    const auto length = std::strlen(text);
    if (length == 0 || length > 20 ||
        (length > 1 && text[0] == '0')) {
        return false;
    }
    std::uint64_t parsed = 0;
    for (std::size_t index = 0; index < length; ++index) {
        const auto character = text[index];
        if (character < '0' || character > '9') return false;
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (UINT64_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (!allow_zero && parsed == 0) return false;
    output = parsed;
    return true;
}

#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
const cJSON* field(const cJSON* root,const char* name) { return cJSON_GetObjectItemCaseSensitive(root,name); }
bool identity(const cJSON* p,media::Identity& id) {
    return secure::decimal(field(p,"capture_id"),id.capture_id,false) &&
        secure::decimal(field(p,"request_id"),id.request_id) && secure::decimal(field(p,"owner_token"),id.owner_token);
}
bool owns(const media::Identity& id) {
    return id.capture_id==g_active_capture_id && id.request_id==g_active_request && id.owner_token==g_active_owner_token &&
        (!id.owner_token || id.owner_token==current_guest_owner_snapshot());
}
// Runs only on the control owner, after the complete authenticated envelope is
// validated. Queued media operations also carry the local session generation.
bool secure_command(const char* kind,const cJSON* payload) {
    if (std::strcmp(kind,"welcome")==0) {
        const auto* transport=field(payload,"transport");
        if (g_welcomed || !cJSON_IsString(transport) || std::strcmp(transport->valuestring,"moq-lite-05")!=0) return false;
        g_welcomed=true;
        Command command{}; command.kind=CommandKind::welcome; command.session=g_control_generation.load(); handle_command(command);
        return true;
    }
    if (!g_welcomed) return false;
    if (std::strcmp(kind,"capture.start")==0) {
        std::uint64_t start=0,duration=0,owner=0;
        if (!secure::decimal(field(payload,"start_id"),start,false) ||
            !secure::number(field(payload,"duration_ms"),duration,30000) || !duration) return false;
        if (start<=g_highest_start) return true;
        const auto* target=field(payload,"target");
        if (target) {
            if (!cJSON_IsString(target) || std::strcmp(target->valuestring,"current_guest")!=0 || !(owner=current_guest_owner_snapshot())) return false;
        }
        g_highest_start=start; g_active_start=start; g_capture_complete=false; g_response={};
        const auto capture=next_capture_id(); if (!capture) return false;
        g_active_capture_id=capture; g_active_owner_token=owner; g_active_request=owner?capture:0;
        if (!media::capture_begin({capture,g_active_request,owner},duration)) {
            send_simple("capture.failed"); publish(VoiceEventKind::error,"Voice link is not ready");
        }
        return true;
    }
    if (std::strcmp(kind,"capture.stop")==0 || std::strcmp(kind,"capture.cancel")==0) {
        std::uint64_t start=0;
        if (!secure::decimal(field(payload,"start_id"),start)) return false;
        if (start!=g_active_start) return true;
        if (field(payload,"capture_id")) {
            media::Identity id{}; if (!identity(payload,id)) return false;
            if (!owns(id)) return true;
        } else if (!start) return false; // An unbound stop cannot target a guest.
        if (std::strcmp(kind,"capture.cancel")==0) { media::cancel(); g_response={}; g_capture_complete=false; }
        else if (!g_capture_complete && !media::capture_finish()) return false;
        return true;
    }
    if (std::strncmp(kind,"playback.",9)==0) {
        media::Identity id{}; std::uint64_t response=0;
        if (!identity(payload,id) || !secure::decimal(field(payload,"response_id"),response,false)) return false;
        if (!owns(id)) return true;
        if (std::strcmp(kind,"playback.begin")==0) {
            std::uint64_t first=0;
            if (!secure::decimal(field(payload,"first_group"),first) || first>=(1ULL<<62) || !g_capture_complete) return false;
            if (response<=g_highest_response) return true;
            media::Response binding{}; binding.session=g_control_generation.load(); binding.response_id=response;
            binding.identity=id; binding.first_group=first;
            if (!media::receive_begin(binding)) return false;
            g_highest_response=response; g_response=binding; g_response_ending=false; g_response_samples=0;
            return true;
        }
        if (response!=g_response.response_id) return true;
        if (std::strcmp(kind,"playback.cancel")==0) {
            if (!media::receive_cancel(g_control_generation.load(),response)) return false;
            g_response={}; return true;
        }
        if (std::strcmp(kind,"playback.end")==0) {
            std::uint64_t first=0,end=0,samples=0;
            if (g_response_ending || !secure::decimal(field(payload,"first_group"),first) || first!=g_response.first_group ||
                !secure::decimal(field(payload,"end_group"),end) || end<=first || end-first>30002 ||
                !secure::decimal(field(payload,"samples"),samples) || samples>600*16000) return false;
            if (!media::receive_end(g_control_generation.load(),response,end)) return false;
            g_response.end_group=end; g_response.has_end=true; g_response_ending=true; g_response_samples=samples;
            return true;
        }
        return false;
    }
    return false;
}
#endif

void parse_message(const char* bytes, std::size_t size) {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    auto* root=secure::json(bytes,size);
    if (!root || !secure_live() || !secure::envelope(root,doodad::board::identity().device_id,g_grant->session,g_server_sequence)) {
        cJSON_Delete(root); retire_control(); return;
    }
#else
    auto* root = cJSON_ParseWithLength(bytes, size);
    if (root == nullptr) return;
#endif
    const auto* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const auto* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const auto* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsNumber(version) || version->valueint != 1 ||
        !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    if (std::strcmp(type->valuestring,"welcome")==0 || std::strncmp(type->valuestring,"capture.",8)==0 ||
        std::strncmp(type->valuestring,"playback.",9)==0) {
        if (!secure_command(type->valuestring,payload)) retire_control();
        cJSON_Delete(root); return;
    }
    if (!g_welcomed || (std::strcmp(type->valuestring,"transcript.final")!=0 &&
        std::strcmp(type->valuestring,"action.invoke")!=0 && std::strcmp(type->valuestring,"agent.state")!=0 &&
        std::strcmp(type->valuestring,"app.ready")!=0)) {
        cJSON_Delete(root); retire_control(); return;
    }
#endif
    if (std::strcmp(type->valuestring, "welcome") == 0) {
        enqueue(CommandKind::welcome);
    } else if (std::strcmp(type->valuestring, "sdp") == 0) {
        const auto* sdp = cJSON_GetObjectItemCaseSensitive(payload, "sdp");
        if (cJSON_IsString(sdp)) {
            enqueue(CommandKind::remote_description, sdp->valuestring,
                    std::strlen(sdp->valuestring));
        }
    } else if (std::strcmp(type->valuestring, "ice") == 0) {
        const auto* candidate = cJSON_GetObjectItemCaseSensitive(payload, "candidate");
        if (cJSON_IsString(candidate)) {
            enqueue(CommandKind::remote_candidate, candidate->valuestring,
                    std::strlen(candidate->valuestring));
        }
    } else if (std::strcmp(type->valuestring, "capture.start") == 0) {
        const auto* duration = cJSON_GetObjectItemCaseSensitive(payload, "duration_ms");
        const auto* target =
            cJSON_GetObjectItemCaseSensitive(payload, "target");
        std::uint64_t owner_token = 0;
        bool target_valid = target == nullptr;
        if (target != nullptr && cJSON_IsString(target) &&
            target->valuestring != nullptr &&
            std::strcmp(target->valuestring, "current_guest") == 0) {
            owner_token = current_guest_owner_snapshot();
            target_valid = owner_token != 0;
        }
        if (!target_valid) {
            ESP_LOGW(kTag, "capture.start guest target unavailable");
        } else {
            enqueue(
                CommandKind::start,
                nullptr,
                0,
                cJSON_IsNumber(duration) ? duration->valueint : 8'000,
                0,
                owner_token);
        }
    } else if (std::strcmp(type->valuestring, "capture.stop") == 0) {
        enqueue(CommandKind::stop);
    } else if (std::strcmp(type->valuestring, "transcript.final") == 0) {
        const auto* transcript = cJSON_GetObjectItemCaseSensitive(payload, "text");
        std::uint64_t capture_id = 0;
        std::uint64_t request_id = 0;
        if (cJSON_IsString(transcript) &&
            transcript->valuestring != nullptr &&
            parse_decimal_u64(
                cJSON_GetObjectItemCaseSensitive(payload, "capture_id"),
                false,
                capture_id) &&
            parse_decimal_u64(
                cJSON_GetObjectItemCaseSensitive(payload, "request_id"),
                true,
                request_id)) {
            enqueue(
                CommandKind::transcript,
                transcript->valuestring,
                std::strlen(transcript->valuestring),
                0,
                request_id,
                0,
                0,
                capture_id);
        } else {
            ESP_LOGW(kTag, "dropped transcript without valid correlation");
        }
    } else if (std::strcmp(type->valuestring, "action.invoke") == 0 ||
               std::strcmp(type->valuestring, "agent.state") == 0) {
        char* encoded = cJSON_PrintUnformatted(payload);
        if (encoded != nullptr) {
            enqueue(
                std::strcmp(type->valuestring, "action.invoke") == 0
                    ? CommandKind::action : CommandKind::agent_state,
                encoded, std::strlen(encoded));
            cJSON_free(encoded);
        }
    } else if (std::strcmp(type->valuestring, "app.ready") == 0 &&
               cJSON_IsObject(payload)) {
        const auto* url = cJSON_GetObjectItemCaseSensitive(payload, "url");
        const auto* digest = cJSON_GetObjectItemCaseSensitive(
            payload, "bundle_sha256");
        const auto* bytes_value = cJSON_GetObjectItemCaseSensitive(
            payload, "bundle_bytes");
        doodad::packages::AppReadyOffer offer{};
        const auto copy_bounded = [](auto& destination, const cJSON* value) {
            if (!cJSON_IsString(value) || value->valuestring == nullptr) {
                return false;
            }
            const auto length = std::strlen(value->valuestring);
            if (length == 0 || length >= destination.size()) return false;
            std::memcpy(
                destination.data(), value->valuestring, length + 1);
            return true;
        };
        const bool size_valid = cJSON_IsNumber(bytes_value) &&
            bytes_value->valuedouble > 0 &&
            bytes_value->valuedouble <= UINT32_MAX &&
            bytes_value->valuedouble ==
                static_cast<double>(bytes_value->valueint);
        if (copy_bounded(offer.url, url) &&
            copy_bounded(offer.bundle_sha256, digest) && size_valid) {
            offer.bundle_bytes = static_cast<std::uint32_t>(
                bytes_value->valuedouble);
            if (!doodad::packages::package_service_offer(offer)) {
                ESP_LOGW(kTag, "app.ready rejected or installer busy");
            } else {
                ESP_LOGI(
                    kTag,
                    "app.ready accepted bundle=%.12s bytes=%u",
                    offer.bundle_sha256.data(),
                    static_cast<unsigned>(offer.bundle_bytes));
            }
        } else {
            ESP_LOGW(kTag, "invalid app.ready payload");
        }
    }
    cJSON_Delete(root);
}

void websocket_event(
    void*, esp_event_base_t, std::int32_t event_id, void* event_data) {
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        g_websocket_connected = true;
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
        if (!enqueue(CommandKind::connected)) retire_control();
#else
        send_hello();
#endif
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED ||
               event_id == WEBSOCKET_EVENT_CLOSED) {
        g_websocket_connected = false;
        g_transport_reset_requested.store(true, std::memory_order_release);
        display_publish_agent_state(0, 0, false, false, false, 0, "", "");
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    } else if (event_id == WEBSOCKET_EVENT_ERROR) {
        const auto* event=static_cast<esp_websocket_event_data_t*>(event_data);
        if (event && (event->error_handle.esp_tls_cert_verify_flags ||
            event->error_handle.esp_tls_stack_err==0x2700 || event->error_handle.esp_tls_stack_err==-0x2700 ||
            event->error_handle.esp_ws_handshake_status_code==401 ||
            event->error_handle.esp_ws_handshake_status_code==403)) {
            g_denied_revision=g_grant->profile_revision;
            publish(VoiceEventKind::error,"Secure voice enrollment rejected");
        }
        retire_control();
#endif
    } else if (event_id == WEBSOCKET_EVENT_DATA) {
        const auto* event = static_cast<esp_websocket_event_data_t*>(event_data);
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
        // SDK events may split one frame across buffers and one message across
        // continuation frames. Never parse stale bytes or a partial message.
        if (!event) { retire_control(); return; }
        if (event->op_code==8 || event->op_code==9 || event->op_code==10) return;
        if (event->op_code!=0 && event->op_code!=1) { retire_control(); return; }
        if (event->payload_offset==0) {
            if (event->op_code==1) {
                if (g_ws_fragmented) { retire_control(); return; }
                g_ws_received=0;
            } else if (!g_ws_fragmented) { retire_control(); return; }
            g_ws_frame_received=0;
        }
        if (event->data_len<0 || event->payload_len<0 || event->payload_offset<0 ||
            static_cast<std::size_t>(event->payload_offset)!=g_ws_frame_received ||
            event->data_len>event->payload_len-event->payload_offset ||
            g_ws_received+event->data_len>kMaximumSignalBytes) { retire_control(); return; }
        if (event->data_len) {
            if (!event->data_ptr) { retire_control(); return; }
            std::memcpy(g_websocket_payload+g_ws_received,event->data_ptr,event->data_len);
        }
        g_ws_received+=event->data_len; g_ws_frame_received+=event->data_len;
        if (g_ws_frame_received==static_cast<std::size_t>(event->payload_len)) {
            g_ws_fragmented=!event->fin;
            if (event->fin) {
                if (!g_ws_received || !enqueue(CommandKind::secure_message,g_websocket_payload,g_ws_received)) retire_control();
                g_ws_received=0;
            }
        }
#else
        if (event == nullptr || event->payload_len <= 0 ||
            event->payload_len > static_cast<int>(kMaximumSignalBytes) ||
            event->payload_offset < 0 || event->data_len < 0 ||
            event->payload_offset + event->data_len > event->payload_len) {
            return;
        }
        std::memcpy(
            g_websocket_payload + event->payload_offset,
            event->data_ptr,
            event->data_len);
        if (event->payload_offset + event->data_len == event->payload_len) {
            g_websocket_payload[event->payload_len] = 0;
            parse_message(g_websocket_payload, event->payload_len);
        }
#endif
    }
}

bool discover_url(char* destination, std::size_t capacity) {
#if !CONFIG_DOODAD_VOICE_UPLINK || CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    // Until the authenticated bootstrap provider is installed, fail closed.
    // MoQ never inherits the legacy anonymous mDNS/ws:// control path.
    (void)destination;
    (void)capacity;
    return false;
#else
#if defined(CONFIG_DOODAD_VOICE_SIGNALING_URL)
    if (CONFIG_DOODAD_VOICE_SIGNALING_URL[0] != 0) {
        std::snprintf(destination, capacity, "%s", CONFIG_DOODAD_VOICE_SIGNALING_URL);
        return true;
    }
#endif
    if (!g_mdns_initialized) {
        if (mdns_init() != ESP_OK) return false;
        mdns_hostname_set("doodad-watch");
        g_mdns_initialized = true;
    }
    mdns_result_t* results = nullptr;
    if (mdns_query_ptr(
            "_doodad-voice", "_tcp", 2'000, 1, &results) != ESP_OK ||
        results == nullptr || results->addr == nullptr) {
        if (results != nullptr) mdns_query_results_free(results);
        return false;
    }
    const mdns_ip_addr_t* address = results->addr;
    while (address != nullptr &&
           address->addr.type != ESP_IPADDR_TYPE_V4) {
        address = address->next;
    }
    if (address == nullptr) {
        mdns_query_results_free(results);
        return false;
    }
    char ip[48]{};
    esp_ip4addr_ntoa(&address->addr.u_addr.ip4, ip, sizeof(ip));
    const auto port = results->port != 0
        ? results->port : CONFIG_DOODAD_VOICE_SIGNALING_PORT;
    std::snprintf(destination, capacity, "ws://%s:%u/ws", ip, port);
    mdns_query_results_free(results);
    // Discovery is bursty. Releasing the mDNS task before creating the
    // WebSocket task avoids permanently fragmenting scarce internal DRAM.
    mdns_free();
    g_mdns_initialized = false;
    return true;
#endif
}

void connect_websocket() {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    const auto now=static_cast<std::uint64_t>(esp_timer_get_time()/1000);
    const auto revision=secure::profile_revision();
    if (revision!=g_connect_revision) {
        g_connect_revision=revision; g_next_connect=0; g_connect_failures=0;
    }
    if (!g_grant || now<g_next_connect || !secure::profile_revision() || g_denied_revision==secure::profile_revision()) return;
    g_next_connect=now+std::min(30000U,1000U<<std::min(g_connect_failures++,5U))+(esp_random()%501);
    if (!secure::acquire(*g_grant)) {
        if (secure::authorization_rejected()) {
            g_denied_revision=secure::profile_revision();
            publish(VoiceEventKind::error,"Secure voice enrollment rejected");
        } else publish(VoiceEventKind::connecting,"Waiting for secure voice host");
        return;
    }
    if (g_control_generation.load()==UINT64_MAX) { secure::wipe(g_grant,sizeof(*g_grant)); return; }
    ++g_control_generation; g_sequence=0; g_server_sequence=0; g_welcomed=false;
    g_active_capture_id=0; g_active_start=0; g_highest_start=0; g_highest_response=0;
    g_capture_complete=false; g_response={}; g_ws_received=0; g_ws_frame_received=0; g_ws_fragmented=false;
    esp_websocket_client_config_t configuration{};
    configuration.uri=g_grant->websocket_url; configuration.cert_pem=g_grant->roots;
    configuration.headers=g_grant->headers; configuration.skip_cert_common_name_check=false;
    configuration.disable_auto_reconnect=true; // Tokens cannot be replayed by the SDK.
    configuration.buffer_size=1024; configuration.task_stack=6144; configuration.task_prio=5;
    configuration.network_timeout_ms=3000; configuration.ping_interval_sec=5;
    g_ready_deadline=static_cast<std::uint64_t>(esp_timer_get_time()/1000)+15000;
#else
    char url[160]{};
    if (!discover_url(url, sizeof(url))) return;
    ESP_LOGI(kTag, "signaling endpoint discovered");
    esp_websocket_client_config_t configuration{};
    configuration.uri = url;
    configuration.buffer_size = 2'048;
    configuration.task_stack = 3 * 1024;
    configuration.task_prio = 5;
    configuration.network_timeout_ms = 5'000;
    configuration.reconnect_timeout_ms = 2'000;
    configuration.ping_interval_sec = 5;
#endif
    g_websocket = esp_websocket_client_init(&configuration);
    if (g_websocket == nullptr) return;
    esp_websocket_register_events(
        g_websocket, WEBSOCKET_EVENT_ANY, websocket_event, nullptr);
    if (esp_websocket_client_start(g_websocket) != ESP_OK) {
        esp_websocket_client_destroy(g_websocket);
        g_websocket = nullptr;
    }
}

#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
void close_control() {
    g_websocket_connected=false; media::disconnect();
    if (g_websocket) {
        esp_websocket_client_stop(g_websocket);
        esp_websocket_client_destroy(g_websocket); g_websocket=nullptr;
    }
    g_transport_reset_requested=false;
    g_response={}; g_capture_complete=false; g_welcomed=false; g_capture_correlations={};
    if (g_grant) secure::wipe(g_grant,sizeof(*g_grant));
    display_publish_agent_state(0,0,false,false,false,0,"","");
}
#endif

void poll_media() {
    media::Event event{};
    for (unsigned count=0; count<16 && media::poll(event); ++count) {
        if (event.session != g_control_generation) continue;
        const bool capture = event.kind == media::EventKind::capture_started ||
            event.kind == media::EventKind::capture_stopped || event.kind == media::EventKind::capture_failed;
        if (capture && (event.identity.capture_id != g_active_capture_id ||
                        event.identity.owner_token != g_active_owner_token)) continue;
        if (capture) {
            g_encoded_frames=event.encoded_frames; g_dropped_frames=event.dropped_frames;
            g_encoded_bytes=event.encoded_bytes; g_first_group=event.first_group; g_end_group=event.end_group;
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            g_capture_samples=event.samples;
#endif
        }
        switch(event.kind) {
        case media::EventKind::ready:
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            g_connect_failures=0; g_ready_deadline=0;
#endif
            send_simple("peer.ready"); publish(VoiceEventKind::ready, "Voice link ready"); break;
        case media::EventKind::capture_started:
            remember_capture({event.identity.capture_id,event.identity.request_id,event.identity.owner_token});
            publish(VoiceEventKind::recording, "Listening"); send_capture_status("capture.started",0); break;
        case media::EventKind::capture_stopped:
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            g_capture_complete=true;
#endif
            publish(VoiceEventKind::stopped, "Processing",event.elapsed_ms);
            send_capture_status("capture.stopped",event.elapsed_ms); display_publish_voice_level(0); break;
        case media::EventKind::capture_failed:
            publish(VoiceEventKind::error, "Audio capture failed"); send_simple("capture.failed"); break;
        case media::EventKind::disconnected:
        case media::EventKind::error:
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            retire_control();
#endif
            display_publish_agent_state(0,0,false,false,false,0,"","");
            publish(VoiceEventKind::error,"Voice transport disconnected"); break;
        case media::EventKind::playback_bound:
        case media::EventKind::playback_started:
        case media::EventKind::playback_finished: {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            if (event.response_id!=g_response.response_id || !owns(event.identity)) break;
            if (event.kind==media::EventKind::playback_finished && !event.cancelled && !event.error &&
                (!g_response_ending || event.samples!=g_response_samples || event.end_group!=g_response.end_group)) {
                retire_control(); break;
            }
#endif
            auto* root=cJSON_CreateObject();
            const char* type=event.kind==media::EventKind::playback_bound ? "playback.bound" :
                event.kind==media::EventKind::playback_started ? "playback.started" : "playback.finished";
            add_envelope(root,type);
            auto* payload=cJSON_AddObjectToObject(root,"payload");
            add_decimal_u64(payload,"response_id",event.response_id);
            add_decimal_u64(payload,"capture_id",event.identity.capture_id);
            add_decimal_u64(payload,"request_id",event.identity.request_id);
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            add_decimal_u64(payload,"owner_token",event.identity.owner_token);
            add_decimal_u64(payload,"first_group",event.first_group);
            add_decimal_u64(payload,"end_group",event.end_group);
#endif
            add_decimal_u64(payload,"samples",event.samples);
            cJSON_AddBoolToObject(payload,"cancelled",event.cancelled);
            cJSON_AddNumberToObject(payload,"error",event.error);
            send_json(root); break;
        }
        }
    }
}

void voice_task(void*) {
    publish(VoiceEventKind::connecting, "Connecting");
    std::uint32_t last_discovery = 0;
    while (true) {
        if (!network_service_connected()) {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            if (g_websocket) close_control();
            g_next_connect=0;
#endif
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            network_service_connect(5000);
#else
            network_service_connect();
#endif
        }
        const auto now = now_ms();
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
        if (g_websocket && (!secure_live() || (g_ready_deadline &&
            static_cast<std::uint64_t>(esp_timer_get_time()/1000)>g_ready_deadline))) retire_control();
        if (g_transport_reset_requested.exchange(false)) close_control();
#endif
        if (network_service_connected() && g_websocket == nullptr &&
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            now - last_discovery >= 250) { // Bootstrap owns bounded backoff; no mDNS wait.
#else
            now - last_discovery >= 3'000) {
#endif
            last_discovery = now;
            connect_websocket();
        }
        Command command{};
        while (xQueueReceive(g_commands, &command, 0) == pdTRUE) {
            handle_command(command);
            free_command(command);
        }
        if (g_transport_reset_requested.exchange(false)) {
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
            close_control();
#else
            media::disconnect();
            g_capture_correlations = {};
#endif
        }
        media::tick();
#ifdef DOODAD_MOQ_DIAGNOSTIC
        voice_moq_diagnostic_tick();
#else
        poll_media();
#endif
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

}  // namespace

bool voice_service_init() {
#if !CONFIG_DOODAD_VOICE_UPLINK
    return true;
#else
    if (g_task != nullptr) return true;
    if (!network_service_init()) return false;
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    if (!secure::init()) return false;
    g_grant=static_cast<secure::Grant*>(heap_caps_calloc(1,sizeof(secure::Grant),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!g_grant) return false;
#endif
    if (!load_agent_state()) {
        ESP_LOGE(kTag, "agent journal initialization failed");
        return false;
    }
    if (esp_event_handler_register(
            kAgentPersistEvent,
            kAgentPersistEventWrite,
            persist_agent_state_event,
            nullptr) != ESP_OK) {
        ESP_LOGE(kTag, "agent journal event handler registration failed");
        return false;
    }
    g_agent_persist_ready = true;
    g_commands = xQueueCreateWithCaps(
        kCommandQueueDepth,
        sizeof(Command),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_events = xQueueCreateWithCaps(
        kEventQueueDepth,
        sizeof(VoiceEvent),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_websocket_payload = static_cast<char*>(heap_caps_malloc(
        kMaximumSignalBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_task_stack = static_cast<StackType_t*>(heap_caps_calloc(
        kVoiceTaskStackBytes / sizeof(StackType_t),
        sizeof(StackType_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_commands || !g_events || !g_websocket_payload || !g_task_stack ||
        !media::init([](media::Signal signal, const char* bytes, std::size_t size) {
            return enqueue(signal == media::Signal::candidate
                ? CommandKind::local_candidate : CommandKind::local_description, bytes, size);
        })) {
        ESP_LOGE(kTag, "voice service initialization failed");
        return false;
    }
#if CONFIG_DOODAD_VOICE_AUTOCONNECT
    g_task = xTaskCreateStaticPinnedToCore(
        voice_task,
        "voice_uplink",
        kVoiceTaskStackBytes,
        nullptr,
        5,
        g_task_stack,
        &g_task_control,
        0);
    if (g_task == nullptr) {
        ESP_LOGE(kTag, "voice task creation failed");
        return false;
    }
#endif
    return true;
#endif
}

bool voice_service_request(
    const char* operation,
    std::uint64_t request_id,
    std::uint32_t duration_ms,
    std::uint64_t owner_token) {
#if !CONFIG_DOODAD_VOICE_UPLINK
    return false;
#else
    if (operation == nullptr || g_commands == nullptr) return false;
#if CONFIG_DOODAD_VOICE_TRANSPORT_MOQ
    if (std::strcmp(operation, "system.voice.cancel") == 0) {
        // The control owner may be blocked sending a WebSocket fragment. Stop
        // local media now; notification to the host is a separate best effort.
        media::cancel();
        if (!enqueue(CommandKind::cancel, nullptr, 0, 0, request_id)) {
            ESP_LOGW(kTag, "local media cancelled; control notification queue full");
        }
        return true;
    }
#endif
    if (std::strcmp(operation, "system.voice.activate") == 0 ||
        std::strcmp(operation, "system.voice.interrupt") == 0) {
        return media::ready() &&
            enqueue(CommandKind::activate, nullptr, 0, 0, request_id);
    }
    if (std::strcmp(operation, "system.voice.finish") == 0) {
        return media::ready() &&
            enqueue(CommandKind::finish, nullptr, 0, 0, request_id);
    }
    if (std::strcmp(operation, "system.voice.cancel") == 0) {
        return g_websocket_connected &&
            enqueue(CommandKind::cancel, nullptr, 0, 0, request_id);
    }
    if (std::strcmp(operation, "voice-notes.record") == 0 ||
        std::strcmp(operation, "voice-notes.again") == 0) {
        return enqueue(
            CommandKind::start,
            nullptr,
            0,
            duration_ms,
            request_id,
            owner_token);
    }
    if (std::strcmp(operation, "voice-notes.finish-capture") == 0 ||
        std::strcmp(operation, "voice-notes.pause") == 0) {
        return enqueue(CommandKind::stop, nullptr, 0, 0, request_id, owner_token);
    }
    return std::strncmp(operation, "voice-notes.", 12) == 0;
#endif
}

void voice_service_set_current_guest_owner(std::uint64_t owner_token) {
    portENTER_CRITICAL(&g_current_guest_owner_lock);
    g_current_guest_owner_token = owner_token;
    portEXIT_CRITICAL(&g_current_guest_owner_lock);
}

bool voice_service_release_owner(std::uint64_t owner_token) {
#if !CONFIG_DOODAD_VOICE_UPLINK
    return true;
#else
    if (owner_token == 0) return true;
    return enqueue(
        CommandKind::release_owner,
        nullptr,
        0,
        0,
        0,
        owner_token,
        pdMS_TO_TICKS(100));
#endif
}

bool voice_service_poll(VoiceEvent& event) {
    return g_events != nullptr && xQueueReceive(g_events, &event, 0) == pdTRUE;
}

bool voice_service_busy() {
    return media::recording();
}

bool voice_service_ready() {
    return media::ready() && g_websocket_connected;
}
