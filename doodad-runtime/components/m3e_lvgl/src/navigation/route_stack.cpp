#include "m3e/navigation/route_stack.hpp"

#include <cstring>

namespace m3e::navigation {
namespace {

bool valid_route(const char* route) {
    if (route == nullptr || route[0] < 'a' || route[0] > 'z') return false;
    const auto length = std::strlen(route);
    if (length == 0 || length > 64) return false;
    for (std::size_t index = 0; index < length; ++index) {
        const char value = route[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '.' || value == '-')) {
            return false;
        }
    }
    return true;
}

bool assign(
    Route& target,
    const char* id,
    LayerOwner owner,
    std::uint32_t generation) {
    if (!valid_route(id)) return false;
    std::strncpy(target.id.data(), id, target.id.size() - 1);
    target.id.back() = '\0';
    target.owner = owner;
    target.generation = generation;
    target.focus_key = 0;
    target.scroll_anchor = 0;
    return true;
}

}  // namespace

bool RouteStack::reset(const char* home_route) {
    Route home{};
    if (!assign(home, home_route, LayerOwner::system, 0)) return false;
    routes_[0] = home;
    depth_ = 1;
    overlay_visible_ = false;
    return true;
}

bool RouteStack::push(
    const char* route,
    LayerOwner owner,
    std::uint32_t generation) {
    if (depth_ == 0 || depth_ >= kCapacity) return false;
    Route next{};
    if (!assign(next, route, owner, generation)) return false;
    routes_[depth_++] = next;
    return true;
}

bool RouteStack::pop(LayerOwner requester) {
    if (depth_ <= 1) return false;
    if (routes_[depth_ - 1].owner == LayerOwner::system &&
        requester != LayerOwner::system) {
        return false;
    }
    --depth_;
    return true;
}

bool RouteStack::show_overlay(const char* route, LayerOwner owner) {
    if (overlay_visible_) return false;
    Route next{};
    if (!assign(next, route, owner, active()->generation)) return false;
    overlay_ = next;
    overlay_visible_ = true;
    return true;
}

bool RouteStack::dismiss_overlay(LayerOwner requester) {
    if (!overlay_visible_ ||
        (overlay_.owner == LayerOwner::system &&
         requester != LayerOwner::system)) {
        return false;
    }
    overlay_visible_ = false;
    return true;
}

bool RouteStack::snapshot_active(
    std::uint32_t focus_key,
    std::int32_t scroll_anchor) {
    if (depth_ == 0) return false;
    routes_[depth_ - 1].focus_key = focus_key;
    routes_[depth_ - 1].scroll_anchor = scroll_anchor;
    return true;
}

const Route* RouteStack::restore_target(
    std::uint32_t active_generation) const {
    const auto* route = active();
    if (route == nullptr || route->generation != active_generation) {
        return nullptr;
    }
    return route;
}

const Route* RouteStack::active() const {
    return depth_ == 0 ? nullptr : &routes_[depth_ - 1];
}

const Route* RouteStack::overlay() const {
    return overlay_visible_ ? &overlay_ : nullptr;
}

std::size_t RouteStack::depth() const {
    return depth_;
}

}  // namespace m3e::navigation
