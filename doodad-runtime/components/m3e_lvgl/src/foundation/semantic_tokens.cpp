#include "m3e/foundation/semantic_tokens.hpp"

#include <algorithm>
#include <cmath>

namespace m3e {

std::int16_t spacing_dp(SpacingRole role) {
    constexpr std::int16_t values[] = {0, 4, 8, 12, 16, 24};
    return values[static_cast<std::uint8_t>(role)];
}

std::uint8_t state_opacity(StateKind state) {
    switch (state) {
        case StateKind::pressed: return 31;   // 12%
        case StateKind::focused: return 26;   // 10%
        case StateKind::selected: return 20;  // 8%
        case StateKind::disabled: return 97;  // 38%
        case StateKind::dragged: return 41;   // 16%
        case StateKind::loading:
        case StateKind::pending: return 18;
        case StateKind::error:
        case StateKind::enabled: return 0;
    }
    return 0;
}

MotionSpec motion_spec(MotionToken token, bool reduced_motion) {
    if (reduced_motion) {
        return MotionSpec{100, 1400, 256, false, false};
    }
    switch (token) {
        case MotionToken::state_fast:
            return {100, 1400, 256, false, false};
        case MotionToken::state_default:
            return {200, 500, 256, false, false};
        case MotionToken::spatial_fast:
            return {200, 800, 179, true, true};
        case MotionToken::spatial_default:
        case MotionToken::container_transform:
        case MotionToken::list_item_transform:
            return {350, 350, 192, true, true};
        case MotionToken::spatial_slow:
        case MotionToken::voice_pulse:
            return {600, 200, 205, true, true};
        case MotionToken::emphasized_enter:
        case MotionToken::dialog_enter:
            return {400, 350, 192, true, true};
        case MotionToken::emphasized_exit:
        case MotionToken::dialog_exit:
            return {200, 500, 256, true, false};
    }
    return {200, 500, 256, false, false};
}

HapticPattern haptic_pattern(
    HapticEvent event,
    bool reduced_feedback) {
    HapticPattern pattern{};
    switch (event) {
        case HapticEvent::selection_tick:
            pattern = {12, 0, 80, 1};
            break;
        case HapticEvent::step_increment:
            pattern = {16, 0, 96, 1};
            break;
        case HapticEvent::step_limit:
            pattern = {24, 0, 150, 1};
            break;
        case HapticEvent::action_commit:
            pattern = {28, 0, 170, 1};
            break;
        case HapticEvent::action_reject:
            pattern = {24, 36, 180, 2};
            break;
        case HapticEvent::success:
        case HapticEvent::build_complete:
            pattern = {22, 42, 150, 2};
            break;
        case HapticEvent::warning:
            pattern = {32, 52, 170, 2};
            break;
        case HapticEvent::error:
            pattern = {40, 44, 210, 3};
            break;
        case HapticEvent::voice_start:
            pattern = {20, 0, 130, 1};
            break;
        case HapticEvent::voice_stop:
            pattern = {14, 30, 110, 2};
            break;
    }
    if (reduced_feedback) {
        pattern.intensity = static_cast<std::uint8_t>(
            std::max<std::uint16_t>(1, pattern.intensity / 2));
        pattern.pulse_count = 1;
        pattern.gap_ms = 0;
    }
    return pattern;
}

std::uint16_t motion_progress_q16(
    MotionToken token,
    std::uint32_t elapsed_ms,
    bool reduced_motion) {
    const auto spec = motion_spec(token, reduced_motion);
    if (elapsed_ms >= spec.duration_ms) {
        return 65535;
    }
    const double t =
        static_cast<double>(elapsed_ms) / static_cast<double>(spec.duration_ms);
    double value = t * t * (3.0 - 2.0 * t);
    if (spec.may_overshoot && !reduced_motion) {
        const double envelope = (1.0 - t) * (1.0 - t);
        value += std::sin(t * 3.141592653589793 * 2.0) * envelope * 0.08;
    }
    value = std::clamp(value, 0.0, 1.0);
    return static_cast<std::uint16_t>(value * 65535.0 + 0.5);
}

}  // namespace m3e
