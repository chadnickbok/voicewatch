#include "package_registry.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace doodad::packages {
namespace {

constexpr std::uint8_t kRegistryMagic[] = {'D', 'D', 'R', '3'};
constexpr std::size_t kMaximumRegistryBytes = 128 * 1024;
constexpr std::uint16_t kMaximumRegistryStringBytes = 512;

bool valid_owner_id(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return (byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
            byte == ':' || byte == '-';
    });
}

bool digest(const std::string& value) {
    Sha256Digest parsed{};
    return parse_sha256_hex(value, parsed);
}

bool valid_app_id(const std::string& value) {
    if (value.empty() || value.size() > kMaximumPersonalAppIdBytes ||
        value.front() < 'a' || value.front() > 'z') return false;
    bool saw_dot = false;
    bool after_dot = false;
    for (std::size_t index = 1; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '.') {
            if (after_dot || value[index - 1] == '.') return false;
            saw_dot = true;
            after_dot = true;
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9')) {
            after_dot = false;
        } else if (!(character == '-' && saw_dot && !after_dot)) {
            return false;
        }
    }
    return saw_dot && !after_dot;
}

bool valid_version(const std::string& value) {
    if (value.empty() ||
        value.size() > kMaximumPersonalAppVersionBytes) return false;
    std::size_t position = 0;
    for (int component = 0; component < 3; ++component) {
        const auto start = position;
        while (position < value.size() && value[position] >= '0' &&
               value[position] <= '9') ++position;
        if (position == start) return false;
        if (component < 2 &&
            (position >= value.size() || value[position++] != '.')) return false;
    }
    if (position == value.size()) return true;
    if (value[position] != '-' && value[position] != '+') return false;
    if (++position == value.size()) return false;
    for (; position < value.size(); ++position) {
        const char character = value[position];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' ||
              character == '-')) return false;
    }
    return true;
}

bool valid_name(const std::string& value) {
    if (value.empty() || value.size() > kMaximumPersonalAppNameBytes) {
        return false;
    }
    std::size_t position = 0;
    std::size_t codepoints = 0;
    while (position < value.size()) {
        if (++codepoints > kMaximumPersonalAppNameCodepoints) return false;
        const auto first = static_cast<std::uint8_t>(value[position++]);
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f) return false;
            continue;
        }
        std::size_t continuation = 0;
        std::uint32_t codepoint = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (position + continuation > value.size()) return false;
        for (std::size_t index = 0; index < continuation; ++index) {
            const auto next = static_cast<std::uint8_t>(value[position++]);
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6) | (next & 0x3fU);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

bool safe_relative_path(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.back() == '/' ||
        path.find("//") != std::string::npos ||
        path.find("/../") != std::string::npos ||
        path == ".." || path.rfind("../", 0) == 0 ||
        path.size() > kMaximumRegistryStringBytes) return false;
    return std::all_of(path.begin(), path.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '-' || character == '+' || character == '/';
    });
}

bool valid_generation(const PackageGeneration& generation) {
    constexpr const char* icons[] = {
        "generic", "timer", "weather", "tasks", "calculator", "calendar",
        "water_drop",
    };
    const bool known_icon = std::any_of(
        std::begin(icons), std::end(icons),
        [&](const char* candidate) { return generation.icon == candidate; });
    const bool valid_seed = generation.theme_seed.size() == kThemeSeedBytes &&
        generation.theme_seed.front() == '#' && std::all_of(
            generation.theme_seed.begin() + 1,
            generation.theme_seed.end(),
            [](char character) {
                return (character >= '0' && character <= '9') ||
                    (character >= 'A' && character <= 'F');
            });
    return valid_version(generation.semantic_version) &&
        valid_name(generation.name) &&
        known_icon && valid_seed &&
        digest(generation.payload_sha256) && digest(generation.bundle_sha256) &&
        safe_relative_path(generation.relative_path) && generation.host_abi > 0;
}

bool valid_generation_identity(const PackageGenerationIdentity& identity) {
    return valid_version(identity.semantic_version) &&
        digest(identity.payload_sha256);
}

PackageGenerationIdentity generation_identity(
    const PackageGeneration& generation) {
    return {generation.semantic_version, generation.payload_sha256};
}

bool has_generation_identity(
    const PackageGeneration& generation,
    const PackageGenerationIdentity& identity) {
    return generation.semantic_version == identity.semantic_version &&
        generation.payload_sha256 == identity.payload_sha256;
}

bool generation_identity_less(
    const PackageGenerationIdentity& left,
    const PackageGenerationIdentity& right) {
    return left.semantic_version < right.semantic_version ||
        (left.semantic_version == right.semantic_version &&
         left.payload_sha256 < right.payload_sha256);
}

bool contains_generation_identity(
    const std::vector<PackageGenerationIdentity>& identities,
    const PackageGenerationIdentity& identity) {
    const auto found = std::lower_bound(
        identities.begin(), identities.end(), identity,
        generation_identity_less);
    return found != identities.end() && *found == identity;
}

bool valid_quarantine_set(
    const std::vector<PackageGenerationIdentity>& identities) {
    if (identities.size() > kMaximumQuarantinedGenerationsPerApp) {
        return false;
    }
    for (std::size_t index = 0; index < identities.size(); ++index) {
        if (!valid_generation_identity(identities[index]) ||
            (index != 0 && !generation_identity_less(
                identities[index - 1], identities[index]))) {
            return false;
        }
    }
    return true;
}

std::string generation_storage_digest(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) {
    // This is the canonical internal encoding of the agreed generation
    // identity. Length-prefixing all three exact fields keeps it unambiguous
    // without making any signed non-identity metadata part of the path.
    std::vector<std::uint8_t> identity;
    identity.reserve(
        app_id.size() + semantic_version.size() + payload_sha256.size() + 12);
    const auto append_field = [&identity](const std::string& value) {
        const auto length = static_cast<std::uint32_t>(value.size());
        identity.push_back(static_cast<std::uint8_t>(length >> 24));
        identity.push_back(static_cast<std::uint8_t>(length >> 16));
        identity.push_back(static_cast<std::uint8_t>(length >> 8));
        identity.push_back(static_cast<std::uint8_t>(length));
        identity.insert(identity.end(), value.begin(), value.end());
    };
    append_field(app_id);
    append_field(semantic_version);
    append_field(payload_sha256);
    return sha256_hex(sha256_bytes(
        identity.data(), identity.size()));
}

std::string generation_relative_path(
    const PersonalBundleMetadata& metadata) {
    return package_generation_relative_path(
        metadata.app_id,
        metadata.semantic_version,
        metadata.payload_sha256);
}

std::string join(const std::string& root, const std::string& relative) {
    return root + "/" + relative;
}

bool path_kind(const std::string& path, bool directory) {
    struct stat status {};
    if (::stat(path.c_str(), &status) != 0) return false;
    return directory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode);
}

bool ensure_directory(const std::string& path) {
    if (::mkdir(path.c_str(), 0755) == 0) return true;
    return errno == EEXIST && path_kind(path, true);
}

void clean_installing_directory(const std::string& path) {
    std::remove(join(path, "app.wasm.part").c_str());
    std::remove(join(path, "app.wasm").c_str());
    std::remove(join(path, "bundle.ddb").c_str());
    ::rmdir(path.c_str());
}

bool remove_generation_directory(const std::string& path) {
    // Generation directories contain only these two immutable v1 files.
    const bool wasm_removed =
        std::remove(join(path, "app.wasm").c_str()) == 0 || errno == ENOENT;
    const bool bundle_removed =
        std::remove(join(path, "bundle.ddb").c_str()) == 0 || errno == ENOENT;
    const bool directory_removed = ::rmdir(path.c_str()) == 0 || errno == ENOENT;
    return wasm_removed && bundle_removed && directory_removed;
}

bool write_all(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    auto* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    bool ok = bytes.empty() ||
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    ok = ok && std::fflush(file) == 0;
    if (ok) ok = ::fsync(::fileno(file)) == 0;
    ok = std::fclose(file) == 0 && ok;
    if (!ok) std::remove(path.c_str());
    return ok;
}

bool read_all(const std::string& path, std::vector<std::uint8_t>& bytes) {
    auto* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? std::ftell(file) : -1;
    ok = length >= 0 && static_cast<std::size_t>(length) <= kMaximumRegistryBytes &&
        std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        bytes.resize(static_cast<std::size_t>(length));
        ok = bytes.empty() ||
            std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    }
    std::fclose(file);
    if (!ok) bytes.clear();
    return ok;
}

bool remove_file_if_exists(const std::string& path) {
    if (std::remove(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool regular_file_status(const std::string& path, bool& exists) {
    struct stat status {};
    if (::stat(path.c_str(), &status) == 0) {
        exists = true;
        return S_ISREG(status.st_mode);
    }
    exists = false;
    return errno == ENOENT;
}

// FatFs f_rename deliberately refuses to replace an existing destination.
// Enforce that contract on POSIX hosts too so native tests exercise the same
// promotion rules rather than relying on POSIX replacement rename.
bool rename_without_replace(
    const std::string& source,
    const std::string& destination) {
    bool destination_exists = false;
    if (!regular_file_status(destination, destination_exists) ||
        destination_exists) {
        errno = EEXIST;
        return false;
    }
    return std::rename(source.c_str(), destination.c_str()) == 0;
}

enum class RegistryFileState : std::uint8_t {
    missing,
    valid,
    invalid,
    io,
};

RegistryFileState read_registry_file(
    const std::string& path,
    PackageRegistry& registry) {
    struct stat status {};
    if (::stat(path.c_str(), &status) != 0) {
        return errno == ENOENT
            ? RegistryFileState::missing
            : RegistryFileState::io;
    }
    if (!S_ISREG(status.st_mode)) return RegistryFileState::invalid;
    std::vector<std::uint8_t> bytes;
    if (!read_all(path, bytes)) return RegistryFileState::io;
    return decode_package_registry(bytes.data(), bytes.size(), registry)
        ? RegistryFileState::valid
        : RegistryFileState::invalid;
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

bool append_string(std::vector<std::uint8_t>& output, const std::string& value) {
    if (value.size() > kMaximumRegistryStringBytes) return false;
    append_u16(output, static_cast<std::uint16_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool append_generation(
    std::vector<std::uint8_t>& output,
    const PackageGeneration& generation) {
    if (!valid_generation(generation)) return false;
    return append_string(output, generation.semantic_version) &&
        append_string(output, generation.name) &&
        append_string(output, generation.icon) &&
        append_string(output, generation.theme_seed) &&
        append_string(output, generation.payload_sha256) &&
        append_string(output, generation.bundle_sha256) &&
        append_string(output, generation.relative_path) &&
        (append_u32(output, generation.host_abi), true);
}

class RegistryReader {
  public:
    RegistryReader(const std::uint8_t* bytes, std::size_t size)
        : bytes_(bytes), size_(size) {}

    bool bytes(std::uint8_t* output, std::size_t count) {
        if (position_ > size_ || count > size_ - position_) return false;
        if (count > 0) std::memcpy(output, bytes_ + position_, count);
        position_ += count;
        return true;
    }

    bool u8(std::uint8_t& output) { return bytes(&output, 1); }

    bool u16(std::uint16_t& output) {
        std::array<std::uint8_t, 2> value{};
        if (!bytes(value.data(), value.size())) return false;
        output = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value[0]) << 8) | value[1]);
        return true;
    }

    bool u32(std::uint32_t& output) {
        std::array<std::uint8_t, 4> value{};
        if (!bytes(value.data(), value.size())) return false;
        output = (static_cast<std::uint32_t>(value[0]) << 24) |
            (static_cast<std::uint32_t>(value[1]) << 16) |
            (static_cast<std::uint32_t>(value[2]) << 8) | value[3];
        return true;
    }

    bool string(std::string& output) {
        std::uint16_t length = 0;
        if (!u16(length) || length > kMaximumRegistryStringBytes ||
            position_ > size_ || length > size_ - position_) return false;
        output.assign(
            reinterpret_cast<const char*>(bytes_ + position_), length);
        position_ += length;
        return true;
    }

    bool generation(PackageGeneration& output) {
        return string(output.semantic_version) &&
            string(output.name) && string(output.icon) &&
            string(output.theme_seed) && string(output.payload_sha256) &&
            string(output.bundle_sha256) && string(output.relative_path) &&
            u32(output.host_abi) && valid_generation(output);
    }

    std::size_t position() const { return position_; }

  private:
    const std::uint8_t* bytes_;
    std::size_t size_;
    std::size_t position_ = 0;
};

bool verify_existing_generation(
    const std::string& final_path,
    const PersonalTrustProfile& trust,
    const VerifiedPersonalBundle& incoming) {
    VerifiedPersonalBundle existing;
    if (verify_personal_bundle_file(
            join(final_path, "bundle.ddb"), trust, incoming.bundle_sha256,
            existing) != PersonalBundleError::ok) return false;
    if (existing.metadata.app_id != incoming.metadata.app_id ||
        existing.metadata.semantic_version != incoming.metadata.semantic_version ||
        existing.metadata.payload_sha256 != incoming.metadata.payload_sha256) {
        return false;
    }
    Sha256Digest wasm{};
    return sha256_file(join(final_path, "app.wasm"), wasm) &&
        sha256_hex(wasm) == incoming.metadata.payload_sha256;
}

}  // namespace

bool PackageGeneration::operator==(const PackageGeneration& other) const {
    return semantic_version == other.semantic_version && name == other.name &&
        icon == other.icon && theme_seed == other.theme_seed &&
        payload_sha256 == other.payload_sha256 &&
        bundle_sha256 == other.bundle_sha256 &&
        relative_path == other.relative_path && host_abi == other.host_abi;
}

bool PackageGenerationIdentity::operator==(
    const PackageGenerationIdentity& other) const {
    return semantic_version == other.semantic_version &&
        payload_sha256 == other.payload_sha256;
}

std::string package_generation_relative_path(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) {
    if (!valid_app_id(app_id) || !valid_version(semantic_version) ||
        !digest(payload_sha256)) return {};
    return "apps/" + app_id + "/" + generation_storage_digest(
        app_id, semantic_version, payload_sha256);
}

PackageRegistry::PackageRegistry(std::string owner_id)
    : owner_id_(std::move(owner_id)) {}

const InstalledApp* PackageRegistry::find(const std::string& id) const {
    const auto found = std::find_if(
        apps_.begin(), apps_.end(), [&](const InstalledApp& app) {
            return app.app_id == id;
        });
    return found == apps_.end() ? nullptr : &*found;
}

InstalledApp* PackageRegistry::find(const std::string& id) {
    const auto found = std::find_if(
        apps_.begin(), apps_.end(), [&](const InstalledApp& app) {
            return app.app_id == id;
        });
    return found == apps_.end() ? nullptr : &*found;
}

RegistryUpdate PackageRegistry::record_install(
    const PersonalBundleMetadata& metadata,
    const std::string& bundle_sha256,
    const std::string& relative_path) {
    if (!valid_owner_id(owner_id_) || metadata.owner_id != owner_id_ ||
        !valid_app_id(metadata.app_id) || !valid_name(metadata.name) ||
        !digest(bundle_sha256)) return RegistryUpdate::invalid;
    PackageGeneration generation{
        metadata.semantic_version,
        metadata.name,
        metadata.icon,
        metadata.theme_seed,
        metadata.payload_sha256,
        bundle_sha256,
        relative_path,
        metadata.host_abi,
    };
    if (!valid_generation(generation) ||
        relative_path != generation_relative_path(metadata)) {
        return RegistryUpdate::invalid;
    }
    auto* app = find(metadata.app_id);
    if (app == nullptr) {
        if (apps_.size() >= kMaximumInstalledApps) return RegistryUpdate::full;
        apps_.push_back(InstalledApp{
            metadata.app_id, generation, std::nullopt, {}, false});
        std::sort(apps_.begin(), apps_.end(), [](const auto& left, const auto& right) {
            return left.app_id < right.app_id;
        });
        return RegistryUpdate::installed;
    }
    const auto incoming_identity = generation_identity(generation);
    if (app->quarantine_saturated || contains_generation_identity(
            app->quarantined_generations, incoming_identity)) {
        // Exact quarantine is one-way even after its bytes are GC'd. Once the
        // bounded history saturates, all installation/replay is blocked until
        // destructive profile reset rather than guessing which entry to drop.
        return RegistryUpdate::invalid;
    }
    if (has_generation_identity(app->current, incoming_identity)) {
        // A generation triple names exactly one immutable signed envelope.
        // Replays are idempotent; differing envelope metadata under the same
        // identity is a conflict and must not create two aliases to one path.
        return app->current == generation
            ? RegistryUpdate::unchanged
            : RegistryUpdate::invalid;
    }
    // Reconnects can replay a durable app.ready announcement. A retained
    // previous generation is already installed; treating it as a fresh update
    // would silently downgrade and flip current/previous.
    if (app->previous.has_value() &&
        has_generation_identity(*app->previous, incoming_identity)) {
        return *app->previous == generation
            ? RegistryUpdate::unchanged
            : RegistryUpdate::invalid;
    }
    app->previous = app->current;
    app->current = std::move(generation);
    return RegistryUpdate::installed;
}

bool PackageRegistry::roll_back(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) {
    auto* app = find(app_id);
    const PackageGenerationIdentity failed_generation{
        semantic_version, payload_sha256};
    if (app == nullptr || !app->previous.has_value() ||
        !has_generation_identity(app->current, failed_generation)) {
        return false;
    }
    if (app->quarantine_saturated) {
        // Repeated reporting of the exact failure is idempotently handled, but
        // a blocked app never exposes a rollback target.
        if (running_.has_value() && running_->app_id == app_id) {
            running_.reset();
        }
        return true;
    }
    if (contains_generation_identity(
            app->quarantined_generations,
            generation_identity(*app->previous))) {
        return false;
    }
    // Record the failure before changing current/previous. At capacity this
    // blocks the whole app without evicting history or swapping the slots.
    if (!quarantine_generation(
            app_id, semantic_version, payload_sha256)) {
        return false;
    }
    if (app->quarantine_saturated) return true;
    std::swap(app->current, *app->previous);
    // The package manager marks a guest running only after run_app succeeds.
    // Rollback therefore invalidates, rather than predicts, resident state.
    running_.reset();
    return true;
}

bool PackageRegistry::rollback_eligible(const std::string& app_id) const {
    const auto* app = find(app_id);
    if (app == nullptr || app->quarantine_saturated ||
        !app->previous.has_value()) return false;
    const auto previous = generation_identity(*app->previous);
    const auto current = generation_identity(app->current);
    return !contains_generation_identity(
               app->quarantined_generations, previous) &&
        (contains_generation_identity(
             app->quarantined_generations, current) ||
         app->quarantined_generations.size() <
             kMaximumQuarantinedGenerationsPerApp);
}

bool PackageRegistry::is_quarantined(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) const {
    const auto* app = find(app_id);
    return app != nullptr &&
        (app->quarantine_saturated || contains_generation_identity(
            app->quarantined_generations,
            PackageGenerationIdentity{semantic_version, payload_sha256}));
}

bool PackageRegistry::is_blocked(const std::string& app_id) const {
    const auto* app = find(app_id);
    return app != nullptr && app->quarantine_saturated;
}

bool PackageRegistry::quarantine_generation(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) {
    auto* app = find(app_id);
    const PackageGenerationIdentity failed{
        semantic_version, payload_sha256};
    if (app == nullptr || !valid_generation_identity(failed)) return false;
    if (app->quarantine_saturated) {
        if (running_.has_value() && running_->app_id == app_id) {
            running_.reset();
        }
        return true;
    }
    auto position = std::lower_bound(
        app->quarantined_generations.begin(),
        app->quarantined_generations.end(),
        failed,
        generation_identity_less);
    if (position == app->quarantined_generations.end() ||
        !(*position == failed)) {
        if (app->quarantined_generations.size() >=
            kMaximumQuarantinedGenerationsPerApp) {
            app->quarantine_saturated = true;
            if (running_.has_value() && running_->app_id == app_id) {
                running_.reset();
            }
            return true;
        }
        app->quarantined_generations.insert(position, failed);
    }
    if (running_.has_value() && running_->app_id == app_id &&
        has_generation_identity(running_->generation, failed)) {
        running_.reset();
    }
    return true;
}

const PackageGeneration* PackageRegistry::launchable_generation(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) const {
    const auto* app = find(app_id);
    if (app == nullptr || app->quarantine_saturated ||
        is_quarantined(app_id, semantic_version, payload_sha256)) {
        return nullptr;
    }
    const PackageGenerationIdentity requested{
        semantic_version, payload_sha256};
    if (has_generation_identity(app->current, requested)) {
        return &app->current;
    }
    if (app->previous.has_value() &&
        has_generation_identity(*app->previous, requested)) {
        return &*app->previous;
    }
    return nullptr;
}

bool PackageRegistry::mark_running(
    const std::string& app_id,
    const std::string& semantic_version,
    const std::string& payload_sha256) {
    const auto* generation = launchable_generation(
        app_id, semantic_version, payload_sha256);
    if (generation == nullptr) return false;
    running_ = RunningPackage{app_id, *generation};
    return true;
}

std::vector<std::uint8_t> encode_package_registry(
    const PackageRegistry& registry) {
    if (!valid_owner_id(registry.owner_id()) ||
        registry.apps().size() > kMaximumInstalledApps) return {};
    std::vector<std::uint8_t> output(
        std::begin(kRegistryMagic), std::end(kRegistryMagic));
    if (!append_string(output, registry.owner_id())) return {};
    append_u16(output, static_cast<std::uint16_t>(registry.apps().size()));
    for (const auto& app : registry.apps()) {
        if (!valid_app_id(app.app_id) ||
            !append_string(output, app.app_id) ||
            !append_generation(output, app.current)) return {};
        output.push_back(app.previous.has_value() ? 1 : 0);
        if (app.previous.has_value() &&
            !append_generation(output, *app.previous)) return {};
        if (!valid_quarantine_set(app.quarantined_generations)) return {};
        if (app.quarantine_saturated &&
            app.quarantined_generations.size() !=
                kMaximumQuarantinedGenerationsPerApp) {
            return {};
        }
        output.push_back(static_cast<std::uint8_t>(
            app.quarantined_generations.size()));
        for (const auto& quarantined : app.quarantined_generations) {
            if (!append_string(output, quarantined.semantic_version) ||
                !append_string(output, quarantined.payload_sha256)) {
                return {};
            }
        }
        output.push_back(app.quarantine_saturated ? 1 : 0);
    }
    const auto checksum = sha256_bytes(output.data(), output.size());
    output.insert(output.end(), checksum.begin(), checksum.end());
    return output;
}

bool decode_package_registry(
    const std::uint8_t* bytes,
    std::size_t size,
    PackageRegistry& registry) {
    registry = {};
    if (bytes == nullptr || size < sizeof(kRegistryMagic) + 2 + 2 + kSha256Bytes ||
        size > kMaximumRegistryBytes) return false;
    const auto content_size = size - kSha256Bytes;
    const auto checksum = sha256_bytes(bytes, content_size);
    if (!std::equal(checksum.begin(), checksum.end(), bytes + content_size)) {
        return false;
    }
    RegistryReader reader(bytes, content_size);
    std::array<std::uint8_t, sizeof(kRegistryMagic)> magic{};
    if (!reader.bytes(magic.data(), magic.size()) ||
        !std::equal(magic.begin(), magic.end(), std::begin(kRegistryMagic))) {
        return false;
    }
    if (!reader.string(registry.owner_id_) || !valid_owner_id(registry.owner_id_)) {
        return false;
    }
    std::uint16_t app_count = 0;
    if (!reader.u16(app_count) || app_count > kMaximumInstalledApps) return false;
    registry.apps_.reserve(app_count);
    std::string prior_app_id;
    for (std::uint16_t index = 0; index < app_count; ++index) {
        InstalledApp app;
        if (!reader.string(app.app_id) || !valid_app_id(app.app_id) ||
            (!prior_app_id.empty() && app.app_id <= prior_app_id) ||
            !reader.generation(app.current)) return false;
        std::uint8_t has_previous = 0;
        if (!reader.u8(has_previous) || has_previous > 1) return false;
        if (has_previous != 0) {
            PackageGeneration previous;
            if (!reader.generation(previous) || previous == app.current ||
                has_generation_identity(
                    previous, generation_identity(app.current))) {
                return false;
            }
            app.previous = std::move(previous);
        }
        std::uint8_t quarantine_count = 0;
        if (!reader.u8(quarantine_count) ||
            quarantine_count > kMaximumQuarantinedGenerationsPerApp) {
            return false;
        }
        app.quarantined_generations.reserve(quarantine_count);
        for (std::uint8_t quarantine_index = 0;
             quarantine_index < quarantine_count;
             ++quarantine_index) {
            PackageGenerationIdentity quarantined;
            if (!reader.string(quarantined.semantic_version) ||
                !reader.string(quarantined.payload_sha256) ||
                !valid_generation_identity(quarantined) ||
                (!app.quarantined_generations.empty() &&
                 !generation_identity_less(
                     app.quarantined_generations.back(), quarantined))) {
                return false;
            }
            app.quarantined_generations.push_back(std::move(quarantined));
        }
        std::uint8_t quarantine_saturated = 0;
        if (!reader.u8(quarantine_saturated) || quarantine_saturated > 1 ||
            (quarantine_saturated != 0 &&
             quarantine_count != kMaximumQuarantinedGenerationsPerApp)) {
            return false;
        }
        app.quarantine_saturated = quarantine_saturated != 0;
        const auto expected_path = [&](const PackageGeneration& generation) {
            return package_generation_relative_path(
                app.app_id,
                generation.semantic_version,
                generation.payload_sha256);
        };
        if (app.current.relative_path != expected_path(app.current) ||
            (app.previous.has_value() &&
             app.previous->relative_path != expected_path(*app.previous))) {
            return false;
        }
        prior_app_id = app.app_id;
        registry.apps_.push_back(std::move(app));
    }
    registry.running_.reset();
    return reader.position() == content_size;
}

PersonalPackageStore::PersonalPackageStore(std::string root)
    : root_(std::move(root)) {
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
}

std::string PersonalPackageStore::incoming_part_path(
    const std::string& bundle_sha256) const {
    Sha256Digest ignored{};
    if (!parse_sha256_hex(bundle_sha256, ignored)) return {};
    return root_ + "/incoming/" + bundle_sha256 + ".part";
}

std::string PersonalPackageStore::wasm_path(
    const PackageGeneration& generation) const {
    if (!valid_generation(generation)) return {};
    return join(root_, generation.relative_path) + "/app.wasm";
}

PackageStoreError PersonalPackageStore::load_registry(
    const std::string& expected_owner_id,
    PackageRegistry& registry) const {
    if (!valid_owner_id(expected_owner_id)) {
        return PackageStoreError::owner_mismatch;
    }
    registry = {};
    const auto final = root_ + "/registry.ddr";
    const auto backup = root_ + "/registry.ddr.bak";
    const auto temporary = root_ + "/registry.ddr.part";
    PackageRegistry final_registry;
    PackageRegistry backup_registry;
    const auto final_state = read_registry_file(final, final_registry);
    const auto backup_state = read_registry_file(backup, backup_registry);

    if (final_state == RegistryFileState::valid) {
        if (final_registry.owner_id() != expected_owner_id) {
            return PackageStoreError::owner_mismatch;
        }
        // A valid final is the commit point. Any backup is the older snapshot,
        // and any part is an uncommitted write interrupted before promotion.
        if (!remove_file_if_exists(backup) ||
            !remove_file_if_exists(temporary)) {
            return PackageStoreError::io;
        }
        registry = std::move(final_registry);
    } else if (backup_state == RegistryFileState::valid) {
        if (backup_registry.owner_id() != expected_owner_id) {
            return PackageStoreError::owner_mismatch;
        }
        // Promotion was interrupted after final -> backup but before the new
        // part became final. Restore the last committed snapshot. Removing an
        // invalid final first is safe because the valid backup remains until
        // the non-replacing rename succeeds.
        if (!remove_file_if_exists(final) ||
            !rename_without_replace(backup, final) ||
            !remove_file_if_exists(temporary)) {
            return PackageStoreError::io;
        }
        registry = std::move(backup_registry);
    } else if (final_state == RegistryFileState::missing &&
               backup_state == RegistryFileState::missing) {
        // A lone part was never committed, including on first installation.
        registry = PackageRegistry(expected_owner_id);
        if (!remove_file_if_exists(temporary)) return PackageStoreError::io;
    } else {
        return final_state == RegistryFileState::io ||
                backup_state == RegistryFileState::io
            ? PackageStoreError::io
            : PackageStoreError::invalid_registry;
    }
    std::size_t ignored = 0;
    return garbage_collect(registry, ignored);
}

PackageStoreError PersonalPackageStore::save_registry(
    const PackageRegistry& registry) const {
    const auto encoded = encode_package_registry(registry);
    if (encoded.empty()) return PackageStoreError::invalid_registry;
    if (!ensure_directory(root_)) return PackageStoreError::io;
    const auto temporary = root_ + "/registry.ddr.part";
    const auto final = root_ + "/registry.ddr";
    const auto backup = root_ + "/registry.ddr.bak";
    if (!write_all(temporary, encoded)) return PackageStoreError::io;

    bool final_exists = false;
    bool backup_exists = false;
    if (!regular_file_status(final, final_exists) ||
        !regular_file_status(backup, backup_exists)) {
        remove_file_if_exists(temporary);
        return PackageStoreError::io;
    }
    if (final_exists) {
        // final is authoritative, so a leftover backup is stale and can be
        // discarded before preserving final as this transaction's rollback.
        if ((backup_exists && !remove_file_if_exists(backup)) ||
            !rename_without_replace(final, backup)) {
            remove_file_if_exists(temporary);
            return PackageStoreError::io;
        }
        backup_exists = true;
    }
    // If final was already absent, retain any valid backup as the last commit
    // until part -> final establishes the new commit point.
    if (!rename_without_replace(temporary, final)) {
        if (backup_exists) rename_without_replace(backup, final);
        remove_file_if_exists(temporary);
        return PackageStoreError::io;
    }
    // The new final is authoritative. Failure to remove the older backup does
    // not make the committed save fail; load_registry will clean it safely.
    remove_file_if_exists(backup);
    return PackageStoreError::ok;
}

PackageStoreError PersonalPackageStore::garbage_collect(
    const PackageRegistry& registry,
    std::size_t& generations_collected) const {
    generations_collected = 0;
    bool traversal_ok = true;
    const auto incoming_root = root_ + "/incoming";
    auto* incoming = ::opendir(incoming_root.c_str());
    if (incoming != nullptr) {
        while (auto* entry = ::readdir(incoming)) {
            const std::string name = entry->d_name;
            if (name.size() != 64 + sizeof(".part") - 1 ||
                name.compare(64, sizeof(".part") - 1, ".part") != 0) {
                continue;
            }
            Sha256Digest digest{};
            if (!parse_sha256_hex(name.substr(0, 64), digest)) continue;
            const auto path = join(incoming_root, name);
            if (path_kind(path, false) && std::remove(path.c_str()) != 0) {
                traversal_ok = false;
            }
        }
        ::closedir(incoming);
    } else if (errno != ENOENT) {
        traversal_ok = false;
    }

    const auto apps_root = root_ + "/apps";
    auto* apps = ::opendir(apps_root.c_str());
    if (apps == nullptr) {
        return errno == ENOENT && traversal_ok
            ? PackageStoreError::ok
            : PackageStoreError::io;
    }
    const auto retained = [&](const std::string& relative_path) {
        for (const auto& app : registry.apps()) {
            if (app.current.relative_path == relative_path ||
                (app.previous.has_value() &&
                 app.previous->relative_path == relative_path)) return true;
        }
        return false;
    };
    while (auto* app_entry = ::readdir(apps)) {
        const std::string app_name = app_entry->d_name;
        if (app_name == "." || app_name == "..") continue;
        const auto app_path = apps_root + "/" + app_name;
        if (!path_kind(app_path, true)) continue;
        auto* generations = ::opendir(app_path.c_str());
        if (generations == nullptr) {
            traversal_ok = false;
            continue;
        }
        while (auto* generation_entry = ::readdir(generations)) {
            const std::string generation_name = generation_entry->d_name;
            if (generation_name == "." || generation_name == "..") continue;
            const auto generation_path = app_path + "/" + generation_name;
            if (!path_kind(generation_path, true)) continue;
            const auto relative =
                "apps/" + app_name + "/" + generation_name;
            if (retained(relative)) continue;
            const bool installing = generation_name.size() >= 11 &&
                generation_name.compare(
                    generation_name.size() - 11, 11, ".installing") == 0;
            if (installing) {
                clean_installing_directory(generation_path);
                if (!path_kind(generation_path, true)) ++generations_collected;
            } else if (remove_generation_directory(generation_path)) {
                ++generations_collected;
            }
        }
        ::closedir(generations);
        // Remove only empty app directories; retained generations keep theirs.
        ::rmdir(app_path.c_str());
    }
    ::closedir(apps);
    return traversal_ok ? PackageStoreError::ok : PackageStoreError::io;
}

bool PersonalPackageStore::load_verified_wasm(
    const std::string& app_id,
    const PackageGeneration& generation,
    const PersonalTrustProfile& trust,
    std::size_t maximum_bytes,
    std::vector<std::uint8_t>& storage) const {
    storage.clear();
    const auto path = wasm_path(generation);
    if (!valid_app_id(app_id) || path.empty() || maximum_bytes == 0 ||
        generation.relative_path != package_generation_relative_path(
            app_id,
            generation.semantic_version,
            generation.payload_sha256)) {
        return false;
    }
    VerifiedPersonalBundle bundle;
    if (verify_personal_bundle_file(
            join(join(root_, generation.relative_path), "bundle.ddb"),
            trust,
            generation.bundle_sha256,
            bundle) != PersonalBundleError::ok ||
        bundle.metadata.app_id != app_id ||
        bundle.metadata.semantic_version != generation.semantic_version ||
        bundle.metadata.name != generation.name ||
        bundle.metadata.payload_sha256 != generation.payload_sha256 ||
        bundle.metadata.host_abi != generation.host_abi) {
        return false;
    }
    auto* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? std::ftell(file) : -1;
    ok = length > 0 &&
        static_cast<std::size_t>(length) <= maximum_bytes &&
        std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        storage.resize(static_cast<std::size_t>(length));
        ok = std::fread(storage.data(), 1, storage.size(), file) ==
            storage.size();
    }
    ok = std::fclose(file) == 0 && ok;
    if (ok) {
        Sha256Digest expected{};
        ok = storage.size() == bundle.metadata.payload_bytes &&
            parse_sha256_hex(generation.payload_sha256, expected) &&
            sha256_bytes(storage.data(), storage.size()) == expected;
    }
    if (!ok) storage.clear();
    return ok;
}

PackageInstallResult PersonalPackageStore::prepare_part(
    const std::string& part_path,
    const std::string& expected_bundle_sha256,
    const PersonalTrustProfile& trust) const {
    PackageInstallResult result;
    const auto expected_part = incoming_part_path(expected_bundle_sha256);
    if (expected_part.empty() || part_path != expected_part ||
        !path_kind(part_path, false)) {
        result.error = PackageStoreError::invalid_part_path;
        return result;
    }
    result.bundle_error = verify_personal_bundle_file(
        part_path, trust, expected_bundle_sha256, result.bundle);
    if (result.bundle_error != PersonalBundleError::ok) {
        result.error = PackageStoreError::bundle_verification;
        return result;
    }
    const auto relative = generation_relative_path(result.bundle.metadata);
    const auto app_root = root_ + "/apps/" + result.bundle.metadata.app_id;
    const auto final_path = join(root_, relative);
    result.generation_path = final_path;
    if (!ensure_directory(root_) || !ensure_directory(root_ + "/incoming") ||
        !ensure_directory(root_ + "/apps") || !ensure_directory(app_root)) {
        result.error = PackageStoreError::io;
        return result;
    }

    if (path_kind(final_path, true)) {
        if (!verify_existing_generation(final_path, trust, result.bundle)) {
            result.error = PackageStoreError::generation_conflict;
            return result;
        }
        std::remove(part_path.c_str());
    } else {
        const auto installing_path = final_path + ".installing";
        if (path_kind(installing_path, true)) {
            clean_installing_directory(installing_path);
        }
        if (!ensure_directory(installing_path)) {
            result.error = PackageStoreError::io;
            return result;
        }
        const auto bundle_path = join(installing_path, "bundle.ddb");
        const auto wasm_part = join(installing_path, "app.wasm.part");
        const auto wasm_path = join(installing_path, "app.wasm");
        if (std::rename(part_path.c_str(), bundle_path.c_str()) != 0) {
            clean_installing_directory(installing_path);
            result.error = PackageStoreError::io;
            return result;
        }
        result.bundle_error = extract_personal_bundle_payload(
            bundle_path, result.bundle, wasm_part);
        if (result.bundle_error != PersonalBundleError::ok ||
            std::rename(wasm_part.c_str(), wasm_path.c_str()) != 0 ||
            std::rename(installing_path.c_str(), final_path.c_str()) != 0) {
            clean_installing_directory(installing_path);
            result.error = result.bundle_error == PersonalBundleError::ok
                ? PackageStoreError::io
                : PackageStoreError::bundle_verification;
            return result;
        }
        result.installed_new_bytes = true;
    }

    return result;
}

PackageStoreError PersonalPackageStore::commit_prepared(
    PackageInstallResult& prepared,
    PackageRegistry& registry) const {
    prepared.registry_changed = false;
    if (!prepared) return prepared.error;
    const auto relative = generation_relative_path(prepared.bundle.metadata);
    if (registry.owner_id() != prepared.bundle.metadata.owner_id) {
        prepared.error = PackageStoreError::owner_mismatch;
        return prepared.error;
    }
    if (prepared.generation_path != join(root_, relative)) {
        prepared.error = PackageStoreError::generation_conflict;
        return prepared.error;
    }

    const auto before = registry;
    const auto update = registry.record_install(
        prepared.bundle.metadata, prepared.bundle.bundle_sha256, relative);
    if (update == RegistryUpdate::full) {
        prepared.error = PackageStoreError::registry_full;
        return prepared.error;
    }
    if (update == RegistryUpdate::invalid) {
        prepared.error = PackageStoreError::invalid_registry;
        return prepared.error;
    }
    prepared.registry_changed = update == RegistryUpdate::installed;
    if (prepared.registry_changed) {
        const auto saved = save_registry(registry);
        if (saved != PackageStoreError::ok) {
            registry = before;
            prepared.registry_changed = false;
            prepared.error = saved;
            return prepared.error;
        }
    }
    prepared.error = PackageStoreError::ok;
    return prepared.error;
}

PackageInstallResult PersonalPackageStore::install_part(
    const std::string& part_path,
    const std::string& expected_bundle_sha256,
    const PersonalTrustProfile& trust,
    PackageRegistry& registry) const {
    if (registry.owner_id() != trust.owner_id) {
        PackageInstallResult result;
        result.error = PackageStoreError::owner_mismatch;
        return result;
    }
    auto result = prepare_part(part_path, expected_bundle_sha256, trust);
    if (!result) return result;
    commit_prepared(result, registry);
    // Registry commit is the authority. Cleanup afterward can be repeated on
    // boot, so a power loss here leaves at most an orphan, never a missing
    // current/previous generation. Cleanup also removes a newly prepared
    // orphan when the registry transaction fails.
    garbage_collect(registry, result.generations_collected);
    return result;
}

const char* package_store_error_name(PackageStoreError error) {
    switch (error) {
        case PackageStoreError::ok: return "ok";
        case PackageStoreError::io: return "io";
        case PackageStoreError::invalid_registry: return "invalid_registry";
        case PackageStoreError::owner_mismatch: return "owner_mismatch";
        case PackageStoreError::invalid_part_path: return "invalid_part_path";
        case PackageStoreError::bundle_verification: return "bundle_verification";
        case PackageStoreError::generation_conflict: return "generation_conflict";
        case PackageStoreError::registry_full: return "registry_full";
    }
    return "unknown";
}

}  // namespace doodad::packages
