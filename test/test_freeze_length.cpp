// Freeze's musical Length: the model alone (freeze_length.hpp) -- the one
// validation path, the canonical fraction set, labels, exact arithmetic and
// the conversion to seconds. The processor, bridge and editor tests build on
// these definitions; nothing here renders audio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/freeze_length.hpp"

#include <set>
#include <string>

using spectr::FreezeLength;
using spectr::LengthError;
using spectr::LengthFraction;

namespace {
int idx(std::string_view text) { return spectr::fraction_index_from_text(text); }
} // namespace

TEST_CASE("Freeze Length validation follows the spec's table", "[freeze-length][model]") {
    // The spec's cases, through the one validation function.
    CHECK(spectr::validate_length(0, idx("0")) == LengthError::zero_length);      // 0 + 0
    CHECK(spectr::validate_length(0, idx("1/8")) == LengthError::none);           // 0 + 1/8
    CHECK(spectr::validate_length(1, idx("0")) == LengthError::none);             // 1 + 0
    CHECK(spectr::validate_length(1, idx("1/8")) == LengthError::none);           // 1 + 1/8
    CHECK(spectr::validate_length(128, idx("0")) == LengthError::none);           // 128 + 0
    CHECK(spectr::validate_length(129, idx("0")) == LengthError::bars_above_limit);
    CHECK(spectr::validate_length(-1, idx("1/8")) == LengthError::bars_below_zero);
    // A fraction outside the shipped set cannot even be named.
    for (const char* text : {"1/5", "2/5", "1/64", "3/4 ", "0.125", "", "1/7", "16/16"}) {
        INFO(text);
        CHECK(idx(text) == -1);
        CHECK(spectr::validate_length(1, idx(text)) == LengthError::unknown_fraction);
        CHECK_FALSE(spectr::make_length(1, idx(text)).has_value());
    }
    CHECK(spectr::validate_length(1, static_cast<int>(LengthFraction::count))
          == LengthError::unknown_fraction);
    CHECK_FALSE(spectr::make_length(0, 0).has_value());
    CHECK(spectr::make_length(0, idx("1/32")) == FreezeLength{0, LengthFraction::f1_32});
}

TEST_CASE("Freeze Length's fraction set is the product list, ascending and exact",
          "[freeze-length][model]") {
    const std::string expected[] = {"0", "1/32", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4",
                                    "1/3", "3/8", "1/2", "5/8", "2/3", "3/4", "5/6", "7/8",
                                    "15/16"};
    REQUIRE(spectr::kLengthFractions.size() == std::size(expected));
    std::int64_t previous = -1;
    for (std::size_t i = 0; i < spectr::kLengthFractions.size(); ++i) {
        const auto& f = spectr::kLengthFractions[i];
        INFO(f.text);
        CHECK(f.text == expected[i]);
        CHECK(idx(f.text) == static_cast<int>(i));
        CHECK(spectr::kLengthUnit % f.denominator == 0);
        const auto units = spectr::length_units({0, static_cast<LengthFraction>(i)});
        CHECK(units > previous);
        previous = units;
    }
}

TEST_CASE("Freeze Length labels read the way the header shows them", "[freeze-length][model]") {
    // One formatting function: mixed numbers, "bar" up to one bar, "bars"
    // past it, a lone fraction when there is no whole bar.
    struct Row { int bars; LengthFraction fraction; const char* label; };
    const Row rows[] = {
        {0, LengthFraction::f1_32, "1/32 bar"},
        {0, LengthFraction::f1_2, "1/2 bar"},
        {0, LengthFraction::f7_8, "7/8 bar"},
        {0, LengthFraction::f15_16, "15/16 bar"},
        {1, LengthFraction::zero, "1 bar"},
        {1, LengthFraction::f1_8, "1 1/8 bars"},
        {2, LengthFraction::zero, "2 bars"},
        {2, LengthFraction::f3_16, "2 3/16 bars"},
        {128, LengthFraction::zero, "128 bars"},
    };
    for (const auto& row : rows)
        CHECK(spectr::length_label({row.bars, row.fraction}) == row.label);
    // The presets' labels are the same formatter's (their order is pinned by
    // "the parameter value selects a preset or Custom").
    for (std::size_t i = 0; i < spectr::kLengthPresets.size(); ++i)
        CHECK(spectr::preset_index_of(spectr::kLengthPresets[i]) == static_cast<int>(i));
    CHECK(spectr::preset_index_of({1, LengthFraction::f1_8}) == -1);
    CHECK(spectr::preset_index_of({0, LengthFraction::f1_2}) == 9);
}

TEST_CASE("Freeze Length is exact: every valid length is distinct and packs losslessly",
          "[freeze-length][model]") {
    std::set<std::int64_t> seen;
    int valid = 0;
    for (int bars = -2; bars <= spectr::kMaxLengthBars + 2; ++bars)
        for (int f = -1; f <= static_cast<int>(LengthFraction::count); ++f) {
            const auto made = spectr::make_length(bars, f);
            if (!made) continue;
            ++valid;
            CHECK(spectr::unpack_length(spectr::pack_length(*made)) == *made);
            CHECK(seen.insert(spectr::length_units(*made)).second);
        }
    CHECK(valid == 129 * 17 - 1);
    // 1 + 1/12 is 1.0833... bars as a double, but 104 units exactly.
    CHECK(spectr::length_units({1, LengthFraction::f1_12}) == 104);
    CHECK(spectr::length_units({2, LengthFraction::f3_16}) == 210);
    CHECK(spectr::length_units({0, LengthFraction::f1_32}) == 3);
}

TEST_CASE("Freeze Length in seconds follows tempo and meter", "[freeze-length][model]") {
    const FreezeLength bar{1, LengthFraction::zero};
    CHECK(spectr::length_seconds(bar, 120.0, 4, 4) == Catch::Approx(2.0));
    CHECK(spectr::length_seconds(bar, 60.0, 4, 4) == Catch::Approx(4.0));
    CHECK(spectr::length_seconds(bar, 90.0, 3, 4) == Catch::Approx(2.0));
    CHECK(spectr::length_seconds(bar, 100.0, 6, 8) == Catch::Approx(1.8));
    CHECK(spectr::length_seconds(bar, 120.0, 7, 8) == Catch::Approx(1.75));
    CHECK(spectr::length_seconds({1, LengthFraction::f1_8}, 120.0, 4, 4) == Catch::Approx(2.25));
    CHECK(spectr::length_seconds({128, LengthFraction::zero}, 60.0, 4, 4) == Catch::Approx(512.0));
    // No transport: 120 BPM 4/4.
    CHECK(spectr::length_seconds(bar, 0.0, 0, 0) == Catch::Approx(2.0));
    CHECK(spectr::length_seconds(bar, -5.0, 4, 0) == Catch::Approx(2.0));
}

TEST_CASE("Freeze Length: the parameter value selects a preset or Custom",
          "[freeze-length][model]") {
    // Menu order: every fraction of a bar on its own, then 1, 2, 4 and 8
    // bars -- ascending throughout -- then Custom.
    using spectr::kLengthPresets;
    REQUIRE(kLengthPresets.size() == 20);
    const char* labels[] = {
        "1/32 bar", "1/16 bar", "1/12 bar", "1/8 bar", "1/6 bar", "3/16 bar", "1/4 bar",
        "1/3 bar", "3/8 bar", "1/2 bar", "5/8 bar", "2/3 bar", "3/4 bar", "5/6 bar",
        "7/8 bar", "15/16 bar", "1 bar", "2 bars", "4 bars", "8 bars"};
    for (std::size_t i = 0; i < kLengthPresets.size(); ++i) {
        CHECK(spectr::length_label(kLengthPresets[i]) == labels[i]);
        CHECK(spectr::valid_length(kLengthPresets[i]));
        CHECK(spectr::preset_index_of(kLengthPresets[i]) == static_cast<int>(i));
        if (i > 0)
            CHECK(spectr::length_units(kLengthPresets[i - 1])
                  < spectr::length_units(kLengthPresets[i]));
    }
    // Every fraction of a bar but "0" is a preset of its own, exactly.
    for (std::size_t f = 1; f < spectr::kLengthFractions.size(); ++f)
        CHECK(spectr::preset_index_of({0, static_cast<LengthFraction>(f)})
              == static_cast<int>(f) - 1);
    CHECK(spectr::kLengthPresetCustom == 20);
    CHECK(spectr::kDefaultLengthPreset == 16);
    CHECK(kLengthPresets[spectr::kDefaultLengthPreset] == spectr::kDefaultFreezeLength);
    // A compound length is not a preset.
    CHECK(spectr::preset_index_of({1, LengthFraction::f1_8}) == -1);
    CHECK(spectr::preset_index_of({3, LengthFraction::zero}) == -1);
    CHECK(spectr::length_preset_from_param(0.0f) == 0);
    CHECK(spectr::length_preset_from_param(16.0f) == 16);
    CHECK(spectr::length_preset_from_param(16.4f) == 16);
    CHECK(spectr::length_preset_from_param(19.4f) == 19);
    CHECK(spectr::length_preset_from_param(19.6f) == spectr::kLengthPresetCustom);
    CHECK(spectr::length_preset_from_param(20.0f) == spectr::kLengthPresetCustom);
    CHECK(spectr::length_preset_from_param(99.0f) == spectr::kLengthPresetCustom);
    CHECK(spectr::length_preset_from_param(-3.0f) == 0);
}
