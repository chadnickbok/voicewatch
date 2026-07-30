#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOODAD_TEXT_STYLE_DISPLAY = 0,
    DOODAD_TEXT_STYLE_TITLE = 1,
    DOODAD_TEXT_STYLE_BODY = 2,
    DOODAD_TEXT_STYLE_CAPTION = 3,
    DOODAD_TEXT_STYLE_MUTED = 4,
};

enum {
    DOODAD_DIRECTION_COLUMN = 0,
    DOODAD_DIRECTION_ROW = 1,
};

enum {
    DOODAD_ALIGN_START = 0,
    DOODAD_ALIGN_CENTER = 1,
    DOODAD_ALIGN_END = 2,
    DOODAD_ALIGN_STRETCH = 3,
};

int doodad_host_create(void);
void doodad_host_destroy(void);
const char* doodad_host_last_error(void);

int doodad_host_start_wasm(const char* path);
void doodad_host_render_now(void);
const uint16_t* doodad_host_framebuffer(void);
size_t doodad_host_framebuffer_pixels(void);
void doodad_host_show_catalog(int story);
int doodad_host_show_appspec(const uint8_t* bytes, size_t size);
int doodad_host_click_first_action(void);

void* doodad_host_ui_begin_document(int direction, int align, int gap);
void* doodad_host_ui_add_stack(
    void* parent, int direction, int align, int gap);
void* doodad_host_ui_add_text(void* parent, const char* text, int style);
void* doodad_host_ui_add_button(
    void* parent, const char* identifier, const char* label, int disabled);
void* doodad_host_ui_add_progress(
    void* parent, const char* label, int value, int maximum);

#ifdef __cplusplus
}
#endif
