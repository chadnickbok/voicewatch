#include <cstdint>
#include <cstring>
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
#include "package_service.hpp"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::size_t kRuntimeThreadStackBytes = 16 * 1024;
constexpr std::uint32_t kHeartbeatIntervalMilliseconds = 60 * 1000;
constexpr std::uint32_t kRuntimePollMilliseconds = 50;
constexpr TickType_t kUiUpdateInterval = pdMS_TO_TICKS(2);

bool run_installed_package(
    const doodad::packages::LaunchRequest& request,
    std::vector<std::uint8_t>& storage) {
    if (!doodad::packages::package_service_load(request, storage)) {
        ESP_LOGE(
            kTag,
            "[packages] could not load %s generation %.12s",
            request.app_id.data(),
            request.payload_sha256.data());
        return false;
    }
    if (!display_prepare_app_switch(request.theme_seed.data())) {
        ESP_LOGE(kTag, "[packages] could not close system overlay for app switch");
        return false;
    }
    const AppImage image{
        .data = storage.data(),
        .size = storage.size(),
        .source = "PERSONAL",
        .app_id = request.app_id.data(),
        .semantic_version = request.semantic_version.data(),
        .generation = request.payload_sha256.data(),
    };
    if (!run_app(image)) {
        ESP_LOGE(
            kTag,
            "[packages] generation failed to start: %s %s %.12s",
            request.app_id.data(),
            request.semantic_version.data(),
            request.payload_sha256.data());
        return false;
    }
    if (!doodad::packages::package_service_mark_running(request)) {
        // The registry may have advanced again while WAMR copied and started
        // this generation. The guest is already valid and resident; volatile
        // bookkeeping must not tear it down.
        ESP_LOGW(
            kTag,
            "[packages] running generation is no longer in the two-slot registry: %s %.12s",
            request.app_id.data(),
            request.payload_sha256.data());
    }
    display_note_app_running(
        request.app_id.data(),
        request.semantic_version.data(),
        request.payload_sha256.data());
    ESP_LOGI(
        kTag,
        "[packages] running %s %s %.12s without reboot",
        request.app_id.data(),
        request.semantic_version.data(),
        request.payload_sha256.data());
    return true;
}

bool restore_previous_package(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    std::vector<std::uint8_t>& storage,
    bool& rollback_selected) {
    rollback_selected = false;
    doodad::packages::LaunchRequest previous{};
    if (!doodad::packages::package_service_rollback(
            app_id,
            failed_semantic_version,
            failed_payload_sha256,
            previous)) {
        return false;
    }
    rollback_selected = true;
    ESP_LOGW(
        kTag,
        "[packages] rolling back %s from %.12s to %.12s",
        app_id,
        failed_payload_sha256,
        previous.payload_sha256.data());
    if (!run_installed_package(previous, storage)) return false;
    display_publish_app_rollback(
        previous.app_id.data(),
        previous.name.data(),
        previous.semantic_version.data(),
        previous.icon.data(),
        previous.theme_seed.data(),
        previous.payload_sha256.data());
    return true;
}

bool recover_safe_current_package(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    std::vector<std::uint8_t>& storage) {
    doodad::packages::LaunchRequest current{};
    if (!doodad::packages::package_service_recover_current(
            app_id,
            failed_semantic_version,
            failed_payload_sha256,
            current)) {
        return false;
    }
    ESP_LOGW(
        kTag,
        "[packages] failed resident %s %s %.12s is not registry current; recovering to current %s %.12s",
        app_id,
        failed_semantic_version,
        failed_payload_sha256,
        current.semantic_version.data(),
        current.payload_sha256.data());
    AppRuntimeIdentity resident{};
    if (app_runtime_current_identity(resident) &&
        std::strcmp(resident.app_id, current.app_id.data()) == 0 &&
        std::strcmp(
            resident.semantic_version,
            current.semantic_version.data()) == 0 &&
        std::strcmp(
            resident.generation,
            current.payload_sha256.data()) == 0) {
        ESP_LOGW(
            kTag,
            "[packages] safe current is already resident; consumed stale failed request without restart");
        return true;
    }
    if (!run_installed_package(current, storage)) {
        // The safe-current target is now itself known to have failed. It may
        // roll back only to a different, non-quarantined retained generation.
        bool rollback_selected = false;
        return restore_previous_package(
            current.app_id.data(),
            current.semantic_version.data(),
            current.payload_sha256.data(),
            storage,
            rollback_selected);
    }
    display_publish_app_current_recovery(
        current.app_id.data(),
        current.name.data(),
        current.semantic_version.data(),
        current.icon.data(),
        current.theme_seed.data(),
        current.payload_sha256.data());
    return true;
}

bool recover_installed_package(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    std::vector<std::uint8_t>& storage) {
    bool rollback_selected = false;
    if (restore_previous_package(
            app_id,
            failed_semantic_version,
            failed_payload_sha256,
            storage,
            rollback_selected)) {
        return true;
    }
    // Once registry rollback selected a target, a failure to run that target
    // is terminal for this bounded attempt. Do not reinterpret the newly
    // current-but-failed target as a stale resident and launch it twice.
    return !rollback_selected && recover_safe_current_package(
        app_id,
        failed_semantic_version,
        failed_payload_sha256,
        storage);
}

void enter_recovery_guest() {
    if (!run_app(embedded_app_image())) {
        ESP_LOGE(kTag, "[packages] embedded recovery guest failed");
    }
    display_show_system_home();
}

void* runtime_thread(void*) {
    // Mount package storage before networking can accept app.ready. The voice
    // task starts independently from app_main, so delaying this until after a
    // guest starts creates a real offer-loss race on fast reconnects.
    if (!doodad::packages::package_service_init()) {
        ESP_LOGW(
            kTag,
            "[packages] personal app storage unavailable; native shell remains usable");
    }

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
    }

    if (!running) {
        ESP_LOGI(kTag, "[host] using embedded recovery app");
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
        if (app_post_embedded_ui_event(reference_event)) {
            app_runtime_update(0);
        } else {
            ESP_LOGE(
                kTag,
                "[host] reference semantic event injection failed");
        }
    }

    // The guest remains loaded, but the trusted host shell owns the root
    // surface. Packages are entered from the launcher rather than replacing
    // the watch face at boot. Hardware conformance builds keep the selected
    // embedded app visible so the same package can be photographed directly.
#if DOODAD_BOOT_CATALOG_STORY >= 0
    display_show_catalog(DOODAD_BOOT_CATALOG_STORY);
#elif !defined(DOODAD_SHOW_APP_AT_BOOT)
    display_show_system_home();
#endif

    ESP_LOGI(kTag, "[host] steady state; free heap: %u bytes",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    std::uint32_t milliseconds_until_heartbeat =
        kHeartbeatIntervalMilliseconds;
    while (true) {
        doodad::packages::LaunchRequest requested{};
        if (doodad::packages::package_service_poll_launch(requested)) {
            if (!run_installed_package(requested, package_storage) &&
                !recover_installed_package(
                    requested.app_id.data(),
                    requested.semantic_version.data(),
                    requested.payload_sha256.data(),
                    package_storage)) {
                ESP_LOGE(
                    kTag,
                    "[packages] launch and bounded recovery failed: %s",
                    requested.app_id.data());
                display_error("APP LAUNCH FAILED");
                enter_recovery_guest();
            }
            continue;
        }
        app_runtime_update(kRuntimePollMilliseconds);
        AppRuntimeFailure failure{};
        if (app_runtime_poll_failure(failure)) {
            ESP_LOGE(
                kTag,
                "[packages] guest failure kind=%u app=%s generation=%.12s",
                static_cast<unsigned>(failure.kind),
                failure.identity.app_id,
                failure.identity.generation);
            if (!recover_installed_package(
                    failure.identity.app_id,
                    failure.identity.semantic_version,
                    failure.identity.generation,
                    package_storage)) {
                display_error("APP CRASHED");
                enter_recovery_guest();
            }
            continue;
        }
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
