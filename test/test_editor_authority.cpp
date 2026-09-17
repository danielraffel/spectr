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

TEST_CASE("external host mutation retires a stale editor gesture",
          "[editor-authority][automation]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();
    REQUIRE(authority.begin_band_edit(0).accepted);

    CHECK(authority.record_external_mutation() == 1);
    CHECK(authority.revision() == 1);

    spectr::DragGesture gesture;
    gesture.start_band = 8;
    gesture.current_band = 8;
    gesture.start_value = 0.0f;
    gesture.current_value = 12.0f;
    gesture.n_visible = 32;
    const auto stale = authority.update_band_edit(
        spectr::EditMode::Sculpt, gesture, 0);
    CHECK_FALSE(stale.accepted);
    CHECK(stale.error == "stale editor revision");
    CHECK(authority.revision() == 1);
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

// ── Undo / redo ────────────────────────────────────────────────────────
//
// The rule these cases exist to hold is the user's own workflow rule: ONE
// drag across many bands is ONE undo step. It is asserted on band VALUES
// after undo, never on a depth alone, because a stack of the right height
// carrying the wrong states would satisfy a count and still lose the work.

namespace {

// A drag as this editor actually issues it: the complete processing state
// republished once per pointer sample. `samples` is what makes the bracket
// load-bearing -- with one sample per drag, bracketed and unbracketed are
// indistinguishable.
void publish_band_sweep_(spectr::EditorAuthority& authority,
                         spectr::Spectr& processor, std::size_t bands,
                         float target_db, int samples) {
    for (int s = 1; s <= samples; ++s) {
        auto field = processor.field();
        const float t = static_cast<float>(s) / static_cast<float>(samples);
        for (std::size_t i = 0; i < bands; ++i) field.bands[i].gain_db = target_db * t;
        REQUIRE(authority.replace_processing_state(
            field, processor.viewport(), processor.layout()).accepted);
    }
}

} // namespace

TEST_CASE("undo: one drag across many bands is one undo step",
          "[editor-authority][undo]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    const auto before = r.processor.field();
    REQUIRE(before.bands[0].gain_db == Approx(0.0f));
    REQUIRE(authority.undo_depth() == 0);

    authority.begin_undo_gesture("Edit bands");
    publish_band_sweep_(authority, r.processor, 32, -12.0f, /*samples=*/20);
    authority.end_undo_gesture();

    // ONE step for the whole drag -- not one per pointer sample, and not one
    // per band. Twenty publications over thirty-two bands went in.
    REQUIRE(authority.undo_depth() == 1);
    REQUIRE(authority.can_undo());
    REQUIRE_FALSE(authority.can_redo());
    REQUIRE(r.processor.field().bands[0].gain_db == Approx(-12.0f));
    REQUIRE(r.processor.field().bands[31].gain_db == Approx(-12.0f));

    // One press returns every band, not just the last one written.
    REQUIRE(authority.undo().accepted);
    for (std::size_t i = 0; i < 32; ++i)
        REQUIRE(r.processor.field().bands[i].gain_db == Approx(0.0f));
    REQUIRE(authority.undo_depth() == 0);
    REQUIRE(authority.can_redo());

    // And redo returns the drag's END state, not an intermediate sample.
    REQUIRE(authority.redo().accepted);
    for (std::size_t i = 0; i < 32; ++i)
        REQUIRE(r.processor.field().bands[i].gain_db == Approx(-12.0f));
}

TEST_CASE("undo: the unbracketed shape is what the gesture replaces",
          "[editor-authority][undo]") {
    // The POSITIVE CONTROL for the case above. Without it "one undo step" is
    // a number with no scale: a reader cannot tell whether the bracket did
    // anything or whether this editor only ever produced one step anyway.
    // The identical sweep, with no gesture open, is the shape that shipped
    // before -- and it is what a user would have had to press undo through.
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    publish_band_sweep_(authority, r.processor, 32, -12.0f, /*samples=*/20);

    REQUIRE(authority.undo_depth() == 20);
    // One press walks back a single pointer sample, leaving the bands very
    // nearly where the drag left them.
    REQUIRE(authority.undo().accepted);
    REQUIRE(r.processor.field().bands[0].gain_db == Approx(-11.4f));
}

TEST_CASE("undo: gain and mute are restored together",
          "[editor-authority][undo]") {
    // Two axes of one band. An undo that restored the level without the mute
    // it was authored under -- or the reverse -- would be worse than none,
    // and it is the exact failure a per-parameter undo produces. An entry is
    // a whole BandField, so this holds by construction; the case proves it
    // rather than trusting the construction.
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    auto field = r.processor.field();
    field.bands[5].gain_db = -6.0f;
    field.bands[5].muted = false;
    field.bands[7].gain_db = 9.0f;
    field.bands[7].muted = true;
    REQUIRE(authority.replace_processing_state(
        field, r.processor.viewport(), r.processor.layout()).accepted);

    const auto authored = r.processor.field();
    REQUIRE(authored.bands[5].gain_db == Approx(-6.0f));
    REQUIRE_FALSE(authored.bands[5].muted);
    REQUIRE(authored.bands[7].muted);

    // Now move BOTH axes on both bands in one gesture: band 5 gets muted at a
    // new level, band 7 gets unmuted at a new level. This is the group-drag
    // shape where gain and mute move together.
    authority.begin_undo_gesture("Edit bands");
    auto next = r.processor.field();
    next.bands[5].gain_db = 3.0f;
    next.bands[5].muted = true;
    next.bands[7].gain_db = -15.0f;
    next.bands[7].muted = false;
    REQUIRE(authority.replace_processing_state(
        next, r.processor.viewport(), r.processor.layout()).accepted);
    authority.end_undo_gesture();

    REQUIRE(authority.undo().accepted);
    const auto restored = r.processor.field();
    // Both fields of both bands, coherent with each other.
    REQUIRE(restored.bands[5].gain_db == Approx(-6.0f));
    REQUIRE_FALSE(restored.bands[5].muted);
    REQUIRE(restored.bands[7].gain_db == Approx(9.0f));
    REQUIRE(restored.bands[7].muted);
}

TEST_CASE("undo: the viewport rides the same entry as the bands",
          "[editor-authority][undo]") {
    // The viewport is sound-defining state in Spectr (snapshot.hpp says so:
    // it sets the band-to-frequency mapping the mask is built from), so an
    // undo that restored bands into a different viewport would restore a
    // sound the user never had.
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    auto field = r.processor.field();
    field.bands[2].gain_db = 4.0f;
    REQUIRE(authority.replace_processing_state(
        field, {200.0f, 4000.0f}, spectr::Layout::Bands32).accepted);
    REQUIRE(authority.undo().accepted);

    REQUIRE(r.processor.field().bands[2].gain_db == Approx(0.0f));
    REQUIRE(r.processor.viewport().min_hz == Approx(20.0f));
    REQUIRE(r.processor.viewport().max_hz == Approx(20000.0f));
}

TEST_CASE("undo: a gesture that changed nothing records no step",
          "[editor-authority][undo]") {
    // A press that only opens a menu, or a drag returned to where it began,
    // must not leave a step that appears to do nothing when pressed.
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    authority.begin_undo_gesture("Edit bands");
    authority.end_undo_gesture();
    REQUIRE(authority.undo_depth() == 0);
    REQUIRE_FALSE(authority.can_undo());

    // A drag that moves out and comes back is also a no-op overall.
    const auto start = r.processor.field();
    authority.begin_undo_gesture("Edit bands");
    auto moved = start;
    moved.bands[1].gain_db = 8.0f;
    REQUIRE(authority.replace_processing_state(
        moved, r.processor.viewport(), r.processor.layout()).accepted);
    REQUIRE(authority.replace_processing_state(
        start, r.processor.viewport(), r.processor.layout()).accepted);
    authority.end_undo_gesture();
    REQUIRE(authority.undo_depth() == 0);
}

TEST_CASE("undo: nothing to undo is reported, never silently accepted",
          "[editor-authority][undo]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    const auto empty = authority.undo();
    REQUIRE_FALSE(empty.accepted);
    REQUIRE(empty.error == "nothing to undo");

    const auto no_redo = authority.redo();
    REQUIRE_FALSE(no_redo.accepted);
    REQUIRE(no_redo.error == "nothing to redo");
}

TEST_CASE("undo: a fresh edit after an undo drops the redo branch",
          "[editor-authority][undo]") {
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    auto field = r.processor.field();
    field.bands[0].gain_db = -3.0f;
    REQUIRE(authority.replace_processing_state(
        field, r.processor.viewport(), r.processor.layout()).accepted);
    REQUIRE(authority.undo().accepted);
    REQUIRE(authority.can_redo());

    auto other = r.processor.field();
    other.bands[10].gain_db = 7.0f;
    REQUIRE(authority.replace_processing_state(
        other, r.processor.viewport(), r.processor.layout()).accepted);

    // Redoing onto a branch the user has already left would resurrect work
    // they replaced.
    REQUIRE_FALSE(authority.can_redo());
}

TEST_CASE("undo: an undo is not itself recorded as a new edit",
          "[editor-authority][undo]") {
    // The loop where undo pushes its own inverse makes undo and redo the
    // same button: press undo twice and the second press redoes the first.
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    auto field = r.processor.field();
    field.bands[4].gain_db = -18.0f;
    REQUIRE(authority.replace_processing_state(
        field, r.processor.viewport(), r.processor.layout()).accepted);
    REQUIRE(authority.undo_depth() == 1);

    REQUIRE(authority.undo().accepted);
    REQUIRE(authority.undo_depth() == 0);
    REQUIRE(r.processor.field().bands[4].gain_db == Approx(0.0f));

    // A second press has nothing left to do -- it must not walk forward.
    REQUIRE_FALSE(authority.undo().accepted);
    REQUIRE(r.processor.field().bands[4].gain_db == Approx(0.0f));
}

TEST_CASE("undo: a band drag through the paint protocol is one step",
          "[editor-authority][undo]") {
    // The paint triad brackets itself, so a caller using paint_start/paint/
    // paint_end gets the same one-step granularity without issuing the undo
    // gesture verbs at all.
    AuthorityRig r;
    auto& authority = r.processor.editor_authority();

    REQUIRE(authority.begin_band_edit().accepted);
    for (int s = 0; s < 12; ++s) {
        spectr::DragGesture g;
        g.start_band = 4;
        g.start_value = 0.0f;
        g.current_band = static_cast<std::size_t>(4 + s);
        g.current_value = -10.0f;
        g.n_visible = spectr::visible_count(r.processor.layout());
        REQUIRE(authority.update_band_edit(spectr::EditMode::Sculpt, g).accepted);
    }
    REQUIRE(authority.end_band_edit().accepted);

    REQUIRE(authority.undo_depth() == 1);
    REQUIRE(authority.undo().accepted);
    for (std::size_t i = 0; i < spectr::visible_count(r.processor.layout()); ++i)
        REQUIRE(r.processor.field().bands[i].gain_db == Approx(0.0f));
}
