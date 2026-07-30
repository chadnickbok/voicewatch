#include "m3e/theme/style_registry.hpp"

#include "m3e/foundation/semantic_tokens.hpp"
#include "m3e/generated/core_tokens.hpp"

namespace m3e {
namespace {

using generated::ColorRole;

lv_color_t lv_color(const ResolvedTheme& theme, ColorRole role) {
    const auto value = theme.color.get(role).rgb888;
    return lv_color_make(value.red, value.green, value.blue);
}

}  // namespace

StyleRegistry::StyleRegistry() = default;

StyleRegistry::~StyleRegistry() {
    if (!initialized_) {
        return;
    }
    for (auto& style : styles_) {
        lv_style_reset(&style);
    }
}

bool StyleRegistry::initialize(const ResolvedTheme& theme) {
    if (!initialized_) {
        for (auto& style : styles_) {
            lv_style_init(&style);
        }
        initialized_ = true;
    }
    return apply_theme(theme);
}

bool StyleRegistry::apply_theme(const ResolvedTheme& theme) {
    if (!initialized_) {
        return initialize(theme);
    }
    if (!validate_resolved_theme(theme).valid()) {
        return false;
    }
    theme_ = theme;
    auto surface = [&](StyleRole style, ColorRole color, std::int32_t radius) {
        auto* target = get(style);
        lv_style_set_bg_color(target, lv_color(theme_, color));
        lv_style_set_bg_opa(target, LV_OPA_COVER);
        lv_style_set_radius(target, radius);
        lv_style_set_border_width(target, 0);
    };
    surface(StyleRole::background, ColorRole::background, 0);
    surface(StyleRole::surface_low, ColorRole::surface_container_low, 18);
    surface(StyleRole::surface, ColorRole::surface_container, 18);
    surface(StyleRole::surface_high, ColorRole::surface_container_high, 18);
    surface(StyleRole::primary, ColorRole::primary, LV_RADIUS_CIRCLE);
    surface(StyleRole::secondary, ColorRole::secondary, LV_RADIUS_CIRCLE);
    surface(StyleRole::tertiary, ColorRole::tertiary, LV_RADIUS_CIRCLE);
    surface(StyleRole::error, ColorRole::error, LV_RADIUS_CIRCLE);

    auto* outline = get(StyleRole::outline);
    lv_style_set_bg_opa(outline, LV_OPA_TRANSP);
    lv_style_set_border_width(outline, 1);
    lv_style_set_border_color(outline, lv_color(theme_, ColorRole::outline));
    lv_style_set_radius(outline, LV_RADIUS_CIRCLE);

    auto text = [&](StyleRole style, ColorRole color) {
        lv_style_set_text_color(get(style), lv_color(theme_, color));
    };
    text(StyleRole::text_on_surface, ColorRole::on_surface);
    text(StyleRole::text_muted, ColorRole::on_surface_variant);
    text(StyleRole::text_on_primary, ColorRole::on_primary);
    text(StyleRole::text_on_secondary, ColorRole::on_secondary);
    text(StyleRole::text_on_tertiary, ColorRole::on_tertiary);

    auto* pressed = get(StyleRole::pressed);
    lv_style_set_transform_scale(pressed, 248);
    lv_style_set_opa(pressed, LV_OPA_90);
    auto* disabled = get(StyleRole::disabled);
    lv_style_set_opa(disabled, 97);

    ++generation_;
    for (auto& style : styles_) {
        lv_obj_report_style_change(&style);
    }
    return true;
}

lv_style_t* StyleRegistry::get(StyleRole role) {
    return &styles_[static_cast<std::size_t>(role)];
}

const ResolvedTheme& StyleRegistry::theme() const {
    return theme_;
}

std::uint32_t StyleRegistry::generation() const {
    return generation_;
}

bool StyleRegistry::initialized() const {
    return initialized_;
}

}  // namespace m3e
