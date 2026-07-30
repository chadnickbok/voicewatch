#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOODAD_SURFACE_WIDTH = 240,
    DOODAD_SURFACE_HEIGHT = 240,
    DOODAD_HEADER_HEIGHT = 36,
    DOODAD_FOOTER_HEIGHT = 24,
};

typedef struct {
    lv_obj_t* screen;
    lv_obj_t* header;
    lv_obj_t* title;
    lv_obj_t* status;
    lv_obj_t* content;
    lv_obj_t* footer;
    lv_obj_t* footer_abi;
    lv_obj_t* source;
} doodad_lvgl_ui_t;

void doodad_lvgl_ui_init(doodad_lvgl_ui_t* ui, lv_obj_t* screen);
void doodad_lvgl_ui_show_shell(
    doodad_lvgl_ui_t* ui, const char* status, const char* source);
void doodad_lvgl_ui_show_text(doodad_lvgl_ui_t* ui, const char* text);
void doodad_lvgl_ui_show_error(doodad_lvgl_ui_t* ui, const char* stage);
lv_obj_t* doodad_lvgl_ui_content(doodad_lvgl_ui_t* ui);

#ifdef __cplusplus
}
#endif
