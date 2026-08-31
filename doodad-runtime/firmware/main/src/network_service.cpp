#include "network_service.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "network-service";
constexpr EventBits_t kConnected = BIT0;
constexpr EventBits_t kFailed = BIT1;

EventGroupHandle_t g_events = nullptr;
bool g_initialized = false;
bool g_sntp_initialized = false;
unsigned g_retry_count = 0;

const char* wifi_ssid() {
#if defined(CONFIG_DOODAD_WIFI_SSID)
    if (CONFIG_DOODAD_WIFI_SSID[0] != '\0') return CONFIG_DOODAD_WIFI_SSID;
#endif
#if defined(CONFIG_DOODAD_WEATHER_WIFI_SSID)
    return CONFIG_DOODAD_WEATHER_WIFI_SSID;
#else
    return "";
#endif
}

const char* wifi_password() {
#if defined(CONFIG_DOODAD_WIFI_PASSWORD)
    if (CONFIG_DOODAD_WIFI_PASSWORD[0] != '\0') return CONFIG_DOODAD_WIFI_PASSWORD;
#endif
#if defined(CONFIG_DOODAD_WEATHER_WIFI_PASSWORD)
    return CONFIG_DOODAD_WEATHER_WIFI_PASSWORD;
#else
    return "";
#endif
}

void event_handler(void*, esp_event_base_t base, std::int32_t event_id, void*) {
    if (g_events == nullptr) return;
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        g_retry_count = 0;
        xEventGroupClearBits(g_events, kFailed);
        xEventGroupSetBits(g_events, kConnected);
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_events, kConnected);
        if (g_retry_count < 5) {
            ++g_retry_count;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(g_events, kFailed);
        }
    }
}

template <std::size_t Size>
void copy_configuration(std::uint8_t (&destination)[Size], const char* source) {
    const auto length = std::min(std::strlen(source), Size - 1);
    std::memcpy(destination, source, length);
    destination[length] = 0;
}

}  // namespace

bool network_service_init() {
    if (g_initialized) return true;
    if (wifi_ssid()[0] == '\0') {
        ESP_LOGE(kTag, "Wi-Fi SSID is not configured");
        return false;
    }
    auto status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
        status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(kTag, "NVS needs explicit recovery; preserving device/user data");
        return false;
    }
    if (status != ESP_OK) return false;
    status = esp_netif_init();
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) return false;
    status = esp_event_loop_create_default();
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) return false;
    if (esp_netif_create_default_wifi_sta() == nullptr) return false;

    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&initialization) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return false;
    }
    g_events = xEventGroupCreate();
    if (g_events == nullptr) return false;
    if (esp_event_handler_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr) != ESP_OK ||
        esp_event_handler_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, nullptr) != ESP_OK) {
        return false;
    }

    wifi_config_t configuration{};
    copy_configuration(configuration.sta.ssid, wifi_ssid());
    copy_configuration(configuration.sta.password, wifi_password());
    configuration.sta.threshold.authmode = WIFI_AUTH_OPEN;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &configuration) != ESP_OK ||
        esp_wifi_start() != ESP_OK ||
        // WebRTC's ICE and RTP traffic is latency-sensitive. Disabling modem
        // sleep also avoids the CoreS3 repeatedly entering null-frame power
        // save exchanges while the ICE agent gathers its host candidate.
        esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
        return false;
    }
    g_initialized = true;
    ESP_LOGI(kTag, "shared Wi-Fi station initialized");
    return true;
}

bool network_service_connected() {
    return g_events != nullptr &&
           (xEventGroupGetBits(g_events) & kConnected) != 0;
}

bool network_service_connect(std::uint32_t timeout_ms) {
    if (!network_service_init()) return false;
    if (network_service_connected()) return true;
    g_retry_count = 0;
    xEventGroupClearBits(g_events, kFailed);
    if (esp_wifi_connect() != ESP_OK) return false;
    const auto bits = xEventGroupWaitBits(
        g_events,
        kConnected | kFailed,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & kConnected) != 0;
}

void network_service_sync_time(std::uint32_t timeout_ms) {
#if defined(CONFIG_DOODAD_TIMEZONE)
    setenv("TZ", CONFIG_DOODAD_TIMEZONE, 1);
    tzset();
#endif
    if (!network_service_connected()) return;
    if (!g_sntp_initialized) {
        esp_sntp_config_t configuration =
            ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&configuration) == ESP_OK) {
            g_sntp_initialized = true;
        }
    }
    if (g_sntp_initialized) {
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    }
}
