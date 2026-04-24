// Milestone 9.5 (slice 1) — JS ↔ C++ editor bridge.
//
// Unit tests for the message router. Every message type is exercised
// through the JSON envelope (i.e. the same path the WebView will
// drive). The `Spectr` plugin and an optional `PatternLibrary` are
// wired up to observe side effects.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/editor_bridge.hpp"
#include "spectr/pattern.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>

#include <memory>
#include <string>

using Catch::Approx;
using spectr::BandField;
using spectr::EditorBridgeState;
using spectr::PatternLibrary;
using spectr::SnapshotBank;
using spectr::Spectr;
using spectr::dispatch_editor_message_json;

namespace {

struct Rig {
    pulp::state::StateStore   store;
    std::unique_ptr<Spectr>   proc;
    PatternLibrary            library;
    EditorBridgeState         bridge;

    Rig() : proc(std::make_unique<Spectr>()) {
        proc->set_state_store(&store);
        proc->define_parameters(store);
    }
};

// choc::json::toString emits `"ok": true/false` with a space after
// the colon — match both with/without space so these helpers stay
// robust if the bridge switches to a no-space emitter later.
bool response_ok(const std::string& r) {
    return r.find("\"ok\": true")  != std::string::npos
        || r.find("\"ok\":true")   != std::string::npos;
}

bool response_has_error(const std::string& r, std::string_view substr) {
    const bool not_ok = r.find("\"ok\": false") != std::string::npos
                     || r.find("\"ok\":false")  != std::string::npos;
    return not_ok && r.find(substr) != std::string::npos;
}

} // namespace

TEST_CASE("M9.5 bridge: malformed JSON returns error") {
    Rig r;
    const auto resp = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
                                                   "not json");
    CHECK(response_has_error(resp, "malformed JSON"));
}

TEST_CASE("M9.5 bridge: missing 'type' returns error") {
    Rig r;
    const auto resp = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
                                                   R"({"payload":{}})");
    CHECK(response_has_error(resp, "'type'"));
}

TEST_CASE("M9.5 bridge: unknown type returns error") {
    Rig r;
    const auto resp = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
                                                   R"({"type":"not_a_message"})");
    CHECK(response_has_error(resp, "unknown message type"));
}

TEST_CASE("M9.5 bridge: reserved-for-future types return 'not implemented'") {
    Rig r;
    for (const auto* t : {"save_preset", "load_preset", "param_set"}) {
        const auto msg = std::string(R"({"type":")") + t + R"("})";
        const auto resp = dispatch_editor_message_json(*r.proc, &r.library, r.bridge, msg);
        INFO("type=" << t);
        CHECK(response_has_error(resp, "not implemented"));
    }
}

TEST_CASE("M9.5 bridge paint: paint without paint_start is rejected") {
    Rig r;
    const auto resp = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
        R"({"type":"paint","payload":{"mode":"Sculpt","start_band":0,"start_value":0,
            "current_band":3,"current_value":-6,"n_visible":32}})");
    CHECK(response_has_error(resp, "paint without paint_start"));
}

TEST_CASE("M9.5 bridge paint: start → paint → end mutates the field") {
    Rig r;
    // Start with a neutral field.
    r.proc->field() = BandField{};

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"paint_start"})")));

    // Sculpt drag from band 2 (0 dB) to band 5 (-6 dB).
    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"paint","payload":{"mode":"Sculpt","start_band":2,"start_value":0,
                "current_band":5,"current_value":-6,"n_visible":32}})")));

    // Bands 2..5 should now sit at -6 dB.
    for (std::size_t i = 2; i <= 5; ++i) {
        CHECK(r.proc->field().bands[i].gain_db == Approx(-6.0f));
    }
    // Bands outside the drag untouched.
    CHECK(r.proc->field().bands[0].gain_db == Approx(0.0f));

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"paint_end"})")));

    // After end, a paint without a new start fails.
    const auto after = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
        R"({"type":"paint","payload":{"mode":"Sculpt","start_band":0,"start_value":0,
            "current_band":0,"current_value":-3,"n_visible":32}})");
    CHECK(response_has_error(after, "paint without paint_start"));
}

TEST_CASE("M9.5 bridge paint: unknown mode returns error without mutating") {
    Rig r;
    const auto before = r.proc->field().bands[0].gain_db;
    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"paint_start"})")));
    const auto resp = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
        R"({"type":"paint","payload":{"mode":"Blaster","start_band":0,"start_value":0,
            "current_band":3,"current_value":-6,"n_visible":32}})");
    CHECK(response_has_error(resp, "unknown edit mode"));
    CHECK(r.proc->field().bands[0].gain_db == Approx(before));
}

TEST_CASE("M9.5 bridge morph: clamps t and applies to live field") {
    Rig r;
    // Populate A with -10 dB flat, B with +10 dB flat.
    for (auto& b : r.proc->field().bands) b.gain_db = -10.0f;
    r.proc->capture_snapshot(SnapshotBank::Slot::A);
    for (auto& b : r.proc->field().bands) b.gain_db = +10.0f;
    r.proc->capture_snapshot(SnapshotBank::Slot::B);

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"morph","payload":{"t":0.5}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(0.0f));

    // Out-of-range t clamps rather than erroring.
    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"morph","payload":{"t":5.0}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(+10.0f));

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"morph","payload":{"t":-2.0}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(-10.0f));
}

TEST_CASE("M9.5 bridge capture_snapshot: slot string is required") {
    Rig r;
    r.proc->field().bands[10].gain_db = -4.0f;

    const auto bad = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
        R"({"type":"capture_snapshot"})");
    CHECK(response_has_error(bad, "'A' or 'B'"));

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"capture_snapshot","payload":{"slot":"A"}})")));
    CHECK(r.proc->snapshots().has(SnapshotBank::Slot::A));
    CHECK(r.proc->snapshots().a.field.bands[10].gain_db == Approx(-4.0f));

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"capture_snapshot","payload":{"slot":"B"}})")));
    CHECK(r.proc->snapshots().has(SnapshotBank::Slot::B));
}

TEST_CASE("M9.5 bridge ab_toggle: flips active slot") {
    Rig r;
    CHECK(r.proc->snapshots().active == SnapshotBank::Slot::A);
    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"ab_toggle"})")));
    CHECK(r.proc->snapshots().active == SnapshotBank::Slot::B);
    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"ab_toggle"})")));
    CHECK(r.proc->snapshots().active == SnapshotBank::Slot::A);
}

TEST_CASE("M9.5 bridge load_pattern: applies by id, errors on unknown") {
    Rig r;
    // Flat factory pattern should land every band at 0 dB.
    for (auto& b : r.proc->field().bands) b.gain_db = -12.0f;

    REQUIRE(response_ok(dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
            R"({"type":"load_pattern","payload":{"id":"factory:flat"}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(0.0f));
    CHECK(r.proc->field().bands[63].gain_db == Approx(0.0f));

    const auto bad = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
        R"({"type":"load_pattern","payload":{"id":"factory:bogus"}})");
    CHECK(response_has_error(bad, "unknown pattern id"));

    const auto empty = dispatch_editor_message_json(*r.proc, &r.library, r.bridge,
        R"({"type":"load_pattern","payload":{}})");
    CHECK(response_has_error(empty, "pattern id missing"));
}

TEST_CASE("M9.5 bridge load_pattern: without a library attached errors") {
    Rig r;
    const auto resp = spectr::dispatch_editor_message_json(*r.proc, /*library*/nullptr,
        r.bridge, R"({"type":"load_pattern","payload":{"id":"factory:flat"}})");
    CHECK(response_has_error(resp, "pattern library"));
}
