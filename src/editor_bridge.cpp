#include "spectr/editor_bridge.hpp"

#include "spectr/spectr.hpp"
#include "spectr/edit_engine.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/pattern.hpp"
#include "spectr/preset_format.hpp"
#include "spectr/snapshot.hpp"

#include <pulp/state/store.hpp>

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

// ── Preset handlers ────────────────────────────────────────────────────

std::string on_save_preset_(Spectr& plugin, const choc::value::ValueView& payload) {
    PresetMetadata meta;
    meta.name        = get_string_(payload, "name");
    meta.author      = get_string_(payload, "author");
    meta.description = get_string_(payload, "description");
    meta.created_at  = get_string_(payload, "created_at");
    meta.modified_at = get_string_(payload, "modified_at");
    const auto preset_json = save_preset_to_string(plugin, meta);

    auto obj = choc::value::createObject("BridgeOk");
    obj.addMember("ok",          true);
    obj.addMember("preset_json", preset_json);
    return choc::json::toString(obj, /*useLineBreaks=*/false);
}

std::string on_load_preset_(Spectr& plugin, const choc::value::ValueView& payload) {
    const auto preset_json = get_string_(payload, "preset_json");
    if (preset_json.empty()) return err_response("preset_json missing");

    const auto result = load_preset_from_string(plugin, preset_json);
    if (!result) {
        return err_response(describe(result.error));
    }
    // Success — echo the metadata the preset carried so JS can refresh
    // whatever UI labels its preset browser, without needing to
    // re-parse the full envelope on its side.
    auto obj = choc::value::createObject("BridgeOk");
    obj.addMember("ok",             true);
    obj.addMember("name",           result.metadata.name);
    obj.addMember("author",         result.metadata.author);
    obj.addMember("description",    result.metadata.description);
    obj.addMember("created_at",     result.metadata.created_at);
    obj.addMember("modified_at",    result.metadata.modified_at);
    obj.addMember("plugin_version", result.plugin_version);
    return choc::json::toString(obj, /*useLineBreaks=*/false);
}

std::string on_param_set_(Spectr& plugin, const choc::value::ValueView& payload) {
    if (!payload.isObject() || !payload.hasObjectMember("id"))
        return err_response("param id missing");
    const auto id_v = payload["id"];
    pulp::state::ParamID id{};
    if      (id_v.isInt32()) id = static_cast<pulp::state::ParamID>(id_v.getInt32());
    else if (id_v.isInt64()) id = static_cast<pulp::state::ParamID>(id_v.getInt64());
    else                     return err_response("param id must be integer");

    if (!payload.hasObjectMember("value"))
        return err_response("param value missing");
    const float value = get_float_(payload, "value", 0.0f);

    // StateStore::set_value returns a bool in some versions; in ours
    // the write is unconditional and the store range-clamps as needed.
    plugin.state().set_value(id, value);
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
        if (type == "save_preset")       return on_save_preset_(plugin, payload);
        if (type == "load_preset")       return on_load_preset_(plugin, payload);
        if (type == "param_set")         return on_param_set_(plugin, payload);
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
