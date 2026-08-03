#pragma once

#include <cstddef>
#include <cstdint>

namespace m3e {

struct ImageAssetView {
    const std::uint8_t* pixels = nullptr;
    std::size_t decoded_bytes = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

bool resolve_image_asset(
    const char* sha256,
    ImageAssetView& output);

}  // namespace m3e
