#include "display.hpp"

#include <algorithm>
#include <cstring>

#include "M5Unified.h"
#include "esp_log.h"

namespace {

constexpr auto kBackground = 0x0841;
constexpr auto kPanel = 0x10A2;
constexpr auto kPrimary = 0xFFFF;
constexpr auto kMuted = 0x9CF3;
constexpr auto kAccent = 0x5FE0;
constexpr auto kError = 0xF986;

bool g_display_ready = false;
char g_source[16] = "EMBEDDED";

void draw_footer() {
    const auto width = M5.Display.width();
    const auto height = M5.Display.height();

    M5.Display.fillRect(0, height - 34, width, 34, kPanel);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(kMuted, kPanel);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_left);
    M5.Display.drawString("HOST ABI v1", 12, height - 17);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_right);
    M5.Display.drawString(g_source, width - 12, height - 17);
}

}  // namespace

bool display_init() {
    auto config = M5.config();
    config.clear_display = true;
    config.internal_imu = false;
    config.internal_rtc = false;
    config.internal_mic = false;
    config.internal_spk = false;
    config.external_display_value = 0;
    config.fallback_board = m5::board_t::board_M5StackCoreS3SE;

    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(96);

    const auto board = M5.getBoard();
    g_display_ready =
        M5.Display.width() > 0 && M5.Display.height() > 0
        && (board == m5::board_t::board_M5StackCoreS3
            || board == m5::board_t::board_M5StackCoreS3SE);

    if (!g_display_ready) {
        ESP_LOGE("doodad", "[host] display init failed (board=%d, size=%dx%d)",
                 static_cast<int>(board), M5.Display.width(), M5.Display.height());
    }
    return g_display_ready;
}

void display_shell(const char* status, const char* source) {
    if (!g_display_ready) {
        return;
    }

    std::strncpy(g_source, source, sizeof(g_source) - 1);
    g_source[sizeof(g_source) - 1] = '\0';

    const auto width = M5.Display.width();
    M5.Display.startWrite();
    M5.Display.fillScreen(kBackground);
    M5.Display.fillRect(0, 0, width, 46, kPanel);

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(kPrimary, kPanel);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_left);
    M5.Display.drawString("DOODAD", 12, 23);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(kAccent, kPanel);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_right);
    M5.Display.drawString(status, width - 12, 23);
    draw_footer();
    M5.Display.endWrite();
}

void display_guest_text(const char* text, std::size_t length) {
    if (!g_display_ready) {
        return;
    }

    constexpr std::size_t kLocalCapacity = 129;
    char local[kLocalCapacity]{};
    const auto copy_length = std::min(length, kLocalCapacity - 1);
    std::memcpy(local, text, copy_length);

    const auto width = M5.Display.width();
    const auto content_top = 46;
    const auto content_height = M5.Display.height() - content_top - 34;

    M5.Display.startWrite();
    M5.Display.fillRect(0, content_top, width, content_height, kBackground);
    M5.Display.setTextColor(kPrimary, kBackground);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_center);
    M5.Display.drawString(local, width / 2, content_top + content_height / 2);
    M5.Display.endWrite();
}

void display_error(const char* stage) {
    if (!g_display_ready) {
        return;
    }

    const auto width = M5.Display.width();
    const auto height = M5.Display.height();
    M5.Display.startWrite();
    M5.Display.fillScreen(kBackground);
    M5.Display.fillRect(0, 0, width, 46, kPanel);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_left);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(kPrimary, kPanel);
    M5.Display.drawString("DOODAD", 12, 23);
    M5.Display.setTextDatum(m5gfx::textdatum_t::middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(kError, kBackground);
    M5.Display.drawString(stage, width / 2, height / 2);
    draw_footer();
    M5.Display.endWrite();
}
