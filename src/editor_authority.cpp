#include "spectr/editor_authority.hpp"

#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spectr {

namespace {

// Exact equality, deliberately. The question is "is this the state we already
// hold", not "is it close": a re-published payload round-trips through the same
// float domain and arrives bit-identical, and a one-dB nudge on a single band is
// a real edit that must still register. Every value reaching these helpers has
// already been validated finite, so there is no NaN self-comparison to reason
// about.
bool same_band_field(const BandField& a, const BandField& b) noexcept {
    for (std::size_t i = 0; i < a.bands.size(); ++i) {
        if (a.bands[i].gain_db != b.bands[i].gain_db
            || a.bands[i].muted != b.bands[i].muted)
            return false;
    }
    return true;
}

bool same_viewport(const Viewport& a, const Viewport& b) noexcept {
    return a.min_hz == b.min_hz && a.max_hz == b.max_hz;
}

} // namespace

EditorAuthority::EditorAuthority(Spectr& processor) noexcept
    : processor_(processor) {}

EditorReceipt EditorAuthority::reject_(std::string error) const {
    return {false, revision(), std::move(error)};
}

EditorReceipt EditorAuthority::accept_without_mutation_() const {
    return {true, revision(), {}};
}

EditorReceipt EditorAuthority::accept_mutation_() noexcept {
    // Hydration transports the revision as a signed JSON integer. Keep the
    // authority inside that exactly representable domain for its full life.
    auto current = revision_.load(std::memory_order_relaxed);
    while (current != kMaxEditorRevision
           && !revision_.compare_exchange_weak(
               current, current + 1,
               std::memory_order_release, std::memory_order_relaxed)) {}
    return {true, current == kMaxEditorRevision ? current : current + 1, {}};
}

EditorRevision EditorAuthority::record_external_mutation() noexcept {
    return accept_mutation_().revision;
}

bool EditorAuthority::matches_(
    std::optional<EditorRevision> expected) const noexcept {
    return !expected || *expected == revision();
}

EditorReceipt EditorAuthority::replace_processing_state(
    const BandField& field, const Viewport& viewport, Layout layout,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    if (!viewport.valid()) return reject_("invalid viewport");
    for (const auto& band : field.bands) {
        if (!std::isfinite(band.gain_db)
            || band.gain_db < kBandGainMinDb
            || band.gain_db > kBandGainMaxDb) {
            return reject_("invalid band field");
        }
    }
    // Idempotence (spectr#49). `revision` is the change signal both the editor
    // and the host-automation path read, so a publication carrying the state we
    // already hold must not report a change. Two call sites publish this state —
    // a tap handler and an async effect — and under contention the second
    // arrival is a byte-identical duplicate of the first, which used to bump the
    // counter for a change that never happened. With #34 making ~140 parameters
    // host-automatable and #37 having the UI observe them, a false change signal
    // at that scale drives spurious parameter writes and redundant repaints.
    //
    // Evaluated AFTER the validation above, so a malformed duplicate still
    // rejects rather than being waved through as a no-op. The write itself is
    // deliberately still performed: a redundant publication of identical state
    // recompiles an identical mask, so keeping it makes the counter the ONLY
    // observable difference this change introduces. Skipping the write too would
    // be a further improvement, but it is a separate behavioural claim about the
    // audio-thread publish path and does not belong in this fix.
    const auto current = processor_.processing_state_snapshot();
    const bool unchanged = layout == current.layout
        && same_viewport(viewport, current.viewport)
        && same_band_field(field, current.field);
    if (!processor_.replace_processing_state(field, viewport, layout))
        return reject_("invalid processing state");
    edit_snapshot_.reset();
    if (unchanged) return accept_without_mutation_();
    return accept_mutation_();
}

EditorReceipt EditorAuthority::begin_band_edit(
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    edit_snapshot_ = BandSnapshot::capture(
        processor_.processing_state_snapshot().field);
    // spectr#34: open a host-gesture epoch so the paint's parameter writes
    // bracket as one begin/end per touched band per drag, not per event.
    processor_.begin_param_gesture_epoch();
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::update_band_edit(
    EditMode mode, const DragGesture& gesture,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) {
        edit_snapshot_.reset();
        processor_.end_param_gesture_epoch();
        return reject_("stale editor revision");
    }
    if (!edit_snapshot_) return reject_("paint without paint_start");
    const auto current = processor_.processing_state_snapshot();
    const auto visible = visible_count(current.layout);
    if (gesture.n_visible != visible
        || gesture.start_band >= visible
        || gesture.current_band >= visible
        || !std::isfinite(gesture.start_value)
        || !std::isfinite(gesture.current_value)
        || gesture.start_value < kBandGainMinDb
        || gesture.start_value > kBandGainMaxDb
        || gesture.current_value < kBandGainMinDb
        || gesture.current_value > kBandGainMaxDb) {
        return reject_("paint geometry or values are invalid");
    }
    auto next = current.field;
    dispatch_edit(mode, next, gesture, *edit_snapshot_);
    if (!processor_.replace_processing_state(
            next, current.viewport, current.layout)) {
        return reject_("paint produced invalid processing state");
    }
    return accept_mutation_();
}

EditorReceipt EditorAuthority::end_band_edit() noexcept {
    edit_snapshot_.reset();
    processor_.end_param_gesture_epoch();
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::cancel_band_edit() noexcept {
    edit_snapshot_.reset();
    processor_.end_param_gesture_epoch();
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::capture_snapshot(
    SnapshotBank::Slot slot,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    processor_.capture_snapshot(slot);
    return accept_mutation_();
}

EditorReceipt EditorAuthority::recall_snapshot(
    SnapshotBank::Slot slot,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    const auto& snapshot = processor_.snapshots().get(slot);
    if (!snapshot.populated) return reject_("snapshot slot is empty");
    if (!processor_.replace_processing_state(
            snapshot.field, snapshot.viewport, snapshot.layout)) {
        return reject_("snapshot state is invalid");
    }
    edit_snapshot_.reset();
    return accept_mutation_();
}

EditorReceipt EditorAuthority::apply_morph(
    float amount, std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    if (!std::isfinite(amount)) return reject_("morph amount must be finite");
    if (!processor_.snapshots().has(SnapshotBank::Slot::A)
        && !processor_.snapshots().has(SnapshotBank::Slot::B)) {
        return reject_("both snapshot slots are empty");
    }
    processor_.apply_morph_to_live(std::clamp(amount, 0.0f, 1.0f));
    edit_snapshot_.reset();
    return accept_mutation_();
}

void EditorAuthority::reset_transient_state() noexcept {
    edit_snapshot_.reset();
    processor_.end_param_gesture_epoch();
}

} // namespace spectr
