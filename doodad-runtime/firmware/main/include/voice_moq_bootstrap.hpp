#pragma once
#include "voice_moq_protocol.hpp"
namespace doodad::moq_control {
bool init();
bool acquire(Grant& grant);
bool clock_valid();
bool authorization_rejected();
std::uint32_t profile_revision();
}
