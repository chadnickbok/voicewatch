#include "m3e/appspec/canvas_display_list.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "m3e/foundation/display_profile.hpp"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace m3e::appspec {
namespace {

constexpr std::size_t kMaximumPaletteColors = 8;
constexpr std::size_t kMaximumCommands = 32;
constexpr std::size_t kMaximumTiles = 64;
constexpr std::int32_t kMaximumPhysicalEdge = 240;
constexpr std::size_t kMaximumCanvasPixels =
    kMaximumPhysicalEdge * kMaximumPhysicalEdge;

#if defined(ESP_PLATFORM)
std::uint16_t* g_canvas_pixels = nullptr;

std::uint16_t* canvas_pixels() {
    if (g_canvas_pixels == nullptr) {
        g_canvas_pixels = static_cast<std::uint16_t*>(
            heap_caps_malloc(
                kMaximumCanvasPixels * sizeof(std::uint16_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return g_canvas_pixels;
}
#else
alignas(4) std::array<std::uint16_t,
                      kMaximumCanvasPixels>
    g_canvas_pixels{};

std::uint16_t* canvas_pixels() {
    return g_canvas_pixels.data();
}
#endif

struct Palette {
    std::array<std::uint16_t, kMaximumPaletteColors> colors{};
    std::size_t count = 0;
};

struct Raster {
    std::uint16_t* pixels = nullptr;
    std::int32_t width = 0;
    std::int32_t height = 0;

    void clear(std::uint16_t color) {
        std::fill(pixels, pixels + width * height, color);
    }

    void pixel(std::int32_t x, std::int32_t y, std::uint16_t color) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        pixels[y * width + x] = color;
    }

    void rounded_rect(
        std::int32_t x,
        std::int32_t y,
        std::int32_t rect_width,
        std::int32_t rect_height,
        std::int32_t radius,
        std::uint16_t color) {
        const auto left = logical_edge(x);
        const auto top = logical_edge(y);
        const auto right = logical_edge(x + rect_width);
        const auto bottom = logical_edge(y + rect_height);
        const auto physical_radius = logical_edge(radius);
        for (auto py = top; py < bottom; ++py) {
            for (auto px = left; px < right; ++px) {
                if (physical_radius <= 0 ||
                    inside_rounded_rect(
                        px - left,
                        py - top,
                        right - left,
                        bottom - top,
                        physical_radius)) {
                    pixel(px, py, color);
                }
            }
        }
    }

    void circle(
        std::int32_t center_x,
        std::int32_t center_y,
        std::int32_t radius,
        std::uint16_t color) {
        const auto cx = logical_edge(center_x);
        const auto cy = logical_edge(center_y);
        const auto pr = logical_edge(radius);
        const auto squared = pr * pr;
        for (auto y = cy - pr; y <= cy + pr; ++y) {
            for (auto x = cx - pr; x <= cx + pr; ++x) {
                const auto dx = x - cx;
                const auto dy = y - cy;
                if (dx * dx + dy * dy <= squared) {
                    pixel(x, y, color);
                }
            }
        }
    }

    void line(
        std::int32_t x1,
        std::int32_t y1,
        std::int32_t x2,
        std::int32_t y2,
        std::int32_t stroke,
        std::uint16_t color) {
        auto x = logical_edge(x1);
        auto y = logical_edge(y1);
        const auto end_x = logical_edge(x2);
        const auto end_y = logical_edge(y2);
        const auto dx = end_x >= x ? end_x - x : x - end_x;
        const auto sx = x < end_x ? 1 : -1;
        const auto dy = -(end_y >= y ? end_y - y : y - end_y);
        const auto sy = y < end_y ? 1 : -1;
        auto error = dx + dy;
        const auto physical_stroke =
            std::max<std::int32_t>(1, logical_edge(stroke));
        for (;;) {
            const auto offset = physical_stroke / 2;
            for (auto py = y - offset;
                 py < y - offset + physical_stroke;
                 ++py) {
                for (auto px = x - offset;
                     px < x - offset + physical_stroke;
                     ++px) {
                    pixel(px, py, color);
                }
            }
            if (x == end_x && y == end_y) break;
            const auto doubled = error * 2;
            if (doubled >= dy) {
                error += dy;
                x += sx;
            }
            if (doubled <= dx) {
                error += dx;
                y += sy;
            }
        }
    }

 private:
    static std::int32_t logical_edge(std::int32_t logical) {
        return dp_edge_to_px(logical, watch_square_192.density_q8_8);
    }

    static bool inside_rounded_rect(
        std::int32_t x,
        std::int32_t y,
        std::int32_t width,
        std::int32_t height,
        std::int32_t radius) {
        const auto closest_x =
            x < radius ? radius :
            x >= width - radius ? width - radius - 1 : x;
        const auto closest_y =
            y < radius ? radius :
            y >= height - radius ? height - radius - 1 : y;
        const auto dx = x - closest_x;
        const auto dy = y - closest_y;
        return dx * dx + dy * dy <= radius * radius;
    }
};

class Parser {
 public:
    Parser(
        const char* display_list,
        const Palette& palette,
        std::int32_t logical_width,
        std::int32_t logical_height,
        Raster* raster)
        : cursor_(display_list),
          palette_(palette),
          width_(logical_width),
          height_(logical_height),
          raster_(raster) {}

    bool parse() {
        if (cursor_ == nullptr ||
            std::strlen(cursor_) >
                static_cast<std::size_t>(
                    kMaximumCanvasDisplayListBytes) ||
            cursor_[0] != 'v' || cursor_[1] != '1' ||
            cursor_[2] != '|') {
            return false;
        }
        cursor_ += 3;
        bool cleared = false;
        std::size_t command_count = 0;
        while (*cursor_ != '\0') {
            if (++command_count > kMaximumCommands) return false;
            const auto opcode = *cursor_++;
            if ((command_count == 1 && opcode != 'C') ||
                (command_count != 1 && opcode == 'C')) {
                return false;
            }
            const bool ok =
                opcode == 'C' ? clear(cleared) :
                opcode == 'R' ? rectangle() :
                opcode == 'O' ? circle() :
                opcode == 'L' ? line() :
                opcode == 'T' ? tile_map() : false;
            if (!ok) return false;
            if (*cursor_ == '\0') break;
            if (*cursor_++ != '|') return false;
        }
        return cleared && command_count != 0;
    }

 private:
    bool clear(bool& cleared) {
        if (cleared) return false;
        std::int32_t color = 0;
        if (!number(color) || !end_of_command() || !color_index(color)) {
            return false;
        }
        cleared = true;
        if (raster_ != nullptr) raster_->clear(palette_.colors[color]);
        return true;
    }

    bool rectangle() {
        std::array<std::int32_t, 6> values{};
        if (!numbers(values) || !end_of_command() ||
            !color_index(values[0])) {
            return false;
        }
        const auto x = values[1];
        const auto y = values[2];
        const auto rect_width = values[3];
        const auto rect_height = values[4];
        const auto radius = values[5];
        if (rect_width <= 0 || rect_height <= 0 ||
            x + rect_width > width_ || y + rect_height > height_ ||
            radius > std::min(rect_width, rect_height) / 2) {
            return false;
        }
        if (raster_ != nullptr) {
            raster_->rounded_rect(
                x,
                y,
                rect_width,
                rect_height,
                radius,
                palette_.colors[values[0]]);
        }
        return true;
    }

    bool circle() {
        std::array<std::int32_t, 4> values{};
        if (!numbers(values) || !end_of_command() ||
            !color_index(values[0])) {
            return false;
        }
        const auto center_x = values[1];
        const auto center_y = values[2];
        const auto radius = values[3];
        if (radius <= 0 || center_x < radius || center_y < radius ||
            center_x + radius > width_ ||
            center_y + radius > height_) {
            return false;
        }
        if (raster_ != nullptr) {
            raster_->circle(
                center_x,
                center_y,
                radius,
                palette_.colors[values[0]]);
        }
        return true;
    }

    bool line() {
        std::array<std::int32_t, 6> values{};
        if (!numbers(values) || !end_of_command() ||
            !color_index(values[0])) {
            return false;
        }
        if (values[1] >= width_ || values[3] >= width_ ||
            values[2] >= height_ || values[4] >= height_ ||
            values[5] < 1 || values[5] > 16) {
            return false;
        }
        if (raster_ != nullptr) {
            raster_->line(
                values[1],
                values[2],
                values[3],
                values[4],
                values[5],
                palette_.colors[values[0]]);
        }
        return true;
    }

    bool tile_map() {
        std::array<std::int32_t, 7> values{};
        if (!numbers(values) || *cursor_++ != ',') return false;
        const auto inset = values[0];
        const auto x = values[1];
        const auto y = values[2];
        const auto cell_width = values[3];
        const auto cell_height = values[4];
        const auto columns = values[5];
        const auto rows = values[6];
        const auto count = columns * rows;
        if (cell_width <= 0 || cell_height <= 0 ||
            columns <= 0 || rows <= 0 ||
            count > static_cast<std::int32_t>(kMaximumTiles) ||
            inset * 2 >= std::min(cell_width, cell_height) ||
            x + cell_width * columns > width_ ||
            y + cell_height * rows > height_) {
            return false;
        }
        for (auto index = 0; index < count; ++index) {
            const auto character = *cursor_++;
            if (character < '0' || character > '7') return false;
            const auto color = character - '0';
            if (!color_index(color)) return false;
            if (raster_ == nullptr || color == 0) continue;
            const auto column = index % columns;
            const auto row = index / columns;
            const auto inner_width = cell_width - inset * 2;
            const auto inner_height = cell_height - inset * 2;
            raster_->rounded_rect(
                x + column * cell_width + inset,
                y + row * cell_height + inset,
                inner_width,
                inner_height,
                std::min<std::int32_t>(
                    3,
                    std::min(inner_width, inner_height) / 3),
                palette_.colors[color]);
        }
        return end_of_command();
    }

    template <std::size_t N>
    bool numbers(std::array<std::int32_t, N>& output) {
        for (std::size_t index = 0; index < N; ++index) {
            if (!number(output[index])) return false;
            if (index + 1 < N && *cursor_++ != ',') return false;
        }
        return true;
    }

    bool number(std::int32_t& output) {
        if (*cursor_ < '0' || *cursor_ > '9') return false;
        std::int32_t value = 0;
        while (*cursor_ >= '0' && *cursor_ <= '9') {
            value = value * 10 + (*cursor_++ - '0');
            if (value > kMaximumCanvasLogicalEdge) return false;
        }
        output = value;
        return true;
    }

    bool end_of_command() const {
        return *cursor_ == '\0' || *cursor_ == '|';
    }

    bool color_index(std::int32_t value) const {
        return value >= 0 &&
               value < static_cast<std::int32_t>(palette_.count);
    }

    const char* cursor_;
    const Palette& palette_;
    std::int32_t width_;
    std::int32_t height_;
    Raster* raster_;
};

std::int32_t hex(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool parse_palette(const char* encoded, Palette& output) {
    output = {};
    if (encoded == nullptr ||
        std::strlen(encoded) >
            static_cast<std::size_t>(kMaximumCanvasPaletteBytes)) {
        return false;
    }
    auto* cursor = encoded;
    while (*cursor != '\0') {
        if (output.count >= output.colors.size()) return false;
        std::uint32_t rgb = 0;
        for (std::size_t index = 0; index < 6; ++index) {
            const auto nibble = hex(*cursor++);
            if (nibble < 0) return false;
            rgb = (rgb << 4U) | static_cast<std::uint32_t>(nibble);
        }
        output.colors[output.count++] = lv_color_to_u16(
            lv_color_make(
                static_cast<std::uint8_t>(rgb >> 16U),
                static_cast<std::uint8_t>(rgb >> 8U),
                static_cast<std::uint8_t>(rgb)));
        if (*cursor == '\0') break;
        if (*cursor++ != ',') return false;
    }
    return output.count != 0;
}

bool valid_dimensions(std::int32_t width, std::int32_t height) {
    return width > 0 && height > 0 &&
           width <= kMaximumCanvasLogicalEdge &&
           height <= kMaximumCanvasLogicalEdge;
}

}  // namespace

bool validate_canvas_display_list(
    const char* display_list,
    const char* palette,
    std::int32_t logical_width,
    std::int32_t logical_height) {
    Palette decoded{};
    return valid_dimensions(logical_width, logical_height) &&
           parse_palette(palette, decoded) &&
           Parser(
               display_list,
               decoded,
               logical_width,
               logical_height,
               nullptr)
               .parse();
}

bool render_canvas_display_list(
    lv_obj_t* canvas,
    const char* display_list,
    const char* palette,
    std::int32_t logical_width,
    std::int32_t logical_height) {
    if (canvas == nullptr ||
        !lv_obj_check_type(canvas, &lv_canvas_class) ||
        !valid_dimensions(logical_width, logical_height)) {
        return false;
    }
    Palette decoded{};
    if (!parse_palette(palette, decoded)) return false;
    const auto width =
        dp_edge_to_px(logical_width, watch_square_192.density_q8_8);
    const auto height =
        dp_edge_to_px(logical_height, watch_square_192.density_q8_8);
    if (width <= 0 || height <= 0 ||
        width > kMaximumPhysicalEdge ||
        height > kMaximumPhysicalEdge) {
        return false;
    }
    auto* pixels = canvas_pixels();
    if (pixels == nullptr) return false;
    Raster raster{pixels, width, height};
    if (!Parser(
             display_list,
             decoded,
             logical_width,
             logical_height,
             &raster)
             .parse()) {
        return false;
    }
    lv_canvas_set_buffer(
        canvas,
        pixels,
        width,
        height,
        LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, width, height);
    lv_obj_invalidate(canvas);
    return true;
}

}  // namespace m3e::appspec
