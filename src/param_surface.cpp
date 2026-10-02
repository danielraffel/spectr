#include "spectr/param_surface.hpp"
#include "spectr/macro_field.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace spectr {

// ── Viewport log-frequency codec ─────────────────────────────────────────

std::pair<float, float> encode_viewport(const Viewport& v) noexcept {
    const float lo = std::log10(std::max(v.min_hz, 1.0e-6f));
    const float hi = std::log10(std::max(v.max_hz, 1.0e-6f));
    const float width = std::max(hi - lo, kViewportMinWidthLog);
    return {(lo + hi) * 0.5f, width};
}

Viewport decode_viewport(float center_log, float width_log) noexcept {
    width_log = std::clamp(width_log, kViewportMinWidthLog, kViewportMaxWidthLog);
    const float half = width_log * 0.5f;
    center_log = std::clamp(center_log,
                            kViewportLogMinHz + half,
                            kViewportLogMaxHz - half);
    Viewport v;
    v.min_hz = std::pow(10.0f, center_log - half);
    v.max_hz = std::pow(10.0f, center_log + half);
    // The clamps above keep the window inside the display domain, which is
    // itself well inside Viewport::valid()'s envelope. Fall back to the
    // default window rather than ever publish an inverted one.
    if (!v.valid()) v = Viewport{};
    return v;
}

Layout layout_from_param_value(float value) noexcept {
    // Snap to the nearest legal step (32/40/48/56/64); the store only
    // clamps to the range, so a host writing 47 lands on the nearest layout.
    const float snapped = 32.0f + std::round((value - 32.0f) / 8.0f) * 8.0f;
    switch (static_cast<int>(std::clamp(snapped, 32.0f, 64.0f))) {
        case 40: return Layout::Bands40;
        case 48: return Layout::Bands48;
        case 56: return Layout::Bands56;
        case 64: return Layout::Bands64;
        default: return Layout::Bands32;
    }
}

float param_value_from_layout(Layout layout) noexcept {
    return static_cast<float>(visible_count(layout));
}

// ── Registration ─────────────────────────────────────────────────────────

namespace {

// Group ids (StateStore ParamGroup). The pinned SDK's AU adapter projects
// these to AudioUnit parameter clumps, so a host that draws clumps (Logic
// does) shows the surface grouped rather than as one flat list of 151. The
// names are what the user reads there, so they are product copy, not
// internal labels.
constexpr int kGroupGlobal    = 1;
constexpr int kGroupBandGain  = 2;
constexpr int kGroupBandMute  = 3;
constexpr int kGroupSnapshots = 4;
constexpr int kGroupViewport  = 5;
constexpr int kGroupModes     = 6;
constexpr int kGroupModulation= 7;
constexpr int kGroupMacros    = 8;

std::string band_name(std::size_t i, const char* suffix) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Band %02zu %s", i + 1, suffix);
    return buf;
}

std::string hz_string(float log_hz) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.1f Hz",
                  static_cast<double>(std::pow(10.0f, log_hz)));
    return buf;
}

float parse_hz(std::string_view text) {
    // Accept a plain number (Hz) with an optional "Hz" suffix.
    std::string s(text);
    float hz = 0.0f;
    if (std::sscanf(s.c_str(), "%f", &hz) != 1 || hz <= 0.0f) return 0.0f;
    return std::log10(hz);
}

std::string octaves_string(float width_log) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.1f oct",
                  static_cast<double>(width_log / kViewportMinWidthLog));
    return buf;
}

float parse_octaves(std::string_view text) {
    // Accept the formatter's numeric value with an optional "oct" suffix.
    // StateStore performs range clamping after parsing, so preserve the raw
    // logarithmic value here and let the parameter contract own bounds.
    std::string s(text);
    float octaves = 0.0f;
    if (std::sscanf(s.c_str(), "%f", &octaves) != 1
        || !std::isfinite(octaves) || octaves <= 0.0f)
        return 0.0f;
    return octaves * kViewportMinWidthLog;
}

// LFO rate in the units the editor shows: "4 beats", "0.25 beats", "1 beat".
std::string beats_string(float beats) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%g %s", static_cast<double>(beats),
                  beats == 1.0f ? "beat" : "beats");
    return buf;
}

float parse_beats(std::string_view text) {
    std::string s(text);
    float beats = 0.0f;
    if (std::sscanf(s.c_str(), "%f", &beats) != 1 || !std::isfinite(beats))
        return 0.0f;
    return beats;
}

// LFO depth as the editor shows it: "50%". Typed input accepts "50%", "50",
// or a fraction ("0.5"): a value above 1, or one with a percent sign, is a
// percentage.
std::string percent_string(float fraction) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%",
                  static_cast<int>(std::lround(fraction * 100.0f)));
    return buf;
}

float parse_percent(std::string_view text) {
    std::string s(text);
    float value = 0.0f;
    if (std::sscanf(s.c_str(), "%f", &value) != 1 || !std::isfinite(value))
        return 0.0f;
    if (s.find('%') != std::string::npos || value > 1.0f) value /= 100.0f;
    return value;
}

void add_enum_labels(pulp::state::ParamInfo& info,
                     std::initializer_list<const char*> labels) {
    for (const char* label : labels) info.value_labels.emplace_back(label);
}

} // namespace

void register_surface_params(pulp::state::StateStore& store) {
    store.add_group({kGroupGlobal, "Global", 0});
    store.add_group({kGroupBandGain, "Band Gain", 0});
    store.add_group({kGroupBandMute, "Band Mute", 0});
    store.add_group({kGroupSnapshots, "Snapshots", 0});
    store.add_group({kGroupViewport, "Viewport", 0});
    store.add_group({kGroupModes, "Modes", 0});
    store.add_group({kGroupModulation, "Modulation", 0});
    store.add_group({kGroupMacros, "Macros", 0});

    {
        // A global beside Mix and Output. Automatable: freezing changes
        // neither latency nor topology, so a host may ride it freely.
        pulp::state::ParamInfo info;
        info.id = kParamFreeze;
        info.name = "Freeze";
        info.range = {0.0f, 1.0f, 0.0f, 1.0f};
        info.group_id = kGroupGlobal;
        info.kind = pulp::state::ParamKind::Toggle;
        add_enum_labels(info, {"Live", "Frozen"});
        store.add_parameter(info);
    }
    {
        // How much the next freeze takes in. An enum of the header's common
        // lengths plus "Custom" (the bars + fraction in the plugin state):
        // a host can automate it, and like Freeze it moves neither latency
        // nor topology. Every valid length (129 x 17) as one stepped lane
        // would be unusable to draw automation on.
        pulp::state::ParamInfo info;
        info.id = kParamFreezeLength;
        info.name = "Freeze Length";
        info.range = {0.0f, static_cast<float>(kLengthPresetCustom),
                      static_cast<float>(kDefaultLengthPreset), 1.0f};
        info.group_id = kGroupGlobal;
        info.kind = pulp::state::ParamKind::Enum;
        for (const auto& preset : kLengthPresets)
            info.value_labels.emplace_back(length_label(preset));
        info.value_labels.emplace_back("Custom");
        store.add_parameter(info);
    }

    for (std::size_t i = 0; i < kMaxBands; ++i) {
        pulp::state::ParamInfo info;
        info.id = band_gain_param_id(i);
        info.name = band_name(i, "Gain");
        info.unit = "dB";
        info.range = {kBandGainMinDb, kBandGainMaxDb, 0.0f};
        info.group_id = kGroupBandGain;
        store.add_parameter(info);
    }
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        pulp::state::ParamInfo info;
        info.id = band_mute_param_id(i);
        info.name = band_name(i, "Mute");
        info.range = {0.0f, 1.0f, 0.0f, 1.0f};
        info.group_id = kGroupBandMute;
        info.kind = pulp::state::ParamKind::Toggle;
        add_enum_labels(info, {"Off", "On"});
        store.add_parameter(info);
    }

    {
        pulp::state::ParamInfo info;
        info.id = kParamMorph;
        info.name = "A/B Morph";
        info.range = {0.0f, 1.0f, 0.0f};
        info.group_id = kGroupSnapshots;
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamViewportCenter;
        info.name = "Viewport Center";
        info.unit = "log Hz";
        info.range = {kViewportLogMinHz, kViewportLogMaxHz,
                      kViewportDefaultCenterLog};
        info.group_id = kGroupViewport;
        info.to_string = [](float v) { return hz_string(v); };
        info.from_string = [](const std::string& s) { return parse_hz(s); };
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamViewportWidth;
        info.name = "Viewport Width";
        info.unit = "dec";
        info.range = {kViewportMinWidthLog, kViewportMaxWidthLog,
                      kViewportMaxWidthLog};
        info.group_id = kGroupViewport;
        info.to_string = [](float v) { return octaves_string(v); };
        info.from_string = [](const std::string& s) { return parse_octaves(s); };
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamBandCount;
        info.name = "Band Count";
        info.range = {32.0f, 64.0f, 32.0f, 8.0f};
        info.group_id = kGroupViewport;
        info.kind = pulp::state::ParamKind::Integer;
        add_enum_labels(info, {"32", "40", "48", "56", "64"});
        store.add_parameter(info);
    }

    {
        pulp::state::ParamInfo info;
        info.id = kParamMotionMode;
        info.name = "Motion Mode";
        info.range = {0.0f, 1.0f, 0.0f, 1.0f};
        info.group_id = kGroupModes;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Live", "Precision"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamAnalyzerMode;
        info.name = "Analyzer Mode";
        info.range = {0.0f, 3.0f, 0.0f, 1.0f};
        info.group_id = kGroupModes;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Peak", "Avg", "Both", "Off"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamEditMode;
        info.name = "Edit Mode";
        info.range = {0.0f, 4.0f, 0.0f, 1.0f};
        info.group_id = kGroupModes;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Sculpt", "Level", "Boost", "Flare", "Glide"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamVisualization;
        info.name = "Visualization";
        info.range = {0.0f, 2.0f, 2.0f, 1.0f};
        info.group_id = kGroupModes;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Bars", "Response", "Both"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfoEnabled;
        info.name = "LFO Enabled";
        info.range = {0.0f, 1.0f, 0.0f, 1.0f};
        info.group_id = kGroupModulation;
        info.kind = pulp::state::ParamKind::Toggle;
        add_enum_labels(info, {"Off", "On"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfoShape;
        info.name = "LFO Shape";
        info.range = {0.0f, 3.0f, 0.0f, 1.0f};
        info.group_id = kGroupModulation;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Sine", "Triangle", "Square", "Saw"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfoRate;
        info.name = "LFO Rate";
        // No separate unit: the display string carries it ("4 beats"), and
        // VST3 hosts print `units` after that string, which would double it.
        info.range = {0.25f, 16.0f, 4.0f};
        info.group_id = kGroupModulation;
        info.to_string = [](float v) { return beats_string(v); };
        info.from_string = [](const std::string& t) { return parse_beats(t); };
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfoDepth;
        info.name = "LFO Depth";
        info.range = {0.0f, 1.0f, 0.5f};
        info.group_id = kGroupModulation;
        info.to_string = [](float v) { return percent_string(v); };
        info.from_string = [](const std::string& t) { return parse_percent(t); };
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfoTarget;
        info.name = "LFO Target";
        info.range = {0.0f, 3.0f, 0.0f, 1.0f};
        info.group_id = kGroupModulation;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Whole Bank", "Snapshot A", "Snapshot B", "Morph"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfo2Enabled;
        info.name = "LFO 2 Enabled";
        info.range = {0.0f, 1.0f, 0.0f, 1.0f};
        info.group_id = kGroupModulation;
        info.kind = pulp::state::ParamKind::Toggle;
        add_enum_labels(info, {"Off", "On"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfo2Shape;
        info.name = "LFO 2 Shape";
        info.range = {0.0f, 3.0f, 0.0f, 1.0f};
        info.group_id = kGroupModulation;
        info.kind = pulp::state::ParamKind::Enum;
        add_enum_labels(info, {"Sine", "Triangle", "Square", "Saw"});
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfo2Rate;
        info.name = "LFO 2 Rate";
        // No separate unit: the display string carries it ("4 beats"), and
        // VST3 hosts print `units` after that string, which would double it.
        info.range = {0.25f, 16.0f, 4.0f};
        info.group_id = kGroupModulation;
        info.to_string = [](float v) { return beats_string(v); };
        info.from_string = [](const std::string& t) { return parse_beats(t); };
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfo2Depth;
        info.name = "LFO 2 Depth";
        info.range = {0.0f, 1.0f, 0.0f};
        info.group_id = kGroupModulation;
        info.to_string = [](float v) { return percent_string(v); };
        info.from_string = [](const std::string& t) { return parse_percent(t); };
        store.add_parameter(info);
    }

    // Macros. Registered unconditionally like every other slot in the
    // surface: a macro with no members is inert, but its lane must exist
    // before the user assigns one, or a host would have to rescan to see it.
    //
    // The range is the full band range rather than something narrower. A
    // macro is an OFFSET, so +24 dB only reaches the ceiling for a member
    // already at 0 dB; a member the user drew at -12 dB needs the whole
    // span to be driven to the top. Matching the band range also means the
    // host's automation lane reads in the same units as the thing it moves.
    for (std::size_t m = 0; m < kMacroCount; ++m) {
        pulp::state::ParamInfo info;
        info.id = macro_param_id(m);
        char name[24];
        std::snprintf(name, sizeof(name), "Macro %zu", m + 1);
        info.name = name;
        info.unit = "dB";
        info.range = {kBandGainMinDb, kBandGainMaxDb, 0.0f};
        info.group_id = kGroupMacros;
        store.add_parameter(info);
    }

    // LFO routing, appended after every lane that shipped before it so no
    // existing parameter moves. Each LFO drives any set of destinations at
    // once; each destination has an on/off lane and its own Depth (there is no
    // LFO-level depth any more; 4003/4013 are command lanes onto these).
    // Defaults reproduce a fresh 1.0.x instance: both LFOs on the whole bank at
    // 50 %, nothing else.
    static constexpr const char* kRouteNames[kRouteTargetCount] = {
        "Bank", "Snapshot A", "Snapshot B", "Morph",
        "Band shift", "Band spread"};
    for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
        for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
            pulp::state::ParamInfo info;
            info.id = lfo_route_enabled_param_id(lfo, t);
            info.name = "LFO " + std::to_string(lfo + 1) + " " + kRouteNames[t];
            info.range = {0.0f, 1.0f, t == 0 ? 1.0f : 0.0f, 1.0f};
            info.group_id = kGroupModulation;
            info.kind = pulp::state::ParamKind::Toggle;
            add_enum_labels(info, {"Off", "On"});
            store.add_parameter(info);
        }
        for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
            pulp::state::ParamInfo info;
            info.id = lfo_route_amount_param_id(lfo, t);
            info.name = "LFO " + std::to_string(lfo + 1) + " " + kRouteNames[t]
                + " Depth";
            // 50 %: the LFO Depth a fresh 1.0.x instance opened with, so a new
            // instance's LFO 1 on Bank sounds as it did.
            info.range = {0.0f, 1.0f, 0.5f};
            info.group_id = kGroupModulation;
            info.to_string = [](float v) { return percent_string(v); };
            info.from_string = [](const std::string& text) { return parse_percent(text); };
            store.add_parameter(info);
        }
    }
}

} // namespace spectr

// ── Spectr sync engine (spectr#34) ───────────────────────────────────────
//
// Spectr method definitions live here so spectr.cpp keeps its focus on the
// processor lifecycle. The locking contract is documented on the members in
// spectr.hpp: processing_state_mutex_ guards field_/viewport_/layout_ and
// every mask publish; store writes happen outside it.

namespace spectr {

void Spectr::param_sync_trampoline_(void* ctx, const ParamSyncTask&) noexcept {
    // Fixture-only. Holds this worker back so a test can prove the audio path
    // reads the store on its own rather than by winning a race with this
    // thread. Read once; unset in every shipping configuration, where the
    // branch is a single relaxed load of a zero.
    static const int stall_ms = [] {
        const char* raw = std::getenv("SPECTR_TEST_PARAM_SYNC_STALL_MS");
        if (raw == nullptr) return 0;
        const int parsed = std::atoi(raw);
        return parsed > 0 ? std::min(parsed, 5000) : 0;
    }();
    if (stall_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));
    (void)static_cast<Spectr*>(ctx)->apply_surface_params(/*apply_morph=*/true);
}

Spectr::SurfaceDrift Spectr::sample_surface_drift_() noexcept {
    SurfaceDrift drift;
    const auto* store = param_store_;
    if (!store) return drift;
    for (std::size_t slot = 0; slot < kSurfaceCacheSlots; ++slot) {
        const float value =
            store->get_value(detail::surface_slot_param_id(slot));
        audio_surface_scratch_[slot] = value;
        if (value != applied_param_cache_[slot].load(std::memory_order_relaxed))
            drift.worker = true;
        if (value != audio_applied_surface_[slot])
            drift.audio = true;
    }
    // Nothing has been pushed yet, so the zero-initialised record describes
    // no block and cannot be trusted to match.
    if (!audio_applied_surface_valid_) drift.audio = true;
    return drift;
}

ModulationSettings Spectr::modulation_from_store_() const noexcept {
    ModulationSettings settings;
    const auto* store = param_store_;
    if (!store) return settings;
    settings.enabled = store->get_value(kParamLfoEnabled) >= 0.5f;
    settings.shape = static_cast<LfoShape>(std::clamp(
        static_cast<int>(std::lround(store->get_value(kParamLfoShape))), 0, 3));
    settings.beats_per_cycle = std::clamp(
        store->get_value(kParamLfoRate), 0.25f, 16.0f);
    settings.depth = std::clamp(
        store->get_value(kParamLfoDepth), 0.0f, 1.0f);
    settings.target = static_cast<ModulationTarget>(std::clamp(
        static_cast<int>(std::lround(store->get_value(kParamLfoTarget))), 0, 3));
    settings.lfo2_enabled = store->get_value(kParamLfo2Enabled) >= 0.5f;
    settings.lfo2_shape = static_cast<LfoShape>(std::clamp(
        static_cast<int>(std::lround(store->get_value(kParamLfo2Shape))), 0, 3));
    settings.lfo2_beats_per_cycle = std::clamp(
        store->get_value(kParamLfo2Rate), 0.25f, 16.0f);
    settings.lfo2_depth = std::clamp(
        store->get_value(kParamLfo2Depth), 0.0f, 1.0f);
    for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
        for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
            auto& route = settings.routes[lfo][t];
            route.enabled =
                store->get_value(lfo_route_enabled_param_id(lfo, t)) >= 0.5f;
            route.amount = std::clamp(
                store->get_value(lfo_route_amount_param_id(lfo, t)), 0.0f, 1.0f);
        }
    }
    // The legacy mask is DERIVED from LFO 1's routing, so a reader of the
    // single-target API sees what LFO 1 is actually driving.
    settings.target_mask = static_cast<std::uint8_t>(
        route_mask(settings.routes[0]) & kModulationTargetMaskAll);
    return settings;
}

bool Spectr::apply_surface_params(bool apply_morph) noexcept {
    auto* store = param_store_;
    if (!store) return false;

    std::unique_lock<std::mutex> lock(processing_state_mutex_);
    bool sound_changed = false;
    bool editor_changed = false;
    // A move of the legacy single-target lane is a command: see below. Its
    // routing writes go to the store after the lock is released.
    std::array<std::uint8_t, kRouteLfoCount> legacy_route_masks{};
    bool legacy_target_command = false;
    struct LaneWrite { pulp::state::ParamID id; float value; };
    std::array<LaneWrite, kRouteParamCount> legacy_depth_writes{};
    std::size_t legacy_depth_write_count = 0;

    // Apply morph before individual band lanes. A host can automate morph and
    // a band in the same block; the explicit band value must remain reflected
    // in canonical state (and become a sparse override), rather than being
    // marked applied and then silently overwritten by a later morph pass.
    if (apply_morph) {
        const float t = store->get_value(kParamMorph);
        if (t != applied_param_cache_[detail::kSlotMorph].load(std::memory_order_relaxed)) {
            applied_param_cache_[detail::kSlotMorph].store(t, std::memory_order_relaxed);
            // The Morph slider shows the lane even before both snapshots
            // exist, so a move is always news to the editor.
            editor_changed = true;
            const bool has_a = snapshots_.has(SnapshotBank::Slot::A);
            const bool has_b = snapshots_.has(SnapshotBank::Slot::B);
            if (has_a && has_b) {
                morph_fields(field_, snapshots_.a.field, snapshots_.b.field, t);
                sound_changed = true;
            } else if (has_a || has_b) {
                field_ = has_a ? snapshots_.a.field : snapshots_.b.field;
                sound_changed = true;
            }
            // The viewport follows the same derivation as the bands, so an
            // automated morph moves the window the shape is drawn in rather
            // than leaving the two describing different sounds.
            if (morph_applies_viewport_ && (has_a || has_b)) {
                if (!has_b) viewport_ = snapshots_.a.viewport;
                else if (!has_a) viewport_ = snapshots_.b.viewport;
                else viewport_ = morph_viewports(snapshots_.a.viewport,
                                                 snapshots_.b.viewport, t);
                synced_viewport_ = viewport_;
            }
            // The applied cache is deliberately NOT stamped here. The explicit
            // viewport lane below diffs the store against it, so a host that
            // automates morph AND the viewport in the same pass keeps the
            // explicit write — the same precedence the band lanes already use,
            // where an automated band value overrides the morph that derived
            // it. When only morph moved, the lane sees no drift and the
            // derived window survives.
            // The morph result is the new pushed-state baseline: unchanged
            // band parameters intentionally keep their pre-morph values.
            synced_field_ = field_;
            morph_derived_ = has_a || has_b;
            morph_overrides_.reset();
        }
    }

    for (std::size_t i = 0; i < kMaxBands; ++i) {
        const float gain = store->get_value(band_gain_param_id(i));
        if (gain != applied_param_cache_[i].load(std::memory_order_relaxed)) {
            applied_param_cache_[i].store(gain, std::memory_order_relaxed);
            field_.bands[i].gain_db = gain;  // the store already clamps
            synced_field_.bands[i].gain_db = gain;
            if (morph_derived_) morph_overrides_.set(i);
            sound_changed = true;
        }
        const float mute = store->get_value(band_mute_param_id(i));
        auto& mute_cache = applied_param_cache_[64 + i];
        if (mute != mute_cache.load(std::memory_order_relaxed)) {
            mute_cache.store(mute, std::memory_order_relaxed);
            const bool muted = mute >= 0.5f;
            field_.bands[i].muted = muted;
            synced_field_.bands[i].muted = muted;
            if (morph_derived_) morph_overrides_.set(i);
            sound_changed = true;
        }
    }

    const float center = store->get_value(kParamViewportCenter);
    const float width  = store->get_value(kParamViewportWidth);
    if (center != applied_param_cache_[detail::kSlotCenter].load(std::memory_order_relaxed)
        || width != applied_param_cache_[detail::kSlotWidth].load(std::memory_order_relaxed)) {
        applied_param_cache_[detail::kSlotCenter].store(center, std::memory_order_relaxed);
        applied_param_cache_[detail::kSlotWidth].store(width, std::memory_order_relaxed);
        viewport_ = decode_viewport(center, width);
        synced_viewport_ = viewport_;
        sound_changed = true;
    }

    const float count = store->get_value(kParamBandCount);
    if (count != applied_param_cache_[detail::kSlotBandCount].load(std::memory_order_relaxed)) {
        applied_param_cache_[detail::kSlotBandCount].store(count, std::memory_order_relaxed);
        layout_ = layout_from_param_value(count);
        synced_layout_ = layout_;
        sound_changed = true;
    }

    // Mode toggles carry no separate C++-side state — the parameter IS the
    // state. A host-originated change still needs a new editor projection so
    // playback automation is visible without echoing it back to the host.
    for (std::size_t m = 0; m < 4; ++m) {
        const auto id = kParamMotionMode + static_cast<pulp::state::ParamID>(m);
        const float value = store->get_value(id);
        auto& cached = applied_param_cache_[detail::kSlotModeBase + m];
        if (value != cached.load(std::memory_order_relaxed)) {
            cached.store(value, std::memory_order_relaxed);
            editor_changed = true;
        }
    }

    // Freeze is read by the audio thread straight off the parameter, per
    // automation slice, so there is nothing to republish here. A host-side
    // change only has to reach the editor's toggle, through the same live
    // projection the mode toggles ride -- and it stays out of their loop,
    // which is sized to the four modes.
    {
        const float value = store->get_value(kParamFreeze);
        auto& cached = applied_param_cache_[detail::kSlotFreeze];
        if (value != cached.load(std::memory_order_relaxed)) {
            cached.store(value, std::memory_order_relaxed);
            editor_changed = true;
        }
    }
    // Freeze Length likewise: the audio thread reads it each block, and a
    // host-side change only has to reach the header's dropdown.
    {
        const float value = store->get_value(kParamFreezeLength);
        auto& cached = applied_param_cache_[detail::kSlotFreezeLength];
        if (value != cached.load(std::memory_order_relaxed)) {
            cached.store(value, std::memory_order_relaxed);
            editor_changed = true;
        }
    }

    ModulationSettings next_modulation = modulation_from_store_();
    const std::array<float, 9> modulation_values{
        next_modulation.enabled ? 1.0f : 0.0f,
        static_cast<float>(next_modulation.shape),
        next_modulation.beats_per_cycle,
        next_modulation.depth,
        static_cast<float>(next_modulation.target),
        next_modulation.lfo2_enabled ? 1.0f : 0.0f,
        static_cast<float>(next_modulation.lfo2_shape),
        next_modulation.lfo2_beats_per_cycle,
        next_modulation.lfo2_depth};
    // Offsets of kParamLfoTarget, kParamLfoDepth and kParamLfo2Depth within
    // modulation_values above.
    constexpr std::size_t kModulationTargetValueIndex = 4;
    constexpr std::size_t kDepthValueIndex[kRouteLfoCount] = {3, 8};
    bool modulation_changed = false;
    bool target_lane_changed = false;
    bool depth_lane_changed[kRouteLfoCount] = {false, false};
    for (std::size_t i = 0; i < modulation_values.size(); ++i) {
        auto& cached = applied_param_cache_[detail::kSlotLfoBase + i];
        if (cached.load(std::memory_order_relaxed) != modulation_values[i]) {
            cached.store(modulation_values[i], std::memory_order_relaxed);
            modulation_changed = true;
            if (i == kModulationTargetValueIndex) target_lane_changed = true;
            for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo)
                if (i == kDepthValueIndex[lfo]) depth_lane_changed[lfo] = true;
        }
    }
    // The routing lanes: the audio owner reads them straight off the cursor,
    // so this only stamps the cache (an unstamped slot reads as drift forever)
    // and tells the editor.
    for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
        for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
            const auto& route = next_modulation.routes[lfo][t];
            const float values[2] = {route.enabled ? 1.0f : 0.0f, route.amount};
            const std::size_t slots[2] = {detail::route_enabled_slot(lfo, t),
                                          detail::route_amount_slot(lfo, t)};
            for (std::size_t k = 0; k < 2; ++k) {
                auto& cached = applied_param_cache_[slots[k]];
                if (cached.load(std::memory_order_relaxed) != values[k]) {
                    cached.store(values[k], std::memory_order_relaxed);
                    modulation_changed = true;
                }
            }
        }
    }
    if (target_lane_changed) {
        // The host moved the legacy single-target lane (4004): automation
        // written before per-LFO routing existed, or a host edit of it. It
        // must never be silently discarded, so it is honoured as the command
        // it always was -- "modulate THIS destination" -- for both LFOs (the
        // lane was shared by both). Among the four destinations it can name it
        // selects exactly that one; the viewport routes and every amount are
        // left as they are. The audio owner already plays it from the cursor
        // (see process()); these writes make the routing lanes and the editor
        // agree with what is heard.
        const auto bit = modulation_target_bit(next_modulation.target);
        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
            const std::uint8_t keep = static_cast<std::uint8_t>(
                route_mask(next_modulation.routes[lfo]) & ~kModulationTargetMaskAll);
            legacy_route_masks[lfo] = static_cast<std::uint8_t>(keep | bit);
            set_route_mask(next_modulation.routes[lfo], legacy_route_masks[lfo]);
            for (std::size_t t = 0; t < kRouteTargetCount; ++t)
                applied_param_cache_[detail::route_enabled_slot(lfo, t)].store(
                    ((legacy_route_masks[lfo] >> t) & 1u) ? 1.0f : 0.0f,
                    std::memory_order_relaxed);
        }
        next_modulation.target_mask = static_cast<std::uint8_t>(
            route_mask(next_modulation.routes[0]) & kModulationTargetMaskAll);
        legacy_target_command = true;
    }
    // The LFO-level Depth lanes (4003, 4013) are commands as well: each target
    // has its own Depth, and a host move of an LFO's Depth lane -- automation
    // written before per-target depth existed -- sets the Depth of every target
    // that LFO currently drives. Never written back.
    for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
        if (!depth_lane_changed[lfo]) continue;
        const float depth = lfo == 0 ? next_modulation.depth : next_modulation.lfo2_depth;
        for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
            auto& route = next_modulation.routes[lfo][t];
            if (!route.enabled) continue;
            route.amount = depth;
            applied_param_cache_[detail::route_amount_slot(lfo, t)].store(
                depth, std::memory_order_relaxed);
            legacy_depth_writes[legacy_depth_write_count++] =
                {lfo_route_amount_param_id(lfo, t), depth};
        }
    }
    if (modulation_changed) {
        modulation_ = next_modulation;
        sound_changed = true;
        editor_changed = true;
    }

    // Macros. The VALUE is an ordinary host lane, so a move has to stamp the
    // applied cache here like every other slot: `sample_surface_drift_`
    // sweeps all of kSurfaceCacheSlots, so a slot nothing ever stamps reports
    // drift on every single block and respawns the sync worker forever.
    //
    // Membership is not read from the store — it has no lane — so this loop
    // only reconciles values. A macro with no members is INERT: its value
    // moved, the editor should show that, but nothing audible changed and a
    // mask republish would be pure cost.
    for (std::size_t m = 0; m < kMacroCount; ++m) {
        const float value = store->get_value(macro_param_id(m));
        auto& cached = applied_param_cache_[detail::kSlotMacroBase + m];
        if (value != cached.load(std::memory_order_relaxed)) {
            cached.store(value, std::memory_order_relaxed);
            if (macro_members_[m].any()) sound_changed = true;
            editor_changed = true;
        }
    }

    if (sound_changed || editor_changed) {
        if (sound_changed) publish_processing_state_();
        host_automation_revision_.store(
            editor_authority_.record_external_mutation(),
            std::memory_order_release);
    }
    lock.unlock();
    // Outside the lock: set_value fires listeners that may read processor
    // state back. The cache was stamped above with these exact values, so the
    // writes read as applied rather than as fresh drift. No gesture: this is
    // the processor following a host-driven lane, not a user edit to record.
    if (legacy_target_command) {
        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo)
            for (std::size_t t = 0; t < kRouteTargetCount; ++t)
                store->set_value(lfo_route_enabled_param_id(lfo, t),
                                 ((legacy_route_masks[lfo] >> t) & 1u) ? 1.0f : 0.0f);
    }
    for (std::size_t k = 0; k < legacy_depth_write_count; ++k)
        store->set_value(legacy_depth_writes[k].id, legacy_depth_writes[k].value);
    return sound_changed || editor_changed;
}

float Spectr::editor_mode_param(pulp::state::ParamID id) const noexcept {
    if (id < kParamMotionMode || id > kParamVisualization) return 0.0f;
    const auto slot = detail::kSlotModeBase
        + static_cast<std::size_t>(id - kParamMotionMode);
    return applied_param_cache_[slot].load(std::memory_order_relaxed);
}

ModulationSettings Spectr::modulation_settings() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return modulation_;
}

bool Spectr::morph_applies_viewport() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return morph_applies_viewport_;
}

void Spectr::set_morph_applies_viewport(bool enabled) noexcept {
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        if (morph_applies_viewport_ == enabled) return;
        morph_applies_viewport_ = enabled;
        // Publish so the audio thread's own morph derivation agrees with the
        // editor's on the very next block. Turning the switch OFF deliberately
        // leaves `viewport_` exactly where it is: the user keeps looking at
        // (and hearing) the window they are on, and nothing they captured is
        // lost — re-enabling resumes from the live morph value.
        publish_audio_modulation_state_();
    }
}

bool Spectr::keyboard_shortcuts_in_daw() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return keyboard_shortcuts_in_daw_;
}

void Spectr::set_keyboard_shortcuts_in_daw(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    keyboard_shortcuts_in_daw_ = enabled;
}

bool Spectr::set_modulation_target_mask(std::uint8_t mask) noexcept {
    // Legacy "Destinations" selection (both LFOs, the four field
    // destinations). It is now expressed as the per-LFO routing lanes, each
    // written as its own host gesture so a host records it; the viewport
    // routes are left as they are.
    mask = static_cast<std::uint8_t>(mask & kModulationTargetMaskAll);
    std::array<std::uint8_t, kRouteLfoCount> masks{};
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
            masks[lfo] = static_cast<std::uint8_t>(
                (route_mask(modulation_.routes[lfo]) & ~kModulationTargetMaskAll)
                | mask);
            set_route_mask(modulation_.routes[lfo], masks[lfo]);
        }
        modulation_.target_mask = mask;
    }
    if (param_store_) {
        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo)
            for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
                const float value = ((masks[lfo] >> t) & 1u) ? 1.0f : 0.0f;
                const auto id = lfo_route_enabled_param_id(lfo, t);
                // Only lanes that change: an unchanged lane is no edit, and a
                // gesture on it would read as a touch in a host's Latch mode.
                if (param_store_->get_value(id) == value) continue;
                push_surface_param_(id, detail::route_enabled_slot(lfo, t),
                                    value, /*emit_gesture=*/true);
            }
    }
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        publish_audio_modulation_state_();
    }
    host_automation_revision_.store(
        editor_authority_.record_external_mutation(),
        std::memory_order_release);
    return true;
}

MacroMembership<kMaxBands> Spectr::macro_members(
    std::size_t macro) const noexcept {
    if (macro >= kMacroCount) return {};
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return macro_members_[macro];
}

bool Spectr::set_macro_members(
    std::size_t macro, const MacroMembership<kMaxBands>& members) noexcept {
    if (macro >= kMacroCount) return false;
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        if (macro_members_[macro] == members) return true;
        macro_members_[macro] = members;
        // Republish the whole processing state, not just the modulation
        // publication: changing membership changes the AUDIBLE field, because
        // the macro's current value now reaches a different set of bands. A
        // macro sitting at +6 dB that gains a member must lift it on the very
        // next block, exactly as it would have had the member been assigned
        // before the value moved.
        publish_processing_state_();
    }
    host_automation_revision_.store(
        editor_authority_.record_external_mutation(),
        std::memory_order_release);
    return true;
}

BandMacroBank Spectr::macro_bank_locked_() const noexcept {
    BandMacroBank bank;
    bank.members = macro_members_;
    const auto* store = param_store_;
    if (!store) return bank;
    for (std::size_t m = 0; m < kMacroCount; ++m)
        bank.values[m] = store->get_value(macro_param_id(m));
    return bank;
}

BandMacroBank Spectr::macro_bank() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return macro_bank_locked_();
}

bool Spectr::set_macro_value(std::size_t macro, float value_db) noexcept {
    if (macro >= kMacroCount) return false;
    if (!param_store_) return false;
    if (!std::isfinite(value_db)) return false;
    const float clamped = std::clamp(value_db, kBandGainMinDb, kBandGainMaxDb);
    // Clamped BEFORE the push so the applied cache records the value the
    // store will actually hold. push_surface_param_ mirrors what it is given,
    // and an out-of-range mirror would read as permanent drift.
    push_surface_param_(macro_param_id(macro),
                        detail::kSlotMacroBase + macro, clamped,
                        /*emit_gesture=*/true);
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        // Republish here rather than leaving it to the sync worker: the push
        // above stamped the applied cache, so `apply_surface_params` will see
        // no drift and would never republish the mask this value changed.
        // An unassigned macro changes nothing audible, so it skips the
        // redesign and only advances the editor revision below.
        if (macro_members_[macro].any()) publish_processing_state_();
    }
    host_automation_revision_.store(
        editor_authority_.record_external_mutation(),
        std::memory_order_release);
    return true;
}

void Spectr::push_surface_param_(pulp::state::ParamID id, std::size_t slot,
                                 float value, bool emit_gesture) {
    auto* store = param_store_;
    if (!store) return;
    const bool in_epoch = param_gesture_epoch_open_;
    if (in_epoch
        && std::find(epoch_gesture_params_.begin(), epoch_gesture_params_.end(), id)
               == epoch_gesture_params_.end()) {
        // One host gesture bracket per touched parameter per drag. The
        // matching end_gesture runs in end_param_gesture_epoch().
        store->begin_gesture(id);
        epoch_gesture_params_.push_back(id);
    } else if (!in_epoch && emit_gesture) {
        // Discrete commands and the materialized editor's atomic state
        // publications do not carry an explicit drag epoch. They still need a
        // complete host gesture or hosts have no touch boundary to record.
        store->begin_gesture(id);
    }
    store->set_value(id, value);
    applied_param_cache_[slot].store(value, std::memory_order_relaxed);
    if (!in_epoch && emit_gesture) store->end_gesture(id);
}

void Spectr::sync_params_from_field(bool emit_gestures) noexcept {
    if (!param_store_) return;

    // Compute the delta against the last-pushed state and advance the delta
    // base under one lock hold, so a concurrent drain cannot interleave
    // between the snapshot and the mirror update. The store writes happen
    // after release: set_value fires listeners that may read processor
    // state back, and holding the lock across them would self-deadlock.
    struct PendingPush {
        pulp::state::ParamID id;
        std::size_t slot;
        float value;
    };
    std::array<PendingPush, detail::kSurfaceSlots> pending{};
    std::size_t n = 0;

    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            const float gain = field_.bands[i].gain_db;
            if (gain != synced_field_.bands[i].gain_db) {
                synced_field_.bands[i].gain_db = gain;
                pending[n++] = {band_gain_param_id(i), i, gain};
                if (morph_derived_) morph_overrides_.set(i);
            }
            const float muted = field_.bands[i].muted ? 1.0f : 0.0f;
            if (field_.bands[i].muted != synced_field_.bands[i].muted) {
                synced_field_.bands[i].muted = field_.bands[i].muted;
                pending[n++] = {band_mute_param_id(i), 64 + i, muted};
                if (morph_derived_) morph_overrides_.set(i);
            }
        }
        if (viewport_.min_hz != synced_viewport_.min_hz
            || viewport_.max_hz != synced_viewport_.max_hz) {
            synced_viewport_ = viewport_;
            const auto [center, width] = encode_viewport(viewport_);
            pending[n++] = {kParamViewportCenter, detail::kSlotCenter, center};
            pending[n++] = {kParamViewportWidth, detail::kSlotWidth, width};
        }
        if (layout_ != synced_layout_) {
            synced_layout_ = layout_;
            pending[n++] = {kParamBandCount, detail::kSlotBandCount,
                            param_value_from_layout(layout_)};
        }
    }

    for (std::size_t k = 0; k < n; ++k) {
        push_surface_param_(pending[k].id, pending[k].slot, pending[k].value,
                            emit_gestures);
    }
}

bool Spectr::set_freeze_from_editor(bool frozen) noexcept {
    auto* store = param_store_;
    if (!store) return false;
    // Its own bracket even inside an open drag epoch: a press is a discrete
    // command, and the epoch closes only the parameters its drag touched.
    store->begin_gesture(kParamFreeze);
    store->set_value(kParamFreeze, frozen ? 1.0f : 0.0f);
    store->end_gesture(kParamFreeze);
    return true;
}

bool Spectr::is_editor_plain_param(pulp::state::ParamID id) noexcept {
    return id == kMix || id == kOutputTrim
        || (id >= kParamLfoEnabled && id <= kParamLfoTarget)
        || (id >= kParamLfo2Enabled && id <= kParamLfo2Depth)
        || is_lfo_route_param(id);
}

bool Spectr::edit_param_from_editor(pulp::state::ParamID id,
                                    float value) noexcept {
    auto* store = param_store_;
    if (!store || !is_editor_plain_param(id) || !std::isfinite(value))
        return false;
    const bool in_drag =
        std::find(editor_param_gestures_.begin(), editor_param_gestures_.end(), id)
        != editor_param_gestures_.end();
    // The value only, with no applied-cache stamp: these lanes are read by the
    // audio owner straight off the store, and the sync worker's drift sweep
    // is what republishes the modulation settings and advances the editor's
    // live projection -- exactly the path a host write takes.
    if (!in_drag) store->begin_gesture(id);
    store->set_value(id, value);
    if (!in_drag) store->end_gesture(id);
    return true;
}

bool Spectr::begin_editor_param_gesture(pulp::state::ParamID id) noexcept {
    auto* store = param_store_;
    if (!store || !is_editor_plain_param(id)) return false;
    if (std::find(editor_param_gestures_.begin(), editor_param_gestures_.end(), id)
        != editor_param_gestures_.end())
        return true;
    store->begin_gesture(id);
    editor_param_gestures_.push_back(id);
    return true;
}

bool Spectr::end_editor_param_gesture(pulp::state::ParamID id) noexcept {
    auto* store = param_store_;
    if (!store || !is_editor_plain_param(id)) return false;
    const auto it = std::find(editor_param_gestures_.begin(),
                              editor_param_gestures_.end(), id);
    if (it == editor_param_gestures_.end()) return true;
    editor_param_gestures_.erase(it);
    store->end_gesture(id);
    return true;
}

void Spectr::end_editor_param_gestures() noexcept {
    auto* store = param_store_;
    if (store)
        for (const auto id : editor_param_gestures_) store->end_gesture(id);
    editor_param_gestures_.clear();
}

void Spectr::begin_param_gesture_epoch() noexcept {
    // UI thread only (EditorAuthority). Re-entrant-safe: a stale epoch from
    // a cancelled realm is closed, not stacked.
    end_param_gesture_epoch();
    param_gesture_epoch_open_ = true;
}

void Spectr::end_param_gesture_epoch() noexcept {
    auto* store = param_store_;
    if (store) {
        for (const auto id : epoch_gesture_params_) store->end_gesture(id);
    }
    epoch_gesture_params_.clear();
    param_gesture_epoch_open_ = false;
}

} // namespace spectr
