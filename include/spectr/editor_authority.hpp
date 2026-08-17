#pragma once

#include "spectr/edit_engine.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/viewport.hpp"

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

    [[nodiscard]] EditorRevision revision() const noexcept { return revision_; }

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
    [[nodiscard]] EditorReceipt apply_morph(
        float amount,
        std::optional<EditorRevision> expected = std::nullopt) noexcept;

    /// A committed realm handshake or editor close invalidates only transient
    /// gesture state. Authoritative state and the monotonic revision survive.
    void reset_transient_state() noexcept;

private:
    [[nodiscard]] EditorReceipt reject_(std::string error) const;
    [[nodiscard]] EditorReceipt accept_without_mutation_() const;
    [[nodiscard]] EditorReceipt accept_mutation_() noexcept;
    [[nodiscard]] bool matches_(std::optional<EditorRevision> expected) const noexcept;

    Spectr& processor_;
    std::optional<BandSnapshot> edit_snapshot_;
    EditorRevision revision_ = 0;
};

} // namespace spectr
