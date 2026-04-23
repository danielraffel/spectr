// Milestone 9 — Preset file format.
//
// Coverage:
//   - save → load → round-trip matches working state exactly (both
//     StateStore params and plugin-owned supplemental state).
//   - Schema-mismatch rejection with a clear migration error.
//   - Corrupt / malformed file handling.
//   - Metadata round-trip (name / author / description / timestamps).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/preset_format.hpp"
#include "spectr/spectr.hpp"
#include "spectr/snapshot.hpp"

#include <pulp/state/store.hpp>

#include <string>

using Catch::Approx;
using spectr::Spectr;
using spectr::SnapshotBank;
using spectr::PresetLoadError;
using spectr::PresetMetadata;
using spectr::save_preset_to_string;
using spectr::load_preset_from_string;
using spectr::kPresetSchemaVersion;

namespace {

struct Rig {
    pulp::state::StateStore store;
    std::unique_ptr<Spectr>  proc;

    Rig() : proc(std::make_unique<Spectr>()) {
        proc->set_state_store(&store);
        proc->define_parameters(store);
    }
};

} // namespace

TEST_CASE("M9 preset round-trip preserves working state") {
    Rig a;
    // Build a non-default state across both flat params and supplemental.
    a.store.set_value(spectr::kMix,        42.0f);
    a.store.set_value(spectr::kOutputTrim,  6.0f);
    a.store.set_value(spectr::kMorph,       0.33f);
    a.proc->field().bands[7].gain_db  = -9.0f;
    a.proc->field().bands[42].muted   = true;
    a.proc->viewport().min_hz = 120.0f;
    a.proc->viewport().max_hz = 7200.0f;
    a.proc->set_layout(spectr::Layout::Bands56);

    // Capture into both snapshot slots so the preset exercises the full
    // bank round-trip path too.
    a.proc->capture_snapshot(SnapshotBank::Slot::A);
    a.proc->field() = spectr::BandField{};
    a.proc->field().bands[0].gain_db = +6.0f;
    a.proc->capture_snapshot(SnapshotBank::Slot::B);
    a.proc->snapshots().active = SnapshotBank::Slot::B;

    PresetMetadata meta;
    meta.name = "RoundTrip Test";
    meta.author = "M9";
    meta.description = "Exercises every M9 round-trip path.";
    meta.created_at = "2026-04-23T19:00:00Z";
    meta.modified_at = "2026-04-23T19:00:05Z";

    const std::string json = save_preset_to_string(*a.proc, meta);
    REQUIRE_FALSE(json.empty());

    Rig b;
    const auto result = load_preset_from_string(*b.proc, json);
    REQUIRE(result);
    CHECK(result.error == PresetLoadError::None);
    CHECK(result.file_schema_version == kPresetSchemaVersion);
    CHECK(result.metadata.name        == "RoundTrip Test");
    CHECK(result.metadata.author      == "M9");
    CHECK(result.metadata.description == "Exercises every M9 round-trip path.");
    CHECK(result.metadata.created_at  == "2026-04-23T19:00:00Z");
    CHECK(result.metadata.modified_at == "2026-04-23T19:00:05Z");

    // Flat params restored.
    CHECK(b.store.get_value(spectr::kMix)        == Approx(42.0f));
    CHECK(b.store.get_value(spectr::kOutputTrim) == Approx(6.0f));
    CHECK(b.store.get_value(spectr::kMorph)      == Approx(0.33f));

    // Supplemental state restored.
    CHECK(b.proc->field().bands[0].gain_db == Approx(+6.0f));  // live field = B's snapshot
    CHECK(b.proc->viewport().min_hz == Approx(120.0f));
    CHECK(b.proc->viewport().max_hz == Approx(7200.0f));
    CHECK(b.proc->layout() == spectr::Layout::Bands56);

    // Snapshot bank restored.
    CHECK(b.proc->snapshots().has(SnapshotBank::Slot::A));
    CHECK(b.proc->snapshots().has(SnapshotBank::Slot::B));
    CHECK(b.proc->snapshots().a.field.bands[7].gain_db  == Approx(-9.0f));
    CHECK(b.proc->snapshots().a.field.bands[42].muted   == true);
    CHECK(b.proc->snapshots().b.field.bands[0].gain_db  == Approx(+6.0f));
    CHECK(b.proc->snapshots().active == SnapshotBank::Slot::B);
}

TEST_CASE("M9 schema mismatch returns a clear migration error") {
    Rig r;

    // Valid JSON, valid format tag, but schema_version = 999.
    const std::string bad =
        R"({"format":"spectr.preset","schema_version":999,"plugin_version":"1.0.0",)"
        R"("metadata":{},)"
        R"("state":{"state_store":"","plugin_state":{}}})";

    const auto result = load_preset_from_string(*r.proc, bad);
    CHECK_FALSE(result);
    CHECK(result.error == PresetLoadError::SchemaMismatch);
    CHECK(result.file_schema_version == 999);
    // Message mentions migration path.
    const std::string msg = spectr::describe(result.error);
    CHECK(msg.find("schema") != std::string::npos);
    CHECK(msg.find("migrate") != std::string::npos);
}

TEST_CASE("M9 rejects files that aren't Spectr presets") {
    Rig r;
    const std::string not_ours =
        R"({"format":"someother.preset","schema_version":1})";
    const auto result = load_preset_from_string(*r.proc, not_ours);
    CHECK_FALSE(result);
    CHECK(result.error == PresetLoadError::NotASpectrPreset);
}

TEST_CASE("M9 rejects malformed JSON without touching processor state") {
    Rig r;
    r.store.set_value(spectr::kMix, 50.0f);
    r.proc->field().bands[0].gain_db = -3.0f;

    const auto result = load_preset_from_string(*r.proc, "not json at all {");
    CHECK_FALSE(result);
    CHECK(result.error == PresetLoadError::MalformedJson);

    // Original state unchanged.
    CHECK(r.store.get_value(spectr::kMix) == Approx(50.0f));
    CHECK(r.proc->field().bands[0].gain_db == Approx(-3.0f));
}

TEST_CASE("M9 reports MissingState when the state block is absent") {
    Rig r;
    const std::string no_state =
        R"({"format":"spectr.preset","schema_version":1,"plugin_version":"1.0.0","metadata":{}})";
    const auto result = load_preset_from_string(*r.proc, no_state);
    CHECK_FALSE(result);
    CHECK(result.error == PresetLoadError::MissingState);
}

TEST_CASE("M9 reports CorruptState on undecodable state_store") {
    Rig r;
    const std::string bad_b64 =
        R"({"format":"spectr.preset","schema_version":1,"plugin_version":"1.0.0",)"
        R"("metadata":{},)"
        R"("state":{"state_store":"!!!not base64!!!","plugin_state":{}}})";
    const auto result = load_preset_from_string(*r.proc, bad_b64);
    CHECK_FALSE(result);
    CHECK(result.error == PresetLoadError::CorruptState);
}

TEST_CASE("M9 file save/load round-trip works end-to-end") {
    Rig a;
    a.store.set_value(spectr::kMix, 25.0f);
    a.proc->field().bands[3].gain_db = -5.0f;

    const std::string path = "/tmp/spectr-m9-roundtrip.preset";
    PresetMetadata meta;
    meta.name = "File";
    REQUIRE(spectr::save_preset_to_file(*a.proc, meta, path));

    Rig b;
    const auto result = spectr::load_preset_from_file(*b.proc, path);
    REQUIRE(result);
    CHECK(b.store.get_value(spectr::kMix) == Approx(25.0f));
    CHECK(b.proc->field().bands[3].gain_db == Approx(-5.0f));
    CHECK(result.metadata.name == "File");
}

TEST_CASE("M9 describe() returns non-empty stable messages for every error") {
    using E = PresetLoadError;
    for (E e : {E::None, E::MalformedJson, E::NotASpectrPreset,
                E::SchemaMismatch, E::MissingState, E::CorruptState}) {
        const char* msg = spectr::describe(e);
        REQUIRE(msg != nullptr);
        CHECK(std::string(msg).size() > 0);
    }
}
