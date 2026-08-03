#include "m3e/services/provider_event_c.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

bool contains(
    const std::uint8_t* bytes,
    std::size_t size,
    const char* value) {
    const auto value_size = std::strlen(value);
    for (std::size_t index = 0; index + value_size <= size; ++index) {
        if (std::memcmp(bytes + index, value, value_size) == 0) return true;
    }
    return false;
}

}  // namespace

int main() {
    std::array<std::uint8_t, 512> first{};
    std::array<std::uint8_t, 512> second{};
    const auto first_size = m3e_encode_voice_provider_event(
        4, 17, 8000, 399, 0, "Voice streaming works", 9, 1234,
        first.data(), first.size());
    const auto second_size = m3e_encode_voice_provider_event(
        4, 17, 8000, 399, 0, "Voice streaming works", 9, 1234,
        second.data(), second.size());
    assert(first_size > 0 && first_size == second_size);
    assert(std::memcmp(first.data(), second.data(), first_size) == 0);
    assert(contains(first.data(), first_size, "audio"));
    assert(contains(first.data(), first_size, "voice.changed"));
    assert(contains(first.data(), first_size, "Voice streaming works"));

    assert(m3e_encode_voice_provider_event(
               6, 17, 0, 0, 0, "invalid", 9, 0,
               first.data(), first.size()) == 0);
    assert(m3e_encode_voice_provider_event(
               4, 17, 0, 0, 0, "invalid", 0, 0,
               first.data(), first.size()) == 0);
    const std::string long_text(193, 'x');
    assert(m3e_encode_voice_provider_event(
               4, 17, 0, 0, 0, long_text.c_str(), 9, 0,
               first.data(), first.size()) == 0);
    assert(m3e_encode_voice_provider_event(
               4, 17, 0, 0, 0, "too small", 9, 0,
               first.data(), 8) == 0);
    return 0;
}
