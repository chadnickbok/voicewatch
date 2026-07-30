#pragma once

#include <cstddef>

bool display_init();
void display_shell(const char* status, const char* source);
void display_guest_text(const char* text, std::size_t length);
void display_error(const char* stage);
