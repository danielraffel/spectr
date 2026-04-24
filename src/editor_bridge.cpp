#include "spectr/editor_bridge.hpp"

#include "spectr/host_bridge.hpp"
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

// The bridge's wiring sits on top of the generic `spectr::HostBridge`
// framework (include/spectr/host_bridge.hpp). This file is now
// Spectr-specific: it knows about BandSnapshot, EditMode, SnapshotBank,
// PatternLibrary, presets, and kMix/kMorph/etc. ParamIDs. The generic
// parts — envelope parsing, dispatch table, value coercion, response
// builders — all live in HostBridge and will move upstream to
// `pulp::view::EditorBridge` when pulp#709 lands.

namespace spectr {

namespace {

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

// Configure a HostBridge with all of Spectr's editor-side handlers.
// Pure registration — the plugin / library / state references are
// captured by lambdas. Called once per dispatch in the current
// function-style entry points; when EditorView owns a persistent
// HostBridge this function is called once at setup.
void register_spectr_handlers_(HostBridge& bridge,
                               Spectr& plugin,
                               PatternLibrary* library,
                               EditorBridgeState& state)
{
    // paint_start — capture BandSnapshot for subsequent paint messages.
    bridge.add_handler("paint_start",
        [&state, &plugin](const choc::value::ValueView&) {
            state.drag_snap = BandSnapshot::capture(plugin.field());
            return HostBridge::ok_response();
        });

    // paint — dispatch edit against the held snapshot.
    bridge.add_handler("paint",
        [&state, &plugin](const choc::value::ValueView& p) -> std::string {
            if (!state.drag_snap) return HostBridge::err_response("paint without paint_start");
            const auto mode = parse_edit_mode_(HostBridge::get_string(p, "mode"));
            if (!mode) return HostBridge::err_response("unknown edit mode");

            DragGesture drag;
            drag.start_band    = HostBridge::get_uint (p, "start_band",   0);
            drag.start_value   = HostBridge::get_float(p, "start_value",  0.0f);
            drag.current_band  = HostBridge::get_uint (p, "current_band", drag.start_band);
            drag.current_value = HostBridge::get_float(p, "current_value", drag.start_value);
            drag.n_visible     = HostBridge::get_uint (p, "n_visible",    32);

            dispatch_edit(*mode, plugin.field(), drag, *state.drag_snap);
            return HostBridge::ok_response();
        });

    // paint_end — drop the snapshot.
    bridge.add_handler("paint_end",
        [&state](const choc::value::ValueView&) {
            state.drag_snap.reset();
            return HostBridge::ok_response();
        });

    // morph — apply A/B morph to live field.
    bridge.add_handler("morph",
        [&plugin](const choc::value::ValueView& p) {
            const auto t = std::clamp(HostBridge::get_float(p, "t", 0.0f), 0.0f, 1.0f);
            plugin.apply_morph_to_live(t);
            return HostBridge::ok_response();
        });

    // capture_snapshot — copy live field into slot A or B.
    bridge.add_handler("capture_snapshot",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            const auto slot = parse_slot_(HostBridge::get_string(p, "slot"));
            if (!slot) return HostBridge::err_response("slot must be 'A' or 'B'");
            plugin.capture_snapshot(*slot);
            return HostBridge::ok_response();
        });

    // ab_toggle — flip the active slot.
    bridge.add_handler("ab_toggle",
        [&plugin](const choc::value::ValueView&) {
            auto& b = plugin.snapshots();
            b.active = (b.active == SnapshotBank::Slot::A) ? SnapshotBank::Slot::B
                                                           : SnapshotBank::Slot::A;
            return HostBridge::ok_response();
        });

    // load_pattern — apply a library pattern to the live field.
    bridge.add_handler("load_pattern",
        [library, &plugin](const choc::value::ValueView& p) -> std::string {
            if (!library) return HostBridge::err_response("no pattern library attached");
            const auto id = HostBridge::get_string(p, "id");
            if (id.empty()) return HostBridge::err_response("pattern id missing");
            const auto* pat = library->find(id);
            if (!pat) return HostBridge::err_response("unknown pattern id");
            pat->apply_to(plugin.field());
            return HostBridge::ok_response();
        });

    // save_preset — serialize current state + metadata; return the
    // JSON blob so JS can write it to disk.
    bridge.add_handler("save_preset",
        [&plugin](const choc::value::ValueView& p) {
            PresetMetadata meta;
            meta.name        = HostBridge::get_string(p, "name");
            meta.author      = HostBridge::get_string(p, "author");
            meta.description = HostBridge::get_string(p, "description");
            meta.created_at  = HostBridge::get_string(p, "created_at");
            meta.modified_at = HostBridge::get_string(p, "modified_at");

            auto extras = choc::value::createObject("SavePresetExtras");
            extras.addMember("preset_json", save_preset_to_string(plugin, meta));
            return HostBridge::ok_response(extras);
        });

    // load_preset — parse JSON and apply state; echo metadata.
    bridge.add_handler("load_preset",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            const auto preset_json = HostBridge::get_string(p, "preset_json");
            if (preset_json.empty()) return HostBridge::err_response("preset_json missing");

            const auto result = load_preset_from_string(plugin, preset_json);
            if (!result) return HostBridge::err_response(describe(result.error));

            auto extras = choc::value::createObject("LoadPresetExtras");
            extras.addMember("name",           result.metadata.name);
            extras.addMember("author",         result.metadata.author);
            extras.addMember("description",    result.metadata.description);
            extras.addMember("created_at",     result.metadata.created_at);
            extras.addMember("modified_at",    result.metadata.modified_at);
            extras.addMember("plugin_version", result.plugin_version);
            return HostBridge::ok_response(extras);
        });

    // param_set — write through StateStore so undo/snapshot capture see it.
    bridge.add_handler("param_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("id"))
                return HostBridge::err_response("param id missing");
            const auto id_v = p["id"];
            pulp::state::ParamID id{};
            if      (id_v.isInt32()) id = static_cast<pulp::state::ParamID>(id_v.getInt32());
            else if (id_v.isInt64()) id = static_cast<pulp::state::ParamID>(id_v.getInt64());
            else                     return HostBridge::err_response("param id must be integer");

            if (!p.hasObjectMember("value"))
                return HostBridge::err_response("param value missing");
            const float value = HostBridge::get_float(p, "value", 0.0f);
            plugin.state().set_value(id, value);
            return HostBridge::ok_response();
        });
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────
//
// Both entry points build and configure a fresh HostBridge on each
// call. Cheap (just populating an unordered_map of closures) and
// matches the existing function-style API without forcing callers to
// manage a persistent bridge. When EditorView switches to owning a
// long-lived HostBridge (follow-up slice), these wrappers can route
// through that shared instance instead.

std::string dispatch_editor_message(Spectr& plugin,
                                    PatternLibrary* library,
                                    EditorBridgeState& state,
                                    std::string_view type,
                                    const choc::value::ValueView& payload) noexcept
{
    HostBridge bridge;
    register_spectr_handlers_(bridge, plugin, library, state);

    // Rebuild the envelope HostBridge expects so the dispatch path
    // stays uniform — both entry points funnel through the same
    // `dispatch_json` underneath.
    auto envelope = choc::value::createObject("Envelope");
    envelope.addMember("type", std::string(type));
    if (payload.isObject() || payload.isArray())
        envelope.addMember("payload", payload);
    const auto envelope_json = choc::json::toString(envelope, /*useLineBreaks=*/false);
    return bridge.dispatch_json(envelope_json);
}

std::string dispatch_editor_message_json(Spectr& plugin,
                                         PatternLibrary* library,
                                         EditorBridgeState& state,
                                         std::string_view json) noexcept
{
    HostBridge bridge;
    register_spectr_handlers_(bridge, plugin, library, state);
    return bridge.dispatch_json(json);
}

} // namespace spectr
