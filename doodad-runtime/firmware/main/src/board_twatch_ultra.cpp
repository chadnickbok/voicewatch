#include "board.hpp"
#include "board_ultra.hpp"
#include "twatch_ultra_ui.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace doodad::board {
namespace {
constexpr char kTag[]="twatch-ultra";
twatch_ultra_t* g_board=nullptr;
twatch_ultra_ui_t* g_ui=nullptr;
Identity g_identity{"LilyGO T-Watch Ultra", "t-watch-ultra", {}};
AudioConfig g_audio{16000,160,3,160,2,6,64,1};
twatch_ultra_input_t g_touch{};
InputState g_input{};
bool g_awake=true,g_wake_touch=false,g_recording=false;
std::uint64_t g_speaker_owner=0,g_last_poll=0,g_microphone_generation=0,g_next_sample=0;
}
bool init() {
    if(g_ui) return true;
    if(g_board) return false; // A failed init retains reachable cleanup state.
    auto r=twatch_ultra_open(&g_board);
    if(!r) r=twatch_ultra_ui_open(g_board,&g_ui);
    if(r) { ESP_LOGE(kTag,"board init failed: %s",esp_err_to_name(r)); return false; }
    std::uint8_t mac[6]{};
    if(esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) return false;
    std::snprintf(g_identity.device_id,sizeof(g_identity.device_id),
        "ultra-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    ESP_LOGI(kTag,"board ready display=410x502 mic=off speaker=off");
    return true;
}
const Identity& identity() { return g_identity; }
std::int32_t display_width() { return TWATCH_ULTRA_WIDTH; }
std::int32_t display_height() { return TWATCH_ULTRA_HEIGHT; }
std::int32_t viewport_x() { return 0; }
std::uint32_t display_spi_frequency_hz() { return 40000000; }
std::uint32_t draw_buffer_caps() { return MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT; }
bool display_flush(std::int32_t x,std::int32_t y,std::int32_t width,std::int32_t height,const std::uint16_t* pixels) {
    return x>=0 && y>=0 && width>0 && height>0 && twatch_ultra_ui_flush(g_ui,x,y,width,height,pixels)==ESP_OK;
}
void display_fill(std::uint16_t color) {
    std::uint16_t row[TWATCH_ULTRA_WIDTH];
    std::fill(row,row+TWATCH_ULTRA_WIDTH,color);
    for(unsigned y=0;y<TWATCH_ULTRA_HEIGHT;++y)
        if(twatch_ultra_ui_flush(g_ui,0,y,TWATCH_ULTRA_WIDTH,1,row)!=ESP_OK) {
            ESP_LOGE(kTag,"display fill failed"); break;
        }
}
std::uint8_t display_default_brightness() { return 80; }
void display_set_brightness(std::uint8_t level) {
    if(twatch_ultra_ui_brightness(g_ui,level)==ESP_OK) g_awake=level!=0;
}
void update() {
    const auto now=static_cast<std::uint64_t>(esp_timer_get_time());
    if(!g_ui || now-g_last_poll<10000) return;
    g_last_poll=now;
    if(twatch_ultra_ui_poll(g_ui,&g_touch)!=ESP_OK) g_touch.pressed=false;
    g_input.power_clicked|=g_touch.power_clicked;
    // Existing shell button_b_held is a one-shot action, not a physical level.
    // Emit once on a debounced fresh press; never toggle capture every UI tick.
    g_input.button_b_held|=g_touch.button_pressed;
    g_input.button_a_clicked|=g_touch.palm;
    if(!g_awake && g_touch.pressed && !g_wake_touch) {
        g_input.power_clicked=true; g_wake_touch=true;
    }
    if(!g_touch.pressed) g_wake_touch=false;
}
InputState take_input() { auto value=g_input; g_input={}; return value; }
bool touch_read(TouchPoint& point) {
    if(!g_awake || g_wake_touch || !g_touch.pressed) return false;
    point={static_cast<std::int16_t>(g_touch.x),static_cast<std::int16_t>(g_touch.y)}; return true;
}
bool has_microsd() { return false; } // SD peripheral not enabled by this BSP.
bool haptic(std::uint8_t effect) { return twatch_ultra_ui_haptic(g_ui,effect)==ESP_OK; }
int battery_percent() { twatch_ultra_ui_stats_t s{}; twatch_ultra_ui_stats(g_ui,&s); return g_ui?s.battery_percent:-1; }
twatch_ultra_t* ultra_audio_board() { return g_board; }
void audio_configure(const AudioConfig& config) { g_audio=config; }
bool microphone_begin() {
    if(!g_board || g_audio.sample_rate!=TWATCH_ULTRA_AUDIO_RATE) return false;
    if(microphone_running()) return true;
    g_microphone_generation=0; g_next_sample=0;
    return twatch_ultra_microphone_start(g_board)==ESP_OK;
}
bool microphone_running() { twatch_ultra_audio_stats_t s{}; twatch_ultra_audio_stats(g_board,&s); return s.microphone_running; }
bool microphone_recording() { return g_recording; }
bool microphone_record(std::int16_t* samples,std::size_t count,std::uint32_t rate) {
    if(!samples || !count || count>320 || count%160 || rate!=16000 || !microphone_running()) return false;
    g_recording=true;
    const auto deadline=esp_timer_get_time()+60000;
    std::size_t copied=0;
    while(copied<count && esp_timer_get_time()<deadline) {
        twatch_ultra_microphone_chunk_t chunk{};
        const auto r=twatch_ultra_microphone_read(g_board,&chunk);
        if(r==ESP_ERR_NOT_FOUND) { vTaskDelay(1); continue; }
        if(r!=ESP_OK || (g_microphone_generation && (chunk.generation!=g_microphone_generation || chunk.sample_index!=g_next_sample))) break;
        g_microphone_generation=chunk.generation; g_next_sample=chunk.sample_index+160;
        std::memcpy(samples+copied,chunk.pcm,sizeof(chunk.pcm)); copied+=160;
    }
    g_recording=false; return copied==count;
}
void microphone_end() { if(g_board) twatch_ultra_microphone_stop(g_board); g_recording=false; }
bool speaker_begin() {
    if(!g_board || g_audio.sample_rate!=16000 || g_speaker_owner==std::numeric_limits<std::uint64_t>::max()) return false;
    if(speaker_running()) return true;
    return twatch_ultra_speaker_start(g_board,++g_speaker_owner,g_audio.speaker_volume)==ESP_OK;
}
bool speaker_running() { twatch_ultra_audio_stats_t s{}; twatch_ultra_audio_stats(g_board,&s); return s.speaker_running; }
bool speaker_playing() { twatch_ultra_audio_stats_t s{}; twatch_ultra_audio_stats(g_board,&s); return !s.speaker_drained; }
bool speaker_play(const std::int16_t* samples,std::size_t count,std::uint32_t rate) {
    return rate==16000 && twatch_ultra_speaker_submit(g_board,g_speaker_owner,samples,count);
}
void speaker_end() { if(g_board) twatch_ultra_speaker_stop(g_board); }
} // namespace doodad::board
