#pragma once
#include "sdkconfig.h"
#include "esp_attr.h"
#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "Ultra LVGL pool requires external BSS placement"
#endif
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY EXT_RAM_BSS_ATTR
