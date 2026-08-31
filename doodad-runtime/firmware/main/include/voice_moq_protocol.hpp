#pragma once
#include <cstddef>
#include <cstdint>
#include "cJSON.h"

namespace doodad::moq_control {
constexpr std::size_t kRootsCapacity = 4097;
struct Profile {
    std::uint32_t version = 1, revision = 0;
    char device[65]{}, host[254]{}, roots[kRootsCapacity]{};
    std::uint16_t control_port = 0, time_port = 0;
    std::uint8_t key[32]{};
};
struct Grant {
    char session[33]{}, host[254]{}, roots[kRootsCapacity]{};
    char setup[80]{}, publish[160]{}, subscribe[160]{};
    char websocket_url[320]{}, headers[80]{};
    std::uint16_t port = 0;
    std::uint32_t profile_revision = 0;
    std::uint64_t until_ms = 0, trusted_until_ms = 0;
};
struct Renewal {
    std::uint64_t revision=0, until_ms=0, trusted_until_ms=0;
};
using Hmac = bool (*)(const std::uint8_t*, const std::uint8_t*, std::size_t, std::uint8_t*);
void wipe(void* bytes, std::size_t size);
cJSON* json(const char* bytes, std::size_t size, std::size_t limit = 16384);
bool decimal(const cJSON* value, std::uint64_t& out, bool zero = true);
bool number(const cJSON* value, std::uint64_t& out, std::uint64_t max);
bool valid_profile(const Profile& profile, const char* device);
bool profile(const cJSON* root, const char* device, Profile& out);
bool time_proof(const cJSON* root, const Profile& profile, const char* nonce,
                std::uint64_t round_trip_ms, Hmac hmac, std::uint64_t& unix_ms);
bool bootstrap_proof(const Profile& profile, const char* challenge, Hmac hmac, char (&proof)[65]);
bool renewal_proof(const Profile& profile,const char* session,const char* nonce,Hmac hmac,char (&proof)[65]);
bool renewal(const cJSON* root,const Profile& profile,const char* nonce,std::uint64_t next_revision,
             std::uint64_t started_ms,std::uint64_t now_ms,std::uint64_t unix_ms,
             const Grant& current,Hmac hmac,Renewal& out);
bool grant(const cJSON* root, const Profile& profile, std::uint64_t unix_ms,
           std::uint64_t request_ms, std::uint64_t now_ms,
           std::uint64_t trusted_until_ms, Grant& out);
bool envelope(const cJSON* root, const char* device, const char* session, std::uint64_t& sequence);
}
