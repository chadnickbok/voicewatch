#include <cstdint>
#include <pthread.h>
#include <vector>

#include "app_runner.hpp"
#include "app_sources.hpp"
#include "display.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::size_t kRuntimeThreadStackBytes = 32 * 1024;
constexpr TickType_t kHeartbeatInterval = pdMS_TO_TICKS(60 * 1000);

void* runtime_thread(void*) {
    if (!app_runtime_init()) {
        return nullptr;
    }

    std::vector<std::uint8_t> sd_storage;
    AppImage selected{};
    bool running = false;

    if (load_microsd_app(sd_storage, selected)) {
        running = run_app(selected);
        if (!running) {
            ESP_LOGW(kTag, "[host] microSD app failed; trying embedded recovery app");
        }
    } else {
        ESP_LOGI(kTag, "[host] using embedded recovery app");
    }

    if (!running) {
        running = run_app(embedded_app_image());
    }

    if (!running) {
        ESP_LOGE(kTag, "[host] no runnable app remains");
        return nullptr;
    }

    ESP_LOGI(kTag, "[host] steady state; free heap: %u bytes",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    while (true) {
        vTaskDelay(kHeartbeatInterval);
        ESP_LOGI(kTag, "[host] uptime heartbeat; free heap: %u bytes",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    }
}

}  // namespace

extern "C" void app_main() {
    ESP_LOGI(kTag, "[host] boot");

    if (!display_init()) {
        ESP_LOGE(kTag, "[host] DISPLAY INIT FAILED");
        return;
    }
    ESP_LOGI(kTag, "[host] display ready");
    display_shell("STARTING", "NATIVE");

    pthread_attr_t attributes;
    int result = pthread_attr_init(&attributes);
    if (result != 0) {
        ESP_LOGE(kTag, "[host] pthread attribute initialization failed: %d", result);
        display_error("RUNTIME START FAILED");
        return;
    }

    result = pthread_attr_setstacksize(&attributes, kRuntimeThreadStackBytes);
    pthread_t thread;
    if (result == 0) {
        result = pthread_create(&thread, &attributes, runtime_thread, nullptr);
    }
    pthread_attr_destroy(&attributes);
    if (result != 0) {
        ESP_LOGE(kTag, "[host] runtime pthread creation failed: %d", result);
        display_error("RUNTIME START FAILED");
        return;
    }

    ESP_LOGI(kTag, "[host] runtime pthread started");
    pthread_join(thread, nullptr);
    ESP_LOGE(kTag, "[host] runtime pthread exited unexpectedly");
}
