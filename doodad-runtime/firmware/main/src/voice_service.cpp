#include "voice_service.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.hpp"
#include "cJSON.h"
#include "decoder/impl/esp_g711_dec.h"
#include "decoder/impl/esp_opus_dec.h"
#include "display.hpp"
#include "encoder/impl/esp_g711_enc.h"
#include "encoder/impl/esp_opus_enc.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_peer.h"
#include "esp_peer_default.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"
#include "network_service.hpp"
#include "nvs.h"
#include "package_service.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "voice-service";
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
constexpr std::uint32_t kCaptureSampleRate = 16'000;
constexpr std::uint32_t kRtpClockRate = 48'000;
constexpr esp_peer_audio_codec_t kPeerAudioCodec = ESP_PEER_AUDIO_CODEC_OPUS;
constexpr char kCodecName[] = "Opus";
constexpr char kCodecDescription[] = "OPUS/48000/1;PCM/16000/1";
constexpr int kOpusBitrate = 24'000;
#else
constexpr std::uint32_t kCaptureSampleRate = 8'000;
constexpr std::uint32_t kRtpClockRate = 8'000;
constexpr esp_peer_audio_codec_t kPeerAudioCodec = ESP_PEER_AUDIO_CODEC_G711U;
constexpr char kCodecName[] = "PCMU";
constexpr char kCodecDescription[] = "PCMU/8000/1";
#endif
constexpr std::uint32_t kFrameDurationMs = 20;
constexpr std::size_t kSamplesPerFrame =
    kCaptureSampleRate * kFrameDurationMs / 1'000;
constexpr std::size_t kMaximumSignalBytes = 16 * 1024;
constexpr std::size_t kMaximumEncodedBytes = 1500;
constexpr std::size_t kMaximumDecodedBytes = kMaximumEncodedBytes * 2;
constexpr std::uint32_t kPlaybackIdleMs = 240;
constexpr int kSignalingFragmentBytes = 256;
constexpr std::size_t kSdpChunkBytes = 64;
constexpr std::size_t kSignalQueueDepth = 6;
constexpr std::size_t kCommandQueueDepth = 6;
constexpr std::size_t kEventQueueDepth = 6;
constexpr std::size_t kPlaybackQueueDepth = 12;
constexpr std::size_t kPlaybackSlotCount = 3;
constexpr std::size_t kCaptureCorrelationCount = 8;
constexpr std::size_t kPlaybackPrebufferFrames = 3;
constexpr std::uint32_t kPlaybackPrebufferTimeoutMs = 80;
constexpr std::size_t kSpeakerDmaBufferLength = 128;
constexpr std::size_t kSpeakerDmaBufferCount = 3;
constexpr std::size_t kMicrophoneDmaBufferLength = 96;
constexpr std::size_t kMicrophoneDmaBufferCount = 2;
constexpr std::size_t kAudioDmaReserveBytes = 4 * 1024;
constexpr std::uint32_t kPlaybackUnderflowMinimumMs =
    kSpeakerDmaBufferLength * kSpeakerDmaBufferCount * 1'000 /
    kCaptureSampleRate;
constexpr std::uint16_t kNearFullScaleThreshold = 30'000;
constexpr std::uint16_t kPlaybackSilencePeakThreshold = 24;
#ifdef CONFIG_DOODAD_SPEAKER_VOLUME
constexpr std::uint8_t kSpeakerVolume = CONFIG_DOODAD_SPEAKER_VOLUME;
#else
constexpr std::uint8_t kSpeakerVolume = 96;
#endif
// Keep the WebRTC worker in PSRAM so ICE, DTLS, signaling, and audio callbacks
// have ample stack without consuming the small internal-RAM pool.
constexpr std::size_t kVoiceTaskStackBytes = 64 * 1024;

enum class CommandKind : std::uint8_t {
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
};

struct CaptureCorrelation {
    std::uint64_t capture_id = 0;
    std::uint64_t request_id = 0;
    std::uint64_t owner_token = 0;
};

struct PlaybackFrame {
    std::array<std::int16_t, kSamplesPerFrame> samples{};
    std::uint16_t sample_count = 0;
    std::uint16_t peak = 0;
    std::uint32_t generation = 0;
};

struct PlaybackTelemetrySnapshot {
    std::uint32_t received = 0;
    std::uint32_t queued = 0;
    std::uint32_t submitted = 0;
    std::uint32_t rejected = 0;
    std::uint32_t dropped = 0;
    std::uint32_t underflows = 0;
    std::uint32_t prebuffer_starts = 0;
    std::uint32_t queue_high_water = 0;
    std::uint32_t speaker_failures = 0;
    std::uint32_t near_full_scale_samples = 0;
};

QueueHandle_t g_commands = nullptr;
QueueHandle_t g_events = nullptr;
QueueHandle_t g_playback_frames = nullptr;
TaskHandle_t g_task = nullptr;
TaskHandle_t g_peer_task = nullptr;
volatile bool g_peer_loop_running = false;
StaticTask_t g_task_control{};
StackType_t* g_task_stack = nullptr;
esp_websocket_client_handle_t g_websocket = nullptr;
esp_peer_handle_t g_peer = nullptr;
void* g_encoder = nullptr;
void* g_decoder = nullptr;
void* g_audio_dma_reserve = nullptr;
bool g_websocket_connected = false;
bool g_peer_connected = false;
std::atomic<bool> g_recording{false};
std::atomic<bool> g_playing{false};
bool g_mdns_initialized = false;
bool g_microphone_ready = false;
std::uint64_t g_sequence = 0;
std::uint64_t g_active_request = 0;
std::uint64_t g_active_owner_token = 0;
std::uint64_t g_active_capture_id = 0;
std::uint64_t g_capture_sequence = 0;
std::array<CaptureCorrelation, kCaptureCorrelationCount>
    g_capture_correlations{};
std::size_t g_capture_correlation_cursor = 0;
portMUX_TYPE g_current_guest_owner_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint64_t g_current_guest_owner_token = 0;
std::uint32_t g_capture_duration_ms = 8'000;
std::uint32_t g_capture_started_ms = 0;
std::uint32_t g_encoded_frames = 0;
std::uint32_t g_dropped_frames = 0;
std::uint32_t g_encoded_bytes = 0;
std::atomic<std::uint32_t> g_received_frames{0};
std::atomic<std::uint32_t> g_queued_playback_frames{0};
std::atomic<std::uint32_t> g_submitted_playback_frames{0};
std::atomic<std::uint32_t> g_rejected_playback_frames{0};
std::atomic<std::uint32_t> g_dropped_playback_frames{0};
std::atomic<std::uint32_t> g_playback_underflows{0};
std::atomic<std::uint32_t> g_prebuffer_starts{0};
std::atomic<std::uint32_t> g_playback_queue_high_water{0};
std::atomic<std::uint32_t> g_speaker_submission_failures{0};
std::atomic<std::uint32_t> g_near_full_scale_samples{0};
std::atomic<std::uint32_t> g_last_valid_playback_packet_ms{0};
std::atomic<std::uint32_t> g_last_audible_playback_packet_ms{0};
std::atomic<std::uint32_t> g_playback_underflow_candidate_ms{0};
std::atomic<std::uint32_t> g_prebuffer_started_ms{0};
std::uint32_t g_last_level_publish_ms = 0;
std::uint16_t g_peak_pcm = 0;
std::atomic<std::uint16_t> g_playback_peak_pcm{0};
PlaybackFrame* g_playback_slots = nullptr;
std::uint8_t g_playback_slot_index = 0;
std::int8_t g_last_playback_slot = -1;
std::atomic<bool> g_prebuffering{false};
std::atomic<bool> g_playback_underflow_candidate{false};
std::atomic<bool> g_playback_reset_requested{false};
std::atomic<bool> g_have_audible_playback_packet{false};
std::atomic<bool> g_last_queued_playback_frame_silent{false};
std::atomic<bool> g_playback_accepting{false};
std::atomic<std::uint32_t> g_playback_generation{1};
std::atomic<std::uint32_t> g_decoder_users{0};
portMUX_TYPE g_playback_state_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_playback_telemetry_mux = portMUX_INITIALIZER_UNLOCKED;
PlaybackTelemetrySnapshot g_playback_baseline{};
std::uint16_t g_playback_run_peak_pcm = 0;
std::uint32_t g_playback_run_queue_high_water = 0;
bool g_playback_baseline_active = false;
std::int16_t* g_pcm = nullptr;
std::uint8_t* g_encoded_audio = nullptr;
std::int16_t* g_decoded_audio = nullptr;
char* g_websocket_payload = nullptr;

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

bool acquire_audio_dma_reserve() {
    if (g_audio_dma_reserve != nullptr) return true;
    g_audio_dma_reserve = heap_caps_malloc(
        kAudioDmaReserveBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (g_audio_dma_reserve == nullptr) {
        ESP_LOGE(
            kTag,
            "audio DMA reserve failed free=%u largest=%u",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
        return false;
    }
    ESP_LOGI(
        kTag,
        "audio DMA reserved bytes=%u free=%u largest=%u",
        static_cast<unsigned>(kAudioDmaReserveBytes),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    return true;
}

void release_audio_dma_reserve() {
    if (g_audio_dma_reserve == nullptr) return;
    heap_caps_free(g_audio_dma_reserve);
    g_audio_dma_reserve = nullptr;
}

template <typename T>
void atomic_max(std::atomic<T>& destination, T candidate) {
    auto current = destination.load(std::memory_order_relaxed);
    while (current < candidate &&
           !destination.compare_exchange_weak(
               current, candidate, std::memory_order_relaxed)) {}
}

PlaybackTelemetrySnapshot playback_telemetry_snapshot() {
    return {
        g_received_frames.load(std::memory_order_relaxed),
        g_queued_playback_frames.load(std::memory_order_relaxed),
        g_submitted_playback_frames.load(std::memory_order_relaxed),
        g_rejected_playback_frames.load(std::memory_order_relaxed),
        g_dropped_playback_frames.load(std::memory_order_relaxed),
        g_playback_underflows.load(std::memory_order_relaxed),
        g_prebuffer_starts.load(std::memory_order_relaxed),
        g_playback_queue_high_water.load(std::memory_order_relaxed),
        g_speaker_submission_failures.load(std::memory_order_relaxed),
        g_near_full_scale_samples.load(std::memory_order_relaxed),
    };
}

PlaybackTelemetrySnapshot telemetry_delta(
    const PlaybackTelemetrySnapshot& current,
    const PlaybackTelemetrySnapshot& baseline) {
    return {
        current.received - baseline.received,
        current.queued - baseline.queued,
        current.submitted - baseline.submitted,
        current.rejected - baseline.rejected,
        current.dropped - baseline.dropped,
        current.underflows - baseline.underflows,
        current.prebuffer_starts - baseline.prebuffer_starts,
        current.queue_high_water,
        current.speaker_failures - baseline.speaker_failures,
        current.near_full_scale_samples - baseline.near_full_scale_samples,
    };
}

std::uint32_t invalidate_playback_generation() {
    g_playback_accepting.store(false, std::memory_order_release);
    return g_playback_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void note_accepted_playback_frame(
    std::uint16_t peak,
    std::uint32_t near_full_scale,
    std::uint32_t queue_depth) {
    portENTER_CRITICAL(&g_playback_telemetry_mux);
    if (!g_playback_baseline_active) {
        g_playback_baseline = playback_telemetry_snapshot();
        // The first frame has already reached the cumulative counters. Move
        // the baseline back by its contribution so every physical playback
        // run includes the complete prebuffer, not just post-start packets.
        if (g_playback_baseline.received != 0) {
            --g_playback_baseline.received;
        }
        if (g_playback_baseline.queued != 0) {
            --g_playback_baseline.queued;
        }
        g_playback_baseline.near_full_scale_samples -= std::min(
            g_playback_baseline.near_full_scale_samples,
            near_full_scale);
        g_playback_run_peak_pcm = peak;
        g_playback_run_queue_high_water = queue_depth;
        g_playback_baseline_active = true;
    } else {
        g_playback_run_peak_pcm = std::max(g_playback_run_peak_pcm, peak);
        g_playback_run_queue_high_water = std::max(
            g_playback_run_queue_high_water,
            queue_depth);
    }
    portEXIT_CRITICAL(&g_playback_telemetry_mux);
}

bool take_playback_run_telemetry(
    PlaybackTelemetrySnapshot& delta,
    std::uint16_t& peak,
    std::uint32_t& queue_high_water) {
    portENTER_CRITICAL(&g_playback_telemetry_mux);
    const bool active = g_playback_baseline_active;
    if (active) {
        delta = telemetry_delta(
            playback_telemetry_snapshot(), g_playback_baseline);
        delta.queue_high_water = g_playback_run_queue_high_water;
        peak = g_playback_run_peak_pcm;
        queue_high_water = g_playback_run_queue_high_water;
        g_playback_baseline_active = false;
        g_playback_run_peak_pcm = 0;
        g_playback_run_queue_high_water = 0;
    }
    portEXIT_CRITICAL(&g_playback_telemetry_mux);
    return active;
}

void abandon_playback_run_telemetry() {
    portENTER_CRITICAL(&g_playback_telemetry_mux);
    g_playback_baseline_active = false;
    g_playback_run_peak_pcm = 0;
    g_playback_run_queue_high_water = 0;
    portEXIT_CRITICAL(&g_playback_telemetry_mux);
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
    cJSON_AddStringToObject(root, "session_id", "watch-uplink");
    cJSON_AddStringToObject(
        root, "device_id", doodad::board::identity().device_id);
    cJSON_AddNumberToObject(root, "seq", ++g_sequence);
}

bool send_json(cJSON* root) {
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
    }
    cJSON_free(encoded);
    return sent == length;
}

void send_simple(const char* type) {
    auto* root = cJSON_CreateObject();
    add_envelope(root, type);
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
    cJSON_AddStringToObject(payload, "transport", "webrtc");
    cJSON_AddStringToObject(payload, "audio", kCodecDescription);
    cJSON_AddNumberToObject(payload, "frame_ms", kFrameDurationMs);
    auto* capabilities = cJSON_AddObjectToObject(payload, "capabilities");
    cJSON_AddBoolToObject(capabilities, "touch", true);
    cJSON_AddBoolToObject(capabilities, "microphone", true);
    cJSON_AddBoolToObject(capabilities, "speaker", true);
    cJSON_AddBoolToObject(capabilities, "haptic",
        std::strcmp(identity.board, "t-watch-s3") == 0);
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
    const auto* install = cJSON_GetObjectItemCaseSensitive(background, "install_state");
    display_publish_agent_state(
        voice_phase_value(cJSON_IsString(phase) ? phase->valuestring : "error"),
        cJSON_IsNumber(running) ? std::clamp(running->valueint, 0, 255) : 0,
        cJSON_IsTrue(focused), cJSON_IsTrue(review), cJSON_IsTrue(completion),
        cJSON_IsNumber(install) ? std::clamp(install->valueint, 0, 4) : 0,
        cJSON_IsString(transcript) ? transcript->valuestring : "",
        cJSON_IsString(response) ? response->valuestring : "");
    cJSON_Delete(payload);
}

int peer_state(esp_peer_state_t state, void*) {
    ESP_LOGI(kTag, "peer state=%d", static_cast<int>(state));
    if (state == ESP_PEER_STATE_CONNECTED) {
        g_peer_connected = true;
        g_playback_accepting.store(true, std::memory_order_release);
        send_simple("peer.ready");
        publish(VoiceEventKind::ready, "Voice link ready");
    } else if (state == ESP_PEER_STATE_DISCONNECTED ||
               state == ESP_PEER_STATE_CONNECT_FAILED ||
               state == ESP_PEER_STATE_CLOSED) {
        g_peer_connected = false;
        g_recording.store(false, std::memory_order_release);
        invalidate_playback_generation();
        g_playback_reset_requested.store(true, std::memory_order_release);
        display_publish_agent_state(0, 0, false, false, false, 0, "", "");
    }
    return 0;
}

int peer_message(esp_peer_msg_t* message, void*) {
    if (message == nullptr || message->data == nullptr || message->size <= 0) {
        return 0;
    }
    ESP_LOGI(
        kTag,
        "local peer message type=%d size=%d",
        static_cast<int>(message->type),
        message->size);
    // esp_peer invokes this callback from inside its state machine. Queue the
    // payload and let voice_task send it after esp_peer_new_connection() or
    // esp_peer_main_loop() has returned; re-entering the WebSocket client from
    // here can report a successful write without putting bytes on the wire.
    return enqueue(
        message->type == ESP_PEER_MSG_TYPE_CANDIDATE
            ? CommandKind::local_candidate
            : CommandKind::local_description,
        reinterpret_cast<const char*>(message->data),
        static_cast<std::size_t>(message->size))
        ? 0
        : -1;
}

bool start_microphone();

bool open_decoder() {
    if (g_decoder != nullptr) return true;
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
    esp_opus_dec_cfg_t configuration = ESP_OPUS_DEC_CONFIG_DEFAULT();
    configuration.sample_rate = kCaptureSampleRate;
    configuration.channel = 1;
    configuration.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS;
    configuration.self_delimited = false;
    const auto result = esp_opus_dec_open(
        &configuration, sizeof(configuration), &g_decoder);
#else
    esp_g711_dec_cfg_t configuration = ESP_G711_DEC_CONFIG_DEFAULT();
    const auto result = esp_g711_dec_open(
        &configuration, sizeof(configuration), &g_decoder);
#endif
    if (result != ESP_AUDIO_ERR_OK) {
        g_decoder = nullptr;
        ESP_LOGE(kTag, "%s decoder initialization failed", kCodecName);
        return false;
    }
    ESP_LOGI(
        kTag, "%s decoder ready pcm_rate=%u frame_samples=%u",
        kCodecName,
        static_cast<unsigned>(kCaptureSampleRate),
        static_cast<unsigned>(kSamplesPerFrame));
    return true;
}

void close_decoder() {
    // A receive callback may already have acquired the decoder when capture
    // invalidates the playback generation. Wait for that one bounded decode
    // to finish before returning its internal heap to the microphone.
    while (g_decoder_users.load(std::memory_order_acquire) != 0) {
        vTaskDelay(1);
    }
    if (g_decoder != nullptr) {
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
        esp_opus_dec_close(g_decoder);
#else
        esp_g711_dec_close(g_decoder);
#endif
        g_decoder = nullptr;
        ESP_LOGI(kTag, "%s decoder closed", kCodecName);
    }
}

void clear_playback_slots() {
    if (g_playback_slots != nullptr) {
        for (std::size_t index = 0; index < kPlaybackSlotCount; ++index) {
            g_playback_slots[index] = PlaybackFrame{};
        }
    }
    g_playback_slot_index = 0;
    g_last_playback_slot = -1;
}

void stop_and_reset_playback(const char* reason) {
    const auto generation = invalidate_playback_generation();
    if (doodad::board::speaker_running()) doodad::board::speaker_end();
    acquire_audio_dma_reserve();
    g_playing.store(false, std::memory_order_release);
    if (g_playback_frames != nullptr) xQueueReset(g_playback_frames);
    clear_playback_slots();
    portENTER_CRITICAL(&g_playback_state_mux);
    g_prebuffering.store(false, std::memory_order_release);
    g_playback_underflow_candidate.store(false, std::memory_order_release);
    g_playback_underflow_candidate_ms.store(0, std::memory_order_relaxed);
    g_prebuffer_started_ms.store(0, std::memory_order_relaxed);
    g_last_valid_playback_packet_ms.store(0, std::memory_order_relaxed);
    g_last_audible_playback_packet_ms.store(0, std::memory_order_relaxed);
    g_have_audible_playback_packet.store(false, std::memory_order_release);
    g_last_queued_playback_frame_silent.store(
        false, std::memory_order_release);
    g_playback_reset_requested.store(false, std::memory_order_release);
    portEXIT_CRITICAL(&g_playback_state_mux);
    abandon_playback_run_telemetry();
    ESP_LOGI(
        kTag,
        "downlink reset reason=%s received=%u queued=%u submitted=%u "
        "rejected=%u dropped=%u generation=%u",
        reason == nullptr ? "unspecified" : reason,
        static_cast<unsigned>(g_received_frames.load()),
        static_cast<unsigned>(g_queued_playback_frames.load()),
        static_cast<unsigned>(g_submitted_playback_frames.load()),
        static_cast<unsigned>(g_rejected_playback_frames.load()),
        static_cast<unsigned>(g_dropped_playback_frames.load()),
        static_cast<unsigned>(generation));
}

int peer_audio_info(esp_peer_audio_stream_info_t* info, void*) {
    if (info == nullptr) return -1;
    ESP_LOGI(
        kTag,
        "remote audio codec=%d rate=%u channels=%u",
        static_cast<int>(info->codec),
        static_cast<unsigned>(info->sample_rate),
        static_cast<unsigned>(info->channel));
    const bool channel_supported = info->channel == 1
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
        || info->channel == 2
#endif
        ;
    return info->codec == kPeerAudioCodec &&
            info->sample_rate == kRtpClockRate && channel_supported
        ? 0
        : -1;
}

int peer_audio_data(esp_peer_audio_frame_t* incoming, void*) {
    if (incoming == nullptr || incoming->data == nullptr || incoming->size <= 0 ||
        incoming->size > static_cast<int>(kMaximumEncodedBytes)) {
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    g_received_frames.fetch_add(1, std::memory_order_relaxed);
    const auto generation =
        g_playback_generation.load(std::memory_order_acquire);
    if (!g_playback_accepting.load(std::memory_order_acquire) ||
        g_recording.load(std::memory_order_acquire)) {
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    g_decoder_users.fetch_add(1, std::memory_order_acq_rel);
    // Recheck after acquiring the lease. If capture won the race, it will now
    // wait for this lease and we must not open or touch the decoder.
    if (!g_playback_accepting.load(std::memory_order_acquire) ||
        g_recording.load(std::memory_order_acquire)) {
        g_decoder_users.fetch_sub(1, std::memory_order_acq_rel);
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    if (!open_decoder()) {
        g_decoder_users.fetch_sub(1, std::memory_order_acq_rel);
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    esp_audio_dec_in_raw_t raw{
        incoming->data, static_cast<std::uint32_t>(incoming->size), 0,
        ESP_AUDIO_DEC_RECOVERY_NONE};
    esp_audio_dec_out_frame_t decoded{
        reinterpret_cast<std::uint8_t*>(g_decoded_audio),
        kMaximumDecodedBytes,
        0,
        0};
    esp_audio_dec_info_t information{};
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
    const auto decode_result = esp_opus_dec_decode(
        g_decoder, &raw, &decoded, &information);
#else
    const auto decode_result = esp_g711u_dec_decode(
        g_decoder, &raw, &decoded, &information);
#endif
    g_decoder_users.fetch_sub(1, std::memory_order_acq_rel);
    if (decode_result != ESP_AUDIO_ERR_OK || decoded.decoded_size == 0) {
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(kTag, "discarding invalid %s downlink frame", kCodecName);
        return -1;
    }

    const auto sample_count = decoded.decoded_size / sizeof(std::int16_t);
    if (sample_count == 0 || sample_count > kSamplesPerFrame) {
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(
            kTag,
            "discarding unexpected %s packet with %u samples",
            kCodecName,
            static_cast<unsigned>(sample_count));
        return -1;
    }
    std::uint16_t peak = 0;
    std::uint32_t near_full_scale = 0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const auto sample = g_decoded_audio[index];
        const auto magnitude = static_cast<std::uint16_t>(
            sample == INT16_MIN ? INT16_MAX : std::abs(sample));
        peak = std::max(peak, magnitude);
        if (magnitude >= kNearFullScaleThreshold) ++near_full_scale;
    }
    atomic_max(g_playback_peak_pcm, peak);
    g_near_full_scale_samples.fetch_add(
        near_full_scale, std::memory_order_relaxed);
    const auto packet_ms = now_ms();
    const bool silent = peak < kPlaybackSilencePeakThreshold;
    if (silent) {
        // Preserve pauses and final-frame padding only inside a bounded active
        // utterance. This prevents an alternate sender's idle silent
        // keepalives from retaining the shared speaker codec indefinitely.
        const auto last_audible = g_last_audible_playback_packet_ms.load(
            std::memory_order_acquire);
        if ((!g_playing.load(std::memory_order_acquire) &&
             !g_prebuffering.load(std::memory_order_acquire)) ||
            !g_have_audible_playback_packet.load(std::memory_order_acquire) ||
            packet_ms - last_audible >= kPlaybackIdleMs) {
            return 0;
        }
    }

    // A capture, cancellation, or transport close may have invalidated this
    // callback while the codec was decoding. Never enqueue it as current data.
    if (!g_playback_accepting.load(std::memory_order_acquire) ||
        g_recording.load(std::memory_order_acquire) ||
        generation != g_playback_generation.load(std::memory_order_acquire)) {
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    PlaybackFrame frame{};
    std::copy_n(g_decoded_audio, sample_count, frame.samples.begin());
    frame.sample_count = static_cast<std::uint16_t>(sample_count);
    frame.peak = peak;
    frame.generation = generation;
    if (xQueueSend(g_playback_frames, &frame, 0) != pdTRUE) {
        g_dropped_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    g_queued_playback_frames.fetch_add(1, std::memory_order_relaxed);
    const auto queue_depth = static_cast<std::uint32_t>(
        uxQueueMessagesWaiting(g_playback_frames));
    atomic_max(g_playback_queue_high_water, queue_depth);

    // Commit cross-core playback state only if the generation is still live.
    // The reset path uses the same short critical section after invalidating
    // the generation, so an in-flight callback can commit either before the
    // reset (and be cleared) or after it (and be rejected), never across it.
    portENTER_CRITICAL(&g_playback_state_mux);
    const bool still_current =
        g_playback_accepting.load(std::memory_order_acquire) &&
        !g_recording.load(std::memory_order_acquire) &&
        generation == g_playback_generation.load(std::memory_order_acquire);
    if (!still_current) {
        portEXIT_CRITICAL(&g_playback_state_mux);
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    if (!silent) {
        g_have_audible_playback_packet.store(true, std::memory_order_release);
        g_last_audible_playback_packet_ms.store(
            packet_ms, std::memory_order_release);
    }
    if (g_playback_underflow_candidate.load(std::memory_order_acquire)) {
        // isPlaying() describes M5Unified's source queue, while its I2S DMA
        // ring still owns up to 48 ms of rendered audio. Only count starvation
        // that outlasts that hardware tail; shorter source-queue gaps are not
        // audible underflows.
        const auto starvation_ms = packet_ms -
            g_playback_underflow_candidate_ms.load(std::memory_order_relaxed);
        if (!silent &&
            !g_last_queued_playback_frame_silent.load(
                std::memory_order_acquire) &&
            starvation_ms >= kPlaybackUnderflowMinimumMs &&
            packet_ms - g_last_valid_playback_packet_ms.load(
                std::memory_order_acquire) < kPlaybackIdleMs) {
            g_playback_underflows.fetch_add(1, std::memory_order_relaxed);
        }
        g_playback_underflow_candidate.store(false, std::memory_order_release);
        g_playback_underflow_candidate_ms.store(0, std::memory_order_relaxed);
    }
    g_last_valid_playback_packet_ms.store(packet_ms, std::memory_order_release);
    g_last_queued_playback_frame_silent.store(
        silent, std::memory_order_release);
    note_accepted_playback_frame(peak, near_full_scale, queue_depth);
    if (!g_playing.load(std::memory_order_acquire) &&
        !g_prebuffering.exchange(true, std::memory_order_acq_rel)) {
        g_prebuffer_started_ms.store(packet_ms, std::memory_order_release);
    }
    portEXIT_CRITICAL(&g_playback_state_mux);
    return 0;
}

void discard_stale_playback_frames() {
    PlaybackFrame head{};
    while (xQueuePeek(g_playback_frames, &head, 0) == pdTRUE) {
        const bool current =
            g_playback_accepting.load(std::memory_order_acquire) &&
            head.generation ==
                g_playback_generation.load(std::memory_order_acquire);
        if (current) break;
        if (xQueueReceive(g_playback_frames, &head, 0) != pdTRUE) break;
        g_rejected_playback_frames.fetch_add(1, std::memory_order_relaxed);
    }
}

void play_queued_audio() {
    discard_stale_playback_frames();
    if (g_recording.load(std::memory_order_acquire) ||
        !g_playback_accepting.load(std::memory_order_acquire)) {
        return;
    }

    auto waiting = uxQueueMessagesWaiting(g_playback_frames);
    if (!g_playing.load(std::memory_order_acquire)) {
        if (waiting == 0) return;
        if (!g_prebuffering.exchange(true, std::memory_order_acq_rel)) {
            g_prebuffer_started_ms.store(now_ms(), std::memory_order_release);
        }
        const auto prebuffer_elapsed = now_ms() -
            g_prebuffer_started_ms.load(std::memory_order_acquire);
        if (waiting < kPlaybackPrebufferFrames &&
            prebuffer_elapsed < kPlaybackPrebufferTimeoutMs) {
            return;
        }

        while (doodad::board::microphone_recording()) vTaskDelay(1);
        if (doodad::board::microphone_running()) {
            doodad::board::microphone_end();
        }
        g_microphone_ready = false;
        release_audio_dma_reserve();
        if (!doodad::board::speaker_begin()) {
            acquire_audio_dma_reserve();
            g_speaker_submission_failures.fetch_add(
                1, std::memory_order_relaxed);
            g_dropped_playback_frames.fetch_add(
                waiting, std::memory_order_relaxed);
            xQueueReset(g_playback_frames);
            g_prebuffering.store(false, std::memory_order_release);
            ESP_LOGE(kTag, "speaker initialization failed");
            return;
        }
        g_playing.store(true, std::memory_order_release);
        g_prebuffering.store(false, std::memory_order_release);
        g_playback_underflow_candidate.store(false, std::memory_order_release);
        g_playback_underflow_candidate_ms.store(0, std::memory_order_relaxed);
        g_prebuffer_starts.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGI(
            kTag,
            "downlink playback started buffered=%u wait_ms=%u volume=%u",
            static_cast<unsigned>(waiting),
            static_cast<unsigned>(prebuffer_elapsed),
            static_cast<unsigned>(kSpeakerVolume));
    }

    while (!g_recording.load(std::memory_order_acquire) &&
           g_playback_accepting.load(std::memory_order_acquire)) {
        // The board adapter consumes or retains each persistent slot according
        // to its native I2S implementation before returning.
        auto& slot = g_playback_slots[g_playback_slot_index];
        if (xQueueReceive(g_playback_frames, &slot, 0) != pdTRUE) break;
        if (slot.generation !=
                g_playback_generation.load(std::memory_order_acquire) ||
            !g_playback_accepting.load(std::memory_order_acquire) ||
            g_recording.load(std::memory_order_acquire)) {
            g_rejected_playback_frames.fetch_add(
                1, std::memory_order_relaxed);
            continue;
        }
        const auto submitted_slot = g_playback_slot_index;
        if (doodad::board::speaker_play(
                slot.samples.data(), slot.sample_count, kCaptureSampleRate)) {
            const auto submitted =
                g_submitted_playback_frames.fetch_add(
                    1, std::memory_order_relaxed) + 1;
            g_last_playback_slot = static_cast<std::int8_t>(submitted_slot);
            g_playback_slot_index = static_cast<std::uint8_t>(
                (g_playback_slot_index + 1) % kPlaybackSlotCount);
            if (submitted <= kPlaybackSlotCount || submitted % 50 == 0) {
                ESP_LOGI(
                    kTag,
                    "downlink frame=%u peak=%u samples=%u slot=%u buffer=%p",
                    static_cast<unsigned>(submitted),
                    static_cast<unsigned>(slot.peak),
                    static_cast<unsigned>(slot.sample_count),
                    static_cast<unsigned>(submitted_slot),
                    static_cast<void*>(slot.samples.data()));
            }
        } else {
            g_dropped_playback_frames.fetch_add(1, std::memory_order_relaxed);
            g_speaker_submission_failures.fetch_add(
                1, std::memory_order_relaxed);
        }
    }
}

void finish_playback_if_idle() {
    if (!g_playing.load(std::memory_order_acquire) ||
        uxQueueMessagesWaiting(g_playback_frames) != 0) {
        return;
    }
    if (doodad::board::speaker_playing()) return;
    const auto idle_ms = now_ms() -
        g_last_valid_playback_packet_ms.load(std::memory_order_acquire);
    if (idle_ms < kPlaybackIdleMs) {
        if (!g_last_queued_playback_frame_silent.load(
                std::memory_order_acquire) &&
            !g_playback_underflow_candidate.exchange(
                true, std::memory_order_acq_rel)) {
            g_playback_underflow_candidate_ms.store(
                now_ms(), std::memory_order_release);
        }
        return;
    }
    doodad::board::speaker_end();
    acquire_audio_dma_reserve();
    g_playing.store(false, std::memory_order_release);
    g_prebuffering.store(false, std::memory_order_release);
    g_playback_underflow_candidate.store(false, std::memory_order_release);
    g_playback_underflow_candidate_ms.store(0, std::memory_order_relaxed);
    g_prebuffer_started_ms.store(0, std::memory_order_relaxed);
    PlaybackTelemetrySnapshot run{};
    std::uint16_t run_peak = 0;
    std::uint32_t run_high_water = 0;
    take_playback_run_telemetry(run, run_peak, run_high_water);
    const auto total = playback_telemetry_snapshot();
    ESP_LOGI(
        kTag,
        "downlink playback stopped received=%u queued=%u submitted=%u "
        "rejected=%u dropped=%u underflow=%u prebuffer=%u high_water=%u "
        "speaker_fail=%u peak=%u near_full=%u volume=%u codec=%u "
        "pcm_rate=%u slot=%d "
        "last_silent=%u audible_age_ms=%u total_received=%u "
        "total_dropped=%u total_underflow=%u total_speaker_fail=%u",
        static_cast<unsigned>(run.received),
        static_cast<unsigned>(run.queued),
        static_cast<unsigned>(run.submitted),
        static_cast<unsigned>(run.rejected),
        static_cast<unsigned>(run.dropped),
        static_cast<unsigned>(run.underflows),
        static_cast<unsigned>(run.prebuffer_starts),
        static_cast<unsigned>(run_high_water),
        static_cast<unsigned>(run.speaker_failures),
        static_cast<unsigned>(run_peak),
        static_cast<unsigned>(run.near_full_scale_samples),
        static_cast<unsigned>(kSpeakerVolume),
        static_cast<unsigned>(kPeerAudioCodec),
        static_cast<unsigned>(kCaptureSampleRate),
        static_cast<int>(g_last_playback_slot),
        static_cast<unsigned>(g_last_queued_playback_frame_silent.load()),
        static_cast<unsigned>(
            now_ms() - g_last_audible_playback_packet_ms.load()),
        static_cast<unsigned>(total.received),
        static_cast<unsigned>(total.dropped),
        static_cast<unsigned>(total.underflows),
        static_cast<unsigned>(total.speaker_failures));
    clear_playback_slots();
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

bool create_peer() {
    if (g_peer != nullptr) return true;
    esp_peer_default_cfg_t defaults{};
    defaults.agent_recv_timeout = 100;
    defaults.rtp_cfg.audio_recv_jitter.cache_timeout = 100;
    // Keep enough room for several 20 ms voice packets of scheduling jitter
    // without reserving video-oriented defaults from scarce internal RAM.
    defaults.rtp_cfg.audio_recv_jitter.cache_size = 2 * 1024;
    defaults.rtp_cfg.send_pool_size = 2 * 1024;
    defaults.rtp_cfg.send_queue_num = 12;
    defaults.rtp_cfg.max_resend_count = 2;
    // This is a same-LAN uplink, so one UDP host candidate is sufficient and
    // avoids holding candidate-gathering resources for unused interfaces.
    defaults.max_candidates = 1;
    esp_peer_cfg_t configuration{};
    configuration.role = ESP_PEER_ROLE_CONTROLLING;
    configuration.audio_info.codec = kPeerAudioCodec;
    configuration.audio_info.sample_rate = kRtpClockRate;
    configuration.audio_info.channel = 1;
    configuration.audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV;
    configuration.video_dir = ESP_PEER_MEDIA_DIR_NONE;
    configuration.no_auto_reconnect = true;
    configuration.on_state = peer_state;
    configuration.on_msg = peer_message;
    configuration.on_audio_info = peer_audio_info;
    configuration.on_audio_data = peer_audio_data;
    configuration.extra_cfg = &defaults;
    configuration.extra_size = sizeof(defaults);
    const auto result = esp_peer_open(
        &configuration, esp_peer_get_default_impl(), &g_peer);
    if (result != ESP_PEER_ERR_NONE) {
        ESP_LOGE(kTag, "esp_peer_open failed: %d", result);
        g_peer = nullptr;
        return false;
    }
    return true;
}

bool start_peer_loop() {
    if (g_peer == nullptr || g_peer_task != nullptr) return false;
    g_peer_loop_running = true;
    if (xTaskCreatePinnedToCoreWithCaps(
            [](void*) {
                while (g_peer_loop_running) {
                    if (g_peer != nullptr) esp_peer_main_loop(g_peer);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                g_peer_task = nullptr;
                vTaskDelete(nullptr);
            },
            "voice_peer", 32 * 1024, nullptr, 5, &g_peer_task, 1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        g_peer_loop_running = false;
        esp_peer_close(g_peer);
        g_peer = nullptr;
        ESP_LOGE(kTag, "peer loop task creation failed");
        return false;
    }
    return true;
}

void close_encoder() {
    if (g_encoder != nullptr) {
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
        esp_opus_enc_close(g_encoder);
#else
        esp_g711_enc_close(g_encoder);
#endif
        g_encoder = nullptr;
        ESP_LOGI(kTag, "%s encoder closed", kCodecName);
    }
}

void close_peer() {
    g_peer_loop_running = false;
    while (g_peer_task != nullptr) vTaskDelay(pdMS_TO_TICKS(1));
    if (g_peer != nullptr) {
        esp_peer_close(g_peer);
        g_peer = nullptr;
    }
    g_peer_connected = false;
    stop_and_reset_playback("peer closed");
    close_decoder();
    close_encoder();
}

bool open_encoder() {
    if (g_encoder != nullptr) return true;
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
    esp_opus_enc_config_t configuration = ESP_OPUS_ENC_CONFIG_DEFAULT();
    configuration.sample_rate = kCaptureSampleRate;
    configuration.channel = 1;
    configuration.bits_per_sample = 16;
    configuration.bitrate = kOpusBitrate;
    configuration.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    configuration.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    configuration.complexity = 0;
    configuration.enable_fec = false;
    configuration.enable_dtx = false;
    configuration.enable_vbr = true;
    const auto open_result = esp_opus_enc_open(
        &configuration, sizeof(configuration), &g_encoder);
#else
    esp_g711_enc_config_t configuration = ESP_G711_ENC_CONFIG_DEFAULT();
    configuration.sample_rate = kCaptureSampleRate;
    configuration.channel = 1;
    configuration.bits_per_sample = 16;
    configuration.frame_duration = kFrameDurationMs;
    const auto open_result = esp_g711u_enc_open(
        &configuration, sizeof(configuration), &g_encoder);
#endif
    if (open_result != ESP_AUDIO_ERR_OK) {
        g_encoder = nullptr;
        return false;
    }
    ESP_LOGI(
        kTag, "%s encoder ready pcm_rate=%u frame_samples=%u",
        kCodecName,
        static_cast<unsigned>(kCaptureSampleRate),
        static_cast<unsigned>(kSamplesPerFrame));
    int input_size = 0;
    int output_size = 0;
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
    const auto frame_result = esp_opus_enc_get_frame_size(
        g_encoder, &input_size, &output_size);
#else
    const auto frame_result = esp_g711_enc_get_frame_size(
        g_encoder, &input_size, &output_size);
#endif
    if (frame_result != ESP_AUDIO_ERR_OK ||
        input_size <= 0 || output_size <= 0 ||
        kSamplesPerFrame * sizeof(std::int16_t) % input_size != 0 ||
        kSamplesPerFrame * sizeof(std::int16_t) / input_size * output_size >
            kMaximumEncodedBytes) {
        ESP_LOGE(
            kTag,
            "unexpected %s frame sizes in=%d out=%d",
            kCodecName,
            input_size,
            output_size);
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
        esp_opus_enc_close(g_encoder);
#else
        esp_g711_enc_close(g_encoder);
#endif
        g_encoder = nullptr;
        return false;
    }
    return true;
}

bool start_microphone() {
    if (g_microphone_ready && doodad::board::microphone_running()) return true;
    release_audio_dma_reserve();
    for (std::uint32_t attempt = 1; attempt <= 2; ++attempt) {
        ESP_LOGI(
            kTag,
            "microphone handoff attempt=%u dma_free=%u dma_largest=%u",
            static_cast<unsigned>(attempt),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_DMA)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
        g_microphone_ready = doodad::board::microphone_begin();
        if (g_microphone_ready) {
            ESP_LOGI(kTag, "microphone started for push-to-talk turn");
            return true;
        }
        // A failed ESP-IDF I2S mode setup can leave a newly-created channel
        // for the next begin() call to uninstall. Retry once after yielding so
        // deferred speaker-task frees are also visible to the DMA heap.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    acquire_audio_dma_reserve();
    ESP_LOGE(kTag, "microphone initialization failed");
    return false;
}

void start_capture(
    std::uint64_t request_id,
    std::uint64_t owner_token,
    std::uint32_t duration_ms) {
    const auto capture_id = next_capture_id();
    if (capture_id == 0) {
        ESP_LOGE(kTag, "capture correlation sequence exhausted");
        return;
    }
    if (request_id == 0 && owner_token != 0) {
        // Explicit remote diagnostic capture has no guest import call from
        // which to inherit a request ID. Reuse the firmware-issued capture
        // ID so the resulting provider event remains nonzero and bounded.
        request_id = capture_id;
    }
    g_active_request = request_id;
    g_active_owner_token = owner_token;
    g_active_capture_id = capture_id;
    if (!g_peer_connected) {
        publish(VoiceEventKind::error, "Voice link is not ready");
        send_simple("capture.failed");
        return;
    }
    // Block the peer callback before releasing speaker-owned buffers and
    // handing the shared CoreS3 codec back to the microphone.
    g_recording.store(true, std::memory_order_release);
    stop_and_reset_playback("capture started");
    close_decoder();
    acquire_audio_dma_reserve();
    // Reserve the I2S DMA descriptors before opening Opus. The software codec
    // can use ordinary heap/PSRAM, while the microphone ring can only come
    // from the small contiguous DMA-capable pool.
    if ((!g_microphone_ready || !doodad::board::microphone_running()) &&
        !start_microphone()) {
        g_recording.store(false, std::memory_order_release);
        g_playback_accepting.store(true, std::memory_order_release);
        publish(VoiceEventKind::error, "Microphone failed to start");
        send_simple("capture.failed");
        return;
    }
    if (!open_encoder()) {
        if (doodad::board::microphone_running()) {
            doodad::board::microphone_end();
        }
        g_microphone_ready = false;
        acquire_audio_dma_reserve();
        g_recording.store(false, std::memory_order_release);
        g_playback_accepting.store(true, std::memory_order_release);
        publish(VoiceEventKind::error, "Microphone codec failed to start");
        send_simple("capture.failed");
        return;
    }
    g_capture_duration_ms = std::clamp<std::uint32_t>(
        duration_ms, 1'000, 30'000);
    g_capture_started_ms = now_ms();
    g_encoded_frames = 0;
    g_dropped_frames = 0;
    g_encoded_bytes = 0;
    g_peak_pcm = 0;
    remember_capture(CaptureCorrelation{
        capture_id,
        request_id,
        owner_token,
    });
    publish(VoiceEventKind::recording, "Listening");
    send_capture_status("capture.started", 0);
}

void stop_capture(bool accept_downlink = true) {
    if (!g_recording.load(std::memory_order_acquire)) return;
    g_recording.store(false, std::memory_order_release);
    while (doodad::board::microphone_recording()) vTaskDelay(1);
    if (doodad::board::microphone_running()) {
        doodad::board::microphone_end();
    }
    g_microphone_ready = false;
    close_encoder();
    acquire_audio_dma_reserve();
    const auto elapsed = now_ms() - g_capture_started_ms;
    ESP_LOGI(kTag,
             "capture complete elapsed=%u frames=%u dropped=%u bytes=%u peak=%u",
             static_cast<unsigned>(elapsed),
             static_cast<unsigned>(g_encoded_frames),
             static_cast<unsigned>(g_dropped_frames),
             static_cast<unsigned>(g_encoded_bytes),
             static_cast<unsigned>(g_peak_pcm));
    publish(VoiceEventKind::stopped, "Processing", elapsed);
    // Reopen the matching generation before notifying the host, which may
    // begin its TTS response immediately on receipt of capture.stopped.
    g_playback_accepting.store(accept_downlink, std::memory_order_release);
    send_capture_status("capture.stopped", elapsed);
    display_publish_voice_level(0);
}

void capture_frame() {
    const bool first_frame = g_encoded_frames == 0 && g_dropped_frames == 0;
    if (!doodad::board::microphone_record(
            g_pcm, kSamplesPerFrame, kCaptureSampleRate)) {
        ++g_dropped_frames;
        vTaskDelay(1);
        return;
    }
    while (doodad::board::microphone_recording() &&
           g_recording.load(std::memory_order_acquire)) {
        vTaskDelay(1);
    }
    if (!g_recording.load(std::memory_order_acquire)) return;
    esp_audio_enc_in_frame_t input{
        reinterpret_cast<std::uint8_t*>(g_pcm),
        kSamplesPerFrame * sizeof(std::int16_t)};
    std::uint16_t pcm_peak = 0;
    for (std::size_t index = 0; index < kSamplesPerFrame; ++index) {
        const auto sample = g_pcm[index];
        const auto magnitude = static_cast<std::uint16_t>(
            sample == INT16_MIN ? INT16_MAX : std::abs(sample));
        pcm_peak = std::max(pcm_peak, magnitude);
    }
    g_peak_pcm = std::max(g_peak_pcm, pcm_peak);
    const auto level_now = now_ms();
    if (level_now - g_last_level_publish_ms >= 100) {
        g_last_level_publish_ms = level_now;
        display_publish_voice_level(static_cast<std::uint8_t>(
            std::min<std::uint32_t>(100, pcm_peak * 100U / 12'000U)));
    }
    esp_audio_enc_out_frame_t output{
        g_encoded_audio, kMaximumEncodedBytes, 0, 0};
#if CONFIG_DOODAD_VOICE_CODEC_OPUS
    const auto encode_result = esp_opus_enc_process(g_encoder, &input, &output);
#else
    const auto encode_result = esp_g711_enc_process(g_encoder, &input, &output);
#endif
    if (encode_result != ESP_AUDIO_ERR_OK ||
        output.encoded_bytes == 0) {
        ++g_dropped_frames;
        return;
    }
    // esp_peer's default implementation treats pts as a monotonic frame
    // sequence (as in its upstream peer_demo), then derives the negotiated RTP
    // clock. Wall-clock milliseconds cause its bounded sender to stall.
    esp_peer_audio_frame_t frame{
        g_encoded_frames,
        output.buffer,
        static_cast<int>(output.encoded_bytes)};
    const auto send_result = esp_peer_send_audio(g_peer, &frame);
    if (send_result == ESP_PEER_ERR_NONE) {
        ++g_encoded_frames;
        g_encoded_bytes += output.encoded_bytes;
    } else {
        ++g_dropped_frames;
    }
    if (first_frame || g_encoded_frames % 100 == 0) {
        ESP_LOGI(kTag, "audio frame=%u pcm_peak=%u encoded=%u send=%d",
                 static_cast<unsigned>(g_encoded_frames),
                 static_cast<unsigned>(pcm_peak),
                 static_cast<unsigned>(output.encoded_bytes),
                 send_result);
        // The default peer implementation reports and clears its compact RTP
        // counters here. This is intentionally bounded to one line per ~4 s;
        // it lets hardware runs distinguish an encoder/capture problem from a
        // packet leaving (or failing to leave) the SRTP transport.
        esp_peer_query(g_peer);
    }
}

void handle_command(Command& command) {
    switch (command.kind) {
        case CommandKind::welcome:
            send_simple("welcome.ack");
            send_watch_snapshot();
            // A fresh signaling session must not inherit ICE candidates or a
            // failed state from the previous Mac receiver instance.
            if (g_peer != nullptr) {
                close_peer();
            }
            if (create_peer()) {
                const auto result = esp_peer_new_connection(g_peer);
                if (result == ESP_PEER_ERR_NONE && start_peer_loop()) {
                    send_simple("peer.created");
                } else {
                    ESP_LOGE(kTag, "peer startup failed: %d", result);
                    close_peer();
                }
            }
            break;
        case CommandKind::local_description:
        case CommandKind::local_candidate:
            send_local_peer_message(command);
            break;
        case CommandKind::remote_description:
        case CommandKind::remote_candidate:
            if (g_peer != nullptr && command.data != nullptr) {
                esp_peer_msg_t message{
                    command.kind == CommandKind::remote_candidate
                        ? ESP_PEER_MSG_TYPE_CANDIDATE
                        : ESP_PEER_MSG_TYPE_SDP,
                    command.data,
                    static_cast<int>(command.size)};
                const auto result = esp_peer_send_msg(g_peer, &message);
                ESP_LOGI(
                    kTag,
                    "remote peer message type=%d size=%u result=%d",
                    static_cast<int>(message.type),
                    static_cast<unsigned>(command.size),
                    result);
            }
            break;
        case CommandKind::start:
            start_capture(
                command.request_id,
                command.owner_token,
                command.duration_ms);
            break;
        case CommandKind::stop:
            stop_capture();
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
            stop_capture(false);
            stop_and_reset_playback("cancelled");
            send_simple("listen.cancelled");
            break;
        case CommandKind::release_owner:
            if (command.owner_token != 0 &&
                command.owner_token == g_active_owner_token) {
                // Keep the retired token active after stopping. Any delayed
                // transcript or lifecycle event is then still attributable
                // to the outgoing guest and cannot become owner-neutral.
                stop_capture(false);
                stop_and_reset_playback("guest released");
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

void parse_message(const char* bytes, std::size_t size) {
    auto* root = cJSON_ParseWithLength(bytes, size);
    if (root == nullptr) return;
    const auto* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const auto* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const auto* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsNumber(version) || version->valueint != 1 ||
        !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }
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
        send_hello();
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED ||
               event_id == WEBSOCKET_EVENT_CLOSED) {
        g_websocket_connected = false;
        g_peer_connected = false;
        g_recording.store(false, std::memory_order_release);
        invalidate_playback_generation();
        g_playback_reset_requested.store(true, std::memory_order_release);
        display_publish_agent_state(0, 0, false, false, false, 0, "", "");
    } else if (event_id == WEBSOCKET_EVENT_DATA) {
        const auto* event = static_cast<esp_websocket_event_data_t*>(event_data);
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
    }
}

bool discover_url(char* destination, std::size_t capacity) {
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
}

void connect_websocket() {
    char url[160]{};
    if (!discover_url(url, sizeof(url))) return;
    ESP_LOGI(kTag, "signaling endpoint discovered: %s", url);
    esp_websocket_client_config_t configuration{};
    configuration.uri = url;
    configuration.buffer_size = 2'048;
    configuration.task_stack = 3 * 1024;
    configuration.task_prio = 5;
    configuration.network_timeout_ms = 5'000;
    configuration.reconnect_timeout_ms = 2'000;
    configuration.ping_interval_sec = 5;
    g_websocket = esp_websocket_client_init(&configuration);
    if (g_websocket == nullptr) return;
    esp_websocket_register_events(
        g_websocket, WEBSOCKET_EVENT_ANY, websocket_event, nullptr);
    if (esp_websocket_client_start(g_websocket) != ESP_OK) {
        esp_websocket_client_destroy(g_websocket);
        g_websocket = nullptr;
    }
}

void voice_task(void*) {
    publish(VoiceEventKind::connecting, "Connecting");
    std::uint32_t last_discovery = 0;
    while (true) {
        if (!network_service_connected()) {
            network_service_connect();
        }
        const auto now = now_ms();
        if (network_service_connected() && g_websocket == nullptr &&
            now - last_discovery >= 3'000) {
            last_discovery = now;
            connect_websocket();
        }
        Command command{};
        while (xQueueReceive(g_commands, &command, 0) == pdTRUE) {
            handle_command(command);
            free_command(command);
        }
        if (g_playback_reset_requested.load(std::memory_order_acquire)) {
            stop_and_reset_playback("transport closed");
        }
        play_queued_audio();
        finish_playback_if_idle();
        if (g_recording.load(std::memory_order_acquire)) {
            if (now_ms() - g_capture_started_ms >= g_capture_duration_ms) {
                stop_capture();
            } else {
                capture_frame();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

}  // namespace

bool voice_service_init() {
#if !CONFIG_DOODAD_VOICE_UPLINK
    return true;
#else
    if (g_task != nullptr) return true;
    // Configure a compact I2S ring now, but allocate and start it only for an
    // explicit push-to-talk turn. This keeps the physical microphone off while
    // the voice link is merely connected and preserves DMA memory for DTLS.
    doodad::board::audio_configure(doodad::board::AudioConfig{
        .sample_rate = kCaptureSampleRate,
        .microphone_dma_length = kMicrophoneDmaBufferLength,
        .microphone_dma_count = kMicrophoneDmaBufferCount,
        .speaker_dma_length = kSpeakerDmaBufferLength,
        .speaker_dma_count = kSpeakerDmaBufferCount,
        .task_priority = 6,
        .speaker_volume = kSpeakerVolume,
        .microphone_gain = 8,
    });
    if (!network_service_init()) return false;
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
    g_playback_frames = xQueueCreateWithCaps(
        kPlaybackQueueDepth,
        sizeof(PlaybackFrame),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_websocket_payload = static_cast<char*>(heap_caps_malloc(
        kMaximumSignalBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_encoded_audio = static_cast<std::uint8_t*>(heap_caps_malloc(
        kMaximumEncodedBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_decoded_audio = static_cast<std::int16_t*>(heap_caps_malloc(
        kMaximumDecodedBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_pcm = static_cast<std::int16_t*>(heap_caps_calloc(
        kSamplesPerFrame,
        sizeof(std::int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_playback_slots = static_cast<PlaybackFrame*>(heap_caps_calloc(
        kPlaybackSlotCount,
        sizeof(PlaybackFrame),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_task_stack = static_cast<StackType_t*>(heap_caps_calloc(
        kVoiceTaskStackBytes / sizeof(StackType_t),
        sizeof(StackType_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_commands == nullptr || g_events == nullptr ||
        g_playback_frames == nullptr ||
        g_websocket_payload == nullptr || g_encoded_audio == nullptr ||
        g_decoded_audio == nullptr || g_pcm == nullptr ||
        g_playback_slots == nullptr ||
        g_task_stack == nullptr) {
        ESP_LOGE(kTag, "voice service allocation failed");
        return false;
    }
    // Keep one contiguous internal block out of ordinary WebRTC/Opus
    // allocations. It is released only while M5Unified creates the I2S DMA
    // descriptors and audio worker stack, then reacquired at codec handoff.
    if (!acquire_audio_dma_reserve()) return false;
#if CONFIG_DOODAD_VOICE_AUTOCONNECT
    g_task = xTaskCreateStaticPinnedToCore(
        voice_task,
        "voice_uplink",
        kVoiceTaskStackBytes / sizeof(StackType_t),
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
    if (std::strcmp(operation, "system.voice.activate") == 0 ||
        std::strcmp(operation, "system.voice.interrupt") == 0) {
        return g_peer_connected &&
            enqueue(CommandKind::activate, nullptr, 0, 0, request_id);
    }
    if (std::strcmp(operation, "system.voice.finish") == 0) {
        return g_peer_connected &&
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
        return enqueue(CommandKind::stop, nullptr, 0, 0, request_id);
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
    return g_recording.load(std::memory_order_acquire);
}

bool voice_service_ready() {
    return g_peer_connected && g_websocket_connected;
}
