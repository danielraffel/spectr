#pragma once

// Milestone 9.5 — JS ↔ C++ editor bridge (Spectr-side handler map).
//
// The generic dispatcher, envelope parsing, response builders, and
// value-coercion helpers all live upstream in pulp#711 as
// `pulp::view::EditorBridge` (Pulp v0.41.0+). This header + its .cpp
// just declare the Spectr-specific handler-registration surface and
// documentation of the message schema we target. When EditorView is
// constructed, it builds a `pulp::view::EditorBridge`, calls
// `register_spectr_editor_handlers()` from below to populate it, and
// attaches it to its WebViewPanel.
//
// ── Envelope ───────────────────────────────────────────────────────────
//
// Inbound: `{"type": "<kind>", "payload": {…}}` (payload optional).
// Outbound: `{"ok": true|false, "error": "…"|<extras>}`.
// Envelope-level error strings ("malformed JSON", "unknown message
// type", "envelope missing 'type'", "internal error") are emitted by
// the framework. Plugin-specific errors use `err_response("…")`.
//
// ── Drag protocol (paint_start / paint / paint_end) ────────────────────
//
//   paint_start   → capture a BandSnapshot from Spectr.field() into
//                   the shared `EditorDragState` (held by EditorView)
//   paint         → dispatch_edit with that captured snapshot
//   paint_end     → drop the snapshot
//
// A `paint` without a preceding `paint_start` returns an error. A
// second `paint_start` without an intervening `paint_end` replaces
// the held snapshot (treated as a new drag).
//
// ── Message types ──────────────────────────────────────────────────────
//
//  type="paint_start"       — payload: {}
//                             effect: capture BandSnapshot
//  type="paint"             — payload: {mode, start_band, start_value,
//                                       current_band, current_value,
//                                       n_visible}
//                             effect: dispatch_edit with snapshot
//  type="paint_end"         — payload: {}
//                             effect: clear snapshot
//  type="morph"             — payload: {t: float}
//                             effect: Spectr::apply_morph_to_live(t)
//  type="capture_snapshot"  — payload: {slot: "A"|"B"}
//                             effect: Spectr::capture_snapshot(slot)
//  type="ab_toggle"         — payload: {}
//                             effect: flip snapshots().active
//  type="load_pattern"      — payload: {id: "<pattern_id>"}
//                             effect: apply pattern to field
//  type="save_preset"       — payload: {name, author, description,
//                                       created_at, modified_at}
//                             effect: return preset_json in response extras
//  type="load_preset"       — payload: {preset_json: "<serialized>"}
//                             effect: apply state; echo metadata in extras
//  type="param_set"         — payload: {id: int, value: float}
//                             effect: StateStore::set_value(id, value)

#include <pulp/view/editor_bridge.hpp>

#include <optional>

#include "spectr/edit_engine.hpp"  // BandSnapshot

namespace spectr {

class Spectr;
class PatternLibrary;

/// Cross-call state the drag handlers need. Owned by whoever owns the
/// bridge (typically EditorView) so handler closures can capture a
/// stable reference.
struct EditorDragState {
    std::optional<BandSnapshot> snap;
};

/// Register Spectr's 10 editor-bridge handlers on the given pulp
/// EditorBridge. All state references are captured by closures and
/// must outlive the bridge. Intended to be called once at EditorView
/// construction.
void register_spectr_editor_handlers(pulp::view::EditorBridge& bridge,
                                     Spectr& plugin,
                                     PatternLibrary& library,
                                     EditorDragState& drag);

} // namespace spectr
