#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/band_state.hpp"

using Catch::Approx;
using spectr::BandField;
using spectr::Layout;
using spectr::kMaxBands;

TEST_CASE("BandField is 64 slots by default") {
    BandField f;
    REQUIRE(f.bands.size() == kMaxBands);
}

TEST_CASE("BandField default slots are neutral") {
    BandField f;
    for (const auto& b : f.bands) {
        CHECK(b.gain_db == 0.0f);
        CHECK_FALSE(b.muted);
    }
}

TEST_CASE("reset() restores neutral state from arbitrary input") {
    BandField f;
    f.bands[0]  = {.gain_db = -24.0f, .muted = false};
    f.bands[10] = {.gain_db =  +6.0f, .muted = true};
    f.bands[63] = {.gain_db = -60.0f, .muted = true};

    f.reset();

    for (const auto& b : f.bands) {
        CHECK(b.gain_db == 0.0f);
        CHECK_FALSE(b.muted);
    }
}

TEST_CASE("linear_gain returns unity at 0 dB") {
    BandField f;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        CHECK(f.linear_gain(i) == Approx(1.0f).margin(1e-6f));
    }
}

TEST_CASE("linear_gain returns 0 for muted bands") {
    BandField f;
    f.bands[5].gain_db = +12.0f;  // would otherwise be ~3.98
    f.bands[5].muted = true;
    CHECK(f.linear_gain(5) == 0.0f);
}

TEST_CASE("linear_gain is monotonic in dB") {
    BandField f;
    f.bands[0].gain_db = -24.0f;
    f.bands[1].gain_db = -12.0f;
    f.bands[2].gain_db =   0.0f;
    f.bands[3].gain_db =  +6.0f;
    CHECK(f.linear_gain(0) < f.linear_gain(1));
    CHECK(f.linear_gain(1) < f.linear_gain(2));
    CHECK(f.linear_gain(2) < f.linear_gain(3));
}

TEST_CASE("Layout visible_count matches enumerator value") {
    CHECK(spectr::visible_count(Layout::Bands32) == 32);
    CHECK(spectr::visible_count(Layout::Bands40) == 40);
    CHECK(spectr::visible_count(Layout::Bands48) == 48);
    CHECK(spectr::visible_count(Layout::Bands56) == 56);
    CHECK(spectr::visible_count(Layout::Bands64) == 64);
}

TEST_CASE("linear_gain on out-of-range index is safe") {
    BandField f;
    CHECK(f.linear_gain(999) == 1.0f);
}
