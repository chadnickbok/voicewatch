#pragma once

#include <cstddef>
#include <cstdint>

struct AppImage {
    const std::uint8_t* data;
    std::size_t size;
    const char* source;
    // Package identity is copied by run_app(), so callers only need to keep
    // these pointers alive for the duration of that call. Trailing defaults
    // preserve the existing three-field aggregate initializers used by the
    // embedded and legacy package sources.
    const char* app_id = nullptr;
    const char* semantic_version = nullptr;
    const char* generation = nullptr;
};
