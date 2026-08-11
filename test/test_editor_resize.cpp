#include <catch2/catch_test_macros.hpp>

#include <limits>

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

TEST_CASE("native editor resize validation rejects non-finite and non-positive input") {
    CHECK_FALSE(normalize_editor_resize(
        std::numeric_limits<double>::quiet_NaN(), 645.0));
    CHECK_FALSE(normalize_editor_resize(
        990.0, std::numeric_limits<double>::infinity()));
    CHECK_FALSE(normalize_editor_resize(0.0, 645.0));
    CHECK_FALSE(normalize_editor_resize(990.0, -1.0));
}

TEST_CASE("native editor resize validation clamps and restores authored aspect") {
    CHECK(normalize_editor_resize(1.0, 1.0)
        == EditorResizeSize{792, 516});
    CHECK(normalize_editor_resize(990.0, 1.0)
        == EditorResizeSize{990, 645});
    CHECK(normalize_editor_resize(1980.0, 99999.0)
        == EditorResizeSize{1980, 1290});
    CHECK(normalize_editor_resize(99999.0, 99999.0)
        == EditorResizeSize{2640, 1720});
}
