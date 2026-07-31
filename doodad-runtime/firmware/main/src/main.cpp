#include <cstdint>
#include <pthread.h>
#include <vector>

#include "app_runner.hpp"
#include "app_sources.hpp"
#include "display.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::size_t kRuntimeThreadStackBytes = 16 * 1024;
constexpr std::uint32_t kHeartbeatIntervalMilliseconds = 60 * 1000;
constexpr std::uint32_t kRuntimePollMilliseconds = 50;
constexpr TickType_t kUiUpdateInterval = pdMS_TO_TICKS(2);

void* runtime_thread(void*) {
    if (!app_runtime_init()) {
        return nullptr;
    }

    std::vector<std::uint8_t> package_storage;
    AppImage selected{};
    bool running = false;
    bool running_embedded = false;

    if (load_onboard_app(package_storage, selected)) {
        running = run_app(selected);
        if (!running) {
            ESP_LOGW(
                kTag,
                "[host] onboard package failed; trying microSD");
        }
    }

    if (!running && load_microsd_app(package_storage, selected)) {
        running = run_app(selected);
        if (!running) {
            ESP_LOGW(kTag, "[host] microSD app failed; trying embedded recovery app");
        }
    } else {
        ESP_LOGI(kTag, "[host] using embedded recovery app");
    }

    if (!running) {
        running = run_app(embedded_app_image());
        running_embedded = running;
    }

    if (!running) {
        ESP_LOGE(kTag, "[host] no runnable app remains");
        return nullptr;
    }

    // The embedded development fixture exercises the complete actor path on
    // every flashed build. Desktop `doodad test hello` supplies the event via
    // LVGL; hardware boot injects the same semantic envelope so unattended
    // serial validation also proves host→WAMR→in-place CommandBatch behavior.
    if (running_embedded) {
        const m3e::appspec::UiEvent reference_event{
            1,
            "hello",
            "hello.screen",
            "hello.action",
            "say_hello",
            m3e::appspec::EventKind::tap,
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000),
        };
        if (app_post_ui_event(reference_event)) {
            app_runtime_update(0);
        } else {
            ESP_LOGE(
                kTag,
                "[host] reference semantic event injection failed");
        }
    }

    // The guest remains loaded, but the trusted host shell owns the root
    // surface. Packages are entered from the launcher rather than replacing
    // the watch face at boot.
    display_show_system_home();

    ESP_LOGI(kTag, "[host] steady state; free heap: %u bytes",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    std::uint32_t milliseconds_until_heartbeat =
        kHeartbeatIntervalMilliseconds;
    while (true) {
        app_runtime_update(kRuntimePollMilliseconds);
        if (milliseconds_until_heartbeat > kRuntimePollMilliseconds) {
            milliseconds_until_heartbeat -= kRuntimePollMilliseconds;
        } else {
            ESP_LOGI(
                kTag,
                "[host] uptime heartbeat; free heap: %u bytes",
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_8BIT)));
            milliseconds_until_heartbeat = kHeartbeatIntervalMilliseconds;
        }
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
    while (true) {
        display_update();
        vTaskDelay(kUiUpdateInterval);
    }
}
