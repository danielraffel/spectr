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
#include "spectr/modulation.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>

#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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

// Catch2 renders a std::uint8_t as a character, so a mask mismatch prints as
// unreadable punctuation. Compare masks as ints and a failure names the bits.
static constexpr int mask_int(std::uint8_t mask) noexcept {
    return static_cast<int>(mask);
}

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

TEST_CASE("internal modulation is a non-destructive overlay") {
    spectr::BandField canonical;
    canonical.bands[0].gain_db = 3.0f;
    spectr::SnapshotBank bank;
    spectr::ModulationSettings settings;
    settings.enabled = true;
    settings.depth = 0.5f;
    settings.target = spectr::ModulationTarget::WholeBank;

    const auto audible = spectr::apply_internal_modulation(
        canonical, bank, 0.0f, settings, 1.0f);
    CHECK(audible.bands[0].gain_db == Catch::Approx(9.0f));
    CHECK(canonical.bands[0].gain_db == Catch::Approx(3.0f));
}

TEST_CASE("internal modulation targets snapshots and host morph independently") {
    spectr::BandField canonical;
    canonical.bands[0].gain_db = 0.0f;
    spectr::SnapshotBank bank;
    spectr::BandField a = canonical;
    spectr::BandField b = canonical;
    a.bands[0].gain_db = -12.0f;
    b.bands[0].gain_db = 12.0f;
    bank.capture_into(spectr::SnapshotBank::Slot::A, a, {}, spectr::Layout::Bands32);
    bank.capture_into(spectr::SnapshotBank::Slot::B, b, {}, spectr::Layout::Bands32);

    spectr::ModulationSettings settings;
    settings.enabled = true;
    settings.depth = 1.0f;
    settings.target = spectr::ModulationTarget::SnapshotA;
    auto audible = spectr::apply_internal_modulation(
        canonical, bank, 0.5f, settings, 1.0f);
    CHECK(audible.bands[0].gain_db == Catch::Approx(-12.0f));

    settings.target = spectr::ModulationTarget::SnapshotB;
    audible = spectr::apply_internal_modulation(
        canonical, bank, 0.5f, settings, 1.0f);
    CHECK(audible.bands[0].gain_db == Catch::Approx(12.0f));

    settings.target = spectr::ModulationTarget::Morph;
    settings.depth = 0.5f;
    audible = spectr::apply_internal_modulation(
        canonical, bank, 0.5f, settings, 1.0f);
    CHECK(audible.bands[0].gain_db == Catch::Approx(6.0f));
    CHECK(canonical.bands[0].gain_db == Catch::Approx(0.0f));
}

TEST_CASE("internal modulation target mask composes selected destinations") {
    spectr::BandField canonical;
    canonical.bands[0].gain_db = 0.0f;
    spectr::SnapshotBank bank;
    spectr::BandField a = canonical;
    a.bands[0].gain_db = 6.0f;
    bank.capture_into(spectr::SnapshotBank::Slot::A, a, {}, spectr::Layout::Bands32);
    spectr::ModulationSettings settings;
    settings.enabled = true;
    settings.depth = 1.0f;
    settings.target_mask = (std::uint8_t{1} << 0) | (std::uint8_t{1} << 1);
    const auto audible = spectr::apply_internal_modulation(
        canonical, bank, 0.0f, settings, 0.0f);
    CHECK(audible.bands[0].gain_db == Catch::Approx(3.0f));
    CHECK(canonical.bands[0].gain_db == Catch::Approx(0.0f));
}

TEST_CASE("an unset target mask follows the automatable target enum") {
    spectr::ModulationSettings settings;
    // A fresh settings object carries no explicit destination selection, so
    // the automatable enum lane decides.
    CHECK(mask_int(settings.target_mask)
          == mask_int(spectr::kModulationTargetMaskUnset));
    settings.target = spectr::ModulationTarget::SnapshotB;
    CHECK(mask_int(spectr::resolve_modulation_target_mask(settings))
          == mask_int(spectr::modulation_target_bit(
                 spectr::ModulationTarget::SnapshotB)));

    // An explicitly empty selection is a different statement — the user asked
    // for no destinations — and must not be confused with "unset".
    settings.target_mask = 0;
    CHECK(mask_int(spectr::resolve_modulation_target_mask(settings)) == 0);

    spectr::BandField canonical;
    canonical.bands[0].gain_db = 0.0f;
    spectr::SnapshotBank bank;
    settings.enabled = true;
    settings.depth = 1.0f;
    settings.target = spectr::ModulationTarget::WholeBank;
    settings.target_mask = 0;
    const auto silent = spectr::apply_internal_modulation(
        canonical, bank, 0.0f, settings, 1.0f);
    CHECK(silent.bands[0].gain_db == Catch::Approx(0.0f));

    // Positive control: the same LFO with no explicit selection is audible,
    // so the check above is measuring the selection and not a dead LFO.
    settings.target_mask = spectr::kModulationTargetMaskUnset;
    const auto audible = spectr::apply_internal_modulation(
        canonical, bank, 0.0f, settings, 1.0f);
    CHECK(audible.bands[0].gain_db == Catch::Approx(12.0f));
}

TEST_CASE("plugin state round-trips the modulation target mask") {
    // Bank + Morph: a selection no single enum value can express, so a reader
    // that quietly fell back to the enum could not fake it.
    constexpr std::uint8_t kMask =
        static_cast<std::uint8_t>((std::uint8_t{1} << 0) | (std::uint8_t{1} << 3));

    Spectr a;
    pulp::state::StateStore store_a;
    a.set_state_store(&store_a);
    a.define_parameters(store_a);
    REQUIRE(a.set_modulation_target_mask(kMask));
    REQUIRE(mask_int(a.modulation_settings().target_mask) == mask_int(kMask));
    const auto blob = a.serialize_plugin_state();
    REQUIRE_FALSE(blob.empty());

    Spectr b;
    pulp::state::StateStore store_b;
    b.set_state_store(&store_b);
    b.define_parameters(store_b);
    REQUIRE(mask_int(b.modulation_settings().target_mask)
            == mask_int(spectr::kModulationTargetMaskUnset));
    REQUIRE(b.deserialize_plugin_state(blob));
    CHECK(mask_int(b.modulation_settings().target_mask) == mask_int(kMask));
}

TEST_CASE("a plugin state blob without a target mask loads as unset") {
    // A writer that predates the Targets control emits no mask member. Such a
    // blob must restore the pre-control behaviour — follow the enum — and must
    // NOT read as an explicit empty selection, which would silence modulation.
    Spectr a;
    pulp::state::StateStore store_a;
    a.set_state_store(&store_a);
    a.define_parameters(store_a);
    const auto blob = a.serialize_plugin_state();
    std::string json(blob.begin(), blob.end());

    const auto key = std::string("\"modulation_target_mask\":");
    auto at = json.find(key);
    REQUIRE(at != std::string::npos);  // control: the field is there to remove
    auto end = json.find_first_of(",}", at);
    REQUIRE(end != std::string::npos);
    if (json[end] == ',') {
        ++end;  // this member, plus the separator that follows it
    } else {
        // Last member: walk back over the separator that precedes it instead,
        // otherwise the erase leaves a trailing comma and invalid JSON.
        while (at > 0 && std::isspace(static_cast<unsigned char>(json[at - 1])))
            --at;
        REQUIRE(at > 0);
        REQUIRE(json[at - 1] == ',');
        --at;
    }
    json.erase(at, end - at);
    REQUIRE(json.find(key) == std::string::npos);
    // Control: the surgery must leave a payload the reader still accepts, so a
    // rejection below is about the missing member, not about broken JSON.
    REQUIRE(choc::json::parse(json).isObject());

    Spectr b;
    pulp::state::StateStore store_b;
    b.set_state_store(&store_b);
    b.define_parameters(store_b);
    // Start from an explicit empty selection so a reader that simply left the
    // member alone, or zeroed it, would be caught.
    REQUIRE(b.set_modulation_target_mask(0));
    const std::vector<uint8_t> legacy(json.begin(), json.end());
    REQUIRE(b.deserialize_plugin_state(legacy));
    CHECK(mask_int(b.modulation_settings().target_mask)
          == mask_int(spectr::kModulationTargetMaskUnset));
}

TEST_CASE("tempo LFO waveform is deterministic and bounded") {
    for (const auto shape : {spectr::LfoShape::Sine, spectr::LfoShape::Triangle,
                             spectr::LfoShape::Square, spectr::LfoShape::Saw}) {
        CHECK(spectr::lfo_value(shape, 0.125) ==
              Catch::Approx(spectr::lfo_value(shape, 1.125)));
        CHECK(std::abs(spectr::lfo_value(shape, 0.37)) <= 1.0f);
    }
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

// ── Morph moves the viewport ────────────────────────────────────────────
//
// A snapshot has always captured its viewport; until now morph ignored it,
// so the bands blended while the window they are drawn in — and masked with
// — snapped. These cover the interpolation itself, the playback switch that
// governs whether morph applies it, and the two paths that derive it.
//
// The midpoint is the load-bearing assertion in every one of them: a test
// that only pins t=0 and t=1 passes unchanged against the broken behaviour,
// because snapping already produced the right answer at both ends.

TEST_CASE("morph_viewports: ends are exact") {
    const Viewport a{20.0f, 2000.0f};
    const Viewport b{200.0f, 20000.0f};
    CHECK(spectr::morph_viewports(a, b, 0.0f).min_hz == Approx(20.0f));
    CHECK(spectr::morph_viewports(a, b, 0.0f).max_hz == Approx(2000.0f));
    CHECK(spectr::morph_viewports(a, b, 1.0f).min_hz == Approx(200.0f));
    CHECK(spectr::morph_viewports(a, b, 1.0f).max_hz == Approx(20000.0f));
}

TEST_CASE("morph_viewports: the midpoint is the geometric mean and not the arithmetic one") {
    // The whole point of the feature. 20 -> 2000 Hz is two decades, so half
    // way along the LOG axis is 200 Hz. A linear lerp would answer 1010 Hz,
    // which is 0.7 decades from the top and 1.3 from the bottom: the sweep
    // would cross most of the visible range in its first fifth and then crawl.
    const Viewport a{20.0f, 200.0f};
    const Viewport b{2000.0f, 20000.0f};
    const auto mid = spectr::morph_viewports(a, b, 0.5f);

    CHECK(mid.min_hz == Approx(200.0f).epsilon(0.0005));
    CHECK(mid.max_hz == Approx(2000.0f).epsilon(0.0005));

    // Stated as the property rather than the number, so this fails for any
    // interpolation that is not log-linear, not just for the linear one.
    CHECK(mid.min_hz == Approx(std::sqrt(a.min_hz * b.min_hz)).epsilon(0.0005));
    CHECK(mid.max_hz == Approx(std::sqrt(a.max_hz * b.max_hz)).epsilon(0.0005));

    // And the discriminating negative: linear would land here.
    const float linear_min = a.min_hz + (b.min_hz - a.min_hz) * 0.5f;
    CHECK(linear_min == Approx(1010.0f));
    CHECK(mid.min_hz != Approx(linear_min).epsilon(0.01));
}

TEST_CASE("morph_viewports: equal ratio steps across a sweep") {
    // Log-linear means every equal step in t multiplies the bound by the same
    // factor. Sampling the ratio across the sweep catches an interpolation
    // that is right at the ends and midpoint but wrong in between.
    const Viewport a{20.0f, 200.0f};
    const Viewport b{2000.0f, 20000.0f};
    REQUIRE(a.valid());
    REQUIRE(b.valid());
    float previous = a.min_hz;
    float first_ratio = 0.0f;
    for (int step = 1; step <= 10; ++step) {
        const float t = static_cast<float>(step) / 10.0f;
        const float current = spectr::morph_viewports(a, b, t).min_hz;
        const float ratio = current / previous;
        if (step == 1) first_ratio = ratio;
        else CHECK(ratio == Approx(first_ratio).epsilon(0.002));
        previous = current;
    }
    CHECK(first_ratio > 1.0f);
}

TEST_CASE("morph_viewports: clamps t and always returns a usable window") {
    const Viewport a{20.0f, 2000.0f};
    const Viewport b{200.0f, 20000.0f};
    CHECK(spectr::morph_viewports(a, b, -5.0f).min_hz == Approx(20.0f));
    CHECK(spectr::morph_viewports(a, b, +5.0f).min_hz == Approx(200.0f));
    for (int step = 0; step <= 20; ++step) {
        const auto v = spectr::morph_viewports(
            a, b, static_cast<float>(step) / 20.0f);
        CHECK(v.valid());
    }
}

TEST_CASE("morph_viewports: an invalid endpoint falls back to the dominant slot") {
    const Viewport good{20.0f, 2000.0f};
    Viewport bad;
    bad.min_hz = 0.0f;      // fails Viewport::valid()
    bad.max_hz = 100.0f;
    REQUIRE_FALSE(bad.valid());

    CHECK(spectr::morph_viewports(good, bad, 0.25f).min_hz == Approx(20.0f));
    CHECK(spectr::morph_viewports(bad, good, 0.75f).min_hz == Approx(20.0f));
    CHECK(spectr::morph_viewports(bad, bad, 0.5f).valid());
}

TEST_CASE("morph moves the viewport at the midpoint and not only at the ends") {
    Spectr s;
    pulp::state::StateStore store;
    s.define_parameters(store);
    s.set_state_store(&store);
    REQUIRE(s.morph_applies_viewport());

    s.viewport() = Viewport{20.0f, 200.0f};
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.viewport() = Viewport{2000.0f, 20000.0f};
    s.capture_snapshot(SnapshotBank::Slot::B);

    s.apply_morph_to_live(0.0f);
    CHECK(s.viewport().min_hz == Approx(20.0f).epsilon(0.001));
    s.apply_morph_to_live(1.0f);
    CHECK(s.viewport().min_hz == Approx(2000.0f).epsilon(0.001));

    // The assertion the broken behaviour cannot satisfy: snapping answers
    // 20 below 0.5 and 2000 at/above it, never 200.
    s.apply_morph_to_live(0.5f);
    CHECK(s.viewport().min_hz == Approx(200.0f).epsilon(0.001));
    CHECK(s.viewport().max_hz == Approx(2000.0f).epsilon(0.001));
}

TEST_CASE("the viewport switch is a playback switch: capture always stores the window") {
    Spectr s;
    pulp::state::StateStore store;
    s.define_parameters(store);
    s.set_state_store(&store);

    // Captured with the switch OFF — the window must still be recorded, or
    // turning the switch on later could not work.
    s.set_morph_applies_viewport(false);
    s.viewport() = Viewport{20.0f, 200.0f};
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.viewport() = Viewport{2000.0f, 20000.0f};
    s.capture_snapshot(SnapshotBank::Slot::B);

    CHECK(s.snapshots().a.viewport.min_hz == Approx(20.0f));
    CHECK(s.snapshots().b.viewport.min_hz == Approx(2000.0f));

    s.viewport() = Viewport{100.0f, 1000.0f};
    s.apply_morph_to_live(0.5f);
    CHECK(s.viewport().min_hz == Approx(100.0f));   // untouched
    CHECK(s.viewport().max_hz == Approx(1000.0f));

    // Re-enabling just works, from the information capture never discarded.
    s.set_morph_applies_viewport(true);
    s.apply_morph_to_live(0.5f);
    CHECK(s.viewport().min_hz == Approx(200.0f).epsilon(0.001));
}

TEST_CASE("the switch off leaves the viewport untouched across a whole sweep") {
    Spectr s;
    pulp::state::StateStore store;
    s.define_parameters(store);
    s.set_state_store(&store);

    s.viewport() = Viewport{20.0f, 200.0f};
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.viewport() = Viewport{2000.0f, 20000.0f};
    s.capture_snapshot(SnapshotBank::Slot::B);

    s.set_morph_applies_viewport(false);
    const Viewport parked{440.0f, 4400.0f};
    s.viewport() = parked;

    for (int step = 0; step <= 20; ++step) {
        s.apply_morph_to_live(static_cast<float>(step) / 20.0f);
        CHECK(s.viewport().min_hz == Approx(parked.min_hz));
        CHECK(s.viewport().max_hz == Approx(parked.max_hz));
    }
    // The bands still morphed — "off" must disable the viewport, not morph.
    CHECK(s.field().bands[0].gain_db == Approx(0.0f));
}

TEST_CASE("flipping the switch mid-sweep never strands the viewport") {
    Spectr s;
    pulp::state::StateStore store;
    s.define_parameters(store);
    s.set_state_store(&store);

    s.viewport() = Viewport{20.0f, 200.0f};
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.viewport() = Viewport{2000.0f, 20000.0f};
    s.capture_snapshot(SnapshotBank::Slot::B);

    s.apply_morph_to_live(0.25f);
    const float at_quarter = s.viewport().min_hz;
    CHECK(at_quarter > 20.0f);
    CHECK(at_quarter < 200.0f);

    // Disabling parks the user on the window they are looking at, rather than
    // yanking them back to either endpoint.
    s.set_morph_applies_viewport(false);
    CHECK(s.viewport().min_hz == Approx(at_quarter));
    s.apply_morph_to_live(0.75f);
    CHECK(s.viewport().min_hz == Approx(at_quarter));

    // Re-enabling resumes from the live morph value, not from where it left.
    s.set_morph_applies_viewport(true);
    s.apply_morph_to_live(0.75f);
    CHECK(s.viewport().min_hz
          == Approx(spectr::morph_viewports(s.snapshots().a.viewport,
                                            s.snapshots().b.viewport,
                                            0.75f).min_hz).epsilon(0.001));
    CHECK(s.viewport().valid());
}

TEST_CASE("morph never changes the layout") {
    // Band count is discrete and the five selectable counts do not share a
    // band grid, so there is nothing to interpolate. Morph leaves the active
    // layout alone even when the two slots were captured under different ones.
    Spectr s;
    pulp::state::StateStore store;
    s.define_parameters(store);
    s.set_state_store(&store);

    s.set_layout(Layout::Bands32);
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.set_layout(Layout::Bands64);
    s.capture_snapshot(SnapshotBank::Slot::B);
    s.set_layout(Layout::Bands48);

    for (int step = 0; step <= 10; ++step) {
        s.apply_morph_to_live(static_cast<float>(step) / 10.0f);
        CHECK(s.layout() == Layout::Bands48);
    }
    CHECK(s.snapshots().a.layout == Layout::Bands32);
    CHECK(s.snapshots().b.layout == Layout::Bands64);
}

TEST_CASE("a morph-derived viewport survives a plugin-state round trip") {
    Spectr a;
    pulp::state::StateStore store_a;
    a.define_parameters(store_a);
    a.set_state_store(&store_a);

    a.viewport() = Viewport{20.0f, 200.0f};
    a.capture_snapshot(SnapshotBank::Slot::A);
    a.viewport() = Viewport{2000.0f, 20000.0f};
    a.capture_snapshot(SnapshotBank::Slot::B);
    a.apply_morph_to_live(0.5f);
    const float derived = a.viewport().min_hz;
    REQUIRE(derived == Approx(200.0f).epsilon(0.001));

    const auto bytes = a.serialize_plugin_state();
    REQUIRE_FALSE(bytes.empty());

    Spectr b;
    pulp::state::StateStore store_b;
    b.define_parameters(store_b);
    b.set_state_store(&store_b);
    // The morph parameter rides the base state blob, so mirror it the way a
    // host would before handing over the supplemental bytes.
    store_b.set_value(spectr::kParamMorph, store_a.get_value(spectr::kParamMorph));
    REQUIRE(b.deserialize_plugin_state(bytes));

    CHECK(b.morph_applies_viewport());
    CHECK(b.viewport().min_hz == Approx(derived).epsilon(0.001));
}

TEST_CASE("plugin state round-trips the viewport switch and absence reads as enabled") {
    Spectr a;
    pulp::state::StateStore store_a;
    a.define_parameters(store_a);
    a.set_state_store(&store_a);
    a.set_morph_applies_viewport(false);

    const auto bytes = a.serialize_plugin_state();
    Spectr b;
    pulp::state::StateStore store_b;
    b.define_parameters(store_b);
    b.set_state_store(&store_b);
    REQUIRE(b.deserialize_plugin_state(bytes));
    CHECK_FALSE(b.morph_applies_viewport());

    // A writer that predates the switch omits the member. Reading that as
    // DISABLED would open an old session with the feature mysteriously off.
    const auto text = std::string(bytes.begin(), bytes.end());
    auto root = choc::json::parse(text);
    REQUIRE(root.hasObjectMember("morph_applies_viewport"));
    auto stripped = choc::value::createObject("SpectrPluginState");
    for (uint32_t i = 0; i < root.size(); ++i) {
        const auto member = root.getObjectMemberAt(i);
        if (std::string(member.name) == "morph_applies_viewport") continue;
        stripped.addMember(member.name, member.value);
    }
    const auto legacy = choc::json::toString(stripped, false);
    const std::vector<uint8_t> legacy_bytes(legacy.begin(), legacy.end());

    Spectr c;
    pulp::state::StateStore store_c;
    c.define_parameters(store_c);
    c.set_state_store(&store_c);
    c.set_morph_applies_viewport(false);
    REQUIRE(c.deserialize_plugin_state(legacy_bytes));
    CHECK(c.morph_applies_viewport());
}

// ── Clearing a slot ────────────────────────────────────────────────────
//
// Until `clear_snapshot` existed a filled slot could only be OVERWRITTEN,
// never emptied, by any route. Measured on the revision that added it:
// `clear_snapshot` read 0 C++ files and 0 occurrences in the shipping editor
// document, against `capture_snapshot` at 6 files and 1 occurrence on the
// same instruments. RESET ALL could not clear a slot either, because there
// was no handler for it to call.

TEST_CASE("snapshot clear: empties the whole slot, not just its flag") {
    SnapshotBank bank;
    bank.capture_into(SnapshotBank::Slot::A, ramp_field(-12.0f, 0.5f),
                      Viewport{}, Layout::Bands64);
    REQUIRE(bank.has(SnapshotBank::Slot::A));

    bank.clear(SnapshotBank::Slot::A);
    CHECK_FALSE(bank.has(SnapshotBank::Slot::A));
    // A cleared slot and a slot never captured must be the same thing to
    // every reader. Leaving the field behind the cleared flag would mean a
    // later `populated = true` resurrected data the user believed gone.
    const SnapshotBank fresh;
    CHECK(bank.a.field.bands[0].gain_db
          == Approx(fresh.a.field.bands[0].gain_db));
    CHECK(mask_int(static_cast<std::uint8_t>(bank.a.layout))
          == mask_int(static_cast<std::uint8_t>(fresh.a.layout)));
}

TEST_CASE("snapshot clear: touches only the named slot") {
    SnapshotBank bank;
    bank.capture_into(SnapshotBank::Slot::A, make_field(-6.0f), Viewport{},
                      Layout::Bands32);
    bank.capture_into(SnapshotBank::Slot::B, make_field(+6.0f), Viewport{},
                      Layout::Bands32);

    bank.clear(SnapshotBank::Slot::A);
    CHECK_FALSE(bank.has(SnapshotBank::Slot::A));
    REQUIRE(bank.has(SnapshotBank::Slot::B));
    CHECK(bank.b.field.bands[0].gain_db == Approx(+6.0f));
}

TEST_CASE("snapshot clear: an empty slot is a no-op, so a caller need not ask first") {
    SnapshotBank bank;
    CHECK_FALSE(bank.has(SnapshotBank::Slot::B));
    bank.clear(SnapshotBank::Slot::B);
    CHECK_FALSE(bank.has(SnapshotBank::Slot::B));
}

TEST_CASE("snapshot clear: Spectr::clear_snapshot empties a captured slot") {
    Spectr s;
    s.field() = make_field(-8.0f);
    s.capture_snapshot(SnapshotBank::Slot::A);
    REQUIRE(s.snapshots().has(SnapshotBank::Slot::A));

    s.clear_snapshot(SnapshotBank::Slot::A);
    CHECK_FALSE(s.snapshots().has(SnapshotBank::Slot::A));
}

TEST_CASE("snapshot clear: a cleared slot stops being a morph endpoint") {
    Spectr s;
    s.field() = make_field(-10.0f);
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.field() = make_field(+10.0f);
    s.capture_snapshot(SnapshotBank::Slot::B);

    // Control: with both slots filled, t=0 resolves to A.
    s.field().reset();
    s.apply_morph_to_live(0.0f);
    REQUIRE(s.field().bands[0].gain_db == Approx(-10.0f));

    // With A cleared, the populated side wins at every t -- which is the
    // same rule an empty slot has always had, now reachable by clearing.
    s.clear_snapshot(SnapshotBank::Slot::A);
    s.field().reset();
    s.apply_morph_to_live(0.0f);
    CHECK(s.field().bands[0].gain_db == Approx(+10.0f));
}

TEST_CASE("snapshot clear: both slots cleared leaves morph nothing to apply") {
    Spectr s;
    s.field() = make_field(-10.0f);
    s.capture_snapshot(SnapshotBank::Slot::A);
    s.field() = make_field(+10.0f);
    s.capture_snapshot(SnapshotBank::Slot::B);

    s.clear_snapshot(SnapshotBank::Slot::A);
    s.clear_snapshot(SnapshotBank::Slot::B);

    s.field() = make_field(-4.0f);
    s.apply_morph_to_live(0.5f);
    // Same contract as a processor that never captured anything: morph leaves
    // the live field alone rather than guessing.
    CHECK(s.field().bands[0].gain_db == Approx(-4.0f));
}

TEST_CASE("snapshot clear: a cleared slot round-trips plugin state as empty") {
    Spectr writer;
    writer.field() = make_field(-5.0f);
    writer.capture_snapshot(SnapshotBank::Slot::A);
    writer.capture_snapshot(SnapshotBank::Slot::B);
    writer.clear_snapshot(SnapshotBank::Slot::A);

    const auto blob = writer.serialize_plugin_state();

    Spectr reader;
    REQUIRE(reader.deserialize_plugin_state(blob));
    CHECK_FALSE(reader.snapshots().has(SnapshotBank::Slot::A));
    CHECK(reader.snapshots().has(SnapshotBank::Slot::B));
    CHECK(reader.snapshots().b.field.bands[0].gain_db == Approx(-5.0f));
}

// An LFO modulates LEVELS. It must never move a mute the user authored.
//
// Reported as "if muted these jiggle/kinda glitch when LFO modulating morph":
// bands carrying mute badges, painted at different heights, moving with the
// LFO. The badge comes from the authored field and the height from the
// modulated one, so a modulated frame that drops a mute paints a badge over a
// moving bar — and because `Spectr::process` publishes that same BandField to
// the DSP (`slot.field = audible`) and `linear_gain()` gates on `Band::muted`,
// the band is also HEARD. The paint was the visible half of an audible bug.
//
// Every destination reaches its field through `morph_fields`, which picks mute
// wholesale from whichever endpoint dominates at t. Measured on the revision
// before this one: Morph lost the mute at every depth (its two endpoints are
// the snapshots, so the authored field is not even an input), and the snapshot
// destinations lost it from depth 0.5, where the unipolar amount first reaches
// the dominance flip.
namespace {

struct MuteProbe {
    spectr::BandField canonical;
    spectr::SnapshotBank bank;
};

/// Snapshots captured with band 5 UN-muted; the user mutes it afterwards.
/// Band 9 is the reverse: un-muted live, muted in snapshot A.
MuteProbe make_mute_probe() {
    MuteProbe p;
    spectr::BandField a, b;
    a.reset();
    b.reset();
    for (std::size_t i = 0; i < spectr::kMaxBands; ++i) {
        a.bands[i].gain_db = -6.0f;
        b.bands[i].gain_db = +6.0f;
    }
    a.bands[9].muted = true;
    p.bank.capture_into(spectr::SnapshotBank::Slot::A, a, {}, spectr::Layout::Bands32);
    p.bank.capture_into(spectr::SnapshotBank::Slot::B, b, {}, spectr::Layout::Bands32);
    p.canonical.reset();
    p.canonical.bands[5].gain_db = -3.0f;
    p.canonical.bands[5].muted = true;
    return p;
}

} // namespace

TEST_CASE("authored mute: no destination un-mutes a band the user muted") {
    const auto p = make_mute_probe();
    const spectr::ModulationTarget targets[] = {
        spectr::ModulationTarget::WholeBank, spectr::ModulationTarget::SnapshotA,
        spectr::ModulationTarget::SnapshotB, spectr::ModulationTarget::Morph};

    for (auto target : targets) {
        for (float depth : {0.25f, 0.5f, 1.0f}) {
            spectr::ModulationSettings s;
            s.enabled = true;
            s.depth = depth;
            s.target = target;
            for (int step = 0; step < 64; ++step) {
                const float wave =
                    spectr::lfo_value(spectr::LfoShape::Sine, step / 64.0);
                const auto out = spectr::apply_internal_modulation(
                    p.canonical, p.bank, 0.5f, s, wave);
                // The paint half: the editor draws this band at the mute
                // sentinel only while the frame still reports it muted.
                REQUIRE(out.bands[5].muted);
                // The audible half, and the serious one.
                REQUIRE(out.linear_gain(5) == 0.0f);
                // A muted band is excluded from modulation entirely, so
                // unmuting mid-sweep reveals the authored level rather than
                // whatever phase the LFO happened to be at.
                REQUIRE(out.bands[5].gain_db == Approx(-3.0f));
            }
        }
    }
}

TEST_CASE("authored mute: an LFO never strobes a mute at the dominance flip") {
    const auto p = make_mute_probe();
    // Band 9 is muted in snapshot A and un-muted live. `morph_fields` flips
    // its pick at t = 0.5; a user dragging the morph slider controls that
    // crossing, but an LFO crosses it twice per cycle, so the band would pop
    // in and out of silence at LFO rate.
    const spectr::ModulationTarget targets[] = {
        spectr::ModulationTarget::SnapshotA, spectr::ModulationTarget::Morph};

    for (auto target : targets) {
        spectr::ModulationSettings s;
        s.enabled = true;
        s.depth = 1.0f;
        s.target = target;
        for (int step = 0; step < 256; ++step) {
            const float wave =
                spectr::lfo_value(spectr::LfoShape::Sine, step / 256.0);
            const auto out = spectr::apply_internal_modulation(
                p.canonical, p.bank, 0.5f, s, wave);
            REQUIRE_FALSE(out.bands[9].muted);
        }
    }
}

TEST_CASE("authored mute: un-muted bands are still modulated") {
    // POSITIVE CONTROL for the two cases above. Holding every mute still is
    // trivially satisfied by a modulator that does nothing, and that failure
    // would leave both of those tests green.
    const auto p = make_mute_probe();
    struct Expect { spectr::ModulationTarget target; float swing_db; };
    const Expect expected[] = {
        {spectr::ModulationTarget::WholeBank, 24.0f},
        {spectr::ModulationTarget::SnapshotA, 6.0f},
        {spectr::ModulationTarget::SnapshotB, 6.0f},
        {spectr::ModulationTarget::Morph, 12.0f},
    };

    for (const auto& e : expected) {
        spectr::ModulationSettings s;
        s.enabled = true;
        s.depth = 1.0f;
        s.target = e.target;
        float lo = 1e9f, hi = -1e9f;
        for (int step = 0; step < 256; ++step) {
            const float wave =
                spectr::lfo_value(spectr::LfoShape::Sine, step / 256.0);
            const auto out = spectr::apply_internal_modulation(
                p.canonical, p.bank, 0.5f, s, wave);
            lo = std::min(lo, out.bands[3].gain_db);
            hi = std::max(hi, out.bands[3].gain_db);
        }
        CHECK((hi - lo) == Approx(e.swing_db));
    }
}

TEST_CASE("authored mute: a user-dragged morph still moves mute") {
    // The guard is scoped to internal modulation. `morph_fields` is also the
    // engine behind the MORPH parameter, which the user drags and watches, and
    // its dominance rule there is deliberate — mute is captured state, and a
    // morph that could not reach it would not be a morph. Narrowing the fix to
    // the modulator is the whole point, so it is asserted rather than assumed.
    spectr::BandField a, b, out;
    a.reset();
    b.reset();
    a.bands[0].muted = true;
    spectr::morph_fields(out, a, b, 0.0f);
    CHECK(out.bands[0].muted);
    spectr::morph_fields(out, a, b, 0.49f);
    CHECK(out.bands[0].muted);
    spectr::morph_fields(out, a, b, 0.5f);
    CHECK_FALSE(out.bands[0].muted);
}

TEST_CASE("plugin state round-trips macro membership") {
    // Scattered, overlapping, and reaching a slot above the default visible
    // count. Contiguity would round-trip through almost any encoding; this
    // shape only survives if the indices themselves are carried.
    Spectr a;
    pulp::state::StateStore store_a;
    a.set_state_store(&store_a);
    a.define_parameters(store_a);

    spectr::MacroMembership<spectr::kMaxBands> first;
    for (const std::size_t slot : {0u, 7u, 31u, 63u}) first.set(slot);
    spectr::MacroMembership<spectr::kMaxBands> second;
    for (const std::size_t slot : {7u, 8u}) second.set(slot);  // 7 is shared
    REQUIRE(a.set_macro_members(0, first));
    REQUIRE(a.set_macro_members(3, second));

    const auto blob = a.serialize_plugin_state();
    REQUIRE_FALSE(blob.empty());

    Spectr b;
    pulp::state::StateStore store_b;
    b.set_state_store(&store_b);
    b.define_parameters(store_b);
    REQUIRE(b.macro_members(0).none());  // control: it starts empty
    REQUIRE(b.deserialize_plugin_state(blob));

    CHECK(b.macro_members(0) == first);
    CHECK(b.macro_members(3) == second);
    // The macros that were never assigned must still be empty — a reader that
    // smeared one macro's membership across the bank would otherwise pass.
    CHECK(b.macro_members(1).none());
    CHECK(b.macro_members(2).none());
}

TEST_CASE("a plugin state blob without macro members loads with none assigned") {
    // A writer that predates macros emits no member. That absence means "no
    // macros assigned", which is exactly what that writer meant — so the blob
    // must load, not be refused, and must not leave a stale assignment behind.
    Spectr a;
    pulp::state::StateStore store_a;
    a.set_state_store(&store_a);
    a.define_parameters(store_a);
    const auto blob = a.serialize_plugin_state();
    std::string json(blob.begin(), blob.end());

    // Rebuild the object without the member rather than cutting the text:
    // `macro_members` is an array OF arrays, so a comma-scan would stop inside
    // the first nested array and corrupt the payload.
    const auto parsed = choc::json::parse(json);
    REQUIRE(parsed.isObject());
    REQUIRE(parsed.hasObjectMember("macro_members"));  // control: it is there
    auto stripped = choc::value::createObject("SpectrPluginState");
    for (uint32_t i = 0; i < parsed.size(); ++i) {
        const auto member = parsed.getObjectMemberAt(i);
        if (std::string_view(member.name) == "macro_members") continue;
        stripped.addMember(member.name, member.value);
    }
    REQUIRE_FALSE(stripped.hasObjectMember("macro_members"));
    const auto text = choc::json::toString(stripped, false);

    Spectr b;
    pulp::state::StateStore store_b;
    b.set_state_store(&store_b);
    b.define_parameters(store_b);
    // Start from an assignment, so a reader that simply left the field alone
    // would be caught rather than passing by luck on a fresh instance.
    spectr::MacroMembership<spectr::kMaxBands> prior;
    prior.set(12);
    REQUIRE(b.set_macro_members(2, prior));

    const std::vector<uint8_t> bytes(text.begin(), text.end());
    REQUIRE(b.deserialize_plugin_state(bytes));
    CHECK(b.macro_members(2).none());

    // Control on the same instrument: the UNstripped blob must still restore
    // a membership, so the "none" above is the absence being honoured and not
    // the reader having stopped working.
    REQUIRE(a.set_macro_members(2, prior));
    REQUIRE(b.deserialize_plugin_state(a.serialize_plugin_state()));
    CHECK(b.macro_members(2).test(12));
}
