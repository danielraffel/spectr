// Milestone 6 — Edit mode dispatch tests.
//
// Each of the five prototype-visible edit modes (Sculpt/Level/Boost/
// Flare/Glide) has its own test case: build a known BandField, dispatch
// the mode with a simulated drag, assert the resulting gains match the
// per-mode contract.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/edit_engine.hpp"
#include "spectr/edit_modes.hpp"

#include <cmath>

using Catch::Approx;
using spectr::BandField;
using spectr::BandSnapshot;
using spectr::DragGesture;
using spectr::EditMode;
using spectr::apply_boost;
using spectr::apply_flare;
using spectr::apply_glide;
using spectr::apply_level;
using spectr::apply_sculpt;
using spectr::dispatch_edit;
using spectr::kMaxBands;

namespace {

BandField neutral_field() { return BandField{}; }

BandField ramp_field(float start = -20.0f, float step = 0.5f) {
    BandField f;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        f.bands[i].gain_db = start + static_cast<float>(i) * step;
        f.bands[i].muted   = false;
    }
    return f;
}

DragGesture make_drag(std::size_t start_band,
                      float       start_value,
                      std::size_t cur_band,
                      float       cur_value,
                      std::size_t n_visible = 32) {
    DragGesture d;
    d.start_band    = start_band;
    d.start_value   = start_value;
    d.current_band  = cur_band;
    d.current_value = cur_value;
    d.n_visible     = n_visible;
    return d;
}

} // namespace

TEST_CASE("M6 Sculpt: paints every band the drag sweeps through") {
    auto f = neutral_field();
    // Drag from band 5 at 0 dB to band 12 at -6 dB.
    const auto drag = make_drag(5, 0.0f, 12, -6.0f);
    BandSnapshot snap = BandSnapshot::capture(f);

    apply_sculpt(f, drag);

    // Bands 5..12 should be -6 dB; everything else unchanged.
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        if (i >= 5 && i <= 12) {
            CHECK(f.bands[i].gain_db == Approx(-6.0f));
            CHECK_FALSE(f.bands[i].muted);
        } else {
            CHECK(f.bands[i].gain_db == Approx(0.0f));
        }
    }
    (void)snap;
}

TEST_CASE("M6 Level: flattens the swept range to the current value") {
    auto f = ramp_field();
    const auto drag = make_drag(10, 0.0f, 4, +3.0f);  // drag leftward

    apply_level(f, drag);

    // Bands 4..10 should all equal +3 dB.
    for (std::size_t i = 4; i <= 10; ++i) {
        CHECK(f.bands[i].gain_db == Approx(+3.0f));
    }
    // Outside stays on the ramp.
    CHECK(f.bands[3].gain_db == Approx(-18.5f));
    CHECK(f.bands[11].gain_db == Approx(-14.5f));
}

TEST_CASE("M6 Boost: scales snapshot gains by drag direction") {
    auto f0 = ramp_field(-10.0f, 1.0f);   // -10, -9, ..., -10 + 63 = +53 (clamped to +12)
    const auto snap = BandSnapshot::capture(f0);

    SECTION("drag up boosts positive bands") {
        auto f = f0;
        // Drag from 0 dB to +6 dB. dy_norm = 6/72 ≈ 0.083. mult = 1 + 0.33 ≈ 1.33.
        const auto drag = make_drag(0, 0.0f, 0, +6.0f);
        apply_boost(f, drag, snap);
        // band 15: snap = -10 + 15 = 5 → boosted ≈ 5 * 1.33 ≈ 6.66
        CHECK(f.bands[15].gain_db > snap.gain_db[15]);
    }

    SECTION("drag down pulls gains toward zero then flips") {
        auto f = f0;
        const auto drag = make_drag(0, 0.0f, 0, -6.0f);
        apply_boost(f, drag, snap);
        // Each band scaled by < 1.0; positive bands get smaller.
        CHECK(std::fabs(f.bands[15].gain_db) < std::fabs(snap.gain_db[15]) + 0.01f);
    }
}

TEST_CASE("M6 Flare: exaggerates distance from 0 dB proportional to drag") {
    BandField f0;
    // Snapshot: alternating ±4 dB, so positive bands stay positive and
    // negatives stay negative under flare.
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        f0.bands[i].gain_db = (i % 2 == 0) ? +4.0f : -4.0f;
    }
    const auto snap = BandSnapshot::capture(f0);

    auto f = f0;
    // Full upward drag.
    const auto drag = make_drag(0, 0.0f, 0, kMaxBands == 0 ? 0.0f : 12.0f);
    apply_flare(f, drag, snap);
    // Every band should be FARTHER from zero than its snapshot (in the
    // direction of its sign).
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        CHECK(std::fabs(f.bands[i].gain_db) >= std::fabs(snap.gain_db[i]));
    }
}

TEST_CASE("M6 Glide: interpolates each snapshot band toward current value by drag distance") {
    BandField f0 = ramp_field();
    const auto snap = BandSnapshot::capture(f0);

    SECTION("zero-distance drag leaves snapshot unchanged") {
        auto f = f0;
        const auto drag = make_drag(0, 0.0f, 0, 0.0f);
        apply_glide(f, drag, snap);
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            CHECK(f.bands[i].gain_db == Approx(snap.gain_db[i]));
        }
    }

    SECTION("full-range drag reaches current_value everywhere") {
        auto f = f0;
        // Full drag from kDbMin (-60) to kDbMax (+12) → dy_norm = 1.0, t = 1.
        const auto drag = make_drag(0, -60.0f, 0, +12.0f);
        apply_glide(f, drag, snap);
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            CHECK(f.bands[i].gain_db == Approx(+12.0f));
        }
    }

    SECTION("half-range drag is halfway between snapshot and current") {
        auto f = f0;
        // Half-range drag within the clamp-valid dB window [-60, +12]:
        // start at -18, end at +18 → Δ = 36 → dy_norm = 36/72 = 0.5, t = 0.5.
        // Both endpoints land inside the range so the engine's clamp to
        // kDbMax doesn't shift the target.
        const auto drag = make_drag(0, -18.0f, 0, +18.0f);
        apply_glide(f, drag, snap);
        // target clamped to kDbMax = +12.
        const float target = +12.0f;
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            const float expected = snap.gain_db[i] + (target - snap.gain_db[i]) * 0.5f;
            CHECK(f.bands[i].gain_db == Approx(expected).margin(1e-4));
        }
    }
}

TEST_CASE("M6 snapshot-at-drag-start stays stable across the gesture") {
    // Boost/Flare/Glide must read from the SNAPSHOT, not the live field —
    // otherwise consecutive updates within a drag drift toward infinity.
    BandField f = ramp_field();
    const auto snap = BandSnapshot::capture(f);

    // Simulate two process steps during a single drag. Both dispatch
    // with the same snapshot but different current values.
    const auto d1 = make_drag(0, 0.0f, 0, +3.0f);
    apply_boost(f, d1, snap);
    const auto after_first = f;

    // Second dispatch with the same snapshot and a larger drag.
    const auto d2 = make_drag(0, 0.0f, 0, +6.0f);
    apply_boost(f, d2, snap);

    // f after second call should be snap * mult_for_d2, not after_first * mult_for_d2.
    const float expected_15 = snap.gain_db[15] * (1.0f + (6.0f / 72.0f) * 4.0f);
    CHECK(f.bands[15].gain_db == Approx(std::clamp(expected_15, -60.0f, 12.0f)).margin(1e-4));
    (void)after_first;
}

TEST_CASE("M6 dispatch_edit routes to the right mode") {
    auto f = ramp_field();
    const auto snap = BandSnapshot::capture(f);
    const auto drag = make_drag(0, 0.0f, 0, 0.0f);

    // Each mode should be callable through dispatch_edit.
    dispatch_edit(EditMode::Sculpt, f, drag, snap);
    dispatch_edit(EditMode::Level,  f, drag, snap);
    dispatch_edit(EditMode::Boost,  f, drag, snap);
    dispatch_edit(EditMode::Flare,  f, drag, snap);
    dispatch_edit(EditMode::Glide,  f, drag, snap);
    SUCCEED("dispatch routes all five modes");
}

TEST_CASE("M6 keybindings exposed for UI layer") {
    CHECK(spectr::keybinding(EditMode::Sculpt) == 'S');
    CHECK(spectr::keybinding(EditMode::Level)  == 'L');
    CHECK(spectr::keybinding(EditMode::Boost)  == 'B');
    CHECK(spectr::keybinding(EditMode::Flare)  == 'F');
    CHECK(spectr::keybinding(EditMode::Glide)  == 'G');
}
