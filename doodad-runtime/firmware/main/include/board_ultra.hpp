#pragma once
#include "twatch_ultra.h"
namespace doodad::board {
// Board-owned handle for the MoQ audio owner. Never close it from voice code.
// Use its timestamped capture and nonblocking generation-fenced speaker API.
twatch_ultra_t* ultra_audio_board();
}
