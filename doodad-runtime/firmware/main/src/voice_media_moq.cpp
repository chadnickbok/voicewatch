#include "voice_media_transport.hpp"
#include "board_ultra.hpp"
#include "display.hpp"
#include "esp_moq/audio_esp.h"
#include "esp_moq/capture.h"
#include "esp_moq/endpoint.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

#ifdef DOODAD_MOQ_DIAGNOSTIC
void voice_moq_diagnostic_pcm(const std::int16_t*, std::size_t);
#endif

namespace doodad::voice_media {
namespace {
constexpr char kTag[] = "voice-moq";
constexpr unsigned kQueueDepth = 16;
constexpr unsigned kGain = CONFIG_DOODAD_SPEAKER_VOLUME;
constexpr auto kRam = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr int kMicrophoneGain = 8; // Preserve the existing VoiceWatch voice input profile.
constexpr std::uint64_t kMicrophoneWarmupSamples = 320; // T3902 wake-up: up to 20 ms.

struct CopiedSession {
    Session value{};
    char host[254]{}, roots[8193]{}, path[3073]{};
    char local[257]{}, remote[257]{};
};
enum class Operation { connect, start, finish, receive, receive_end, context };
struct Command {
    Operation operation{};
    std::uint64_t revision = 0;
    CopiedSession* config = nullptr;
    Identity identity{};
    std::uint32_t duration_ms = 0;
    Response response{};
};

// API callers only access the fields below under g_mutex. The audio task owns
// all codec, board lifecycle, and endpoint destruction operations. Lock order
// is API mutex -> service mutex -> board handoff spinlock; never the reverse.
SemaphoreHandle_t g_mutex = nullptr;
QueueHandle_t g_commands = nullptr, g_events = nullptr;
TaskHandle_t g_task = nullptr;
esp_moq_endpoint_t* g_endpoint = nullptr;
esp_moq_service_t* g_service = nullptr;
std::uint64_t g_publish = 0, g_receive = 0, g_revision = 1;
std::uint64_t g_last_session = 0;
Response g_latest_response{}, g_preserve_capture{};
std::uint64_t g_preserve_revision=0;
bool g_disconnect = false;
std::atomic<bool> g_ready{false}, g_recording{false}, g_overflow{false}, g_initialization_failed{false};
class Lock {
public:
    Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(g_mutex); }
};

struct Owner {
    twatch_ultra_t* board = nullptr; // Borrowed; the UI retains its shared I2C bus.
    esp_moq_endpoint_t* endpoint = nullptr;
    esp_moq_service_t* service = nullptr;
    esp_moq_audio_encoder_t encoder{};
    esp_moq_audio_decoder_t decoder{};
    esp_moq_audio_capture_t* capture = nullptr;
    esp_moq_audio_player_t* player = nullptr;
    void* capture_arena = nullptr;
    void* player_arena = nullptr;
    std::uint64_t revision = 1, session = 0, connection = 0, attempt = 0;
    std::uint64_t publish = 0, receive = 0, first = 0, started_us = 0;
    std::uint64_t response_started_us = 0, received_frames = 0;
    bool reported_pressure = false;
    std::uint64_t expected_sample = 0, microphone_generation = 0;
    std::uint64_t response_floor = 0, capture_floor = 0;
    std::uint64_t samples = 0, finish_deadline = 0, last_level_us = 0;
    std::uint64_t speaker_owner = 0, next_speaker_owner = 1;
    std::uint32_t duration_ms = 0, dropped_frames = 0, encoded_bytes = 0;
    Identity identity{};
    Response response{};
    bool capturing = false, encoding = false, finishing = false;
    bool playing = false, receiving_end = false, speaker = false, pending = false;
    bool closing = false, failed = false;
    bool start_pending = false;
    bool receive_pending = false;
    Response pending_response{};
    std::uint64_t receive_deadline=0;
    Command start_command{};
    std::uint64_t start_deadline = 0;
    esp_moq_audio_chunk_t chunk{};
    std::int16_t pcm[ESP_MOQ_AUDIO_SAMPLES]{};
    twatch_ultra_microphone_chunk_t microphone{};
    bool have_microphone = false;
};
Owner* g_owner = nullptr;

std::uint64_t now_us() { return static_cast<std::uint64_t>(esp_timer_get_time()); }
esp_moq_slice_t slice(const char* text) {
    return {reinterpret_cast<const std::uint8_t*>(text), std::strlen(text)};
}
esp_moq_voice_identity_t wire_identity(Identity id) {
    return {id.capture_id, id.request_id, id.owner_token};
}
void wipe_free(CopiedSession* config) {
    if (!config) return;
    volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(config);
    for (std::size_t i=0; i<sizeof(*config); ++i) p[i]=0;
    heap_caps_free(config);
}
void log_publisher_stats(Owner& o) {
    if (!o.service) return;
    esp_moq_service_stats_t snapshot{}; esp_moq_service_stats(o.service,&snapshot);
    const auto& s=snapshot.publisher;
    ESP_LOGI(kTag,"publisher cache_drop=%llu expired=%llu failed=%llu cancelled=%llu submitted=%llu retired=%llu last_first=%llu last_end=%llu last_code=%llu",
        static_cast<unsigned long long>(s.cache_drop_groups),static_cast<unsigned long long>(s.expired_groups),
        static_cast<unsigned long long>(s.failed_groups),static_cast<unsigned long long>(s.cancelled_groups),
        static_cast<unsigned long long>(s.submitted_groups),static_cast<unsigned long long>(s.retired_groups),
        static_cast<unsigned long long>(s.last_drop_first),static_cast<unsigned long long>(s.last_drop_end),
        static_cast<unsigned long long>(s.last_drop_code));
    ESP_LOGI(kTag,"control timeouts deadline=%llu transmit=%llu",
        static_cast<unsigned long long>(s.control_deadlines),static_cast<unsigned long long>(s.control_tx_expired));
    if(o.endpoint) {
        esp_moq_endpoint_status_t endpoint{}; esp_moq_endpoint_status(o.endpoint,&endpoint);
        ESP_LOGI(kTag,"transport pressure local=%llu credit=%llu blocks=%llu stopped=%u",
            static_cast<unsigned long long>(endpoint.open_local_blocked),
            static_cast<unsigned long long>(endpoint.open_credit_blocked),
            static_cast<unsigned long long>(endpoint.write_blocks_blocked),endpoint.blocked_stopped_high);
        ESP_LOGI(kTag,"owner timing at_ms=%llu gap_us=%llu service_us=%llu transport_us=%llu other_us=%llu wait_us=%llu rx_us=%llu tx_us=%llu dispatch_us=%llu",
            static_cast<unsigned long long>(endpoint.slowest_at_ms),static_cast<unsigned long long>(endpoint.slowest_gap_us),
            static_cast<unsigned long long>(endpoint.slowest_service_us),static_cast<unsigned long long>(endpoint.slowest_transport_us),
            static_cast<unsigned long long>(endpoint.slowest_other_us),static_cast<unsigned long long>(endpoint.slowest_wait_us),
            static_cast<unsigned long long>(endpoint.slowest_rx_us),static_cast<unsigned long long>(endpoint.slowest_tx_us),
            static_cast<unsigned long long>(endpoint.slowest_dispatch_us));
        ESP_LOGI(kTag,"QUIC heap live=%u peak=%u limit=%u blocks=%u allocations=%llu frees=%llu denied=%llu failures=%llu",
            static_cast<unsigned>(endpoint.quic_heap_live),static_cast<unsigned>(endpoint.quic_heap_peak),
            static_cast<unsigned>(endpoint.quic_heap_limit),static_cast<unsigned>(endpoint.quic_heap_blocks),
            static_cast<unsigned long long>(endpoint.quic_heap_allocations),static_cast<unsigned long long>(endpoint.quic_heap_frees),
            static_cast<unsigned long long>(endpoint.quic_heap_denied),static_cast<unsigned long long>(endpoint.quic_heap_failures));
    }
}
void emit(Owner& o, EventKind kind, int error=0, bool cancelled=false) {
    Event event{}; event.kind=kind; event.session=o.session;
    event.identity=o.identity; event.first_group=o.first;
    event.elapsed_ms=o.started_us ? (now_us()-o.started_us)/1000 : 0;
    event.dropped_frames=o.dropped_frames; event.encoded_bytes=o.encoded_bytes;
    event.error=error; event.cancelled=cancelled;
    if (o.capture) {
        esp_moq_audio_capture_stats_t stats{};
        esp_moq_audio_capture_stats(o.capture,&stats);
        event.encoded_frames=stats.encoded_packets;
    }
    if (kind==EventKind::capture_failed && o.service) {
        log_publisher_stats(o);
        esp_moq_service_stats_t stats{}; esp_moq_service_stats(o.service,&stats);
        esp_moq_endpoint_status_t endpoint{}; esp_moq_endpoint_status(o.endpoint,&endpoint);
        ESP_LOGI(kTag,"capture retired encoded=%llu accepted=%llu sent=%llu stale=%llu queued=%u high=%u mic_drops=%u poll_gap_ms=%llu",
            static_cast<unsigned long long>(event.encoded_frames),static_cast<unsigned long long>(stats.accepted_tx),
            static_cast<unsigned long long>(stats.sent_tx),static_cast<unsigned long long>(stats.stale_tx),
            stats.tx_queued,stats.tx_high_water,event.dropped_frames,
            static_cast<unsigned long long>(endpoint.running_max_poll_gap_ms));
    }
    if (kind==EventKind::playback_bound || kind==EventKind::playback_started || kind==EventKind::playback_finished) {
        event.identity=o.response.identity; event.response_id=o.response.response_id;
        event.samples=o.samples; event.first_group=o.response.first_group;
        event.end_group=o.response.end_group;
    }
    if (xQueueSend(g_events,&event,0)!=pdTRUE) g_overflow.store(true);
}
void invalidate_locked() {
    g_latest_response={};
    if (g_revision!=UINT64_MAX) ++g_revision;
    else { g_disconnect=true; g_ready.store(false); }
    if (g_service) {
        if (g_publish) esp_moq_service_cancel(g_service,g_publish);
        if (g_receive) esp_moq_service_cancel(g_service,g_receive);
    }
    if (g_owner && g_owner->board)
        twatch_ultra_speaker_invalidate(g_owner->board,0); // zero invalidates any owner
}
void fail(Owner& o, int result) {
    { Lock lock; if (o.revision!=g_revision) return; }
    if (o.failed) return;
    o.failed=true; g_ready.store(false);
    ESP_LOGE(kTag,"media failure result=%d",result); // Never a URI/token/PCM.
    esp_moq_endpoint_status_t endpoint{}; esp_moq_endpoint_status(o.endpoint,&endpoint);
    ESP_LOGE(kTag,"endpoint failure=%u result=%d transport=%llu alert=%u library=%d site=%u detail=%d stream=%llu protocol_site=%u protocol_stream=%llu",
        static_cast<unsigned>(endpoint.failure),static_cast<int>(endpoint.result),
        static_cast<unsigned long long>(endpoint.transport_error),static_cast<unsigned>(endpoint.tls_alert),
        endpoint.transport_library_error,endpoint.transport_site,endpoint.transport_detail,
        static_cast<unsigned long long>(endpoint.transport_stream),endpoint.protocol_site,
        static_cast<unsigned long long>(endpoint.protocol_stream));
    emit(o,EventKind::error,result);
    Lock lock;
    invalidate_locked(); g_disconnect=true;
    if (g_endpoint && o.attempt) esp_moq_endpoint_close(g_endpoint,o.attempt);
}

void cancel_audio(Owner& o) {
    // API cancellation has already invalidated service commits and the board
    // FIFO. Stop/clear on the one audio owner before processing any new command.
    const bool had_capture=o.publish!=0;
    const bool had_response=o.receive!=0;
    int result=twatch_ultra_microphone_stop(o.board);
    const int speaker_result=twatch_ultra_speaker_stop(o.board);
    if (!result) result=speaker_result;
    if (o.capture) esp_moq_audio_capture_cancel(o.capture);
    if (o.player) esp_moq_audio_player_cancel(o.player);
    o.capturing=o.encoding=o.finishing=o.have_microphone=o.start_pending=false;
    o.playing=o.speaker=o.pending=o.receiving_end=o.receive_pending=false;
    g_recording.store(false);
    if (had_capture) emit(o,EventKind::capture_failed,result,true);
    if (had_response) emit(o,EventKind::playback_finished,result,true);
    o.publish=o.receive=0; o.identity={}; o.response={};
    { Lock lock; g_publish=g_receive=0; }
    std::memset(o.pcm,0,sizeof(o.pcm));
    std::memset(&o.microphone,0,sizeof(o.microphone));
    display_publish_voice_level(0);
    if (result) fail(o,result);
}
void close_start(Owner& o) {
    if (!o.endpoint || o.closing) return;
    g_ready.store(false);
    esp_moq_endpoint_close(o.endpoint,o.attempt);
    { Lock lock; g_endpoint=nullptr; g_service=nullptr; g_publish=g_receive=0; }
    o.closing=true;
}
void close_step(Owner& o) {
    if (!o.closing) return;
    // Release every lease before destroy; even cancelled FRAME events own bytes.
    esp_moq_service_event_t e{};
    while (esp_moq_service_poll(o.service,&e)==ESP_MOQ_OK)
        if (e.type==ESP_MOQ_SERVICE_FRAME) esp_moq_service_release(o.service,e.lease);
    const auto result=esp_moq_endpoint_destroy(o.endpoint);
    if (result==ESP_MOQ_ERR_WOULD_BLOCK) return;
    if (result) { fail(o,result); return; } // Retain any non-destroyed handle.
    o.endpoint=nullptr; o.service=nullptr; o.closing=false; o.connection=o.attempt=0;
    emit(o,EventKind::disconnected);
}
void open_session(Owner& o, CopiedSession& copied) {
    auto& s=copied.value;
    o.session=s.generation; o.failed=false; o.identity={}; o.response={}; o.response_floor=0;
    esp_moq_endpoint_config_t config{};
    config.host=s.host; config.port=s.port; config.roots_pem=slice(s.roots_pem);
    config.local_broadcast=slice(s.local_broadcast); config.remote_broadcast=slice(s.remote_broadcast);
    config.sample_rate=16000; config.channels=1; config.bitrate=24000;
    config.has_opus_head=true; config.opus_pre_skip=ESP_MOQ_AUDIO_ESP_PRE_SKIP;
    config.origin=o.session; config.handshake_timeout_ms=15000;
    // app_main owns LVGL on CPU0 at priority 1. A runnable priority-5 TLS
    // worker there can starve an individual flush for hundreds of ms. CPU1
    // runs below the bounded priority-6 audio owner instead, so TLS work cannot
    // consume the microphone's 40 ms queue headroom while PCM is ready.
    config.pin_workers=true; config.worker_core=1;
    // All callers run with caches enabled. Retain scarce internal SRAM for
    // worker stacks, Wi-Fi and board DMA, not the task-only ~29 KiB arena.
    config.control_in_psram=true;
    auto result=esp_moq_endpoint_create(&config,&o.endpoint);
    if (!result) {
        o.service=esp_moq_endpoint_service(o.endpoint);
        esp_moq_endpoint_authorization_t auth{};
        auth.setup_path=slice(s.setup_path);
        auth.authorization_valid_until_ms=s.authorization_valid_until_ms;
        auth.trusted_time_valid_until_ms=s.trusted_time_valid_until_ms;
        result=esp_moq_endpoint_connect(o.endpoint,&auth,&o.attempt);
        Lock lock; g_endpoint=o.endpoint; g_service=o.service;
    }
    if (result) fail(o,result);
}
bool start_capture(Owner& o,const Command& command) {
    if (!g_ready.load() || o.publish || o.receive || command.identity.capture_id<=o.capture_floor) {
        const auto previous=o.identity; o.identity=command.identity;
        emit(o,EventKind::capture_failed,ESP_MOQ_ERR_INVALID_STATE); o.identity=previous; return true;
    }
    o.identity=command.identity;
    o.started_us=now_us(); o.duration_ms=std::clamp<std::uint32_t>(command.duration_ms,1000,30000);
    o.expected_sample=o.microphone_generation=0; o.dropped_frames=o.encoded_bytes=0;
    o.encoding=o.finishing=o.have_microphone=false;
    int result=ESP_MOQ_OK;
    {
        Lock lock;
        if (command.revision!=g_revision) return true;
        result=esp_moq_service_publish_begin(o.service,o.connection,wire_identity(o.identity),&o.publish,&o.first);
        if (!result) g_publish=o.publish;
    }
    if (result==ESP_MOQ_ERR_WOULD_BLOCK) return false;
    if (result) { emit(o,EventKind::capture_failed,result); return true; }
    o.capture_floor=command.identity.capture_id;
    result=twatch_ultra_microphone_start(o.board);
    if (result) { fail(o,result); return true; }
    o.capturing=true; g_recording.store(true);
    emit(o,EventKind::capture_started);
    return true;
}
void finish_capture(Owner& o) {
    if (!o.capturing) return;
    const auto result=twatch_ultra_microphone_stop(o.board);
    o.capturing=false; g_recording.store(false);
    if (result) { fail(o,result); return; }
    // A just-copied chunk belongs to this capture and must precede its tail.
    o.finishing=true; o.finish_deadline=now_us()+2000000;
}
bool begin_response(Owner& o,const Response& response) {
    if (!o.service || response.session!=o.session || !response.response_id ||
        response.response_id<=o.response_floor || o.receive || o.capturing || o.finishing ||
        response.identity.capture_id!=o.identity.capture_id ||
        response.identity.request_id!=o.identity.request_id ||
        response.identity.owner_token!=o.identity.owner_token) {
        // Reject stale or ambiguous ownership without interrupting its replacement.
        ESP_LOGW(kTag,"response binding rejected"); return true;
    }
    esp_moq_media_binding_t binding{}; binding.connection=o.connection;
    binding.voice=wire_identity(response.identity); binding.first_group=response.first_group;
    binding.end_group=response.end_group; binding.has_end=response.has_end;
    int result=ESP_MOQ_OK;
    {
        Lock lock;
        if (o.revision!=g_revision) return true;
        result=esp_moq_service_receive_begin(o.service,&binding,&o.receive);
        if (!result) g_receive=o.receive;
    }
    if (result==ESP_MOQ_ERR_WOULD_BLOCK) return false;
    if (result) { fail(o,result); return true; }
    o.response=response; o.response_floor=response.response_id; o.samples=0;
    o.response_started_us=now_us(); o.received_frames=0; o.reported_pressure=false;
    if (o.next_speaker_owner==UINT64_MAX) { fail(o,ESP_MOQ_ERR_VALUE_TOO_LARGE); return true; }
    o.speaker_owner=o.next_speaker_owner++;
    // The native producer primes an empty reset group. Acknowledge only after
    // SUBSCRIBE_START/MEDIA_READY, so paced PCM cannot accumulate while TRACK
    // and SUBSCRIBE control are still negotiating on the impaired media path.
    return true;
}

void poll_service(Owner& o) {
    for (unsigned i=0;i<16;++i) {
        esp_moq_service_event_t e{};
        auto result=esp_moq_service_poll(o.service,&e);
        if (result==ESP_MOQ_ERR_WOULD_BLOCK) break;
        if (result) { fail(o,result); return; }
        if (e.type==ESP_MOQ_SERVICE_CONNECTED) o.connection=e.connection;
        if (e.type==ESP_MOQ_SERVICE_MEDIA_READY && e.media==o.receive && !e.cancelled) {
            ESP_LOGI(kTag,"response ready id=%llu elapsed_us=%llu first=%llu pts=%llu",
                static_cast<unsigned long long>(o.response.response_id),
                static_cast<unsigned long long>(now_us()-o.response_started_us),
                static_cast<unsigned long long>(o.response.first_group),
                static_cast<unsigned long long>(o.response.pts_us));
            result=esp_moq_audio_player_begin(o.player,e.connection,e.media,&e.format);
            if (!result && o.response.has_timeline) {
                result=esp_moq_audio_player_timeline(o.player,o.response.pts_us);
                if (!result && o.response.has_end)
                    result=esp_moq_audio_player_end(o.player,o.response.samples,now_us());
            }
            if (!result) { o.playing=true; emit(o,EventKind::playback_bound); }
        }
        if ((e.type==ESP_MOQ_SERVICE_FRAME || e.type==ESP_MOQ_SERVICE_AUDIO_END ||
             e.type==ESP_MOQ_SERVICE_DISCONTINUITY || e.type==ESP_MOQ_SERVICE_RECEIVE_END) &&
            e.media==o.receive && o.receive && !e.cancelled && !e.result) {
            if (e.type!=ESP_MOQ_SERVICE_FRAME || esp_moq_service_lease_valid(o.service,e.lease))
                result=esp_moq_audio_player_push(o.player,&e,now_us());
            if (e.type==ESP_MOQ_SERVICE_FRAME) {
                ++o.received_frames;
                esp_moq_audio_stats_t audio{}; esp_moq_audio_player_stats(o.player,&audio);
                if (o.received_frames<=3 || (!o.reported_pressure && audio.pressure)) {
                    ESP_LOGI(kTag,"response frame id=%llu elapsed_us=%llu frames=%llu group=%llu pts=%llu samples=%llu queued=%u pressure=%llu",
                        static_cast<unsigned long long>(o.response.response_id),
                        static_cast<unsigned long long>(now_us()-o.response_started_us),
                        static_cast<unsigned long long>(o.received_frames),static_cast<unsigned long long>(e.group),
                        static_cast<unsigned long long>(e.timestamp_us),static_cast<unsigned long long>(o.samples),
                        audio.packets,static_cast<unsigned long long>(audio.pressure));
                    if (audio.pressure) o.reported_pressure=true;
                }
            }
            if (e.type==ESP_MOQ_SERVICE_RECEIVE_END) {
                o.receiving_end=true; o.response.end_group=e.end_group;
            }
        }
        if (e.type==ESP_MOQ_SERVICE_FRAME) {
            // A full live-player queue rejects an older packet with
            // WOULD_BLOCK and counts pressure. It is a bounded media drop,
            // not a transport failure; timestamps drive subsequent PLC.
            if (result==ESP_MOQ_ERR_WOULD_BLOCK) result=ESP_MOQ_OK;
            const auto release=esp_moq_service_release(o.service,e.lease);
            if (!result) result=release;
        }
        if (e.type==ESP_MOQ_SERVICE_PUBLISH_END && e.media==o.publish && o.publish) {
            Event event{}; event.session=o.session; event.identity=o.identity;
            event.kind=e.cancelled || e.result ? EventKind::capture_failed : EventKind::capture_stopped;
            event.cancelled=e.cancelled; event.error=e.result;
            event.first_group=e.group; event.end_group=e.end_group;
            event.elapsed_ms=(now_us()-o.started_us)/1000;
            event.dropped_frames=o.dropped_frames; event.encoded_bytes=o.encoded_bytes;
            esp_moq_audio_capture_stats_t stats{}; esp_moq_audio_capture_stats(o.capture,&stats);
            log_publisher_stats(o);
            ESP_LOGI(kTag,"capture samples=%llu discarded=%llu resets=%llu dropped_chunks=%llu",
                static_cast<unsigned long long>(stats.accepted_samples),
                static_cast<unsigned long long>(stats.discarded_buffered_samples),
                static_cast<unsigned long long>(stats.discontinuities),
                static_cast<unsigned long long>(o.dropped_frames));
            event.samples=stats.timeline_samples; event.encoded_frames=stats.encoded_packets;
            if (xQueueSend(g_events,&event,0)!=pdTRUE) g_overflow.store(true);
            o.publish=0; o.finishing=false;
            { Lock lock; g_publish=0; }
        }
        if (e.type==ESP_MOQ_SERVICE_RECEIVE_END && e.media==o.receive && (e.cancelled || e.result))
            result=e.result ? e.result : ESP_MOQ_ERR_INVALID_STATE;
        if (e.type==ESP_MOQ_SERVICE_ERROR && !e.cancelled) result=e.result;
        if (result && result!=ESP_MOQ_ERR_NOT_FOUND) {
            ESP_LOGE(kTag,"service event failure type=%u result=%d code=%llu",
                static_cast<unsigned>(e.type),result,static_cast<unsigned long long>(e.error_code));
            fail(o,result); return;
        }
    }
    esp_moq_service_stats_t stats{}; esp_moq_service_stats(o.service,&stats);
    const bool usable=stats.ready && stats.catalog_ready;
    bool became_ready=false;
    {
        Lock lock;
        if (o.revision==g_revision && !g_disconnect && !o.failed) {
            became_ready=usable && !g_ready.exchange(usable);
            if (!usable) g_ready.store(false);
        }
    }
    if (became_ready) { log_publisher_stats(o); emit(o,EventKind::ready); }
}

esp_moq_result_t publish_group(void* context,std::uint64_t connection,std::uint64_t media,
                               const esp_moq_audio_publication_t* group) {
    auto& o=*static_cast<Owner*>(context);
    const auto result=esp_moq_audio_service_publish(o.service,connection,media,group);
    if (!result) for (unsigned i=0;i<group->count;++i) o.encoded_bytes+=group->packets[i].packet.length;
    return result;
}
void capture_step(Owner& o) {
    if (!o.publish) return;
    // Enforce these even when backpressure keeps returning before a read.
    if ((o.finishing && now_us()>o.finish_deadline) ||
        (o.capturing && now_us()-o.started_us>static_cast<std::uint64_t>(o.duration_ms+1000)*1000)) {
        fail(o,ESP_MOQ_ERR_TRANSPORT); return;
    }
    esp_moq_audio_capture_stats_t stats{};
    if (o.encoding) {
        esp_moq_audio_capture_stats(o.capture,&stats);
        if (stats.pending) {
            const auto result=esp_moq_audio_capture_pump(o.capture,publish_group,&o);
            if (result==ESP_MOQ_ERR_WOULD_BLOCK) return;
            if (result) { fail(o,result); return; }
            esp_moq_audio_capture_stats(o.capture,&stats);
            if (stats.pending) return;
        }
    }
    if (o.capturing && !o.have_microphone) {
        const auto result=twatch_ultra_microphone_read(o.board,&o.microphone);
        if (result==ESP_OK) {
            o.have_microphone=true;
            // Apply once when acquiring a new chunk, never on a backpressure
            // retry of held PCM. The board's raw capture API does not apply the
            // gain used by the previous WebRTC board microphone_record path.
            for (auto& sample:o.microphone.pcm)
                sample=o.microphone.sample_index<kMicrophoneWarmupSamples ? 0 :
                    static_cast<std::int16_t>(std::clamp(static_cast<int>(sample)*kMicrophoneGain,-32768,32767));
        }
        else if (result!=ESP_ERR_NOT_FOUND) { fail(o,result); return; }
    }
    if (o.have_microphone) {
        auto& chunk=o.microphone;
        esp_moq_result_t result=ESP_MOQ_OK;
        if (!o.encoding) {
            result=esp_moq_audio_capture_begin(o.capture,o.connection,o.publish,chunk.timestamp_us);
            o.encoding=!result; o.microphone_generation=chunk.generation;
        } else if (chunk.generation!=o.microphone_generation) {
            fail(o,ESP_MOQ_ERR_INVALID_STATE); return;
        } else if (chunk.sample_index!=o.expected_sample) {
            if (chunk.sample_index<o.expected_sample) { fail(o,ESP_MOQ_ERR_INVALID_STATE); return; }
            o.dropped_frames+=(chunk.sample_index-o.expected_sample+159)/160;
            result=esp_moq_audio_capture_discontinuity(o.capture,chunk.timestamp_us);
            if (!result) o.expected_sample=chunk.sample_index;
        }
        if (!result) {
            result=esp_moq_audio_capture_pump(o.capture,publish_group,&o);
            // A reset may be blocked; retry that same held PCM after pumping it.
            if (result==ESP_MOQ_ERR_WOULD_BLOCK) return;
        }
        if (!result) result=esp_moq_audio_capture_write(o.capture,chunk.pcm,160,chunk.timestamp_us);
        if (result) { fail(o,result); return; }
        o.expected_sample=chunk.sample_index+160; o.have_microphone=false;
        if (now_us()-o.last_level_us>=100000) {
            unsigned peak=0;
            for (auto sample:chunk.pcm) peak=std::max(peak,static_cast<unsigned>(sample<0?-static_cast<int>(sample):sample));
            display_publish_voice_level(std::min(100U,peak*100/12000)); o.last_level_us=now_us();
        }
        std::memset(&chunk,0,sizeof(chunk));
    }
    if (o.finishing) {
        if (!o.encoding) { fail(o,ESP_MOQ_ERR_INVALID_STATE); return; }
        esp_moq_audio_capture_stats(o.capture,&stats);
        if (!stats.pending && !stats.finishing && !stats.finished) {
            const auto result=esp_moq_audio_capture_finish(o.capture);
            if (result) fail(o,result);
        }
        if (now_us()>o.finish_deadline) fail(o,ESP_MOQ_ERR_TRANSPORT);
    }
    if (o.capturing && o.expected_sample>=static_cast<std::uint64_t>(o.duration_ms)*16)
        finish_capture(o);
    if (o.capturing && now_us()-o.started_us>static_cast<std::uint64_t>(o.duration_ms+1000)*1000)
        fail(o,ESP_MOQ_ERR_TRANSPORT);
}
bool commit(void* context) {
    auto& o=*static_cast<Owner*>(context);
    return twatch_ultra_speaker_submit(o.board,o.speaker_owner,o.pcm,o.chunk.samples);
}
void playback_step(Owner& o) {
    if (!o.playing || !o.receive) return;
    if (!o.pending) {
        const auto result=esp_moq_audio_player_render(o.player,now_us(),&o.chunk);
        if (!result) {
            std::memcpy(o.pcm,o.chunk.pcm,o.chunk.samples*sizeof(std::int16_t));
            o.chunk.pcm=o.pcm; o.pending=true;
        } else if (result!=ESP_MOQ_ERR_NOT_FOUND && result!=ESP_MOQ_ERR_WOULD_BLOCK) { fail(o,result); return; }
    }
    if (o.pending) {
        if (!o.speaker) {
            const auto result=twatch_ultra_speaker_start(o.board,o.speaker_owner,kGain);
            if (result) { fail(o,result); return; }
            o.speaker=true;
        }
        if (esp_moq_service_media_commit(o.service,o.connection,o.receive,commit,&o)) {
            if (!o.samples) emit(o,EventKind::playback_started);
#ifdef DOODAD_MOQ_DIAGNOSTIC
            voice_moq_diagnostic_pcm(o.pcm,o.chunk.samples);
#endif
            o.samples+=o.chunk.samples; o.pending=false;
        }
    }
    esp_moq_audio_stats_t stats{}; esp_moq_audio_player_stats(o.player,&stats);
    twatch_ultra_audio_stats_t io{}; twatch_ultra_audio_stats(o.board,&io);
    if (io.callback_fault) { fail(o,ESP_MOQ_ERR_INVALID_STATE); return; }
    if (stats.drained && !o.pending && o.receiving_end && (!o.speaker || io.speaker_drained)) {
        const auto result=twatch_ultra_speaker_stop(o.board);
        if (result) { fail(o,result); return; }
        // This completion follows real DMA drainage, not receipt of FIN or an
        // empty server queue. Old replies retain the exact response identity.
        emit(o,EventKind::playback_finished);
        esp_moq_endpoint_status_t endpoint{}; esp_moq_endpoint_status(o.endpoint,&endpoint);
        ESP_LOGI(kTag,"playout samples=%llu concealed=%llu late=%llu pressure=%llu silence=%llu audio_stack=%u network_stack=%u dns_stack=%u",
            static_cast<unsigned long long>(o.samples),static_cast<unsigned long long>(stats.concealed),
            static_cast<unsigned long long>(stats.late),static_cast<unsigned long long>(stats.pressure),
            static_cast<unsigned long long>(stats.silence),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),static_cast<unsigned>(endpoint.network_stack_free),
            static_cast<unsigned>(endpoint.resolver_stack_free));
        o.receive=0; o.playing=o.speaker=o.receiving_end=false;
        { Lock lock; g_receive=0; }
    }
}

void audio_task(void*) {
    auto& o=*g_owner;
    if (esp_moq_audio_esp_encoder_open(&o.encoder) || esp_moq_audio_esp_decoder_open(&o.decoder) ||
        esp_moq_audio_capture_init(o.capture_arena,esp_moq_audio_capture_size(),o.encoder,&o.capture) ||
        esp_moq_audio_player_init(o.player_arena,esp_moq_audio_player_size(),o.decoder,&o.player)) {
        g_initialization_failed.store(true);
        fail(o,ESP_MOQ_ERR_INVALID_STATE);
        // Retain the permanent error owner, but never accept/retain grants.
        esp_moq_audio_esp_encoder_close(&o.encoder);
        esp_moq_audio_esp_decoder_close(&o.decoder);
        while (true) {
            Command command{};
            while (xQueueReceive(g_commands,&command,0)==pdTRUE) wipe_free(command.config);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    while (true) {
        std::uint64_t revision; bool disconnecting; Response preserve{};
        { Lock lock; revision=g_revision; disconnecting=g_disconnect;
          if (g_preserve_revision==revision) preserve=g_preserve_capture; }
        if (o.revision!=revision) {
            const bool retain=!disconnecting && !o.capturing && !o.finishing && !o.start_pending &&
                preserve.session==o.session && preserve.identity.capture_id &&
                preserve.identity.capture_id==o.identity.capture_id &&
                preserve.identity.request_id==o.identity.request_id && preserve.identity.owner_token==o.identity.owner_token;
            cancel_audio(o);
            if (retain) o.identity=preserve.identity;
            o.revision=revision;
        }
        if (disconnecting) close_start(o);
        close_step(o);
        // Poll old terminal events before reusing the service's one operation
        // slot. Abandonment may still need a network tick, so a queued start
        // has one bounded retry slot rather than losing an authorized PTT.
        if (o.endpoint && !o.closing && !o.failed) poll_service(o);
        if (!o.closing) {
            Command command{};
            // Bounded work per audio tick; stale queued starts cannot restart a
            // microphone after cancel/disconnect, even when the queue was full.
            for (unsigned i=0;i<4;++i) {
                {
                    Lock lock;
                    // A producer can invalidate the owner after the cleanup
                    // above and queue a replacement start. Leave that command
                    // queued until this owner has applied the new revision;
                    // otherwise next tick's cancel_audio erases start_pending.
                    // The check and dequeue share the producers' mutex.
                    if (o.revision!=g_revision || xQueueReceive(g_commands,&command,0)!=pdTRUE) break;
                    revision=g_revision;
                }
                if (command.revision!=revision) { wipe_free(command.config); continue; }
                switch (command.operation) {
                case Operation::connect:
                    if (!o.endpoint) {
                        { Lock lock; g_disconnect=false; }
                        open_session(o,*command.config);
                    }
                    wipe_free(command.config); break;
                case Operation::start:
                    o.start_command=command; o.start_pending=true; o.start_deadline=now_us()+500000;
                    break;
                case Operation::finish:
                    if (o.start_pending) { o.start_pending=false; o.identity=o.start_command.identity; emit(o,EventKind::capture_failed,0,true); o.identity={}; }
                    finish_capture(o); break;
                case Operation::context:
                    if (command.identity.capture_id<=o.capture_floor || o.capturing || o.finishing || o.receive || o.publish) {
                        fail(o,ESP_MOQ_ERR_INVALID_STATE); break;
                    }
                    o.identity=command.identity;
                    o.capture_floor=command.identity.capture_id;
                    emit(o,EventKind::response_context_ready);
                    break;
                case Operation::receive:
                    o.pending_response=command.response; o.receive_pending=true; o.receive_deadline=now_us()+500000;
                    break;
                case Operation::receive_end:
                    if (o.receive && command.response.session==o.session &&
                        command.response.response_id==o.response.response_id) {
                        o.response.end_group=command.response.end_group;
                        o.response.samples=command.response.samples; o.response.has_end=true;
                        auto result=esp_moq_service_receive_end(o.service,o.receive,command.response.end_group);
                        if (!result && o.playing && o.response.has_timeline)
                            result=esp_moq_audio_player_end(o.player,command.response.samples,now_us());
                        if (result) fail(o,result);
                    }
                    break;
                }
            }
        }
        { Lock lock; revision=g_revision; }
        if (o.revision!=revision) { vTaskDelay(1); continue; }
        if (o.start_pending) {
            if (now_us()>o.start_deadline) { o.start_pending=false; emit(o,EventKind::capture_failed,ESP_MOQ_ERR_WOULD_BLOCK); }
            else if (start_capture(o,o.start_command)) o.start_pending=false;
        }
        if (o.receive_pending) {
            if (now_us()>o.receive_deadline) { o.receive_pending=false; fail(o,ESP_MOQ_ERR_WOULD_BLOCK); }
            else if (begin_response(o,o.pending_response)) o.receive_pending=false;
        }
        if (o.endpoint && !o.closing && !o.failed) {
            esp_moq_endpoint_status_t status{}; esp_moq_endpoint_status(o.endpoint,&status);
            if (status.state==ESP_MOQ_ENDPOINT_CLOSED) fail(o,status.result ? status.result : ESP_MOQ_ERR_TRANSPORT);
            else {
                poll_service(o);
                // Each DMA chunk is 10 ms, also one RTOS tick. Reading only one
                // per tick cannot catch up after Opus/network work misses a tick.
                // Drain at most the four board slots before yielding; command
                // handling and playback still get a turn in every owner cycle.
                for (unsigned i=0;i<4 && !o.failed && o.publish;++i) capture_step(o);
                if (!o.failed) playback_step(o);
            }
        }
        if (g_overflow.exchange(false)) fail(o,ESP_MOQ_ERR_TOO_MANY);
        vTaskDelay(1);
    }
}
bool enqueue(Command command) {
    if (!g_commands) return false;
    Lock lock;
    if (g_revision==UINT64_MAX) return false;
    command.revision=g_revision;
    return xQueueSend(g_commands,&command,0)==pdTRUE;
}
template<std::size_t N> bool copy(char (&to)[N],const char* from) {
    if (!from || !from[0]) return false;
    const auto size=strnlen(from,N);
    if (size==N) return false;
    std::memcpy(to,from,size+1); return true;
}
} // namespace

const char* name() { return "moq"; }
const char* codec_description() { return "HANG/OPUS/48000/1;PCM/16000/1"; }
bool init(SignalSink) {
    if (g_task) return true;
    auto* memory=heap_caps_calloc(1,sizeof(Owner),kRam);
    if (!memory) return false;
    g_owner=new(memory) Owner{};
    auto& o=*g_owner; o.board=doodad::board::ultra_audio_board();
    const auto cleanup = [&]() {
        heap_caps_free(o.capture_arena); heap_caps_free(o.player_arena);
        if (g_commands) vQueueDeleteWithCaps(g_commands);
        if (g_events) vQueueDeleteWithCaps(g_events);
        if (g_mutex) vSemaphoreDelete(g_mutex);
        g_commands=g_events=nullptr; g_mutex=nullptr;
        o.~Owner(); heap_caps_free(g_owner); g_owner=nullptr;
        return false;
    };
    if (!o.board) return cleanup();
    g_mutex=xSemaphoreCreateMutex();
    g_commands=xQueueCreateWithCaps(kQueueDepth,sizeof(Command),kRam);
    g_events=xQueueCreateWithCaps(kQueueDepth,sizeof(Event),kRam);
    o.capture_arena=heap_caps_aligned_alloc(16,esp_moq_audio_capture_size(),kRam);
    o.player_arena=heap_caps_aligned_alloc(16,esp_moq_audio_player_size(),kRam);
    if (!g_mutex || !g_commands || !g_events || !o.capture_arena || !o.player_arena) return cleanup();
    if (xTaskCreatePinnedToCoreWithCaps(audio_task,"voice_moq_audio",65536,nullptr,6,&g_task,1,kRam)!=pdPASS) return cleanup();
    return true;
}
bool connect(const Session& session) {
    if (!g_task || g_initialization_failed.load() || !session.generation || !session.port ||
        session.authorization_valid_until_ms<=now_us()/1000 || session.trusted_time_valid_until_ms<=now_us()/1000) return false;
    auto* memory=heap_caps_malloc(sizeof(CopiedSession),kRam);
    if (!memory) return false;
    auto* owned=new(memory) CopiedSession{};
    owned->value=session;
    if (!copy(owned->host,session.host) || !copy(owned->roots,session.roots_pem) ||
        !copy(owned->path,session.setup_path) || !copy(owned->local,session.local_broadcast) ||
        !copy(owned->remote,session.remote_broadcast)) { wipe_free(owned); return false; }
    owned->value.host=owned->host; owned->value.roots_pem=owned->roots; owned->value.setup_path=owned->path;
    owned->value.local_broadcast=owned->local; owned->value.remote_broadcast=owned->remote;
    Lock lock;
    if (g_revision>=UINT64_MAX-1 || session.generation<=g_last_session || uxQueueSpacesAvailable(g_commands)==0) { wipe_free(owned); return false; }
    invalidate_locked(); g_ready.store(false); g_disconnect=true;
    Command command{}; command.operation=Operation::connect; command.revision=g_revision; command.config=owned;
    if (xQueueSend(g_commands,&command,0)!=pdTRUE) { wipe_free(owned); return false; }
    g_last_session=session.generation; return true;
}
void disconnect() {
    if (!g_mutex) return;
    Lock lock; invalidate_locked(); g_ready.store(false); g_disconnect=true;
}
bool renew(std::uint64_t session,std::uint64_t authorization_until,std::uint64_t trusted_until) {
    if (!g_mutex) return false;
    Lock lock;
    if (!g_ready.load() || g_disconnect || !g_endpoint || session!=g_last_session) return false;
    esp_moq_endpoint_status_t status{};
    esp_moq_endpoint_status(g_endpoint,&status);
    // close_start removes this pointer under the same mutex before destruction.
    return esp_moq_endpoint_renew(g_endpoint,status.attempt,authorization_until,trusted_until)==ESP_MOQ_OK;
}
bool signal(Signal,const char*,std::size_t) { return false; }
bool capture_begin(Identity identity,std::uint32_t duration_ms) {
    if (!g_ready.load() || !identity.capture_id) return false;
    cancel();
    Command command{}; command.operation=Operation::start; command.identity=identity; command.duration_ms=duration_ms;
    return enqueue(command);
}
bool capture_finish() {
    Command command{}; command.operation=Operation::finish;
    return enqueue(command);
}
bool response_context_begin(Identity identity) {
    if (!g_ready.load() || !identity.capture_id) return false;
    cancel();
    Command command{}; command.operation=Operation::context; command.identity=identity;
    return enqueue(command);
}
void cancel() { if (g_mutex) { Lock lock; invalidate_locked(); } }
bool receive_begin(const Response& response) {
    if (!g_ready.load() || !response.response_id || !response.identity.capture_id ||
        (response.has_end && response.end_group<response.first_group)) return false;
    Command command{}; command.operation=Operation::receive; command.response=response;
    Lock lock;
    if (g_revision==UINT64_MAX || response.session!=g_last_session) return false;
    command.revision=g_revision;
    if (xQueueSend(g_commands,&command,0)!=pdTRUE) return false;
    g_latest_response=response; return true;
}
bool receive_cancel(std::uint64_t session,std::uint64_t response_id) {
    if (!g_mutex) return false;
    Lock lock;
    if (!response_id || session!=g_latest_response.session || response_id!=g_latest_response.response_id) return false;
    const auto preserve=g_latest_response;
    invalidate_locked(); // Immediate service/DMA fence also invalidates queued old bindings.
    g_preserve_capture=preserve; g_preserve_revision=g_revision;
    return true;
}
bool receive_end(std::uint64_t session,std::uint64_t response_id,std::uint64_t end_group,std::uint64_t samples) {
    Command command{}; command.operation=Operation::receive_end;
    command.response.session=session; command.response.response_id=response_id; command.response.end_group=end_group;
    command.response.samples=samples;
    return enqueue(command);
}
void tick() {}
bool poll(Event& event) { return g_events && xQueueReceive(g_events,&event,0)==pdTRUE; }
bool ready() { return g_ready.load(); }
bool recording() { return g_recording.load(); }
} // namespace doodad::voice_media
