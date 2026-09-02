#include "spectr/param_surface.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

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

// Group ids (StateStore ParamGroup). The pinned SDK's adapters do not yet
// surface groups to hosts; they are registered so the contract exists when
// they do, and the zero-padded names carry the ordering today.
constexpr int kGroupGlobal    = 1;
constexpr int kGroupBandGain  = 2;
constexpr int kGroupBandMute  = 3;
constexpr int kGroupSnapshots = 4;
constexpr int kGroupViewport  = 5;
constexpr int kGroupModes     = 6;
constexpr int kGroupModulation= 7;

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
        info.unit = "beats";
        info.range = {0.25f, 16.0f, 4.0f};
        info.group_id = kGroupModulation;
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfoDepth;
        info.name = "LFO Depth";
        info.range = {0.0f, 1.0f, 0.5f};
        info.group_id = kGroupModulation;
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
        info.unit = "beats";
        info.range = {0.25f, 16.0f, 4.0f};
        info.group_id = kGroupModulation;
        store.add_parameter(info);
    }
    {
        pulp::state::ParamInfo info;
        info.id = kParamLfo2Depth;
        info.name = "LFO 2 Depth";
        info.range = {0.0f, 1.0f, 0.0f};
        info.group_id = kGroupModulation;
        store.add_parameter(info);
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
    (void)static_cast<Spectr*>(ctx)->apply_surface_params(/*apply_morph=*/true);
}

bool Spectr::surface_params_drifted_() const noexcept {
    const auto* store = param_store_;
    if (!store) return false;
    for (std::size_t slot = 0; slot < kSurfaceCacheSlots; ++slot) {
        if (store->get_value(detail::surface_slot_param_id(slot))
                != applied_param_cache_[slot].load(std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

bool Spectr::apply_surface_params(bool apply_morph) noexcept {
    auto* store = param_store_;
    if (!store) return false;

    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    bool sound_changed = false;
    bool editor_changed = false;

    // Apply morph before individual band lanes. A host can automate morph and
    // a band in the same block; the explicit band value must remain reflected
    // in canonical state (and become a sparse override), rather than being
    // marked applied and then silently overwritten by a later morph pass.
    if (apply_morph) {
        const float t = store->get_value(kParamMorph);
        if (t != applied_param_cache_[detail::kSlotMorph].load(std::memory_order_relaxed)) {
            applied_param_cache_[detail::kSlotMorph].store(t, std::memory_order_relaxed);
            const bool has_a = snapshots_.has(SnapshotBank::Slot::A);
            const bool has_b = snapshots_.has(SnapshotBank::Slot::B);
            if (has_a && has_b) {
                morph_fields(field_, snapshots_.a.field, snapshots_.b.field, t);
                sound_changed = true;
            } else if (has_a || has_b) {
                field_ = has_a ? snapshots_.a.field : snapshots_.b.field;
                sound_changed = true;
            }
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

    ModulationSettings next_modulation;
    next_modulation.enabled = store->get_value(kParamLfoEnabled) >= 0.5f;
    next_modulation.shape = static_cast<LfoShape>(std::clamp(
        static_cast<int>(std::lround(store->get_value(kParamLfoShape))), 0, 3));
    next_modulation.beats_per_cycle = std::clamp(
        store->get_value(kParamLfoRate), 0.25f, 16.0f);
    next_modulation.depth = std::clamp(
        store->get_value(kParamLfoDepth), 0.0f, 1.0f);
    next_modulation.target = static_cast<ModulationTarget>(std::clamp(
        static_cast<int>(std::lround(store->get_value(kParamLfoTarget))), 0, 3));
    next_modulation.lfo2_enabled = store->get_value(kParamLfo2Enabled) >= 0.5f;
    next_modulation.lfo2_shape = static_cast<LfoShape>(std::clamp(
        static_cast<int>(std::lround(store->get_value(kParamLfo2Shape))), 0, 3));
    next_modulation.lfo2_beats_per_cycle = std::clamp(
        store->get_value(kParamLfo2Rate), 0.25f, 16.0f);
    next_modulation.lfo2_depth = std::clamp(
        store->get_value(kParamLfo2Depth), 0.0f, 1.0f);
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
    bool modulation_changed = false;
    for (std::size_t i = 0; i < modulation_values.size(); ++i) {
        auto& cached = applied_param_cache_[detail::kSlotLfoBase + i];
        if (cached.load(std::memory_order_relaxed) != modulation_values[i]) {
            cached.store(modulation_values[i], std::memory_order_relaxed);
            modulation_changed = true;
        }
    }
    if (modulation_changed) {
        modulation_ = next_modulation;
        sound_changed = true;
        editor_changed = true;
    }

    if (sound_changed || editor_changed) {
        if (sound_changed) publish_processing_state_();
        host_automation_revision_.store(
            editor_authority_.record_external_mutation(),
            std::memory_order_release);
    }
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

bool Spectr::set_modulation_target_mask(std::uint8_t mask) noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    modulation_.target_mask = static_cast<std::uint8_t>(mask & 0x0f);
    publish_audio_modulation_state_();
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
