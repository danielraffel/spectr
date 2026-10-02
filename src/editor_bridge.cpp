#include "spectr/editor_bridge.hpp"

#include "spectr/spectr.hpp"
#include "spectr/detail/gpu_audio_status_projection.hpp"
#include "spectr/edit_engine.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/pattern.hpp"
#include "spectr/preset_format.hpp"
#include "spectr/snapshot.hpp"

#include <pulp/state/store.hpp>
#include <pulp/platform/clipboard.hpp>
#include <pulp/runtime/build_info.hpp>
#include <pulp/runtime/trace.hpp>
#include <pulp/view/editor_bridge.hpp>

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifndef SPECTR_PULP_SDK_SOURCE_GIT_SHA
#define SPECTR_PULP_SDK_SOURCE_GIT_SHA ""
#endif
#ifndef SPECTR_PULP_SDK_PROVENANCE_EXACT
#define SPECTR_PULP_SDK_PROVENANCE_EXACT 0
#endif
#ifndef SPECTR_PRODUCT_GIT_SHA
#define SPECTR_PRODUCT_GIT_SHA ""
#endif
#ifndef SPECTR_PRODUCT_GIT_DIRTY
#define SPECTR_PRODUCT_GIT_DIRTY 1
#endif

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

struct ModeParamValue {
    pulp::state::ParamID id;
    float value;
};

std::optional<ModeParamValue> parse_mode_param_(std::string_view kind,
                                                std::string_view value) {
    if (kind == "motion") {
        if (value == "live") return ModeParamValue{kParamMotionMode, 0.0f};
        if (value == "precision") return ModeParamValue{kParamMotionMode, 1.0f};
    } else if (kind == "analyzer") {
        if (value == "peak") return ModeParamValue{kParamAnalyzerMode, 0.0f};
        if (value == "avg") return ModeParamValue{kParamAnalyzerMode, 1.0f};
        if (value == "both") return ModeParamValue{kParamAnalyzerMode, 2.0f};
        if (value == "off") return ModeParamValue{kParamAnalyzerMode, 3.0f};
    } else if (kind == "edit") {
        if (value == "sculpt") return ModeParamValue{kParamEditMode, 0.0f};
        if (value == "level") return ModeParamValue{kParamEditMode, 1.0f};
        if (value == "boost") return ModeParamValue{kParamEditMode, 2.0f};
        if (value == "flare") return ModeParamValue{kParamEditMode, 3.0f};
        if (value == "glide") return ModeParamValue{kParamEditMode, 4.0f};
    } else if (kind == "visualization") {
        if (value == "bars") return ModeParamValue{kParamVisualization, 0.0f};
        if (value == "response") return ModeParamValue{kParamVisualization, 1.0f};
        if (value == "both") return ModeParamValue{kParamVisualization, 2.0f};
    }
    return std::nullopt;
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
    // The captured window. A snapshot has always stored it; projecting it is
    // what lets the editor draw a morph that moves the viewport without
    // asking the processor for the endpoints on every pointer sample.
    result.addMember("min_hz", static_cast<double>(snapshot.viewport.min_hz));
    result.addMember("max_hz", static_cast<double>(snapshot.viewport.max_hz));
    return result;
}

choc::value::Value pattern_library_projection_(const PatternLibrary& library) {
    auto result = choc::value::createObject("SpectrPatternLibraryProjection");
    result.addMember("patterns_json", library.export_json());
    return result;
}

std::string build_info_copy_text_(const Spectr& plugin, const GpuAudioStatus& gpu_status) {
    std::string result;
    const auto append = [&result](std::string_view label, std::string_view value) {
        if (value.empty()) return;
        result.append(label);
        result.append(": ");
        result.append(value);
        result.push_back('\n');
    };
    append("Spectr", plugin.descriptor().version);
    constexpr std::string_view product_sha{SPECTR_PRODUCT_GIT_SHA};
    append("Spectr SHA", product_sha.empty() ? std::string_view{"unknown"}
                                              : product_sha);
    append("Spectr source", product_sha.empty() ? std::string_view{"unknown"}
        : SPECTR_PRODUCT_GIT_DIRTY ? std::string_view{"dirty"}
                                  : std::string_view{"clean"});
    append("Pulp SDK", pulp::runtime::kSdkVersion);
    constexpr std::string_view exact_sha{SPECTR_PULP_SDK_SOURCE_GIT_SHA};
    append("Pulp SDK SHA", exact_sha.empty()
        ? std::string_view{pulp::runtime::kGitSha} : exact_sha);
    append("Pulp SDK source", SPECTR_PULP_SDK_PROVENANCE_EXACT == 0
        ? std::string_view{"unknown"}
        : pulp::runtime::kGitDirty ? std::string_view{"dirty"}
                                  : std::string_view{"clean"});
    append("Build", pulp::runtime::kBuildType);
    append("Built", pulp::runtime::kBuildIso8601);
    result.append(detail::gpu_audio_status_copy_text(gpu_status));
    return result;
}

choc::value::Value build_info_projection_(const Spectr& plugin) {
    auto result = choc::value::createObject("SpectrBuildInfo");
    result.addMember("product_version", plugin.descriptor().version);
    constexpr std::string_view product_sha{SPECTR_PRODUCT_GIT_SHA};
    if (!product_sha.empty())
        result.addMember("product_sha", std::string{product_sha});
    result.addMember("product_dirty", SPECTR_PRODUCT_GIT_DIRTY != 0);
    result.addMember("product_provenance_known", !product_sha.empty());
    result.addMember("sdk_version", std::string{pulp::runtime::kSdkVersion});
    constexpr std::string_view exact_sha{SPECTR_PULP_SDK_SOURCE_GIT_SHA};
    const auto sdk_sha = exact_sha.empty()
        ? std::string_view{pulp::runtime::kGitSha} : exact_sha;
    if (!sdk_sha.empty()) result.addMember("sdk_sha", std::string{sdk_sha});
    result.addMember("sdk_provenance_exact",
                     SPECTR_PULP_SDK_PROVENANCE_EXACT != 0);
    if (!pulp::runtime::kBuildType.empty())
        result.addMember("build_type", std::string{pulp::runtime::kBuildType});
    if (!pulp::runtime::kBuildIso8601.empty())
        result.addMember("build_time", std::string{pulp::runtime::kBuildIso8601});
    result.addMember("sdk_dirty", pulp::runtime::kGitDirty);
    const auto gpu_status=plugin.gpu_audio_status();
    result.addMember("gpu_audio", detail::gpu_audio_status_projection(gpu_status));
    result.addMember("copy_text", build_info_copy_text_(plugin,gpu_status));
    return result;
}

// The revision of the last state the editor applied when it built a
// publication. Absent or malformed reads as absent: the publication is then
// diffed against the latest state handed over, which is never worse than
// taking it wholesale.
std::optional<EditorRevision> drawn_revision_(
    const choc::value::ValueView& payload) {
    if (!payload.isObject() || !payload.hasObjectMember("drawn_revision"))
        return std::nullopt;
    const auto value = payload["drawn_revision"];
    if (value.isInt32() && value.getInt32() >= 0)
        return static_cast<EditorRevision>(value.getInt32());
    if (value.isInt64() && value.getInt64() >= 0)
        return static_cast<EditorRevision>(value.getInt64());
    if (value.isFloat64()) {
        const double d = value.getFloat64();
        if (std::isfinite(d) && d >= 0.0 && d == std::floor(d)
            && d <= static_cast<double>(kMaxEditorRevision))
            return static_cast<EditorRevision>(d);
    }
    return std::nullopt;
}

std::optional<EditorRevision> expected_revision_(
    const choc::value::ValueView& payload) {
    if (!payload.isObject() || !payload.hasObjectMember("expected_revision"))
        return std::nullopt;
    const auto value = payload["expected_revision"];
    if (value.isInt32() && value.getInt32() >= 0)
        return static_cast<EditorRevision>(value.getInt32());
    if (value.isInt64() && value.getInt64() >= 0)
        return static_cast<EditorRevision>(value.getInt64());
    // A present but malformed precondition must never degrade into an
    // unconditional mutation. This sentinel is outside the authority's
    // signed-JSON revision domain and therefore always rejects as stale.
    return kMaxEditorRevision + 1;
}

/// The modulation lanes as the editor reads them. Shared by the hydration
/// payload and the live projection: every one of these is an automatable host
/// parameter, so an editor that only ever reads them at hydration shows the
/// user a settings panel frozen at the value the session opened with while the
/// host drives the audio somewhere else.
// Where this editor lives and whether its plain-key shortcuts are on there.
// "standalone" always binds them; "plugin" only when the user asked.
choc::value::Value make_keyboard_policy_payload_(const Spectr& plugin) {
    auto keyboard = choc::value::createObject("SpectrKeyboardPolicy");
    keyboard.addMember("host_kind",
                       std::string(editor_is_standalone() ? "standalone" : "plugin"));
    keyboard.addMember("shortcuts_in_daw", plugin.keyboard_shortcuts_in_daw());
    return keyboard;
}

// Freeze. `frozen` and the Length are host parameters, so they ride the
// LIVE projection too: automation turns the toggle over, or moves the
// dropdown, without a hydration. The vocabulary -- the common lengths, the
// fraction set, the bar limit -- is the processor's (freeze_length.hpp) and
// rides hydration, so the editor never keeps a list of its own.
choc::value::Value make_freeze_payload_(const Spectr& plugin, bool with_settings) {
    auto freeze = choc::value::createObject("SpectrFreeze");
    freeze.addMember("frozen", plugin.state().get_value(kParamFreeze) >= 0.5f);
    const auto length = plugin.freeze_length();
    const auto custom = plugin.freeze_custom_length();
    auto current = choc::value::createObject("SpectrFreezeLength");
    current.addMember("preset", static_cast<std::int32_t>(plugin.freeze_length_preset()));
    current.addMember("bars", static_cast<std::int32_t>(length.bars));
    current.addMember("fraction", std::string(fraction_of(length).text));
    current.addMember("label", length_label(length));
    current.addMember("custom_bars", static_cast<std::int32_t>(custom.bars));
    current.addMember("custom_fraction", std::string(fraction_of(custom).text));
    // What it is at the tempo last seen, and whether the loop cap bites:
    // the editor says so only then.
    const double seconds = plugin.freeze_length_seconds();
    const double cap = plugin.freeze_loop_cap_seconds();
    current.addMember("seconds", seconds);
    current.addMember("cap_seconds", cap);
    current.addMember("capped", seconds > cap);
    current.addMember("tempo_bpm", plugin.transport_tempo_bpm());
    freeze.addMember("length", current);
    if (with_settings) {
        auto presets = choc::value::createEmptyArray();
        for (const auto& preset : kLengthPresets) {
            auto item = choc::value::createObject("SpectrFreezeLengthPreset");
            item.addMember("bars", static_cast<std::int32_t>(preset.bars));
            item.addMember("fraction", std::string(fraction_of(preset).text));
            item.addMember("label", length_label(preset));
            presets.addArrayElement(item);
        }
        freeze.addMember("length_presets", presets);
        auto fractions = choc::value::createEmptyArray();
        for (const auto& fraction : kLengthFractions)
            fractions.addArrayElement(std::string(fraction.text));
        freeze.addMember("length_fractions", fractions);
        freeze.addMember("length_max_bars", static_cast<std::int32_t>(kMaxLengthBars));
    }
    return freeze;
}

// A length request's shape: {bars: integer, fraction: "n/d"}. Returns an
// error for a malformed request, or "" with `bars` (pinned to -1 or 129 when
// out of range, so validate_length names the reason) and the fraction's
// index (-1 for one outside the set). Validity itself is validate_length's.
std::string read_length_request_(const choc::value::ValueView& p, int& bars, int& fraction) {
    if (!p.isObject() || !p.hasObjectMember("bars") || !p.hasObjectMember("fraction"))
        return "bars and fraction required";
    const auto& b = p["bars"];
    const auto& f = p["fraction"];
    std::int64_t value = 0;
    if (b.isInt32()) value = b.getInt32();
    else if (b.isInt64()) value = b.getInt64();
    else if (b.isFloat64() && std::isfinite(b.getFloat64())
             && b.getFloat64() == std::floor(b.getFloat64())
             && std::abs(b.getFloat64()) < 1.0e9)
        value = static_cast<std::int64_t>(b.getFloat64());
    else return "bars must be an integer";
    if (!f.isString()) return "fraction must be a string";
    bars = value < 0 ? -1 : value > kMaxLengthBars ? kMaxLengthBars + 1 : static_cast<int>(value);
    fraction = fraction_index_from_text(f.getString());
    return "";
}

choc::value::Value make_modulation_payload_(const Spectr& plugin) {
    const auto modulation_state = plugin.modulation_settings();
    auto modulation = choc::value::createObject("SpectrModulationState");
    modulation.addMember("enabled", modulation_state.enabled);
    modulation.addMember("shape", static_cast<std::int32_t>(modulation_state.shape));
    modulation.addMember("beats_per_cycle", static_cast<double>(
        modulation_state.beats_per_cycle));
    modulation.addMember("depth", static_cast<double>(modulation_state.depth));
    modulation.addMember("target", static_cast<std::int32_t>(
        modulation_state.target));
    modulation.addMember("lfo2_enabled", modulation_state.lfo2_enabled);
    modulation.addMember("lfo2_shape", static_cast<std::int32_t>(modulation_state.lfo2_shape));
    modulation.addMember("lfo2_beats_per_cycle", static_cast<double>(
        modulation_state.lfo2_beats_per_cycle));
    modulation.addMember("lfo2_depth", static_cast<double>(modulation_state.lfo2_depth));
    // The resolved selection, never the raw sentinel: the editor draws these
    // bits directly, so "no explicit selection" must present as the single
    // enum destination that is actually being modulated.
    modulation.addMember("target_mask", static_cast<std::int32_t>(
        resolve_modulation_target_mask(modulation_state)));
    // Per-LFO routing: each LFO's enabled destinations as a 6-bit mask (enum
    // order Bank, A, B, Morph, Band shift, Band spread) and each
    // destination's amount. These are host lanes, so they ride the live
    // projection and the editor's toggles and Amount rows follow playback.
    auto routes = choc::value::createEmptyArray();
    for (std::size_t lfo = 0; lfo < kLfoCount; ++lfo) {
        auto route = choc::value::createObject("SpectrLfoRoutes");
        route.addMember("mask", static_cast<std::int32_t>(
            route_mask(modulation_state.routes[lfo])));
        auto amounts = choc::value::createEmptyArray();
        for (const auto& r : modulation_state.routes[lfo])
            amounts.addArrayElement(static_cast<double>(r.amount));
        route.addMember("amounts", amounts);
        routes.addArrayElement(route);
    }
    modulation.addMember("routes", routes);
    return modulation;
}

void add_history_and_macros_(choc::value::Value& payload, const Spectr& plugin,
                             std::size_t n) {
    // Undo availability rides the LIVE per-revision projection so a menu row
    // or a shortcut can be disabled the moment the stack empties, rather than
    // firing a command that reports "nothing to undo" after the fact. The
    // DEPTHS are carried too, because a boolean cannot distinguish "one step
    // left" from "many" — which is exactly what a gate asserting that one
    // drag is ONE undo step has to read.
    payload.addMember("can_undo", plugin.editor_authority().can_undo());
    payload.addMember("can_redo", plugin.editor_authority().can_redo());
    payload.addMember("undo_depth", static_cast<std::int32_t>(
        plugin.editor_authority().undo_depth()));
    payload.addMember("redo_depth", static_cast<std::int32_t>(
        plugin.editor_authority().redo_depth()));
    // Macros ride the LIVE per-revision projection rather than the
    // hydration-only block below, because unlike "Morph moves the view" a
    // macro IS a host parameter: automation moves it, and the editor has to
    // redraw the offset it applies without waiting for a re-hydration.
    //
    // Membership and value are both carried. The editor needs both to draw
    // the offset itself — it cannot read the macro back off the band lanes,
    // because a macro is never written to them.
    auto macros = choc::value::createEmptyArray();
    for (std::size_t m = 0; m < kMacroCount; ++m) {
        auto entry = choc::value::createObject("SpectrMacro");
        entry.addMember("value_db", static_cast<double>(
            plugin.state().get_value(macro_param_id(m))));
        auto slots = choc::value::createEmptyArray();
        const auto members = plugin.macro_members(m);
        // Only VISIBLE members are projected. Membership is kept across a
        // layout change on the C++ side, but the editor draws `n_visible`
        // bands and an index past that end would be an out-of-range write in
        // the render pass.
        for (std::size_t i = 0; i < n; ++i)
            if (members.test(i)) slots.addArrayElement(static_cast<std::int32_t>(i));
        entry.addMember("slots", slots);
        macros.addArrayElement(entry);
    }
    payload.addMember("macros", macros);
    // The Morph lane, so the slider follows host playback. The bands it
    // derives are projected on their own; this is the thumb.
    payload.addMember("morph", static_cast<double>(
        plugin.state().get_value(kParamMorph)));

}

// A response the editor applies: what it carries is what the editor is shown.
std::string shown_response_(EditorAuthority& authority, const Spectr& plugin,
                            EditorRevision revision) {
    FieldSnapshot shown;
    auto payload = make_editor_state_payload(plugin, revision, &shown);
    authority.note_editor_shown(shown, revision);
    return EditorBridge::ok_response(payload);
}

std::string authority_response_(EditorAuthority& authority, const Spectr& plugin,
                                const EditorReceipt& receipt) {
    if (!receipt.accepted) return EditorBridge::err_response(receipt.error);
    return shown_response_(authority, plugin, receipt.revision);
}

// The response to the editor's own full-state publication. The editor does
// not apply it (it replays only the history availability), so it is not a
// state the editor was shown; `publish_editor_state` records what was sent.
std::string publication_response_(const Spectr& plugin,
                                  const EditorReceipt& receipt) {
    if (!receipt.accepted) return EditorBridge::err_response(receipt.error);
    return EditorBridge::ok_response(
        make_editor_state_payload(plugin, receipt.revision));
}

} // namespace

choc::value::Value make_editor_state_payload(const Spectr& plugin,
                                             EditorRevision revision,
                                             FieldSnapshot* shown) {
    const auto state = plugin.processing_state_snapshot();
    if (shown) *shown = {state.field, state.viewport, state.layout, true};
    const auto n = visible_count(state.layout);
    auto gains = choc::value::createEmptyArray();
    auto muted = choc::value::createEmptyArray();
    for (std::size_t i = 0; i < n; ++i) {
        gains.addArrayElement(static_cast<double>(state.field.bands[i].gain_db));
        muted.addArrayElement(state.field.bands[i].muted);
    }

    auto snapshots = choc::value::createObject("SpectrSnapshotState");
    snapshots.addMember("A", snapshot_projection_(state.snapshots.a, n));
    snapshots.addMember("B", snapshot_projection_(state.snapshots.b, n));

    auto payload = choc::value::createObject("SpectrEditorState");
    payload.addMember("revision", static_cast<std::int64_t>(
        std::min(revision, kMaxEditorRevision)));
    payload.addMember("n_visible", static_cast<std::int32_t>(n));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", muted);
    payload.addMember("min_hz", static_cast<double>(state.viewport.min_hz));
    payload.addMember("max_hz", static_cast<double>(state.viewport.max_hz));
    payload.addMember("motion_mode", static_cast<double>(
        plugin.editor_mode_param(kParamMotionMode)));
    payload.addMember("analyzer_mode", static_cast<double>(
        plugin.editor_mode_param(kParamAnalyzerMode)));
    payload.addMember("edit_mode", static_cast<double>(
        plugin.editor_mode_param(kParamEditMode)));
    payload.addMember("visualization_mode", static_cast<double>(
        plugin.editor_mode_param(kParamVisualization)));
    add_history_and_macros_(payload, plugin, n);

    auto modulation = make_modulation_payload_(plugin);
    // Whether a morph moves the viewport is drawn in the same Settings group
    // as the LFO lanes, but unlike them it is not a host parameter and cannot
    // change under automation. It therefore belongs here, in the hydration
    // payload the panel reads once, and deliberately NOT in the live
    // per-revision projection, which stays exactly the automatable lanes.
    modulation.addMember("morph_applies_viewport",
                         plugin.morph_applies_viewport());
    // The plain-key shortcut policy. Hydration-only, like the switch above:
    // it is never automated, so the live per-revision projection omits it.
    payload.addMember("keyboard", make_keyboard_policy_payload_(plugin));
    // The editor's Range (level_controls.hpp): editor state, hydration only.
    payload.addMember("range_db", static_cast<std::int32_t>(plugin.editor_range_db()));
    payload.addMember("freeze", make_freeze_payload_(plugin, /*with_settings=*/true));
    // The Latency control. Not a host parameter and not automatable, so like
    // "Morph moves the view" it rides the hydration payload the panel reads
    // once and deliberately never appears in the live per-revision projection,
    // which stays exactly the automatable lanes.
    //
    // Every option ships its own label, its guidance line and its measured
    // cost, all derived here rather than written into the panel. A figure the
    // UI typed would be wrong at 96 kHz and wrong again the day a mode's
    // geometry moves; a label the UI typed would drift from the one the About
    // guide and the detector agree on.
    auto latency = choc::value::createObject("Latency");
    latency.addMember("control_label", std::string(kRenderModeControlLabel));
    latency.addMember("mode", std::string(render_mode_token(plugin.render_mode())));
    latency.addMember("samples", static_cast<double>(
        plugin.render_mode_latency_samples(plugin.render_mode())));
    latency.addMember("ms", plugin.render_mode_latency_ms(plugin.render_mode()));
    auto options = choc::value::createEmptyArray();
    for (const auto mode : kRenderModes) {
        auto option = choc::value::createObject("LatencyOption");
        option.addMember("mode", std::string(render_mode_token(mode)));
        option.addMember("label", std::string(render_mode_label(mode)));
        option.addMember("description", std::string(render_mode_description(mode)));
        option.addMember("samples", static_cast<double>(
            plugin.render_mode_latency_samples(mode)));
        option.addMember("ms", plugin.render_mode_latency_ms(mode));
        options.addArrayElement(option);
    }
    latency.addMember("options", options);
    payload.addMember("latency", latency);

    payload.addMember("modulation", modulation);
    payload.addMember("snapshots", snapshots);
    payload.addMember("patterns_json", plugin.patterns().export_json());
    return payload;
}

choc::value::Value make_editor_live_state_payload(const Spectr& plugin,
                                                  EditorRevision revision,
                                                  FieldSnapshot* shown) {
    const auto state = plugin.processing_state_snapshot();
    if (shown) *shown = {state.field, state.viewport, state.layout, true};
    const auto n = visible_count(state.layout);
    auto gains = choc::value::createEmptyArray();
    auto muted = choc::value::createEmptyArray();
    for (std::size_t i = 0; i < n; ++i) {
        gains.addArrayElement(static_cast<double>(state.field.bands[i].gain_db));
        muted.addArrayElement(state.field.bands[i].muted);
    }

    auto payload = choc::value::createObject("SpectrEditorLiveState");
    payload.addMember("revision", static_cast<std::int64_t>(
        std::min(revision, kMaxEditorRevision)));
    payload.addMember("n_visible", static_cast<std::int32_t>(n));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", muted);
    payload.addMember("min_hz", static_cast<double>(state.viewport.min_hz));
    payload.addMember("max_hz", static_cast<double>(state.viewport.max_hz));
    payload.addMember("motion_mode", static_cast<double>(
        plugin.editor_mode_param(kParamMotionMode)));
    payload.addMember("analyzer_mode", static_cast<double>(
        plugin.editor_mode_param(kParamAnalyzerMode)));
    payload.addMember("edit_mode", static_cast<double>(
        plugin.editor_mode_param(kParamEditMode)));
    payload.addMember("visualization_mode", static_cast<double>(
        plugin.editor_mode_param(kParamVisualization)));
    add_history_and_macros_(payload, plugin, n);
    payload.addMember("modulation", make_modulation_payload_(plugin));
    payload.addMember("freeze", make_freeze_payload_(plugin, /*with_settings=*/false));
    return payload;
}

void register_spectr_editor_handlers(EditorBridge& bridge,
                                     Spectr& plugin,
                                     PatternLibrary& library,
                                     EditorAuthority& authority,
                                     ClipboardWriter clipboard_writer)
{
    if (!clipboard_writer) {
        clipboard_writer = [](std::string_view text) {
            return pulp::platform::Clipboard::set_text(std::string{text});
        };
    }

    bridge.add_handler("processing_state_get",
        [&plugin, &authority](const choc::value::ValueView&) {
            // Every committed realm requests state on mount. Treat that as a
            // new gesture epoch so a reload can never resume a C++ snapshot
            // captured by callbacks from the retired realm.
            authority.reset_transient_state();
            return authority_response_(authority, plugin, {true, authority.revision(), {}});
        });

    bridge.add_handler("build_info_get",
        [&plugin](const choc::value::ValueView&) {
            return EditorBridge::ok_response(build_info_projection_(plugin));
        });

    // The generic clipboard verb. `build_info_copy` cannot serve it: that
    // handler ignores its payload entirely and copies the build report. Nor is
    // there a browser fallback -- `navigator` is undefined in this runtime, so
    // the captured web app's `navigator.clipboard` paths are dead code here.
    //
    // Both handlers capture the writer BY COPY. The obvious alternative --
    // leaving `build_info_copy` to `std::move` it, and registering everything
    // that needs it first -- makes correctness depend on registration ORDER
    // against a moved-from `std::function`, whose state is unspecified. It
    // happens to stay callable on this toolchain, which is worse than if it did
    // not: the hazard is invisible here and would surface as a copy button that
    // reports success and writes nothing, on some other standard library. A
    // copy costs one refcount at construction.
    bridge.add_handler("clipboard_write",
        [clipboard_writer](const choc::value::ValueView& p) {
            if (!p.isObject() || !p.hasObjectMember("text")
                || !p["text"].isString())
                return EditorBridge::err_response("text must be a string");
            const std::string text{p["text"].getString()};
            if (text.empty())
                return EditorBridge::err_response("text must not be empty");
            if (!clipboard_writer(text))
                return EditorBridge::err_response("clipboard unavailable");
            return EditorBridge::ok_response();
        });

    bridge.add_handler("build_info_copy",
        [&plugin, clipboard_writer](
            const choc::value::ValueView&) {
            const auto text = build_info_copy_text_(plugin,plugin.gpu_audio_status());
            if (!clipboard_writer(text))
                return EditorBridge::err_response("clipboard unavailable");
            return EditorBridge::ok_response();
        });

    // Complete JS field publication. Gain and mute are deliberately separate:
    // JSON never transports -Infinity, while a muted band still reaches an
    // exact 0.0 multiplier in BandField::linear_gain().
    bridge.add_handler("band_field_set",
        [&plugin, &authority](const choc::value::ValueView& p) -> std::string {
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

            const auto state = plugin.processing_state_snapshot();
            auto next = state.field;
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

            return publication_response_(plugin, authority.publish_editor_state(
                next, state.viewport, *layout, drawn_revision_(p),
                expected_revision_(p)));
        });

    // Atomic state publication for the imported live editor. Zoom/pan changes
    // are sound-defining in Spectr, so the viewport and the full band field
    // cross the bridge together and compile into one complete Pulp mask table.
    bridge.add_handler("processing_state_set",
        [&plugin, &authority](const choc::value::ValueView& p) -> std::string {
            PULP_TRACE_SCOPE_NAMED("state", "spectr_processing_state_set");
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

            BandField next = plugin.processing_state_snapshot().field;
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
            return publication_response_(plugin, authority.publish_editor_state(
                next, viewport, *layout, drawn_revision_(p),
                expected_revision_(p)));
        });

    // ── Drag protocol ──────────────────────────────────────────────────

    bridge.add_handler("paint_start",
        [&plugin, &authority](const choc::value::ValueView& p) {
            return authority_response_(authority,
                plugin, authority.begin_band_edit(expected_revision_(p)));
        });

    bridge.add_handler("paint",
        [&plugin, &authority](const choc::value::ValueView& p) -> std::string {
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

            return authority_response_(authority, plugin, authority.update_band_edit(
                *mode, g, expected_revision_(p)));
        });

    bridge.add_handler("paint_end",
        [&plugin, &authority](const choc::value::ValueView&) {
            return authority_response_(authority, plugin, authority.end_band_edit());
        });

    // ── Morph / snapshot / A-B ─────────────────────────────────────────

    bridge.add_handler("morph",
        [&plugin, &authority](const choc::value::ValueView& p) {
            const auto t = std::clamp(EditorBridge::get_float(p, "t", 0.0f), 0.0f, 1.0f);
            return authority_response_(authority,
                plugin, authority.apply_morph(t, expected_revision_(p)));
        });

    bridge.add_handler("capture_snapshot",
        [&plugin, &authority](const choc::value::ValueView& p) -> std::string {
            const auto slot = parse_slot_(EditorBridge::get_string(p, "slot"));
            if (!slot) return EditorBridge::err_response("slot must be 'A' or 'B'");
            return authority_response_(authority, plugin, authority.capture_snapshot(
                *slot, expected_revision_(p)));
        });

    bridge.add_handler("clear_snapshot",
        [&plugin, &authority](const choc::value::ValueView& p) -> std::string {
            const auto slot = parse_slot_(EditorBridge::get_string(p, "slot"));
            if (!slot) return EditorBridge::err_response("slot must be 'A' or 'B'");
            return authority_response_(authority, plugin, authority.clear_snapshot(
                *slot, expected_revision_(p)));
        });

    bridge.add_handler("recall_snapshot",
        [&plugin, &authority](const choc::value::ValueView& p) -> std::string {
            const auto slot = parse_slot_(EditorBridge::get_string(p, "slot"));
            if (!slot) return EditorBridge::err_response("slot must be 'A' or 'B'");
            return authority_response_(authority, plugin, authority.recall_snapshot(
                *slot, expected_revision_(p)));
        });

    bridge.add_handler("ab_toggle",
        [&plugin](const choc::value::ValueView&) {
            auto& b = plugin.snapshots();
            b.active = (b.active == SnapshotBank::Slot::A) ? SnapshotBank::Slot::B
                                                           : SnapshotBank::Slot::A;
            return EditorBridge::ok_response();
        });

    // ── Undo / redo ────────────────────────────────────────────────────
    //
    // The gesture pair is shaped like `macro_drag_start`/`macro_drag_end`
    // above, and for the same reason stated there: a drag is ONE gesture, not
    // one per event. It matters more here than it does for a host bracket.
    // This editor republishes the complete processing state on every pointer
    // sample, so without these two verbs a single drag across 32 bands would
    // land as dozens of separate undo steps and the user would press undo
    // dozens of times to get back to where they started.
    //
    // Unbalanced calls are safe by construction: an unmatched end is ignored
    // and a second start keeps the first one's base, so a JS error path that
    // drops an `undo_gesture_end` cannot corrupt the stack — the worst case
    // is the next edit joining the open gesture.
    bridge.add_handler("undo_gesture_start",
        [&authority](const choc::value::ValueView& p) -> std::string {
            auto name = EditorBridge::get_string(p, "name");
            authority.begin_undo_gesture(std::move(name));
            return EditorBridge::ok_response();
        });

    bridge.add_handler("undo_gesture_end",
        [&plugin, &authority](const choc::value::ValueView&) -> std::string {
            authority.end_undo_gesture();
            return authority_response_(authority, plugin, {true, authority.revision(), {}});
        });

    bridge.add_handler("undo",
        [&plugin, &authority](const choc::value::ValueView&) -> std::string {
            return authority_response_(authority, plugin, authority.undo());
        });

    bridge.add_handler("redo",
        [&plugin, &authority](const choc::value::ValueView&) -> std::string {
            return authority_response_(authority, plugin, authority.redo());
        });

    // ── Pattern library ────────────────────────────────────────────────

    bridge.add_handler("load_pattern",
        [&library, &plugin](const choc::value::ValueView& p) -> std::string {
            const auto id = EditorBridge::get_string(p, "id");
            if (id.empty()) return EditorBridge::err_response("pattern id missing");
            const auto* pat = library.find(id);
            if (!pat) return EditorBridge::err_response("unknown pattern id");
            auto field = plugin.processing_state_snapshot().field;
            pat->apply_to(field);
            plugin.replace_field(field);
            return EditorBridge::ok_response();
        });

    bridge.add_handler("save_current_pattern",
        [&library, &plugin](const choc::value::ValueView& p) -> std::string {
            auto name = EditorBridge::get_string(p, "name");
            if (name.size() > 48) name.resize(48);
            const auto saved = library.save_current(
                plugin.processing_state_snapshot().field, std::move(name));
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

    // Both of these route to PatternLibrary methods that already existed and
    // were already unit-tested, and had ZERO production callers: the editor
    // did each of them by writing React state alone. That state is replaced
    // wholesale from this library by the next command's response, so a
    // duplicate or a default survived only until the user's next save --
    // silently, and invisibly to a row count, because a copy being dropped
    // and a save landing cancel out in the total.
    bridge.add_handler("duplicate_pattern",
        [&library](const choc::value::ValueView& p) -> std::string {
            const auto id = EditorBridge::get_string(p, "id");
            if (id.empty()) return EditorBridge::err_response("pattern id missing");
            const auto copy = library.duplicate(id);
            if (!copy)
                return EditorBridge::err_response("unknown pattern id");
            auto payload = pattern_library_projection_(library);
            payload.addMember("id", copy->id);
            payload.addMember("name", copy->name);
            return EditorBridge::ok_response(payload);
        });

    // Factory ids are accepted deliberately: FLAT is the library's own resting
    // default, so refusing them would make the default unresettable from the
    // editor. set_default validates the id against factory + user itself.
    bridge.add_handler("set_default_pattern",
        [&library](const choc::value::ValueView& p) -> std::string {
            const auto id = EditorBridge::get_string(p, "id");
            if (id.empty()) return EditorBridge::err_response("pattern id missing");
            if (!library.set_default(id))
                return EditorBridge::err_response("unknown pattern id");
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

    // ── Product mode writes ───────────────────────────────────────────

    bridge.add_handler("mode_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject()) return EditorBridge::err_response("mode payload missing");
            const auto kind = EditorBridge::get_string(p, "kind");
            const auto value = EditorBridge::get_string(p, "value");
            const auto parsed = parse_mode_param_(kind, value);
            if (!parsed) return EditorBridge::err_response("invalid mode kind or value");
            if (!plugin.set_editor_mode_param(parsed->id, parsed->value))
                return EditorBridge::err_response("mode parameter unavailable");
            return EditorBridge::ok_response();
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

    // Editor edits of the plain parameters (Mix, Output trim, both LFOs):
    // see Spectr::edit_param_from_editor. `param_edit` is the value; outside
    // a drag it is a complete host gesture, inside one it joins it.
    // `param_gesture_begin` / `param_gesture_end` bracket a drag. Unlike
    // `param_set`, which writes a bare value, every one of these is something
    // a host recording in Touch, Latch or Write can record.
    const auto param_id_of = [](const choc::value::ValueView& p)
        -> std::optional<pulp::state::ParamID> {
        if (!p.isObject() || !p.hasObjectMember("id")) return std::nullopt;
        const auto id_v = p["id"];
        if (id_v.isInt32()) return static_cast<pulp::state::ParamID>(id_v.getInt32());
        if (id_v.isInt64()) return static_cast<pulp::state::ParamID>(id_v.getInt64());
        return std::nullopt;
    };

    bridge.add_handler("param_edit",
        [&plugin, param_id_of](const choc::value::ValueView& p) -> std::string {
            const auto id = param_id_of(p);
            if (!id) return EditorBridge::err_response("param id must be an integer");
            if (!p.hasObjectMember("value"))
                return EditorBridge::err_response("param value missing");
            const auto value = finite_number_(p["value"]);
            if (!value) return EditorBridge::err_response("param value must be finite");
            if (!plugin.edit_param_from_editor(*id, static_cast<float>(*value)))
                return EditorBridge::err_response("param is not editor-editable");
            return EditorBridge::ok_response();
        });

    bridge.add_handler("param_gesture_begin",
        [&plugin, param_id_of](const choc::value::ValueView& p) -> std::string {
            const auto id = param_id_of(p);
            if (!id) return EditorBridge::err_response("param id must be an integer");
            if (!plugin.begin_editor_param_gesture(*id))
                return EditorBridge::err_response("param is not editor-editable");
            return EditorBridge::ok_response();
        });

    bridge.add_handler("param_gesture_end",
        [&plugin, param_id_of](const choc::value::ValueView& p) -> std::string {
            const auto id = param_id_of(p);
            if (!id) return EditorBridge::err_response("param id must be an integer");
            if (!plugin.end_editor_param_gesture(*id))
                return EditorBridge::err_response("param is not editor-editable");
            return EditorBridge::ok_response();
        });

    // A drag on a control the PROCESSOR writes as a derived value -- Morph,
    // pushed by apply_morph_to_live as the snapshot derivation runs. The pair
    // opens and closes the processor's gesture epoch, the same bracket a
    // paint drag and `macro_drag_start`/`macro_drag_end` use: each parameter
    // the drag writes opens its host gesture once and every one closes on
    // release, so a host in Touch sees one gesture per drag instead of the
    // control released between every two moves.
    bridge.add_handler("param_drag_start",
        [&plugin](const choc::value::ValueView&) -> std::string {
            plugin.begin_param_gesture_epoch();
            return EditorBridge::ok_response();
        });

    bridge.add_handler("param_drag_end",
        [&plugin](const choc::value::ValueView&) -> std::string {
            plugin.end_param_gesture_epoch();
            return EditorBridge::ok_response();
        });

    bridge.add_handler("modulation_targets_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("targets")
                || !p["targets"].isArray())
                return EditorBridge::err_response("targets must be an array");
            const auto targets = p["targets"];
            std::uint8_t mask = 0;
            for (std::uint32_t i = 0; i < targets.size(); ++i) {
                if (!targets[i].isString())
                    return EditorBridge::err_response("target names must be strings");
                const auto name = targets[i].getString();
                if (name == "bank") mask |= 1u << 0;
                else if (name == "snapshot-a") mask |= 1u << 1;
                else if (name == "snapshot-b") mask |= 1u << 2;
                else if (name == "morph") mask |= 1u << 3;
                else return EditorBridge::err_response("unknown modulation target");
            }
            return plugin.set_modulation_target_mask(mask)
                ? EditorBridge::ok_response()
                : EditorBridge::err_response("modulation target state unavailable");
        });

    // Assign or clear one macro's membership. The macro's VALUE is not here
    // on purpose: it is an ordinary parameter and goes through `param_set`
    // like every other lane, which is what lets a host record and automate it.
    bridge.add_handler("macro_set_members",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("macro"))
                return EditorBridge::err_response("macro index missing");
            const auto macro = EditorBridge::get_uint(p, "macro", kMacroCount);
            if (macro >= kMacroCount)
                return EditorBridge::err_response("macro index out of range");
            if (!p.hasObjectMember("slots") || !p["slots"].isArray())
                return EditorBridge::err_response("slots must be an array");
            const auto slots = p["slots"];
            MacroMembership<kMaxBands> members;
            for (std::uint32_t i = 0; i < slots.size(); ++i) {
                const auto& entry = slots[i];
                if (!entry.isInt32() && !entry.isInt64())
                    return EditorBridge::err_response("slot indices must be integers");
                const auto slot = entry.isInt32()
                    ? static_cast<std::int64_t>(entry.getInt32())
                    : entry.getInt64();
                if (slot < 0 || slot >= static_cast<std::int64_t>(kMaxBands))
                    return EditorBridge::err_response("slot index out of range");
                members.set(static_cast<std::size_t>(slot));
            }
            // An empty array is a legitimate payload — it is "Clear Macro N" —
            // so it must not be mistaken for a malformed one.
            if (!plugin.set_macro_members(macro, members))
                return EditorBridge::err_response("macro state unavailable");
            return shown_response_(plugin.editor_authority(), plugin,
                                   plugin.editor_authority().revision());
        });

    // The macro DRAG triad. Shaped like paint_start/paint/paint_end and for
    // the same reason: a drag is one host gesture, not one per event.
    //
    // `macro_drag_start` opens the gesture epoch, every `macro_set` inside it
    // reuses the single open bracket, and `macro_drag_end` closes it. A
    // `macro_set` sent OUTSIDE a drag (a menu item, a typed value) still
    // emits its own complete bracket, so a host always sees a balanced
    // begin/end whichever surface issued the change.
    bridge.add_handler("macro_drag_start",
        [&plugin](const choc::value::ValueView&) -> std::string {
            plugin.begin_param_gesture_epoch();
            return EditorBridge::ok_response();
        });

    bridge.add_handler("macro_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("macro"))
                return EditorBridge::err_response("macro index missing");
            const auto macro = EditorBridge::get_uint(p, "macro", kMacroCount);
            if (macro >= kMacroCount)
                return EditorBridge::err_response("macro index out of range");
            if (!p.hasObjectMember("value_db"))
                return EditorBridge::err_response("macro value missing");
            const auto value = finite_number_(p["value_db"]);
            if (!value || *value < kBandGainMinDb || *value > kBandGainMaxDb)
                return EditorBridge::err_response(
                    "macro value must be finite and within -24 and +24 dB");
            if (!plugin.set_macro_value(macro, static_cast<float>(*value)))
                return EditorBridge::err_response("macro state unavailable");
            return EditorBridge::ok_response();
        });

    bridge.add_handler("macro_drag_end",
        [&plugin](const choc::value::ValueView&) -> std::string {
            plugin.end_param_gesture_epoch();
            return EditorBridge::ok_response();
        });

    // The plain-key shortcut policy, read synchronously by the document
    // before its first render so a hosted editor never binds a letter the
    // DAW owns, not even for the frames before hydration.
    bridge.add_handler("keyboard_policy_get",
        [&plugin](const choc::value::ValueView&) -> std::string {
            return EditorBridge::ok_response(make_keyboard_policy_payload_(plugin));
        });

    // Range: the plot's vertical scale and how far a full-height edit
    // reaches. Editor state persisted with the session, never a host
    // parameter and never audible. {range_db: 3 | 6 | 12 | 24}.
    bridge.add_handler("range_get",
        [&plugin](const choc::value::ValueView&) -> std::string {
            auto out = choc::value::createObject("SpectrRange");
            out.addMember("range_db", static_cast<std::int32_t>(plugin.editor_range_db()));
            return EditorBridge::ok_response(out);
        });
    bridge.add_handler("range_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("range_db"))
                return EditorBridge::err_response("range_db missing");
            const auto value = finite_number_(p["range_db"]);
            if (!value || *value != std::floor(*value)
                || !plugin.set_editor_range_db(static_cast<int>(*value)))
                return EditorBridge::err_response("range_db must be 3, 6, 12 or 24");
            auto out = choc::value::createObject("SpectrRange");
            out.addMember("range_db", static_cast<std::int32_t>(plugin.editor_range_db()));
            return EditorBridge::ok_response(out);
        });

    // "Keyboard shortcuts in DAW". Shaped like morph_viewport_set: an editor
    // preference persisted in the plugin state, never a host parameter.
    bridge.add_handler("keyboard_shortcuts_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("enabled"))
                return EditorBridge::err_response("enabled missing");
            const auto& flag = p["enabled"];
            if (!flag.isBool())
                return EditorBridge::err_response("enabled must be a boolean");
            plugin.set_keyboard_shortcuts_in_daw(flag.getBool());
            return EditorBridge::ok_response(make_keyboard_policy_payload_(plugin));
        });

    // The LIVE / FROZEN toggle and its keyboard shortcuts. Freeze is host
    // parameter 3, so an editor write is a user edit the host must be able
    // to record: it goes out as one complete gesture (begin, value, end), the
    // bracket Touch / Latch / Write automation keys on. `param_set` writes
    // the value alone, which moves the DSP but leaves a host that records
    // on gestures nothing to record.
    bridge.add_handler("freeze_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("frozen"))
                return EditorBridge::err_response("frozen missing");
            const auto& flag = p["frozen"];
            if (!flag.isBool())
                return EditorBridge::err_response("frozen must be a boolean");
            if (!plugin.set_freeze_from_editor(flag.getBool()))
                return EditorBridge::err_response("freeze parameter unavailable");
            return EditorBridge::ok_response(
                make_freeze_payload_(plugin, /*with_settings=*/false));
        });

    // Freeze's Length, from the header dropdown or its Custom length
    // popover: {bars: integer, fraction: "1/8"}. Validated here by the one
    // model function (validate_length), whatever the editor checked: a
    // refused length leaves the length in force untouched. A common length
    // selects its preset; anything else becomes the custom length. One host
    // gesture, so automation records it.
    bridge.add_handler("freeze_length_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            int bars = 0, fraction = -1;
            if (const auto shape = read_length_request_(p, bars, fraction); !shape.empty())
                return EditorBridge::err_response(shape);
            const auto error = validate_length(bars, fraction);
            if (error != LengthError::none)
                return EditorBridge::err_response(std::string(length_error_message(error)));
            const auto length = make_length(bars, fraction);
            if (!length || !plugin.set_freeze_length_from_editor(*length))
                return EditorBridge::err_response("freeze length unavailable");
            return EditorBridge::ok_response(
                make_freeze_payload_(plugin, /*with_settings=*/false));
        });

    // The Custom length editor's preview: the same validation, the same
    // label, nothing committed. The editor formats no length itself.
    bridge.add_handler("freeze_length_describe",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            int bars = 0, fraction = -1;
            if (const auto shape = read_length_request_(p, bars, fraction); !shape.empty())
                return EditorBridge::err_response(shape);
            auto out = choc::value::createObject("SpectrFreezeLengthPreview");
            const auto error = validate_length(bars, fraction);
            out.addMember("valid", error == LengthError::none);
            out.addMember("message", std::string(length_error_message(error)));
            if (const auto length = make_length(bars, fraction)) {
                const double seconds = length_seconds(
                    *length, plugin.transport_tempo_bpm(),
                    plugin.transport_time_sig_numerator(),
                    plugin.transport_time_sig_denominator());
                out.addMember("label", length_label(*length));
                out.addMember("seconds", seconds);
                out.addMember("capped", seconds > plugin.freeze_loop_cap_seconds());
                out.addMember("cap_seconds", plugin.freeze_loop_cap_seconds());
                out.addMember("tempo_bpm", plugin.transport_tempo_bpm());
            }
            return EditorBridge::ok_response(out);
        });

    // The Length as it stands now, for an editor about to show it: its
    // seconds and the loop cap depend on the host tempo, which moves without
    // a projection.
    bridge.add_handler("freeze_length_get",
        [&plugin](const choc::value::ValueView&) -> std::string {
            return EditorBridge::ok_response(
                make_freeze_payload_(plugin, /*with_settings=*/false));
        });

    bridge.add_handler("morph_viewport_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("enabled"))
                return EditorBridge::err_response("enabled missing");
            const auto& flag = p["enabled"];
            if (!flag.isBool())
                return EditorBridge::err_response("enabled must be a boolean");
            plugin.set_morph_applies_viewport(flag.getBool());
            return EditorBridge::ok_response();
        });

    // Changing the Latency control. Deliberately shaped like
    // morph_viewport_set rather than like mode_set: mode_set writes a host
    // parameter, and this is not one.
    //
    // The mode is sent as its stable token, never as an index. An index would
    // make the panel's option order part of the wire contract, so reordering
    // the list in the UI would silently change what the control does.
    bridge.add_handler("render_mode_set",
        [&plugin](const choc::value::ValueView& p) -> std::string {
            if (!p.isObject() || !p.hasObjectMember("mode"))
                return EditorBridge::err_response("mode missing");
            const auto& value = p["mode"];
            if (!value.isString())
                return EditorBridge::err_response("mode must be a string");
            MaskRenderMode mode{};
            if (!render_mode_from_token(std::string(value.getString()), mode))
                return EditorBridge::err_response("unknown render mode");
            // A switch that cannot be prepared leaves the running mode live,
            // so report the failure rather than an ok the panel would draw as
            // a completed change.
            if (!plugin.set_render_mode(mode))
                return EditorBridge::err_response("could not prepare that mode");
            // Return the rehydrated panel state: the switch moves the reported
            // latency, and the caller needs the new figures without a second
            // round trip.
            return shown_response_(plugin.editor_authority(), plugin,
                                   plugin.editor_authority().revision());
        });
}

} // namespace spectr
