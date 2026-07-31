#include <cassert>
#include <cstring>

#include "m3e/os/surface_registry.hpp"

namespace {

void text(char* destination, const char* source) {
    std::strcpy(destination, source);
}

}  // namespace

int main() {
    using namespace m3e::os;

    DomainSurfaceSnapshot timer{};
    text(timer.app_id.data(), "dev.doodad.timer");
    timer.domain_revision = 7;
    timer.observed_at_ms = 55'000;
    timer.declared_mask =
        surface_bit(SurfaceKind::app) |
        surface_bit(SurfaceKind::glance) |
        surface_bit(SurfaceKind::complication) |
        surface_bit(SurfaceKind::notification) |
        surface_bit(SurfaceKind::ongoing) |
        surface_bit(SurfaceKind::voice);
    for (auto& projection : timer.projections) {
        projection.revision = timer.domain_revision;
    }
    auto& app =
        timer.projections[static_cast<std::size_t>(SurfaceKind::app)];
    app.active = true;
    text(app.primary.data(), "0:05");
    auto& glance =
        timer.projections[static_cast<std::size_t>(SurfaceKind::glance)];
    glance.active = true;
    text(glance.primary.data(), "0:05");
    text(glance.secondary.data(), "Tea");
    text(glance.action_id.data(), "timer.cancel");
    auto& complication =
        timer.projections[
            static_cast<std::size_t>(SurfaceKind::complication)];
    complication.active = true;
    text(complication.primary.data(), "0:05");
    auto& ongoing =
        timer.projections[
            static_cast<std::size_t>(SurfaceKind::ongoing)];
    ongoing.active = true;
    text(ongoing.primary.data(), "Tea timer");
    text(ongoing.secondary.data(), "0:05 remaining");
    auto& voice =
        timer.projections[static_cast<std::size_t>(SurfaceKind::voice)];
    voice.active = true;
    text(voice.primary.data(), "Cancel my tea timer");

    SurfaceRegistry registry;
    assert(registry.publish(timer));
    assert(registry.size() == 1);
    assert(registry.active_count(SurfaceKind::glance) == 1);
    assert(registry.active_count(SurfaceKind::notification) == 0);
    assert(registry.active_count(SurfaceKind::ongoing) == 1);

    ShellState shell;
    assert(shell.initialize());
    registry.sync_shell_counts(shell);
    assert(shell.snapshot().live_card_count == 1);
    assert(shell.snapshot().notification_count == 0);
    assert(shell.snapshot().ongoing_count == 1);

    // One mismatched projection rejects the entire update, preserving the
    // previously published revision.
    auto inconsistent = timer;
    inconsistent.domain_revision = 8;
    inconsistent.projections[
        static_cast<std::size_t>(SurfaceKind::app)].revision = 8;
    assert(!registry.publish(inconsistent));
    assert(registry.find("dev.doodad.timer")->domain_revision == 7);

    auto fired = timer;
    fired.domain_revision = 8;
    fired.observed_at_ms = 60'000;
    for (auto& projection : fired.projections) {
        projection.revision = 8;
    }
    fired.projections[
        static_cast<std::size_t>(SurfaceKind::notification)].active = true;
    text(
        fired.projections[
            static_cast<std::size_t>(SurfaceKind::notification)]
            .primary.data(),
        "Timer complete");
    fired.projections[
        static_cast<std::size_t>(SurfaceKind::ongoing)].active = false;
    fired.projections[
        static_cast<std::size_t>(SurfaceKind::ongoing)].primary[0] = '\0';
    assert(registry.publish(fired));
    assert(registry.active_count(SurfaceKind::notification) == 1);
    assert(registry.active_count(SurfaceKind::ongoing) == 0);
    assert(!registry.publish(fired));

    // Quarantine immediately removes all host-owned projections and rejects
    // new publications until recovery explicitly restores the package.
    assert(registry.quarantine("dev.doodad.timer"));
    assert(registry.quarantined("dev.doodad.timer"));
    assert(registry.active_count(SurfaceKind::glance) == 0);
    auto blocked = fired;
    blocked.domain_revision = 9;
    for (auto& projection : blocked.projections) {
        projection.revision = 9;
    }
    assert(!registry.publish(blocked));
    assert(registry.restore("dev.doodad.timer"));
    assert(!registry.quarantined("dev.doodad.timer"));
    assert(registry.publish(blocked));

    return 0;
}
