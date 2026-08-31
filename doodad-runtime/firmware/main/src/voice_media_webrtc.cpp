#include "voice_media_transport.hpp"
#include "board.hpp"
#include "display.hpp"
#include "sdkconfig.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "decoder/impl/esp_g711_dec.h"
#include "decoder/impl/esp_opus_dec.h"
#include "encoder/impl/esp_g711_enc.h"
#include "encoder/impl/esp_opus_enc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_peer.h"
#include "esp_peer_default.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace doodad::voice_media {
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

QueueHandle_t g_playback_frames = nullptr;
TaskHandle_t g_peer_task = nullptr;
volatile bool g_peer_loop_running = false;
esp_peer_handle_t g_peer = nullptr;
void* g_encoder = nullptr;
void* g_decoder = nullptr;
void* g_audio_dma_reserve = nullptr;
std::atomic<bool> g_peer_connected{false};
std::atomic<bool> g_recording{false};
std::atomic<bool> g_playing{false};
bool g_microphone_ready = false;
std::uint64_t g_active_request = 0;
std::uint64_t g_active_owner_token = 0;
std::uint64_t g_active_capture_id = 0;
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
QueueHandle_t g_events = nullptr;
SignalSink g_signaling = nullptr;
std::uint64_t g_session = 0;
std::atomic<bool> g_event_overflow{false};
std::uint32_t now_ms() { return static_cast<std::uint32_t>(esp_timer_get_time()/1000); }
void emit(EventKind kind, std::uint32_t elapsed_ms = 0) {
    Event e{}; e.kind=kind; e.session=g_session;
    e.identity={g_active_capture_id,g_active_request,g_active_owner_token};
    e.elapsed_ms=elapsed_ms; e.encoded_frames=g_encoded_frames;
    e.dropped_frames=g_dropped_frames; e.encoded_bytes=g_encoded_bytes;
    if (xQueueSend(g_events,&e,0)!=pdTRUE) g_event_overflow.store(true);
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

int peer_state(esp_peer_state_t state, void*) {
    ESP_LOGI(kTag, "peer state=%d", static_cast<int>(state));
    if (state == ESP_PEER_STATE_CONNECTED) {
        g_peer_connected = true;
        g_playback_accepting.store(true, std::memory_order_release);
        emit(EventKind::ready);
    } else if (state == ESP_PEER_STATE_DISCONNECTED ||
               state == ESP_PEER_STATE_CONNECT_FAILED ||
               state == ESP_PEER_STATE_CLOSED) {
        g_peer_connected = false;
        g_recording.store(false, std::memory_order_release);
        invalidate_playback_generation();
        g_playback_reset_requested.store(true, std::memory_order_release);
        emit(EventKind::disconnected);
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
    return g_signaling && g_signaling(
        message->type == ESP_PEER_MSG_TYPE_CANDIDATE ? Signal::candidate : Signal::description,
        reinterpret_cast<const char*>(message->data), static_cast<std::size_t>(message->size)) ? 0 : -1;
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
                vTaskDeleteWithCaps(nullptr);
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

void start_capture(Identity identity, std::uint32_t duration_ms) {
    g_active_request = identity.request_id;
    g_active_owner_token = identity.owner_token;
    g_active_capture_id = identity.capture_id;
    if (!g_peer_connected) {
        emit(EventKind::capture_failed);
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
        emit(EventKind::capture_failed);
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
        emit(EventKind::capture_failed);
        return;
    }
    g_capture_duration_ms = std::clamp<std::uint32_t>(
        duration_ms, 1'000, 30'000);
    g_capture_started_ms = now_ms();
    g_encoded_frames = 0;
    g_dropped_frames = 0;
    g_encoded_bytes = 0;
    g_peak_pcm = 0;
    emit(EventKind::capture_started);

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

    // Reopen the matching generation before notifying the host, which may
    // begin its TTS response immediately on receipt of capture.stopped.
    g_playback_accepting.store(accept_downlink, std::memory_order_release);
    emit(EventKind::capture_stopped, elapsed);
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

}  // namespace

const char* name() { return "webrtc"; }
const char* codec_description() { return kCodecDescription; }
bool init(SignalSink signaling) {
    if (g_events) return true;
    g_signaling=signaling;
    g_events=xQueueCreateWithCaps(16,sizeof(Event),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
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
    g_playback_frames = xQueueCreateWithCaps(
        kPlaybackQueueDepth,
        sizeof(PlaybackFrame),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
    return g_events && g_playback_frames && g_encoded_audio && g_decoded_audio &&
        g_pcm && g_playback_slots && acquire_audio_dma_reserve();
}
bool connect(const Session& session) {
    if (!session.generation || session.generation<=g_session) return false;
    disconnect(); g_session=session.generation;
    if (!create_peer()) return false;
    if (esp_peer_new_connection(g_peer)!=ESP_PEER_ERR_NONE || !start_peer_loop()) {
        close_peer(); return false;
    }
    return true;
}
void disconnect() { stop_capture(false); close_peer(); }
bool signal(Signal kind,const char* bytes,std::size_t size) {
    if (!g_peer || !bytes || !size || size>kMaximumSignalBytes) return false;
    esp_peer_msg_t message{kind==Signal::candidate?ESP_PEER_MSG_TYPE_CANDIDATE:ESP_PEER_MSG_TYPE_SDP,
        reinterpret_cast<std::uint8_t*>(const_cast<char*>(bytes)),static_cast<int>(size)};
    return esp_peer_send_msg(g_peer,&message)==ESP_PEER_ERR_NONE;
}
bool capture_begin(Identity identity,std::uint32_t duration_ms) {
    if (!identity.capture_id || !g_peer_connected) return false;
    stop_capture(false); start_capture(identity,duration_ms); return true;
}
bool capture_finish() { stop_capture(); return true; }
void cancel() { stop_capture(false); stop_and_reset_playback("cancelled"); }
bool receive_begin(const Response&) { return false; }
bool receive_end(std::uint64_t,std::uint64_t,std::uint64_t) { return false; }
void tick() {
    if (g_event_overflow.exchange(false)) { disconnect(); emit(EventKind::error); }
    if (g_playback_reset_requested.load(std::memory_order_acquire)) {
        if (doodad::board::microphone_running()) doodad::board::microphone_end();
        g_microphone_ready=false;
        stop_and_reset_playback("transport closed");
    }
    play_queued_audio(); finish_playback_if_idle();
    if (g_recording.load(std::memory_order_acquire)) {
        if (now_ms()-g_capture_started_ms>=g_capture_duration_ms) stop_capture();
        else capture_frame();
    }
}
bool poll(Event& event) { return g_events && xQueueReceive(g_events,&event,0)==pdTRUE; }
bool ready() { return g_peer_connected.load(); }
bool recording() { return g_recording.load(); }
}  // namespace doodad::voice_media
