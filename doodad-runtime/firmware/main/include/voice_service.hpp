#pragma once

#include <array>
#include <cstdint>

enum class VoiceEventKind : std::uint8_t {
    connecting,
    ready,
    recording,
    stopped,
    transcript,
    error,
};

struct VoiceEvent {
    VoiceEventKind kind;
    std::uint64_t request_id;
    std::uint32_t elapsed_ms;
    std::uint32_t encoded_frames;
    std::uint32_t dropped_frames;
    std::array<char, 193> text;
};

bool voice_service_init();
bool voice_service_request(
    const char* operation,
    std::uint64_t request_id,
    std::uint32_t duration_ms = 8'000);
bool voice_service_poll(VoiceEvent& event);
bool voice_service_busy();
bool voice_service_ready();
