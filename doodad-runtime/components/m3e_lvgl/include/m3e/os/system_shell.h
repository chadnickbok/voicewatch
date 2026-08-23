#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    M3E_SYSTEM_SHELL_MAX_LAUNCHER_ACTIONS = 32,
    M3E_SYSTEM_SHELL_MAX_AGENT_ACTIONS = 8,
    M3E_SYSTEM_SHELL_VOICE_BAR_COUNT = 5,
};

enum {
    M3E_SYSTEM_SHELL_APP_ICON_GENERIC = 0,
    M3E_SYSTEM_SHELL_APP_ICON_TIMER = 1,
    M3E_SYSTEM_SHELL_APP_ICON_WEATHER = 2,
    M3E_SYSTEM_SHELL_APP_ICON_TASKS = 3,
    M3E_SYSTEM_SHELL_APP_ICON_CALCULATOR = 4,
    M3E_SYSTEM_SHELL_APP_ICON_CALENDAR = 5,
    M3E_SYSTEM_SHELL_APP_ICON_WATER_DROP = 6,
};

enum {
    M3E_SYSTEM_SHELL_SURFACE_WATCH_FACE = 0,
    M3E_SYSTEM_SHELL_SURFACE_LIVE_CARDS = 1,
    M3E_SYSTEM_SHELL_SURFACE_LAUNCHER = 2,
    M3E_SYSTEM_SHELL_SURFACE_CONTROL_CENTER = 3,
    M3E_SYSTEM_SHELL_SURFACE_APP = 4,
    M3E_SYSTEM_SHELL_SURFACE_APP_MANAGER = 5,
    M3E_SYSTEM_SHELL_SURFACE_APP_DETAIL = 6,
    M3E_SYSTEM_SHELL_SURFACE_INSTALL_PROGRESS = 7,
    M3E_SYSTEM_SHELL_SURFACE_CRASH_RECOVERY = 8,
    M3E_SYSTEM_SHELL_SURFACE_AGENTS = 9,
    M3E_SYSTEM_SHELL_SURFACE_AGENT_DETAIL = 10,
};

enum {
    M3E_SYSTEM_SHELL_OVERLAY_NONE = 0,
    M3E_SYSTEM_SHELL_OVERLAY_VOICE = 1,
};

enum {
    M3E_SYSTEM_SHELL_INTENT_BACK = 0,
    M3E_SYSTEM_SHELL_INTENT_HOME_OR_LAUNCHER = 1,
    M3E_SYSTEM_SHELL_INTENT_OPEN_VOICE = 2,
    M3E_SYSTEM_SHELL_INTENT_OPEN_AGENTS = 3,
    M3E_SYSTEM_SHELL_INTENT_OPEN_AGENT_DETAIL = 4,
};

enum {
    M3E_SYSTEM_SHELL_AGENT_ICON_APP_BUILDER = 0,
    M3E_SYSTEM_SHELL_AGENT_ICON_RESEARCH = 1,
    M3E_SYSTEM_SHELL_AGENT_ICON_MONITORING = 2,
    M3E_SYSTEM_SHELL_AGENT_ICON_PRESENTATION = 3,
};

enum {
    M3E_SYSTEM_SHELL_VOICE_IDLE = 0,
    M3E_SYSTEM_SHELL_VOICE_LISTENING = 1,
    M3E_SYSTEM_SHELL_VOICE_THINKING = 2,
    M3E_SYSTEM_SHELL_VOICE_SPEAKING = 3,
    M3E_SYSTEM_SHELL_VOICE_CLARIFYING = 4,
    M3E_SYSTEM_SHELL_VOICE_ERROR = 5,
    M3E_SYSTEM_SHELL_VOICE_READY = 6,
};

typedef struct {
    const char* time;
    const char* weekday;
    const char* calendar_date;
    const char* weather;
    const char* battery;
    uint8_t agent_count;
    bool agent_status_changed;
    bool live_clock;
} m3e_system_shell_home_model_t;

typedef struct {
    lv_obj_t* apps_action;
    lv_obj_t* voice_action;
    lv_obj_t* agents_action;
} m3e_system_shell_home_view_t;

typedef struct {
    const char* app_id;
    const char* name;
    const char* detail;
    // Required package-owned color encoded as 0xRRGGBB.
    uint32_t primary_color_rgb;
    // Host-derived accessible icon color encoded as 0xRRGGBB.
    uint32_t on_primary_color_rgb;
    // Required semantic icon from M3E_SYSTEM_SHELL_APP_ICON_*.
    uint8_t icon;
} m3e_system_shell_launcher_item_t;

typedef struct {
    size_t action_count;
    lv_obj_t* actions[M3E_SYSTEM_SHELL_MAX_LAUNCHER_ACTIONS];
} m3e_system_shell_launcher_view_t;

typedef struct {
    const char* task_id;
    const char* title;
    const char* status;
    const char* elapsed;
    uint32_t primary_color_rgb;
    uint8_t icon;
} m3e_system_shell_agent_item_t;

typedef struct {
    size_t action_count;
    lv_obj_t* actions[M3E_SYSTEM_SHELL_MAX_AGENT_ACTIONS];
} m3e_system_shell_agents_view_t;

typedef struct {
    const char* title;
    const char* status;
    const char* elapsed;
    const char* context_label;
    const char* context;
    const char* stages[4];
    uint8_t completed_stage_count;
    uint8_t active_stage;
    uint8_t progress_percent;
    uint32_t primary_color_rgb;
    uint8_t icon;
} m3e_system_shell_agent_detail_model_t;

typedef struct {
    lv_obj_t* back_action;
} m3e_system_shell_agent_detail_view_t;

typedef struct {
    lv_obj_t* primary_action;
    lv_obj_t* cancel_action;
    lv_obj_t* level_ring;
    lv_obj_t* level_bars[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT];
} m3e_system_shell_voice_view_t;

typedef struct m3e_system_shell_controller m3e_system_shell_controller_t;

void m3e_system_shell_default_home_model(
    m3e_system_shell_home_model_t* model);
void m3e_system_shell_show_home(
    lv_obj_t* screen,
    const m3e_system_shell_home_model_t* model,
    m3e_system_shell_home_view_t* view);
void m3e_system_shell_show_launcher(
    lv_obj_t* screen,
    const m3e_system_shell_launcher_item_t* items,
    size_t item_count,
    m3e_system_shell_launcher_view_t* view);
void m3e_system_shell_show_agents(
    lv_obj_t* screen,
    const m3e_system_shell_agent_item_t* items,
    size_t item_count,
    uint8_t active_count,
    m3e_system_shell_agents_view_t* view);
void m3e_system_shell_show_agent_detail(
    lv_obj_t* screen,
    const m3e_system_shell_agent_detail_model_t* model,
    m3e_system_shell_agent_detail_view_t* view);
void m3e_system_shell_show_voice_overlay(
    lv_obj_t* screen,
    int phase,
    const char* transcript,
    const char* response,
    m3e_system_shell_voice_view_t* view);

m3e_system_shell_controller_t* m3e_system_shell_controller_create(void);
void m3e_system_shell_controller_destroy(
    m3e_system_shell_controller_t* controller);
int m3e_system_shell_controller_initialize(
    m3e_system_shell_controller_t* controller);
int m3e_system_shell_controller_dispatch(
    m3e_system_shell_controller_t* controller,
    int intent);
int m3e_system_shell_controller_open_app(
    m3e_system_shell_controller_t* controller,
    const char* app_id,
    uint32_t generation);
int m3e_system_shell_controller_surface(
    const m3e_system_shell_controller_t* controller);
int m3e_system_shell_controller_overlay(
    const m3e_system_shell_controller_t* controller);
int m3e_system_shell_controller_voice_phase(
    const m3e_system_shell_controller_t* controller);

#ifdef __cplusplus
}
#endif
