#include "spectr/editor_bridge.hpp"

#include "spectr/spectr.hpp"
#include "spectr/edit_engine.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/pattern.hpp"
#include "spectr/snapshot.hpp"

#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace spectr {

namespace {

// ── Response helpers ───────────────────────────────────────────────────

// Both success and failure go through choc::json::toString so callers
// see a single, consistent envelope shape — choc formats as
// `{"ok": true}` / `{"ok": false, "error": "…"}` (note the space after
// the colon; choc always inserts one even in no-linebreak mode).
std::string ok_response() {
    auto obj = choc::value::createObject("BridgeOk");
    obj.addMember("ok", true);
    return choc::json::toString(obj, /*useLineBreaks=*/false);
}

std::string err_response(std::string_view msg) {
    auto obj = choc::value::createObject("BridgeError");
    obj.addMember("ok",    false);
    obj.addMember("error", std::string(msg));
    return choc::json::toString(obj, /*useLineBreaks=*/false);
}

// ── Value coercion helpers ─────────────────────────────────────────────
//
// choc's ValueView throws if you call the wrong getter; these never
// throw and map missing/wrong-type to defaults. Each type's handler
// defends against a malformed payload without crashing the bridge.

float get_float_(const choc::value::ValueView& v, const char* key, float dflt) {
    if (!v.isObject() || !v.hasObjectMember(key)) return dflt;
    const auto e = v[key];
    if (e.isFloat64()) return static_cast<float>(e.getFloat64());
    if (e.isInt64())   return static_cast<float>(e.getInt64());
    if (e.isInt32())   return static_cast<float>(e.getInt32());
    return dflt;
}

std::size_t get_uint_(const choc::value::ValueView& v, const char* key, std::size_t dflt) {
    if (!v.isObject() || !v.hasObjectMember(key)) return dflt;
    const auto e = v[key];
    if (e.isInt32())   { const auto x = e.getInt32(); return x < 0 ? 0 : static_cast<std::size_t>(x); }
    if (e.isInt64())   { const auto x = e.getInt64(); return x < 0 ? 0 : static_cast<std::size_t>(x); }
    if (e.isFloat64()) { const auto x = e.getFloat64(); return x < 0 ? 0 : static_cast<std::size_t>(x); }
    return dflt;
}

std::string get_string_(const choc::value::ValueView& v, const char* key) {
    if (!v.isObject() || !v.hasObjectMember(key)) return {};
    const auto e = v[key];
    if (!e.isString()) return {};
    return std::string(e.getString());
}

// Parse an EditMode label. Returns nullopt on unknown.
std::optional<EditMode> parse_edit_mode_(std::string_view s) {
    if (s == "Sculpt") return EditMode::Sculpt;
    if (s == "Level")  return EditMode::Level;
    if (s == "Boost")  return EditMode::Boost;
    if (s == "Flare")  return EditMode::Flare;
    if (s == "Glide")  return EditMode::Glide;
    return std::nullopt;
}

// Parse a snapshot slot label.
std::optional<SnapshotBank::Slot> parse_slot_(std::string_view s) {
    if (s == "A") return SnapshotBank::Slot::A;
    if (s == "B") return SnapshotBank::Slot::B;
    return std::nullopt;
}

// ── Handlers ───────────────────────────────────────────────────────────

std::string on_paint_start_(Spectr& plugin, EditorBridgeState& state,
                            const choc::value::ValueView& /*payload*/) {
    state.drag_snap = BandSnapshot::capture(plugin.field());
    return ok_response();
}

std::string on_paint_(Spectr& plugin, EditorBridgeState& state,
                      const choc::value::ValueView& payload) {
    if (!state.drag_snap) {
        return err_response("paint without paint_start");
    }
    const auto mode_str = get_string_(payload, "mode");
    const auto mode = parse_edit_mode_(mode_str);
    if (!mode) return err_response("unknown edit mode");

    DragGesture drag;
    drag.start_band    = get_uint_(payload, "start_band",   0);
    drag.start_value   = get_float_(payload, "start_value", 0.0f);
    drag.current_band  = get_uint_(payload, "current_band", drag.start_band);
    drag.current_value = get_float_(payload, "current_value", drag.start_value);
    drag.n_visible     = get_uint_(payload, "n_visible",    32);

    dispatch_edit(*mode, plugin.field(), drag, *state.drag_snap);
    return ok_response();
}

std::string on_paint_end_(Spectr& /*plugin*/, EditorBridgeState& state,
                          const choc::value::ValueView& /*payload*/) {
    state.drag_snap.reset();
    return ok_response();
}

std::string on_morph_(Spectr& plugin, EditorBridgeState& /*state*/,
                      const choc::value::ValueView& payload) {
    const auto t = std::clamp(get_float_(payload, "t", 0.0f), 0.0f, 1.0f);
    plugin.apply_morph_to_live(t);
    return ok_response();
}

std::string on_capture_snapshot_(Spectr& plugin, EditorBridgeState& /*state*/,
                                 const choc::value::ValueView& payload) {
    const auto slot = parse_slot_(get_string_(payload, "slot"));
    if (!slot) return err_response("slot must be 'A' or 'B'");
    plugin.capture_snapshot(*slot);
    return ok_response();
}

std::string on_ab_toggle_(Spectr& plugin, EditorBridgeState& /*state*/,
                          const choc::value::ValueView& /*payload*/) {
    auto& b = plugin.snapshots();
    b.active = (b.active == SnapshotBank::Slot::A) ? SnapshotBank::Slot::B
                                                   : SnapshotBank::Slot::A;
    return ok_response();
}

std::string on_load_pattern_(Spectr& plugin, PatternLibrary* library,
                             const choc::value::ValueView& payload) {
    if (!library) return err_response("no pattern library attached");
    const auto id = get_string_(payload, "id");
    if (id.empty()) return err_response("pattern id missing");
    const auto* p = library->find(id);
    if (!p) return err_response("unknown pattern id");
    p->apply_to(plugin.field());
    return ok_response();
}

} // namespace

// ── Dispatch ───────────────────────────────────────────────────────────

std::string dispatch_editor_message(Spectr& plugin,
                                    PatternLibrary* library,
                                    EditorBridgeState& state,
                                    std::string_view type,
                                    const choc::value::ValueView& payload) noexcept
{
    try {
        if (type == "paint_start")       return on_paint_start_(plugin, state, payload);
        if (type == "paint")             return on_paint_(plugin, state, payload);
        if (type == "paint_end")         return on_paint_end_(plugin, state, payload);
        if (type == "morph")             return on_morph_(plugin, state, payload);
        if (type == "capture_snapshot")  return on_capture_snapshot_(plugin, state, payload);
        if (type == "ab_toggle")         return on_ab_toggle_(plugin, state, payload);
        if (type == "load_pattern")      return on_load_pattern_(plugin, library, payload);
        // Reserved-for-future types return a distinguishable error so a
        // JS caller sending these during development gets a clear
        // signal rather than a silent drop.
        if (type == "save_preset" ||
            type == "load_preset" ||
            type == "param_set")         return err_response("not implemented in this slice");
        return err_response("unknown message type");
    } catch (...) {
        return err_response("internal error");
    }
}

std::string dispatch_editor_message_json(Spectr& plugin,
                                         PatternLibrary* library,
                                         EditorBridgeState& state,
                                         std::string_view json) noexcept
{
    choc::value::Value root;
    try {
        root = choc::json::parse(json);
    } catch (...) {
        return err_response("malformed JSON");
    }
    if (!root.isObject()) return err_response("envelope must be an object");

    const auto type = get_string_(root, "type");
    if (type.empty()) return err_response("envelope missing 'type'");

    // Payload is optional; handlers defend against missing fields.
    if (!root.hasObjectMember("payload")) {
        // Pass an empty object as the payload.
        auto empty = choc::value::createObject("Empty");
        return dispatch_editor_message(plugin, library, state, type, empty);
    }
    return dispatch_editor_message(plugin, library, state, type, root["payload"]);
}

} // namespace spectr
