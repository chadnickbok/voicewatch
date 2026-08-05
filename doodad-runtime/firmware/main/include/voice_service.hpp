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
    // Zero denotes a host-owned/system voice turn and is never forwarded to
    // a guest. Guest-originated turns carry the resident runtime token
    // supplied with the request so a live app switch can discard completions
    // from the outgoing WAMR instance.
    std::uint64_t owner_token;
    std::uint32_t elapsed_ms;
    std::uint32_t encoded_frames;
    std::uint32_t dropped_frames;
    std::array<char, 193> text;
};

bool voice_service_init();
bool voice_service_request(
    const char* operation,
    std::uint64_t request_id,
    std::uint32_t duration_ms = 8'000,
    std::uint64_t owner_token = 0);
// Publishes the one resident guest generation that an explicitly targeted
// remote diagnostic capture may bind to. Zero means no guest target; ordinary
// live-agent/system capture remains native-owned regardless of this value.
void voice_service_set_current_guest_owner(std::uint64_t owner_token);
// Queues behind any already-accepted commands from `owner_token`, then stops
// capture/playback only if that exact guest still owns the active turn.
// Owner zero is the trusted native voice surface and is never released here.
bool voice_service_release_owner(std::uint64_t owner_token);
bool voice_service_poll(VoiceEvent& event);
bool voice_service_busy();
bool voice_service_ready();
