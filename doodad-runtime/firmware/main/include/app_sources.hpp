#pragma once

#include <cstdint>
#include <vector>

#include "app_image.hpp"

AppImage embedded_app_image();
bool load_onboard_app(std::vector<std::uint8_t>& storage, AppImage& image);
bool load_microsd_app(std::vector<std::uint8_t>& storage, AppImage& image);
