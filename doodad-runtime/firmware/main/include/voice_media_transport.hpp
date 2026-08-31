#pragma once

#include <cstddef>
#include <cstdint>

// Internal media seam. Product control, guest routing, actions, NVS, packages
// and LVGL stay in voice_service. Exactly one implementation is linked.
namespace doodad::voice_media {

struct Identity {
    std::uint64_t capture_id = 0;
    std::uint64_t request_id = 0;
    std::uint64_t owner_token = 0;
};

enum class Signal { description, candidate };
using SignalSink = bool (*)(Signal, const char*, std::size_t);

enum class EventKind {
    ready, disconnected, capture_started, capture_stopped, capture_failed,
    playback_bound, playback_started, playback_finished, error,
};

struct Event {
    EventKind kind{};
    std::uint64_t session = 0;
    Identity identity{};
    // MoQ group boundaries are exclusive at the end. These fields let the
    // authenticated control peer bind the corresponding media operation.
    std::uint64_t first_group = 0, end_group = 0;
    std::uint64_t response_id = 0, samples = 0;
    std::uint32_t elapsed_ms = 0, encoded_frames = 0, dropped_frames = 0;
    std::uint32_t encoded_bytes = 0;
    int error = 0;
    bool cancelled = false;
};

struct Session {
    // Monotonic control-session generation. Zero and reused generations fail.
    // Strings are borrowed only during connect(), which copies them.
    std::uint64_t generation = 0;
    const char* host = nullptr;
    std::uint16_t port = 0;
    const char* roots_pem = nullptr;
    const char* setup_path = nullptr;
    const char* local_broadcast = nullptr;
    const char* remote_broadcast = nullptr;
    // Produced by authenticated bootstrap/time policy, not untrusted JSON or
    // SNTP. TLS additionally verifies the actual platform UTC clock.
    std::uint64_t authorization_valid_until_ms = 0;
    std::uint64_t trusted_time_valid_until_ms = 0;
};

struct Response {
    std::uint64_t session = 0, response_id = 0;
    Identity identity{};
    std::uint64_t first_group = 0, end_group = 0;
    bool has_end = false;
};

bool init(SignalSink signaling);
const char* name();
const char* codec_description();
// WebRTC uses only generation; MoQ requires all authenticated session fields.
bool connect(const Session& session);
void disconnect();
bool signal(Signal kind, const char* bytes, std::size_t size);
bool capture_begin(Identity identity, std::uint32_t duration_ms);
bool capture_finish();
// Cancels both media directions, including DMA handoffs; never starts capture.
void cancel();
// Only an explicitly authenticated response binding may enable MoQ playback.
bool receive_begin(const Response& response);
bool receive_end(std::uint64_t session, std::uint64_t response_id,
                 std::uint64_t end_group);
// Called by the control owner. MoQ performs audio/network work on other tasks.
void tick();
bool poll(Event& event);
bool ready();
bool recording();

}  // namespace doodad::voice_media
