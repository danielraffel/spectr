#include "spectr/editor_authority.hpp"

#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spectr {

EditorAuthority::EditorAuthority(Spectr& processor) noexcept
    : processor_(processor) {}

EditorReceipt EditorAuthority::reject_(std::string error) const {
    return {false, revision_, std::move(error)};
}

EditorReceipt EditorAuthority::accept_without_mutation_() const {
    return {true, revision_, {}};
}

EditorReceipt EditorAuthority::accept_mutation_() noexcept {
    // Hydration transports the revision as a signed JSON integer. Keep the
    // authority inside that exactly representable domain for its full life.
    if (revision_ != kMaxEditorRevision) ++revision_;
    return {true, revision_, {}};
}

bool EditorAuthority::matches_(
    std::optional<EditorRevision> expected) const noexcept {
    return !expected || *expected == revision_;
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
    if (!processor_.replace_processing_state(field, viewport, layout))
        return reject_("invalid processing state");
    edit_snapshot_.reset();
    return accept_mutation_();
}

EditorReceipt EditorAuthority::begin_band_edit(
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) return reject_("stale editor revision");
    edit_snapshot_ = BandSnapshot::capture(processor_.field());
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::update_band_edit(
    EditMode mode, const DragGesture& gesture,
    std::optional<EditorRevision> expected) noexcept {
    if (!matches_(expected)) {
        edit_snapshot_.reset();
        return reject_("stale editor revision");
    }
    if (!edit_snapshot_) return reject_("paint without paint_start");
    const auto visible = visible_count(processor_.layout());
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
    auto next = processor_.field();
    dispatch_edit(mode, next, gesture, *edit_snapshot_);
    if (!processor_.replace_processing_state(
            next, processor_.viewport(), processor_.layout())) {
        return reject_("paint produced invalid processing state");
    }
    return accept_mutation_();
}

EditorReceipt EditorAuthority::end_band_edit() noexcept {
    edit_snapshot_.reset();
    return accept_without_mutation_();
}

EditorReceipt EditorAuthority::cancel_band_edit() noexcept {
    edit_snapshot_.reset();
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
}

} // namespace spectr
