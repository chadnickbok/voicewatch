#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"
#include "m3e/theme/resolved_theme.hpp"

namespace m3e {

enum class StyleRole : std::uint8_t {
    background,
    surface_low,
    surface,
    surface_high,
    primary,
    secondary,
    tertiary,
    error,
    outline,
    text_on_surface,
    text_muted,
    text_on_primary,
    text_on_secondary,
    text_on_tertiary,
    pressed,
    disabled,
    count,
};

class StyleRegistry {
 public:
    StyleRegistry();
    ~StyleRegistry();

    StyleRegistry(const StyleRegistry&) = delete;
    StyleRegistry& operator=(const StyleRegistry&) = delete;

    bool initialize(const ResolvedTheme& theme);
    bool apply_theme(const ResolvedTheme& theme);
    lv_style_t* get(StyleRole role);
    const ResolvedTheme& theme() const;
    std::uint32_t generation() const;
    bool initialized() const;

 private:
    std::array<lv_style_t, static_cast<std::size_t>(StyleRole::count)> styles_{};
    ResolvedTheme theme_{};
    std::uint32_t generation_ = 0;
    bool initialized_ = false;
};

}  // namespace m3e
