#include "package_service.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "display.hpp"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "package_registry.hpp"
#include "personal_bundle.hpp"
#include "sdkconfig.h"

namespace doodad::packages {
namespace {

constexpr char kTag[] = "package-service";
constexpr char kMountPoint[] = "/packages";
#if CONFIG_DOODAD_BOARD_TWATCH_ULTRA
constexpr char kPartition[] = "ffat"; // Inspected Ultra layout; never repartition as bring-up cleanup.
#else
constexpr char kPartition[] = "packages";
#endif
constexpr std::size_t kOfferQueueDepth = kMaximumInstalledApps;
constexpr std::size_t kLaunchQueueDepth = 4;
constexpr std::size_t kInstallerStackBytes = 12 * 1024;
constexpr std::uint32_t kMaximumBundleBytes =
    kPersonalBundleHeaderBytes + kMaximumBundleMetadataBytes +
    kMaximumBundlePayloadBytes + kSha256Bytes;
constexpr std::size_t kMaximumRunnableWasmBytes =
    kMaximumBundlePayloadBytes;
constexpr std::size_t kDownloadBlockBytes = 4096;
constexpr std::size_t kMaximumGenerationEntryBytes =
    (kSha256Bytes * 2) + (sizeof(".installing") - 1);

#if !defined(CONFIG_FATFS_LFN_HEAP) && !defined(CONFIG_FATFS_LFN_STACK)
#error "Personal package storage requires FATFS long filename support"
#endif
static_assert(
    CONFIG_FATFS_MAX_LFN >= kMaximumGenerationEntryBytes,
    "FATFS_MAX_LFN is too small for personal package generation paths");

SemaphoreHandle_t g_lock = nullptr;
SemaphoreHandle_t g_files_lock = nullptr;
QueueHandle_t g_offers = nullptr;
QueueHandle_t g_launches = nullptr;
TaskHandle_t g_installer_task = nullptr;
wl_handle_t g_wear_level = WL_INVALID_HANDLE;
bool g_mounted = false;
bool g_install_enabled = false;
std::uint32_t g_revision = 0;
PersonalTrustProfile g_trust{};
std::unique_ptr<PersonalPackageStore> g_store;
PackageRegistry g_registry{};

class LockGuard {
  public:
    LockGuard() : held_(g_lock != nullptr &&
        xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {}
    ~LockGuard() {
        if (held_) xSemaphoreGive(g_lock);
    }
    explicit operator bool() const { return held_; }

  private:
    bool held_;
};

class FileLockGuard {
  public:
    FileLockGuard() : held_(g_files_lock != nullptr &&
        xSemaphoreTake(g_files_lock, portMAX_DELAY) == pdTRUE) {}
    ~FileLockGuard() {
        if (held_) xSemaphoreGive(g_files_lock);
    }
    explicit operator bool() const { return held_; }

  private:
    bool held_;
};

template <std::size_t Size>
bool copy_text(std::array<char, Size>& destination, const std::string& source) {
    if (source.empty() || source.size() >= destination.size()) return false;
    std::memcpy(destination.data(), source.c_str(), source.size() + 1);
    return true;
}

template <std::size_t Size>
bool copy_text(std::array<char, Size>& destination, const char* source) {
    return source != nullptr && copy_text(destination, std::string(source));
}

bool same_text(const char* left, const std::string& right) {
    return left != nullptr && right == left;
}

bool digest_text(const char* value) {
    if (value == nullptr) return false;
    Sha256Digest ignored{};
    return parse_sha256_hex(value, ignored);
}

bool ensure_directory(const char* path) {
    if (::mkdir(path, 0755) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat status {};
    return ::stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

bool package_partition_is_erased() {
    const auto* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        kPartition);
    if (partition == nullptr) return false;
    std::array<std::uint8_t, 1024> block{};
    for (std::size_t offset = 0; offset < partition->size;
         offset += block.size()) {
        const auto count = std::min<std::size_t>(
            block.size(), partition->size - offset);
        if (esp_partition_read(partition, offset, block.data(), count) != ESP_OK ||
            !std::all_of(
                block.begin(),
                block.begin() + count,
                [](std::uint8_t value) { return value == 0xFF; })) {
            return false;
        }
    }
    return true;
}

void abandon_package_mount() {
    g_store.reset();
    g_registry = {};
    g_trust = {};
    g_install_enabled = false;
    g_mounted = false;
    if (g_wear_level == WL_INVALID_HANDLE) return;
    const auto unmounted = esp_vfs_fat_spiflash_unmount_rw_wl(
        kMountPoint, g_wear_level);
    if (unmounted != ESP_OK) {
        ESP_LOGE(
            kTag,
            "package storage unmount after init failure: %s",
            esp_err_to_name(unmounted));
    }
    g_wear_level = WL_INVALID_HANDLE;
}

bool decode_key(const char* hexadecimal, std::vector<std::uint8_t>& key) {
    key.clear();
    if (hexadecimal == nullptr) return false;
    const auto size = std::strlen(hexadecimal);
    if (size != 64) return false;
    key.reserve(size / 2);
    const auto nibble = [](char character, std::uint8_t& value) {
        if (character >= '0' && character <= '9') {
            value = static_cast<std::uint8_t>(character - '0');
            return true;
        }
        if (character >= 'a' && character <= 'f') {
            value = static_cast<std::uint8_t>(character - 'a' + 10);
            return true;
        }
        if (character >= 'A' && character <= 'F') {
            value = static_cast<std::uint8_t>(character - 'A' + 10);
            return true;
        }
        return false;
    };
    for (std::size_t index = 0; index < size; index += 2) {
        std::uint8_t high = 0;
        std::uint8_t low = 0;
        if (!nibble(hexadecimal[index], high) ||
            !nibble(hexadecimal[index + 1], low)) {
            key.clear();
            return false;
        }
        key.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool fill_launch(
    const InstalledApp& app,
    const PackageGeneration& generation,
    LaunchRequest& request) {
    request = {};
    return copy_text(request.app_id, app.app_id) &&
        copy_text(request.name, generation.name) &&
        copy_text(request.semantic_version, generation.semantic_version) &&
        copy_text(request.icon, generation.icon) &&
        copy_text(request.theme_seed, generation.theme_seed) &&
        copy_text(request.payload_sha256, generation.payload_sha256);
}

bool generation_is_known(const char* bundle_sha256) {
    for (const auto& app : g_registry.apps()) {
        if (same_text(bundle_sha256, app.current.bundle_sha256) ||
            (app.previous.has_value() &&
             same_text(bundle_sha256, app.previous->bundle_sha256))) {
            return true;
        }
    }
    return false;
}

bool download(const AppReadyOffer& offer, const std::string& path) {
    esp_http_client_config_t configuration{};
    configuration.url = offer.url.data();
    configuration.timeout_ms = 15'000;
    configuration.buffer_size = kDownloadBlockBytes;
    configuration.crt_bundle_attach = esp_crt_bundle_attach;
    configuration.keep_alive_enable = false;
    auto* client = esp_http_client_init(&configuration);
    if (client == nullptr) return false;

    bool ok = esp_http_client_open(client, 0) == ESP_OK;
    const auto content_length = ok ? esp_http_client_fetch_headers(client) : -1;
    ok = ok && esp_http_client_get_status_code(client) == 200 &&
        content_length == static_cast<std::int64_t>(offer.bundle_bytes);
    auto* output = ok ? std::fopen(path.c_str(), "wb") : nullptr;
    ok = output != nullptr;
    std::array<char, kDownloadBlockBytes> block{};
    std::uint32_t received = 0;
    while (ok && received < offer.bundle_bytes) {
        const auto wanted = std::min<std::size_t>(
            block.size(), offer.bundle_bytes - received);
        const int count = esp_http_client_read(
            client, block.data(), static_cast<int>(wanted));
        if (count <= 0 || static_cast<std::size_t>(count) > wanted) {
            ok = false;
            break;
        }
        ok = std::fwrite(block.data(), 1, count, output) ==
            static_cast<std::size_t>(count);
        received += static_cast<std::uint32_t>(count);
    }
    ok = ok && received == offer.bundle_bytes &&
        std::fflush(output) == 0 && ::fsync(::fileno(output)) == 0;
    if (output != nullptr) ok = std::fclose(output) == 0 && ok;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!ok) std::remove(path.c_str());
    return ok;
}

void install_one(const AppReadyOffer& offer) {
    {
        LockGuard guard;
        if (!guard || generation_is_known(offer.bundle_sha256.data())) {
            return;
        }
    }
    display_publish_install_state(
        static_cast<std::uint8_t>(InstallPhase::downloading),
        "Downloading app");
    const auto part_path = g_store->incoming_part_path(
        offer.bundle_sha256.data());
    if (part_path.empty() || !download(offer, part_path)) {
        ESP_LOGE(kTag, "download failed: %.12s", offer.bundle_sha256.data());
        display_publish_install_state(
            static_cast<std::uint8_t>(InstallPhase::failed),
            "App download failed");
        return;
    }

    display_publish_install_state(
        static_cast<std::uint8_t>(InstallPhase::installing),
        "Verifying app");
    PackageInstallResult result;
    {
        FileLockGuard guard;
        if (!guard) return;
        result = g_store->prepare_part(
            part_path, offer.bundle_sha256.data(), g_trust);
    }
    PackageRegistry retained;
    bool have_registry_snapshot = false;
    if (result) {
        LockGuard guard;
        if (!guard) return;
        g_store->commit_prepared(result, g_registry);
        if (result && result.registry_changed) ++g_revision;
        retained = g_registry;
        have_registry_snapshot = true;
    }
    if (have_registry_snapshot) {
        FileLockGuard guard;
        if (guard) {
            const auto cleaned = g_store->garbage_collect(
                retained, result.generations_collected);
            if (cleaned != PackageStoreError::ok) {
                ESP_LOGW(
                    kTag,
                    "post-install cleanup deferred: %s",
                    package_store_error_name(cleaned));
            }
        }
    }
    if (!result) {
        std::remove(part_path.c_str());
        ESP_LOGE(
            kTag,
            "install failed store=%s bundle=%s",
            package_store_error_name(result.error),
            personal_bundle_error_name(result.bundle_error));
        display_publish_install_state(
            static_cast<std::uint8_t>(InstallPhase::failed),
            "App verification failed");
        return;
    }

    const auto& metadata = result.bundle.metadata;
    ESP_LOGI(
        kTag,
        "installed %s %s payload=%.12s bundle=%.12s",
        metadata.app_id.c_str(),
        metadata.semantic_version.c_str(),
        metadata.payload_sha256.c_str(),
        result.bundle.bundle_sha256.c_str());
    display_publish_install_state(
        static_cast<std::uint8_t>(InstallPhase::ready),
        "App ready");
    display_refresh_installed_apps();
    display_publish_app_ready(
        metadata.app_id.c_str(),
        metadata.name.c_str(),
        metadata.semantic_version.c_str(),
        metadata.icon.c_str(),
        metadata.theme_seed.c_str(),
        metadata.payload_sha256.c_str());
}

void installer_task(void*) {
    AppReadyOffer offer{};
    while (true) {
        if (xQueueReceive(g_offers, &offer, portMAX_DELAY) == pdTRUE) {
            install_one(offer);
        }
    }
}

}  // namespace

bool package_service_init() {
    if (g_mounted) return true;
    if (g_lock == nullptr) g_lock = xSemaphoreCreateMutex();
    if (g_files_lock == nullptr) g_files_lock = xSemaphoreCreateMutex();
    if (g_offers == nullptr) {
        g_offers = xQueueCreateWithCaps(
            kOfferQueueDepth,
            sizeof(AppReadyOffer),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (g_launches == nullptr) {
        g_launches = xQueueCreate(kLaunchQueueDepth, sizeof(LaunchRequest));
    }
    if (g_lock == nullptr || g_files_lock == nullptr || g_offers == nullptr ||
        g_launches == nullptr) {
        ESP_LOGE(kTag, "queue allocation failed");
        return false;
    }

    esp_vfs_fat_mount_config_t mount = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount.format_if_mount_failed = false;
    mount.max_files = 12;
    mount.allocation_unit_size = 4 * 1024;
    const bool erased_partition = package_partition_is_erased();
    auto mounted = esp_vfs_fat_spiflash_mount_rw_wl(
        kMountPoint, kPartition, &mount, &g_wear_level);
    if (mounted != ESP_OK && erased_partition) {
        // This is an explicit first-use initialization, not recovery from a
        // generic mount error. Any non-erased/corrupt package filesystem is
        // preserved for diagnosis and rollback rather than reformatted.
        ESP_LOGI(kTag, "initializing erased package partition");
        mount.format_if_mount_failed = true;
        mounted = esp_vfs_fat_spiflash_mount_rw_wl(
            kMountPoint, kPartition, &mount, &g_wear_level);
    }
    if (mounted != ESP_OK) {
        ESP_LOGE(
            kTag,
            "package storage mount failed without formatting: %s",
            esp_err_to_name(mounted));
        return false;
    }
    g_store = std::make_unique<PersonalPackageStore>(kMountPoint);
    if (!ensure_directory("/packages/incoming") ||
        !ensure_directory("/packages/apps")) {
        ESP_LOGE(kTag, "package storage directories unavailable");
        abandon_package_mount();
        return false;
    }

#if CONFIG_DOODAD_PERSONAL_APPS
    g_trust.owner_id = CONFIG_DOODAD_PERSONAL_OWNER_ID;
    g_trust.signer_key_id = CONFIG_DOODAD_PERSONAL_SIGNER_KEY_ID;
    g_trust.host_abi = CONFIG_DOODAD_PERSONAL_HOST_ABI;
    const bool owner_configured = !g_trust.owner_id.empty();
    if (owner_configured) {
        const auto loaded = g_store->load_registry(g_trust.owner_id, g_registry);
        if (loaded != PackageStoreError::ok) {
            ESP_LOGE(
                kTag,
                "package registry unavailable: %s",
                package_store_error_name(loaded));
            abandon_package_mount();
            return false;
        }
    }
    g_install_enabled = owner_configured &&
        !g_trust.signer_key_id.empty() &&
        decode_key(CONFIG_DOODAD_PERSONAL_HMAC_KEY_HEX, g_trust.hmac_key);
#endif
    if (!g_install_enabled) {
        ESP_LOGW(
            kTag,
            "personal installs disabled; configure owner and 32-byte HMAC key");
    } else if (xTaskCreateWithCaps(
            installer_task,
            "app_installer",
            kInstallerStackBytes,
            nullptr,
            4,
            &g_installer_task,
            // The installer writes the internal flash-backed package
            // filesystem. Its stack must remain usable while ESP-IDF has the
            // external flash cache disabled during those writes.
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(kTag, "installer task creation failed");
        abandon_package_mount();
        return false;
    }

    std::uint64_t total = 0;
    std::uint64_t free = 0;
    if (esp_vfs_fat_info(kMountPoint, &total, &free) == ESP_OK) {
        ESP_LOGI(
            kTag,
            "package storage mounted: %llu KiB free / %llu KiB apps=%u",
            static_cast<unsigned long long>(free / 1024),
            static_cast<unsigned long long>(total / 1024),
            static_cast<unsigned>(g_registry.apps().size()));
    }
    g_mounted = true;
    return true;
}

bool package_service_mounted() {
    return g_mounted;
}

bool package_service_offer(const AppReadyOffer& offer) {
    if (!g_install_enabled || g_offers == nullptr ||
        offer.url[0] == '\0' || offer.url.back() != '\0' ||
        (std::strncmp(offer.url.data(), "http://", 7) != 0 &&
         std::strncmp(offer.url.data(), "https://", 8) != 0) ||
        !digest_text(offer.bundle_sha256.data()) ||
        offer.bundle_bytes <= kPersonalBundleHeaderBytes + kSha256Bytes ||
        offer.bundle_bytes > kMaximumBundleBytes) {
        return false;
    }
    return xQueueSend(g_offers, &offer, 0) == pdTRUE;
}

bool package_service_catalog(CatalogSnapshot& snapshot) {
    snapshot = {};
    LockGuard guard;
    if (!guard || !g_mounted) return false;
    snapshot.revision = g_revision;
    for (const auto& app : g_registry.apps()) {
        if (snapshot.count == snapshot.apps.size()) break;
        // CatalogEntry has no disabled state. Keep terminally blocked apps in
        // the durable registry for reset/forensics, but do not advertise them
        // as launchable choices.
        if (g_registry.is_blocked(app.app_id)) continue;
        auto& output = snapshot.apps[snapshot.count];
        if (!copy_text(output.app_id, app.app_id) ||
            !copy_text(output.name, app.current.name) ||
            !copy_text(output.semantic_version, app.current.semantic_version) ||
            !copy_text(output.icon, app.current.icon) ||
            !copy_text(output.theme_seed, app.current.theme_seed) ||
            !copy_text(output.payload_sha256, app.current.payload_sha256)) {
            continue;
        }
        output.has_previous = app.previous.has_value();
        output.rollback_available = g_registry.rollback_eligible(app.app_id);
        ++snapshot.count;
    }
    return true;
}

bool package_service_request_launch(
    const char* app_id,
    const char* semantic_version,
    const char* payload_sha256) {
    if (g_launches == nullptr || semantic_version == nullptr ||
        !digest_text(payload_sha256)) {
        return false;
    }
    LaunchRequest request{};
    {
        LockGuard guard;
        if (!guard) return false;
        const auto* app = g_registry.find(app_id == nullptr ? "" : app_id);
        if (app == nullptr) return false;
        const auto* generation = g_registry.launchable_generation(
            app->app_id, semantic_version, payload_sha256);
        if (generation == nullptr ||
            !fill_launch(*app, *generation, request)) return false;
    }
    return xQueueSend(g_launches, &request, 0) == pdTRUE;
}

bool package_service_poll_launch(LaunchRequest& request) {
    request = {};
    return g_launches != nullptr &&
        xQueueReceive(g_launches, &request, 0) == pdTRUE;
}

bool package_service_load(
    const LaunchRequest& request,
    std::vector<std::uint8_t>& storage) {
    storage.clear();
    if (g_store == nullptr) return false;
    PackageGeneration selected;
    {
        LockGuard guard;
        if (!guard) return false;
        const auto* app = g_registry.find(request.app_id.data());
        if (app == nullptr) return false;
        const auto* generation = g_registry.launchable_generation(
            app->app_id,
            request.semantic_version.data(),
            request.payload_sha256.data());
        if (generation == nullptr) {
            return false;
        }
        selected = *generation;
    }
    {
        FileLockGuard guard;
        if (!guard || !g_store->load_verified_wasm(
                request.app_id.data(),
                selected,
                g_trust,
                kMaximumRunnableWasmBytes,
                storage)) {
            ESP_LOGE(
                kTag,
                "installed payload verification failed: %.12s",
                selected.payload_sha256.c_str());
            return false;
        }
    }
    // An install may have advanced current/previous while the immutable file
    // was being read. Reject a now-unregistered selection before handing the
    // verified, caller-owned bytes to WAMR.
    LockGuard guard;
    const auto* generation = guard
        ? g_registry.launchable_generation(
            request.app_id.data(),
            request.semantic_version.data(),
            request.payload_sha256.data())
        : nullptr;
    if (generation == nullptr || *generation != selected) {
        storage.clear();
        return false;
    }
    return true;
}

bool package_service_mark_running(const LaunchRequest& request) {
    LockGuard guard;
    return guard && g_registry.mark_running(
        request.app_id.data(),
        request.semantic_version.data(),
        request.payload_sha256.data());
}

bool package_service_rollback(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    LaunchRequest& previous) {
    previous = {};
    if (app_id == nullptr || failed_semantic_version == nullptr ||
        !digest_text(failed_payload_sha256) ||
        g_store == nullptr) return false;
    LockGuard guard;
    if (!guard) return false;
    auto* app = g_registry.find(app_id);
    if (app == nullptr || !app->previous.has_value() ||
        app->current.semantic_version != failed_semantic_version ||
        app->current.payload_sha256 != failed_payload_sha256 ||
        g_registry.is_blocked(app->app_id)) {
        return false;
    }
    const auto before = g_registry;
    const auto failed_id = app->app_id;
    // `roll_back` returning true means the failure was consumed. At a full
    // quarantine history that consumption persistently blocks the app without
    // swapping slots, so it does not necessarily yield a launch target.
    if (!g_registry.roll_back(
            failed_id, failed_semantic_version, failed_payload_sha256) ||
        g_store->save_registry(g_registry) != PackageStoreError::ok) {
        g_registry = before;
        return false;
    }
    if (g_registry.is_blocked(failed_id)) {
        // Saturation was successfully persisted. Never restore the pre-state
        // and never expose current/previous as a fallback launch request.
        ++g_revision;
        display_refresh_installed_apps();
        return false;
    }
    app = g_registry.find(failed_id);
    if (app == nullptr || !fill_launch(*app, app->current, previous)) {
        g_registry = before;
        g_store->save_registry(g_registry);
        return false;
    }
    ++g_revision;
    display_refresh_installed_apps();
    return true;
}

bool package_service_recover_current(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    LaunchRequest& current) {
    current = {};
    if (app_id == nullptr || failed_semantic_version == nullptr ||
        !digest_text(failed_payload_sha256) || g_store == nullptr) {
        return false;
    }
    LockGuard guard;
    if (!guard) return false;
    const auto* app = g_registry.find(app_id);
    if (app == nullptr ||
        (app->current.semantic_version == failed_semantic_version &&
         app->current.payload_sha256 == failed_payload_sha256) ||
        !fill_launch(*app, app->current, current)) {
        current = {};
        return false;
    }
    if (g_registry.launchable_generation(
            app_id,
            current.semantic_version.data(),
            current.payload_sha256.data()) == nullptr) {
        current = {};
        return false;
    }
    // A duplicate stale request may arrive after this exact failure was
    // already quarantined and recovery current is running. Return that same
    // safe target idempotently without rewriting state or clearing residency.
    if (g_registry.is_quarantined(
            app_id, failed_semantic_version, failed_payload_sha256)) {
        return true;
    }

    const auto before = g_registry;
    if (!g_registry.quarantine_generation(
            app_id, failed_semantic_version, failed_payload_sha256) ||
        g_store->save_registry(g_registry) != PackageStoreError::ok) {
        g_registry = before;
        current = {};
        return false;
    }
    if (g_registry.is_blocked(app_id)) {
        // Save saturation before considering a recovery launch. The blocked
        // app has no safe current/previous selector and the persisted state is
        // intentionally not rolled back.
        current = {};
        ++g_revision;
        display_refresh_installed_apps();
        return false;
    }
    if (g_registry.launchable_generation(
            app_id,
            current.semantic_version.data(),
            current.payload_sha256.data()) == nullptr) {
        g_registry = before;
        g_store->save_registry(g_registry);
        current = {};
        return false;
    }
    ++g_revision;
    display_refresh_installed_apps();
    return true;
}

}  // namespace doodad::packages
