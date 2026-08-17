#include <catch2/catch_test_macros.hpp>

#include "spectr/editor_resize.hpp"
#include "spectr/spectr.hpp"

using namespace spectr;

TEST_CASE("Spectr editor publishes a smaller fixed-aspect host contract") {
    Spectr processor;
    const auto hints = processor.view_size();

    CHECK(hints.preferred_width == 990);
    CHECK(hints.preferred_height == 645);
    CHECK(hints.min_width == 792);
    CHECK(hints.min_height == 516);
    CHECK(hints.max_width == 2640);
    CHECK(hints.max_height == 1720);
    CHECK(hints.aspect_ratio == kEditorAspectRatio);
}
