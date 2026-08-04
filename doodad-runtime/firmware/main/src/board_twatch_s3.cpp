#include "board.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace doodad::board {
namespace {

constexpr char kTag[] = "twatch_s3";
constexpr i2c_port_t kSystemI2c = I2C_NUM_0;
constexpr i2c_port_t kTouchI2c = I2C_NUM_1;
constexpr std::uint8_t kPmicAddress = 0x34;
constexpr std::uint8_t kTouchAddress = 0x38;
constexpr std::uint8_t kHapticAddress = 0x5a;
constexpr gpio_num_t kSystemSda = GPIO_NUM_10;
constexpr gpio_num_t kSystemScl = GPIO_NUM_11;
constexpr gpio_num_t kTouchSda = GPIO_NUM_39;
constexpr gpio_num_t kTouchScl = GPIO_NUM_40;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_16;
constexpr gpio_num_t kPmicInterrupt = GPIO_NUM_21;
constexpr gpio_num_t kDisplayClock = GPIO_NUM_18;
constexpr gpio_num_t kDisplayMosi = GPIO_NUM_13;
constexpr gpio_num_t kDisplayCs = GPIO_NUM_12;
constexpr gpio_num_t kDisplayDc = GPIO_NUM_38;
constexpr gpio_num_t kDisplayBacklight = GPIO_NUM_45;
constexpr gpio_num_t kMicClock = GPIO_NUM_44;
constexpr gpio_num_t kMicData = GPIO_NUM_47;
constexpr gpio_num_t kSpeakerBclk = GPIO_NUM_48;
constexpr gpio_num_t kSpeakerLrclk = GPIO_NUM_15;
constexpr gpio_num_t kSpeakerData = GPIO_NUM_46;

Identity g_identity{"LilyGO T-Watch S3", "t-watch-s3", {}};
AudioConfig g_audio{};
esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
SemaphoreHandle_t g_flush_done = nullptr;
i2s_chan_handle_t g_microphone = nullptr;
i2s_chan_handle_t g_speaker = nullptr;
bool g_microphone_enabled = false;
bool g_microphone_recording = false;
bool g_speaker_enabled = false;
std::int64_t g_speaker_until_us = 0;
volatile bool g_pmic_interrupt_pending = false;
bool g_power_clicked = false;
bool g_display_awake = true;
bool g_touch_wake_latched = false;

bool check(esp_err_t result, const char* action) {
    if (result == ESP_OK) return true;
    ESP_LOGE(kTag, "%s failed: %s", action, esp_err_to_name(result));
    return false;
}

esp_err_t i2c_write(i2c_port_t port, std::uint8_t address,
                    std::uint8_t reg, std::uint8_t value) {
    const std::uint8_t payload[]{reg, value};
    return i2c_master_write_to_device(
        port, address, payload, sizeof(payload), pdMS_TO_TICKS(100));
}

esp_err_t i2c_read(i2c_port_t port, std::uint8_t address,
                   std::uint8_t reg, std::uint8_t* output,
                   std::size_t length) {
    return i2c_master_write_read_device(
        port, address, &reg, 1, output, length, pdMS_TO_TICKS(100));
}

bool update_register(i2c_port_t port, std::uint8_t address,
                     std::uint8_t reg, std::uint8_t mask,
                     std::uint8_t value) {
    std::uint8_t current = 0;
    return i2c_read(port, address, reg, &current, 1) == ESP_OK &&
        i2c_write(port, address, reg,
                  static_cast<std::uint8_t>((current & ~mask) | value)) == ESP_OK;
}

bool configure_i2c(i2c_port_t port, gpio_num_t sda, gpio_num_t scl) {
    const i2c_config_t config{
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = 400'000},
        .clk_flags = 0,
    };
    return i2c_param_config(port, &config) == ESP_OK &&
        i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0) == ESP_OK;
}

bool configure_pmic() {
    std::uint8_t status = 0;
    if (i2c_read(kSystemI2c, kPmicAddress, 0x00, &status, 1) != ESP_OK) {
        ESP_LOGE(kTag, "AXP2101 did not respond");
        return false;
    }
    // ALDO2: LCD backlight, ALDO3: LCD/touch, BLDO2: DRV2605.
    // AXP2101 encodes 0.5 V + N*0.1 V for these rails; 0x1c is 3.3 V.
    const bool voltages =
        i2c_write(kSystemI2c, kPmicAddress, 0x93, 0x1c) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x94, 0x1c) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x97, 0x1c) == ESP_OK;
    const bool enabled = update_register(
        kSystemI2c, kPmicAddress, 0x90, 0x26, 0x26);
    if (!voltages || !enabled) {
        ESP_LOGE(kTag, "AXP2101 display/haptic rail setup failed");
        return false;
    }
    // Match LilyGO's conservative T-Watch charge policy: 4.36 V VBUS DPM,
    // 900 mA input limit, 50 mA precharge, 125 mA constant current,
    // 25 mA termination, and the board's 4.35 V high-voltage battery.
    const bool charging =
        update_register(kSystemI2c, kPmicAddress, 0x15, 0x0f, 0x06) &&
        update_register(kSystemI2c, kPmicAddress, 0x16, 0x07, 0x02) &&
        update_register(kSystemI2c, kPmicAddress, 0x61, 0x0f, 0x02) &&
        update_register(kSystemI2c, kPmicAddress, 0x62, 0x1f, 0x05) &&
        update_register(kSystemI2c, kPmicAddress, 0x63, 0x0f, 0x01) &&
        update_register(kSystemI2c, kPmicAddress, 0x64, 0x07, 0x04);
    // Only power-key IRQs are enabled here; charge/battery state is polled.
    const bool interrupts =
        i2c_write(kSystemI2c, kPmicAddress, 0x40, 0x00) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x41, 0x0c) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x42, 0x00) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x48, 0xff) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x49, 0xff) == ESP_OK &&
        i2c_write(kSystemI2c, kPmicAddress, 0x4a, 0xff) == ESP_OK;
    if (!charging || !interrupts) {
        ESP_LOGE(kTag, "AXP2101 charge/interrupt setup failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

void IRAM_ATTR pmic_interrupt(void*) {
    g_pmic_interrupt_pending = true;
}

bool IRAM_ATTR flush_finished(
    esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(g_flush_done, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

bool configure_display() {
    spi_bus_config_t bus{};
    bus.mosi_io_num = kDisplayMosi;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.sclk_io_num = kDisplayClock;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 240 * 40 * sizeof(std::uint16_t);
    if (!check(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
               "display SPI bus")) return false;

    g_flush_done = xSemaphoreCreateBinary();
    if (g_flush_done == nullptr) return false;
    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = kDisplayCs;
    io.dc_gpio_num = kDisplayDc;
    io.spi_mode = 0;
    io.pclk_hz = display_spi_frequency_hz();
    io.trans_queue_depth = 2;
    io.on_color_trans_done = flush_finished;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    if (!check(esp_lcd_new_panel_io_spi(
            static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST),
            &io, &g_panel_io),
        "display panel IO")) return false;

    esp_lcd_panel_dev_config_t panel{};
    panel.reset_gpio_num = GPIO_NUM_NC;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel.bits_per_pixel = 16;
    if (!check(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel),
               "ST7789 panel") ||
        !check(esp_lcd_panel_reset(g_panel), "panel reset") ||
        !check(esp_lcd_panel_init(g_panel), "panel init")) return false;

    struct Command {
        std::uint8_t command;
        std::uint8_t data[14];
        std::uint8_t length;
    };
    static constexpr Command commands[]{
        {0x11, {}, 0},
        {0xb2, {0x1f, 0x1f, 0x00, 0x33, 0x33}, 5},
        {0x35, {0x00}, 1}, {0x36, {0x00}, 1}, {0x3a, {0x05}, 1},
        {0xb7, {0x00}, 1}, {0xbb, {0x36}, 1}, {0xc0, {0x2c}, 1},
        {0xc2, {0x01}, 1}, {0xc3, {0x13}, 1}, {0xc4, {0x20}, 1},
        {0xc6, {0x13}, 1}, {0xd6, {0xa1}, 1}, {0xd0, {0xa4, 0xa1}, 2},
        {0xe0, {0xf0, 0x08, 0x0e, 0x09, 0x08, 0x04, 0x2f,
                0x33, 0x45, 0x36, 0x13, 0x12, 0x2a, 0x2d}, 14},
        {0xe1, {0xf0, 0x0e, 0x12, 0x0c, 0x0a, 0x15, 0x2e,
                0x32, 0x44, 0x39, 0x17, 0x18, 0x2b, 0x2f}, 14},
        {0xe4, {0x1d, 0x00, 0x00}, 3},
    };
    for (const auto& command : commands) {
        if (!check(esp_lcd_panel_io_tx_param(
                g_panel_io, command.command, command.data, command.length),
            "panel vendor command")) return false;
        if (command.command == 0x11) vTaskDelay(pdMS_TO_TICKS(120));
    }
    if (!check(esp_lcd_panel_invert_color(g_panel, true), "panel inversion") ||
        !check(esp_lcd_panel_set_gap(g_panel, 0, 80), "panel gap") ||
        !check(esp_lcd_panel_swap_xy(g_panel, false), "panel orientation") ||
        !check(esp_lcd_panel_mirror(g_panel, true, true), "panel mirror") ||
        !check(esp_lcd_panel_disp_on_off(g_panel, true), "panel on")) {
        return false;
    }

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (!check(ledc_timer_config(&timer), "backlight timer")) return false;
    ledc_channel_config_t channel{};
    channel.gpio_num = kDisplayBacklight;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = display_default_brightness();
    if (!check(ledc_channel_config(&channel), "backlight")) return false;
    return true;
}

void initialize_identity() {
    std::uint8_t mac[6]{};
    esp_efuse_mac_get_default(mac);
    std::snprintf(
        g_identity.device_id, sizeof(g_identity.device_id),
        "t-watch-s3-%02x%02x%02x%02x%02x%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace

bool init() {
    if (!configure_i2c(kSystemI2c, kSystemSda, kSystemScl) ||
        !configure_i2c(kTouchI2c, kTouchSda, kTouchScl) ||
        !configure_pmic() || !configure_display()) {
        return false;
    }
    gpio_set_direction(kTouchInterrupt, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kTouchInterrupt, GPIO_PULLUP_ONLY);
    gpio_set_direction(kPmicInterrupt, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kPmicInterrupt, GPIO_PULLUP_ONLY);
    const auto isr_result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isr_result != ESP_OK && isr_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "GPIO ISR service failed: %s", esp_err_to_name(isr_result));
        return false;
    }
    if (gpio_isr_handler_add(kPmicInterrupt, pmic_interrupt, nullptr) != ESP_OK ||
        gpio_set_intr_type(kPmicInterrupt, GPIO_INTR_NEGEDGE) != ESP_OK) {
        ESP_LOGE(kTag, "PMIC interrupt setup failed");
        return false;
    }
    initialize_identity();
    display_fill(0);
    ESP_LOGI(kTag, "board ready device_id=%s", g_identity.device_id);
    return true;
}

const Identity& identity() { return g_identity; }
std::int32_t display_width() { return 240; }
std::int32_t display_height() { return 240; }
std::int32_t viewport_x() { return 0; }
std::uint32_t display_spi_frequency_hz() { return 40U * 1000U * 1000U; }
std::uint32_t draw_buffer_caps() {
    return MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
}

bool display_flush(std::int32_t x, std::int32_t y,
                   std::int32_t width, std::int32_t height,
                   const std::uint16_t* pixels) {
    if (g_panel == nullptr || pixels == nullptr) return false;
    while (xSemaphoreTake(g_flush_done, 0) == pdTRUE) {}
    if (esp_lcd_panel_draw_bitmap(
            g_panel, x, y, x + width, y + height, pixels) != ESP_OK) {
        return false;
    }
    return xSemaphoreTake(g_flush_done, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void display_fill(std::uint16_t color) {
    constexpr std::size_t kRows = 8;
    constexpr std::size_t kPixels = 240 * kRows;
    auto* strip = static_cast<std::uint16_t*>(heap_caps_malloc(
        kPixels * sizeof(std::uint16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (strip == nullptr) {
        ESP_LOGE(kTag, "display fill scratch allocation failed");
        return;
    }
    std::fill(strip, strip + kPixels, color);
    for (int y = 0; y < 240; y += kRows) {
        display_flush(0, y, 240, kRows, strip);
    }
    heap_caps_free(strip);
}

std::uint8_t display_default_brightness() { return 255; }

void display_set_brightness(std::uint8_t level) {
    g_display_awake = level != 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

bool touch_read(TouchPoint& point) {
    std::uint8_t data[5]{};
    if (i2c_read(kTouchI2c, kTouchAddress, 0x02, data, sizeof(data)) != ESP_OK ||
        (data[0] & 0x0f) == 0) {
        return false;
    }
    const auto raw_x = static_cast<std::int16_t>(((data[1] & 0x0f) << 8) | data[2]);
    const auto raw_y = static_cast<std::int16_t>(((data[3] & 0x0f) << 8) | data[4]);
    point.x = std::clamp<std::int16_t>(raw_x, 0, 239);
    point.y = std::clamp<std::int16_t>(raw_y, 0, 239);
    return true;
}

void update() {
    if (g_pmic_interrupt_pending) {
        g_pmic_interrupt_pending = false;
        std::uint8_t status = 0;
        if (i2c_read(kSystemI2c, kPmicAddress, 0x49, &status, 1) == ESP_OK) {
            g_power_clicked = g_power_clicked || (status & 0x08) != 0;
        }
        i2c_write(kSystemI2c, kPmicAddress, 0x48, 0xff);
        i2c_write(kSystemI2c, kPmicAddress, 0x49, 0xff);
        i2c_write(kSystemI2c, kPmicAddress, 0x4a, 0xff);
    }
    // Keep the FT6336U powered. When the display is off, the first touch is a
    // wake gesture and is consumed instead of leaking through to the app.
    const bool touching = gpio_get_level(kTouchInterrupt) == 0;
    if (!g_display_awake && touching && !g_touch_wake_latched) {
        g_power_clicked = true;
        g_touch_wake_latched = true;
    } else if (!touching) {
        g_touch_wake_latched = false;
    }
}
InputState take_input() {
    InputState state{};
    state.power_clicked = g_power_clicked;
    g_power_clicked = false;
    return state;
}
bool has_microsd() { return false; }

void audio_configure(const AudioConfig& config) { g_audio = config; }

bool microphone_begin() {
    if (g_microphone_enabled) return true;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&channel, nullptr, &g_microphone) != ESP_OK) return false;
    i2s_pdm_rx_config_t config = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(g_audio.sample_rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = kMicClock,
            .din = kMicData,
            .invert_flags = {.clk_inv = false},
        },
    };
    if (i2s_channel_init_pdm_rx_mode(g_microphone, &config) != ESP_OK ||
        i2s_channel_enable(g_microphone) != ESP_OK) {
        i2s_del_channel(g_microphone);
        g_microphone = nullptr;
        return false;
    }
    g_microphone_enabled = true;
    return true;
}

bool microphone_running() { return g_microphone_enabled; }
bool microphone_recording() { return g_microphone_recording; }
bool microphone_record(std::int16_t* samples, std::size_t count,
                       std::uint32_t) {
    if (!g_microphone_enabled || samples == nullptr) return false;
    g_microphone_recording = true;
    std::size_t bytes_read = 0;
    const auto status = i2s_channel_read(
        g_microphone, samples, count * sizeof(*samples), &bytes_read,
        pdMS_TO_TICKS(250));
    g_microphone_recording = false;
    if (status != ESP_OK || bytes_read != count * sizeof(*samples)) return false;
    if (g_audio.microphone_gain > 1) {
        for (std::size_t index = 0; index < count; ++index) {
            const auto scaled = static_cast<std::int32_t>(samples[index]) *
                g_audio.microphone_gain;
            samples[index] = static_cast<std::int16_t>(
                std::clamp<std::int32_t>(scaled, INT16_MIN, INT16_MAX));
        }
    }
    return true;
}

void microphone_end() {
    if (g_microphone != nullptr) {
        if (g_microphone_enabled) i2s_channel_disable(g_microphone);
        i2s_del_channel(g_microphone);
    }
    g_microphone = nullptr;
    g_microphone_enabled = false;
    g_microphone_recording = false;
}

bool speaker_begin() {
    if (g_speaker_enabled) return true;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&channel, &g_speaker, nullptr) != ESP_OK) return false;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(g_audio.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = kSpeakerBclk,
            .ws = kSpeakerLrclk,
            .dout = kSpeakerData,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    if (i2s_channel_init_std_mode(g_speaker, &config) != ESP_OK ||
        i2s_channel_enable(g_speaker) != ESP_OK) {
        i2s_del_channel(g_speaker);
        g_speaker = nullptr;
        return false;
    }
    g_speaker_enabled = true;
    return true;
}

bool speaker_running() { return g_speaker_enabled; }
bool speaker_playing() {
    return g_speaker_enabled && esp_timer_get_time() < g_speaker_until_us;
}
bool speaker_play(const std::int16_t* samples, std::size_t count,
                  std::uint32_t sample_rate) {
    if (!g_speaker_enabled || samples == nullptr || count > 320) return false;
    std::array<std::int16_t, 320> adjusted{};
    for (std::size_t index = 0; index < count; ++index) {
        adjusted[index] = static_cast<std::int16_t>(
            static_cast<std::int32_t>(samples[index]) * g_audio.speaker_volume / 255);
    }
    std::size_t bytes_written = 0;
    const auto status = i2s_channel_write(
        g_speaker, adjusted.data(), count * sizeof(adjusted[0]),
        &bytes_written, pdMS_TO_TICKS(250));
    g_speaker_until_us = esp_timer_get_time() +
        static_cast<std::int64_t>(count) * 1'000'000 / sample_rate;
    return status == ESP_OK && bytes_written == count * sizeof(adjusted[0]);
}
void speaker_end() {
    if (g_speaker != nullptr) {
        if (g_speaker_enabled) i2s_channel_disable(g_speaker);
        i2s_del_channel(g_speaker);
    }
    g_speaker = nullptr;
    g_speaker_enabled = false;
    g_speaker_until_us = 0;
}

bool haptic(std::uint8_t effect) {
    const auto chosen = static_cast<std::uint8_t>(std::clamp<int>(effect, 1, 123));
    return i2c_write(kSystemI2c, kHapticAddress, 0x01, 0x00) == ESP_OK &&
        i2c_write(kSystemI2c, kHapticAddress, 0x04, chosen) == ESP_OK &&
        i2c_write(kSystemI2c, kHapticAddress, 0x0c, 0x01) == ESP_OK;
}

int battery_percent() {
    std::uint8_t percent = 0;
    return i2c_read(kSystemI2c, kPmicAddress, 0xa4, &percent, 1) == ESP_OK
        ? std::min<int>(percent, 100) : -1;
}

}  // namespace doodad::board
