#pragma once

#include <cstdint>

namespace m3e {

enum class SpacingRole : std::uint8_t {
    none,
    xs,
    sm,
    md,
    lg,
    xl,
};

enum class MotionToken : std::uint8_t {
    state_fast,
    state_default,
    spatial_fast,
    spatial_default,
    spatial_slow,
    emphasized_enter,
    emphasized_exit,
    container_transform,
    list_item_transform,
    dialog_enter,
    dialog_exit,
    voice_pulse,
};

enum class StateKind : std::uint8_t {
    enabled,
    pressed,
    focused,
    selected,
    disabled,
    dragged,
    loading,
    error,
    pending,
};

enum class HapticEvent : std::uint8_t {
    selection_tick,
    step_increment,
    step_limit,
    action_commit,
    action_reject,
    success,
    warning,
    error,
    voice_start,
    voice_stop,
    build_complete,
};

struct MotionSpec {
    std::uint16_t duration_ms;
    std::uint16_t stiffness;
    std::uint16_t damping_ratio_q8_8;
    bool spatial;
    bool may_overshoot;
};

struct HapticPattern {
    std::uint16_t pulse_ms;
    std::uint16_t gap_ms;
    std::uint8_t intensity;
    std::uint8_t pulse_count;
};

std::int16_t spacing_dp(SpacingRole role);
std::uint8_t state_opacity(StateKind state);
MotionSpec motion_spec(MotionToken token, bool reduced_motion = false);
HapticPattern haptic_pattern(
    HapticEvent event,
    bool reduced_feedback = false);

// Deterministic, allocation-free normalized motion sample in Q0.16.
std::uint16_t motion_progress_q16(
    MotionToken token,
    std::uint32_t elapsed_ms,
    bool reduced_motion = false);

}  // namespace m3e
