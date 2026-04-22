#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/viewport.hpp"

using Catch::Approx;
using spectr::Viewport;

TEST_CASE("Default viewport spans 20 Hz – 20 kHz") {
    Viewport v;
    CHECK(v.min_hz == 20.0f);
    CHECK(v.max_hz == 20000.0f);
    CHECK(v.valid());
}

TEST_CASE("Viewport rejects inverted or zero bounds") {
    Viewport v;
    v.min_hz = 1000.0f; v.max_hz = 500.0f;
    CHECK_FALSE(v.valid());

    v.min_hz = 0.0f;    v.max_hz = 20000.0f;
    CHECK_FALSE(v.valid());
}

TEST_CASE("band_center_hz anchors first and last bands at the viewport bounds") {
    Viewport v;  // 20 Hz .. 20 kHz
    for (std::size_t n : {32u, 40u, 48u, 56u, 64u}) {
        CHECK(v.band_center_hz(0, n)     == Approx(20.0f).epsilon(1e-4));
        CHECK(v.band_center_hz(n - 1, n) == Approx(20000.0f).epsilon(1e-4));
    }
}

TEST_CASE("band_center_hz is monotonically increasing") {
    Viewport v;
    const std::size_t n = 32;
    float prev = -1.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float hz = v.band_center_hz(i, n);
        CHECK(hz > prev);
        prev = hz;
    }
}

TEST_CASE("band_center_hz is roughly log-spaced") {
    Viewport v;
    const std::size_t n = 32;
    // ratio between consecutive band centers should be constant in log space
    const float r0 = v.band_center_hz(1, n) / v.band_center_hz(0, n);
    const float rN = v.band_center_hz(n - 1, n) / v.band_center_hz(n - 2, n);
    CHECK(r0 == Approx(rN).epsilon(1e-3));
}

TEST_CASE("band_for_hz is the inverse of band_center_hz at centers") {
    Viewport v;
    const std::size_t n = 48;
    for (std::size_t i = 0; i < n; ++i) {
        const float hz = v.band_center_hz(i, n);
        CHECK(v.band_for_hz(hz, n) == i);
    }
}

TEST_CASE("band_for_hz clamps outside the viewport") {
    Viewport v;
    CHECK(v.band_for_hz(5.0f, 32) == 0);
    CHECK(v.band_for_hz(40000.0f, 32) == 31);
}

TEST_CASE("Viewport remapping is deterministic across layout changes") {
    // A specific frequency should map to a well-defined band index for each
    // visible layout, and those indices should be stable call-to-call.
    Viewport v;
    const float freq = 1000.0f;
    const auto idx32 = v.band_for_hz(freq, 32);
    const auto idx64 = v.band_for_hz(freq, 64);
    CHECK(v.band_for_hz(freq, 32) == idx32);
    CHECK(v.band_for_hz(freq, 64) == idx64);
    // 64-band layout should place 1 kHz at a higher raw index than 32-band.
    CHECK(idx64 >= idx32);
}
