#include "m3e/os/surface_registry.hpp"

#include <algorithm>
#include <cstring>

namespace m3e::os {
namespace {

template <std::size_t Size>
bool bounded_text(
    const std::array<char, Size>& value,
    bool allow_empty = false) {
    const auto* end = static_cast<const char*>(
        std::memchr(value.data(), '\0', value.size()));
    return end != nullptr && (allow_empty || end != value.data());
}

bool same_id(const std::array<char, 97>& left, const char* right) {
    return right != nullptr && std::strcmp(left.data(), right) == 0;
}

}  // namespace

bool SurfaceRegistry::valid(
    const DomainSurfaceSnapshot& snapshot) {
    constexpr std::uint8_t kAllSurfaces = (1U << 6U) - 1U;
    if (!bounded_text(snapshot.app_id) ||
        snapshot.domain_revision == 0 ||
        snapshot.declared_mask == 0 ||
        (snapshot.declared_mask & ~kAllSurfaces) != 0) {
        return false;
    }
    for (std::size_t index = 0;
         index < snapshot.projections.size();
         ++index) {
        const auto declared =
            (snapshot.declared_mask & (1U << index)) != 0;
        const auto& projection = snapshot.projections[index];
        if (!declared) {
            if (projection.revision != 0) return false;
            continue;
        }
        if (projection.revision != snapshot.domain_revision) {
            return false;
        }
        // Inactive notification/ongoing surfaces intentionally carry no
        // presentation. Every other declared projection needs primary text.
        const auto lifecycle_surface =
            index == static_cast<std::size_t>(
                SurfaceKind::notification) ||
            index == static_cast<std::size_t>(
                SurfaceKind::ongoing);
        if ((!lifecycle_surface || projection.active) &&
            !bounded_text(projection.primary)) {
            return false;
        }
        if (!bounded_text(projection.secondary, true) ||
            !bounded_text(projection.action_id, true)) {
            return false;
        }
    }
    return true;
}

DomainSurfaceSnapshot* SurfaceRegistry::find_mutable(
    const char* app_id) {
    if (app_id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_id(snapshots_[index].app_id, app_id)) {
            return &snapshots_[index];
        }
    }
    return nullptr;
}

const DomainSurfaceSnapshot* SurfaceRegistry::find(
    const char* app_id) const {
    if (app_id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_id(snapshots_[index].app_id, app_id)) {
            return &snapshots_[index];
        }
    }
    return nullptr;
}

bool SurfaceRegistry::publish(
    const DomainSurfaceSnapshot& snapshot) {
    if (!valid(snapshot) || quarantined(snapshot.app_id.data())) {
        return false;
    }
    auto* existing = find_mutable(snapshot.app_id.data());
    if (existing != nullptr) {
        if (snapshot.domain_revision <= existing->domain_revision) {
            return false;
        }
        *existing = snapshot;
        return true;
    }
    if (size_ >= snapshots_.size()) return false;
    snapshots_[size_++] = snapshot;
    return true;
}

std::size_t SurfaceRegistry::size() const {
    return size_;
}

std::size_t SurfaceRegistry::active_count(SurfaceKind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    const auto bit = surface_bit(kind);
    std::size_t count = 0;
    for (std::size_t item = 0; item < size_; ++item) {
        const auto& snapshot = snapshots_[item];
        if ((snapshot.declared_mask & bit) != 0 &&
            snapshot.projections[index].active) {
            ++count;
        }
    }
    return count;
}

bool SurfaceRegistry::quarantined(const char* app_id) const {
    if (app_id == nullptr) return false;
    for (std::size_t index = 0;
         index < quarantined_count_;
         ++index) {
        if (std::strcmp(
                quarantined_ids_[index].data(), app_id) == 0) {
            return true;
        }
    }
    return false;
}

bool SurfaceRegistry::quarantine(const char* app_id) {
    if (app_id == nullptr || app_id[0] == '\0' ||
        quarantined(app_id) ||
        quarantined_count_ >= quarantined_ids_.size()) {
        return false;
    }
    const auto length = std::strlen(app_id);
    if (length >= quarantined_ids_[0].size()) return false;
    std::memcpy(
        quarantined_ids_[quarantined_count_].data(),
        app_id,
        length + 1);
    ++quarantined_count_;
    auto* snapshot = find_mutable(app_id);
    if (snapshot != nullptr) {
        for (auto& projection : snapshot->projections) {
            projection.active = false;
        }
    }
    return true;
}

bool SurfaceRegistry::restore(const char* app_id) {
    if (app_id == nullptr) return false;
    for (std::size_t index = 0;
         index < quarantined_count_;
         ++index) {
        if (std::strcmp(
                quarantined_ids_[index].data(), app_id) != 0) {
            continue;
        }
        for (std::size_t move = index + 1;
             move < quarantined_count_;
             ++move) {
            quarantined_ids_[move - 1] =
                quarantined_ids_[move];
        }
        quarantined_ids_[--quarantined_count_] = {};
        return true;
    }
    return false;
}

void SurfaceRegistry::sync_shell_counts(ShellState& shell) const {
    const auto bounded = [](std::size_t value) {
        return static_cast<std::uint8_t>(
            std::min<std::size_t>(value, 255));
    };
    shell.publish_surface_counts(
        bounded(active_count(SurfaceKind::glance)),
        bounded(active_count(SurfaceKind::notification)),
        bounded(active_count(SurfaceKind::ongoing)));
}

}  // namespace m3e::os
