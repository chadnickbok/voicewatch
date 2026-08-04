#include "board.hpp"

#include <cstdio>

#include "M5Unified.h"
#include "esp_mac.h"

namespace doodad::board {
namespace {

Identity g_identity{"M5Stack CoreS3 SE", "cores3-se", {}};
AudioConfig g_audio{};

void initialize_identity() {
    std::uint8_t mac[6]{};
    esp_efuse_mac_get_default(mac);
    std::snprintf(
        g_identity.device_id, sizeof(g_identity.device_id),
        "cores3-se-%02x%02x%02x%02x%02x%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace

bool init() {
    auto config = M5.config();
    config.clear_display = true;
    config.internal_imu = false;
    config.internal_rtc = false;
    config.internal_mic = true;
    config.internal_spk = true;
    config.external_display_value = 0;
    config.fallback_board = m5::board_t::board_M5StackCoreS3SE;
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(96);
    M5.Display.setSwapBytes(true);
    auto* bus = M5.Display.getPanel()->getBus();
    if (bus == nullptr) return false;
    bus->setClock(display_spi_frequency_hz());
    M5.Display.initDMA();
    const auto detected = M5.getBoard();
    if (detected != m5::board_t::board_M5StackCoreS3 &&
        detected != m5::board_t::board_M5StackCoreS3SE) {
        return false;
    }
    initialize_identity();
    return display_width() >= 240 && display_height() >= 240;
}

const Identity& identity() { return g_identity; }
std::int32_t display_width() { return M5.Display.width(); }
std::int32_t display_height() { return M5.Display.height(); }
std::int32_t viewport_x() { return (display_width() - 240) / 2; }
std::uint32_t display_spi_frequency_hz() { return 40U * 1000U * 1000U; }
std::uint32_t draw_buffer_caps() { return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT; }

bool display_flush(std::int32_t x, std::int32_t y,
                   std::int32_t width, std::int32_t height,
                   const std::uint16_t* pixels) {
    M5.Display.pushImage(x, y, width, height, pixels);
    return true;
}

void display_fill(std::uint16_t color) { M5.Display.fillScreen(color); }
std::uint8_t display_default_brightness() { return 96; }
void display_set_brightness(std::uint8_t level) {
    M5.Display.setBrightness(level);
}

bool touch_read(TouchPoint& point) {
    if (M5.Touch.getCount() == 0) return false;
    const auto detail = M5.Touch.getDetail(0);
    point.x = static_cast<std::int16_t>(detail.x - viewport_x());
    point.y = static_cast<std::int16_t>(detail.y);
    return point.x >= 0 && point.x < 240 && point.y >= 0 && point.y < 240;
}

void update() { M5.update(); }

InputState take_input() {
    return InputState{
        M5.BtnA.wasClicked(), M5.BtnB.wasClicked(), M5.BtnB.wasHold(),
        M5.BtnC.wasClicked(), M5.BtnPWR.wasClicked()};
}

bool has_microsd() { return true; }

void audio_configure(const AudioConfig& config) {
    g_audio = config;
    auto microphone = M5.Mic.config();
    microphone.sample_rate = config.sample_rate;
    microphone.magnification = config.microphone_gain;
    microphone.dma_buf_len = config.microphone_dma_length;
    microphone.dma_buf_count = config.microphone_dma_count;
    microphone.task_priority = config.task_priority;
    M5.Mic.config(microphone);
    auto speaker = M5.Speaker.config();
    speaker.sample_rate = config.sample_rate;
    speaker.dma_buf_len = config.speaker_dma_length;
    speaker.dma_buf_count = config.speaker_dma_count;
    speaker.task_priority = config.task_priority;
    M5.Speaker.config(speaker);
}

bool microphone_begin() { return M5.Mic.begin(); }
bool microphone_running() { return M5.Mic.isRunning(); }
bool microphone_recording() { return M5.Mic.isRecording(); }
bool microphone_record(std::int16_t* samples, std::size_t count,
                       std::uint32_t sample_rate) {
    return M5.Mic.record(samples, count, sample_rate, false);
}
void microphone_end() { M5.Mic.end(); }
bool speaker_begin() {
    M5.Speaker.setVolume(g_audio.speaker_volume);
    return M5.Speaker.begin();
}
bool speaker_running() { return M5.Speaker.isRunning(); }
bool speaker_playing() { return M5.Speaker.isPlaying(0) != 0; }
bool speaker_play(const std::int16_t* samples, std::size_t count,
                  std::uint32_t sample_rate) {
    return M5.Speaker.playRaw(samples, count, sample_rate, false, 1, 0, false);
}
void speaker_end() { M5.Speaker.end(); }
bool haptic(std::uint8_t) { return false; }
int battery_percent() { return -1; }

}  // namespace doodad::board
