#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3e::navigation {

enum class LayerOwner : std::uint8_t {
    application,
    system,
};

struct Route {
    std::array<char, 65> id;
    LayerOwner owner;
    std::uint32_t generation;
    std::uint32_t focus_key;
    std::int32_t scroll_anchor;
};

class RouteStack {
 public:
    static constexpr std::size_t kCapacity = 8;

    bool reset(const char* home_route);
    bool push(
        const char* route,
        LayerOwner owner,
        std::uint32_t generation);
    bool pop(LayerOwner requester);
    bool show_overlay(const char* route, LayerOwner owner);
    bool dismiss_overlay(LayerOwner requester);
    bool snapshot_active(std::uint32_t focus_key, std::int32_t scroll_anchor);
    const Route* restore_target(std::uint32_t active_generation) const;
    const Route* active() const;
    const Route* overlay() const;
    std::size_t depth() const;

 private:
    std::array<Route, kCapacity> routes_{};
    std::size_t depth_ = 0;
    Route overlay_{};
    bool overlay_visible_ = false;
};

}  // namespace m3e::navigation
