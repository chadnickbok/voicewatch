#pragma once
#include "voice_moq_protocol.hpp"
namespace doodad::moq_control {
bool init();
bool acquire(Grant& grant);
bool sign_renewal(const Grant& grant,const char* nonce,char (&proof)[65]);
bool verify_renewal(const Grant& grant,const cJSON* payload,const char* nonce,
                   std::uint64_t revision,std::uint64_t started_ms,Renewal& out);
bool commit_renewal(Grant& grant,const Renewal& renewal);
bool clock_valid();
bool authorization_rejected();
std::uint32_t profile_revision();
struct ArtifactTrust {
    char roots[kRootsCapacity]{};
    std::uint32_t revision=0;
};
bool artifact_trust(const char* url,const char* digest,ArtifactTrust& out);
}
