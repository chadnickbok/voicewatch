#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "personal_bundle.hpp"

namespace doodad::packages {

constexpr std::size_t kMaximumInstalledApps = 32;
constexpr std::size_t kMaximumQuarantinedGenerationsPerApp = 8;

struct PackageGeneration {
    std::string semantic_version;
    std::string name;
    std::string payload_sha256;
    std::string bundle_sha256;
    std::string relative_path;
    std::uint32_t host_abi = 0;

    bool operator==(const PackageGeneration& other) const;
};

struct PackageGenerationIdentity {
    std::string semantic_version;
    std::string payload_sha256;

    bool operator==(const PackageGenerationIdentity& other) const;
};

struct InstalledApp {
    std::string app_id;
    PackageGeneration current;
    std::optional<PackageGeneration> previous;
    // Failed identities are a bounded, canonical set. Entries are never
    // evicted: if the set is full, recording another failure fails closed.
    // A retained failed generation may remain `previous` for inspection, but
    // is never selected as an automatic rollback target.
    std::vector<PackageGenerationIdentity> quarantined_generations;
    // Saturation is an app-level terminal state. It is set when another
    // distinct failure cannot be recorded without evicting history and is
    // cleared only by destructive profile/package-store reset.
    bool quarantine_saturated = false;
};

struct RunningPackage {
    std::string app_id;
    PackageGeneration generation;
};

enum class RegistryUpdate : std::uint8_t {
    unchanged = 0,
    installed,
    full,
    invalid,
};

// The durable registry keeps current/previous per app. The resident guest is
// deliberately separate and volatile: installing an app does not launch it,
// and restoring the registry after boot does not claim a guest is running.
class PackageRegistry {
  public:
    PackageRegistry() = default;
    explicit PackageRegistry(std::string owner_id);

    const std::string& owner_id() const { return owner_id_; }
    const std::vector<InstalledApp>& apps() const { return apps_; }
    const std::optional<RunningPackage>& running() const { return running_; }

    const InstalledApp* find(const std::string& app_id) const;
    InstalledApp* find(const std::string& app_id);

    RegistryUpdate record_install(
        const PersonalBundleMetadata& metadata,
        const std::string& bundle_sha256,
        const std::string& relative_path);
    bool rollback_eligible(const std::string& app_id) const;
    // Consumes an exact current-generation failure. `true` means either the
    // slots were swapped or a full quarantine history terminally blocked the
    // app without exposing a rollback target; inspect is_blocked() afterward.
    bool roll_back(
        const std::string& app_id,
        const std::string& semantic_version,
        const std::string& payload_sha256);
    bool is_quarantined(
        const std::string& app_id,
        const std::string& semantic_version,
        const std::string& payload_sha256) const;
    bool is_blocked(const std::string& app_id) const;
    bool quarantine_generation(
        const std::string& app_id,
        const std::string& semantic_version,
        const std::string& payload_sha256);
    const PackageGeneration* launchable_generation(
        const std::string& app_id,
        const std::string& semantic_version,
        const std::string& payload_sha256) const;
    bool mark_running(
        const std::string& app_id,
        const std::string& semantic_version,
        const std::string& payload_sha256);
    void clear_running() { running_.reset(); }

  private:
    friend bool decode_package_registry(
        const std::uint8_t*, std::size_t, PackageRegistry&);
    std::string owner_id_;
    std::vector<InstalledApp> apps_;
    std::optional<RunningPackage> running_;
};

// Deterministic, checksummed DDR2 representation. DDR2 stores the signed name
// per generation, each app's canonical persisted one-way quarantine set, and
// its terminal saturation bit; older layouts fail closed rather than being
// misparsed.
// `running()` is intentionally
// absent from this encoding because it describes a live WAMR instance.
std::vector<std::uint8_t> encode_package_registry(
    const PackageRegistry& registry);
bool decode_package_registry(
    const std::uint8_t* bytes,
    std::size_t size,
    PackageRegistry& registry);

// Maps the signed generation identity to a lowercase, FAT-case-invariant
// storage key. Version remains in signed metadata and the registry, never as a
// case-sensitive filename component.
std::string package_generation_relative_path(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256);

enum class PackageStoreError : std::uint8_t {
    ok = 0,
    io,
    invalid_registry,
    owner_mismatch,
    invalid_part_path,
    bundle_verification,
    generation_conflict,
    registry_full,
};

struct PackageInstallResult {
    PackageStoreError error = PackageStoreError::ok;
    PersonalBundleError bundle_error = PersonalBundleError::ok;
    VerifiedPersonalBundle bundle;
    std::string generation_path;
    bool installed_new_bytes = false;
    bool registry_changed = false;
    std::size_t generations_collected = 0;

    explicit operator bool() const { return error == PackageStoreError::ok; }
};

class PersonalPackageStore {
  public:
    explicit PersonalPackageStore(std::string root);

    const std::string& root() const { return root_; }
    std::string incoming_part_path(const std::string& bundle_sha256) const;
    std::string wasm_path(const PackageGeneration& generation) const;

    PackageStoreError load_registry(
        const std::string& expected_owner_id,
        PackageRegistry& registry) const;
    PackageStoreError save_registry(const PackageRegistry& registry) const;
    PackageStoreError garbage_collect(
        const PackageRegistry& registry,
        std::size_t& generations_collected) const;

    // Reads an immutable installed payload into caller-owned memory and
    // re-audits its retained bundle signature, owner/ABI binding, generation
    // metadata, and the exact loaded bytes against the registry. A failed
    // audit always leaves `storage` empty.
    bool load_verified_wasm(
        const std::string& app_id,
        const PackageGeneration& generation,
        const PersonalTrustProfile& trust,
        std::size_t maximum_bytes,
        std::vector<std::uint8_t>& storage) const;

    // Slow, streaming filesystem work is deliberately split from the small
    // registry transaction. `prepare_part` verifies and promotes immutable
    // bytes without consulting or mutating a PackageRegistry.
    PackageInstallResult prepare_part(
        const std::string& part_path,
        const std::string& expected_bundle_sha256,
        const PersonalTrustProfile& trust) const;

    // Advances current/previous and persists the registry for a successfully
    // prepared generation. It performs no bundle streaming or extraction.
    PackageStoreError commit_prepared(
        PackageInstallResult& prepared,
        PackageRegistry& registry) const;

    // The incoming file must be exactly
    //   <root>/incoming/<expected_bundle_sha256>.part
    // and remain on the same package filesystem. Successful installation
    // verifies before mutation, promotes bytes through an `.installing`
    // directory, and only then atomically advances the registry. This wrapper
    // composes prepare/commit/cleanup for single-threaded callers; firmware
    // uses the split operations so only the registry transaction is locked.
    PackageInstallResult install_part(
        const std::string& part_path,
        const std::string& expected_bundle_sha256,
        const PersonalTrustProfile& trust,
        PackageRegistry& registry) const;

  private:
    std::string root_;
};

const char* package_store_error_name(PackageStoreError error);

}  // namespace doodad::packages
