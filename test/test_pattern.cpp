// Milestone 7 — Pattern library data model tests.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/pattern.hpp"

#include <string>

using Catch::Approx;
using spectr::all_factory_patterns;
using spectr::BandField;
using spectr::build_factory_pattern;
using spectr::factory_ids::kAirLift;
using spectr::factory_ids::kAlternate;
using spectr::factory_ids::kComb;
using spectr::factory_ids::kFlat;
using spectr::factory_ids::kHarmonic;
using spectr::factory_ids::kSubOnly;
using spectr::factory_ids::kTilt;
using spectr::factory_ids::kVocal;
using spectr::kMaxBands;
using spectr::kPatternSchemaVersion;
using spectr::Pattern;
using spectr::PatternLibrary;
using spectr::PatternSource;

TEST_CASE("M7: all 8 factory patterns build") {
    auto all = all_factory_patterns();
    REQUIRE(all.size() == 8);

    const std::vector<std::string> expected_ids = {
        kFlat, kHarmonic, kAlternate, kComb,
        kVocal, kSubOnly, kTilt, kAirLift,
    };
    for (std::size_t i = 0; i < expected_ids.size(); ++i) {
        CHECK(all[i].id == expected_ids[i]);
        CHECK(all[i].source == PatternSource::Factory);
        CHECK_FALSE(all[i].name.empty());
    }
}

TEST_CASE("M7: FLAT factory pattern is all-zero, all-unmuted") {
    auto flat = build_factory_pattern(kFlat);
    REQUIRE(flat.has_value());
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        CHECK(flat->gain_db[i] == Approx(0.0f));
        CHECK_FALSE(flat->muted[i]);
    }
}

TEST_CASE("M7: ALTERNATING factory pattern mutes every other band") {
    auto alt = build_factory_pattern(kAlternate);
    REQUIRE(alt.has_value());
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        if (i % 2 == 0) {
            CHECK(alt->gain_db[i] > 0.0f);   // 0.6 → +7.2 dB
            CHECK_FALSE(alt->muted[i]);
        } else {
            CHECK(alt->muted[i]);            // -Infinity → muted
        }
    }
}

TEST_CASE("M7: SUB ONLY factory pattern mutes bands above ~160 Hz") {
    auto sub = build_factory_pattern(kSubOnly);
    REQUIRE(sub.has_value());
    // Band 0..~7 cover low frequencies (under 160 Hz in a 64-band log-20-20k
    // layout). The higher bands must be muted.
    bool any_muted = false;
    bool any_live  = false;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        if (sub->muted[i]) any_muted = true;
        else               any_live  = true;
    }
    CHECK(any_muted);
    CHECK(any_live);
    // Highest bands (near 20 kHz) must be muted.
    CHECK(sub->muted[kMaxBands - 1]);
}

TEST_CASE("M7: build_factory_pattern returns nullopt for unknown id") {
    auto bogus = build_factory_pattern("factory:bogus");
    CHECK_FALSE(bogus.has_value());
}

TEST_CASE("M7: Pattern::apply_to copies gain/mute onto a BandField") {
    auto flat = build_factory_pattern(kFlat).value();
    BandField f;
    // Seed non-zero state so we can see apply_to overwrite it.
    for (auto& b : f.bands) { b.gain_db = -24.0f; b.muted = true; }
    flat.apply_to(f);
    for (const auto& b : f.bands) {
        CHECK(b.gain_db == Approx(0.0f));
        CHECK_FALSE(b.muted);
    }
}

TEST_CASE("M7 library: starts with 8 factory + 0 user + flat default") {
    PatternLibrary lib;
    CHECK(lib.factory().size() == 8);
    CHECK(lib.user().empty());
    CHECK(lib.default_id() == std::string{kFlat});
}

TEST_CASE("M7 library: save_current captures the live BandField") {
    PatternLibrary lib;
    BandField f;
    f.bands[5].gain_db = -12.0f;
    f.bands[10].muted  = true;

    const auto saved = lib.save_current(f);
    CHECK(saved.source == PatternSource::User);
    CHECK(saved.gain_db[5] == Approx(-12.0f));
    CHECK(saved.muted[10]  == true);
    CHECK_FALSE(saved.name.empty());
    CHECK_FALSE(saved.created_at.empty());
    CHECK(lib.user().size() == 1);
}

TEST_CASE("M7 library: auto-named patterns use PATTERN NN format") {
    PatternLibrary lib;
    BandField f;
    const auto a = lib.save_current(f);
    const auto b = lib.save_current(f);
    CHECK(a.name == "PATTERN 01");
    CHECK(b.name == "PATTERN 02");
}

TEST_CASE("M7 library: rename / duplicate / remove / update_from_current") {
    PatternLibrary lib;
    BandField f;
    f.bands[0].gain_db = +6.0f;
    const auto p = lib.save_current(f, "ORIGINAL");
    const std::string id = p.id;

    // Rename.
    CHECK(lib.rename(id, "RENAMED"));
    CHECK(lib.find(id)->name == "RENAMED");

    // Duplicate.
    const auto dup = lib.duplicate(id);
    REQUIRE(dup.has_value());
    CHECK(dup->name == "RENAMED COPY");
    CHECK(dup->id != id);
    CHECK(lib.user().size() == 2);

    // Update from current.
    f.bands[0].gain_db = -3.0f;
    CHECK(lib.update_from_current(id, f));
    CHECK(lib.find(id)->gain_db[0] == Approx(-3.0f));

    // Remove.
    CHECK(lib.remove(id));
    CHECK(lib.find(id) == nullptr);
    CHECK(lib.user().size() == 1);  // copy still there
}

TEST_CASE("M7 library: set_default rejects unknown ids") {
    PatternLibrary lib;
    CHECK_FALSE(lib.set_default("factory:bogus"));
    CHECK(lib.default_id() == std::string{kFlat});

    CHECK(lib.set_default(kHarmonic));
    CHECK(lib.default_id() == std::string{kHarmonic});
}

TEST_CASE("M7 library: removing the default pattern resets to FLAT") {
    PatternLibrary lib;
    BandField f;
    const auto p = lib.save_current(f, "MINE");
    REQUIRE(lib.set_default(p.id));
    REQUIRE(lib.remove(p.id));
    CHECK(lib.default_id() == std::string{kFlat});
}

TEST_CASE("M7 library: export → import round-trips user patterns") {
    PatternLibrary lib_a;
    BandField f;
    f.bands[3].gain_db = -18.0f;
    f.bands[7].muted   = true;
    lib_a.save_current(f, "SHAPE_A");
    f.bands[3].gain_db = +6.0f;
    f.bands[7].muted   = false;
    lib_a.save_current(f, "SHAPE_B");
    REQUIRE(lib_a.set_default(lib_a.user()[1].id));

    const auto json = lib_a.export_json();
    REQUIRE(!json.empty());

    PatternLibrary lib_b;
    const auto n = lib_b.import_json(json);
    CHECK(n == 2);
    REQUIRE(lib_b.user().size() == 2);
    // Shapes preserved.
    const Pattern* a = nullptr;
    const Pattern* b = nullptr;
    for (const auto& p : lib_b.user()) {
        if (p.name == "SHAPE_A") a = &p;
        if (p.name == "SHAPE_B") b = &p;
    }
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->gain_db[3] == Approx(-18.0f));
    CHECK(a->muted[7]   == true);
    CHECK(b->gain_db[3] == Approx(+6.0f));
    CHECK_FALSE(b->muted[7]);
    // Default_id transferred.
    CHECK(lib_b.default_id() == b->id);
}

TEST_CASE("M7 library: import rejects unknown schema version") {
    PatternLibrary lib;
    const std::string bad = R"({"format":"spectr.patterns","version":999,"patterns":[]})";
    CHECK(lib.import_json(bad) == 0);
    CHECK(lib.user().empty());
}

TEST_CASE("M7 library: import rejects malformed JSON") {
    PatternLibrary lib;
    CHECK(lib.import_json("not json at all") == 0);
    CHECK(lib.user().empty());
}

TEST_CASE("M7 library: import name-clash suffixes with (N)") {
    PatternLibrary lib;
    BandField f;
    lib.save_current(f, "TWIN");

    // Build JSON envelope by hand that also contains a pattern named TWIN.
    const std::string json = R"({
      "format": "spectr.patterns",
      "version": 1,
      "patterns": [
        {"id":"user:clone","name":"TWIN","source":"user",
         "created_at":"2026-01-01T00:00:00Z","updated_at":"2026-01-01T00:00:00Z",
         "gain_db":[], "muted":[]}
      ]
    })";
    const auto n = lib.import_json(json);
    REQUIRE(n == 1);
    CHECK(lib.user().size() == 2);
    // One named TWIN, one named "TWIN (2)".
    bool saw_twin = false, saw_twin2 = false;
    for (const auto& p : lib.user()) {
        if (p.name == "TWIN")     saw_twin  = true;
        if (p.name == "TWIN (2)") saw_twin2 = true;
    }
    CHECK(saw_twin);
    CHECK(saw_twin2);
}
