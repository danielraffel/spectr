#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "spectr/editor_resize.hpp"
#include "spectr/spectr.hpp"

using namespace spectr;

TEST_CASE("Spectr editor publishes a fixed-aspect host contract") {
    Spectr processor;
    const auto hints = processor.view_size();

    // Preferred opens at 75% of the authored design box. See test_spectr.cpp.
    CHECK(hints.preferred_width == 990);
    CHECK(hints.preferred_height == 645);
    CHECK(hints.min_width == 792);
    CHECK(hints.min_height == 516);
    CHECK(hints.max_width == 2640);
    CHECK(hints.max_height == 1720);
    CHECK(hints.aspect_ratio == kEditorAspectRatio);
}

// The editor owns its resize gesture because AU v2 has no host->plugin resize
// callback (see resolve_editor_resize). These cases pin the two guarantees the
// grip depends on: the authored aspect ratio survives every drag, and the
// result never leaves the advertised host bounds.
TEST_CASE("resize grip preserves the authored aspect ratio") {
    const auto grown = resolve_editor_resize(990, 645, 330.0, 0.0);
    CHECK(grown.width == 1320);
    CHECK(grown.height == 860);

    const auto shrunk = resolve_editor_resize(1320, 860, -330.0, 0.0);
    CHECK(shrunk.width == 990);
    CHECK(shrunk.height == 645);

    // Every reachable size stays on the authored ratio to within rounding.
    for (double dx = -2000.0; dx <= 2000.0; dx += 37.0) {
        const auto target = resolve_editor_resize(990, 645, dx, 0.0);
        const double ratio = static_cast<double>(target.width)
            / static_cast<double>(target.height);
        CHECK(std::abs(ratio - kEditorAspectRatio) < 0.005);
    }
}

TEST_CASE("resize grip clamps to the advertised host bounds") {
    const auto floored = resolve_editor_resize(990, 645, -100000.0, -100000.0);
    CHECK(floored.width == kEditorMinimumWidth);
    CHECK(floored.height == kEditorMinimumHeight);

    const auto ceilinged = resolve_editor_resize(990, 645, 100000.0, 100000.0);
    CHECK(ceilinged.width == kEditorMaximumWidth);
    CHECK(ceilinged.height == kEditorMaximumHeight);
}

TEST_CASE("resize grip follows whichever axis the pointer moved further") {
    // A vertical-only drag must still resize — a grip that only tracked dx
    // would feel dead when dragged straight down.
    const auto vertical = resolve_editor_resize(990, 645, 0.0, 215.0);
    CHECK(vertical.height == 860);
    CHECK(vertical.width == 1320);

    // Width-equivalent comparison: dy*aspect (215*1.535 = 330) ties dx=330, and
    // a larger dx must win outright.
    const auto horizontal = resolve_editor_resize(990, 645, 660.0, 215.0);
    CHECK(horizontal.width == 1650);
}
