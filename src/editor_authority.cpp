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
    // The pre-edit state, captured before the write below lands. `current` is
    // already exactly that, so this costs a copy rather than a second read.
    FieldSnapshot before{};
    before.field = current.field;
    before.viewport = current.viewport;
    before.layout = current.layout;
    before.populated = true;

    if (!processor_.replace_processing_state(field, viewport, layout))
        return reject_("invalid processing state");
    edit_snapshot_.reset();
    if (unchanged) return accept_without_mutation_();
    // Recorded only outside a gesture; during a drag the bracket owns it.
    record_history_("Edit bands", before);
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
    // The same boundary is the undo boundary: one drag is one undo step,
    // however many `update_band_edit` calls it streams.
    begin_undo_gesture("Edit bands");
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::update_band_edit(
    EditMode mode, const DragGesture& gesture,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) {
        edit_snapshot_.reset();
        processor_.end_param_gesture_epoch();
        // Close the undo bracket on this path too. Leaving it open would
        // fold every later edit into the abandoned drag's entry, so one undo
        // would revert work the user did long after it.
        end_undo_gesture();
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
    end_undo_gesture();
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::cancel_band_edit() noexcept {
    edit_snapshot_.reset();
    processor_.end_param_gesture_epoch();
    // `cancel` here does NOT revert — it drops the drag snapshot and closes
    // the host epoch, leaving whatever the drag already applied in place. So
    // the undo bracket is closed the same way `end` closes it: the state
    // moved and must stay reachable by undo. If cancel ever gains a real
    // revert, this is the line that has to become a discard instead.
    end_undo_gesture();
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::capture_snapshot(
    SnapshotBank::Slot slot,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    processor_.capture_snapshot(slot);
    return accept_mutation_();
}

EditorReceipt EditorAuthority::clear_snapshot(
    SnapshotBank::Slot slot,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    processor_.clear_snapshot(slot);
    return accept_mutation_();
}

EditorReceipt EditorAuthority::recall_snapshot(
    SnapshotBank::Slot slot,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    const auto& snapshot = processor_.snapshots().get(slot);
    if (!snapshot.populated) return reject_("snapshot slot is empty");
    const auto before = capture_state_();
    if (!processor_.replace_processing_state(
            snapshot.field, snapshot.viewport, snapshot.layout)) {
        return reject_("snapshot state is invalid");
    }
    edit_snapshot_.reset();
    record_history_(slot == SnapshotBank::Slot::A ? "Recall A" : "Recall B",
                    before);
    return accept_mutation_();
}

// ── Undo / redo ────────────────────────────────────────────────────────

FieldSnapshot EditorAuthority::capture_state_() const noexcept {
    const auto current = processor_.processing_state_snapshot();
    FieldSnapshot s{};
    s.field = current.field;
    s.viewport = current.viewport;
    s.layout = current.layout;
    s.populated = true;
    return s;
}

bool EditorAuthority::restore_state_(const FieldSnapshot& state) noexcept {
    // Straight to the processor, deliberately bypassing this class's own
    // `replace_processing_state`. That method exists to validate and record
    // an EDIT; a history replay is neither. Routing it back through there
    // would re-enter the recorder and make undo push its own inverse.
    return processor_.replace_processing_state(state.field, state.viewport,
                                               state.layout);
}

void EditorAuthority::record_history_(std::string name,
                                      const FieldSnapshot& before) noexcept {
    if (replaying_history_) return;
    // Inside a gesture the close records the whole span; recording here too
    // would give the drag one entry per pointer sample AND one for itself.
    if (gesture_depth_ > 0) return;
    const auto after = capture_state_();
    history_.add_without_executing(pulp::state::UndoAction::create(
        std::move(name),
        [this, before] { (void)restore_state_(before); },
        [this, after] { (void)restore_state_(after); }));
}

void EditorAuthority::begin_undo_gesture(std::string name) noexcept {
    if (gesture_depth_++ > 0) return;  // already open; keep the true base
    gesture_base_ = capture_state_();
    gesture_name_ = name.empty() ? std::string{"Edit bands"} : std::move(name);
}

void EditorAuthority::end_undo_gesture() noexcept {
    if (gesture_depth_ == 0) return;   // unbalanced close; nothing to record
    if (--gesture_depth_ > 0) return;  // inner close of a nested pair
    if (!gesture_base_) return;
    const auto before = *gesture_base_;
    gesture_base_.reset();
    const auto after = capture_state_();
    // A gesture that changed nothing records nothing. Without this a click
    // that only opens the context menu leaves an undo step that appears to
    // do nothing when pressed.
    if (same_band_field(before.field, after.field)
        && same_viewport(before.viewport, after.viewport)
        && before.layout == after.layout) {
        return;
    }
    if (replaying_history_) return;
    history_.add_without_executing(pulp::state::UndoAction::create(
        gesture_name_,
        [this, before] { (void)restore_state_(before); },
        [this, after] { (void)restore_state_(after); }));
}

EditorReceipt EditorAuthority::undo() noexcept {
    if (!history_.can_undo()) return reject_("nothing to undo");
    replaying_history_ = true;
    const bool ok = history_.undo();
    replaying_history_ = false;
    if (!ok) return reject_("undo failed");
    edit_snapshot_.reset();
    return accept_mutation_();
}

EditorReceipt EditorAuthority::redo() noexcept {
    if (!history_.can_redo()) return reject_("nothing to redo");
    replaying_history_ = true;
    const bool ok = history_.redo();
    replaying_history_ = false;
    if (!ok) return reject_("redo failed");
    edit_snapshot_.reset();
    return accept_mutation_();
}

bool EditorAuthority::can_undo() const noexcept { return history_.can_undo(); }
bool EditorAuthority::can_redo() const noexcept { return history_.can_redo(); }

std::size_t EditorAuthority::undo_depth() const noexcept {
    return static_cast<std::size_t>(history_.undo_count());
}

std::size_t EditorAuthority::redo_depth() const noexcept {
    return static_cast<std::size_t>(history_.redo_count());
}

void EditorAuthority::clear_history() noexcept {
    history_.clear();
    gesture_base_.reset();
    gesture_depth_ = 0;
}

EditorReceipt EditorAuthority::apply_morph(
    float amount, std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    if (!std::isfinite(amount)) return reject_("morph amount must be finite");
    if (!processor_.snapshots().has(SnapshotBank::Slot::A)
        && !processor_.snapshots().has(SnapshotBank::Slot::B)) {
        return reject_("both snapshot slots are empty");
    }
    const auto before = capture_state_();
    processor_.apply_morph_to_live(std::clamp(amount, 0.0f, 1.0f));
    edit_snapshot_.reset();
    // A morph DRAG brackets itself through begin/end_undo_gesture, so this
    // records only a morph applied as a discrete command.
    record_history_("Morph", before);
    return accept_mutation_();
}

void EditorAuthority::reset_transient_state() noexcept {
    edit_snapshot_.reset();
    processor_.end_param_gesture_epoch();
    // An open gesture is transient state and dies with the realm. The
    // HISTORY is not: it survives an editor close/reopen, which is what lets
    // a user close the window, reopen it, and still undo their last drag.
    // Dropping the base without recording is right here precisely because
    // there is no longer an editor to have finished the gesture.
    gesture_base_.reset();
    gesture_depth_ = 0;
}

} // namespace spectr
