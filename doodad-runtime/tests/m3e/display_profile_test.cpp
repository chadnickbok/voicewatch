#include "m3e/foundation/display_profile.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "display_profile_test: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace m3e;

    expect(density_q8_8(5, 4) == 320, "1.25 density must be exact in Q8.8");
    expect(dp_edge_to_px(0, 320) == 0, "zero edge");
    expect(dp_edge_to_px(1, 320) == 1, "one dp rounds to one pixel");
    expect(dp_edge_to_px(4, 320) == 5, "four dp maps to five pixels");
    expect(dp_edge_to_px(8, 320) == 10, "eight dp maps to ten pixels");
    expect(dp_edge_to_px(48, 320) == 60, "touch target maps to 60 pixels");
    expect(dp_edge_to_px(192, 320) == 240, "watch edge maps to 240 pixels");
    expect(dp_edge_to_px(-4, 320) == -5, "negative edges round symmetrically");

    // Mapping shared edges prevents independently rounded children from
    // accumulating layout error.
    const auto first = dp_span_to_px(0, 1, 320);
    const auto second = dp_span_to_px(1, 1, 320);
    const auto third = dp_span_to_px(2, 1, 320);
    const auto fourth = dp_span_to_px(3, 1, 320);
    expect(first + second + third + fourth == 5,
           "four one-dp spans must fill the same five pixels as one 4dp span");

    expect(profile_is_valid(watch_square_192), "square profile must be valid");
    expect(profile_is_valid(twatch_ultra_portrait),
           "T-Watch Ultra profile must be valid");
    expect(twatch_ultra_portrait.physical_width_px == 410,
           "T-Watch Ultra width");
    expect(twatch_ultra_portrait.physical_height_px == 502,
           "T-Watch Ultra height");
    expect(logical_x_to_physical_px(twatch_ultra_portrait, 205) == 410,
           "T-Watch Ultra logical right edge");
    expect(logical_y_to_physical_px(twatch_ultra_portrait, 251) == 502,
           "T-Watch Ultra logical bottom edge");
    expect(profile_is_valid(cores3_watch_preview),
           "CoreS3 preview profile must be valid");
    expect(profile_is_valid(wear_round_192_reference),
           "round reference profile must be valid");
    expect(profile_is_valid(wear_large_225_reference),
           "large reference profile must be valid");

    expect(logical_x_to_physical_px(cores3_watch_preview, 0) == 40,
           "CoreS3 logical origin must start after left rail");
    expect(logical_x_to_physical_px(cores3_watch_preview, 192) == 280,
           "CoreS3 logical edge must end before right rail");
    expect(logical_y_to_physical_px(cores3_watch_preview, 192) == 240,
           "CoreS3 logical bottom must match panel bottom");
    expect(find_display_profile("watch_square_192") == &watch_square_192,
           "profile lookup");
    expect(find_display_profile("twatch_ultra_410x502") ==
               &twatch_ultra_portrait,
           "T-Watch Ultra profile lookup");
    expect(find_display_profile("missing") == nullptr,
           "unknown profile lookup");

    DisplayProfile invalid = watch_square_192;
    invalid.viewport_origin_x_px = 1;
    expect(!profile_is_valid(invalid), "overflowing viewport must be rejected");

    return 0;
}
