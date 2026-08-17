#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "spectr/editor_authority.hpp"
#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>
#include <pulp/view/editor_bridge.hpp>

#include <choc/text/choc_JSON.h>

#include <limits>

using Catch::Approx;

namespace {

struct AuthorityRig {
    pulp::state::StateStore store;
    spectr::Spectr processor;

    AuthorityRig() {
        processor.set_state_store(&store);
        processor.define_parameters(store);
    }
};

} // namespace

TEST_CASE("editor authority owns one monotonic revision across state snapshots and morph",
          "[editor-authority]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();
    REQUIRE(authority.revision() == 0);

    auto field = r.processor.field();
    field.bands[3].gain_db = -9.0f;
    REQUIRE(authority.replace_processing_state(
        field, {80.0f, 8000.0f}, spectr::Layout::Bands32, 0).revision == 1);
    REQUIRE(authority.capture_snapshot(spectr::SnapshotBank::Slot::A, 1).revision == 2);

    field.bands[3].gain_db = 15.0f;
    REQUIRE(authority.replace_processing_state(
        field, {80.0f, 8000.0f}, spectr::Layout::Bands32, 2).revision == 3);
    REQUIRE(authority.capture_snapshot(spectr::SnapshotBank::Slot::B, 3).revision == 4);

    const auto morph = authority.apply_morph(0.5f, 4);
    REQUIRE(morph.accepted);
    REQUIRE(morph.revision == 5);
    REQUIRE(r.processor.field().bands[3].gain_db == Approx(3.0f));
}

TEST_CASE("editor authority rejects stale or invalid commands failure atomically",
          "[editor-authority]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();
    auto field = r.processor.field();
    field.bands[9].gain_db = 6.0f;
    REQUIRE(authority.replace_processing_state(
        field, r.processor.viewport(), r.processor.layout(), 0).accepted);

    const auto before = r.processor.field();
    const auto before_viewport = r.processor.viewport();
    const auto before_layout = r.processor.layout();
    const auto revision = authority.revision();

    auto stale = before;
    stale.bands[9].gain_db = -17.0f;
    const auto stale_receipt = authority.replace_processing_state(
        stale, {300.0f, 600.0f}, spectr::Layout::Bands64, 0);
    REQUIRE_FALSE(stale_receipt.accepted);
    CHECK(stale_receipt.error == "stale editor revision");

    auto invalid = before;
    invalid.bands[9].gain_db = std::numeric_limits<float>::infinity();
    const auto invalid_receipt = authority.replace_processing_state(
        invalid, {300.0f, 600.0f}, spectr::Layout::Bands64, revision);
    REQUIRE_FALSE(invalid_receipt.accepted);
    CHECK(authority.revision() == revision);
    CHECK(r.processor.field().bands[9].gain_db == Approx(before.bands[9].gain_db));
    CHECK(r.processor.viewport().min_hz == Approx(before_viewport.min_hz));
    CHECK(r.processor.viewport().max_hz == Approx(before_viewport.max_hz));
    CHECK(r.processor.layout() == before_layout);
}

TEST_CASE("editor authority invalidates a captured gesture on concurrent mutation",
          "[editor-authority]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();
    REQUIRE(authority.begin_band_edit(0).accepted);

    auto concurrent = r.processor.field();
    concurrent.bands[4].gain_db = -7.0f;
    REQUIRE(authority.replace_processing_state(
        concurrent, r.processor.viewport(), r.processor.layout(), 0).accepted);

    spectr::DragGesture gesture;
    gesture.start_band = 4;
    gesture.current_band = 4;
    gesture.start_value = -7.0f;
    gesture.current_value = 12.0f;
    gesture.n_visible = 32;
    const auto stale_update = authority.update_band_edit(
        spectr::EditMode::Sculpt, gesture, 0);
    REQUIRE_FALSE(stale_update.accepted);
    CHECK(r.processor.field().bands[4].gain_db == Approx(-7.0f));
    CHECK(authority.revision() == 1);

    const auto retired_update = authority.update_band_edit(
        spectr::EditMode::Sculpt, gesture, 1);
    REQUIRE_FALSE(retired_update.accepted);
    CHECK(retired_update.error == "paint without paint_start");
}

TEST_CASE("editor bridge never degrades a malformed revision into an unconditional mutation",
          "[editor-authority]") {
    AuthorityRig r;
    pulp::view::EditorBridge bridge;
    spectr::register_spectr_editor_handlers(
        bridge, r.processor, r.processor.patterns(),
        r.processor.editor_authority());

    auto gains = choc::value::createArray(32, [](std::uint32_t i) {
        return i == 6 ? 9.0 : 0.0;
    });
    auto mutes = choc::value::createArray(32, [](std::uint32_t) {
        return false;
    });
    auto payload = choc::value::createObject("BandFieldPayload");
    payload.addMember("n_visible", 32);
    payload.addMember("gain_db", gains);
    payload.addMember("muted", mutes);
    payload.addMember("expected_revision", "not-a-revision");
    auto envelope = choc::value::createObject("Envelope");
    envelope.addMember("type", "band_field_set");
    envelope.addMember("payload", payload);

    const auto response = bridge.dispatch_json(choc::json::toString(envelope, false));
    REQUIRE(response.find("stale editor revision") != std::string::npos);
    CHECK(r.processor.editor_authority().revision() == 0);
    CHECK(r.processor.field().bands[6].gain_db == Approx(0.0f));
}

// spectr#49. `revision` is the editor's change signal: the UI and the host
// automation path both treat a bump as "the state moved". Two call sites
// publish the same processing state (a tap handler and an async effect), so
// under contention the same state arrives twice and the second arrival used to
// bump anyway — a change report for a change that did not happen. With #34
// making ~140 parameters host-automatable and #37 having the UI observe them,
// a false change signal at that scale drives spurious parameter writes and
// redundant repaints, so idempotence is the correct invariant rather than a
// workaround for a flaky test.
TEST_CASE("editor authority treats a re-published identical state as no change",
          "[editor-authority][idempotence]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();
    REQUIRE(authority.revision() == 0);

    const spectr::Viewport viewport{80.0f, 8000.0f};
    auto field = r.processor.field();
    field.bands[16].muted = true;

    // A real change advances exactly once.
    const auto first = authority.replace_processing_state(
        field, viewport, spectr::Layout::Bands32, 0);
    REQUIRE(first.accepted);
    REQUIRE(first.revision == 1);
    REQUIRE(r.processor.field().bands[16].muted);

    // The duplicate the reproduction captured: byte-identical payload, arriving
    // after the original was already applied. Accepted, but not a change.
    const auto duplicate = authority.replace_processing_state(
        field, viewport, spectr::Layout::Bands32, 1);
    CHECK(duplicate.accepted);
    CHECK(duplicate.revision == 1);
    CHECK(authority.revision() == 1);
    CHECK(r.processor.field().bands[16].muted);

    // Repeating it cannot creep the counter either.
    for (int repeat = 0; repeat < 4; ++repeat) {
        const auto again = authority.replace_processing_state(
            field, viewport, spectr::Layout::Bands32, 1);
        CHECK(again.accepted);
        CHECK(again.revision == 1);
    }
    CHECK(authority.revision() == 1);

    // Idempotence must not swallow a real edit that follows.
    field.bands[16].muted = false;
    const auto unmute = authority.replace_processing_state(
        field, viewport, spectr::Layout::Bands32, 1);
    CHECK(unmute.accepted);
    CHECK(unmute.revision == 2);
    CHECK_FALSE(r.processor.field().bands[16].muted);

    // Nor a change confined to the viewport, or to the layout, with the band
    // field untouched — both are sound-defining in Spectr.
    const auto zoom = authority.replace_processing_state(
        field, {100.0f, 10000.0f}, spectr::Layout::Bands32, 2);
    CHECK(zoom.accepted);
    CHECK(zoom.revision == 3);

    const auto relayout = authority.replace_processing_state(
        field, {100.0f, 10000.0f}, spectr::Layout::Bands40, 3);
    CHECK(relayout.accepted);
    CHECK(relayout.revision == 4);

    // A single-band, single-dB delta is a change, not rounding noise.
    field.bands[0].gain_db = 1.0f;
    const auto nudge = authority.replace_processing_state(
        field, {100.0f, 10000.0f}, spectr::Layout::Bands40, 4);
    CHECK(nudge.accepted);
    CHECK(nudge.revision == 5);

    // A stale expectation must still reject, and must not be reinterpreted as
    // an idempotent no-op just because the payload happens to match.
    const auto stale = authority.replace_processing_state(
        field, {100.0f, 10000.0f}, spectr::Layout::Bands40, 1);
    CHECK_FALSE(stale.accepted);
    CHECK(stale.revision == 5);
}
