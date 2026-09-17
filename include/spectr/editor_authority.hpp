#pragma once

#include "spectr/edit_engine.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/viewport.hpp"

#include <pulp/state/undo_manager.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace spectr {

class Spectr;

using EditorRevision = std::uint64_t;
inline constexpr EditorRevision kMaxEditorRevision =
    static_cast<EditorRevision>(std::numeric_limits<std::int64_t>::max());

struct EditorReceipt {
    bool accepted = false;
    EditorRevision revision = 0;
    std::string error;
};

/// Renderer-neutral authority for editor mutations.
///
/// WebView compatibility handlers and the native controller both call this
/// service. Sound/state mutation, optimistic-concurrency validation, gesture
/// lifetime, and revision advancement therefore have one owner that outlives
/// any particular editor realm or window.
class EditorAuthority final {
public:
    explicit EditorAuthority(Spectr& processor) noexcept;

    [[nodiscard]] EditorRevision revision() const noexcept {
        return revision_.load(std::memory_order_acquire);
    }

    /// Record a processor mutation that originated outside the editor (for
    /// example host automation playback). The next editor gesture carrying
    /// the previous revision will reject as stale instead of overwriting the
    /// host's newer value.
    [[nodiscard]] EditorRevision record_external_mutation() noexcept;

    [[nodiscard]] EditorReceipt replace_processing_state(
        const BandField& field, const Viewport& viewport, Layout layout,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;

    [[nodiscard]] EditorReceipt begin_band_edit(
        std::optional<EditorRevision> expected = std::nullopt) noexcept;
    [[nodiscard]] EditorReceipt update_band_edit(
        EditMode mode, const DragGesture& gesture,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;
    [[nodiscard]] EditorReceipt end_band_edit() noexcept;
    [[nodiscard]] EditorReceipt cancel_band_edit() noexcept;

    [[nodiscard]] EditorReceipt capture_snapshot(
        SnapshotBank::Slot slot,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;
    [[nodiscard]] EditorReceipt recall_snapshot(
        SnapshotBank::Slot slot,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;
    /// Empty a slot. Unlike `recall_snapshot` this does NOT reject an empty
    /// slot: clearing is idempotent, so a caller clearing both slots does not
    /// have to ask first which of them were filled.
    [[nodiscard]] EditorReceipt clear_snapshot(
        SnapshotBank::Slot slot,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;
    [[nodiscard]] EditorReceipt apply_morph(
        float amount,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;

    // ── Undo / redo ────────────────────────────────────────────────────
    //
    // An undo entry is a whole `FieldSnapshot` — the 64-slot BandField plus
    // viewport and layout — never a per-band or per-parameter delta. That is
    // deliberate and it is what makes gain and mute restore COHERENTLY: a
    // band's level and its muted flag are two fields of one `BandState`, and
    // `snapshot.hpp` already states the governing rule that a snapshot
    // captures everything. An undo that restored a level without the mute it
    // was authored under (or the reverse) would be worse than no undo, and
    // this shape makes that failure unrepresentable rather than merely
    // untested.
    //
    // GRANULARITY is owned by the gesture bracket below, not by the mutation
    // count. The materialized editor republishes the COMPLETE processing
    // state on every pointer sample (`processing_state_set`), so one drag
    // across 32 bands arrives here as dozens of `replace_processing_state`
    // calls. Recording each would make that drag dozens of undo steps, which
    // is precisely the bulk-edit failure this exists to avoid. While a
    // gesture is open nothing is recorded; closing it records ONE entry
    // spanning the whole drag. That also bounds memory to one snapshot per
    // gesture rather than one per pointer sample.
    //
    // The stack itself is `pulp::state::UndoManager` rather than a hand-rolled
    // one: it already owns depth limiting, redo invalidation on a new edit,
    // and named entries. Spectr supplies only the snapshot semantics.

    /// Open an undo gesture. Every mutation until the matching
    /// `end_undo_gesture()` folds into one history entry named `name`.
    /// Re-entrant: nested opens are counted, and only the outermost close
    /// records. An already-open gesture is never restarted, so a stray
    /// second open cannot silently discard the drag's true starting point.
    void begin_undo_gesture(std::string name) noexcept;

    /// Close the gesture opened above and record one entry — but only if the
    /// state actually moved. A press that selects without editing, or a drag
    /// that returns to where it started, records nothing rather than pushing
    /// a no-op step the user has to press undo twice to get past.
    void end_undo_gesture() noexcept;

    [[nodiscard]] EditorReceipt undo() noexcept;
    [[nodiscard]] EditorReceipt redo() noexcept;
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] std::size_t undo_depth() const noexcept;
    [[nodiscard]] std::size_t redo_depth() const noexcept;
    /// Drop the whole history. Used when authoritative state arrives from
    /// somewhere the history cannot describe — host state restore, a preset
    /// load — because an entry captured before that arrival would undo TO a
    /// state the user never edited from.
    void clear_history() noexcept;

    /// A committed realm handshake or editor close invalidates only transient
    /// gesture state. Authoritative state and the monotonic revision survive.
    void reset_transient_state() noexcept;

private:
    [[nodiscard]] EditorReceipt reject_(std::string error) const;
    [[nodiscard]] EditorReceipt accept_without_mutation_() const;
    [[nodiscard]] EditorReceipt accept_mutation_() noexcept;
    [[nodiscard]] bool matches_(std::optional<EditorRevision> expected) const noexcept;

    /// The complete undoable state as it stands right now.
    [[nodiscard]] FieldSnapshot capture_state_() const noexcept;
    /// Write a captured state back through the processor. Returns false when
    /// the processor rejects it, so a failed restore reports rather than
    /// leaving the stacks describing a state that was never applied.
    [[nodiscard]] bool restore_state_(const FieldSnapshot& state) noexcept;
    /// Record one entry for a mutation that has ALREADY been applied, unless
    /// a gesture is open (the gesture records instead) or we are ourselves
    /// replaying history (an undo must not push its own inverse as a new
    /// edit — that is the loop where undo and redo become the same button).
    void record_history_(std::string name, const FieldSnapshot& before) noexcept;

    Spectr& processor_;
    std::optional<BandSnapshot> edit_snapshot_;
    std::atomic<EditorRevision> revision_{0};

    pulp::state::UndoManager history_;
    std::optional<FieldSnapshot> gesture_base_;
    std::string gesture_name_;
    int gesture_depth_ = 0;
    bool replaying_history_ = false;
};

} // namespace spectr
