#pragma once

// Milestone 9.5 — JS ↔ C++ editor bridge (foundation slice).
//
// Routes JSON messages from the editor WebView into Spectr's C++ state
// — paint gestures, edit-mode selection, snapshot capture, morph
// slider, pattern library picks, preset save/load, flat-parameter
// writes. Lives behind a stateless-per-call dispatch function so
// tests can exercise every message path without touching a WebView.
//
// The schema documented in this header is the canonical contract
// between whatever JS shim sits inside the editor HTML and the C++
// processor. Any new message type lands here before it lands in the
// HTML side.
//
//
// ── Envelope ───────────────────────────────────────────────────────────
//
// Every bridge message is a JSON object of this shape:
//
//   {
//     "type": "<kind>",
//     "payload": { ... }     // optional, shape per type
//   }
//
// Responses are JSON objects of this shape:
//
//   { "ok": true }                          // on success
//   { "ok": false, "error": "…" }           // on failure
//
// The bridge never throws. Malformed JSON, unknown `type`, or a
// payload that violates the per-type contract all yield the second
// response shape with an error string describing what went wrong.
//
// ── Drag protocol (paint_start / paint / paint_end) ─────────────────────
//
// Spectr's edit modes are gesture-based: some modes (Boost / Flare /
// Glide) need a stable snapshot taken at drag start so that
// successive paint updates don't drift. The bridge enforces that by
// tracking drag state across messages:
//
//   paint_start   → capture a BandSnapshot from Spectr.field()
//   paint         → dispatch_edit with the captured snapshot
//   paint_end     → drop the snapshot
//
// A paint message without a preceding paint_start is rejected.
// A second paint_start without an intervening paint_end replaces the
// held snapshot (treated as a new drag).
//
// ── Message types ─────────────────────────────────────────────────────
//
//  type="paint_start"
//    payload: {} — no fields required.
//    Effect: capture BandSnapshot of the current field.
//
//  type="paint"
//    payload: {
//      "mode":          "Sculpt" | "Level" | "Boost" | "Flare" | "Glide",
//      "start_band":    uint,     // canonical band index of drag origin
//      "start_value":   float,    // dB, [-60, +12]
//      "current_band":  uint,
//      "current_value": float,
//      "n_visible":     uint      // visible layout band count
//    }
//    Effect: dispatch_edit(mode, field, drag, snapshot).
//
//  type="paint_end"
//    payload: {} — no fields.
//    Effect: clear the held snapshot.
//
//  type="morph"
//    payload: { "t": float }      // [0, 1], clamped
//    Effect: Spectr::apply_morph_to_live(t).
//
//  type="capture_snapshot"
//    payload: { "slot": "A" | "B" }
//    Effect: Spectr::capture_snapshot(slot).
//
//  type="ab_toggle"
//    payload: {} — no fields.
//    Effect: flip snapshots().active between A and B.
//
//  type="load_pattern"
//    payload: { "id": "<pattern_id>" }
//    Effect: look up in PatternLibrary and apply to Spectr.field().
//    The library is the plugin's current library; id matches
//    Pattern::id.
//    (Payload future-compat: optional "library" field for non-default
//    libraries; ignored in this slice.)
//
//  type="save_preset"
//    payload: {
//      "name":        string,        // optional; all metadata optional
//      "author":      string,
//      "description": string,
//      "created_at":  string,        // ISO-8601
//      "modified_at": string
//    }
//    Effect: build a preset JSON string from the current processor
//    state + the provided metadata and return it in the response:
//      {"ok": true, "preset_json": "<serialized>"}
//    The bridge does not touch the filesystem — JS is responsible
//    for prompting the user and writing the blob wherever the host
//    OS allows. Mirrors how the prototype's pattern-manager modal
//    handles export.
//
//  type="load_preset"
//    payload: { "preset_json": "<serialized>" }
//    Effect: parse the JSON and apply to StateStore + plugin state.
//    Response includes the metadata from the preset on success:
//      {"ok": true, "name": "…", "author": "…", … }
//    On schema mismatch returns {"ok": false, "error": "…"} with
//    a message describing the mismatch (see PresetLoadError).
//
//  type="param_set"
//    payload: { "id": int, "value": float }
//    Effect: StateStore::set_value(id, value). No range checking —
//    the StateStore enforces the parameter's declared range.
//    Hosts automate through their own channels; this is for
//    editor-driven changes that should loop through the StateStore
//    (so undo groups + snapshot capture see the write).
//
// ── Not yet in this slice ─────────────────────────────────────────────
//
//  state_push (C++ → JS, for automation-to-UI sync). Requires
//  panel_->execute_script() wiring that doesn't fight the prototype's
//  render loop. Planned for M9.5 slice 4.

#include <choc/containers/choc_Value.h>

#include <optional>
#include <string>
#include <string_view>

#include "spectr/edit_engine.hpp"  // BandSnapshot — full type needed for optional

namespace spectr {

class Spectr;
class PatternLibrary;

/// State carried across bridge messages within a single editor session.
/// A WebView owns one; `dispatch_editor_message` reads and mutates
/// it under the covers.
struct EditorBridgeState {
    /// Snapshot captured at paint_start. Present while a drag is live.
    std::optional<BandSnapshot> drag_snap;
};

/// Dispatch one already-parsed bridge message. The `type` and
/// `payload` come from the envelope; callers that have a JSON string
/// should use `dispatch_editor_message_json` below.
std::string dispatch_editor_message(Spectr& plugin,
                                    PatternLibrary* library,
                                    EditorBridgeState& state,
                                    std::string_view type,
                                    const choc::value::ValueView& payload) noexcept;

/// Parse a JSON envelope and dispatch. Returns the same
/// `{"ok":true|false,"error":"…"}` response as the parsed variant.
std::string dispatch_editor_message_json(Spectr& plugin,
                                         PatternLibrary* library,
                                         EditorBridgeState& state,
                                         std::string_view json) noexcept;

} // namespace spectr
