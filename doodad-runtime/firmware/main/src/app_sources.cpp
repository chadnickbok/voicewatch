#include "app_sources.hpp"

#include <cstdio>

#include "board.hpp"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "package_service.hpp"
#include "sdmmc_cmd.h"

namespace {

constexpr char kTag[] = "doodad";
constexpr char kMountPoint[] = "/sdcard";
constexpr char kAppPath[] = "/sdcard/doodad/hello.wasm";
constexpr char kOnboardMountPoint[] = "/packages";
constexpr char kOnboardPartition[] = "packages";
constexpr char kOnboardAppPath[] = "/packages/active.wasm";
constexpr std::size_t kMaximumModuleBytes = 256 * 1024;

extern const std::uint8_t embedded_hello_start[]
    asm("_binary_hello_wasm_start");
extern const std::uint8_t embedded_hello_end[]
    asm("_binary_hello_wasm_end");

bool load_file(
    const char* path,
    std::vector<std::uint8_t>& storage) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return false;
    bool loaded = false;
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long file_size = std::ftell(file);
        if (file_size > 0 &&
            static_cast<std::size_t>(file_size) <= kMaximumModuleBytes &&
            std::fseek(file, 0, SEEK_SET) == 0) {
            storage.resize(static_cast<std::size_t>(file_size));
            loaded =
                std::fread(storage.data(), 1, storage.size(), file) ==
                storage.size();
        } else {
            ESP_LOGW(
                kTag,
                "[host] package has invalid size: %ld",
                file_size);
        }
    }
    std::fclose(file);
    if (!loaded) storage.clear();
    return loaded;
}

}  // namespace

AppImage embedded_app_image() {
    return AppImage{
        .data = embedded_hello_start,
        .size = static_cast<std::size_t>(embedded_hello_end - embedded_hello_start),
        .source = "EMBEDDED",
    };
}

bool load_onboard_app(
    std::vector<std::uint8_t>& storage,
    AppImage& image) {
    // The package service owns the long-lived /packages mount. Reuse it when
    // available so the legacy active.wasm fallback cannot unmount the registry
    // underneath the installer or launcher.
    if (doodad::packages::package_service_mounted()) {
        if (!load_file(kOnboardAppPath, storage)) {
            ESP_LOGI(
                kTag,
                "[host] no legacy onboard package at %s",
                kOnboardAppPath);
            return false;
        }
        image = AppImage{
            .data = storage.data(),
            .size = storage.size(),
            .source = "ONBOARD-LEGACY",
        };
        ESP_LOGI(
            kTag,
            "[host] legacy onboard package loaded: %u bytes",
            static_cast<unsigned>(image.size));
        return true;
    }

    esp_vfs_fat_mount_config_t mount_config =
        VFS_FAT_MOUNT_DEFAULT_CONFIG();
    // Installed packages are user data. A transient mount failure must never
    // erase the registry or its current/previous generations.
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 4 * 1024;
    wl_handle_t wear_level_handle = WL_INVALID_HANDLE;
    const auto mount_result = esp_vfs_fat_spiflash_mount_rw_wl(
        kOnboardMountPoint,
        kOnboardPartition,
        &mount_config,
        &wear_level_handle);
    if (mount_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "[host] onboard package storage unavailable: %s",
            esp_err_to_name(mount_result));
        return false;
    }

    std::uint64_t total = 0;
    std::uint64_t free = 0;
    if (esp_vfs_fat_info(
            kOnboardMountPoint, &total, &free) == ESP_OK) {
        ESP_LOGI(
            kTag,
            "[host] onboard package storage: %llu KiB free / %llu KiB",
            static_cast<unsigned long long>(free / 1024),
            static_cast<unsigned long long>(total / 1024));
    }

    const bool loaded = load_file(kOnboardAppPath, storage);
    if (!loaded) {
        ESP_LOGI(
            kTag,
            "[host] no legacy onboard package at %s",
            kOnboardAppPath);
    }
    esp_vfs_fat_spiflash_unmount_rw_wl(
        kOnboardMountPoint, wear_level_handle);
    if (!loaded) return false;

    image = AppImage{
        .data = storage.data(),
        .size = storage.size(),
        .source = "ONBOARD-LEGACY",
    };
    ESP_LOGI(
        kTag,
        "[host] legacy onboard package loaded: %u bytes",
        static_cast<unsigned>(image.size));
    return true;
}

bool load_microsd_app(std::vector<std::uint8_t>& storage, AppImage& image) {
    if (!doodad::board::has_microsd()) {
        ESP_LOGI(kTag, "[host] board has no microSD fallback");
        return false;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);
    slot_config.gpio_cs = GPIO_NUM_4;

    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 2;
    mount_config.allocation_unit_size = 4 * 1024;

    sdmmc_card_t* card = nullptr;
    const esp_err_t mount_result =
        esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
    if (mount_result != ESP_OK) {
        ESP_LOGW(kTag, "[host] microSD unavailable: %s", esp_err_to_name(mount_result));
        return false;
    }

    ESP_LOGI(kTag, "[host] microSD mounted");
    const bool loaded = load_file(kAppPath, storage);
    if (!loaded) {
        ESP_LOGW(kTag, "[host] microSD app missing: %s", kAppPath);
        esp_vfs_fat_sdcard_unmount(kMountPoint, card);
        return false;
    }
    esp_vfs_fat_sdcard_unmount(kMountPoint, card);

    image = AppImage{
        .data = storage.data(),
        .size = storage.size(),
        .source = "MICROSD",
    };
    ESP_LOGI(kTag, "[host] microSD app loaded: %u bytes",
             static_cast<unsigned>(image.size));
    return true;
}
