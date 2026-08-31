// Explicitly selected private hardware diagnostic. Not linked in normal images.
// Exercises the real media seam while LVGL, WAMR, storage and Wi-Fi keep running.
#include "voice_media_transport.hpp"
#include "board_ultra.hpp"
#include "network_service.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "ultra_test_config.h"
#include <cinttypes>
#include <cstdio>
#include <sys/time.h>

namespace {
namespace media=doodad::voice_media;
constexpr char tag[]="shell-moq-test";
constexpr unsigned kSamples=19200;
std::int16_t* pcm=nullptr;
unsigned samples=0;
bool overflow=false;
enum class State { network, ready, capturing, response, cancelling, closing, idle, done, failed };
State state=State::network;
std::uint64_t deadline=0, start=0;
void failure(const char* why) {
    ESP_LOGE(tag,"FAIL: %s",why); media::disconnect(); state=State::failed;
}
}
void voice_moq_diagnostic_pcm(const std::int16_t* source,std::size_t count) {
    // Called only on the audio owner before its completion is placed in the
    // queue. Queue transfer orders this buffer before the control-owner export.
    if (!pcm || samples+count>kSamples) { overflow=true; return; }
    for (std::size_t i=0;i<count;++i) pcm[samples+i]=source[i];
    samples+=count;
}
void voice_moq_diagnostic_tick() {
    const auto now=static_cast<std::uint64_t>(esp_timer_get_time())/1000;
    if (!start) { start=now; deadline=now+25000; }
    if (state==State::done || state==State::failed) return;
    if (now>deadline) { failure("state deadline"); return; }
    if (state==State::network && network_service_connected()) {
        // USB-provisioned test time is an explicit bench trust input. Stop
        // unrelated SNTP adjustments during this short test. Production must
        // implement authenticated time/bootstrap; this is not that policy.
        esp_netif_sntp_deinit();
        timeval utc{}; utc.tv_sec=ULTRA_PROVISIONED_UNIX_TIME;
        if (settimeofday(&utc,nullptr)!=0) { failure("test time"); return; }
        pcm=static_cast<std::int16_t*>(heap_caps_malloc(kSamples*sizeof(*pcm),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!pcm) { failure("record buffer"); return; }
        media::Session session{}; session.generation=1;
        session.host=ULTRA_PEER_HOST; session.port=ULTRA_PEER_PORT; session.roots_pem=ULTRA_CA_PEM;
        session.setup_path="/voicewatch-test?jwt=public-test-token";
        session.local_broadcast="room/watch"; session.remote_broadcast="room/server";
        session.authorization_valid_until_ms=now+45000; session.trusted_time_valid_until_ms=now+45000;
        if (!media::connect(session)) { failure("connect rejected"); return; }
        state=State::ready; deadline=now+20000;
        ESP_LOGI(tag,"explicit USB diagnostic connected; microphone still off");
    }
    media::Event e{};
    while (media::poll(e)) {
        ESP_LOGI(tag,"event kind=%u session=%" PRIu64 " capture=%" PRIu64 " response=%" PRIu64 " samples=%" PRIu64 " first=%" PRIu64 " end=%" PRIu64 " cancelled=%d error=%d",
            static_cast<unsigned>(e.kind),e.session,e.identity.capture_id,e.response_id,e.samples,e.first_group,e.end_group,e.cancelled,e.error);
        if (state!=State::cancelling && state!=State::closing && state!=State::idle &&
            (e.kind==media::EventKind::error || e.kind==media::EventKind::capture_failed || e.kind==media::EventKind::disconnected)) {
            failure("unexpected media lifecycle failure"); return;
        }
        if (state==State::ready && e.kind==media::EventKind::ready) {
            twatch_ultra_audio_stats_t io{}; twatch_ultra_audio_stats(doodad::board::ultra_audio_board(),&io);
            if (io.microphone_running || io.speaker_running) { failure("automatic audio start"); return; }
            if (!media::capture_begin({71,72,73},1200)) { failure("capture command"); return; }
            state=State::capturing; deadline=now+10000;
        } else if (state==State::capturing && e.kind==media::EventKind::capture_stopped) {
            if (e.samples!=kSamples || e.dropped_frames || e.cancelled || e.first_group!=0 || e.end_group!=62) {
                failure("capture count/tail/ownership"); return;
            }
            media::Response response{}; response.session=1; response.response_id=1;
            response.identity={71,72,73}; response.first_group=0; response.end_group=61; response.has_end=true;
            if (!media::receive_begin(response)) { failure("response command"); return; }
            state=State::response; deadline=now+10000;
        } else if (state==State::response && e.kind==media::EventKind::playback_finished) {
            if (e.cancelled || e.error || e.samples!=kSamples || samples!=kSamples || overflow) {
                failure("response length/completion"); return;
            }
            twatch_ultra_audio_stats_t io{}; twatch_ultra_audio_stats(doodad::board::ultra_audio_board(),&io);
            if (io.microphone_running || io.speaker_running || !io.speaker_drained || io.microphone_dropped || io.speaker_completed!=kSamples) {
                failure("DMA/capture completion"); return;
            }
            for (unsigned at=0;at<samples;at+=160) {
                char hex[641];
                for (unsigned i=0;i<160;++i) { const auto sample=static_cast<std::uint16_t>(pcm[at+i]); std::snprintf(hex+4*i,5,"%02x%02x",sample&255,sample>>8); }
                ESP_LOGI(tag,"ECHOPCM offset=%u hex=%s",at,hex);
            }
            heap_caps_free(pcm); pcm=nullptr;
            // The reference removes its rendition after finishing. Cancellation
            // still clears local media even when readiness has already fallen.
            media::cancel(); state=State::cancelling; deadline=now+1000;
        } else if (state==State::closing && e.kind==media::EventKind::disconnected) {
            state=State::idle; deadline=start+90000;
        }
    }
    if (state==State::cancelling && deadline-now<=800) {
        twatch_ultra_audio_stats_t io{}; twatch_ultra_audio_stats(doodad::board::ultra_audio_board(),&io);
        if (media::recording() || io.microphone_running || io.speaker_running) { failure("cancel revived audio"); return; }
        media::disconnect(); state=State::closing; deadline=now+5000;
    }
    if (state==State::idle && now-start>=60000) {
        twatch_ultra_audio_stats_t io{}; twatch_ultra_audio_stats(doodad::board::ultra_audio_board(),&io);
        if (media::ready() || media::recording() || io.microphone_running || io.speaker_running || !heap_caps_check_integrity_all(true)) {
            failure("idle cleanup"); return;
        }
        ESP_LOGI(tag,"SHELL_MOQ_FINAL pass=1 samples=%u internal_free=%u internal_min=%u internal_largest=%u psram_free=%u",
            samples,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        state=State::done;
    }
}
