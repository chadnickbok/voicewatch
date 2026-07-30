#pragma once

#include <cstddef>
#include <cstdint>

struct AppImage {
    const std::uint8_t* data;
    std::size_t size;
    const char* source;
};
