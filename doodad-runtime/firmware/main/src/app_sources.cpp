#include "app_sources.hpp"

#include <cstdio>

#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {

constexpr char kTag[] = "doodad";
constexpr char kMountPoint[] = "/sdcard";
constexpr char kAppPath[] = "/sdcard/doodad/hello.wasm";
constexpr std::size_t kMaximumModuleBytes = 256 * 1024;

extern const std::uint8_t embedded_hello_start[]
    asm("_binary_hello_wasm_start");
extern const std::uint8_t embedded_hello_end[]
    asm("_binary_hello_wasm_end");

}  // namespace

AppImage embedded_app_image() {
    return AppImage{
        .data = embedded_hello_start,
        .size = static_cast<std::size_t>(embedded_hello_end - embedded_hello_start),
        .source = "EMBEDDED",
    };
}

bool load_microsd_app(std::vector<std::uint8_t>& storage, AppImage& image) {
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
    std::FILE* file = std::fopen(kAppPath, "rb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "[host] microSD app missing: %s", kAppPath);
        esp_vfs_fat_sdcard_unmount(kMountPoint, card);
        return false;
    }

    bool loaded = false;
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long file_size = std::ftell(file);
        if (file_size > 0 && static_cast<std::size_t>(file_size) <= kMaximumModuleBytes
            && std::fseek(file, 0, SEEK_SET) == 0) {
            storage.resize(static_cast<std::size_t>(file_size));
            loaded = std::fread(storage.data(), 1, storage.size(), file) == storage.size();
        } else {
            ESP_LOGW(kTag, "[host] microSD app has invalid size: %ld", file_size);
        }
    }

    std::fclose(file);
    esp_vfs_fat_sdcard_unmount(kMountPoint, card);

    if (!loaded) {
        storage.clear();
        ESP_LOGW(kTag, "[host] microSD app read failed");
        return false;
    }

    image = AppImage{
        .data = storage.data(),
        .size = storage.size(),
        .source = "MICROSD",
    };
    ESP_LOGI(kTag, "[host] microSD app loaded: %u bytes",
             static_cast<unsigned>(image.size));
    return true;
}
