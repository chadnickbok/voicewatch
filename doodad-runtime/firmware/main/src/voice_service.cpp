#include "voice_service.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "M5Unified.h"
#include "cJSON.h"
#include "encoder/impl/esp_g711_enc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_peer.h"
#include "esp_peer_default.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"
#include "network_service.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "voice-service";
constexpr std::uint32_t kCaptureSampleRate = 8'000;
constexpr std::uint32_t kRtpClockRate = 8'000;
constexpr std::uint32_t kFrameDurationMs = 20;
constexpr std::size_t kSamplesPerFrame =
    kCaptureSampleRate * kFrameDurationMs / 1'000;
constexpr std::size_t kMaximumSignalBytes = 16 * 1024;
constexpr std::size_t kMaximumEncodedBytes = 1500;
constexpr int kSignalingFragmentBytes = 256;
constexpr std::size_t kSdpChunkBytes = 64;
constexpr std::size_t kSignalQueueDepth = 6;
constexpr std::size_t kCommandQueueDepth = 6;
constexpr std::size_t kEventQueueDepth = 6;
// Keep the WebRTC worker in PSRAM so ICE, DTLS, signaling, and audio callbacks
// have ample stack without consuming the small internal-RAM pool.
constexpr std::size_t kVoiceTaskStackBytes = 64 * 1024;

enum class CommandKind : std::uint8_t {
    welcome,
    local_description,
    local_candidate,
    remote_description,
    remote_candidate,
    prepare_microphone,
    start,
    stop,
    transcript,
};

struct Command {
    CommandKind kind;
    std::uint64_t request_id;
    std::uint32_t duration_ms;
    std::uint8_t* data;
    std::size_t size;
};

QueueHandle_t g_commands = nullptr;
QueueHandle_t g_events = nullptr;
TaskHandle_t g_task = nullptr;
StaticTask_t g_task_control{};
StackType_t* g_task_stack = nullptr;
esp_websocket_client_handle_t g_websocket = nullptr;
esp_peer_handle_t g_peer = nullptr;
void* g_encoder = nullptr;
bool g_websocket_connected = false;
bool g_peer_connected = false;
bool g_recording = false;
bool g_mdns_initialized = false;
bool g_microphone_ready = false;
std::uint64_t g_sequence = 0;
std::uint64_t g_active_request = 0;
std::uint32_t g_capture_duration_ms = 8'000;
std::uint32_t g_capture_started_ms = 0;
std::uint32_t g_encoded_frames = 0;
std::uint32_t g_dropped_frames = 0;
std::uint32_t g_encoded_bytes = 0;
std::uint16_t g_peak_pcm = 0;
std::int16_t g_pcm[kSamplesPerFrame];
std::uint8_t* g_encoded_audio = nullptr;
char* g_websocket_payload = nullptr;

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void free_command(Command& command) {
    if (command.data != nullptr) heap_caps_free(command.data);
    command.data = nullptr;
    command.size = 0;
}

void publish(
    VoiceEventKind kind,
    const char* text = nullptr,
    std::uint32_t elapsed_ms = 0) {
    if (g_events == nullptr) return;
    VoiceEvent event{};
    event.kind = kind;
    event.request_id = g_active_request;
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

bool enqueue(
    CommandKind kind,
    const char* data = nullptr,
    std::size_t size = 0,
    std::uint32_t duration_ms = 0,
    std::uint64_t request_id = 0) {
    if (g_commands == nullptr || size > kMaximumSignalBytes) return false;
    Command command{kind, request_id, duration_ms, nullptr, size};
    if (size != 0) {
        command.data = static_cast<std::uint8_t*>(heap_caps_malloc(
            size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (command.data == nullptr) return false;
        std::memcpy(command.data, data, size);
        command.data[size] = 0;
    }
    if (xQueueSend(g_commands, &command, 0) != pdTRUE) {
        free_command(command);
        return false;
    }
    return true;
}

void add_envelope(cJSON* root, const char* type) {
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "session_id", "watch-uplink");
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

void send_capture_status(const char* type, std::uint32_t elapsed_ms) {
    auto* root = cJSON_CreateObject();
    add_envelope(root, type);
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddNumberToObject(payload, "elapsed_ms", elapsed_ms);
    cJSON_AddNumberToObject(payload, "encoded_frames", g_encoded_frames);
    cJSON_AddNumberToObject(payload, "dropped_frames", g_dropped_frames);
    cJSON_AddNumberToObject(payload, "encoded_bytes", g_encoded_bytes);
    send_json(root);
}

void send_hello() {
    auto* root = cJSON_CreateObject();
    add_envelope(root, "hello");
    auto* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(payload, "device", "cores3");
    cJSON_AddStringToObject(payload, "transport", "webrtc");
    cJSON_AddStringToObject(payload, "audio", "PCMU/8000/1");
    cJSON_AddNumberToObject(payload, "frame_ms", kFrameDurationMs);
    send_json(root);
}

int peer_state(esp_peer_state_t state, void*) {
    ESP_LOGI(kTag, "peer state=%d", static_cast<int>(state));
    if (state == ESP_PEER_STATE_CONNECTED) {
        g_peer_connected = true;
        send_simple("peer.ready");
        publish(VoiceEventKind::ready, "Voice link ready");
        enqueue(CommandKind::prepare_microphone);
    } else if (state == ESP_PEER_STATE_DISCONNECTED ||
               state == ESP_PEER_STATE_CONNECT_FAILED ||
               state == ESP_PEER_STATE_CLOSED) {
        g_peer_connected = false;
        g_recording = false;
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
    defaults.rtp_cfg.send_pool_size = 4 * 1024;
    defaults.rtp_cfg.send_queue_num = 12;
    defaults.rtp_cfg.max_resend_count = 2;
    // This is a same-LAN uplink, so one UDP host candidate is sufficient and
    // avoids holding candidate-gathering resources for unused interfaces.
    defaults.max_candidates = 1;
    esp_peer_cfg_t configuration{};
    configuration.role = ESP_PEER_ROLE_CONTROLLING;
    configuration.audio_info.codec = ESP_PEER_AUDIO_CODEC_G711U;
    configuration.audio_info.sample_rate = kRtpClockRate;
    configuration.audio_info.channel = 1;
    configuration.audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY;
    configuration.video_dir = ESP_PEER_MEDIA_DIR_NONE;
    configuration.no_auto_reconnect = true;
    configuration.on_state = peer_state;
    configuration.on_msg = peer_message;
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

bool open_encoder() {
    if (g_encoder != nullptr) return true;
    esp_g711_enc_config_t configuration = ESP_G711_ENC_CONFIG_DEFAULT();
    configuration.sample_rate = kCaptureSampleRate;
    configuration.channel = 1;
    configuration.bits_per_sample = 16;
    configuration.frame_duration = kFrameDurationMs;
    if (esp_g711u_enc_open(
            &configuration, sizeof(configuration), &g_encoder) !=
        ESP_AUDIO_ERR_OK) {
        g_encoder = nullptr;
        return false;
    }
    ESP_LOGI(kTag, "PCMU encoder ready");
    int input_size = 0;
    int output_size = 0;
    if (esp_g711_enc_get_frame_size(
            g_encoder, &input_size, &output_size) != ESP_AUDIO_ERR_OK ||
        input_size <= 0 || output_size <= 0 ||
        sizeof(g_pcm) % input_size != 0 ||
        sizeof(g_pcm) / input_size * output_size > kMaximumEncodedBytes) {
        ESP_LOGE(
            kTag,
            "unexpected PCMU frame sizes in=%d out=%d",
            input_size,
            output_size);
        esp_g711_enc_close(g_encoder);
        g_encoder = nullptr;
        return false;
    }
    return true;
}

bool start_microphone() {
    if (g_microphone_ready && M5.Mic.isRunning()) return true;
    g_microphone_ready = M5.Mic.begin();
    if (!g_microphone_ready) {
        ESP_LOGE(kTag, "microphone initialization failed");
    } else {
        ESP_LOGI(kTag, "microphone reserved after SDP exchange");
    }
    return g_microphone_ready;
}

void start_capture(std::uint64_t request_id, std::uint32_t duration_ms) {
    g_active_request = request_id;
    if (!g_peer_connected || !open_encoder()) {
        publish(VoiceEventKind::error, "Voice link is not ready");
        return;
    }
    if (!g_microphone_ready || !M5.Mic.isRunning()) {
        publish(VoiceEventKind::error, "Microphone failed to start");
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
    g_recording = true;
    publish(VoiceEventKind::recording, "Listening");
    send_capture_status("capture.started", 0);
}

void stop_capture() {
    if (!g_recording) return;
    g_recording = false;
    while (M5.Mic.isRecording()) vTaskDelay(1);
    const auto elapsed = now_ms() - g_capture_started_ms;
    ESP_LOGI(kTag,
             "capture complete elapsed=%u frames=%u dropped=%u bytes=%u peak=%u",
             static_cast<unsigned>(elapsed),
             static_cast<unsigned>(g_encoded_frames),
             static_cast<unsigned>(g_dropped_frames),
             static_cast<unsigned>(g_encoded_bytes),
             static_cast<unsigned>(g_peak_pcm));
    publish(VoiceEventKind::stopped, "Processing", elapsed);
    send_capture_status("capture.stopped", elapsed);
}

void capture_frame() {
    const bool first_frame = g_encoded_frames == 0 && g_dropped_frames == 0;
    if (!M5.Mic.record(
            g_pcm, kSamplesPerFrame, kCaptureSampleRate, false)) {
        ++g_dropped_frames;
        vTaskDelay(1);
        return;
    }
    while (M5.Mic.isRecording() && g_recording) {
        vTaskDelay(1);
    }
    if (!g_recording) return;
    esp_audio_enc_in_frame_t input{
        reinterpret_cast<std::uint8_t*>(g_pcm), sizeof(g_pcm)};
    std::uint16_t pcm_peak = 0;
    for (const auto sample : g_pcm) {
        const auto magnitude = static_cast<std::uint16_t>(
            sample == INT16_MIN ? INT16_MAX : std::abs(sample));
        pcm_peak = std::max(pcm_peak, magnitude);
    }
    g_peak_pcm = std::max(g_peak_pcm, pcm_peak);
    esp_audio_enc_out_frame_t output{
        g_encoded_audio, kMaximumEncodedBytes, 0, 0};
    if (esp_g711_enc_process(g_encoder, &input, &output) != ESP_AUDIO_ERR_OK ||
        output.encoded_bytes == 0) {
        ++g_dropped_frames;
        return;
    }
    // esp_peer accepts presentation timestamps in milliseconds and converts
    // them to the negotiated RTP clock internally. Use capture-wall
    // time so a temporarily slow encoder does not create artificial jitter.
    esp_peer_audio_frame_t frame{
        now_ms() - g_capture_started_ms,
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
    }
}

void handle_command(Command& command) {
    switch (command.kind) {
        case CommandKind::welcome:
            send_simple("welcome.ack");
            // A fresh signaling session must not inherit ICE candidates or a
            // failed state from the previous Mac receiver instance.
            if (g_peer != nullptr) {
                esp_peer_close(g_peer);
                g_peer = nullptr;
                g_peer_connected = false;
            }
            if (create_peer()) {
                esp_peer_new_connection(g_peer);
                send_simple("peer.created");
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
        case CommandKind::prepare_microphone:
            if (!start_microphone()) {
                publish(VoiceEventKind::error, "Microphone failed to start");
                send_simple("capture.failed");
            }
            break;
        case CommandKind::start:
            start_capture(command.request_id, command.duration_ms);
            break;
        case CommandKind::stop:
            stop_capture();
            break;
        case CommandKind::transcript:
            g_active_request = command.request_id != 0
                ? command.request_id : g_active_request;
            publish(
                VoiceEventKind::transcript,
                reinterpret_cast<char*>(command.data));
            break;
    }
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
        enqueue(CommandKind::start, nullptr, 0,
                cJSON_IsNumber(duration) ? duration->valueint : 8'000, 0);
    } else if (std::strcmp(type->valuestring, "capture.stop") == 0) {
        enqueue(CommandKind::stop);
    } else if (std::strcmp(type->valuestring, "transcript.final") == 0) {
        const auto* transcript = cJSON_GetObjectItemCaseSensitive(payload, "text");
        if (cJSON_IsString(transcript)) {
            enqueue(CommandKind::transcript, transcript->valuestring,
                    std::strlen(transcript->valuestring), 0, 0);
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
        g_recording = false;
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
        // esp_peer_main_loop may wait for up to agent_recv_timeout. Keep that
        // behavior for reliable ICE/DTLS setup, but do not put its 100 ms poll
        // in the real-time microphone loop. Outbound RTP is sent directly by
        // esp_peer_send_audio; RTCP can be drained after this short capture.
        if (g_peer != nullptr && !g_recording) esp_peer_main_loop(g_peer);
        if (g_recording) {
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
    // Configure a compact I2S ring now, but start it only after our SDP offer
    // has reached the receiver. Starting it before signaling leaves too little
    // contiguous internal RAM for the WebSocket TCP path on ESP32-S3.
    auto microphone = M5.Mic.config();
    microphone.sample_rate = kCaptureSampleRate;
    // CoreS3's codec defaults to a conservative 2x software gain. A voice
    // source beside the watch needs more headroom for narrowband uplink while
    // remaining comfortably below clipping in the physical conformance test.
    microphone.magnification = 8;
    microphone.dma_buf_len = 128;
    microphone.dma_buf_count = 4;
    microphone.task_priority = 6;
    M5.Mic.config(microphone);
    if (!network_service_init()) return false;
    g_commands = xQueueCreate(kCommandQueueDepth, sizeof(Command));
    g_events = xQueueCreate(kEventQueueDepth, sizeof(VoiceEvent));
    g_websocket_payload = static_cast<char*>(heap_caps_malloc(
        kMaximumSignalBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_encoded_audio = static_cast<std::uint8_t*>(heap_caps_malloc(
        kMaximumEncodedBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_task_stack = static_cast<StackType_t*>(heap_caps_calloc(
        kVoiceTaskStackBytes / sizeof(StackType_t),
        sizeof(StackType_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_commands == nullptr || g_events == nullptr ||
        g_websocket_payload == nullptr || g_encoded_audio == nullptr ||
        g_task_stack == nullptr) {
        ESP_LOGE(kTag, "voice service allocation failed");
        return false;
    }
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
    std::uint32_t duration_ms) {
#if !CONFIG_DOODAD_VOICE_UPLINK
    return false;
#else
    if (operation == nullptr || g_commands == nullptr) return false;
    if (std::strcmp(operation, "voice-notes.record") == 0 ||
        std::strcmp(operation, "voice-notes.again") == 0) {
        return enqueue(CommandKind::start, nullptr, 0, duration_ms, request_id);
    }
    if (std::strcmp(operation, "voice-notes.finish-capture") == 0 ||
        std::strcmp(operation, "voice-notes.pause") == 0) {
        return enqueue(CommandKind::stop, nullptr, 0, 0, request_id);
    }
    return std::strncmp(operation, "voice-notes.", 12) == 0;
#endif
}

bool voice_service_poll(VoiceEvent& event) {
    return g_events != nullptr && xQueueReceive(g_events, &event, 0) == pdTRUE;
}

bool voice_service_busy() {
    return g_recording;
}
