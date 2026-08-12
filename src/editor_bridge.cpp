#include "spectr/editor_bridge.hpp"

#include "spectr/spectr.hpp"
#include "spectr/edit_engine.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/pattern.hpp"
#include "spectr/preset_format.hpp"
#include "spectr/snapshot.hpp"

#include <pulp/state/store.hpp>
#include <pulp/view/editor_bridge.hpp>

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

// Spectr-specific handler registrations. The generic envelope parse +
// dispatch + response builders live in pulp::view::EditorBridge (upstream
// pulp#711). This file only encodes Spectr's product semantics: edit-mode
// labels, snapshot slots, pattern library lookup, preset JSON handling,
// and the ParamID coercion for param_set.

namespace spectr {

namespace {

using pulp::view::EditorBridge;

std::optional<EditMode> parse_edit_mode_(std::string_view s) {
    if (s == "Sculpt") return EditMode::Sculpt;
    if (s == "Level")  return EditMode::Level;
    if (s == "Boost")  return EditMode::Boost;
    if (s == "Flare")  return EditMode::Flare;
    if (s == "Glide")  return EditMode::Glide;
    return std::nullopt;
}

std::optional<SnapshotBank::Slot> parse_slot_(std::string_view s) {
    if (s == "A") return SnapshotBank::Slot::A;
    if (s == "B") return SnapshotBank::Slot::B;
    return std::nullopt;
}

std::optional<Layout> parse_layout_(std::uint32_t n) {
    switch (n) {
        case 32: return Layout::Bands32;
        case 40: return Layout::Bands40;
        case 48: return Layout::Bands48;
        case 56: return Layout::Bands56;
        case 64: return Layout::Bands64;
        default: return std::nullopt;
    }
}

std::optional<float> finite_number_(const choc::value::ValueView& value) {
    double number = 0.0;
    if      (value.isFloat64()) number = value.getFloat64();
    else if (value.isInt64())   number = static_cast<double>(value.getInt64());
    else if (value.isInt32())   number = static_cast<double>(value.getInt32());
    else return std::nullopt;

    if (!std::isfinite(number)) return std::nullopt;
    return static_cast<float>(number);
}

choc::value::Value snapshot_projection_(const FieldSnapshot& snapshot,
                                        std::size_t visible) {
    auto result = choc::value::createObject("SpectrSnapshotProjection");
    result.addMember("populated", snapshot.populated);
    auto gains = choc::value::createEmptyArray();
    auto muted = choc::value::createEmptyArray();
    for (std::size_t i = 0; i < visible; ++i) {
        gains.addArrayElement(static_cast<double>(snapshot.field.bands[i].gain_db));
        muted.addArrayElement(snapshot.field.bands[i].muted);
    }
    result.addMember("gain_db", gains);
    result.addMember("muted", muted);
    return result;
}

choc::value::Value pattern_library_projection_(const PatternLibrary& library) {
    auto result = choc::value::createObject("SpectrPatternLibraryProjection");
    result.addMember("patterns_json", library.export_json());
    return result;
}

std::string authoritative_response_(const Spectr& plugin,
                                    std::uint32_t* revision,
                                    bool mutated,
                                    std::uint32_t fallback_revision = 0) {
    if (revision != nullptr && mutated
        && *revision != std::numeric_limits<std::uint32_t>::max()) {
        ++*revision;
    }
    return EditorBridge::ok_response(make_editor_state_payload(
        plugin, revision != nullptr ? *revision : fallback_revision));
}

} // namespace

choc::value::Value make_editor_state_payload(const Spectr& plugin,
                                             std::uint32_t revision) {
    const auto n = visible_count(plugin.layout());
    auto gains = choc::value::createEmptyArray();
    auto muted = choc::value::createEmptyArray();
    for (std::size_t i = 0; i < n; ++i) {
        gains.addArrayElement(static_cast<double>(plugin.field().bands[i].gain_db));
        muted.addArrayElement(plugin.field().bands[i].muted);
    }

    auto snapshots = choc::value::createObject("SpectrSnapshotState");
    snapshots.addMember("A", snapshot_projection_(plugin.snapshots().a, n));
    snapshots.addMember("B", snapshot_projection_(plugin.snapshots().b, n));

    auto payload = choc::value::createObject("SpectrEditorState");
    payload.addMember("revision", static_cast<std::int64_t>(revision));
    payload.addMember("n_visible", static_cast<std::int32_t>(n));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", muted);
    payload.addMember("min_hz", static_cast<double>(plugin.viewport().min_hz));
    payload.addMember("max_hz", static_cast<double>(plugin.viewport().max_hz));
    payload.addMember("snapshots", snapshots);
    payload.addMember("patterns_json", plugin.patterns().export_json());
    return payload;
}

void register_spectr_editor_handlers(EditorBridge& bridge,
                                     Spectr& plugin,
                                     PatternLibrary& library,
                                     EditorDragState& drag,
                                     std::uint32_t* authoritative_revision)
{
    bridge.add_handler("processing_state_get",
        [&plugin, &drag, authoritative_revision](const choc::value::ValueView&) {
            // Every committed realm requests state on mount. Treat that as a
            // new gesture epoch so a reload can never resume a C++ snapshot
            // captured by callbacks from the retired realm.
            drag.snap.reset();
            return authoritative_response_(plugin, authoritative_revision, false);
        });

    // Complete JS field publication. Gain and mute are deliberately separate:
    // JSON never transports -Infinity, while a muted band still reaches an
    // exact 0.0 multiplier in BandField::linear_gain().
    bridge.add_handler("band_field_set",
        [&plugin, authoritative_revision](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject()) return EditorBridge::err_response("payload must be object");

            const auto n_visible = EditorBridge::get_uint(p, "n_visible", 0);
            const auto layout = parse_layout_(n_visible);
            if (!layout) return EditorBridge::err_response("n_visible must be 32, 40, 48, 56, or 64");

            if (!p.hasObjectMember("gain_db") || !p["gain_db"].isArray())
                return EditorBridge::err_response("gain_db must be array");
            if (!p.hasObjectMember("muted") || !p["muted"].isArray())
                return EditorBridge::err_response("muted must be array");

            const auto gains = p["gain_db"];
            const auto mutes = p["muted"];
            if (gains.size() != n_visible || mutes.size() != n_visible)
                return EditorBridge::err_response("gain_db and muted lengths must equal n_visible");

            auto next = plugin.field();
            for (std::uint32_t i = 0; i < n_visible; ++i) {
                const auto gain = finite_number_(gains[i]);
                if (!gain) return EditorBridge::err_response("gain_db values must be finite numbers");
                if (*gain < kBandGainMinDb || *gain > kBandGainMaxDb)
                    return EditorBridge::err_response("gain_db values must be within -24 and +24 dB");
                if (!mutes[i].isBool())
                    return EditorBridge::err_response("muted values must be boolean");
                next.bands[i].gain_db = *gain;
                next.bands[i].muted = mutes[i].getBool();
            }

            if (!plugin.replace_processing_state(next, plugin.viewport(), *layout))
                return EditorBridge::err_response("invalid band field");
            if (authoritative_revision != nullptr)
                return authoritative_response_(plugin, authoritative_revision, true);
            return EditorBridge::ok_response();
        });

    // Atomic state publication for the imported live editor. Zoom/pan changes
    // are sound-defining in Spectr, so the viewport and the full band field
    // cross the bridge together and compile into one complete Pulp mask table.
    bridge.add_handler("processing_state_set",
        [&plugin, authoritative_revision](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject())
                return EditorBridge::err_response("payload must be object");

            const auto n_visible = EditorBridge::get_uint(p, "n_visible", 0);
            const auto layout = parse_layout_(n_visible);
            if (!layout)
                return EditorBridge::err_response(
                    "n_visible must be 32, 40, 48, 56, or 64");
            if (!p.hasObjectMember("gain_db") || !p["gain_db"].isArray())
                return EditorBridge::err_response("gain_db must be array");
            if (!p.hasObjectMember("muted") || !p["muted"].isArray())
                return EditorBridge::err_response("muted must be array");
            if (!p.hasObjectMember("min_hz") || !p.hasObjectMember("max_hz"))
                return EditorBridge::err_response("min_hz and max_hz are required");

            const auto gains = p["gain_db"];
            const auto mutes = p["muted"];
            if (gains.size() != n_visible || mutes.size() != n_visible)
                return EditorBridge::err_response(
                    "gain_db and muted lengths must equal n_visible");

            BandField next = plugin.field();
            for (std::uint32_t i = 0; i < n_visible; ++i) {
                const auto gain = finite_number_(gains[i]);
                if (!gain || *gain < kBandGainMinDb || *gain > kBandGainMaxDb)
                    return EditorBridge::err_response(
                        "gain_db values must be finite and within -24 and +24 dB");
                if (!mutes[i].isBool())
                    return EditorBridge::err_response("muted values must be boolean");
                next.bands[i].gain_db = *gain;
                next.bands[i].muted = mutes[i].getBool();
            }

            const auto min_hz = finite_number_(p["min_hz"]);
            const auto max_hz = finite_number_(p["max_hz"]);
            if (!min_hz || !max_hz)
                return EditorBridge::err_response(
                    "min_hz and max_hz must be finite numbers");
            const Viewport viewport{*min_hz, *max_hz};
            if (!plugin.replace_processing_state(next, viewport, *layout))
                return EditorBridge::err_response("invalid viewport");
            if (authoritative_revision != nullptr)
                return authoritative_response_(plugin, authoritative_revision, true);
            return EditorBridge::ok_response();
        });

    // ── Drag protocol ──────────────────────────────────────────────────

    bridge.add_handler("paint_start",
        [&drag, &plugin](const choc::value::ValueView&) {
            drag.snap = BandSnapshot::capture(plugin.field());
            return EditorBridge::ok_response();
        });

    bridge.add_handler("paint",
        [&drag, &plugin, authoritative_revision](const choc::value::ValueView& p) -> std::string {
            if (!drag.snap) return EditorBridge::err_response("paint without paint_start");
            if (!p.isObject()) return EditorBridge::err_response("payload must be object");
            const auto mode = parse_edit_mode_(EditorBridge::get_string(p, "mode"));
            if (!mode) return EditorBridge::err_response("unknown edit mode");

            const auto n_visible = EditorBridge::get_uint(p, "n_visible", 0);
            const auto start_band = EditorBridge::get_uint(p, "start_band", n_visible);
            const auto current_band = EditorBridge::get_uint(p, "current_band", n_visible);
            if (n_visible != visible_count(plugin.layout())
                || start_band >= n_visible || current_band >= n_visible) {
                return EditorBridge::err_response("paint geometry is outside the active layout");
            }
            if (!p.hasObjectMember("start_value") || !p.hasObjectMember("current_value"))
                return EditorBridge::err_response("paint values are required");
            const auto start_value = finite_number_(p["start_value"]);
            const auto current_value = finite_number_(p["current_value"]);
            if (!start_value || !current_value
                || *start_value < kBandGainMinDb || *start_value > kBandGainMaxDb
                || *current_value < kBandGainMinDb || *current_value > kBandGainMaxDb) {
                return EditorBridge::err_response(
                    "paint values must be finite and within -24 and +24 dB");
            }

            DragGesture g;
            g.start_band    = start_band;
            g.start_value   = *start_value;
            g.current_band  = current_band;
            g.current_value = *current_value;
            g.n_visible     = n_visible;

            dispatch_edit(*mode, plugin.field(), g, *drag.snap);
            plugin.publish_field();
            if (authoritative_revision != nullptr)
                return authoritative_response_(plugin, authoritative_revision, true);
            return EditorBridge::ok_response();
        });

    bridge.add_handler("paint_end",
        [&drag](const choc::value::ValueView&) {
            drag.snap.reset();
            return EditorBridge::ok_response();
        });

    // ── Morph / snapshot / A-B ─────────────────────────────────────────

    bridge.add_handler("morph",
        [&plugin](const choc::value::ValueView& p) {
            const auto t = std::clamp(EditorBridge::get_float(p, "t", 0.0f), 0.0f, 1.0f);
            const auto revision = EditorBridge::get_uint(p, "revision", 0);
            plugin.apply_morph_to_live(t);
            return EditorBridge::ok_response(
                make_editor_state_payload(plugin, revision));
        });

    bridge.add_handler("capture_snapshot",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            const auto slot = parse_slot_(EditorBridge::get_string(p, "slot"));
            if (!slot) return EditorBridge::err_response("slot must be 'A' or 'B'");
            const auto revision = EditorBridge::get_uint(p, "revision", 0);
            plugin.capture_snapshot(*slot);
            return EditorBridge::ok_response(
                make_editor_state_payload(plugin, revision));
        });

    bridge.add_handler("recall_snapshot",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            const auto slot = parse_slot_(EditorBridge::get_string(p, "slot"));
            if (!slot) return EditorBridge::err_response("slot must be 'A' or 'B'");
            const auto& snapshot = plugin.snapshots().get(*slot);
            if (!snapshot.populated)
                return EditorBridge::err_response("snapshot slot is empty");
            const auto revision = EditorBridge::get_uint(p, "revision", 0);
            if (!plugin.replace_processing_state(
                    snapshot.field, snapshot.viewport, snapshot.layout))
                return EditorBridge::err_response("snapshot state is invalid");
            return EditorBridge::ok_response(
                make_editor_state_payload(plugin, revision));
        });

    bridge.add_handler("ab_toggle",
        [&plugin](const choc::value::ValueView&) {
            auto& b = plugin.snapshots();
            b.active = (b.active == SnapshotBank::Slot::A) ? SnapshotBank::Slot::B
                                                           : SnapshotBank::Slot::A;
            return EditorBridge::ok_response();
        });

    // ── Pattern library ────────────────────────────────────────────────

    bridge.add_handler("load_pattern",
        [&library, &plugin](const choc::value::ValueView& p) -> std::string {
            const auto id = EditorBridge::get_string(p, "id");
            if (id.empty()) return EditorBridge::err_response("pattern id missing");
            const auto* pat = library.find(id);
            if (!pat) return EditorBridge::err_response("unknown pattern id");
            pat->apply_to(plugin.field());
            plugin.publish_field();
            return EditorBridge::ok_response();
        });

    bridge.add_handler("save_current_pattern",
        [&library, &plugin](const choc::value::ValueView& p) -> std::string {
            auto name = EditorBridge::get_string(p, "name");
            if (name.size() > 48) name.resize(48);
            const auto saved = library.save_current(plugin.field(), std::move(name));
            auto extras = pattern_library_projection_(library);
            extras.addMember("id", saved.id);
            extras.addMember("name", saved.name);
            return EditorBridge::ok_response(extras);
        });

    bridge.add_handler("rename_pattern",
        [&library](const choc::value::ValueView& p) -> std::string {
            const auto id = EditorBridge::get_string(p, "id");
            auto name = EditorBridge::get_string(p, "name");
            if (id.empty()) return EditorBridge::err_response("pattern id missing");
            if (name.empty()) return EditorBridge::err_response("pattern name missing");
            if (name.size() > 48) name.resize(48);
            if (!library.rename(id, std::move(name)))
                return EditorBridge::err_response("unknown user pattern id");
            return EditorBridge::ok_response(pattern_library_projection_(library));
        });

    bridge.add_handler("delete_pattern",
        [&library](const choc::value::ValueView& p) -> std::string {
            const auto id = EditorBridge::get_string(p, "id");
            if (id.empty()) return EditorBridge::err_response("pattern id missing");
            if (!library.remove(id))
                return EditorBridge::err_response("unknown user pattern id");
            return EditorBridge::ok_response(pattern_library_projection_(library));
        });

    // ── Preset save/load ───────────────────────────────────────────────

    bridge.add_handler("save_preset",
        [&plugin](const choc::value::ValueView& p) {
            PresetMetadata meta;
            meta.name        = EditorBridge::get_string(p, "name");
            meta.author      = EditorBridge::get_string(p, "author");
            meta.description = EditorBridge::get_string(p, "description");
            meta.created_at  = EditorBridge::get_string(p, "created_at");
            meta.modified_at = EditorBridge::get_string(p, "modified_at");

            auto extras = choc::value::createObject("SavePresetExtras");
            extras.addMember("preset_json", save_preset_to_string(plugin, meta));
            return EditorBridge::ok_response(extras);
        });

    bridge.add_handler("load_preset",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            const auto preset_json = EditorBridge::get_string(p, "preset_json");
            if (preset_json.empty()) return EditorBridge::err_response("preset_json missing");

            const auto result = load_preset_from_string(plugin, preset_json);
            if (!result) return EditorBridge::err_response(describe(result.error));

            auto extras = choc::value::createObject("LoadPresetExtras");
            extras.addMember("name",           result.metadata.name);
            extras.addMember("author",         result.metadata.author);
            extras.addMember("description",    result.metadata.description);
            extras.addMember("created_at",     result.metadata.created_at);
            extras.addMember("modified_at",    result.metadata.modified_at);
            extras.addMember("plugin_version", result.plugin_version);
            return EditorBridge::ok_response(extras);
        });

    // ── Flat param write ───────────────────────────────────────────────

    bridge.add_handler("param_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("id"))
                return EditorBridge::err_response("param id missing");
            const auto id_v = p["id"];
            pulp::state::ParamID id{};
            if      (id_v.isInt32()) id = static_cast<pulp::state::ParamID>(id_v.getInt32());
            else if (id_v.isInt64()) id = static_cast<pulp::state::ParamID>(id_v.getInt64());
            else                     return EditorBridge::err_response("param id must be integer");

            if (!p.hasObjectMember("value"))
                return EditorBridge::err_response("param value missing");
            const float value = EditorBridge::get_float(p, "value", 0.0f);
            plugin.state().set_value(id, value);
            return EditorBridge::ok_response();
        });
}

} // namespace spectr
