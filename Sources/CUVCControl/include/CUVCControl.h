#ifndef CLEANCAM_UVC_CONTROL_H
#define CLEANCAM_UVC_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct CleanCamUVCHandle CleanCamUVCHandle;

typedef struct {
    int minimum;
    int maximum;
    int current;
    int default_value;
} CleanCamUVCRange;

int cleancam_uvc_open(uint16_t vendor_id, uint16_t product_id, CleanCamUVCHandle **out_handle);
void cleancam_uvc_close(CleanCamUVCHandle *handle);

int cleancam_uvc_get_exposure(CleanCamUVCHandle *handle, CleanCamUVCRange *out_range);
int cleancam_uvc_set_exposure(CleanCamUVCHandle *handle, uint32_t value);
int cleancam_uvc_get_gain(CleanCamUVCHandle *handle, CleanCamUVCRange *out_range);
int cleancam_uvc_set_gain(CleanCamUVCHandle *handle, uint16_t value);
int cleancam_uvc_get_white_balance_temperature(
    CleanCamUVCHandle *handle,
    CleanCamUVCRange *out_range
);
int cleancam_uvc_set_white_balance_temperature(
    CleanCamUVCHandle *handle,
    uint16_t value
);
int cleancam_uvc_get_focus(
    CleanCamUVCHandle *handle,
    CleanCamUVCRange *out_range
);
int cleancam_uvc_set_focus(CleanCamUVCHandle *handle, uint16_t value);
int cleancam_uvc_set_auto_focus(CleanCamUVCHandle *handle, bool enabled);
int cleancam_uvc_get_auto_exposure(CleanCamUVCHandle *handle, bool *out_enabled);
int cleancam_uvc_set_auto_exposure(CleanCamUVCHandle *handle, bool enabled);
int cleancam_uvc_disable_backlight_compensation(CleanCamUVCHandle *handle);
int cleancam_uvc_reset_device(uint16_t vendor_id, uint16_t product_id);

const char *cleancam_uvc_error_string(int code);

#endif
