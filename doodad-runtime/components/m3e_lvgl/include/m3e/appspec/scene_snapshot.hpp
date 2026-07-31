#pragma once

#include <cstddef>

#include "m3e/appspec/wire.hpp"

namespace m3e::appspec {

// Serializes only renderer-neutral, accepted AppSpec state. The function
// allocates no memory and follows the standard two-pass convention: callers
// may pass a null output to query the required byte count.
std::size_t scene_snapshot_json(
    const WireDocument& document,
    char* output,
    std::size_t output_size,
    const char* origin = "guest_appspec");

// Serializes the post-layout LVGL evidence corresponding to the accepted
// document. Renderer/run metadata is added by the desktop evidence producer.
std::size_t node_layout_evidence_json(
    const WireDocument& document,
    char* output,
    std::size_t output_size);

}  // namespace m3e::appspec
