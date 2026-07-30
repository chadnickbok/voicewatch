#include "doodad_lvgl_ui.h"

#include <string.h>

static const uint32_t kBackground = 0x080B12;
static const uint32_t kPanel = 0x101A2A;
static const uint32_t kPrimary = 0xF4F7FB;
static const uint32_t kMuted = 0x92A4BC;
static const uint32_t kAccent = 0x41D9B2;
static const uint32_t kError = 0xFF6B7A;

static void reset_object(lv_obj_t* object) {
    lv_obj_remove_style_all(object);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    reset_object(label);
    lv_label_set_text(label, text);
    return label;
}

void doodad_lvgl_ui_init(doodad_lvgl_ui_t* ui, lv_obj_t* screen) {
    memset(ui, 0, sizeof(*ui));
    ui->screen = screen;

    reset_object(screen);
    lv_obj_set_size(screen, DOODAD_SURFACE_WIDTH, DOODAD_SURFACE_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    ui->header = lv_obj_create(screen);
    reset_object(ui->header);
    lv_obj_set_pos(ui->header, 0, 0);
    lv_obj_set_size(ui->header, DOODAD_SURFACE_WIDTH, DOODAD_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(ui->header, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(ui->header, LV_OPA_COVER, 0);

    ui->title = make_label(ui->header, "DOODAD");
    lv_obj_set_pos(ui->title, 10, 8);
    lv_obj_set_style_text_color(ui->title, lv_color_hex(kPrimary), 0);
    lv_obj_set_style_text_font(ui->title, &lv_font_montserrat_18, 0);

    ui->status = make_label(ui->header, "STARTING");
    lv_obj_align(ui->status, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(ui->status, lv_color_hex(kAccent), 0);
    lv_obj_set_style_text_font(ui->status, &lv_font_montserrat_10, 0);

    ui->content = lv_obj_create(screen);
    reset_object(ui->content);
    lv_obj_set_pos(ui->content, 0, DOODAD_HEADER_HEIGHT);
    lv_obj_set_size(
        ui->content,
        DOODAD_SURFACE_WIDTH,
        DOODAD_SURFACE_HEIGHT - DOODAD_HEADER_HEIGHT - DOODAD_FOOTER_HEIGHT);
    lv_obj_set_style_bg_color(ui->content, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(ui->content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui->content, 10, 0);

    ui->footer = lv_obj_create(screen);
    reset_object(ui->footer);
    lv_obj_set_pos(
        ui->footer, 0, DOODAD_SURFACE_HEIGHT - DOODAD_FOOTER_HEIGHT);
    lv_obj_set_size(ui->footer, DOODAD_SURFACE_WIDTH, DOODAD_FOOTER_HEIGHT);
    lv_obj_set_style_bg_color(ui->footer, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(ui->footer, LV_OPA_COVER, 0);

    ui->footer_abi = make_label(ui->footer, "HOST ABI v1");
    lv_obj_align(ui->footer_abi, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(ui->footer_abi, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(ui->footer_abi, &lv_font_montserrat_10, 0);

    ui->source = make_label(ui->footer, "NATIVE");
    lv_obj_align(ui->source, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(ui->source, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(ui->source, &lv_font_montserrat_10, 0);
}

void doodad_lvgl_ui_show_shell(
    doodad_lvgl_ui_t* ui, const char* status, const char* source) {
    lv_label_set_text(ui->status, status);
    lv_label_set_text(ui->source, source);
}

void doodad_lvgl_ui_show_text(doodad_lvgl_ui_t* ui, const char* text) {
    lv_obj_clean(ui->content);
    lv_obj_t* label = make_label(ui->content, text);
    lv_obj_set_width(label, DOODAD_SURFACE_WIDTH - 28);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kPrimary), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);
}

void doodad_lvgl_ui_show_error(doodad_lvgl_ui_t* ui, const char* stage) {
    doodad_lvgl_ui_show_shell(ui, "ERROR", "NATIVE");
    lv_obj_clean(ui->content);
    lv_obj_t* label = make_label(ui->content, stage);
    lv_obj_set_width(label, DOODAD_SURFACE_WIDTH - 28);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kError), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
}

lv_obj_t* doodad_lvgl_ui_content(doodad_lvgl_ui_t* ui) {
    return ui->content;
}
