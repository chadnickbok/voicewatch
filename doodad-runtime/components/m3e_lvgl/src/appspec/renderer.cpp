#include "m3e/appspec/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "m3e/components/components.hpp"
#include "m3e/assets/image_assets.hpp"
#include "m3e/assets/weather_fonts.hpp"
#include "m3e/assets/weather_icon_assets.hpp"
#include "m3e/appspec/canvas_display_list.hpp"
#include "m3e/foundation/display_profile.hpp"
#include "m3e/foundation/semantic_tokens.hpp"
#include "m3e/generated/weather_icons.hpp"
#include "m3e/generated/weather_tokens.hpp"

LV_FONT_DECLARE(m3e_calculator_font_20);
LV_FONT_DECLARE(m3e_calculator_result_font_40);
LV_FONT_DECLARE(m3e_timer_font_55);
LV_FONT_DECLARE(m3e_timer_value_font_28);
LV_FONT_DECLARE(m3e_weather_font_55);
LV_FONT_DECLARE(m3e_nutrition_font_32);
LV_FONT_DECLARE(m3e_live_action_font_32);

namespace m3e {
namespace {
std::uint16_t g_weather_font_scale_milli = 1000;
}

void set_weather_font_scale_milli(std::uint16_t scale_milli) {
    g_weather_font_scale_milli = scale_milli == 1300 ? 1300 : 1000;
}

std::uint16_t weather_font_scale_milli() {
    return g_weather_font_scale_milli;
}
}  // namespace m3e

namespace m3e::appspec {
namespace {

struct CalculatorKeyColors {
    lv_color_t container;
    lv_color_t content;
};

std::int32_t px(std::int32_t dp) {
    return dp_edge_to_px(dp, watch_square_192.density_q8_8);
}

std::int32_t px_tenths(std::int32_t dp_tenths) {
    constexpr std::int64_t kDenominator = 10 * 256;
    const auto scaled =
        static_cast<std::int64_t>(dp_tenths) *
        static_cast<std::int64_t>(watch_square_192.density_q8_8);
    return static_cast<std::int32_t>(
        (scaled + kDenominator / 2) / kDenominator);
}

bool large_weather_text() {
    return weather_font_scale_milli() == 1300;
}

std::int32_t gap_px(std::uint8_t gap) {
    const auto role =
        gap == 0 ? SpacingRole::none :
        gap == 1 ? SpacingRole::xs :
        gap == 2 ? SpacingRole::sm :
        gap == 3 ? SpacingRole::md : SpacingRole::lg;
    return px(spacing_dp(role));
}

lv_flex_align_t align(std::uint8_t value) {
    switch (value) {
        case 0: return LV_FLEX_ALIGN_START;
        case 2: return LV_FLEX_ALIGN_END;
        case 3: return LV_FLEX_ALIGN_SPACE_EVENLY;
        default: return LV_FLEX_ALIGN_CENTER;
    }
}

generated::TypographyRole typography(std::uint8_t value) {
    using generated::TypographyRole;
    switch (value) {
        case 0: return TypographyRole::display_medium;
        case 1: return TypographyRole::title_medium;
        case 2: return TypographyRole::label_medium;
        case 4: return TypographyRole::numeral_large;
        case 5: return TypographyRole::body_extra_small;
        default: return TypographyRole::body_medium;
    }
}

bool is_keypad_document(const WireDocument& document) {
    return std::any_of(
        document.nodes.begin() + 1,
        document.nodes.begin() + document.node_count,
        [](const WireNode& node) {
            return node.kind == ComponentKind::keypad;
        });
}

bool is_countdown_document(const WireDocument& document) {
    if (document.node_count != 5 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    bool progress = false;
    bool numeral = false;
    bool stepper = false;
    bool button = false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        switch (node.kind) {
            case ComponentKind::progress:
                progress = node.variant == 1;
                break;
            case ComponentKind::text:
                numeral = node.variant == 4;
                break;
            case ComponentKind::stepper:
                stepper = true;
                break;
            case ComponentKind::button:
                button = true;
                break;
            default:
                return false;
        }
    }
    return progress && numeral && stepper && button;
}

bool is_weather_hero_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 6) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 4 && cards == 1 && buttons == 1;
}

bool is_notification_stack_document(const WireDocument& document) {
    if (document.node_count < 5 ||
        document.nodes[0].child_count != 1 ||
        document.nodes[1].kind != ComponentKind::scroll ||
        document.nodes[1].parent_index != 0) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 2; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 1) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return document.nodes[1].child_count ==
            texts + cards + buttons &&
        texts == 1 &&
        cards >= 1 && cards <= 2 &&
        (buttons == 1 || buttons == 3);
}

bool is_calendar_agenda_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 1 ||
        document.nodes[1].kind != ComponentKind::scroll ||
        document.nodes[1].parent_index != 0 ||
        document.nodes[1].child_count != 1 ||
        document.nodes[2].kind != ComponentKind::column ||
        document.nodes[2].parent_index != 1 ||
        document.nodes[2].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 3; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 2) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 1 &&
        ((cards == 2 && buttons == 1) ||
         (cards == 1 && buttons == 2));
}

bool is_task_list_document(const WireDocument& document) {
    if (document.node_count < 5 ||
        document.nodes[0].child_count != 1 ||
        document.nodes[1].kind != ComponentKind::scroll ||
        document.nodes[1].parent_index != 0) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t toggles = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 2; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 1) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::toggle) {
            ++toggles;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return document.nodes[1].child_count ==
            texts + toggles + buttons &&
        texts == 1 &&
        ((toggles == 2 && buttons == 1) ||
         (toggles == 3 && buttons == 0));
}

bool is_workout_set_document(const WireDocument& document) {
    if (document.node_count != 5 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t steppers = 0;
    std::size_t live_cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::stepper) {
            ++steppers;
        } else if (node.kind == ComponentKind::live_card) {
            ++live_cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 1 && steppers == 1 &&
        live_cards == 1 && buttons == 1;
}

bool is_workout_rest_document(const WireDocument& document) {
    if (document.node_count != 6 ||
        document.nodes[0].child_count != 5) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t live_cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::live_card) {
            ++live_cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 2 && live_cards == 1 && buttons == 2;
}

bool is_workout_summary_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t root_texts = 0;
    std::size_t rows = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    std::size_t row_texts = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++root_texts;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                if (node.child_count != 2) return false;
            } else if (node.kind == ComponentKind::card) {
                ++cards;
            } else if (node.kind == ComponentKind::button) {
                ++buttons;
            } else {
                return false;
            }
        } else if (node.parent_index == 2 &&
                   node.kind == ComponentKind::text) {
            ++row_texts;
        } else {
            return false;
        }
    }
    return root_texts == 1 && rows == 1 && row_texts == 2 &&
        cards == 1 && buttons == 1;
}

bool is_nutrition_dashboard_document(
    const WireDocument& document) {
    if (document.node_count != 6 ||
        document.nodes[0].child_count != 5) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t progress = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::progress) {
            ++progress;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 2 && progress == 1 &&
        cards == 1 && buttons == 1;
}

bool is_nutrition_quick_add_document(
    const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t steppers = 0;
    std::size_t cards = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::stepper) {
                ++steppers;
            } else if (node.kind == ComponentKind::card) {
                ++cards;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return texts == 1 && steppers == 1 &&
        cards == 1 && rows == 1 && buttons == 2;
}

bool is_nutrition_review_document(
    const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::card) {
                ++cards;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return texts == 2 && cards == 1 &&
        rows == 1 && buttons == 2;
}

bool is_voice_ready_document(const WireDocument& document) {
    if (document.node_count != 4 ||
        document.nodes[0].child_count != 3) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t orbs = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::voice_orb) {
            ++orbs;
        } else {
            return false;
        }
    }
    return texts == 1 && cards == 1 && orbs == 1;
}

bool is_live_action_detail_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t live_cards = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::live_card) {
                ++live_cards;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return texts == 2 && live_cards == 1 &&
        rows == 1 && buttons == 2;
}

bool is_media_player_document(const WireDocument& document) {
    if (document.node_count != 8 ||
        document.nodes[0].child_count != 5) {
        return false;
    }
    std::size_t images = 0;
    std::size_t texts = 0;
    std::size_t progress = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::image) {
                ++images;
            } else if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::progress) {
                ++progress;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return images == 1 && texts == 2 && progress == 1 &&
        rows == 1 && buttons == 2;
}

bool is_camera_remote_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t images = 0;
    std::size_t texts = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::image) {
                ++images;
            } else if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return images == 1 && texts == 2 &&
        rows == 1 && buttons == 2;
}

bool is_wallet_qr_document(const WireDocument& document) {
    if (document.node_count != 6 ||
        document.nodes[0].child_count != 3) {
        return false;
    }
    std::size_t images = 0;
    std::size_t texts = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::image) {
                ++images;
            } else if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return images == 1 && texts == 1 &&
        rows == 1 && buttons == 2;
}

bool is_canvas_game_document(const WireDocument& document) {
    if (document.node_count != 5 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t canvases = 0;
    std::size_t texts = 0;
    std::size_t keypads = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::canvas) {
            ++canvases;
        } else if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::keypad) {
            ++keypads;
            if (node.key_count != 3 || node.key_columns != 3) {
                return false;
            }
        } else {
            return false;
        }
    }
    return canvases == 1 && texts == 2 && keypads == 1;
}

bool uses_weather_components(const WireDocument& document) {
    return std::any_of(
        document.nodes.begin() + 1,
        document.nodes.begin() + document.node_count,
        [](const WireNode& node) {
            return node.kind == ComponentKind::icon ||
                node.kind == ComponentKind::surface ||
                node.kind == ComponentKind::chart ||
                node.kind == ComponentKind::pager;
        });
}

bool has_node_id(
    const WireDocument& document,
    const char* expected) {
    return std::any_of(
        document.nodes.begin() + 1,
        document.nodes.begin() + document.node_count,
        [&document, expected](const WireNode& node) {
            return std::strcmp(
                       document.string_at(node.id_offset), expected) == 0;
        });
}

bool is_weather_current_document(const WireDocument& document) {
    return std::strcmp(document.string_at(document.app_id_offset), "weather") == 0 &&
        (has_node_id(document, "weather.current") ||
         has_node_id(document, "weather.hourly") ||
         has_node_id(document, "weather.daily-page") ||
         has_node_id(document, "weather.details-page") ||
         has_node_id(document, "weather.rain-page"));
}

std::size_t count_kind(
    const WireDocument& document,
    ComponentKind kind) {
    return static_cast<std::size_t>(std::count_if(
        document.nodes.begin() + 1,
        document.nodes.begin() + document.node_count,
        [kind](const WireNode& node) {
            return node.kind == kind;
        }));
}

std::size_t kind_ordinal(
    const WireDocument& document,
    std::size_t node_index,
    ComponentKind kind) {
    return static_cast<std::size_t>(std::count_if(
        document.nodes.begin() + 1,
        document.nodes.begin() + node_index,
        [kind](const WireNode& node) {
            return node.kind == kind;
        }));
}

const char* calculator_glyph(const char* key) {
    if (std::strcmp(key, "+/-") == 0) return "±";
    if (std::strcmp(key, "/") == 0) return "÷";
    if (std::strcmp(key, "*") == 0) return "×";
    if (std::strcmp(key, "<-") == 0) return "⌫";
    return key;
}

CalculatorKeyColors calculator_key_colors(
    bool is_operator,
    bool is_utility) {
    // Keep the production renderer on the exact violet-dark roles used by
    // ReferenceTheme. LVGL quantizes these colors to RGB565 on hardware.
    if (is_operator) {
        return {
            lv_color_make(0xD8, 0xB9, 0xFF),
            lv_color_make(0x35, 0x11, 0x51),
        };
    }
    if (is_utility) {
        return {
            lv_color_make(0xCF, 0xC0, 0xDA),
            lv_color_make(0x34, 0x2B, 0x3D),
        };
    }
    return {
        lv_color_make(0x49, 0x44, 0x53),
        lv_color_make(0xF6, 0xED, 0xFF),
    };
}

Tone tone(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(Tone::error)
        ? static_cast<Tone>(value)
        : Tone::primary;
}

ComponentSize component_size(std::uint8_t value) {
    switch (value) {
        case 0: return ComponentSize::compact;
        case 2: return ComponentSize::large;
        default: return ComponentSize::normal;
    }
}

bool weather_icon(const char* name, generated::WeatherIcon& output) {
    if (name == nullptr) return false;
    for (std::size_t index = 0;
         index < generated::kWeatherIconWireNames.size(); ++index) {
        if (generated::kWeatherIconWireNames[index] == name) {
            output = static_cast<generated::WeatherIcon>(index);
            return true;
        }
    }
    return false;
}

lv_color_t weather_color(generated::WeatherColorRole role) {
    const auto& color = generated::kWeatherColors[
        static_cast<std::size_t>(role)].rgb888;
    return lv_color_make(color.red, color.green, color.blue);
}

lv_obj_t* create_weather_icon(
    lv_obj_t* parent,
    generated::WeatherIcon icon,
    std::int32_t size,
    bool button_content = false) {
    const auto* asset = weather_icon_asset(icon, size);
    if (asset == nullptr) return nullptr;
    auto* object = lv_image_create(parent);
    ComponentFactory::reset(object);
    lv_image_set_src(object, asset);
    lv_obj_set_size(object, size, size);
    const auto source_width = static_cast<std::int32_t>(asset->header.w);
    if (source_width > 0 && source_width != size) {
        lv_image_set_scale(
            object,
            static_cast<std::uint32_t>(
                (size * 256 + source_width / 2) / source_width));
    }
    const auto& spec = generated::kWeatherIcons[
        static_cast<std::size_t>(icon)];
    if (spec.render == generated::WeatherIconRender::mask) {
        lv_obj_set_style_image_recolor(
            object,
            button_content
                ? weather_color(generated::WeatherColorRole::on_primary)
                : weather_color(spec.tint_role),
            0);
        lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    }
    return object;
}

void floating_box(
    lv_obj_t* object,
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height) {
    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(object, px(x), px(y));
    lv_obj_set_size(object, px(width), px(height));
}


void floating_box_tenths(
    lv_obj_t* object,
    std::int32_t x_dp_tenths,
    std::int32_t y_dp_tenths,
    std::int32_t width_dp_tenths,
    std::int32_t height_dp_tenths) {
    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(
        object,
        px_tenths(x_dp_tenths),
        px_tenths(y_dp_tenths));
    lv_obj_set_size(
        object,
        px_tenths(width_dp_tenths),
        px_tenths(height_dp_tenths));
}

void draw_weather_cut_corner_surface(lv_event_t* event) {
    auto* object = static_cast<lv_obj_t*>(
        lv_event_get_current_target(event));
    auto* layer = lv_event_get_layer(event);
    if (object == nullptr || layer == nullptr) return;

    lv_area_t bounds{};
    lv_obj_get_coords(object, &bounds);
    const auto cut = px(10);
    const auto color =
        weather_color(generated::WeatherColorRole::rain);

    lv_draw_fill_dsc_t fill{};
    lv_draw_fill_dsc_init(&fill);
    fill.color = color;
    fill.opa = LV_OPA_COVER;
    lv_area_t vertical{
        bounds.x1 + cut, bounds.y1,
        bounds.x2 - cut, bounds.y2,
    };
    lv_area_t horizontal{
        bounds.x1, bounds.y1 + cut,
        bounds.x2, bounds.y2 - cut,
    };
    lv_draw_fill(layer, &fill, &vertical);
    lv_draw_fill(layer, &fill, &horizontal);

    lv_draw_triangle_dsc_t triangle{};
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = color;
    triangle.opa = LV_OPA_COVER;
    const auto draw_triangle =
        [layer, &triangle](
            lv_point_precise_t first,
            lv_point_precise_t second,
            lv_point_precise_t third) {
            triangle.p[0] = first;
            triangle.p[1] = second;
            triangle.p[2] = third;
            lv_draw_triangle(layer, &triangle);
        };
    draw_triangle(
        {bounds.x1 + cut, bounds.y1},
        {bounds.x1 + cut, bounds.y1 + cut},
        {bounds.x1, bounds.y1 + cut});
    draw_triangle(
        {bounds.x2 - cut, bounds.y1},
        {bounds.x2, bounds.y1 + cut},
        {bounds.x2 - cut, bounds.y1 + cut});
    draw_triangle(
        {bounds.x1, bounds.y2 - cut},
        {bounds.x1 + cut, bounds.y2 - cut},
        {bounds.x1 + cut, bounds.y2});
    draw_triangle(
        {bounds.x2 - cut, bounds.y2 - cut},
        {bounds.x2, bounds.y2 - cut},
        {bounds.x2 - cut, bounds.y2});
}

void configure_weather_current_node(
    const WireDocument& document,
    const WireNode& node,
    lv_obj_t* object,
    lv_obj_t* parent) {
    const auto* id = document.string_at(node.id_offset);
    using generated::WeatherColorRole;
    using generated::WeatherTypographyRole;
    if (std::strcmp(id, "weather.pages") == 0 ||
        std::strcmp(id, "weather.current") == 0) {
        floating_box(object, 0, 0, 192, 192);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }
    if (std::strcmp(id, "weather.location-row") == 0) {
        floating_box(object, 8, 6, 128, 16);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(4), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.location-icon") == 0) {
        lv_obj_set_size(object, px(10), px(10));
        lv_image_set_scale(object, 139);
        return;
    }
    if (std::strcmp(id, "weather.location") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        return;
    }
    if (std::strcmp(id, "weather.status-chip") == 0) {
        floating_box(object, 149, 6, 38, 18);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::secondary_container), 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        return;
    }
    if (std::strcmp(id, "weather.status-row") == 0) {
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.status") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        auto* dot = lv_obj_create(parent);
        ComponentFactory::reset(dot);
        lv_obj_set_size(dot, px(5), px(5));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            dot, weather_color(WeatherColorRole::fresh), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_move_to_index(dot, 0);
        return;
    }
    if (std::strcmp(id, "weather.hero-row") == 0) {
        floating_box(object, 0, 0, 192, 100);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        return;
    }
    if (std::strcmp(id, "weather.summary") == 0) {
        floating_box(object, 8, 22, 107, 61);
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::hero), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        // Compose centers the 68 px face inside its 73 px line box and the
        // Android font metrics add further top leading. LVGL otherwise draws
        // the numeral almost against the box edge.
        lv_obj_set_style_pad_top(object, px(9), 0);
        return;
    }
    if (std::strcmp(id, "weather.condition-icon") == 0) {
        floating_box(object, 110, 22, 72, 60);
        lv_image_set_scale(object, 440);
        return;
    }
    if (std::strcmp(id, "weather.symbol") == 0) {
        floating_box(object, 10, 81, 173, 18);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::row), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_top(object, px(2), 0);
        return;
    }
    if (std::strcmp(id, "weather.high-low") == 0) {
        floating_box(object, 8, 101, 176, 24);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(5), 0);
        return;
    }
    if (std::strcmp(id, "weather.high-pill") == 0 ||
        std::strcmp(id, "weather.low-pill") == 0) {
        lv_obj_set_height(object, LV_PCT(100));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        return;
    }
    if (std::strcmp(id, "weather.high-row") == 0 ||
        std::strcmp(id, "weather.low-row") == 0) {
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        return;
    }
    if (std::strcmp(id, "weather.high-icon") == 0 ||
        std::strcmp(id, "weather.low-icon") == 0) {
        lv_obj_set_size(object, px(14), px(14));
        lv_image_set_scale(object, 192);
        return;
    }
    if (std::strcmp(id, "weather.high") == 0 ||
        std::strcmp(id, "weather.low") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_background), 0);
        return;
    }
    if (std::strcmp(id, "weather.feels-pill") == 0) {
        floating_box(object, 8, 127, 176, 19);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::surface_low), 0);
        return;
    }
    if (std::strcmp(id, "weather.feels-row") == 0) {
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_hor(object, px(8), 0);
        lv_obj_set_style_pad_ver(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(6), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.feels-icon") == 0) {
        lv_obj_set_size(object, px(13), px(13));
        lv_image_set_scale(object, 171);
        return;
    }
    if (std::strcmp(id, "weather.feels-label") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(object, 1);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        return;
    }
    if (std::strcmp(id, "weather.feels") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_background), 0);
        return;
    }
    if (std::strcmp(id, "weather.primary") == 0) {
        floating_box(object, 0, 144, 192, 48);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        if (lv_obj_get_child_count(object) > 1) {
            auto* image = lv_obj_get_child(object, 0);
            auto* label = lv_obj_get_child(object, 0);
            if (lv_obj_check_type(image, &lv_image_class) &&
                lv_obj_get_child_count(object) > 1) {
                label = lv_obj_get_child(object, 1);
            }
            auto* visual = lv_obj_create(object);
            ComponentFactory::reset(visual);
            lv_obj_add_flag(visual, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(visual, px(179), px(29));
            lv_obj_align(visual, LV_ALIGN_CENTER, 0, px(6));
            lv_obj_set_style_radius(visual, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(
                visual, weather_color(WeatherColorRole::primary), 0);
            lv_obj_set_style_bg_opa(visual, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(visual, px(14), 0);
            lv_obj_set_style_pad_ver(visual, 0, 0);
            lv_obj_set_flex_flow(visual, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                visual,
                LV_FLEX_ALIGN_SPACE_BETWEEN,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            if (label != nullptr &&
                lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_parent(label, visual);
                lv_obj_set_width(label, LV_SIZE_CONTENT);
                lv_obj_set_style_text_font(
                    label, weather_font(WeatherTypographyRole::row), 0);
            }
            if (image != nullptr &&
                lv_obj_check_type(image, &lv_image_class)) {
                lv_obj_set_parent(image, visual);
                lv_obj_set_size(image, px(16), px(16));
                lv_image_set_scale(image, 213);
            }
        }
        return;
    }
    if (std::strcmp(id, "weather.hourly") == 0 ||
        std::strcmp(id, "weather.daily-page") == 0) {
        floating_box(object, 0, 0, 192, 192);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }
    if (std::strcmp(id, "weather.hourly-summary") == 0) {
        floating_box(object, 6, 4, 123, 34);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(6), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.hourly-condition-icon") == 0) {
        lv_obj_set_size(object, px(32), px(28));
        lv_image_set_scale(object, 205);
        return;
    }
    if (std::strcmp(id, "weather.hourly-summary-copy") == 0) {
        lv_obj_set_height(object, LV_PCT(100));
        lv_obj_set_flex_grow(object, 1);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_START);
        return;
    }
    if (std::strcmp(id, "weather.hourly-now") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::row), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_background), 0);
        lv_obj_set_style_translate_y(object, px(2), 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-condition") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-status-chip") == 0) {
        floating_box(object, 149, 6, 38, 18);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::secondary_container), 0);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        auto* dot = lv_obj_create(object);
        ComponentFactory::reset(dot);
        lv_obj_set_size(dot, px(5), px(5));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            dot, weather_color(WeatherColorRole::fresh), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_move_to_index(dot, 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-status") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-chart-card") == 0) {
        floating_box(object, 6, 42, 179, 46);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, px(14), 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::surface), 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-chart-heading") == 0) {
        floating_box(object, 6, 3, 166, 14);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.hourly-rain-icon") == 0) {
        lv_obj_set_size(object, px(11), px(11));
        lv_image_set_scale(object, 149);
        return;
    }
    if (std::strcmp(id, "weather.hourly-rain-label") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(object, 1);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-rain-value") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-chart") == 0) {
        floating_box(object, 6, 17, 166, 19);
        lv_chart_set_range(object, LV_CHART_AXIS_PRIMARY_Y, -60, 100);
        lv_obj_set_style_line_width(object, px(2), LV_PART_ITEMS);
        lv_obj_set_style_width(object, px(4), LV_PART_INDICATOR);
        lv_obj_set_style_height(object, px(4), LV_PART_INDICATOR);
        lv_chart_refresh(object);
        return;
    }
    if (std::strcmp(id, "weather.hourly-times") == 0) {
        floating_box(object, 6, 37, 166, 8);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_SPACE_BETWEEN,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strncmp(id, "weather.hourly-time-", 20) == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::outline_variant), 0);
        return;
    }
    if (std::strcmp(id, "weather.hourly-tiles") == 0) {
        floating_box(object, 6, 93, 179, 46);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(5), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    const bool hourly_tile =
        std::strcmp(id, "weather.hour-now-tile") == 0 ||
        std::strcmp(id, "weather.hour-10-tile") == 0 ||
        std::strcmp(id, "weather.hour-11-tile") == 0 ||
        std::strcmp(id, "weather.hour-12-tile") == 0;
    if (hourly_tile) {
        lv_obj_set_height(object, LV_PCT(100));
        lv_obj_set_flex_grow(object, 1);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, px(14), 0);
        lv_obj_set_style_bg_color(
            object,
            std::strcmp(id, "weather.hour-now-tile") == 0
                ? weather_color(WeatherColorRole::primary_container)
                : weather_color(WeatherColorRole::surface_low),
            0);
        return;
    }
    const bool hourly_tile_column =
        std::strcmp(id, "weather.hour-now") == 0 ||
        std::strcmp(id, "weather.hour-10") == 0 ||
        std::strcmp(id, "weather.hour-11") == 0 ||
        std::strcmp(id, "weather.hour-12") == 0;
    if (hourly_tile_column) {
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strstr(id, "weather.hour-") == id &&
        std::strstr(id, "-label") != nullptr) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strstr(id, "weather.hour-") == id &&
        std::strstr(id, "-icon") != nullptr) {
        lv_obj_set_size(object, px(19), px(16));
        // Compose applies a 2.05x optical transform to the Meteocons glyph
        // while retaining the compact layout box. LVGL needs the equivalent
        // draw scale because the source artwork contains generous whitespace.
        lv_image_set_scale(object, 350);
        return;
    }
    if (std::strstr(id, "weather.hour-") == id &&
        std::strstr(id, "-temp") != nullptr) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::row), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strcmp(id, "weather.daily-action") == 0) {
        floating_box(object, 0, 144, 192, 48);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        if (lv_obj_get_child_count(object) > 1) {
            auto* image = lv_obj_get_child(object, 0);
            auto* label = lv_obj_get_child(object, 0);
            if (lv_obj_check_type(image, &lv_image_class)) {
                label = lv_obj_get_child(object, 1);
            }
            auto* visual = lv_obj_create(object);
            ComponentFactory::reset(visual);
            lv_obj_add_flag(visual, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(visual, px(179), px(29));
            lv_obj_align(visual, LV_ALIGN_CENTER, 0, px(6));
            lv_obj_set_style_radius(visual, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(
                visual, weather_color(WeatherColorRole::secondary_container), 0);
            lv_obj_set_style_bg_opa(visual, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(visual, px(14), 0);
            lv_obj_set_style_pad_ver(visual, 0, 0);
            lv_obj_set_flex_flow(visual, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                visual,
                LV_FLEX_ALIGN_SPACE_BETWEEN,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            if (label != nullptr && lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_parent(label, visual);
                lv_obj_set_width(label, LV_SIZE_CONTENT);
                lv_obj_set_style_text_font(
                    label, weather_font(WeatherTypographyRole::row), 0);
                lv_obj_set_style_text_color(
                    label, weather_color(WeatherColorRole::on_secondary_container), 0);
            }
            if (image != nullptr && lv_obj_check_type(image, &lv_image_class)) {
                lv_obj_set_parent(image, visual);
                lv_obj_set_size(image, px(16), px(16));
                lv_image_set_scale(image, 213);
                lv_obj_set_style_image_recolor(
                    image, weather_color(WeatherColorRole::on_secondary_container), 0);
                lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
            }
        }
        return;
    }
    if (std::strcmp(id, "weather.daily-title") == 0) {
        floating_box(object, 8, 8, 176, 32);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::headline), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    if (std::strcmp(id, "weather.daily-location-row") == 0) {
        floating_box(object, 8, 6, 128, 16);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(4), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.daily-location-icon") == 0) {
        lv_obj_set_size(object, px(10), px(10));
        lv_image_set_scale(object, 139);
        return;
    }
    if (std::strcmp(id, "weather.daily-location") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strcmp(id, "weather.daily-status-chip") == 0) {
        floating_box(object, 149, 6, 38, 18);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::secondary_container), 0);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        auto* dot = lv_obj_create(object);
        ComponentFactory::reset(dot);
        lv_obj_set_size(dot, px(5), px(5));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            dot, weather_color(WeatherColorRole::fresh), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_move_to_index(dot, 0);
        return;
    }
    if (std::strcmp(id, "weather.daily-status") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        return;
    }
    if (std::strcmp(id, "weather.daily-list") == 0) {
        floating_box(object, 6, 30, 179, 150);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(4), 0);
        return;
    }
    const bool daily_tile =
        std::strcmp(id, "weather.day-today-tile") == 0 ||
        std::strcmp(id, "weather.day-mon-tile") == 0 ||
        std::strcmp(id, "weather.day-tue-tile") == 0 ||
        std::strcmp(id, "weather.day-wed-tile") == 0;
    if (daily_tile) {
        lv_obj_set_size(object, LV_PCT(100), px(34));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, px(14), 0);
        lv_obj_set_style_bg_color(
            object,
            std::strcmp(id, "weather.day-today-tile") == 0
                ? weather_color(WeatherColorRole::primary_container)
                : weather_color(WeatherColorRole::surface_low),
            0);
        return;
    }
    const bool daily_row =
        std::strcmp(id, "weather.day-today") == 0 ||
        std::strcmp(id, "weather.day-mon") == 0 ||
        std::strcmp(id, "weather.day-tue") == 0 ||
        std::strcmp(id, "weather.day-wed") == 0;
    if (daily_row) {
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_hor(object, px(10), 0);
        lv_obj_set_style_pad_ver(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strstr(id, "weather.day-") == id &&
        std::strstr(id, "-label") != nullptr) {
        if (large_weather_text()) {
            floating_box(object, 10, 8, 54, 18);
        } else {
            lv_obj_set_width(object, px(54));
        }
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strstr(id, "weather.day-") == id &&
        std::strstr(id, "-icon") != nullptr) {
        if (large_weather_text()) {
            floating_box(object, 62, 0, 41, 34);
        } else {
            lv_obj_set_size(object, px(37), px(24));
        }
        lv_image_set_scale(object, 190);
        if (!large_weather_text()) {
            lv_obj_set_style_translate_x(object, px(16), 0);
        }
        return;
    }
    if (std::strstr(id, "weather.day-") == id &&
        std::strstr(id, "-low") != nullptr) {
        if (large_weather_text()) {
            floating_box(object, 101, 8, 34, 18);
        } else {
            lv_obj_set_width(object, px(34));
        }
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::label), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface_variant), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strstr(id, "weather.day-") == id &&
        std::strstr(id, "-high") != nullptr) {
        if (large_weather_text()) {
            floating_box(object, 135, 6, 34, 24);
        } else {
            lv_obj_set_width(object, px(34));
        }
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::row), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strcmp(id, "weather.daily-dots") == 0) {
        floating_box(object, 78, 184, 35, 5);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strncmp(id, "weather.daily-dot-", 18) == 0) {
        const auto selected =
            std::strcmp(id, "weather.daily-dot-selected") == 0;
        lv_obj_set_size(object, px(selected ? 6 : 3), px(3));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            object,
            selected
                ? weather_color(WeatherColorRole::primary)
                : weather_color(WeatherColorRole::outline_variant),
            0);
        return;
    }
    if (std::strcmp(id, "weather.details-action") == 0) {
        floating_box(object, 0, 144, 192, 48);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(object, LV_OPA_TRANSP, 0);
        return;
    }
    if (std::strcmp(id, "weather.details-page") == 0) {
        floating_box(object, 0, 0, 192, 192);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }
    if (std::strcmp(id, "weather.details-summary") == 0) {
        floating_box(object, 6, 3, 128, 32);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(4), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.details-condition-icon") == 0) {
        lv_obj_set_size(object, px(29), px(24));
        lv_image_set_scale(object, 214);
        return;
    }
    if (std::strcmp(id, "weather.details-temperature") == 0) {
        lv_obj_set_width(object, px(38));
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::metric), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        lv_obj_set_style_translate_y(object, px(2), 0);
        return;
    }
    if (std::strcmp(id, "weather.details-condition") == 0) {
        if (large_weather_text()) {
            floating_box(object, 70, 9, 58, 14);
        } else {
            lv_obj_set_flex_grow(object, 1);
        }
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface_variant), 0);
        lv_obj_set_style_translate_y(object, px(1), 0);
        return;
    }
    if (std::strcmp(id, "weather.details-status-chip") == 0) {
        floating_box(object, 149, 6, 38, 18);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::secondary_container), 0);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        auto* dot = lv_obj_create(object);
        ComponentFactory::reset(dot);
        lv_obj_set_size(dot, px(5), px(5));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            dot, weather_color(WeatherColorRole::fresh), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_move_to_index(dot, 0);
        return;
    }
    if (std::strcmp(id, "weather.details-status") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        return;
    }
    if (std::strcmp(id, "weather.details-grid") == 0) {
        floating_box(object, 6, 38, 179, 147);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(6), 0);
        return;
    }
    if (std::strcmp(id, "weather.details-row-top") == 0 ||
        std::strcmp(id, "weather.details-row-bottom") == 0) {
        lv_obj_set_size(object, LV_PCT(100), px(70));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(6), 0);
        return;
    }
    const bool details_tile =
        std::strcmp(id, "weather.humidity-tile") == 0 ||
        std::strcmp(id, "weather.wind-tile") == 0 ||
        std::strcmp(id, "weather.uv-tile") == 0 ||
        std::strcmp(id, "weather.sunrise-tile") == 0;
    if (details_tile) {
        lv_obj_set_height(object, LV_PCT(100));
        lv_obj_set_flex_grow(object, 1);
        lv_obj_set_style_pad_all(object, 0, 0);
        const auto humidity = std::strcmp(id, "weather.humidity-tile") == 0;
        const auto wind = std::strcmp(id, "weather.wind-tile") == 0;
        const auto uv = std::strcmp(id, "weather.uv-tile") == 0;
        lv_obj_set_style_radius(
            object,
            px(uv ? 2 : (humidity ? 18 : (wind ? 22 : 18))),
            0);
        if (uv) {
            lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
            lv_obj_add_event_cb(
                object,
                draw_weather_cut_corner_surface,
                LV_EVENT_DRAW_MAIN,
                nullptr);
        } else {
            lv_obj_set_style_bg_color(
                object,
                wind
                    ? weather_color(WeatherColorRole::primary_container)
                    : weather_color(WeatherColorRole::surface_high),
                0);
        }
        return;
    }
    if (std::strcmp(id, "weather.humidity") == 0 ||
        std::strcmp(id, "weather.wind") == 0 ||
        std::strcmp(id, "weather.uv") == 0 ||
        std::strcmp(id, "weather.sunrise") == 0) {
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(object, 0, 0);
        return;
    }
    if (std::strstr(id, "weather.") == id &&
        std::strstr(id, "-icon") != nullptr &&
        (std::strstr(id, "humidity") != nullptr ||
         std::strstr(id, "wind") != nullptr ||
         std::strstr(id, "uv") != nullptr ||
         std::strstr(id, "sunrise") != nullptr)) {
        // The requested 32dp glyph resolves to the 64px source raster. Give
        // it the same 30dp optical box as Compose's 22.4dp + 1.35x transform,
        // then scale the source once to that box.
        floating_box(object, 3, 6, 30, 30);
        lv_image_set_scale(object, 152);
        return;
    }
    if (std::strstr(id, "weather.") == id &&
        std::strstr(id, "-label") != nullptr &&
        (std::strstr(id, "humidity") != nullptr ||
         std::strstr(id, "wind") != nullptr ||
         std::strstr(id, "uv") != nullptr ||
         std::strstr(id, "sunrise") != nullptr)) {
        floating_box(object, 33, 10, 48, 12);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_top(object, px(1), 0);
        if (std::strstr(id, "weather.uv") == id) {
            lv_obj_set_style_text_color(
                object, weather_color(WeatherColorRole::on_primary), 0);
        }
        return;
    }
    if (std::strstr(id, "weather.") == id &&
        std::strstr(id, "-value") != nullptr &&
        (std::strstr(id, "humidity") != nullptr ||
         std::strstr(id, "wind") != nullptr ||
         std::strstr(id, "uv") != nullptr ||
         std::strstr(id, "sunrise") != nullptr)) {
        const auto uv = std::strstr(id, "weather.uv") == id;
        const auto wind = std::strstr(id, "weather.wind") == id;
        const auto humidity = std::strstr(id, "weather.humidity") == id;
        const auto large = large_weather_text();
        const auto x =
            uv ? (large ? 12 : 20) : wind ? 20 : large ? 22 : 31;
        const auto width =
            uv ? (large ? 24 : 36) : wind ? 36 : large ? (humidity ? 60 : 64) : 51;
        floating_box(object, x, uv && large ? 32 : 35, width, 28);
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::metric), 0);
        lv_obj_set_style_text_color(
            object,
            uv
                ? weather_color(WeatherColorRole::on_primary)
                : weather_color(WeatherColorRole::on_surface),
            0);
        lv_obj_set_style_pad_top(object, px(2), 0);
        return;
    }
    if (std::strstr(id, "weather.") == id &&
        std::strstr(id, "-unit") != nullptr &&
        (std::strstr(id, "wind") != nullptr ||
         std::strstr(id, "uv") != nullptr)) {
        const auto uv = std::strstr(id, "weather.uv") == id;
        if (uv && large_weather_text()) {
            floating_box(object, 20, 56, 60, 12);
        } else {
            floating_box(
                object,
                50,
                uv ? 39 : 44,
                uv ? 34 : 29,
                12);
        }
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_align(
            object,
            uv && large_weather_text()
                ? LV_TEXT_ALIGN_RIGHT
                : LV_TEXT_ALIGN_LEFT,
            0);
        lv_obj_set_style_text_color(
            object,
            uv
                ? weather_color(WeatherColorRole::on_primary)
                : weather_color(WeatherColorRole::on_surface_variant),
            0);
        if (!(uv && large_weather_text())) {
            lv_obj_set_style_pad_top(object, px(2), 0);
        }
        return;
    }
    if (std::strcmp(id, "weather.details-dots") == 0) {
        floating_box(object, 78, 184, 35, 5);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strncmp(id, "weather.details-dot-", 20) == 0) {
        const auto selected =
            std::strcmp(id, "weather.details-dot-selected") == 0;
        lv_obj_set_size(object, px(selected ? 6 : 3), px(3));
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            object,
            selected
                ? weather_color(WeatherColorRole::primary)
                : weather_color(WeatherColorRole::outline_variant),
            0);
        return;
    }
    if (std::strcmp(id, "weather.rain-preview-action") == 0) {
        floating_box(object, 0, 144, 192, 48);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(object, LV_OPA_TRANSP, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-page") == 0) {
        floating_box(object, 0, 0, 192, 192);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }
    if (std::strcmp(id, "weather.rain-hero") == 0) {
        floating_box(object, 12, 4, 60, 56);
        lv_image_set_scale(object, 380);
        return;
    }
    if (std::strcmp(id, "weather.rain-headline") == 0) {
        floating_box_tenths(object, 824, 64, 1024, 560);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-title") == 0) {
        lv_obj_set_size(object, LV_SIZE_CONTENT, px_tenths(480));
        lv_obj_set_style_text_font(
            object,
            large_weather_text()
                ? &m3e_weather_metric_28
                : weather_font(WeatherTypographyRole::metric),
            0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        lv_obj_set_style_text_line_space(object, px(-2), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_top(object, px(5), 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-duration") == 0) {
        lv_obj_set_size(object, LV_SIZE_CONTENT, px_tenths(80));
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface_variant), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_top(object, 0, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-chart-card") == 0) {
        floating_box_tenths(object, 64, 688, 1792, 896);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_radius(object, px(14), 0);
        lv_obj_set_style_bg_color(
            object, weather_color(WeatherColorRole::secondary_container), 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-probability") == 0) {
        floating_box_tenths(object, 64, 48, 1664, 272);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px_tenths(32), 0);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        return;
    }
    if (std::strcmp(id, "weather.rain-probability-icon") == 0) {
        lv_obj_set_size(object, px(20), px(20));
        lv_image_set_scale(object, 100);
        lv_obj_set_style_translate_y(object, 1, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-probability-value") == 0) {
        lv_obj_set_width(object, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::metric), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface), 0);
        lv_obj_set_style_pad_top(object, px(2), 0);
        lv_obj_set_style_translate_y(object, 1, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-probability-label") == 0) {
        lv_obj_set_width(object, px(45));
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface_variant), 0);
        lv_obj_set_style_text_line_space(object, px(-2), 0);
        lv_obj_set_style_pad_top(object, px(2), 0);
        lv_obj_set_style_translate_x(object, 3, 0);
        lv_obj_set_style_translate_y(object, 1, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-bars") == 0) {
        floating_box_tenths(object, 64, 336, 1664, 440);
        lv_chart_set_type(object, LV_CHART_TYPE_LINE);
        lv_obj_set_style_line_opa(object, LV_OPA_TRANSP, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_INDICATOR);

        // Compose draws this chart directly in the physical Canvas: thirteen
        // equal cells, a 55%-cell bar centered in each one, and a one-pixel
        // baseline.  Repeating the arithmetic in logical dp makes rounding
        // drift across the row at 1.25x density, so keep the drawing geometry
        // in the already-scaled LVGL coordinate space.
        const auto chart_width = px_tenths(1664);
        const auto chart_height = px_tenths(440);
        const auto sample_count = static_cast<std::int32_t>(node.sample_count);
        const auto cell_width =
            sample_count > 0 ? chart_width / sample_count : chart_width;
        const auto bar_width = std::max<std::int32_t>(
            1,
            (cell_width * 55 + 50) / 100);

        auto* baseline = lv_obj_create(object);
        ComponentFactory::reset(baseline);
        lv_obj_add_flag(baseline, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(baseline, chart_width, 1);
        lv_obj_set_pos(baseline, 0, chart_height - 1);
        lv_obj_set_style_bg_color(
            baseline,
            weather_color(WeatherColorRole::outline_variant),
            0);
        lv_obj_set_style_bg_opa(baseline, LV_OPA_COVER, 0);

        for (std::size_t index = 0; index < node.sample_count; ++index) {
            auto* bar = lv_obj_create(object);
            ComponentFactory::reset(bar);
            lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
            const auto bar_height =
                std::max<std::int32_t>(
                    2,
                    (static_cast<std::int32_t>(node.samples[index]) *
                         (chart_height - 3) +
                     std::max<std::int32_t>(1, node.maximum) / 2) /
                        std::max<std::int32_t>(1, node.maximum));
            lv_obj_set_size(bar, bar_width, bar_height);
            lv_obj_set_pos(
                bar,
                static_cast<std::int32_t>(index) * cell_width +
                    (cell_width - bar_width) / 2,
                chart_height - bar_height);
            lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(
                bar, weather_color(WeatherColorRole::rain), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        }
        return;
    }
    if (std::strcmp(id, "weather.rain-times") == 0) {
        floating_box_tenths(object, 64, 808, 1664, 80);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        return;
    }
    if (std::strncmp(id, "weather.rain-time-", 18) == 0) {
        lv_obj_set_height(object, LV_PCT(100));
        lv_obj_set_flex_grow(object, 1);
        lv_obj_set_style_text_font(
            object, weather_font(WeatherTypographyRole::micro), 0);
        lv_obj_set_style_text_color(
            object, weather_color(WeatherColorRole::on_surface_variant), 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-actions") == 0) {
        floating_box(object, 0, 144, 192, 48);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, 0, 0);
        return;
    }
    if (std::strcmp(id, "weather.rain-details") == 0) {
        lv_obj_set_size(object, px(133), LV_PCT(100));
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        if (lv_obj_get_child_count(object) > 1) {
            auto* image = lv_obj_get_child(object, 0);
            auto* label = lv_obj_get_child(object, 0);
            if (lv_obj_check_type(image, &lv_image_class)) {
                label = lv_obj_get_child(object, 1);
            }
            auto* visual = lv_obj_create(object);
            ComponentFactory::reset(visual);
            // The semantic button keeps a full 48dp hit target while the
            // compact visual is positioned optically inside it.  This child
            // must opt out of the button's flex layout or LVGL silently drops
            // the same 6dp downward offset used by the Compose oracle.
            lv_obj_add_flag(visual, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(visual, px(120), px(29));
            lv_obj_align(visual, LV_ALIGN_CENTER, 0, px(6));
            lv_obj_set_style_radius(visual, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(
                visual, weather_color(WeatherColorRole::primary_container), 0);
            lv_obj_set_style_bg_opa(visual, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(visual, px(10), 0);
            lv_obj_set_flex_flow(visual, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                visual,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            if (image != nullptr && lv_obj_check_type(image, &lv_image_class)) {
                lv_obj_set_parent(image, visual);
                lv_obj_set_size(image, px(15), px(15));
                lv_image_set_scale(image, 160);
                lv_obj_set_style_image_recolor(
                    image,
                    weather_color(WeatherColorRole::on_primary_container),
                    0);
                lv_obj_set_style_image_recolor_opa(
                    image, LV_OPA_COVER, 0);
            }
            if (label != nullptr && lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_parent(label, visual);
                lv_obj_set_flex_grow(label, 1);
                lv_obj_set_style_text_font(
                    label, weather_font(WeatherTypographyRole::row), 0);
                lv_obj_set_style_text_color(
                    label, weather_color(WeatherColorRole::on_primary_container), 0);
            }
            auto* chevron = create_weather_icon(
                visual,
                generated::WeatherIcon::utility_chevron_right,
                px(15),
                false);
            if (chevron != nullptr) {
                lv_obj_set_style_image_recolor(
                    chevron,
                    weather_color(WeatherColorRole::on_primary_container),
                    0);
                lv_obj_set_style_image_recolor_opa(
                    chevron, LV_OPA_COVER, 0);
            }
        }
        return;
    }
    if (std::strcmp(id, "weather.rain-status") == 0) {
        lv_obj_set_size(object, px(59), LV_PCT(100));
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_pad_gap(object, px(3), 0);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            object,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        if (lv_obj_get_child_count(object) > 1) {
            auto* first = lv_obj_get_child(object, 0);
            auto* second = lv_obj_get_child(object, 1);
            auto* image = lv_obj_check_type(first, &lv_image_class) ? first : second;
            auto* label = lv_obj_check_type(first, &lv_label_class) ? first : second;
            if (image != nullptr && lv_obj_check_type(image, &lv_image_class)) {
                lv_obj_set_size(image, px(15), px(15));
                lv_image_set_scale(image, 160);
                lv_obj_set_style_image_recolor(
                    image,
                    weather_color(WeatherColorRole::on_surface_variant),
                    0);
                lv_obj_set_style_image_recolor_opa(
                    image, LV_OPA_COVER, 0);
            }
            if (label != nullptr && lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_width(label, LV_SIZE_CONTENT);
                lv_obj_set_style_text_font(
                    label, weather_font(WeatherTypographyRole::micro), 0);
                lv_obj_set_style_text_color(
                    label, weather_color(WeatherColorRole::on_surface_variant), 0);
            }
        }
        return;
    }
}

lv_obj_t* layout(
    ComponentFactory& factory,
    lv_obj_t* parent,
    const WireNode& node,
    bool scroll) {
    auto* object = lv_obj_create(parent);
    factory.reset(object);
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_height(object, scroll ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_style_pad_gap(object, gap_px(node.gap), 0);
    lv_obj_set_flex_flow(
        object,
        node.kind == ComponentKind::row
            ? LV_FLEX_FLOW_ROW
            : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        object,
        align(node.alignment),
        align(node.alignment),
        align(node.alignment));
    if (scroll) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(object, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
    }
    return object;
}

lv_event_code_t lvgl_event_code(EventKind kind) {
    switch (kind) {
        case EventKind::tap: return LV_EVENT_CLICKED;
        case EventKind::long_press: return LV_EVENT_LONG_PRESSED;
        case EventKind::repeat: return LV_EVENT_LONG_PRESSED_REPEAT;
        case EventKind::value_changing:
        case EventKind::checked_changed:
        case EventKind::page_changed:
            return LV_EVENT_VALUE_CHANGED;
        case EventKind::value_committed:
        case EventKind::submit:
            return LV_EVENT_RELEASED;
        case EventKind::dismissed:
        case EventKind::cancel:
            return LV_EVENT_CANCEL;
        case EventKind::retry:
            return LV_EVENT_CLICKED;
    }
    return LV_EVENT_CLICKED;
}

void dispatch_bound_event(
    MountedEventBinding* binding,
    EventValue value) {
    auto* event = binding == nullptr ? nullptr : binding->event;
    if (event == nullptr || event->document == nullptr ||
        event->sink == nullptr ||
        event->node_index >= event->document->node_count) {
        return;
    }
    const auto& document = *event->document;
    const auto& node = document.nodes[event->node_index];
    const UiEvent envelope{
        1,
        document.string_at(document.app_id_offset),
        document.string_at(document.nodes[0].id_offset),
        document.string_at(node.id_offset),
        document.string_at(event->action_id_offset),
        event->kind,
        lv_tick_get(),
        value,
    };
    event->sink(envelope, event->sink_context);
}

void dispatch_event(lv_event_t* lv_event) {
    auto* binding = static_cast<MountedEventBinding*>(
        lv_event_get_user_data(lv_event));
    auto* event = binding == nullptr ? nullptr : binding->event;
    if (event == nullptr || event->document == nullptr ||
        event->node_index >= event->document->node_count) {
        return;
    }
    const auto& node = event->document->nodes[event->node_index];
    EventValue value{};
    switch (binding->value_kind) {
        case MountedEventValue::none:
            break;
        case MountedEventValue::integer:
            value = EventValue::integer(binding->integer_value);
            break;
        case MountedEventValue::node_value:
            value = EventValue::integer(node.value);
            break;
        case MountedEventValue::stepper_decrement:
            value = EventValue::integer(std::max(
                node.minimum, node.value - node.step));
            break;
        case MountedEventValue::stepper_increment:
            value = EventValue::integer(std::min(
                node.maximum, node.value + node.step));
            break;
        case MountedEventValue::checked_state:
            value = EventValue::boolean(
                node.mounted_object != nullptr &&
                lv_obj_has_state(
                    static_cast<lv_obj_t*>(node.mounted_object),
                    LV_STATE_CHECKED));
            break;
        case MountedEventValue::keypad_key:
            if (binding->key_index >= event->document->key_count) return;
            value = EventValue::text(event->document->string_at(
                event->document->key_offsets[binding->key_index]));
            break;
    }
    dispatch_bound_event(binding, value);
}

bool bind_event(
    WireDocument& document,
    WireEvent& event,
    lv_obj_t* object,
    MountedEventValue value_kind = MountedEventValue::none,
    std::int32_t integer_value = 0,
    std::uint16_t key_index = 0) {
    if (object == nullptr ||
        document.mounted_event_binding_count >=
            document.mounted_event_bindings.size()) {
        return false;
    }
    auto& binding =
        document.mounted_event_bindings[
            document.mounted_event_binding_count++];
    binding = {&event, value_kind, integer_value, key_index};
    lv_obj_add_event_cb(
        object,
        dispatch_event,
        lvgl_event_code(event.kind),
        &binding);
    return true;
}

void pager_gesture(lv_event_t* lv_event) {
    auto* binding = static_cast<MountedEventBinding*>(
        lv_event_get_user_data(lv_event));
    auto* event = binding == nullptr ? nullptr : binding->event;
    if (event == nullptr || event->document == nullptr ||
        event->node_index >= event->document->node_count) {
        return;
    }
    auto& document = *event->document;
    auto& node = document.nodes[event->node_index];
    auto* indev = lv_indev_active();
    if (indev == nullptr) return;
    const auto direction = lv_indev_get_gesture_dir(indev);
    auto selected = node.value;
    if (direction == LV_DIR_LEFT && selected + 1 < node.maximum) {
        ++selected;
    } else if (direction == LV_DIR_RIGHT && selected > 0) {
        --selected;
    } else {
        return;
    }
    node.value = selected;
    for (std::size_t index = event->node_index + 1;
         index < document.node_count; ++index) {
        auto& page = document.nodes[index];
        if (page.parent_index != event->node_index ||
            page.mounted_object == nullptr) {
            continue;
        }
        std::size_t ordinal = 0;
        for (std::size_t previous = event->node_index + 1;
             previous < index; ++previous) {
            if (document.nodes[previous].parent_index == event->node_index) {
                ++ordinal;
            }
        }
        auto* page_object = static_cast<lv_obj_t*>(page.mounted_object);
        if (ordinal == static_cast<std::size_t>(selected)) {
            lv_obj_remove_flag(page_object, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page_object, LV_OBJ_FLAG_HIDDEN);
        }
    }
    auto* object = static_cast<lv_obj_t*>(node.mounted_object);
    if (object != nullptr && node.checked &&
        lv_obj_get_child_count(object) > 0) {
        auto* indicator = lv_obj_get_child(object, 0);
        if (indicator != nullptr &&
            lv_obj_get_child_count(indicator) ==
                static_cast<std::uint32_t>(node.maximum)) {
            for (std::int32_t page = 0; page < node.maximum; ++page) {
                auto* dot = lv_obj_get_child(indicator, page);
                const auto active = page == selected;
                const auto dot_size = px(active ? 5 : 3);
                lv_obj_set_size(dot, dot_size, dot_size);
                lv_obj_set_style_bg_color(
                    dot,
                    active
                        ? weather_color(generated::WeatherColorRole::primary)
                        : weather_color(
                              generated::WeatherColorRole::outline_variant),
                    0);
            }
        }
        lv_obj_send_event(object, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

bool bind_pager_gesture(
    WireDocument& document,
    WireEvent& event,
    lv_obj_t* object) {
    if (object == nullptr ||
        document.mounted_event_binding_count >=
            document.mounted_event_bindings.size()) {
        return false;
    }
    auto& binding = document.mounted_event_bindings[
        document.mounted_event_binding_count++];
    binding = {&event, MountedEventValue::node_value, 0, 0};
    lv_obj_add_event_cb(
        object, pager_gesture, LV_EVENT_GESTURE, &binding);
    return true;
}

void screen_gesture(lv_event_t* lv_event) {
    auto* binding = static_cast<MountedEventBinding*>(
        lv_event_get_user_data(lv_event));
    auto* indev = lv_indev_active();
    if (binding == nullptr || indev == nullptr) return;
    const auto direction = lv_indev_get_gesture_dir(indev);
    const auto delta =
        direction == LV_DIR_LEFT ? 1 :
        direction == LV_DIR_RIGHT ? -1 : 0;
    if (delta == 0) return;

    auto* root = static_cast<lv_obj_t*>(
        lv_event_get_current_target(lv_event));
    dispatch_bound_event(binding, EventValue::integer(delta));

    // The guest synchronously mounts the selected bounded route. Give that
    // incoming page the same compact directional response as the Material
    // reference without retaining a second full AppSpec tree in RAM.
    if (root == nullptr || !lv_obj_is_valid(root) ||
        lv_obj_get_child_count(root) == 0) {
        return;
    }
    auto* incoming = lv_obj_get_child(root, 0);
    if (incoming == nullptr) return;
    lv_anim_delete(incoming, nullptr);
    const auto animate_translate = [](void* object, std::int32_t value) {
        lv_obj_set_style_translate_x(
            static_cast<lv_obj_t*>(object), value, 0);
    };
    const auto animate_opacity = [](void* object, std::int32_t value) {
        lv_obj_set_style_opa(
            static_cast<lv_obj_t*>(object),
            static_cast<lv_opa_t>(value),
            0);
    };
    const auto offset = delta > 0 ? px(18) : -px(18);
    lv_obj_set_style_translate_x(incoming, offset, 0);
    lv_obj_set_style_opa(incoming, LV_OPA_60, 0);

    lv_anim_t movement{};
    lv_anim_init(&movement);
    lv_anim_set_var(&movement, incoming);
    lv_anim_set_exec_cb(&movement, animate_translate);
    lv_anim_set_values(&movement, offset, 0);
    lv_anim_set_duration(&movement, 220);
    lv_anim_set_path_cb(&movement, lv_anim_path_ease_out);
    lv_anim_start(&movement);

    lv_anim_t fade{};
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, incoming);
    lv_anim_set_exec_cb(&fade, animate_opacity);
    lv_anim_set_values(&fade, LV_OPA_60, LV_OPA_COVER);
    lv_anim_set_duration(&fade, 160);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_start(&fade);
}

bool bind_screen_gesture(
    WireDocument& document,
    WireEvent& event,
    lv_obj_t* object) {
    if (object == nullptr ||
        document.mounted_event_binding_count >=
            document.mounted_event_bindings.size()) {
        return false;
    }
    auto& binding = document.mounted_event_bindings[
        document.mounted_event_binding_count++];
    binding = {&event, MountedEventValue::none, 0, 0};
    lv_obj_add_event_cb(
        object, screen_gesture, LV_EVENT_GESTURE, &binding);
    return true;
}

}  // namespace

Renderer::Renderer(StyleRegistry& styles) : styles_(styles) {}

bool Renderer::mount(
    lv_obj_t* root,
    WireDocument& document,
    WireEventSink event_sink,
    void* event_context) {
    if (root == nullptr || document.node_count == 0 ||
        !styles_.initialized()) {
        return false;
    }
    std::size_t required_bindings = 0;
    if (event_sink != nullptr) {
        for (std::size_t index = 0;
             index < document.node_count;
             ++index) {
            const auto& node = document.nodes[index];
            const auto multiplier =
                node.kind == ComponentKind::keypad
                    ? static_cast<std::size_t>(node.key_count)
                    : node.kind == ComponentKind::stepper ? 2U
                    : node.kind == ComponentKind::pager ? 2U : 1U;
            required_bindings +=
                static_cast<std::size_t>(node.event_count) *
                multiplier;
            if (required_bindings >
                document.mounted_event_bindings.size()) {
                return false;
            }
        }
    }
    document.mounted_event_binding_count = 0;
    ComponentFactory factory(styles_);
    std::array<lv_obj_t*, Reconciler::kCapacity> objects{};
    const auto keypad_document = is_keypad_document(document);
    const auto countdown_document = is_countdown_document(document);
    const auto weather_hero_document =
        is_weather_hero_document(document);
    const auto notification_stack_document =
        is_notification_stack_document(document);
    const auto calendar_agenda_document =
        is_calendar_agenda_document(document);
    const auto task_list_document =
        is_task_list_document(document);
    const auto workout_set_document =
        is_workout_set_document(document);
    const auto workout_rest_document =
        is_workout_rest_document(document);
    const auto workout_summary_document =
        is_workout_summary_document(document);
    const auto nutrition_dashboard_document =
        is_nutrition_dashboard_document(document);
    const auto nutrition_quick_add_document =
        is_nutrition_quick_add_document(document);
    const auto nutrition_review_document =
        is_nutrition_review_document(document);
    const auto voice_ready_document =
        is_voice_ready_document(document);
    const auto live_action_detail_document =
        is_live_action_detail_document(document);
    const auto media_player_document =
        is_media_player_document(document);
    const auto camera_remote_document =
        is_camera_remote_document(document);
    const auto wallet_qr_document =
        is_wallet_qr_document(document);
    const auto canvas_game_document =
        is_canvas_game_document(document);
    const auto weather_component_document =
        uses_weather_components(document);
    const auto weather_current_document =
        is_weather_current_document(document);
    const auto task_toggle_count =
        task_list_document
            ? count_kind(document, ComponentKind::toggle)
            : 0U;
    const auto notification_card_count =
        notification_stack_document
            ? count_kind(document, ComponentKind::card)
            : 0U;
    const auto notification_button_count =
        notification_stack_document
            ? count_kind(document, ComponentKind::button)
            : 0U;
    const auto calendar_card_count =
        calendar_agenda_document
            ? count_kind(document, ComponentKind::card)
            : 0U;
    const auto calendar_button_count =
        calendar_agenda_document
            ? count_kind(document, ComponentKind::button)
            : 0U;
    objects[0] = factory.screen(root);
    if (weather_component_document) {
        lv_obj_set_style_bg_color(
            root,
            weather_color(generated::WeatherColorRole::background),
            0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    }
    document.nodes[0].mounted_object = root;
    if (voice_ready_document || live_action_detail_document ||
        media_player_document || camera_remote_document ||
        wallet_qr_document || canvas_game_document ||
        weather_current_document) {
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_set_style_pad_all(
        root,
        canvas_game_document
            ? 0
            : keypad_document
            ? 5
            : countdown_document || weather_hero_document ||
                    notification_stack_document ||
                    calendar_agenda_document || task_list_document ||
                    workout_set_document || workout_rest_document ||
                    workout_summary_document ||
                    nutrition_dashboard_document ||
                    nutrition_quick_add_document ||
                    nutrition_review_document ||
                    voice_ready_document ||
                    live_action_detail_document
                    || media_player_document || camera_remote_document ||
                    wallet_qr_document || canvas_game_document ||
                    weather_component_document
                ? 0
                : px(12),
        0);
    lv_obj_set_style_pad_gap(
        root,
        canvas_game_document
            ? 0
            : keypad_document ? 4 : gap_px(document.nodes[0].gap),
        0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        root,
        keypad_document
            ? LV_FLEX_ALIGN_START
            : align(document.nodes[0].alignment),
        keypad_document
            ? LV_FLEX_ALIGN_CENTER
            : align(document.nodes[0].alignment),
        keypad_document
            ? LV_FLEX_ALIGN_CENTER
            : align(document.nodes[0].alignment));

    if (event_sink != nullptr) {
        for (std::size_t event_index = 0;
             event_index < document.event_count;
             ++event_index) {
            auto& event = document.events[event_index];
            if (event.node_index != 0) continue;
            if (event.kind != EventKind::page_changed) return false;
            event.document = &document;
            event.sink = event_sink;
            event.sink_context = event_context;
            if (!bind_screen_gesture(document, event, root)) {
                return false;
            }
        }
    }

    for (std::size_t index = 1; index < document.node_count; ++index) {
        auto& node = document.nodes[index];
        if (node.parent_index >= index ||
            objects[node.parent_index] == nullptr) {
            return false;
        }
        auto* parent = objects[node.parent_index];
        const auto* primary =
            document.string_at(node.primary_text_offset);
        const auto* secondary =
            document.string_at(node.secondary_text_offset);
        lv_obj_t* object = nullptr;
        switch (node.kind) {
            case ComponentKind::column:
            case ComponentKind::row:
                object = layout(factory, parent, node, false);
                if (calendar_agenda_document &&
                    node.kind == ComponentKind::column) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (workout_summary_document &&
                           node.kind == ComponentKind::row) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(44));
                    lv_obj_set_pos(object, px(4), px(28));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (
                    (nutrition_quick_add_document ||
                     nutrition_review_document ||
                     live_action_detail_document ||
                     camera_remote_document ||
                     wallet_qr_document) &&
                    node.kind == ComponentKind::row) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(48));
                    lv_obj_set_pos(object, px(4), px(136));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (media_player_document &&
                           node.kind == ComponentKind::row) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(80));
                    lv_obj_set_pos(object, px(4), px(104));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                }
                break;
            case ComponentKind::scroll:
                object = layout(factory, parent, node, true);
                if (notification_stack_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (calendar_agenda_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (task_list_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                }
                break;
            case ComponentKind::text:
                object = factory.text(
                    parent, primary, typography(node.variant));
                lv_obj_set_width(object, LV_PCT(100));
                if (weather_component_document) {
                    using generated::WeatherTypographyRole;
                    const auto role =
                        node.variant == 2 ? WeatherTypographyRole::label :
                        node.variant == 3 ? WeatherTypographyRole::row :
                        node.variant == 4 ? WeatherTypographyRole::headline :
                        node.variant == 5 ? WeatherTypographyRole::micro :
                        node.variant == 1 ? WeatherTypographyRole::metric :
                        WeatherTypographyRole::headline;
                    lv_obj_set_style_text_font(
                        object, weather_font(role), 0);
                    lv_obj_set_style_text_color(
                        object,
                        node.variant == 2 || node.variant == 5
                            ? weather_color(
                                  generated::WeatherColorRole::on_surface_variant)
                            : weather_color(
                                  generated::WeatherColorRole::on_background),
                        0);
                }
                if (node.icon_offset != 0) {
                    generated::WeatherIcon icon{};
                    if (weather_icon(
                            document.string_at(node.icon_offset), icon)) {
                        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(
                            object,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
                        lv_obj_set_style_pad_gap(object, px(6), 0);
                        auto* image = create_weather_icon(
                            object, icon, px(18), true);
                        if (image == nullptr) return false;
                        lv_obj_move_to_index(image, 0);
                    }
                }
                lv_label_set_long_mode(
                    object,
                    keypad_document && node.variant == 4
                        ? LV_LABEL_LONG_DOT
                        : LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(
                    object,
                    node.alignment == 0
                        ? LV_TEXT_ALIGN_LEFT
                        : node.alignment == 2
                            ? LV_TEXT_ALIGN_RIGHT
                            : LV_TEXT_ALIGN_CENTER,
                    0);
                if (canvas_game_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    if (node.variant == 4) {
                        lv_obj_set_size(object, px(52), px(52));
                        lv_obj_set_pos(object, px(132), px(40));
                        lv_obj_set_style_text_font(
                            object, &m3e_live_action_font_32, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xA8, 0xF2, 0x79),
                            0);
                        lv_obj_set_style_pad_top(object, px(8), 0);
                    } else {
                        lv_obj_set_size(object, px(52), px(20));
                        lv_obj_set_pos(object, px(132), px(18));
                        lv_obj_set_style_text_font(
                            object, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0x9C, 0xE8, 0xC2),
                            0);
                        lv_obj_set_style_pad_top(object, px(2), 0);
                    }
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                } else if (keypad_document && node.variant == 4) {
                    lv_obj_set_height(object, 50);
                    lv_obj_set_style_text_font(
                        object, &m3e_calculator_result_font_40, 0);
                    lv_obj_set_style_pad_top(object, 11, 0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_RIGHT, 0);
                } else if (countdown_document && node.variant == 4) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(112), px(58));
                    lv_obj_set_pos(
                        object,
                        (240 - px(112)) / 2,
                        px(41));
                    lv_obj_set_style_text_font(
                        object, &m3e_timer_font_55, 0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object, px(10), 0);
                } else if (weather_hero_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    if (node.variant == 1) {
                        lv_obj_set_size(object, px(44), px(44));
                        lv_obj_set_pos(object, px(16), px(56));
                        lv_obj_set_style_text_opa(
                            object, LV_OPA_TRANSP, 0);

                        auto* halo = lv_obj_create(object);
                        ComponentFactory::reset(halo);
                        lv_obj_set_size(halo, px(40), px(40));
                        lv_obj_center(halo);
                        lv_obj_set_style_radius(
                            halo, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            halo,
                            lv_color_make(0xFF, 0xDC, 0xC2),
                            0);
                        lv_obj_set_style_bg_opa(halo, 82, 0);

                        auto* core = lv_obj_create(object);
                        ComponentFactory::reset(core);
                        lv_obj_set_size(core, px(28), px(28));
                        lv_obj_center(core);
                        lv_obj_set_style_radius(
                            core, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            core,
                            lv_color_make(0xFF, 0xDC, 0xC2),
                            0);
                        lv_obj_set_style_bg_opa(
                            core, LV_OPA_COVER, 0);
                    } else if (node.variant == 2) {
                        lv_obj_set_size(object, px(184), px(24));
                        lv_obj_set_pos(object, px(4), px(4));
                        lv_obj_set_style_text_font(
                            object, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            object, LV_TEXT_ALIGN_CENTER, 0);
                        lv_obj_set_style_pad_top(object, px(3), 0);
                    } else if (node.variant == 4) {
                        lv_obj_set_size(object, px(112), px(64));
                        lv_obj_set_pos(object, px(60), px(48));
                        lv_obj_set_style_text_font(
                            object, &m3e_weather_font_55, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            object, LV_TEXT_ALIGN_CENTER, 0);
                        lv_obj_set_style_pad_top(object, px(8), 0);
                    } else if (node.variant == 5) {
                        lv_obj_set_size(object, px(108), px(16));
                        lv_obj_set_pos(object, px(20), px(168));
                        lv_obj_set_style_text_font(
                            object, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);
                        lv_obj_set_style_text_opa(object, 184, 0);
                        lv_obj_set_style_text_align(
                            object, LV_TEXT_ALIGN_LEFT, 0);
                    }
                } else if (notification_stack_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (calendar_agenda_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (task_list_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (workout_set_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (workout_rest_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        px(184),
                        node.variant == 4 ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        px(4),
                        node.variant == 4 ? px(24) : px(4));
                    lv_obj_set_style_text_font(
                        object,
                        node.variant == 4
                            ? &m3e_timer_value_font_28
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        node.variant == 4
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        node.variant == 4 ? px(5) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (workout_summary_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    if (node.parent_index == 0) {
                        lv_obj_set_size(object, px(184), px(20));
                        lv_obj_set_pos(object, px(4), px(4));
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                    } else {
                        const auto ordinal =
                            kind_ordinal(
                                document, index, ComponentKind::text) -
                            1;
                        lv_obj_set_size(
                            object,
                            ordinal == 0 ? 112 : 113,
                            px(44));
                        lv_obj_set_pos(
                            object,
                            ordinal == 0 ? 0 : 117,
                            0);
                        lv_obj_set_style_radius(object, px(16), 0);
                        lv_obj_set_style_bg_color(
                            object,
                            lv_color_make(0x33, 0x2E, 0x3C),
                            0);
                        lv_obj_set_style_bg_opa(
                            object, LV_OPA_COVER, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                    }
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        node.parent_index == 0 ? px(2) : px(12),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (
                    nutrition_dashboard_document ||
                    nutrition_review_document) {
                    const auto is_total = node.variant == 4;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        px(184),
                        is_total ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        px(4),
                        is_total ? px(24) : px(4));
                    lv_obj_set_style_text_font(
                        object,
                        is_total
                            ? &m3e_nutrition_font_32
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        is_total
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        is_total ? px(6) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (nutrition_quick_add_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (voice_ready_document ||
                           live_action_detail_document ||
                           wallet_qr_document) {
                    const auto is_value =
                        live_action_detail_document && node.variant == 4;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        px(184),
                        is_value ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        px(4),
                        is_value ? px(24) : px(4));
                    lv_obj_set_style_text_font(
                        object,
                        is_value
                            ? &m3e_live_action_font_32
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        is_value
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        is_value ? px(6) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (media_player_document) {
                    const auto is_title = node.variant == 1;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object, px(104), is_title ? px(44) : px(32));
                    lv_obj_set_pos(
                        object, px(84), is_title ? px(4) : px(48));
                    lv_obj_set_style_text_font(
                        object,
                        is_title
                            ? &lv_font_montserrat_18
                            : &lv_font_montserrat_14,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        is_title
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_LEFT, 0);
                    lv_obj_set_style_pad_top(
                        object, is_title ? px(4) : px(2), 0);
                    lv_label_set_long_mode(
                        object,
                        is_title ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
                } else if (camera_remote_document) {
                    const auto is_value = node.variant == 4;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        is_value ? px(144) : px(160),
                        is_value ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        is_value ? px(24) : px(16),
                        is_value ? px(44) : px(8));
                    lv_obj_set_style_radius(
                        object,
                        is_value ? px(24) : px(10),
                        0);
                    lv_obj_set_style_bg_color(
                        object, lv_color_make(0x00, 0x00, 0x00), 0);
                    lv_obj_set_style_bg_opa(
                        object, is_value ? 168 : 199, 0);
                    lv_obj_set_style_text_font(
                        object,
                        is_value
                            ? &m3e_live_action_font_32
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xF6, 0xED, 0xFF),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        is_value ? px(5) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                }
                break;
            case ComponentKind::button:
                object = factory.button(
                    parent,
                    {
                        document.string_at(node.id_offset),
                        primary,
                        tone(node.tone),
                        node.variant <=
                                static_cast<std::uint8_t>(
                                    ButtonVariant::text)
                            ? static_cast<ButtonVariant>(node.variant)
                            : ButtonVariant::filled,
                        component_size(node.size),
                        node.enabled,
                        false,
                    });
                lv_obj_set_width(object, LV_PCT(100));
                if (node.icon_offset != 0 && !weather_hero_document) {
                    generated::WeatherIcon icon{};
                    if (weather_icon(
                            document.string_at(node.icon_offset), icon)) {
                        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(
                            object,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
                        lv_obj_set_style_pad_gap(object, px(6), 0);
                        auto* image = create_weather_icon(
                            object, icon, px(18), true);
                        if (image == nullptr) return false;
                        lv_obj_move_to_index(image, 0);
                    }
                }
                if (weather_component_document) {
                    lv_obj_set_style_bg_color(
                        object,
                        weather_color(generated::WeatherColorRole::primary),
                        0);
                    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
                    auto* label = lv_obj_get_child(object, 0);
                    if (label != nullptr &&
                        lv_obj_check_type(label, &lv_image_class) &&
                        lv_obj_get_child_count(object) > 1) {
                        label = lv_obj_get_child(object, 1);
                    }
                    if (label != nullptr &&
                        lv_obj_check_type(label, &lv_label_class)) {
                        lv_obj_set_style_text_font(
                            label,
                            weather_font(
                                generated::WeatherTypographyRole::label),
                            0);
                        lv_obj_set_style_text_color(
                            label,
                            weather_color(
                                generated::WeatherColorRole::on_primary),
                            0);
                    }
                }
                if (countdown_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(120), px(40));
                    lv_obj_set_pos(
                        object,
                        (240 - px(120)) / 2,
                        px(140));
                    lv_obj_set_height(object, px(48));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                    }
                } else if (weather_hero_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(48), px(48));
                    lv_obj_set_pos(object, px(136), px(136));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    lv_obj_set_style_opa(
                        object,
                        LV_OPA_40,
                        static_cast<lv_style_selector_t>(
                            LV_STATE_DISABLED));
                    if (lv_obj_get_child_count(object) == 1) {
                        lv_obj_set_style_text_opa(
                            lv_obj_get_child(object, 0),
                            LV_OPA_TRANSP,
                            0);
                    }

                    auto* refresh = lv_arc_create(object);
                    lv_obj_remove_flag(
                        refresh, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_size(refresh, px(24), px(24));
                    lv_obj_center(refresh);
                    lv_arc_set_bg_angles(refresh, 35, 310);
                    lv_obj_set_style_arc_width(
                        refresh, px(3), LV_PART_MAIN);
                    lv_obj_set_style_arc_color(
                        refresh,
                        lv_color_make(0x35, 0x11, 0x51),
                        LV_PART_MAIN);
                    lv_obj_set_style_arc_opa(
                        refresh,
                        LV_OPA_TRANSP,
                        LV_PART_INDICATOR);
                    lv_obj_set_style_bg_opa(
                        refresh, LV_OPA_TRANSP, LV_PART_KNOB);
                    lv_obj_set_style_border_opa(
                        refresh, LV_OPA_TRANSP, LV_PART_KNOB);

                    auto* arrow = lv_obj_create(object);
                    ComponentFactory::reset(arrow);
                    lv_obj_set_size(arrow, px(4), px(4));
                    lv_obj_set_pos(arrow, px(31), px(14));
                    lv_obj_set_style_radius(
                        arrow, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        arrow,
                        lv_color_make(0x35, 0x11, 0x51),
                        0);
                    lv_obj_set_style_bg_opa(
                        arrow, LV_OPA_COVER, 0);
                } else if (notification_stack_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto paired =
                        notification_button_count == 3 &&
                        ordinal < 2;
                    const auto x =
                        paired
                            ? ordinal == 0 ? 0 : px(94)
                            : px(32);
                    const auto y = paired ? px(88) : px(136);
                    const auto width =
                        paired ? px(90) : px(120);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, width, px(48));
                    lv_obj_set_pos(object, x, y);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_WRAP);
                    }
                } else if (calendar_agenda_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto paired = calendar_button_count == 2;
                    const auto x =
                        paired
                            ? px(static_cast<std::int32_t>(
                                ordinal) * 94)
                            : px(32);
                    const auto width =
                        paired ? px(90) : px(120);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, width, px(48));
                    lv_obj_set_pos(object, x, px(136));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_WRAP);
                    }
                } else if (task_list_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(48));
                    lv_obj_set_pos(object, 0, px(136));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (workout_set_document ||
                           workout_rest_document ||
                           workout_summary_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto paired = workout_rest_document;
                    const auto x =
                        paired
                            ? px(4 + static_cast<std::int32_t>(
                                ordinal) * 94)
                            : px(36);
                    const auto width =
                        paired ? px(90) : px(120);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, width, px(48));
                    lv_obj_set_pos(object, x, px(140));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (nutrition_dashboard_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(120), px(48));
                    lv_obj_set_pos(object, px(36), px(140));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (
                    nutrition_quick_add_document ||
                    nutrition_review_document ||
                    live_action_detail_document ||
                    camera_remote_document ||
                    wallet_qr_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        ordinal == 0 ? 112 : 113,
                        px(48));
                    lv_obj_set_pos(
                        object,
                        ordinal == 0 ? 0 : 117,
                        0);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (media_player_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        ordinal == 0 ? 113 : 112,
                        px(80));
                    lv_obj_set_pos(
                        object,
                        ordinal == 0 ? 0 : 118,
                        0);
                    lv_obj_set_style_radius(
                        object, px(28), 0);
                    lv_obj_set_style_pad_hor(
                        object, px(8), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_WRAP);
                    }
                }
                break;
            case ComponentKind::card:
                object = factory.card(
                    parent,
                    {
                        primary,
                        secondary,
                        tone(node.tone),
                        node.event_count > 0,
                    });
                if (weather_hero_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(156));
                    lv_obj_set_pos(object, px(4), px(32));
                    lv_obj_set_style_radius(object, px(32), 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x4E, 0x28, 0x6E),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(152), px(24));
                        lv_obj_set_pos(title, px(16), px(78));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);

                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(108), px(38));
                        lv_obj_set_pos(body, px(16), px(102));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);
                        lv_obj_set_style_text_opa(body, 200, 0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_WRAP);
                    }
                } else if (notification_stack_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::card);
                    const auto y =
                        notification_card_count == 2
                            ? px(24 + static_cast<std::int32_t>(
                                    ordinal) * 58)
                            : px(24);
                    const auto height =
                        notification_card_count == 2
                            ? px(54)
                            : notification_button_count == 3
                                ? px(60)
                                : px(108);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(object, 0, y);
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_ver(object, 0, 0);
                    lv_obj_set_style_pad_hor(
                        object, px(12), 0);
                    lv_obj_set_style_pad_row(
                        object, px(2), 0);
                    lv_obj_set_flex_flow(
                        object, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_flex_align(
                        object,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        lv_obj_set_width(title, LV_PCT(100));
                        lv_obj_set_height(
                            title, LV_SIZE_CONTENT);
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            title, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);

                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_set_width(body, LV_PCT(100));
                        lv_obj_set_height(
                            body, LV_SIZE_CONTENT);
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_obj_set_style_text_opa(
                            body, LV_OPA_COVER, 0);
                        lv_obj_set_style_text_align(
                            body, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_WRAP);
                    }
                } else if (calendar_agenda_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::card);
                    const auto y =
                        calendar_card_count == 2
                            ? px(24 + static_cast<std::int32_t>(
                                    ordinal) * 54)
                            : px(24);
                    const auto height =
                        calendar_card_count == 2
                            ? px(50)
                            : px(104);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(object, 0, y);
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            title, px(160), px(20));
                        lv_obj_set_pos(
                            title, px(12), px(6));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            title, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);

                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            body,
                            px(160),
                            std::max(px(18), height - px(26)));
                        lv_obj_set_pos(
                            body, px(12), px(26));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_obj_set_style_text_align(
                            body, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_WRAP);
                    }
                } else if (workout_summary_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(60));
                    lv_obj_set_pos(object, px(4), px(76));
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(20));
                        lv_obj_set_pos(title, px(12), px(6));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(160), px(24));
                        lv_obj_set_pos(body, px(12), px(28));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                    }
                } else if (
                    nutrition_dashboard_document ||
                    nutrition_quick_add_document ||
                    nutrition_review_document) {
                    const auto y =
                        nutrition_dashboard_document
                            ? px(92)
                            : nutrition_quick_add_document
                                ? px(80)
                                : px(76);
                    const auto height =
                        nutrition_dashboard_document
                            ? px(44)
                            : nutrition_quick_add_document
                                ? px(52)
                                : px(56);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(object, px(4), y);
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(18));
                        lv_obj_set_pos(title, px(12), px(5));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            body,
                            px(160),
                            std::max(px(16), height - px(23)));
                        lv_obj_set_pos(body, px(12), px(23));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                    }
                } else if (voice_ready_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(56));
                    lv_obj_set_pos(object, px(4), px(132));
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(18));
                        lv_obj_set_pos(title, px(12), px(5));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(160), px(28));
                        lv_obj_set_pos(body, px(12), px(23));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                    }
                }
                break;
            case ComponentKind::live_card:
                object = factory.live_card(
                    parent,
                    {
                        document.string_at(node.semantic_label_offset),
                        primary,
                        secondary,
                        nullptr,
                        node.value,
                        (node.property_mask & (1UL << 2U)) != 0
                            ? node.maximum
                            : 0,
                        tone(node.tone),
                    });
                if (workout_set_document ||
                    workout_rest_document ||
                    live_action_detail_document) {
                    const auto is_rest = workout_rest_document;
                    const auto height =
                        is_rest ? px(60) : px(56);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_scrollbar_mode(
                        object, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(
                        object,
                        px(4),
                        is_rest || live_action_detail_document
                            ? px(76)
                            : px(80));
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) >= 3) {
                        auto* app = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(app, LV_OBJ_FLAG_HIDDEN);
                        auto* title = lv_obj_get_child(object, 1);
                        auto* body = lv_obj_get_child(object, 2);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(18));
                        lv_obj_set_pos(title, px(12), px(5));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(160), px(16));
                        lv_obj_set_pos(body, px(12), px(23));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                        if ((node.property_mask & (1UL << 2U)) != 0 &&
                            node.maximum > 0 &&
                            lv_obj_get_child_count(object) >= 4) {
                            auto* progress =
                                lv_obj_get_child(object, 3);
                            lv_obj_add_flag(
                                progress, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(
                                progress, px(160), px(12));
                            lv_obj_set_pos(
                                progress, px(12), px(34));
                            lv_obj_set_style_bg_color(
                                progress,
                                lv_color_make(0x33, 0x2E, 0x3C),
                                LV_PART_MAIN);
                            lv_obj_set_style_bg_color(
                                progress,
                                lv_color_make(0xD8, 0xB9, 0xFF),
                                LV_PART_INDICATOR);

                            auto* stop = lv_obj_create(object);
                            ComponentFactory::reset(stop);
                            lv_obj_add_flag(
                                stop, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(stop, px(4), px(4));
                            lv_obj_set_pos(
                                stop, px(162), px(37));
                            lv_obj_set_style_radius(
                                stop, LV_RADIUS_CIRCLE, 0);
                            lv_obj_set_style_bg_color(
                                stop,
                                lv_color_make(0xD8, 0xB9, 0xFF),
                                0);
                            lv_obj_set_style_bg_opa(
                                stop, LV_OPA_COVER, 0);
                        }
                    }
                }
                break;
            case ComponentKind::progress:
                object = node.variant == 1
                    ? factory.circular_progress(
                          parent,
                          {primary, node.value, node.maximum, tone(node.tone)})
                    : factory.linear_progress(
                          parent,
                          {primary, node.value, node.maximum, tone(node.tone)});
                if (countdown_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(132), px(132));
                    lv_obj_set_pos(
                        object,
                        (240 - px(132) + 1) / 2,
                        px(4));
                    lv_obj_set_style_arc_width(
                        object, px(6), LV_PART_MAIN);
                    lv_obj_set_style_arc_width(
                        object, px(6), LV_PART_INDICATOR);
                    lv_obj_set_style_arc_color(
                        object,
                        lv_color_make(0x49, 0x44, 0x53),
                        LV_PART_MAIN);
                    lv_obj_set_style_arc_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        LV_PART_INDICATOR);
                    lv_arc_set_bg_angles(object, 0, 360);
                    lv_arc_set_rotation(object, 270);
                } else if (nutrition_dashboard_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(12));
                    lv_obj_set_pos(object, px(4), px(76));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x49, 0x44, 0x53),
                        LV_PART_MAIN);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        LV_PART_INDICATOR);
                } else if (media_player_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(16));
                    lv_obj_set_pos(object, px(4), px(84));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x49, 0x44, 0x53),
                        LV_PART_MAIN);
                    lv_obj_set_style_bg_color(
                        object,
                        node.tone == static_cast<std::uint8_t>(Tone::error)
                            ? lv_color_make(0xFF, 0xB4, 0xAB)
                            : lv_color_make(0xD8, 0xB9, 0xFF),
                        LV_PART_INDICATOR);
                }
                break;
            case ComponentKind::stepper:
                if (!countdown_document) {
                    object = factory.stepper(
                        parent,
                        {
                            primary,
                            node.value,
                            secondary,
                            node.value <= node.minimum,
                            node.value >= node.maximum,
                        });
                    if (workout_set_document ||
                        nutrition_quick_add_document) {
                        lv_obj_add_flag(
                            object, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            object, px(184), px(48));
                        lv_obj_set_pos(
                            object, px(4), px(28));
                        lv_obj_set_style_pad_all(
                            object, 0, 0);
                        lv_obj_set_style_pad_column(
                            object, px(4), 0);
                        lv_obj_set_style_bg_opa(
                            object, LV_OPA_TRANSP, 0);
                        if (lv_obj_get_child_count(object) == 3) {
                            auto* decrement =
                                lv_obj_get_child(object, 0);
                            auto* value =
                                lv_obj_get_child(object, 1);
                            auto* increment =
                                lv_obj_get_child(object, 2);
                            lv_obj_set_size(
                                decrement, px(48), px(48));
                            lv_obj_set_size(
                                increment, px(48), px(48));
                            lv_obj_set_style_bg_color(
                                decrement,
                                lv_color_make(0x33, 0x2E, 0x3C),
                                0);
                            lv_obj_set_style_bg_color(
                                increment,
                                lv_color_make(0xD8, 0xB9, 0xFF),
                                0);
                            auto* value_box = value;
                            lv_obj_set_height(value_box, px(48));
                            lv_obj_set_flex_grow(value_box, 1);
                            lv_obj_set_style_radius(
                                value_box, px(20), 0);
                            lv_obj_set_style_bg_color(
                                value_box,
                                lv_color_make(0x49, 0x44, 0x53),
                                0);
                            lv_obj_set_style_bg_opa(
                                value_box, LV_OPA_COVER, 0);
                            lv_obj_set_style_text_color(
                                value_box,
                                lv_color_make(0x49, 0x44, 0x53),
                                0);

                            char weight[16];
                            std::snprintf(
                                weight,
                                sizeof(weight),
                                "%ld",
                                static_cast<long>(node.value));
                            auto* weight_label = factory.text(
                                value_box,
                                weight,
                                generated::TypographyRole::numeral_small);
                            lv_obj_add_flag(
                                weight_label, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(
                                weight_label, px(80), px(30));
                            lv_obj_set_pos(weight_label, 0, px(1));
                            lv_obj_set_style_text_font(
                                weight_label,
                                &m3e_timer_value_font_28,
                                0);
                            lv_obj_set_style_text_align(
                                weight_label, LV_TEXT_ALIGN_CENTER, 0);

                            auto* unit_label = factory.text(
                                value_box,
                                secondary,
                                generated::TypographyRole::body_extra_small);
                            lv_obj_add_flag(
                                unit_label, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(
                                unit_label, px(80), px(14));
                            lv_obj_set_pos(unit_label, 0, px(29));
                            lv_obj_set_style_text_color(
                                unit_label,
                                lv_color_make(0xCA, 0xC4, 0xD0),
                                0);
                            lv_obj_set_style_text_align(
                                unit_label, LV_TEXT_ALIGN_CENTER, 0);
                        }
                    }
                } else {
                    object = lv_obj_create(parent);
                    ComponentFactory::reset(object);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(140), px(48));
                    lv_obj_set_pos(
                        object,
                        (240 - px(140) + 1) / 2,
                        px(88));
                    lv_obj_set_style_pad_all(object, px(4), 0);
                    lv_obj_set_style_pad_column(object, px(4), 0);
                    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(
                        object,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

                    auto* decrement = factory.button(
                        object,
                        {
                            "decrement",
                            "-",
                            Tone::neutral,
                            ButtonVariant::tonal,
                            ComponentSize::compact,
                            node.value > node.minimum && node.enabled,
                            false,
                        });
                    lv_obj_set_size(decrement, px(40), px(40));
                    if (lv_obj_get_child_count(decrement) == 1) {
                        lv_obj_set_style_text_font(
                            lv_obj_get_child(decrement, 0),
                            &lv_font_montserrat_18,
                            0);
                    }

                    auto* value_box = lv_obj_create(object);
                    ComponentFactory::reset(value_box);
                    lv_obj_set_size(
                        value_box, px(64), px(40));
                    lv_obj_set_style_radius(
                        value_box, px(14), 0);
                    lv_obj_set_style_bg_color(
                        value_box,
                        lv_color_make(0x49, 0x44, 0x53),
                        0);
                    lv_obj_set_style_bg_opa(
                        value_box, LV_OPA_COVER, 0);
                    lv_obj_set_flex_flow(
                        value_box, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_flex_align(
                        value_box,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    char value_text[16]{};
                    std::snprintf(
                        value_text,
                        sizeof(value_text),
                        "%ld",
                        static_cast<long>(node.value));
                    auto* value_label = factory.text(
                        value_box,
                        value_text,
                        generated::TypographyRole::title_large);
                    lv_obj_set_style_text_font(
                        value_label, &m3e_timer_value_font_28, 0);
                    lv_obj_set_style_text_color(
                        value_label,
                        lv_color_make(0xF6, 0xED, 0xFF),
                        0);
                    auto* unit_label = factory.text(
                        value_box,
                        secondary,
                        generated::TypographyRole::body_extra_small);
                    lv_obj_set_style_text_color(
                        unit_label,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);

                    auto* increment = factory.button(
                        object,
                        {
                            "increment",
                            "+",
                            Tone::primary,
                            ButtonVariant::filled,
                            ComponentSize::compact,
                            node.value < node.maximum && node.enabled,
                            false,
                        });
                    lv_obj_set_size(increment, px(40), px(40));
                    lv_obj_set_style_bg_color(
                        increment,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    if (lv_obj_get_child_count(increment) == 1) {
                        auto* label = lv_obj_get_child(increment, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                    }
                }
                break;
            case ComponentKind::toggle:
                object = factory.toggle_row(
                    parent, primary, node.value != 0, node.enabled);
                lv_obj_add_flag(object, LV_OBJ_FLAG_CHECKABLE);
                if (node.value != 0) {
                    lv_obj_add_state(object, LV_STATE_CHECKED);
                }
                if (task_list_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::toggle);
                    const auto height =
                        task_toggle_count == 3 ? 48 : 52;
                    const auto step =
                        task_toggle_count == 3 ? 52 : 56;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object, px(184), px(height));
                    lv_obj_set_pos(
                        object,
                        0,
                        px(24 + static_cast<std::int32_t>(
                                ordinal) * step));
                    lv_obj_set_style_pad_hor(object, px(12), 0);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x4E, 0x28, 0x6E),
                        static_cast<lv_style_selector_t>(
                            LV_PART_MAIN) |
                            static_cast<lv_style_selector_t>(
                                LV_STATE_CHECKED));
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);

                        auto* indicator =
                            lv_obj_get_child(object, 1);
                        lv_obj_clean(indicator);
                        ComponentFactory::reset(indicator);
                        lv_obj_set_size(
                            indicator, px(24), px(24));
                        lv_obj_set_style_radius(
                            indicator, px(6), 0);
                        lv_obj_set_style_border_width(
                            indicator, px(2), 0);
                        lv_obj_set_style_border_color(
                            indicator,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_obj_set_style_bg_opa(
                            indicator, LV_OPA_TRANSP, 0);
                        lv_obj_set_style_bg_color(
                            indicator,
                            lv_color_make(0xD8, 0xB9, 0xFF),
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_CHECKED));
                        lv_obj_set_style_bg_opa(
                            indicator,
                            LV_OPA_COVER,
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_CHECKED));
                        lv_obj_set_style_border_color(
                            indicator,
                            lv_color_make(0xD8, 0xB9, 0xFF),
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_CHECKED));
                        if (node.value != 0) {
                            lv_obj_add_state(
                                indicator, LV_STATE_CHECKED);
                        }
                        auto* mark = factory.text(
                            indicator,
                            node.value != 0 ? LV_SYMBOL_OK : "",
                            generated::TypographyRole::label_large);
                        lv_obj_set_style_text_color(
                            mark,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                        lv_obj_center(mark);
                    }
                }
                break;
            case ComponentKind::voice_orb:
                object = factory.voice_orb(
                    parent, primary, tone(node.tone));
                if (voice_ready_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_add_flag(
                        object, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                    lv_obj_set_size(object, px(96), px(97));
                    lv_obj_set_pos(object, px(48), px(28));
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(object, 0, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* symbol = lv_obj_get_child(object, 0);
                        auto* state = lv_obj_get_child(object, 1);
                        auto* circle = lv_obj_create(object);
                        ComponentFactory::reset(circle);
                        lv_obj_add_flag(
                            circle, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(circle, px(76), px(76));
                        lv_obj_set_pos(circle, px(10), 0);
                        lv_obj_set_style_radius(
                            circle, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            circle,
                            lv_color_make(0x33, 0x2E, 0x3C),
                            0);
                        lv_obj_set_style_bg_opa(
                            circle, LV_OPA_COVER, 0);
                        lv_obj_set_style_border_width(
                            circle, px(3), 0);
                        lv_obj_set_style_border_color(
                            circle,
                            lv_color_make(0xD8, 0xB9, 0xFF),
                            0);
                        lv_obj_set_parent(symbol, circle);
                        lv_obj_set_parent(state, circle);
                        lv_obj_set_style_text_color(
                            symbol,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_color(
                            state,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_align(
                            symbol, LV_ALIGN_CENTER, 0, -px(8));
                        lv_obj_align(
                            state, LV_ALIGN_CENTER, 0, px(16));
                    }
                    auto* transcript = factory.text(
                        object,
                        secondary,
                        generated::TypographyRole::body_extra_small,
                        true);
                    lv_obj_add_flag(
                        transcript, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        transcript, px(96), px(20));
                    lv_obj_set_pos(
                        transcript, 0, px(78));
                    lv_obj_set_style_text_font(
                        transcript, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_color(
                        transcript,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        transcript, LV_TEXT_ALIGN_CENTER, 0);
                    lv_label_set_long_mode(
                        transcript, LV_LABEL_LONG_DOT);
                }
                break;
            case ComponentKind::keypad: {
                object = layout(factory, parent, node, false);
                if (canvas_game_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(48));
                    lv_obj_set_pos(object, px(4), px(136));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else {
                    lv_obj_set_height(object, 176);
                    lv_obj_set_style_pad_row(object, 4, 0);
                }
                for (std::uint8_t key_index = 0;
                     key_index < node.key_count;) {
                    WireNode row_node{};
                    row_node.kind = ComponentKind::row;
                    row_node.gap = 1;
                    row_node.alignment = 0;
                    auto* row =
                        layout(factory, object, row_node, false);
                    lv_obj_set_height(
                        row, canvas_game_document ? px(48) : 32);
                    lv_obj_set_style_pad_all(row, 0, 0);
                    lv_obj_set_style_pad_column(
                        row, canvas_game_document ? px(4) : 4, 0);
                    lv_obj_set_flex_align(
                        row,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    for (std::uint8_t column = 0;
                         column < node.key_columns &&
                         key_index < node.key_count;
                         ++column, ++key_index) {
                        const auto offset =
                            document.key_offsets[
                                node.key_start + key_index];
                        const auto* key_text =
                            document.string_at(offset);
                        const auto is_operator =
                            column == node.key_columns - 1;
                        const auto is_utility =
                            key_index < node.key_columns ||
                            std::strcmp(key_text, "<-") == 0;
                        const auto colors =
                            canvas_game_document
                                ? CalculatorKeyColors{
                                      column == 1
                                          ? lv_color_make(
                                                0xA8, 0xF2, 0x79)
                                          : lv_color_make(
                                                0x16, 0x30, 0x26),
                                      column == 1
                                          ? lv_color_make(
                                                0x07, 0x11, 0x0D)
                                          : lv_color_make(
                                                0xD5, 0xF5, 0xE4)}
                                : calculator_key_colors(
                                      is_operator, is_utility);
                        auto* key = factory.button(
                            row,
                            {
                                "key",
                                calculator_glyph(key_text),
                                is_operator
                                    ? Tone::primary
                                    : is_utility
                                        ? Tone::secondary
                                        : Tone::neutral,
                                ButtonVariant::tonal,
                                ComponentSize::compact,
                                node.enabled,
                                false,
                            });
                        lv_obj_set_height(
                            key, canvas_game_document ? px(48) : 32);
                        lv_obj_set_style_pad_all(key, 0, 0);
                        lv_obj_set_style_radius(
                            key,
                            canvas_game_document ? px(24) : 15,
                            0);
                        lv_obj_set_style_bg_color(
                            key, colors.container, 0);
                        lv_obj_set_style_transform_scale(
                            key,
                            241,
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_PRESSED));
                        lv_obj_set_style_opa(
                            key,
                            LV_OPA_COVER,
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_PRESSED));
                        lv_obj_set_flex_grow(key, 1);
                        if (lv_obj_get_child_count(key) == 1) {
                            auto* label = lv_obj_get_child(key, 0);
                            lv_obj_set_style_text_font(
                                label,
                                canvas_game_document
                                    ? &lv_font_montserrat_18
                                    : &m3e_calculator_font_20,
                                0);
                            lv_obj_set_style_text_color(
                                label, colors.content, 0);
                        }
                        if (event_sink != nullptr) {
                            for (std::size_t event_index = 0;
                                 event_index < document.event_count;
                                 ++event_index) {
                                auto& event =
                                    document.events[event_index];
                                if (event.node_index != index) {
                                    continue;
                                }
                                if (!bind_event(
                                        document,
                                        event,
                                        key,
                                        MountedEventValue::keypad_key,
                                        0,
                                        static_cast<std::uint16_t>(
                                            node.key_start +
                                            key_index))) {
                                    return false;
                                }
                            }
                        }
                    }
                }
                break;
            }
            case ComponentKind::canvas: {
                object = lv_canvas_create(parent);
                lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_pos(object, px(4), px(4));
                if (!render_canvas_display_list(
                        object,
                        primary,
                        secondary,
                        node.value,
                        node.maximum)) {
                    return false;
                }
                break;
            }
            case ComponentKind::image: {
                object = lv_obj_create(parent);
                ComponentFactory::reset(object);
                lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                const auto image_width =
                    camera_remote_document
                        ? px(184)
                        : wallet_qr_document ? px(108) : px(76);
                const auto image_height =
                    camera_remote_document ? px(120) : image_width;
                lv_obj_set_size(object, image_width, image_height);
                lv_obj_set_pos(
                    object,
                    wallet_qr_document ? px(42) : px(4),
                    wallet_qr_document ? px(26) : px(4));
                lv_obj_set_style_radius(
                    object,
                    wallet_qr_document ? px(12) : px(24),
                    0);
                lv_obj_set_style_clip_corner(object, true, 0);
                lv_obj_set_style_bg_color(
                    object,
                    lv_color_make(0x33, 0x2E, 0x3C),
                    0);
                lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);

                ImageAssetView asset{};
                if (resolve_image_asset(primary, asset)) {
                    auto* canvas = lv_canvas_create(object);
                    lv_canvas_set_buffer(
                        canvas,
                        const_cast<std::uint8_t*>(asset.pixels),
                        asset.width,
                        asset.height,
                        LV_COLOR_FORMAT_RGB565);
                    const auto width_scale =
                        (image_width * 256 + asset.width - 1) /
                        asset.width;
                    const auto height_scale =
                        (image_height * 256 + asset.height - 1) /
                        asset.height;
                    lv_image_set_scale(
                        canvas,
                        node.variant == 1
                            ? std::min(width_scale, height_scale)
                            : std::max(width_scale, height_scale));
                    lv_obj_center(canvas);
                } else {
                    auto* circle = lv_obj_create(object);
                    ComponentFactory::reset(circle);
                    lv_obj_set_size(circle, px(36), px(36));
                    lv_obj_center(circle);
                    lv_obj_set_style_radius(
                        circle, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        circle,
                        lv_color_make(0x7D, 0x52, 0x9C),
                        0);
                    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
                    auto* horizontal = lv_obj_create(object);
                    ComponentFactory::reset(horizontal);
                    lv_obj_set_size(
                        horizontal, px(22), px(3));
                    lv_obj_center(horizontal);
                    lv_obj_set_style_bg_color(
                        horizontal,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_bg_opa(
                        horizontal, LV_OPA_COVER, 0);
                    auto* vertical = lv_obj_create(object);
                    ComponentFactory::reset(vertical);
                    lv_obj_set_size(
                        vertical, px(3), px(22));
                    lv_obj_center(vertical);
                    lv_obj_set_style_bg_color(
                        vertical,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_bg_opa(
                        vertical, LV_OPA_COVER, 0);
                }
                break;
            }
            case ComponentKind::icon: {
                generated::WeatherIcon icon{};
                if (!weather_icon(
                        document.string_at(node.icon_offset), icon)) {
                    return false;
                }
                const auto logical_size =
                    node.size == 0 ? 18 :
                    node.size == 2 ? 32 :
                    node.size == 3 ? 64 : 24;
                object = create_weather_icon(
                    parent, icon, px(logical_size));
                break;
            }
            case ComponentKind::surface: {
                object = layout(factory, parent, node, false);
                lv_obj_set_style_pad_all(
                    object,
                    px(node.variant == 1 || node.variant == 5 ? 4 : 8),
                    0);
                lv_obj_set_style_bg_color(
                    object,
                    node.tone == 0
                        ? weather_color(
                              generated::WeatherColorRole::primary_container)
                        : node.tone == 1
                            ? weather_color(
                                  generated::WeatherColorRole::secondary_container)
                            : node.tone == 2
                                ? weather_color(
                                      generated::WeatherColorRole::surface_high)
                                : node.tone == 4
                                    ? weather_color(
                                          generated::WeatherColorRole::error_container)
                                    : weather_color(
                                          generated::WeatherColorRole::surface),
                    0);
                lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
                const auto radius =
                    node.variant == 1 ? px(28) :
                    node.variant == 2 ? px(22) :
                    node.variant == 3 ? px(24) :
                    node.variant == 5 ? LV_RADIUS_CIRCLE : px(18);
                lv_obj_set_style_radius(object, radius, 0);
                if (node.variant == 4) {
                    lv_obj_set_style_radius(object, px(10), 0);
                }
                break;
            }
            case ComponentKind::chart: {
                object = lv_chart_create(parent);
                ComponentFactory::reset(object);
                lv_obj_set_size(object, LV_PCT(100), px(32));
                lv_chart_set_type(
                    object,
                    node.variant == 1
                        ? LV_CHART_TYPE_BAR
                        : LV_CHART_TYPE_LINE);
                lv_chart_set_point_count(object, node.sample_count);
                lv_chart_set_range(
                    object,
                    LV_CHART_AXIS_PRIMARY_Y,
                    0,
                    node.maximum);
                lv_chart_set_div_line_count(object, 0, 0);
                lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
                lv_obj_set_style_line_width(object, px(2), LV_PART_ITEMS);
                lv_obj_set_style_width(object, px(4), LV_PART_INDICATOR);
                lv_obj_set_style_height(object, px(4), LV_PART_INDICATOR);
                auto* series = lv_chart_add_series(
                    object,
                    node.tone == 2
                        ? weather_color(generated::WeatherColorRole::tertiary)
                        : node.tone == 4
                            ? weather_color(generated::WeatherColorRole::error)
                            : weather_color(generated::WeatherColorRole::rain),
                    LV_CHART_AXIS_PRIMARY_Y);
                for (std::size_t sample = 0;
                     sample < node.sample_count; ++sample) {
                    lv_chart_set_next_value(
                        object, series, node.samples[sample]);
                }
                break;
            }
            case ComponentKind::pager: {
                object = layout(factory, parent, node, false);
                lv_obj_set_style_pad_bottom(
                    object, node.checked ? px(8) : 0, 0);
                if (node.checked && node.maximum > 1) {
                    auto* indicator = lv_obj_create(object);
                    ComponentFactory::reset(indicator);
                    lv_obj_add_flag(indicator, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        indicator,
                        px(node.maximum * 8),
                        px(6));
                    lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, 0);
                    lv_obj_set_flex_flow(indicator, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(
                        indicator,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_gap(indicator, px(3), 0);
                    for (std::int32_t page = 0;
                         page < node.maximum; ++page) {
                        auto* dot = lv_obj_create(indicator);
                        ComponentFactory::reset(dot);
                        const auto dot_size = px(page == node.value ? 5 : 3);
                        lv_obj_set_size(dot, dot_size, dot_size);
                        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            dot,
                            page == node.value
                                ? weather_color(generated::WeatherColorRole::primary)
                                : weather_color(generated::WeatherColorRole::outline_variant),
                            0);
                        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                    }
                }
                break;
            }
            case ComponentKind::screen:
                return false;
        }
        if (object == nullptr) return false;
        lv_obj_set_user_data(
            object,
            const_cast<char*>(
                document.string_at(node.id_offset)));
        if (weather_current_document) {
            configure_weather_current_node(
                document, node, object, parent);
        }
        if (!node.visible) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        const auto& parent_node = document.nodes[node.parent_index];
        if (parent_node.kind == ComponentKind::pager) {
            std::size_t page = 0;
            for (std::size_t previous = node.parent_index + 1;
                 previous < index; ++previous) {
                if (document.nodes[previous].parent_index == node.parent_index) {
                    ++page;
                }
            }
            if (page != static_cast<std::size_t>(parent_node.value)) {
                lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (parent_node.kind == ComponentKind::row &&
            parent_node.alignment == 3) {
            lv_obj_set_flex_grow(object, 1);
        } else if (
            parent_node.kind == ComponentKind::row &&
            node.kind == ComponentKind::text &&
            weather_component_document) {
            lv_obj_set_width(object, LV_SIZE_CONTENT);
        }
        if (!node.enabled) lv_obj_add_state(object, LV_STATE_DISABLED);
        if (event_sink != nullptr) {
            for (std::size_t event_index = 0;
                 event_index < document.event_count;
                 ++event_index) {
                auto& event = document.events[event_index];
                if (event.node_index != index) continue;
                event.document = &document;
                event.sink = event_sink;
                event.sink_context = event_context;
                if (node.kind == ComponentKind::keypad) {
                    continue;
                }
                if (node.kind == ComponentKind::stepper) {
                    if (lv_obj_get_child_count(object) < 3 ||
                        !bind_event(
                            document,
                            event,
                            lv_obj_get_child(object, 0),
                            MountedEventValue::stepper_decrement) ||
                        !bind_event(
                            document,
                            event,
                            lv_obj_get_child(object, 2),
                            MountedEventValue::stepper_increment)) {
                        return false;
                    }
                    continue;
                }
                if (node.kind == ComponentKind::pager) {
                    if (!bind_event(
                            document,
                            event,
                            object,
                            MountedEventValue::node_value) ||
                        !bind_pager_gesture(
                            document, event, object)) {
                        return false;
                    }
                    continue;
                }
                if (!bind_event(
                        document,
                        event,
                        object,
                        node.kind == ComponentKind::toggle
                            ? MountedEventValue::checked_state
                            : MountedEventValue::none)) {
                    return false;
                }
            }
        }
        objects[index] = object;
        node.mounted_object = object;
    }
    return true;
}

}  // namespace m3e::appspec
