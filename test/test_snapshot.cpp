// Milestone 8 — Snapshot A/B + morph.
//
// Coverage:
//   - morph_fields() end conditions, midpoint, and continuity.
//   - SnapshotBank capture / copy / swap / populated bit.
//   - Spectr::capture_snapshot + apply_morph_to_live integration.
//   - Plugin-state round-trip preserves the bank (v2 format).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/snapshot.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>

#include <cmath>

using Catch::Approx;
using spectr::BandField;
using spectr::FieldSnapshot;
using spectr::Layout;
using spectr::SnapshotBank;
using spectr::Spectr;
using spectr::Viewport;
using spectr::kMaxBands;
using spectr::morph_fields;

namespace {

BandField make_field(float constant) {
    BandField f;
    for (auto& b : f.bands) b.gain_db = constant;
    return f;
}

BandField ramp_field(float start, float step) {
    BandField f;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        f.bands[i].gain_db = start + static_cast<float>(i) * step;
    }
    return f;
}

} // namespace

TEST_CASE("M8 morph_fields: t=0 returns A, t=1 returns B") {
    const auto A = make_field(-12.0f);
    const auto B = make_field(+6.0f);
    BandField out;

    morph_fields(out, A, B, 0.0f);
    for (std::size_t i = 0; i < kMaxBands; ++i)
        CHECK(out.bands[i].gain_db == Approx(-12.0f));

    morph_fields(out, A, B, 1.0f);
    for (std::size_t i = 0; i < kMaxBands; ++i)
        CHECK(out.bands[i].gain_db == Approx(+6.0f));
}

TEST_CASE("M8 morph_fields: midpoint is arithmetic mean in dB space") {
    const auto A = make_field(-12.0f);
    const auto B = make_field(+6.0f);
    BandField out;

    morph_fields(out, A, B, 0.5f);
    for (std::size_t i = 0; i < kMaxBands; ++i)
        CHECK(out.bands[i].gain_db == Approx(-3.0f));
}

TEST_CASE("M8 morph_fields: continuity across a sweep") {
    const auto A = ramp_field(-60.0f, 1.0f);
    const auto B = ramp_field(+12.0f, -0.5f);
    BandField out_prev, out_curr;

    morph_fields(out_prev, A, B, 0.0f);
    // Walk t from 0→1 in 0.05 steps; no two adjacent samples should
    // jump more than (|B - A|)*0.05 + epsilon for any band.
    for (float t = 0.05f; t <= 1.0f + 1e-4f; t += 0.05f) {
        morph_fields(out_curr, A, B, t);
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            const float maxA = std::max(A.bands[i].gain_db, B.bands[i].gain_db);
            const float minA = std::min(A.bands[i].gain_db, B.bands[i].gain_db);
            const float span = maxA - minA;
            const float step = std::abs(out_curr.bands[i].gain_db - out_prev.bands[i].gain_db);
            CHECK(step <= span * 0.05f + 1e-4f);
        }
        out_prev = out_curr;
    }
}

TEST_CASE("M8 morph_fields: clamps t to [0, 1]") {
    const auto A = make_field(-10.0f);
    const auto B = make_field(+10.0f);
    BandField out;

    morph_fields(out, A, B, -5.0f);
    CHECK(out.bands[0].gain_db == Approx(-10.0f));
    morph_fields(out, A, B, +5.0f);
    CHECK(out.bands[0].gain_db == Approx(+10.0f));
}

TEST_CASE("M8 morph_fields: mute follows the dominant slot") {
    BandField A, B;
    A.bands[0].muted = true;  A.bands[1].muted = false;
    B.bands[0].muted = false; B.bands[1].muted = true;
    BandField out;

    morph_fields(out, A, B, 0.25f);
    CHECK(out.bands[0].muted == true);   // A dominates
    CHECK(out.bands[1].muted == false);  // A dominates

    morph_fields(out, A, B, 0.75f);
    CHECK(out.bands[0].muted == false);  // B dominates
    CHECK(out.bands[1].muted == true);   // B dominates

    morph_fields(out, A, B, 0.5f);
    // 0.5 is the boundary — implementation picks B (>=). Document the
    // choice in the test so changes are intentional.
    CHECK(out.bands[0].muted == false);
    CHECK(out.bands[1].muted == true);
}

TEST_CASE("M8 SnapshotBank: capture marks slot populated") {
    SnapshotBank bank;
    CHECK_FALSE(bank.has(SnapshotBank::Slot::A));
    CHECK_FALSE(bank.has(SnapshotBank::Slot::B));

    const auto f = make_field(-6.0f);
    bank.capture_into(SnapshotBank::Slot::A, f, Viewport{}, Layout::Bands48);
    CHECK(bank.has(SnapshotBank::Slot::A));
    CHECK_FALSE(bank.has(SnapshotBank::Slot::B));
    CHECK(bank.a.layout == Layout::Bands48);
    CHECK(bank.a.field.bands[0].gain_db == Approx(-6.0f));
}

TEST_CASE("M8 SnapshotBank: copy + swap") {
    SnapshotBank bank;
    bank.capture_into(SnapshotBank::Slot::A, make_field(+3.0f), Viewport{}, Layout::Bands32);

    bank.copy(SnapshotBank::Slot::A, SnapshotBank::Slot::B);
    CHECK(bank.has(SnapshotBank::Slot::B));
    CHECK(bank.b.field.bands[0].gain_db == Approx(+3.0f));

    // Overwrite B, then swap.
    bank.capture_into(SnapshotBank::Slot::B, make_field(-9.0f), Viewport{}, Layout::Bands64);
    bank.swap();
    CHECK(bank.a.field.bands[0].gain_db == Approx(-9.0f));
    CHECK(bank.a.layout == Layout::Bands64);
    CHECK(bank.b.field.bands[0].gain_db == Approx(+3.0f));
    CHECK(bank.b.layout == Layout::Bands32);
}

TEST_CASE("M8 Spectr::apply_morph_to_live: no-op when both slots empty") {
    Spectr s;
    s.field().bands[0].gain_db = -4.0f;
    s.apply_morph_to_live(0.5f);
    CHECK(s.field().bands[0].gain_db == Approx(-4.0f));
}

TEST_CASE("M8 Spectr::apply_morph_to_live: single-populated slot wins") {
    Spectr s;
    s.field() = make_field(-4.0f);
    s.capture_snapshot(SnapshotBank::Slot::A);        // A = -4 dB
    s.field() = make_field(+2.0f);                    // live now mid-edit
    CHECK(s.field().bands[0].gain_db == Approx(+2.0f));

    s.apply_morph_to_live(1.0f);                      // B empty → A wins
    CHECK(s.field().bands[0].gain_db == Approx(-4.0f));
}

TEST_CASE("M8 Spectr::apply_morph_to_live: mid-morph blends A and B") {
    Spectr s;
    s.field() = make_field(-10.0f);
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.field() = make_field(+10.0f);
    s.capture_snapshot(SnapshotBank::Slot::B);
    s.field().reset();

    s.apply_morph_to_live(0.5f);
    CHECK(s.field().bands[0].gain_db == Approx(0.0f));
    CHECK(s.field().bands[63].gain_db == Approx(0.0f));
}

TEST_CASE("M8 plugin-state v2 round-trip preserves snapshot bank") {
    Spectr a;
    pulp::state::StateStore store;
    a.define_parameters(store);
    a.set_state_store(&store);

    // Populate A with -12 dB ramp, B with +3 dB flat.
    a.field() = ramp_field(-12.0f, 0.25f);
    a.capture_snapshot(SnapshotBank::Slot::A);
    a.field() = make_field(+3.0f);
    a.capture_snapshot(SnapshotBank::Slot::B);
    a.snapshots().active = SnapshotBank::Slot::B;

    const auto bytes = a.serialize_plugin_state();
    REQUIRE_FALSE(bytes.empty());

    Spectr b;
    pulp::state::StateStore store_b;
    b.define_parameters(store_b);
    b.set_state_store(&store_b);
    REQUIRE(b.deserialize_plugin_state(bytes));

    const auto& A = b.snapshots().a;
    const auto& B = b.snapshots().b;
    CHECK(A.populated);
    CHECK(B.populated);
    CHECK(A.field.bands[0].gain_db == Approx(-12.0f));
    CHECK(A.field.bands[63].gain_db == Approx(-12.0f + 63 * 0.25f));
    CHECK(B.field.bands[0].gain_db == Approx(+3.0f));
    CHECK(B.field.bands[63].gain_db == Approx(+3.0f));
    CHECK(b.snapshots().active == SnapshotBank::Slot::B);
}

TEST_CASE("M8 plugin-state v1 blob loads with an empty snapshot bank") {
    // Forge a minimal v1 blob (no snapshots member).
    const std::string v1 =
        R"({"version":1,"band_gain":[)" +
        std::string([] {
            std::string s;
            for (std::size_t i = 0; i < kMaxBands; ++i) {
                if (i) s += ",";
                s += "0.0";
            }
            return s;
        }()) + R"(],"band_mute":[)" +
        std::string([] {
            std::string s;
            for (std::size_t i = 0; i < kMaxBands; ++i) {
                if (i) s += ",";
                s += "false";
            }
            return s;
        }()) + R"(],"view_min_hz":20.0,"view_max_hz":20000.0,"layout_index":0,"analyzer_mode":0,"edit_mode":0})";

    Spectr s;
    pulp::state::StateStore store;
    s.define_parameters(store);
    s.set_state_store(&store);

    const std::vector<uint8_t> bytes(v1.begin(), v1.end());
    REQUIRE(s.deserialize_plugin_state(bytes));
    CHECK_FALSE(s.snapshots().has(SnapshotBank::Slot::A));
    CHECK_FALSE(s.snapshots().has(SnapshotBank::Slot::B));
}
