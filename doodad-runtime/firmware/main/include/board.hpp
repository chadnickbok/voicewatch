#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"

namespace doodad::board {

struct Identity {
    const char* model;
    const char* board;
    char device_id[48];
};

struct TouchPoint {
    std::int16_t x;
    std::int16_t y;
};

struct InputState {
    bool button_a_clicked;
    bool button_b_clicked;
    bool button_b_held;
    bool button_c_clicked;
    bool power_clicked;
};

struct AudioConfig {
    std::uint32_t sample_rate;
    std::size_t microphone_dma_length;
    std::size_t microphone_dma_count;
    std::size_t speaker_dma_length;
    std::size_t speaker_dma_count;
    std::uint8_t task_priority;
    std::uint8_t speaker_volume;
    std::uint8_t microphone_gain;
};

bool init();
const Identity& identity();

std::int32_t display_width();
std::int32_t display_height();
std::int32_t viewport_x();
std::uint32_t display_spi_frequency_hz();
std::uint32_t draw_buffer_caps();
bool display_flush(std::int32_t x, std::int32_t y,
                   std::int32_t width, std::int32_t height,
                   const std::uint16_t* pixels);
void display_fill(std::uint16_t color);
std::uint8_t display_default_brightness();
void display_set_brightness(std::uint8_t level);

bool touch_read(TouchPoint& point);
void update();
InputState take_input();

bool has_microsd();

void audio_configure(const AudioConfig& config);
bool microphone_begin();
bool microphone_running();
bool microphone_recording();
bool microphone_record(std::int16_t* samples, std::size_t count,
                       std::uint32_t sample_rate);
void microphone_end();
bool speaker_begin();
bool speaker_running();
bool speaker_playing();
bool speaker_play(const std::int16_t* samples, std::size_t count,
                  std::uint32_t sample_rate);
void speaker_end();

bool haptic(std::uint8_t effect);
int battery_percent();

}  // namespace doodad::board
