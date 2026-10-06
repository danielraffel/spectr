#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
#include <spectr/experimental/shared_spectral_renderer.hpp>
#endif
#include "spectr/spectr.hpp"
#include "spectr/test_seams.hpp"

#include <pulp/runtime/trace.hpp>
#include <pulp/format/param_processing.hpp>
#if !defined(SPECTR_NATIVE_EDITOR)
#include "spectr/ui/editor_view.hpp"
#endif

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>
#include <choc/memory/choc_Base64.h>
#include <cstring>
#include <pulp/runtime/log.hpp>
#include <cassert>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <limits>
#include <string>
#include <cstdlib>
#include <string_view>

namespace spectr {

namespace {

/// Negative-control seam for the route ramp: `SPECTR_MODULATION_PLANT=route-step`
/// switches destinations and amounts in one block, the behaviour before routes
/// were slewed. Read once per process; unset in every shipping run.
bool modulation_plants_route_step() noexcept {
    static const bool planted = [] {
        const char* value = SPECTR_TEST_ENV("SPECTR_MODULATION_PLANT");
        return value != nullptr && std::string_view(value) == "route-step";
    }();
    return planted;
}

// SPECTR_MODULATION_PLANT=level-target-step: the Output destination's gain
// lands once per block instead of ramping across it, the per-block zipper the
// ramp exists to prevent. The Output-target smoothness gate must fail with it.
bool modulation_plants_level_target_step() noexcept {
    static const bool planted = [] {
        const char* value = SPECTR_TEST_ENV("SPECTR_MODULATION_PLANT");
        return value != nullptr && std::string_view(value) == "level-target-step";
    }();
    return planted;
}

// Every negative-control seam the audio thread reads. Called from the
// constructor and from prepare(), never from process().
// SPECTR_PLANT_CALLBACK_BURST=<iterations>: once every 512 samples, burn a
// fixed amount of arithmetic in the callback -- the bursty per-hop work a
// deadline gate exists to catch. Negative control only; read once, on the
// control thread (prepare primes it).
long callback_burst_plant() noexcept {
    static const long iterations = [] {
        const char* v = SPECTR_TEST_ENV("SPECTR_PLANT_CALLBACK_BURST");
        return v ? std::atol(v) : 0L;
    }();
    return iterations;
}

void prime_negative_control_seams() noexcept {
    (void)callback_burst_plant();
    (void)FreezeSource::prime_plants();
    AutoGainMaterial::prime_plants();
    (void)modulation_plants_route_step();
    (void)modulation_plants_level_target_step();
    (void)level_plant("");
}

// See set_editor_is_standalone: asserted by the standalone entry points only.
std::atomic<bool> g_editor_is_standalone{false};
}  // namespace

void set_editor_is_standalone(bool value) {
    g_editor_is_standalone.store(value, std::memory_order_relaxed);
}

bool editor_is_standalone() {
    return g_editor_is_standalone.load(std::memory_order_relaxed);
}

#if !defined(__APPLE__)
// Only AppKit withholds a click that lands in a non-key window from the view.
int install_host_view_first_mouse() { return 0; }
#endif

namespace {

/// Are these two layouts the same mask?
///
/// Bitwise on purpose: the question is whether re-staging would produce an
/// identical impulse response, and anything short of exact equality can. Only
/// the bands the layout declares active are compared; the array's tail is not
/// part of the mask.
[[nodiscard]] bool same_mask_layout_(
    const pulp::signal::SpectralBandLayout& a,
    const pulp::signal::SpectralBandLayout& b) noexcept {
    if (a.active_bands != b.active_bands) return false;
    if (a.min_hz != b.min_hz || a.max_hz != b.max_hz) return false;
    if (a.spacing != b.spacing) return false;
    if (a.edge_policy != b.edge_policy) return false;
    if (a.boundary_kernel != b.boundary_kernel) return false;
    if (a.transition_fraction != b.transition_fraction) return false;
    if (a.transition_frames != b.transition_frames) return false;
    for (std::uint32_t i = 0; i < a.active_bands; ++i) {
        if (a.bands[i].gain_db != b.bands[i].gain_db) return false;
        if (a.bands[i].muted   != b.bands[i].muted)   return false;
    }
    return true;
}

} // namespace


Spectr::Spectr() : editor_authority_(*this) {
    prime_negative_control_seams();
    auto_gain_tap_.bind(&freeze_source_, &auto_gain_material_);
#if defined(SPECTR_NATIVE_EDITOR)
    pulp::view::CommandInfo settings;
    settings.id = kOpenSettingsCommand;
    settings.name = "Settings\u2026";
    settings.category = "App";
    settings.default_key = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    settings.default_modifiers = pulp::view::kModCmd;
#else
    settings.default_modifiers = pulp::view::kModCtrl;
#endif
    native_command_registry_.register_command(settings);

    pulp::view::CommandInfo undo;
    undo.id = kUndoCommand;
    undo.name = "Undo";
    undo.category = "Edit";
    undo.default_key = pulp::view::KeyCode::z;
#if defined(__APPLE__)
    undo.default_modifiers = pulp::view::kModCmd;
#else
    undo.default_modifiers = pulp::view::kModCtrl;
#endif
    native_command_registry_.register_command(undo);

    pulp::view::CommandInfo redo;
    redo.id = kRedoCommand;
    redo.name = "Redo";
    redo.category = "Edit";
    redo.default_key = pulp::view::KeyCode::z;
#if defined(__APPLE__)
    redo.default_modifiers = pulp::view::kModCmd | pulp::view::kModShift;
#else
    redo.default_modifiers = pulp::view::kModCtrl | pulp::view::kModShift;
#endif
    native_command_registry_.register_command(redo);
    native_command_registry_.add_handler(this);
#endif
}

Spectr::~Spectr() {
    switch_reclaim_lane_.stop();
#if defined(SPECTR_NATIVE_EDITOR)
    native_command_registry_.remove_handler(this);
#endif
}

pulp::format::PluginDescriptor Spectr::descriptor() const {
    auto descriptor=make_descriptor();
    // A held spectrum sounds for as long as it is held, with or without
    // input, so a host must not stop processing on silence. The tail is
    // infinite always, on every format, rather than only while frozen.
    //
    // A tail that followed Freeze had to be announced on each edge, from the
    // audio thread, in the very callback a tap lands in: AU v2 turns that
    // into kAudioUnitProperty_TailTime listener calls on the render thread
    // (an out-of-process host such as Logic forwards them across the process
    // boundary), and VST3 into restartComponent(kReloadComponent), which
    // JUCE-based hosts answer with a full release()+prepare(). A constant
    // tail has nothing to announce. Every format adapter reads a negative
    // tail as infinite.
    descriptor.tail_samples = -1;
    return descriptor;
}

namespace {

constexpr std::size_t kLayoutCount = 5;
constexpr std::array<Layout, kLayoutCount> kLayoutValues = {
    Layout::Bands32, Layout::Bands40, Layout::Bands48,
    Layout::Bands56, Layout::Bands64,
};

int layout_to_index(Layout L) noexcept {
    for (std::size_t i = 0; i < kLayoutCount; ++i) {
        if (kLayoutValues[i] == L) return static_cast<int>(i);
    }
    return 0;
}

} // namespace

void Spectr::define_parameters(pulp::state::StateStore& store) {
    store.add_parameter({
        .id    = kMix,
        .name  = "Mix",
        .unit  = "%",
        .range = {0.0f, 100.0f, 100.0f},
        .group_id = 1,
    });
    store.add_parameter({
        .id    = kOutputTrim,
        .name  = "Output",
        .unit  = "dB",
        .range = {-24.0f, 24.0f, 0.0f},
        .group_id = 1,
    });
    // Level controls (level_controls.hpp; IDs 5000..5009 reserved). Static
    // like every other lane: registered unconditionally, appended, never moved.
    store.add_parameter({
        .id    = kParamIntensity,
        .name  = "Intensity",
        .unit  = "%",
        .range = {kIntensityMinPercent, kIntensityMaxPercent,
                  kIntensityDefaultPercent},
        .group_id = 1,
    });
    {
        pulp::state::ParamInfo info;
        info.id = kParamAutoGain;
        info.name = "Auto Gain";
        info.range = {0.0f, 1.0f, kAutoGainDefaultForNewInstances ? 1.0f : 0.0f, 1.0f};
        info.group_id = 1;
        info.kind = pulp::state::ParamKind::Toggle;
        info.to_string = [](float v) { return std::string(v >= 0.5f ? "On" : "Off"); };
        info.from_string = [](const std::string& text) {
            return (text == "On" || text == "on" || text == "1") ? 1.0f : 0.0f;
        };
        store.add_parameter(info);
    }

    // spectr#34 — the full static host-automation surface (64 band gains,
    // 64 band mutes, morph, viewport center/width, band count, 4 modes).
    register_surface_params(store);
    param_store_ = &store;
    // Mirror the registered defaults so the process()-side drift sweep
    // starts quiet: cache == store means "nothing to apply".
    for (std::size_t slot = 0; slot < kSurfaceCacheSlots; ++slot) {
        applied_param_cache_[slot].store(
            store.get_value(detail::surface_slot_param_id(slot)),
            std::memory_order_relaxed);
    }

    // Wire ABCompare now that the store reference is live. Keeps the
    // StateStore-side A/B under pulp::view::ABCompare and the band-field
    // side under SnapshotBank — UI drives both together.
    ab_ = std::make_unique<pulp::view::ABCompare>(&store);
}

// ── Snapshot A/B (Milestone 8) ─────────────────────────────────────────

void Spectr::capture_snapshot(SnapshotBank::Slot slot) noexcept {
    // The sync worker reads the bank when a host-side morph write lands, so
    // capture serializes against the same lock as every field_/bank access.
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    snapshots_.capture_into(slot, field_, viewport_, layout_);
    publish_audio_modulation_state_();
}

void Spectr::clear_snapshot(SnapshotBank::Slot slot) noexcept {
    // Same lock as capture, for the same reason: the sync worker reads the
    // bank when a host-side morph write lands, so emptying a slot has to
    // serialize against every other field_/bank access.
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    snapshots_.clear(slot);
    publish_audio_modulation_state_();
}

void Spectr::apply_morph_to_live(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        const bool has_a = snapshots_.has(SnapshotBank::Slot::A);
        const bool has_b = snapshots_.has(SnapshotBank::Slot::B);
        if (!has_a && !has_b) return;
        if (!has_a) { field_ = snapshots_.b.field; }
        else if (!has_b) { field_ = snapshots_.a.field; }
        else { morph_fields(field_, snapshots_.a.field, snapshots_.b.field, t); }
        // The viewport rides the same derivation as the bands so the window
        // and the shape drawn inside it can never disagree. `synced_viewport_`
        // advances in lockstep below for the same reason the band values do
        // not push: a derived value must not become an authored host write.
        if (morph_applies_viewport_) {
            if (!has_a) viewport_ = snapshots_.b.viewport;
            else if (!has_b) viewport_ = snapshots_.a.viewport;
            else viewport_ = morph_viewports(snapshots_.a.viewport,
                                             snapshots_.b.viewport, t);
        }
        // The morph moves the morph PARAMETER only — pushing the 64 resulting
        // band values as parameter writes would flood the host per slider
        // move and double-drive the field on automation playback (the morph
        // lane would recompute what the band lanes replay). The synced mirror
        // still advances, so a subsequent band edit pushes only its own delta.
        synced_field_ = field_;
        if (morph_applies_viewport_) synced_viewport_ = viewport_;
        morph_derived_ = true;
        morph_overrides_.reset();
        // Published LAST, so the snapshot the audio thread reads carries the
        // derived-ness of the field it is being handed. Publishing before
        // these two flags ships a state that says the field was NOT derived
        // while shipping the derived field itself — harmless while nothing
        // read the flag, and a silently un-morphed DSP once something did.
        publish_processing_state_();
    }
    push_surface_param_(detail::surface_slot_param_id(detail::kSlotMorph),
                        detail::kSlotMorph, t);
}

void Spectr::replace_field(const BandField& field) noexcept {
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        field_ = field;
        publish_processing_state_();
    }
    sync_params_from_field();
}

ProcessingStateSnapshot Spectr::processing_state_snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return {field_, viewport_, layout_, snapshots_};
}

bool Spectr::replace_processing_state(const BandField& field,
                                      const Viewport& viewport,
                                      Layout layout) noexcept {
    PULP_TRACE_SCOPE_NAMED("state", "spectr_replace_processing_state");
    if (!viewport.valid()) return false;
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        field_ = field;
        viewport_ = viewport;
        layout_ = layout;
        {
            PULP_TRACE_SCOPE_NAMED("state", "spectr_mask_publish");
            publish_processing_state_();
        }
    }
    {
        PULP_TRACE_SCOPE_NAMED("state", "spectr_host_param_sync");
        sync_params_from_field();
    }
    return true;
}

void Spectr::publish_field() noexcept {
    // DSP publish only; callers that mutate field_ decide whether the change
    // also reaches the host parameters (sync_params_from_field). Morph is
    // the deliberate exception — see apply_morph_to_live.
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    publish_processing_state_();
}

Spectr::OutputLevelReading Spectr::read_output_level() {
    // The metering the audio thread already computes over the buffer it just
    // handed the host. Reading it here costs a triple-buffer read and no DSP:
    // there is no second analysis path and nothing new runs on the audio
    // thread for this readout to exist.
    const auto& meter = read_meter();
    OutputLevelReading reading;
    reading.trim_db = param_store_
        ? param_store_->get_value(kOutputTrim)
        : 0.0f;
    if (param_store_) {
        reading.intensity_percent = param_store_->get_value(kParamIntensity);
        reading.auto_gain = param_store_->get_value(kParamAutoGain) >= 0.5f;
        reading.mix_percent = param_store_->get_value(kMix);
    }
    reading.auto_gain_db = auto_gain_applied_db();

    float peak = 0.0f;
    bool  over = false;
    const auto channels = std::clamp(
        meter.num_channels, 0,
        static_cast<int>(pulp::signal::kMaxMeterChannels));
    for (int ch = 0; ch < channels; ++ch) {
        const auto value = meter.channels[static_cast<std::size_t>(ch)].peak;
        if (std::isfinite(value)) peak = std::max(peak, value);
        over = over || meter.channels[static_cast<std::size_t>(ch)].clipped;
    }
    // `clipped` is the audio thread's own sample-level verdict and is the
    // authority; the peak comparison only covers a frame whose peak landed on
    // full scale exactly. Deriving `over` from the dB figure alone would make
    // the flag a function of this rounding rather than of the samples.
    over = over || peak >= 1.0f;

    reading.peak_db = peak > 0.0f
        ? 20.0f * std::log10(peak)
        : -std::numeric_limits<float>::infinity();
    reading.over = over;
    return reading;
}

bool Spectr::set_editor_mode_param(pulp::state::ParamID id,
                                   float value) noexcept {
    if (!param_store_ || id < kParamMotionMode || id > kParamVisualization)
        return false;
    const auto slot = detail::kSlotModeBase
        + static_cast<std::size_t>(id - kParamMotionMode);
    push_surface_param_(id, slot, value);
    return true;
}

pulp::signal::SpectralBandLayout Spectr::make_mask_layout_() const noexcept {
    pulp::signal::SpectralBandLayout mask_layout;
    mask_layout.active_bands = static_cast<std::uint32_t>(visible_count(layout_));
    mask_layout.min_hz = viewport_.min_hz;
    mask_layout.max_hz = viewport_.max_hz;
    mask_layout.spacing = pulp::signal::SpectralBandSpacing::logarithmic;
    // Preserve Periscope-style edge ownership: the first band owns bins below
    // the focused viewport (including DC), and the last owns bins above it
    // (including Nyquist). Muting those categorical edge bands makes the
    // viewport an exact isolation boundary; leaving them open retains the
    // exterior signal at their selected gain.
    mask_layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    mask_layout.boundary_kernel = pulp::signal::SpectralMaskBoundaryKernel::hard;
    mask_layout.transition_fraction = 0.0f;
    mask_layout.transition_frames = 0;
    // The macro overlay composes here rather than in field_, because it is
    // non-destructive: canonical state stays exactly what the user drew, and
    // the macros are added on the way to the mask. The audio owner runs the
    // SAME function over the same inputs a few lines further down its own
    // path, which is what makes the control-published mask and the
    // audio-staged one agree.
    BandField audible = field_;
    apply_macro_offsets(audible, mask_layout.active_bands, macro_bank_locked_());
    for (std::size_t i = 0; i < mask_layout.active_bands; ++i) {
        mask_layout.bands[i].gain_db = audible.bands[i].gain_db;
        mask_layout.bands[i].muted = audible.bands[i].muted;
    }
    // Intensity, last, exactly as the audio owner applies it to the mask it
    // stages, so the two publications agree once the slew has settled. An
    // identity at 100 %.
    if (param_store_)
        apply_intensity(mask_layout,
                        intensity_factor(param_store_->get_value(kParamIntensity)));
    return mask_layout;
}

bool Spectr::spectral_resolution(
    pulp::signal::SpectralBandResolution& out_resolution) const noexcept {
    if (!processor_prepared_) return false;
    // make_mask_layout_ reads field_/viewport_/layout_; hold the same lock
    // the writers (UI, sync worker, restore) serialize against.
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    // The grid the LIVE renderer designs against, published as a value under
    // this same lock rather than read back through renderer_. Both shipped
    // modes sample the same 8192 grid, so the build-time constant gives the
    // right answer today -- by coincidence, not by construction, and the
    // disclosure would start lying the moment a realisation changed its grid.
    // It is a value and not a pointer dereference because a mode switch can
    // replace the renderer from another control thread, and this runs on the
    // editor's.
    return pulp::signal::analyze_spectral_band_resolution(
        make_mask_layout_(), active_design_grid_,
        static_cast<float>(sample_rate_), out_resolution);
}

void Spectr::publish_audio_modulation_state_() noexcept {
    // All callers serialize through processing_state_mutex_. TripleBuffer
    // therefore has one logical writer and process() remains its sole reader.
    // Designated rather than positional. This struct just grew a field, and a
    // positional brace-init is the shape that fails SILENTLY when a later
    // change reorders one: every member here is a snapshot of control state,
    // several are bools and integers, and the compiler would accept two of
    // them swapped. Naming them makes a reorder a compile error instead of a
    // morph that quietly reads the viewport switch.
    AudioModulationState published{
        .settings = modulation_,
        .snapshots = snapshots_,
        .morph_applies_viewport = morph_applies_viewport_,
        .morph_derived = morph_derived_,
        .morph_overrides =
            morph_derived_ ? morph_overrides_.to_ullong() : 0ull,
        .macro_members = {},
        .preset = preset_neighbours_};
    for (std::size_t m = 0; m < kMacroCount; ++m)
        published.macro_members[m] = macro_members_[m].to_ullong();
    audio_modulation_publication_.write(published);
}

thread_local Spectr::SyncPublishOrder Spectr::t_sync_publish{};

void Spectr::publish_processing_state_() noexcept {
    publish_audio_modulation_state_();
    auto mask_layout = make_mask_layout_();

    if (!processor_prepared_) return;
    if (!renderer_) return;
    // Republishing a mask the renderer is already realising buys nothing and
    // costs a redesign -- a full cepstral reconstruction on the design grid --
    // so skip it. The sync worker observes drift often and most of it resolves
    // to the same mask, which makes this the common path rather than a corner.
    //
    // It is a COST gate, not a correctness one, and the difference is worth
    // stating because the earlier comment here drew the wrong conclusion from
    // the right observation. Republishing an unchanged mask really did perturb
    // the audio, but not because blending an impulse response with a copy of
    // itself is inexact -- it is exact. It was that a crossfade used to install
    // the incoming response with a ZEROED input delay line, so any swap,
    // changed or not, restarted the convolution from silence. The convolver
    // now keeps ONE input history for every response it renders, so both sides
    // of a fade convolve against the same real past and an unchanged
    // republication costs nothing measurable -- 46 of them sit on the
    // renderer's arithmetic floor. Reproducibility does not rest on this gate.
    // See `kIrCrossfadeSamples` and the swap rules in
    // `test/test_mask_renderer.cpp`.
    if (last_published_layout_valid_
        && same_mask_layout_(last_published_layout_, mask_layout))
        return;
    bool superseded = false;
    const bool ordered = t_sync_publish.renderer == renderer_.get()
        && t_sync_publish.ordinal != 0;
    const bool published = ordered
        ? renderer_->publish_layout_at(mask_layout, t_sync_publish.ordinal, &superseded)
        : renderer_->publish_layout(mask_layout);
    if (published && superseded) {
        param_sync_superseded_.fetch_add(1, std::memory_order_relaxed);
        // A newer request is already staged; nothing of ours was. Do not
        // remember this layout as published, or a later identical publish
        // would be skipped and never reach the audio.
        last_published_layout_valid_ = false;
        return;
    }
    if (!published) {
        last_published_layout_valid_ = false;
        // Invalid control state fails closed; never leave a stale audible
        // table active after a rejected geometry update.
        for (auto& band : mask_layout.bands) band.muted = true;
        mask_layout.min_hz = 20.0f;
        mask_layout.max_hz = std::min(20000.0f,
                                     static_cast<float>(sample_rate_ * 0.5));
        (void)renderer_->publish_layout(mask_layout);
        return;
    }
    last_published_layout_ = mask_layout;
    last_published_layout_valid_ = true;
}

pulp::view::ABCompare* Spectr::ab_compare() noexcept {
    // Constructed in define_parameters once the StateStore reference is
    // live. Callers that invoke this before define_parameters get a
    // nullptr — don't dereference without checking.
    return ab_.get();
}

MaskRendererConfig Spectr::renderer_config_() const noexcept {
    MaskRendererConfig config;
    // The design grid and hop are the product's fixed spectral geometry, not
    // anything the host chose. Latency therefore stays a function of the mode
    // alone, which is what lets a project recall with the same delay
    // compensation on a different machine and a different buffer size.
    config.design_grid_size = kSpectralFftSize;
    config.analysis_hop     = kSpectralAnalysisHop;
    config.channels         = channels_;
    config.max_block        = std::max(max_block_, 1);
    config.sample_rate      = sample_rate_;
    config.initial_mix      = std::clamp(state().get_value(kMix) / 100.0f, 0.0f, 1.0f);
    config.mix_ramp_samples = 64;
    return config;
}

GpuAudioStatus Spectr::gpu_audio_status() const {
    const bool freeze=freeze_source_wired();
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    std::lock_guard<std::mutex> lock(renderer_observation_mutex_);
    if(!renderer_)return {GpuAudioStatus::Availability::NotPrepared,{},freeze};
    const auto* shared=dynamic_cast<const experimental::SharedSpectralMaskRenderer*>(renderer_.get());
    if(!shared)return {GpuAudioStatus::Availability::NonSharedRenderer,{},freeze};
    const auto s=shared->snapshot();
    return {GpuAudioStatus::Availability::Available,
        GpuAudioStatus::Delivery{unsigned(s.state),s.epoch,s.gpu_delivered,
            s.cpu_fallback,s.cancelled,s.lost_records},freeze};
#else
    GpuAudioStatus status;
    status.freeze_available=freeze;
    return status;
#endif
}

#if defined(SPECTR_SHARED_PRODUCT_ACCEPTANCE)
std::uint64_t Spectr::shared_product_trace_run_id_stopped() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    const auto* shared=dynamic_cast<const experimental::SharedSpectralMaskRenderer*>(renderer_.get());
    return shared?shared->trace_run_id_stopped():0;
}
Spectr::SharedProductSnapshot Spectr::shared_product_snapshot() const noexcept {
    const auto* shared = dynamic_cast<const experimental::SharedSpectralMaskRenderer*>(renderer_.get());
    if (!shared) return {};
    const auto s = shared->snapshot();
    return {true, unsigned(s.state), s.epoch, s.gpu_delivered, s.cpu_fallback,
            s.cancelled, s.lost_records};
}
bool Spectr::finalize_shared_product_snapshot(SharedProductSnapshot& out) noexcept {
    stop_param_sync_lane_();
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    auto* shared = dynamic_cast<experimental::SharedSpectralMaskRenderer*>(renderer_.get());
    if (!shared) { out = {}; return false; }
    const bool confirmed = shared->release();
    out = shared_product_snapshot();
    return confirmed;
}
bool Spectr::set_shared_product_force_cpu(bool force) noexcept {
    if (processor_prepared_) return false;
    shared_product_force_cpu_ = force;
    return true;
}
#endif

namespace {
// How long a mode switch crossfades, once the incoming renderer is warm. The
// two sides carry the same material a latency apart, so this is a blend of
// uncorrelated signals: long enough not to read as a cut, short enough that
// the switch is not heard as an echo.
constexpr double kRenderSwitchFadeSeconds = 0.03;
// SPECTR_PLANT_HARD_RENDER_SWITCH restores the cut a switch used to be. Read
// on the control thread only (set_render_mode).
bool render_switch_plants_hard_cut_() noexcept {
    static const bool planted = SPECTR_TEST_ENV("SPECTR_PLANT_HARD_RENDER_SWITCH") != nullptr;
    return planted;
}
} // namespace

std::unique_ptr<MaskRenderer> Spectr::build_renderer_(MaskRenderMode mode, bool gpu) {
    renderer_builds_.fetch_add(1, std::memory_order_relaxed);
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    std::unique_ptr<MaskRenderer> renderer;
    // GPU processing is a Mixing-only choice: Tracking is always the CPU
    // minimum-phase renderer, and Mixing with GPU processing off is the CPU
    // linear-phase renderer at its own (lower) latency.
    if(mode==MaskRenderMode::linear_phase && gpu)
        renderer=std::make_unique<experimental::SharedSpectralMaskRenderer>(
#if defined(SPECTR_SHARED_PRODUCT_ACCEPTANCE)
            shared_product_force_cpu_
#endif
        );
    else renderer=make_mask_renderer(mode);
#else
    (void)gpu;
    auto renderer = make_mask_renderer(mode);
#endif
    if (!renderer) return nullptr;
    if (!renderer->prepare(renderer_config_())) return nullptr;
    // Hand the new renderer the magnitude that is already drawn, so a switch
    // does not pass through a neutral field on its way to the right one.
    //
    // make_mask_layout_ reads field_/viewport_/layout_ and does NOT lock
    // itself -- every other caller holds processing_state_mutex_ around it,
    // and this one must too: a switch runs on the control thread while the
    // editor may be writing a band. Copy the layout out under the lock and
    // publish outside it, so the renderer is never built from a half-written
    // field and the lock is not held across the design work publish_layout
    // does.
    pulp::signal::SpectralBandLayout layout;
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        layout = make_mask_layout_();
    }
    if (!renderer->publish_layout(layout)) return nullptr;
    // Record what the renderer is now realising, so neither the control nor
    // the audio path restages this same mask and pays a crossfade for it.
#if !defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    last_published_layout_ = layout;
    last_published_layout_valid_ = true;
#endif
    renderer->set_mix(std::clamp(state().get_value(kMix) / 100.0f, 0.0f, 1.0f));

    // Publishing a layout only STAGES it; a renderer adopts at its own block
    // boundary, which it reaches by processing. So a renderer handed to the
    // audio thread the instant after publish_layout would render its first
    // block through whatever it was initialised with, not through the mask the
    // user is looking at. Pump silence here, on the control thread, until it
    // reports the staged design adopted -- then reset, which the contract
    // defines as clearing streaming state while KEEPING the adopted magnitude.
    // The renderer therefore arrives live already realising the right mask and
    // with no primed samples of its own.
    //
    // Bounded, and a failure to settle is not fatal: a renderer that never
    // advances its generation still renders, just through its initial mask for
    // one block, which is strictly better than refusing the switch.
    if (renderer->active_generation() == 0) {
        const int block = std::max(1, std::min(max_block_, 512));
        const auto channels = static_cast<std::size_t>(std::max(1, channels_));
        std::vector<float> silence(static_cast<std::size_t>(block), 0.0f);
        // One scratch buffer PER channel. Every channel sharing one would be
        // an aliasing write, and although the output is discarded here, a
        // renderer is entitled to assume its output channels are distinct.
        std::vector<std::vector<float>> scratch(
            channels, std::vector<float>(static_cast<std::size_t>(block), 0.0f));
        std::vector<const float*> in(channels, silence.data());
        std::vector<float*> out(channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch) out[ch] = scratch[ch].data();
        for (int attempt = 0; attempt < 64; ++attempt) {
            if (renderer->active_generation() > 0) break;
            if (!renderer->process(in.data(), out.data(), block)) break;
        }
    }
    renderer->reset();
    // Last: the pump above runs on this (control) thread while the audio
    // thread may be running the outgoing renderer through the same source, so
    // the source is attached only once nothing here will process again.
    //
    // A renderer that refuses the source would play live input while Freeze
    // reads as engaged, so a refusal is reported, never dropped: an error in
    // the log, an assertion in a debug build, and freeze_source_wired() false.
    // The source is attached through Auto Gain v2's tap, which runs it and
    // then shows the estimator what the mask is about to shape
    // (auto_gain_material.hpp).
    bool wired = !freeze_source_.prepared();
    if (freeze_source_.prepared()) {
        wired = renderer->set_wet_source(&auto_gain_tap_);
        if (!wired) {
            pulp::runtime::log_error(
                "[Spectr] the {} renderer refused the freeze source; Freeze "
                "is unavailable in this mode",
                mode == MaskRenderMode::linear_phase ? "linear-phase" : "zero-latency");
            assert(wired && "renderer refused the freeze wet source");
        }
    }
    freeze_source_wired_.store(wired, std::memory_order_release);
    return renderer;
}

void Spectr::drain_retired_renderers_() noexcept {
    // Called only from the control thread, and only where the audio thread is
    // known to be outside process(): either it has never run, or the epoch
    // below proved it left. Freeing one of these from process() would be an
    // allocation on the audio thread.
    retired_renderers_.clear();
}

bool Spectr::set_render_mode(MaskRenderMode mode) {
    std::lock_guard<std::mutex> switch_lock(switch_mutex_);
    // A switch still crossfading is finished first, so there is only ever one
    // outgoing renderer and the mode compared below is the one being heard.
    if (processor_prepared_) settle_render_switch_();
    if (mode == render_mode_) return true;

    // Nothing is prepared yet (a host restoring a project before audio starts,
    // or a test). Record the mode; prepare() builds the matching renderer.
    if (!processor_prepared_) {
        render_mode_ = mode;
        return true;
    }
    return switch_renderer_(mode, gpu_processing_);
}

bool Spectr::set_gpu_processing(bool enabled) {
    std::lock_guard<std::mutex> switch_lock(switch_mutex_);
    if (enabled == gpu_processing_) return true;
    // The choice only selects Mixing's renderer. Unprepared, or in Tracking,
    // it is recorded for the next Mixing renderer and nothing moves now: no
    // renderer is built, no switch is settled or started, and the audio is
    // untouched (test_gpu_audio_status.cpp, tracking_gpu_choice_is_inert).
    if (!processor_prepared_ || render_mode_ != MaskRenderMode::linear_phase) {
        gpu_processing_ = enabled;
        return true;
    }
    // In Mixing it changes the renderer and the latency the host is told:
    // the same rebuild, crossfade and latency-changed path as a
    // Tracking/Mixing switch.
    settle_render_switch_();
    return switch_renderer_(render_mode_, enabled);
}

bool Spectr::switch_renderer_(MaskRenderMode mode, bool gpu) {
    // Build the replacement to completion BEFORE retiring the live one. A
    // switch that cannot be prepared must leave the running mode untouched
    // rather than drop the instance into silence.
    PULP_TRACE_SCOPE_NAMED("state", "spectr_build_renderer (control)");
    auto replacement = build_renderer_(mode, gpu);
    if (!replacement) return false;

    MaskRenderer* incoming = replacement.get();
    std::unique_ptr<MaskRenderer> outgoing;
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    {
        // Control publication uses this mutex too. Swap ownership only after
        // an old publisher has finished, then publish the latest field to the
        // new renderer before the audio pointer becomes reachable.
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        {
            std::lock_guard<std::mutex> observation_lock(renderer_observation_mutex_);
            outgoing=std::move(renderer_);
            renderer_=std::move(replacement);
        }
        render_mode_=mode;
        gpu_processing_=gpu;
        last_published_layout_valid_=false;
        publish_processing_state_();
    }
#else
    {
        std::lock_guard<std::mutex> observation_lock(renderer_observation_mutex_);
        outgoing=std::move(renderer_);
        renderer_=std::move(replacement);
    }
    render_mode_=mode;
    gpu_processing_=gpu;
#endif

    // Hand both renderers to the audio thread. It keeps rendering the old one
    // (still `active_renderer_`), warms the new one on the same input until
    // its delay line and impulse history are full, then crossfades into it
    // and publishes it as active (pulp/signal/processing_switch_crossfade.hpp). A cut here
    // was audible twice: the new renderer's own latency of silence -- 213 ms
    // into Mixing -- and a step where the old one stopped mid-waveform.
    //
    // From here every control-side publication goes to the new renderer, so
    // a mask edited during the fade is the one it fades into.
    const int history = mode == MaskRenderMode::zero_latency
        ? incoming->design_grid_size() : 0;
    switch_plan_ = pulp::signal::plan_processing_switch(
        incoming->latency_samples(), history, sample_rate_, kRenderSwitchFadeSeconds);
    // Negative control: the cut this replaced -- the new renderer heard from
    // its first, history-less sample.
    if (render_switch_plants_hard_cut_()) switch_plan_ = pulp::signal::ProcessingSwitchPlan{0, 1};
    switch_incoming_ = incoming;
    switch_outgoing_ = std::move(outgoing);
    switch_wet_wired_ = freeze_source_.prepared();
    render_switch_state_.store(kSwitchPending, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        active_design_grid_ = renderer_->design_grid_size();
    }

    // The host's delay compensation is now wrong by the difference between the
    // two modes. This is the whole reason the switch is observable to a host.
    flag_latency_changed();
    return true;
}

void Spectr::retire_switch_outgoing_() noexcept {
    std::unique_ptr<MaskRenderer> outgoing = std::move(switch_outgoing_);
    switch_incoming_ = nullptr;
    if (!outgoing) return;
    // Retire the old renderer only once the audio thread cannot still be
    // inside it. An even epoch means it is outside process() right now and
    // will re-read active_renderer_ on its next entry; a changed epoch means
    // the call that may have held the old pointer has returned. Either proves
    // the pointer is unreachable. If neither is observed in the budget below
    // the object is parked instead of freed -- late is fine, freeing it under
    // a live reader is not.
    const std::uint64_t seen = render_epoch_.load(std::memory_order_acquire);
    bool safe_to_free = (seen % 2 == 0);
    for (int spin = 0; !safe_to_free && spin < 2000; ++spin) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        const std::uint64_t now = render_epoch_.load(std::memory_order_acquire);
        safe_to_free = (now != seen) || (now % 2 == 0);
    }
    if (safe_to_free) {
        outgoing.reset();
        drain_retired_renderers_();
    } else {
        retired_renderers_.push_back(std::move(outgoing));
    }
}

void Spectr::settle_render_switch_() noexcept {
    using Clock = std::chrono::steady_clock;
    const double rate = sample_rate_ > 0.0 ? sample_rate_ : 48000.0;
    // Twice the switch's own length in audio time, and a margin: a host that
    // is playing finishes it well inside this.
    const auto budget = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(
            2.0 * double(switch_plan_.warm_samples + switch_plan_.fade_samples) / rate
            + 0.25));
    const auto start = Clock::now();
    std::uint64_t seen_epoch = render_epoch_.load(std::memory_order_acquire);
    auto epoch_moved_at = start;
    for (;;) {
        int s = render_switch_state_.load(std::memory_order_acquire);
        if (s == kSwitchIdle) return;
        if (s == kSwitchDone) {
            if (render_switch_state_.compare_exchange_strong(
                    s, kSwitchIdle, std::memory_order_acq_rel)) {
                retire_switch_outgoing_();
                return;
            }
            continue;
        }
        if (s == kSwitchPending || s == kSwitchRunning) {
            const auto now = Clock::now();
            const auto epoch = render_epoch_.load(std::memory_order_acquire);
            if (epoch != seen_epoch) { seen_epoch = epoch; epoch_moved_at = now; }
            // No block has run for 50 ms: the host is not processing (stopped,
            // or a test driving the processor from this same thread), so no
            // fade will ever be heard. Finish the switch here instead.
            const bool audio_idle = now - epoch_moved_at > std::chrono::milliseconds(50);
            if (audio_idle || now - start > budget) {
                // Taking the word from Pending/Running means the audio thread
                // is not inside the switch and will never claim it again.
                if (render_switch_state_.compare_exchange_strong(
                        s, kSwitchIdle, std::memory_order_acq_rel)) {
                    if (switch_wet_wired_)
                        (void)switch_incoming_->set_wet_source(&auto_gain_tap_);
                    active_renderer_.store(switch_incoming_, std::memory_order_release);
                    retire_switch_outgoing_();
                    return;
                }
                continue;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Spectr::switch_reclaim_trampoline_(void* ctx, const SwitchReclaimTask&) noexcept {
    auto* self = static_cast<Spectr*>(ctx);
    std::lock_guard<std::mutex> switch_lock(self->switch_mutex_);
    int done = kSwitchDone;
    if (self->render_switch_state_.compare_exchange_strong(
            done, kSwitchIdle, std::memory_order_acq_rel))
        self->retire_switch_outgoing_();
}

void Spectr::abandon_render_switch_() noexcept {
    // Only where no audio thread can run (prepare, release): nothing to wait
    // for, and whatever the switch held is about to be rebuilt or freed.
    render_switch_state_.store(kSwitchIdle, std::memory_order_release);
    switch_xfade_.cancel();
    switch_replay_in_.replay = nullptr;
    switch_replay_out_.replay = nullptr;
    switch_outgoing_.reset();
    switch_incoming_ = nullptr;
}

MaskRenderer* Spectr::claim_render_switch_(MaskRenderer* outgoing) noexcept {
    int s = render_switch_state_.load(std::memory_order_acquire);
    if (s != kSwitchPending && s != kSwitchRunning) return nullptr;
    const int claimed = s;
    if (!render_switch_state_.compare_exchange_strong(
            s, kSwitchBusy, std::memory_order_acq_rel))
        return nullptr;
    MaskRenderer* incoming = switch_incoming_;
    if (claimed == kSwitchPending) {
        PULP_TRACE_INSTANT("dsp", "render_switch_begin");
        // Nothing has been heard since prepare or a reset (a host restoring a
        // session's mode before it starts the stream): the new renderer
        // starts the stream itself, as it would after a prepare.
        switch_xfade_.begin(stream_rendered_
            ? switch_plan_ : pulp::signal::ProcessingSwitchPlan{0, 1});
        // Both renderers now listen to one run of the freeze source per
        // block (render_through_), not one each.
        if (switch_wet_wired_ && outgoing != nullptr) {
            switch_replay_out_.forward = &auto_gain_tap_;
            switch_replay_in_.forward = &auto_gain_tap_;
            switch_replay_out_.replay = nullptr;
            switch_replay_in_.replay = nullptr;
            (void)outgoing->set_wet_source(&switch_replay_out_);
            (void)incoming->set_wet_source(&switch_replay_in_);
        }
    }
    return incoming;
}

void Spectr::release_render_switch_(MaskRenderer* incoming) noexcept {
    if (incoming == nullptr) return;
    if (switch_xfade_.finished() || !switch_xfade_.active()) {
        if (switch_wet_wired_) (void)incoming->set_wet_source(&auto_gain_tap_);
        switch_xfade_.cancel();
        // The new renderer has not seen what this thread staged into the old
        // one, nor does the surface cache describe it: restage and resync.
        last_staged_layout_valid_ = false;
        audio_applied_surface_valid_ = false;
        active_renderer_.store(incoming, std::memory_order_release);
        render_switch_state_.store(kSwitchDone, std::memory_order_release);
        PULP_TRACE_INSTANT("dsp", "render_switch_done");
        // Lock-free: the worker frees the outgoing renderer off this thread.
        (void)switch_reclaim_lane_.try_spawn(SwitchReclaimTask{});
    } else {
        render_switch_state_.store(kSwitchRunning, std::memory_order_release);
    }
}

bool Spectr::render_through_(MaskRenderer* renderer, MaskRenderer* incoming,
                             const float* const* input, float* const* output,
                             int num_samples) noexcept {
    if (incoming == nullptr || !switch_xfade_.active())
        return renderer->process(input, output, num_samples);
    PULP_TRACE_SCOPE_NAMED_ARGS("dsp", "render_switch",
        "position", static_cast<std::int64_t>(switch_xfade_.position()),
        "warming", static_cast<std::int64_t>(switch_xfade_.warming() ? 1 : 0));
    const int channels = channels_;
    // The outgoing renderer may run in place, so both read a copy.
    for (int ch = 0; ch < channels; ++ch) {
        auto* copy = const_cast<float*>(switch_in_ptrs_[static_cast<std::size_t>(ch)]);
        std::copy(input[ch], input[ch] + num_samples, copy);
    }
    const float* const* in = switch_in_ptrs_.data();
    if (switch_wet_wired_) {
        auto_gain_tap_.process_block(in, switch_wet_write_.data(), channels, num_samples);
        switch_replay_out_.replay = switch_wet_read_.data();
        switch_replay_in_.replay = switch_wet_read_.data();
        switch_replay_out_.offset = 0;
        switch_replay_in_.offset = 0;
    }
    const bool incoming_ok = incoming->process(in, switch_out_ptrs_.data(), num_samples);
    const bool outgoing_ok = renderer->process(in, output, num_samples);
    switch_replay_out_.replay = nullptr;
    switch_replay_in_.replay = nullptr;
    if (!incoming_ok)
        for (int ch = 0; ch < channels; ++ch)
            std::fill(switch_out_ptrs_[static_cast<std::size_t>(ch)],
                      switch_out_ptrs_[static_cast<std::size_t>(ch)] + num_samples, 0.0f);
    if (!outgoing_ok)
        for (int ch = 0; ch < channels; ++ch)
            std::fill(output[ch], output[ch] + num_samples, 0.0f);
    switch_xfade_.mix(output,
                      const_cast<const float* const*>(switch_out_ptrs_.data()),
                      channels, num_samples);
    return incoming_ok || outgoing_ok;
}

void Spectr::prepare(const pulp::format::PrepareContext& ctx) {
    // Re-prepare may overlap a parameter-sync task launched by the previous
    // process cycle. Join it before rebuilding the renderer: the worker can
    // publish a compiled layout, and publish_layout() must never race
    // MaskRenderer::prepare(). The lane is restarted after the new engine
    // and its initial publication are ready.
    stop_param_sync_lane_();

    // An offline flag is one render session's; a host that set it for a
    // bounce and never cleared it must not make every later block wait.
    host_offline_render_.store(false, std::memory_order_relaxed);
    offline_wait_budget_logged_.store(false, std::memory_order_relaxed);
    // The negative-control seams read the environment once, in function-local
    // statics. Take that first read here, on the control thread, so the audio
    // thread never calls getenv or runs a static initializer's guard: it only
    // loads a value already initialised.
    prime_negative_control_seams();

    sample_rate_ = ctx.sample_rate;
    max_block_   = ctx.max_buffer_size;
    channels_    = std::max(1, ctx.output_channels);

    // No audio thread runs across a prepare. Keep a prepared source whose
    // geometry still fits, so a host re-prepare with the same rate and
    // channels does not throw a playing hold away.
    // The loop rings are sized for the Length at the last transport seen;
    // the storage worker is joined first, so nothing it built for the old
    // geometry can land in the new one.
    freeze_storage_lane_.stop();
    const double freeze_seconds = freeze_hold_seconds_at_(
        transport_tempo_bpm(), transport_time_sig_numerator(),
        transport_time_sig_denominator());
    // A musical Length loops exactly that long, so the loop stays on the
    // host's bar grid pass after pass.
    freeze_source_.set_exact_loop_length(true);
    if (!freeze_source_.prepared_for(sample_rate_, channels_))
        (void)freeze_source_.prepare(sample_rate_, channels_, freeze_seconds);
    else
        freeze_source_.ensure_loop_storage_while_stopped(freeze_seconds);
    freeze_storage_collect_sent_ = false;
    start_freeze_storage_lane_();
    // Auto Gain's reference and v2's material estimator belong to the rate.
    // Prepared before any renderer is built, so the wet-source tap never
    // runs an estimator sized for another geometry.
    auto_gain_reference_.prepare(sample_rate_);
    auto_gain_material_.prepare(sample_rate_, channels_, auto_gain_reference_,
                                kSpectralFftSize);
    auto_gain_last_valid_ = false;

    // No audio thread can be running across a prepare, so the previous
    // renderer and anything a mode switch parked are free to go now.
    switch_reclaim_lane_.stop();
    abandon_render_switch_();
    stream_rendered_ = false;
    trace_stream_pos_ = 0;
    active_renderer_.store(nullptr, std::memory_order_release);
    (void)switch_reclaim_lane_.start(&Spectr::switch_reclaim_trampoline_, this,
                                     pulp::format::BackgroundTaskPolicy::Latest);
    {
        // A switch renders both renderers from a copy of the input, one run
        // of the freeze source, and the incoming output: three planar blocks.
        const auto block = static_cast<std::size_t>(std::max(1, max_block_));
        const auto chans = static_cast<std::size_t>(
            std::min<int>(channels_, static_cast<int>(kMaximumChannels)));
        switch_scratch_.assign(3 * chans * block, 0.0f);
        for (std::size_t ch = 0; ch < chans; ++ch) {
            float* base = switch_scratch_.data() + 3 * ch * block;
            switch_in_ptrs_[ch] = base;
            switch_wet_write_[ch] = base + block;
            switch_wet_read_[ch] = base + block;
            switch_out_ptrs_[ch] = base + 2 * block;
        }
    }
    std::unique_ptr<MaskRenderer> outgoing;
    {
        std::lock_guard<std::mutex> observation_lock(renderer_observation_mutex_);
        outgoing=std::move(renderer_);
    }
    outgoing.reset();
    drain_retired_renderers_();

    if (channels_ <= static_cast<int>(kMaximumChannels)) {
        auto replacement=build_renderer_(render_mode_, gpu_processing_);
        std::lock_guard<std::mutex> observation_lock(renderer_observation_mutex_);
        renderer_=std::move(replacement);
    }
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        last_published_layout_=make_mask_layout_();
        last_published_layout_valid_=false;
    }
#endif
    // The renderer arrives realising the layout build_renderer_ published, so
    // seed the audio thread's cache with it rather than leaving it empty --
    // an empty cache makes the first block restage a mask that is already live.
    last_staged_layout_ = last_published_layout_;
    last_staged_layout_valid_ = last_published_layout_valid_;
    // During prepare the host may already have written the complete parameter
    // set (AUval does this before Initialize).  Treat the first adoption as a
    // snapshot of those values; the legacy LFO target/depth lanes must not be
    // interpreted as compatibility commands until the processor is live.
    processor_prepared_ = false;
    suppress_legacy_lane_commands_ = true;
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        active_design_grid_ = renderer_ ? renderer_->design_grid_size()
                                        : kSpectralFftSize;
    }
    active_renderer_.store(renderer_.get(), std::memory_order_release);
    // The mask processor was just re-prepared, so nothing this thread applied
    // before survives into it.
    audio_applied_surface_valid_ = false;
    output_gain_.set_ramp_time(0.01f, static_cast<float>(sample_rate_));
    output_gain_.set_immediate(std::pow(
        10.0f, state().get_value(kOutputTrim) * 0.05f));
    // Level controls: the first block adopts Intensity and the Auto Gain level
    // without a ramp.
    auto_gain_.set_ramp_time(kAutoGainRampSeconds, static_cast<float>(sample_rate_));
    auto_gain_.set_immediate(1.0f);
    auto_gain_target_db_ = 0.0f;
    auto_gain_primed_ = false;
    audio_intensity_primed_ = false;
    audio_legacy_lanes_.primed = false;
    audio_output_mod_primed_ = false;
    audio_output_mod_db_ = 0.0f;
    audio_intensity_percent_ = state().get_value(kParamIntensity);
    audio_auto_gain_param_ = state().get_value(kParamAutoGain);
    auto_gain_applied_db_.store(0.0f, std::memory_order_relaxed);
    // spectr#34: adopt any parameter state written before prepare (a host
    // may restore a session before audio starts). Morph is excluded — the
    // restored field already encodes it; re-deriving would erase post-morph
    // tweaks. The publish below covers whatever the apply changed.
    apply_surface_params(/*apply_morph=*/false);
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        publish_processing_state_();
    }
    preroll_surviving_hold_();
    // The pre-roll above ran the wet-source tap over silence. Start the
    // estimator's frame grid here, at the stream's first sample, so a bounce
    // and a playback prepared the same way see the same frames -- and keep
    // the estimate a re-prepare carried (Auto Gain v2 never forgets the
    // material because the host re-prepared).
    auto_gain_material_.restart_grid();
    // Audio→worker lane for host-automation adoption (see the drift sweep
    // in process()). Restart cleanly across re-prepare.
    if (!param_sync_lane_.start(&Spectr::param_sync_trampoline_, this,
                                pulp::format::BackgroundTaskPolicy::Latest)) {
        pulp::runtime::log_error(
            "[Spectr] parameter sync worker failed to start; host automation "
            "of the band surface will not reach the DSP");
    }
    suppress_legacy_lane_commands_ = false;
    processor_prepared_ = renderer_ != nullptr;
    configure_bridge_(ctx.output_channels);
}

// ── Freeze Length ────────────────────────────────────────────────────────

FreezeLength Spectr::freeze_length() const noexcept {
    const int preset = freeze_length_preset();
    if (preset >= 0 && preset < kLengthPresetCustom)
        return kLengthPresets[static_cast<std::size_t>(preset)];
    return freeze_custom_length();
}

int Spectr::freeze_length_preset() const noexcept {
    if (!param_store_) return kDefaultLengthPreset;
    return length_preset_from_param(param_store_->get_value(kParamFreezeLength));
}

bool Spectr::set_freeze_custom_length(FreezeLength length) noexcept {
    if (!valid_length(length)) return false;
    freeze_custom_length_.store(pack_length(length), std::memory_order_relaxed);
    return true;
}

bool Spectr::set_freeze_length_from_editor(FreezeLength length) noexcept {
    auto* store = param_store_;
    if (!store || !valid_length(length)) return false;
    const int preset = preset_index_of(length);
    // The custom value first, so the audio thread never reads "Custom" with
    // the previous custom length behind it.
    if (preset < 0) (void)set_freeze_custom_length(length);
    store->begin_gesture(kParamFreezeLength);
    store->set_value(kParamFreezeLength, static_cast<float>(
        preset < 0 ? kLengthPresetCustom : preset));
    store->end_gesture(kParamFreezeLength);
    return true;
}

double Spectr::freeze_hold_seconds_at_(double tempo_bpm, int numerator,
                                       int denominator) const noexcept {
    const double override_seconds =
        freeze_seconds_override_.load(std::memory_order_relaxed);
    if (override_seconds >= 0.0) return override_seconds;
    return length_seconds(freeze_length(), tempo_bpm, numerator, denominator);
}

double Spectr::freeze_length_at_phases_(double tempo_bpm, int numerator, int denominator,
                                        const double phases[2], int* index,
                                        double* reach_seconds) const noexcept {
    const double base = freeze_hold_seconds_at_(tempo_bpm, numerator, denominator);
    *index = -1;
    if (reach_seconds) *reach_seconds = base;
    auto* store = param_store_;
    constexpr auto kLength = static_cast<std::size_t>(ModulationTarget::Length);
    if (!store || freeze_seconds_override_.load(std::memory_order_relaxed) >= 0.0)
        return base;
    // The Length target's coordinate at these phases: each LFO that is on and
    // routed to Length adds wave x Depth.
    const pulp::state::ParamID enabled_ids[2] = {kParamLfoEnabled, kParamLfo2Enabled};
    const LfoShapeFade* fades[2] = {&audio_lfo_shape_fade_, &audio_lfo_2_shape_fade_};
    bool driven = false;
    float coordinate = 0.0f, reach = 0.0f;
    for (std::size_t lfo = 0; lfo < 2; ++lfo) {
        if (store->get_value(enabled_ids[lfo]) < 0.5f) continue;
        if (store->get_value(lfo_route_enabled_param_id(lfo, kLength)) < 0.5f) continue;
        driven = true;
        const float depth = std::clamp(
            store->get_value(lfo_route_amount_param_id(lfo, kLength)), 0.0f, 1.0f);
        coordinate += lfo_value(*fades[lfo], phases[lfo]) * depth;
        reach += depth;
    }
    if (!driven) return base;
    // The user's LENGTH is the centre: its list index, or for a custom length
    // the list entry nearest it.
    int centre = freeze_length_preset();
    if (centre < 0 || centre >= kLengthPresetCustom) {
        const double bars = length_in_bars(freeze_custom_length());
        centre = 0;
        for (int i = 1; i < kLengthPresetCustom; ++i)
            if (std::abs(length_in_bars(kLengthPresets[static_cast<std::size_t>(i)]) - bars)
                < std::abs(length_in_bars(kLengthPresets[static_cast<std::size_t>(centre)]) - bars))
                centre = i;
    }
    *index = modulated_length_index(centre, coordinate, kLengthPresetCustom);
    // The longest length the routes can step to (the wave's crest).
    if (reach_seconds)
        *reach_seconds = std::max(base, length_seconds(
            kLengthPresets[static_cast<std::size_t>(
                modulated_length_index(centre, reach, kLengthPresetCustom))],
            tempo_bpm, numerator, denominator));
    return length_seconds(kLengthPresets[static_cast<std::size_t>(*index)],
                          tempo_bpm, numerator, denominator);
}

double Spectr::modulated_freeze_seconds_(double tempo_bpm, int numerator,
                                         int denominator) noexcept {
    // The engage this block may perform takes the length the LFOs reach now;
    // a hold already playing keeps its own (FreezeSource::set_hold_seconds
    // only shapes the NEXT latch).
    const double phases[2] = {audio_modulation_phase_, audio_modulation_phase_2_};
    int index = -1;
    const double seconds = freeze_length_at_phases_(tempo_bpm, numerator, denominator,
                                                    phases, &index,
                                                    &audio_freeze_reach_seconds_);
    freeze_modulated_length_index_.store(index, std::memory_order_relaxed);
    return seconds;
}

double Spectr::freeze_length_seconds() const noexcept {
    return length_seconds(freeze_length(), transport_tempo_bpm(),
                          transport_time_sig_numerator(),
                          transport_time_sig_denominator());
}

double Spectr::freeze_loop_cap_seconds() const noexcept {
    return FreezeSource::loop_cap_seconds(sample_rate_ > 0.0 ? sample_rate_ : 48000.0,
                                          std::max(1, channels_));
}

void Spectr::freeze_storage_trampoline_(void* ctx, const FreezeStorageTask& task) noexcept {
    auto& source = static_cast<Spectr*>(ctx)->freeze_source_;
    source.collect_retired_loop_storage();
    if (task.seconds > 0.0) {
        auto storage = source.allocate_loop_storage(task.seconds);
        if (storage) source.offer_loop_storage(std::move(storage));
    }
}

void Spectr::start_freeze_storage_lane_() {
    if (!freeze_storage_lane_.start(&Spectr::freeze_storage_trampoline_, this,
                                    pulp::format::BackgroundTaskPolicy::Ordered)) {
        pulp::runtime::log_error(
            "[Spectr] freeze storage worker failed to start; Freeze lengths "
            "longer than the prepared loop memory will loop what it holds");
    }
}

void Spectr::preroll_surviving_hold_() {
    // A host reload at the same geometry kept the freeze source -- and with
    // it a playing hold -- but the renderer was rebuilt empty, so its wet
    // pipeline would open with up to a full latency of silence (a quarter
    // second in Mixing) before the hold reached the output again. No audio
    // thread runs across a prepare: fill the new pipeline with the hold here,
    // off the stream, so the first block after the reload continues it.
    if (!processor_prepared_ || !renderer_ || !freeze_source_.hold_audible())
        return;
    const int span = std::max(renderer_->latency_samples(),
                              renderer_->maximum_tail_samples());
    const int block = std::max(1, max_block_);
    if (span <= 0 || channels_ < 1
        || channels_ > static_cast<int>(kMaximumChannels))
        return;
    std::vector<float> silence(static_cast<std::size_t>(block), 0.0f);
    std::vector<float> scratch(static_cast<std::size_t>(block * channels_), 0.0f);
    std::array<const float*, kMaximumChannels> in{};
    std::array<float*, kMaximumChannels> out{};
    for (int ch = 0; ch < channels_; ++ch) {
        in[static_cast<std::size_t>(ch)] = silence.data();
        out[static_cast<std::size_t>(ch)] =
            scratch.data() + static_cast<std::size_t>(ch * block);
    }
    renderer_->set_mix(std::clamp(state().get_value(kMix) / 100.0f, 0.0f, 1.0f));
    freeze_source_.set_frozen(state().get_value(kParamFreeze) >= 0.5f);
    for (int done = 0; done < span; done += block)
        (void)renderer_->process(in.data(), out.data(), std::min(block, span - done));
    // The pre-roll fed the source silence; keep it out of the next capture.
    freeze_source_.clear_history();
}

std::unique_ptr<pulp::view::View> Spectr::create_view() {
#if defined(SPECTR_NATIVE_EDITOR)
    return create_native_editor_();
#else
    // Release 1 embeds the reviewed editor.html. Visual parity remains
    // by construction; JS↔C++ state sync flows through EditorView's
    // message handler. See include/spectr/ui/editor_view.hpp.
    // No explicit set_bounds — the framework lays us out to the window's
    // content area. EditorView attaches the native child view to that
    // actual laid-out size (or PluginViewHost::get_size() in plugins),
    // so we don't leave a gap if window chrome differs from our
    // preferred size.
    return std::make_unique<EditorView>(*this);
#endif
}

pulp::format::ViewSize Spectr::view_size() const {
    return make_editor_view_size<pulp::format::ViewSize>();
}

void Spectr::on_view_opened(pulp::view::View& view) {
#if defined(SPECTR_NATIVE_EDITOR)
    open_native_editor_(view);
#else
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->attach_if_needed();
    }
#endif
}

void Spectr::on_view_resized(pulp::view::View& view, uint32_t w, uint32_t h) {
#if defined(SPECTR_NATIVE_EDITOR)
    if (&view != native_editor_root_ || w == 0 || h == 0) return;
    native_host_width_ = w;
    native_host_height_ = h;
    // A deferred editor has no document to lay out yet. Keep the host size;
    // the frame that evaluates the document publishes it.
    if (native_scripted_ui_ && native_scripted_ui_->document_load_pending()) return;
    if (pulp::format::should_pin_design_viewport(view_size())) {
        // Pinned viewport: the HOST owns the scale, so the root stays at the
        // authored box at every host size and paint maps it onto the surface.
        //
        // Laying the root out at the host size while a viewport is pinned is
        // the specific bug that renders content into a FRACTION of the surface
        // with the remainder unpainted — measured at 1485 of 1980 physical px
        // (990 design px at 1.5 px/px) with the rest black.
        //
        // And no JS relayout: with a pin there is nothing to reflow, and
        // re-laying out at the host size flashes before the next paint reset
        // (view-bridge SKILL.md, "Proportional resize with aspect lock"). The
        // unpainted band during a live drag came from exactly that round trip —
        // the responsive pass needs ~96 host frames to commit, so the surface
        // outran the content for the whole gesture.
        const pulp::view::Rect authored_bounds{
            0.0f, 0.0f, static_cast<float>(kEditorDesignWidth),
            static_cast<float>(kEditorDesignHeight)};
        if (view.bounds() != authored_bounds) {
            view.set_bounds(authored_bounds);
            view.layout_children();
        }
        // Still run the materialized pass, but ALWAYS at the authored size.
        // It does two jobs: applyMaterializedImportMetadata() restores the
        // captured authored geometry, and only after that does it re-place for
        // the argument size. Under the pin the re-placement must not track the
        // host — but the RESTORE is still required, or elements whose position
        // comes from the import metadata (the canvas-drawn viewport strip) never
        // receive an authored position at all. Skipping the whole call threw the
        // restore out with the reflow, which showed up on every open, not just
        // on resize. Passing the authored box keeps the layout identical at
        // every host size, which is exactly the proportional contract.
        publish_native_layout_(kEditorDesignWidth, kEditorDesignHeight);
        return;
    }
    const pulp::view::Rect host_bounds{
        0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    if (view.bounds() != host_bounds) {
        view.set_bounds(host_bounds);
        view.layout_children();
    }
    publish_native_layout_(w, h);
#else
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->sync_to_host();
    }
#endif
}

#if defined(SPECTR_NATIVE_EDITOR)
void Spectr::publish_native_layout_(std::uint32_t w, std::uint32_t h) {
    // The pass below restores the captured authored geometry and re-places the
    // whole materialized tree — hundreds of bridge writes. Under a pinned
    // viewport on_view_resized always calls it with the SAME authored box, so
    // during a resize drag it re-ran per pointer event to produce a layout
    // identical to the one already on screen. Publish only when the arguments
    // actually move; the first call after an editor is created always does,
    // because native_published_* is reset with the editor.
    if (w == native_published_width_ && h == native_published_height_) return;
    native_published_width_ = w;
    native_published_height_ = h;
    if (native_scripted_ui_ && native_scripted_ui_->bridge()) {
        std::ostringstream script;
        script << "if (typeof globalThis.__spectrResizeNativeEditor === 'function') "
                  "globalThis.__spectrResizeNativeEditor("
               << w << ',' << h << ");";
        try {
            native_scripted_ui_->bridge()->load_script(
                script.str(), "spectr-native-responsive-resize");
        } catch (const std::exception& error) {
            pulp::runtime::log_error(
                "[Spectr native] responsive resize rejected: {}", error.what());
        }
    }
}
// The non-native editor has no responsive-layout hook to publish to, and this
// function does not exist in that build: the #if above guards the definition
// itself. It previously carried an #else branch copied from on_view_resized,
// which referenced a `view` parameter this function does not take -- dead in
// the shipping build and a compile error the moment SPECTR_NATIVE_EDITOR is
// off, i.e. exactly when it would have been reached.
#endif

void Spectr::on_view_closed(pulp::view::View& view) {
#if defined(SPECTR_NATIVE_EDITOR)
    if (&view == native_editor_root_) close_native_editor_();
#else
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->detach_if_needed();
    }
#endif
}

pulp::view::VisualizationConfig analyzer_config(double sample_rate,
                                                int num_channels) noexcept {
    pulp::view::VisualizationConfig c;
    c.fft_size         = kAnalyzerFftSize;
    c.hop_size         = kAnalyzerAnalysisHop;
    c.window           = pulp::signal::WindowFunction::Type::hann;
    c.num_channels     = std::max(1, num_channels);
    c.sample_rate      = static_cast<float>(sample_rate);
    c.capture_waveform = true;
    c.waveform_length  = 1024;
    c.capture_buffer_frames = kAnalyzerCaptureFrames;
    c.backlog_policy = pulp::view::VisualizationBacklogPolicy::latest_window;
    return c;
}

void Spectr::configure_bridge_(int num_channels) {
    bridge_.configure(analyzer_config(sample_rate_, num_channels));
}

void Spectr::release() {
    // Join the sync worker BEFORE touching the mask processor: an in-flight
    // apply publishes into it.
    stop_param_sync_lane_();
    freeze_storage_lane_.stop();
    switch_reclaim_lane_.stop();
    abandon_render_switch_();
    active_renderer_.store(nullptr, std::memory_order_release);
    std::unique_ptr<MaskRenderer> outgoing;
    {
        std::lock_guard<std::mutex> observation_lock(renderer_observation_mutex_);
        outgoing=std::move(renderer_);
    }
    outgoing.reset();
    drain_retired_renderers_();
    processor_prepared_ = false;
    bridge_.reset();
}

int Spectr::latency_samples() const {
    // Latency is a function of the render mode and the product's fixed
    // spectral geometry -- never of the host block size or the machine. Report
    // the prepared renderer's own value when there is one, and the mode's
    // declared value before prepare so an adapter can answer a host that asks
    // early. Both paths are the same number for the same mode, which is what
    // makes a project recall with the same delay compensation everywhere.
    if (processor_prepared_ && renderer_) return renderer_->latency_samples();
    // Geometry only: an adapter may ask before any parameter store is wired.
    // Pulp's AAX adapter builds its descriptor from a bare factory() instance
    // and asks it for latency there; renderer_config_() reads the mix from
    // state(), which is not yet bound, and crashed the plug-in at
    // registration.
    return render_mode_latency_samples(render_mode_);
}

int Spectr::render_mode_latency_samples(MaskRenderMode mode) const noexcept {
    return render_mode_latency_samples(mode, gpu_processing_);
}

int Spectr::render_mode_latency_samples(MaskRenderMode mode, bool gpu) const noexcept {
    const auto config=latency_geometry_();
    auto latency=mask_render_latency_samples(mode,config);
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    if(mode==MaskRenderMode::linear_phase && gpu)
        latency+=int(experimental::SharedSpectralMaskRenderer::additional_latency(config));
#else
    (void)gpu;
#endif
    return latency;
}

void Spectr::set_layout(Layout L) {
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        layout_ = L;
        publish_processing_state_();
    }
    sync_params_from_field();
}

namespace {

/// Publishes "the audio thread is inside process()" as an even/odd counter.
///
/// A mode switch replaces the renderer object underneath a possibly-running
/// audio thread. The switching thread needs to know when the old pointer can
/// no longer be held, and the audio thread must not pay a lock to tell it.
/// Odd means inside, even means outside; a control thread that observes the
/// counter change, or observes it even, knows any pointer read before that
/// point has been released. Incrementing on every exit path is what makes the
/// parity meaningful, hence the destructor.
struct RenderEpochScope {
    std::atomic<std::uint64_t>& epoch;
    explicit RenderEpochScope(std::atomic<std::uint64_t>& e) noexcept : epoch(e) {
        epoch.fetch_add(1, std::memory_order_acq_rel);
    }
    ~RenderEpochScope() noexcept { epoch.fetch_add(1, std::memory_order_release); }
    RenderEpochScope(const RenderEpochScope&) = delete;
    RenderEpochScope& operator=(const RenderEpochScope&) = delete;
};

} // namespace

void Spectr::retire_param_sync_through_(std::uint64_t tag) noexcept {
    auto done = param_sync_done_.load(std::memory_order_relaxed);
    while (tag > done) {
        if (param_sync_done_.compare_exchange_weak(done, tag, std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            detail::g_param_sync_backlog.fetch_sub(tag - done, std::memory_order_acq_rel);
            return;
        }
    }
}

void Spectr::spawn_param_sync_(MaskRenderer* renderer) noexcept {
    const auto tag = param_sync_requested_.load(std::memory_order_relaxed) + 1;
    // Three steps, in this order. The ordinal is reserved first, so a layout
    // the audio thread staged in this block -- or the mask it claimed -- is the
    // NEWER request. The block's handoff is then flushed, which publishes that
    // newer request (a staged layout's ordinal, or the claim's bump). Only
    // then is the worker spawned: it can no longer run before the audio
    // thread's request exists, so its publish is superseded however the
    // scheduler interleaves the two. Spawning before the flush left a window
    // in which a fast worker staged the base mask over the one the audio path
    // owned.
    const auto ordinal = renderer ? renderer->reserve_request_ordinal() : 0;
    if (renderer) renderer->flush_design_handoff();
    if (param_sync_lane_.try_spawn(ParamSyncTask{tag, ordinal, renderer})) {
        param_sync_requested_.store(tag, std::memory_order_relaxed);
        detail::g_param_sync_backlog.fetch_add(1, std::memory_order_acq_rel);
        if (auto* hook = detail::g_param_sync_spawned_hook.load(std::memory_order_relaxed))
            hook();
    }
}

void Spectr::stop_param_sync_lane_() noexcept {
    param_sync_lane_.stop();
    // A stopped lane owes nothing; never leave the process-wide count stuck.
    retire_param_sync_through_(param_sync_requested_.load(std::memory_order_relaxed));
}

bool Spectr::await_param_sync_(std::chrono::steady_clock::time_point deadline) noexcept {
    while (param_sync_done_.load(std::memory_order_acquire)
           < param_sync_requested_.load(std::memory_order_relaxed)) {
        if (!param_sync_lane_.running()) return true;
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return true;
}

void Spectr::await_offline_work_(MaskRenderer* renderer) noexcept {
    PULP_TRACE_SCOPE_NAMED("dsp", "offline: await design workers");
    // One budget for the whole block. Parameter sync first: it can publish a
    // new mask, which the renderer then has to have designed.
    const auto deadline = std::chrono::steady_clock::now() + kOfflineBlockWaitBudget;
    bool settled = await_param_sync_(deadline);
    if (renderer) settled = renderer->await_staged_designs(deadline) && settled;
    if (settled) return;
    offline_wait_budget_exhausted_.fetch_add(1, std::memory_order_relaxed);
    // Once per instance: a stuck flag would otherwise log every block.
    if (!offline_wait_budget_logged_.exchange(true, std::memory_order_relaxed)) {
        pulp::runtime::log_warn(
            "[Spectr] an offline block waited its whole budget for the design "
            "workers and rendered without them; a bounce may differ from "
            "playback here (a starved worker, or a host offline flag left set "
            "after its bounce). Logged once.");
    }
}

void Spectr::process(
    pulp::audio::BufferView<float>& output,
    const pulp::audio::BufferView<const float>& input,
    pulp::midi::MidiBuffer& /*midi_in*/,
    pulp::midi::MidiBuffer& /*midi_out*/,
    const pulp::format::ProcessContext& ctx)
{
    // SPECTR-RENDER-PATH BEGIN
    //
    // Everything from here to the END marker runs on the audio thread and is
    // scanned by tools/ci/check_render_path_clock.py for clocks, sleeps,
    // threads and locks. The mode-adoption code below is inside it
    // deliberately: adopting a renderer is the newest thing on this path and
    // the easiest place to reach for a timestamp or a lock while retiring the
    // old object. It does neither -- the handshake is an atomic pointer and a
    // counter, and the waiting happens on the control thread in
    // set_render_mode().
    //
    // The region ENDS before the modulated-field publication further down,
    // which reads steady_clock on purpose. That read stamps a snapshot for the
    // UI to draw; nothing derived from it reaches a sample. Excluding it is
    // therefore a statement about what the marker covers, not a gap: no audio
    // this function emits depends on that value, and moving the END marker
    // past it would make the scan assert something false rather than
    // something stronger.

    // Mark the block, and take the live renderer exactly once. A mode switch
    // can land between blocks but never within one: the whole block renders
    // through a single realisation, so no output sample is half of one mode
    // and half of the other.
    const RenderEpochScope epoch_scope{render_epoch_};
    // One span per host block, stamped with the stream position of its first
    // sample and its length: a click found at sample N of a render is the
    // block whose [stream_pos, stream_pos + frames) holds N. A no-op unless
    // the SDK is built with tracing and a session is running.
    PULP_TRACE_SCOPE_NAMED_ARGS("dsp", "process",
        "stream_pos", trace_stream_pos_,
        "frames", static_cast<std::int64_t>(output.num_samples()));
    if (const long burst = callback_burst_plant(); burst > 0
        && (trace_stream_pos_ % 512) < static_cast<std::int64_t>(output.num_samples())) {
        volatile double sink = 0.0;
        for (long i = 0; i < burst; ++i) sink = sink + std::sqrt(static_cast<double>(i));
    }
    trace_stream_pos_ += static_cast<std::int64_t>(output.num_samples());
    MaskRenderer* const renderer = active_renderer_.load(std::memory_order_acquire);
    // A mode switch in flight: this block renders both renderers and
    // crossfades (render_through_). Released last, after every other use of
    // the outgoing renderer in this block, so the control thread can free it
    // the moment the switch reads done.
    struct RenderSwitchScope {
        Spectr* self;
        MaskRenderer* incoming;
        bool rendered;
        ~RenderSwitchScope() {
            self->release_render_switch_(incoming);
            if (rendered) self->stream_rendered_ = true;
        }
    };
    const RenderSwitchScope switch_scope{
        this, processor_prepared_ && renderer ? claim_render_switch_(renderer) : nullptr,
        processor_prepared_ && renderer != nullptr};
    MaskRenderer* const switch_in = switch_scope.incoming;
    // A restored session's Auto Gain estimate (deserialize_plugin_state) is
    // adopted here, before any sample reaches the estimator.
    auto_gain_material_.adopt_pending();
    // A session saved with AUTO on before v2 keeps v1 until the user switches
    // AUTO off and on again -- seen here, every block, whatever path renders
    // it (a flat shape never reaches the composed path). 2: just loaded, so
    // what AUTO was before the load is not a toggle.
    {
        const bool auto_on = state().get_value(kParamAutoGain) >= 0.5f;
        int legacy = auto_gain_legacy_v1_.load(std::memory_order_relaxed);
        if (legacy == 2) {
            auto_gain_seen_ = -1;
            int fresh = 2;
            (void)auto_gain_legacy_v1_.compare_exchange_strong(
                fresh, 1, std::memory_order_relaxed);
            legacy = 1;
        }
        if (legacy == 1 && auto_gain_seen_ == 0 && auto_on) {
            auto_gain_model_.store(static_cast<int>(AutoGainModel::material_v2),
                                   std::memory_order_relaxed);
            int expected = 1;
            (void)auto_gain_legacy_v1_.compare_exchange_strong(
                expected, 0, std::memory_order_relaxed);
        }
        // (Negative control: the earlier placement only saw AUTO on the
        // composed path, which a flat shape with AUTO off never runs.)
        if (!(level_plant("autogain-v2-legacy-composed-only") && !auto_on))
            auto_gain_seen_ = auto_on ? 1 : 0;
    }

    // An offline render is not paced, so the workers that design a staged
    // mask or apply drifted host parameters fall a load-dependent number of
    // blocks behind and the bounce stops matching playback. On an offline
    // block only, first wait for what a paced host would already have
    // adopted. A realtime block never waits.
    const bool offline_block = processor_prepared_
        && (ctx.is_offline() || host_offline_render_.load(std::memory_order_relaxed));
    if (offline_block) await_offline_work_(renderer);
    if (renderer) renderer->set_offline_block(offline_block);
    if (switch_in) switch_in->set_offline_block(offline_block);
    // Work this block asks of a worker -- a staged mask's design, a drifted
    // parameter's sync (which publishes a mask of its own) -- is handed over
    // when the block ENDS, never in the middle of it: a call the scheduler
    // preempts could otherwise let that work land between two of this block's
    // render blocks, and the adoption point would depend on load. Deferral is
    // lifted on the way out so control-thread pumps of the renderer hand off
    // at once.
    struct BlockEndHandoff {
        Spectr* self;
        MaskRenderer* renderer;
        bool param_sync = false;
        ~BlockEndHandoff() {
            // A param sync flushes the handoff itself, between reserving its
            // ordinal and spawning (see spawn_param_sync_).
            if (param_sync) self->spawn_param_sync_(renderer);
            else if (renderer) renderer->flush_design_handoff();
            if (renderer) renderer->defer_design_handoff(false);
        }
    };
    BlockEndHandoff block_end_handoff{this, processor_prepared_ ? renderer : nullptr};
    if (block_end_handoff.renderer) block_end_handoff.renderer->defer_design_handoff(true);

    // spectr#34: host-side parameter writes (automation playback, generic
    // controls) land in the store between blocks. On any drift, hand the
    // adoption to the sync worker — mask-table compilation is a
    // control-thread operation and never runs here. One lock-free spawn per
    // block at most; the lane's Latest policy coalesces bursts.
    const auto surface_drift = processor_prepared_
        ? sample_surface_drift_() : SurfaceDrift{};
    block_end_handoff.param_sync = surface_drift.worker;

    // Sync the two continuously automatable audio controls each block.
    const float mix        = state().get_value(kMix) / 100.0f;
    const float out_trim_db= state().get_value(kOutputTrim);
    const float target_output_gain = std::pow(10.0f, out_trim_db * 0.05f);
    // Intensity and Auto Gain live on the block-composed path below: either
    // one engaged -- or still ramping out -- keeps that path running, so the
    // mask it stages and the post gain it applies are this block's.
    const bool level_engaged =
        intensity_factor(state().get_value(kParamIntensity)) < 1.0f
        || audio_intensity_ < 1.0f
        || state().get_value(kParamAutoGain) >= 0.5f
        || auto_gain_.is_smoothing() || auto_gain_.current() != 1.0f;

    // An explicit reset or unexpected seek is a hard DSP-history boundary.
    // Preserve the continuously hot WOLA/dry-delay history across an ordinary
    // host cycle wrap so looping does not emit a fresh startup gap.
    const bool should_reset_stream_history = ctx.should_reset_stream_history();
    if (should_reset_stream_history) {
        if (processor_prepared_ && renderer)
            renderer->reset();
        // A reset is a stream boundary: there is nothing continuous to
        // crossfade across, and warming again would hold the just-reset old
        // renderer -- silent for its own latency -- in the output. The switch
        // completes here, the new renderer starting fresh as a reset starts
        // the old one.
        if (switch_in != nullptr) {
            switch_in->reset();
            switch_xfade_.begin(pulp::signal::ProcessingSwitchPlan{0, 1});
        }
        // A transport jump forgets the input analysed so far and nothing
        // else: a playing hold keeps playing across it.
        freeze_source_.clear_history();
        output_gain_.set_immediate(target_output_gain);
        // Level controls re-adopt their values on the next composed block.
        // Auto Gain v2 keeps what the material sounds like across a jump --
        // playing from a locate starts at the right level -- and restarts
        // only its frame grid, so the block size still cannot matter. (The
        // estimate is also saved with the session, so a reopened project and
        // a bounce start warm from the same state.)
        auto_gain_primed_ = false;
        if (level_plant("autogain-v2-reset-on-seek") || level_plant("autogain-v2a"))
            auto_gain_material_.reset();
        else auto_gain_material_.restart_grid();
        auto_gain_last_valid_ = false;
        audio_intensity_primed_ = false;
        audio_output_mod_primed_ = false;
        audio_output_mod_db_ = 0.0f;
    }
    // Freeze's Length, in seconds at the host's tempo and meter. It only
    // decides the NEXT latch: a hold already playing keeps the loop it
    // took, so a tempo change never stretches or cuts a hold mid-phrase.
    {
        const double tempo = usable_tempo(ctx.tempo_bpm);
        const bool meter = ctx.time_sig_numerator > 0 && ctx.time_sig_denominator > 0;
        const int numerator = meter ? ctx.time_sig_numerator : 4;
        const int denominator = meter ? ctx.time_sig_denominator : 4;
        transport_tempo_bpm_.store(tempo, std::memory_order_relaxed);
        transport_time_sig_numerator_.store(numerator, std::memory_order_relaxed);
        transport_time_sig_denominator_.store(denominator, std::memory_order_relaxed);
        const double seconds = modulated_freeze_seconds_(tempo, numerator, denominator);
        // While a Hold for Length hold plays, its loop is the length the hold
        // is timed for: the Length target keeps moving (and the label shows
        // where it is going), but the latch the source makes a hop or a
        // release fade after the trigger takes the trigger's length.
        freeze_source_.set_hold_seconds(
            freeze_hold_remaining_ > 0 && freeze_hold_seconds_ > 0.0
                ? freeze_hold_seconds_ : seconds);
        {
            // The spectral capture can be skipped at a loop Length only
            // while no LFO can move the Length under a press.
            bool length_driven = false;
            if (param_store_) {
                constexpr auto kLengthTarget = static_cast<std::size_t>(ModulationTarget::Length);
                const pulp::state::ParamID on[2] = {kParamLfoEnabled, kParamLfo2Enabled};
                for (std::size_t lfo = 0; lfo < 2; ++lfo)
                    length_driven = length_driven
                        || (param_store_->get_value(on[lfo]) >= 0.5f
                            && param_store_->get_value(
                                   lfo_route_enabled_param_id(lfo, kLengthTarget)) >= 0.5f);
            }
            freeze_source_.set_spectral_capture_required(length_driven);
        }
        audio_freeze_length_seconds_ = seconds;
        // Longer than the rings reach: ask the worker for bigger ones. A
        // lock-free spawn, at most once per size; the source adopts them at
        // a hop boundary. While the Length target drives it, for the longest
        // length it can reach: rings grown at the trigger that first needs
        // them would be adopted only after that hold, so it would loop
        // what fit in the old ones.
        const double storage_seconds = std::max(seconds, audio_freeze_reach_seconds_);
        if (freeze_source_.wants_loop_storage(storage_seconds)
            && !freeze_storage_lane_.try_spawn(FreezeStorageTask{storage_seconds}))
            freeze_source_.forget_loop_storage_request();
        // ...and hand back the rings it let go of, to be freed there.
        if (freeze_source_.retired_loop_storage_pending()) {
            if (!freeze_storage_collect_sent_)
                freeze_storage_collect_sent_ =
                    freeze_storage_lane_.try_spawn(FreezeStorageTask{0.0});
        } else {
            freeze_storage_collect_sent_ = false;
        }
    }

    // Gate on the pointer this block actually dereferences, not on a separate
    // bool that could in principle disagree with it.
    if (processor_prepared_ && renderer != nullptr
        && output.num_channels() == static_cast<std::size_t>(channels_)
        && input.num_channels() == static_cast<std::size_t>(channels_)
        && output.num_samples() == input.num_samples()) {
        const auto* events = param_events();
        const auto& audio_modulation = audio_modulation_publication_.read();
        const bool has_events = events && !events->events().empty();
        const bool modulation_enabled =
            state().get_value(kParamLfoEnabled) >= 0.5f
            || state().get_value(kParamLfo2Enabled) >= 0.5f;
        // `modulated_field_was_active_` keeps this branch alive for exactly
        // one more pass after the modulator stops. Without it a host that
        // sends no parameter events in the block where the LFO is switched
        // off skips the branch entirely, the falling-edge publication never
        // runs, and the editor's overlay latches on the last modulated frame
        // for the rest of the session -- the release `applyModulationFrame`
        // exists to perform never arrives.
        // `surface_drift.audio` is the store-write lane. A host that changes
        // a band by writing the parameter rather than by sending an event --
        // an AU generic control, a plain `AudioUnitSetParameter`, a restored
        // preset -- leaves the adoption to the sync worker spawned above,
        // which is a THREAD. Gating the audio path on that worker makes the
        // sound depend on it being scheduled rather than on samples
        // processed, so a consumer that runs blocks back to back (an offline
        // render, a test) can clear a whole settling window before the change
        // lands, or miss it entirely. Reading the drifted store here through
        // the cursor makes the block that OBSERVES the drift also act on it.
        // The worker still runs: it owns canonical state for the editor.
        // A level still ramping toward zero after the LFO was switched off
        // keeps the branch alive too: the ramp IS the switch-off, and a host
        // that sends nothing after the off event must still hear it finish.
        // (A route level only matters while its LFO level is non-zero, so
        // this also covers a destination fading out.)
        const bool lfo_level_ramping =
            audio_lfo_level_[0] > 0.0f || audio_lfo_level_[1] > 0.0f;
        if (has_events || modulation_enabled || modulated_field_was_active_
            || lfo_level_ramping || surface_drift.audio || level_engaged) {
            std::array<pulp::format::ParamSnapshotEntry,
                       kSurfaceCacheSlots + 2 + kLevelParamCount> initial{};
            initial[0] = {kMix, audio_mix_percent_};
            initial[1] = {kOutputTrim, audio_output_trim_db_};
            for (std::size_t slot = 0; slot < kSurfaceCacheSlots; ++slot) {
                initial[slot + 2] = {
                    detail::surface_slot_param_id(slot),
                    applied_param_cache_[slot].load(std::memory_order_relaxed)};
            }
            // The level controls take the previous block's end value only when
            // this block carries events for them. A store write with no event
            // (AudioUnitSetParameter, an editor knob) would otherwise be
            // shadowed by that baseline for as long as OTHER lanes keep
            // sending events -- measured through the AU: Auto Gain switched
            // off by set-parameter stayed on under scheduled Intensity
            // automation.
            bool intensity_events = false, auto_gain_events = false;
            if (has_events)
                for (const auto& event : events->events()) {
                    intensity_events |= event.param_id == kParamIntensity;
                    auto_gain_events |= event.param_id == kParamAutoGain;
                }
            initial[kSurfaceCacheSlots + 2] = {kParamIntensity, intensity_events
                ? audio_intensity_percent_ : state().get_value(kParamIntensity)};
            initial[kSurfaceCacheSlots + 3] = {kParamAutoGain, auto_gain_events
                ? audio_auto_gain_param_ : state().get_value(kParamAutoGain)};

            pulp::format::ParamCursor params(
                state(), events,
                has_events
                    ? std::span<const pulp::format::ParamSnapshotEntry>{initial}
                    : std::span<const pulp::format::ParamSnapshotEntry>{});
            std::size_t block_offset = 0;
            pulp::format::for_each_subblock(
                output, input, events, params,
                [&](pulp::audio::BufferView<float>& out_slice,
                    const pulp::audio::BufferView<const float>& in_slice,
                    pulp::format::ParamCursor& cursor) {
                    BandField canonical{};
                    const auto automated_layout = layout_from_param_value(
                        cursor.value(kParamBandCount));
                    for (std::size_t band = 0; band < kMaxBands; ++band) {
                        canonical.bands[band].gain_db =
                            cursor.value(band_gain_param_id(band));
                        canonical.bands[band].muted =
                            cursor.value(band_mute_param_id(band)) >= 0.5f;
                    }

                    const float host_morph = std::clamp(
                        cursor.value(kParamMorph), 0.0f, 1.0f);
                    BandField host_field = canonical;
                    const bool morph_has_both =
                        audio_modulation.snapshots.has(SnapshotBank::Slot::A)
                        && audio_modulation.snapshots.has(SnapshotBank::Slot::B);
                    // Deriving the field from the bank is gated on the control
                    // worker having actually done so. Populating both slots is
                    // not consent to be morphed: the morph parameter defaults
                    // to 0.0, so an ungated derivation replaces the authored
                    // field with snapshot A the instant the second slot is
                    // captured, and every band edit after that is silently
                    // discarded — which is how a muted band came back audible.
                    if (morph_has_both && audio_modulation.morph_derived) {
                        morph_fields(host_field,
                                     audio_modulation.snapshots.a.field,
                                     audio_modulation.snapshots.b.field,
                                     host_morph);
                        // An explicit band write outranks the morph that
                        // derived it — the precedence the control worker
                        // already applies via `morph_overrides_`. The audio
                        // thread re-derives the morph every block, so without
                        // replaying those overrides here it silently reverts
                        // them: a band muted after A and B were captured comes
                        // back un-muted, is HEARD, and (because this same field
                        // feeds the editor's modulation frame) is painted at a
                        // moving height under its own mute badge.
                        const std::uint64_t overrides =
                            audio_modulation.morph_overrides;
                        if (overrides != 0) {
                            for (std::size_t band = 0; band < kMaxBands; ++band)
                                if ((overrides >> band) & 1ull)
                                    host_field.bands[band] =
                                        canonical.bands[band];
                        }
                    }

                    // ── Macros ──────────────────────────────────────────
                    // BEFORE the LFOs, deliberately. A Whole Bank LFO is a
                    // wobble around the level you are listening to, so it has
                    // to wobble around the macro-driven level rather than
                    // around the drawn one — otherwise raising a macro would
                    // slide the whole modulation out from under its own
                    // centre.
                    //
                    // The VALUES come from the cursor, so a macro automated
                    // mid-block lands at its event's sample offset like any
                    // other lane. Only the MEMBERSHIP comes from the
                    // publication, and that is editor state that cannot move
                    // during a block.
                    //
                    // This is the same `apply_macro_offsets` the control
                    // thread calls in `make_mask_layout_`, over the same
                    // inputs. One function, two threads: that is what makes
                    // the mask this block stages and the mask the worker
                    // publishes agree instead of almost-agree.
                    {
                        BandMacroBank bank;
                        for (std::size_t m = 0; m < kMacroCount; ++m) {
                            bank.members[m] = MacroMembership<kMaxBands>(
                                audio_modulation.macro_members[m]);
                            bank.values[m] = cursor.value(macro_param_id(m));
                        }
                        apply_macro_offsets(host_field,
                                            visible_count(automated_layout),
                                            bank);
                    }

                    ModulationSettings modulation_settings;
                    modulation_settings.enabled =
                        cursor.value(kParamLfoEnabled) >= 0.5f;
                    modulation_settings.shape = static_cast<LfoShape>(
                        std::clamp(static_cast<int>(std::lround(
                            cursor.value(kParamLfoShape))), 0, 3));
                    modulation_settings.beats_per_cycle = std::clamp(
                        cursor.value(kParamLfoRate), 0.25f, 16.0f);
                    modulation_settings.depth = std::clamp(
                        cursor.value(kParamLfoDepth), 0.0f, 1.0f);
                    modulation_settings.target =
                        static_cast<ModulationTarget>(std::clamp(
                            static_cast<int>(std::lround(
                                cursor.value(kParamLfoTarget))), 0, 3));
                    // Per-LFO routing, straight off the cursor like every
                    // other LFO lane, so automating a destination on/off or
                    // its amount lands at its event's sample offset.
                    for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
                        for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
                            auto& route = modulation_settings.routes[lfo][t];
                            route.enabled = cursor.value(
                                lfo_route_enabled_param_id(lfo, t)) >= 0.5f;
                            route.amount = std::clamp(cursor.value(
                                lfo_route_amount_param_id(lfo, t)), 0.0f, 1.0f);
                        }
                    }
                    modulation_settings.lfo2_enabled =
                        cursor.value(kParamLfo2Enabled) >= 0.5f;
                    modulation_settings.lfo2_shape = static_cast<LfoShape>(
                        std::clamp(static_cast<int>(std::lround(
                            cursor.value(kParamLfo2Shape))), 0, 3));
                    modulation_settings.lfo2_beats_per_cycle = std::clamp(
                        cursor.value(kParamLfo2Rate), 0.25f, 16.0f);
                    modulation_settings.lfo2_depth = std::clamp(
                        cursor.value(kParamLfo2Depth), 0.0f, 1.0f);
                    // The legacy lanes are COMMANDS (see apply_surface_params):
                    // a move of the single-target lane (4004) selects that one
                    // field destination for both LFOs, and a move of an LFO's
                    // Depth lane (4003/4013) sets the Depth of every target
                    // that LFO drives. The control worker turns a command into
                    // routing-lane writes one pass later; until then it is
                    // latched here so the moved lane is heard at once.
                    //
                    // A command applies only when it moves ALONE. A slice that
                    // also moves an LFO's routing lanes (a host restoring or
                    // setting every parameter at once) is an explicit routing
                    // statement, and the command does not touch that LFO --
                    // the same rule the worker applies, so what is heard
                    // matches what the lanes read back.
                    {
                        auto& legacy = audio_legacy_lanes_;
                        if (!legacy.primed) {
                            legacy.depth[0] = audio_modulation.settings.depth;
                            legacy.depth[1] = audio_modulation.settings.lfo2_depth;
                            legacy.target = static_cast<int>(
                                audio_modulation.settings.target);
                            legacy.routes = audio_modulation.settings.routes;
                            legacy.depth_command[0] = legacy.depth_command[1] = false;
                            legacy.target_command[0] = legacy.target_command[1] = false;
                            legacy.primed = true;
                        }
                        const float lane[2] = {modulation_settings.depth,
                                               modulation_settings.lfo2_depth};
                        const int target = static_cast<int>(modulation_settings.target);
                        const bool target_moved = target != legacy.target;
                        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
                            // A lane that moved onto the published routing is
                            // the worker's own write of a command landing, not
                            // a host statement, and does not count.
                            bool routing_moved = false;
                            for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
                                const auto& now = modulation_settings.routes[lfo][t];
                                const auto& seen = legacy.routes[lfo][t];
                                const auto& published =
                                    audio_modulation.settings.routes[lfo][t];
                                if ((now.enabled != seen.enabled
                                     && now.enabled != published.enabled)
                                    || (now.amount != seen.amount
                                        && now.amount != published.amount))
                                    routing_moved = true;
                            }
                            legacy.routes[lfo] = modulation_settings.routes[lfo];
                            if (target_moved) legacy.target_command[lfo] = !routing_moved;
                            else if (routing_moved) legacy.target_command[lfo] = false;
                            if (lane[lfo] != legacy.depth[lfo])
                                legacy.depth_command[lfo] = !routing_moved;
                            else if (routing_moved)
                                legacy.depth_command[lfo] = false;
                            legacy.depth[lfo] = lane[lfo];
                        }
                        legacy.target = target;
                        const auto bit = modulation_target_bit(modulation_settings.target);
                        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
                            auto& routes = modulation_settings.routes[lfo];
                            if (legacy.target_command[lfo])
                                set_route_mask(routes, static_cast<std::uint16_t>(
                                    (route_mask(routes)
                                     & ~static_cast<std::uint16_t>(kModulationTargetMaskAll))
                                    | bit));
                            if (legacy.depth_command[lfo])
                                for (auto& route : routes)
                                    if (route.enabled) route.amount = lane[lfo];
                        }
                    }
                    if (should_reset_stream_history && block_offset == 0) {
                        audio_modulation_phase_ =
                            ctx.position_beats
                            / std::max(0.0625, static_cast<double>(
                                modulation_settings.beats_per_cycle));
                        audio_modulation_phase_ -=
                            std::floor(audio_modulation_phase_);
                        audio_modulation_phase_2_ =
                            ctx.position_beats
                            / std::max(0.0625, static_cast<double>(
                                modulation_settings.lfo2_beats_per_cycle));
                        audio_modulation_phase_2_ -=
                            std::floor(audio_modulation_phase_2_);
                    }
                    if (!audio_lfo_shape_fade_primed_
                        || (should_reset_stream_history
                            && block_offset == 0)) {
                        audio_lfo_shape_fade_ =
                            settled_lfo_shape(modulation_settings.shape);
                        audio_lfo_2_shape_fade_ =
                            settled_lfo_shape(modulation_settings.lfo2_shape);
                        audio_lfo_shape_fade_primed_ = true;
                    }
                    audio_lfo_shape_fade_ = retarget_lfo_shape(
                        audio_lfo_shape_fade_, modulation_settings.shape);
                    audio_lfo_2_shape_fade_ = retarget_lfo_shape(
                        audio_lfo_2_shape_fade_,
                        modulation_settings.lfo2_shape);
                    // ── Freeze target ───────────────────────────────────
                    // A gate per LFO (the target's Depth is the frozen duty),
                    // OR-ed. Read before the level slew below: the freeze
                    // source has its own engage/release crossfade, so the
                    // gate is a clean on/off.
                    {
                        constexpr auto kFreeze =
                            static_cast<std::size_t>(ModulationTarget::Freeze);
                        bool driven = false, gate = false;
                        const bool lfo_on[2] = {modulation_settings.enabled,
                                                modulation_settings.lfo2_enabled};
                        const LfoShapeFade* fades[2] = {&audio_lfo_shape_fade_,
                                                        &audio_lfo_2_shape_fade_};
                        const double phases[2] = {audio_modulation_phase_,
                                                  audio_modulation_phase_2_};
                        for (std::size_t lfo = 0; lfo < 2; ++lfo) {
                            const auto& route = modulation_settings.routes[lfo][kFreeze];
                            if (!lfo_on[lfo] || !route.enabled) continue;
                            driven = true;
                            gate = gate || lfo_freeze_gate(fades[lfo]->to, phases[lfo],
                                                           route.amount);
                        }
                        // Hold for Length: a rising edge of the gate latches
                        // the freeze for exactly the effective Length at that
                        // edge (the Length target's step included), then
                        // releases; the LFO's off-phase is ignored meanwhile,
                        // and the next rising edge after the release latches
                        // again for the Length in effect then. The release
                        // lands on the slice boundary at or after the Length
                        // (at most one slice late).
                        //
                        // Every latch is a RETRIGGER of the freeze source.
                        // A rising edge on the slice a hold ends (a Length
                        // that is a whole number of LFO cycles: the default
                        // 1 bar at the default 4 beats), or a slice or two
                        // after it, inside one hop, never shows the source a
                        // fallen request; it would play the first loop for
                        // ever. The retrigger releases the old hold and
                        // latches fresh audio at the new Length; the LENGTH
                        // label takes the new length, and a press held over
                        // the old hold ends.
                        //
                        // ONE LFO ON BOTH. An LFO that drives Freeze and
                        // Length rises at the same phase every cycle, so read
                        // at the trigger every hold would take one length.
                        // Its Length route is read freeze_hold_walk_ eighths
                        // of a cycle further along instead: successive holds
                        // step through the wave's shape, an eight-hold cycle
                        // of lengths that follows its shape and Depth. The
                        // walk counts latches since Hold for Length (or its
                        // Freeze drive) came on, a transport start or stop, a
                        // stream reset or a state load -- all stream events,
                        // so a bounce and real-time playback walk alike. A
                        // Length route on the other LFO is read at the
                        // trigger: its phase already differs freeze to freeze.
                        bool latched = false;
                        int latched_index = -1;
                        {
                            const bool hold_mode = driven
                                && cursor.value(kParamFreezeHoldForLength) >= 0.5f;
                            if (!hold_mode || should_reset_stream_history
                                || ctx.is_playing != freeze_hold_playing_last_
                                || freeze_hold_walk_reset_.exchange(
                                       false, std::memory_order_relaxed))
                                freeze_hold_walk_ = 0;
                            freeze_hold_playing_last_ = ctx.is_playing;
                            const bool rising = gate && !freeze_hold_gate_last_;
                            freeze_hold_gate_last_ = gate;
                            const auto slice = static_cast<std::int64_t>(
                                out_slice.num_samples());
                            if (!hold_mode) {
                                freeze_hold_remaining_ = 0;
                                freeze_hold_seconds_ = 0.0;
                            } else if (freeze_hold_remaining_ > 0) {
                                gate = true;
                                freeze_hold_remaining_ -= slice;
                            } else if (rising) {
                                const double rate = ctx.sample_rate > 0.0
                                    ? ctx.sample_rate : sample_rate_;
                                constexpr auto kLength =
                                    static_cast<std::size_t>(ModulationTarget::Length);
                                double length_phases[2] = {audio_modulation_phase_,
                                                           audio_modulation_phase_2_};
                                for (std::size_t lfo = 0; lfo < 2; ++lfo)
                                    if (lfo_on[lfo]
                                        && modulation_settings.routes[lfo][kFreeze].enabled
                                        && modulation_settings.routes[lfo][kLength].enabled)
                                        length_phases[lfo] += kFreezeHoldWalkStep
                                            * static_cast<double>(freeze_hold_walk_);
                                freeze_hold_walk_ = (freeze_hold_walk_ + 1) % kFreezeHoldWalkCycle;
                                // The hold and the loop it plays take one
                                // length: this one, handed to the source now
                                // (its latch may land later in this callback)
                                // and pinned there until the hold ends (see
                                // the preamble).
                                freeze_hold_seconds_ = freeze_length_at_phases_(
                                    transport_tempo_bpm(), transport_time_sig_numerator(),
                                    transport_time_sig_denominator(), length_phases,
                                    &latched_index, nullptr);
                                freeze_source_.set_hold_seconds(freeze_hold_seconds_);
                                freeze_hold_latched_seconds_.store(
                                    freeze_hold_seconds_, std::memory_order_relaxed);
                                freeze_hold_latch_count_.fetch_add(1, std::memory_order_relaxed);
                                freeze_hold_remaining_ = std::max<std::int64_t>(1,
                                    std::llround(freeze_hold_seconds_ * rate))
                                    - slice;
                                if (freeze_hold_remaining_ < 0) freeze_hold_remaining_ = 0;
                                latched = true;
                                gate = true;
                            } else {
                                freeze_hold_seconds_ = 0.0;
                                gate = false;
                            }
                        }
                        const bool param_frozen = cursor.value(kParamFreeze) >= 0.5f;
                        const int press = freeze_press_request_.exchange(
                            -1, std::memory_order_relaxed);
                        bool frozen = param_frozen;
                        if (driven) {
                            // The user's press (or the lane's automation)
                            // holds until the gate next changes.
                            if (param_frozen != freeze_param_last_) {
                                freeze_user_override_ = true;
                                freeze_user_value_ = param_frozen;
                            }
                            if (press >= 0) {
                                freeze_user_override_ = true;
                                freeze_user_value_ = press == 1;
                            }
                            if (gate != freeze_gate_last_ || latched)
                                freeze_user_override_ = false;
                            frozen = freeze_user_override_ ? freeze_user_value_ : gate;
                        } else {
                            freeze_user_override_ = false;
                        }
                        freeze_gate_last_ = gate;
                        freeze_param_last_ = param_frozen;
                        freeze_source_.set_frozen(frozen);
                        if (latched && frozen) freeze_source_.retrigger();
                        freeze_effective_.store(frozen, std::memory_order_relaxed);
                        freeze_gate_driven_.store(driven, std::memory_order_relaxed);
                        // The length this engage takes is the one the LENGTH
                        // label shows while it holds; a retrigger is a new
                        // engage.
                        if (frozen && (!freeze_engage_last_ || latched))
                            freeze_engaged_length_index_.store(
                                latched ? latched_index
                                        : freeze_modulated_length_index_.load(
                                              std::memory_order_relaxed),
                                std::memory_order_relaxed);
                        freeze_engage_last_ = frozen;
                    }
                    // Slew each LFO's audible level, then let the slewed
                    // value stand in for enabled + depth everywhere below:
                    // the modulation, the activity flag and the editor's
                    // publication all see the same ramp, so the drawn overlay
                    // fades exactly as the sound does.
                    {
                        // An LFO's level is its on/off alone: how far it
                        // moves each target is that target's own Depth.
                        const float targets[2] = {
                            modulation_settings.enabled ? 1.0f : 0.0f,
                            modulation_settings.lfo2_enabled ? 1.0f : 0.0f};
                        const double level_seconds =
                            static_cast<double>(out_slice.num_samples())
                            / (ctx.sample_rate > 0.0 ? ctx.sample_rate
                                                     : sample_rate_);
                        for (std::size_t lfo = 0; lfo < 2; ++lfo) {
                            audio_lfo_level_[lfo] = audio_lfo_level_primed_
                                ? slew_lfo_level(audio_lfo_level_[lfo],
                                                 targets[lfo], level_seconds)
                                : targets[lfo];
                            // Each route the same way, and the slewed level
                            // stands in for enabled + amount below.
                            for (std::size_t t = 0; t < kModulationTargetCount; ++t) {
                                auto& route = modulation_settings.routes[lfo][t];
                                const float target_level =
                                    route.enabled ? route.amount : 0.0f;
                                float& level = audio_route_level_[lfo][t];
                                level = (audio_lfo_level_primed_
                                         && !modulation_plants_route_step())
                                    ? slew_lfo_level(level, target_level,
                                                     level_seconds
                                                         * kLfoLevelSlewSeconds
                                                         / route_slew_seconds(t))
                                    : target_level;
                                route.enabled = level > 0.0f;
                                route.amount = level;
                            }
                        }
                        audio_lfo_level_primed_ = true;
                        modulation_settings.enabled = audio_lfo_level_[0] > 0.0f;
                        modulation_settings.depth = audio_lfo_level_[0];
                        modulation_settings.lfo2_enabled =
                            audio_lfo_level_[1] > 0.0f;
                        modulation_settings.lfo2_depth = audio_lfo_level_[1];
                    }
                    const float wave = lfo_value(
                        audio_lfo_shape_fade_, audio_modulation_phase_);
                    const float wave2 = lfo_value(
                        audio_lfo_2_shape_fade_, audio_modulation_phase_2_);
                    // Both LFOs, every routed destination, one composition:
                    // the same pure function the editor evaluates at frame
                    // time, so what is drawn is what is heard.
                    const auto composed = compose_internal_modulation(
                        host_field, audio_modulation.snapshots, host_morph,
                        modulation_settings, wave, wave2);
                    // ── Preset destination ──────────────────────────────
                    // Moves the composed field toward the neighbouring
                    // presets' band gains (menu order) by the coordinate x
                    // kPresetModulationSteps presets: 0 is the field as it
                    // stands, edits included; between two presets the gains
                    // interpolate, the morph's own rule. Mutes are never
                    // moved (preserve_authored_mutes). Stack storage, no
                    // allocation.
                    BandField preset_blended;
                    const BandField* audible_source = &composed.field;
                    {
                        constexpr auto kPresetT =
                            static_cast<std::size_t>(ModulationTarget::Preset);
                        const bool preset_driven =
                            audio_modulation.preset.valid
                            && (modulation_settings.routes[0][kPresetT].enabled
                                || modulation_settings.routes[1][kPresetT].enabled);
                        const float offset = preset_driven
                            ? preset_modulation_offset(composed.coords.value[kPresetT],
                                                       audio_modulation.preset.below,
                                                       audio_modulation.preset.above)
                            : 0.0f;
                        audio_preset_driven_.store(preset_driven, std::memory_order_relaxed);
                        audio_preset_step_.store(preset_modulation_step(offset),
                                                 std::memory_order_relaxed);
                        if (preset_driven && offset != 0.0f) {
                            const int lo = static_cast<int>(std::floor(offset));
                            const float frac = offset - static_cast<float>(lo);
                            const auto gain_at = [&](int step, std::size_t band) {
                                return step == 0
                                    ? composed.field.bands[band].gain_db
                                    : audio_modulation.preset.gains[static_cast<std::size_t>(
                                          kPresetModulationSteps + step)][band];
                            };
                            preset_blended = composed.field;
                            for (std::size_t band = 0; band < kMaxBands; ++band) {
                                const float a = gain_at(lo, band);
                                const float b = frac > 0.0f ? gain_at(lo + 1, band) : a;
                                preset_blended.bands[band].gain_db = std::clamp(
                                    a + (b - a) * frac, kBandGainMinDb, kBandGainMaxDb);
                            }
                            preserve_authored_mutes(preset_blended, host_field);
                            audible_source = &preset_blended;
                        }
                    }
                    const BandField& audible = *audible_source;

                    const auto authored_viewport = decode_viewport(
                        cursor.value(kParamViewportCenter),
                        cursor.value(kParamViewportWidth));
                    // Gated on `morph_derived` for the same reason the bands
                    // are, and it has to be the SAME gate: the window and the
                    // shape drawn inside it must come from one derivation, or
                    // the mask is built for a window the bands were never
                    // mapped to.
                    const auto base_viewport =
                        (morph_has_both
                         && audio_modulation.morph_derived
                         && audio_modulation.morph_applies_viewport)
                        ? morph_viewports(
                              audio_modulation.snapshots.a.viewport,
                              audio_modulation.snapshots.b.viewport,
                              host_morph)
                        : authored_viewport;
                    const Viewport audible_viewport =
                        apply_viewport_modulation(base_viewport, composed.coords);

                    // Hand the post-LFO field to the editor so it can draw the
                    // modulation it is playing. Without this the modulator is
                    // audible but invisible: a band an LFO is sweeping never
                    // moves on screen. Display only — `audible` is a copy, and
                    // apply_internal_modulation deliberately leaves canonical
                    // state and the host parameter lanes untouched.
                    // Depth is part of being active. apply_internal_modulation
                    // returns the canonical field unchanged once depth reaches
                    // zero, so an enabled LFO at depth 0 has nothing to show.
                    // Reporting it as active anyway latches the editor overlay
                    // on for the life of the session: the overlay keeps
                    // ownership of the paint refs, the falling-edge release
                    // below never runs, and every display frame pays for two
                    // choc arrays plus a JSON dispatch carrying canonical
                    // state. This guard is the one apply_internal_modulation
                    // already applies per LFO.
                    const bool modulation_active =
                        modulation_audible(modulation_settings);
                    // The header controls an LFO moves (Intensity, Mix,
                    // Output, Morph, Bands) are drawn at their modulated
                    // value. Those targets do not reshape the field, so they
                    // keep the publication running without claiming the band
                    // overlay (`active` stays the field's own answer).
                    const auto drives_control = [&](ModulationTarget target) {
                        return modulation_drives(modulation_settings, target);
                    };
                    const bool controls_driven =
                        drives_control(ModulationTarget::Intensity)
                        || drives_control(ModulationTarget::Mix)
                        || drives_control(ModulationTarget::Output)
                        || drives_control(ModulationTarget::Morph)
                        || drives_control(ModulationTarget::Bands);
                    const bool publishing = modulation_active || controls_driven;
                    // While running, every block is a new frame. On the falling
                    // edge one last frame carries active=false, which is the
                    // editor's cue to release the overlay and draw canonical
                    // state instead of freezing on the final modulated value.
                    if (publishing || modulated_field_was_active_) {
                        ++modulated_field_sequence_;
                        const auto sequence = modulated_field_sequence_;
                        // The same tempo resolution the phase advance below
                        // uses, so the published rate and the audio owner's
                        // own advance can never disagree.
                        const double publish_tempo = ctx.tempo_bpm > 0.0
                            ? ctx.tempo_bpm : 120.0;
                        const double cycles_per_second = publish_tempo / 60.0;
                        const auto phase_1 = audio_modulation_phase_;
                        const auto phase_2 = audio_modulation_phase_2_;
                        const double rate_1 = cycles_per_second
                            / std::max(0.0625, static_cast<double>(
                                modulation_settings.beats_per_cycle));
                        const double rate_2 = cycles_per_second
                            / std::max(0.0625, static_cast<double>(
                                modulation_settings.lfo2_beats_per_cycle));
                        // SPECTR-RENDER-PATH END
                        //
                        // mach_absolute_time on Apple platforms: a vDSO-style
                        // counter read, no lock and no allocation, so it is
                        // safe on this thread. It timestamps a snapshot the UI
                        // draws from; no audio sample is a function of it,
                        // which is why the scanned region stops here.
                        const auto published_ns = std::chrono::duration_cast<
                            std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now()
                                    .time_since_epoch()).count();
                        modulated_field_publication_.write_with(
                            [&](ModulatedFieldSnapshot& slot) noexcept {
                                slot.field     = audible;
                                slot.sequence  = sequence;
                                slot.active    = modulation_active;
                                slot.controls_driven = controls_driven;
                                slot.pre_field = host_field;
                                slot.settings  = modulation_settings;
                                slot.snapshots = audio_modulation.snapshots;
                                slot.host_morph = host_morph;
                                slot.base_viewport = base_viewport;
                                slot.viewport = audible_viewport;
                                slot.phase     = phase_1;
                                slot.phase_2   = phase_2;
                                slot.phase_per_second   = rate_1;
                                slot.phase_2_per_second = rate_2;
                                slot.shape_fade = audio_lfo_shape_fade_;
                                slot.shape_2_fade = audio_lfo_2_shape_fade_;
                                slot.published_ns = published_ns;
                            });
                    }
                    modulated_field_was_active_ = publishing;

                    pulp::signal::SpectralBandLayout automated;
                    // ── Bands destination ───────────────────────────────
                    // Steps the band count the mask is built with around the
                    // user's BANDS. Structural, but the same path a host
                    // automating the band-count lane takes: the layout holds
                    // all 64 slots preallocated, slots past the user's count
                    // are neutral, and the renderer's swap crossfade carries
                    // the restage between the two banks.
                    {
                        constexpr auto kBandsT =
                            static_cast<std::size_t>(ModulationTarget::Bands);
                        const int user_count =
                            static_cast<int>(visible_count(automated_layout));
                        const bool bands_driven =
                            modulation_settings.routes[0][kBandsT].enabled
                            || modulation_settings.routes[1][kBandsT].enabled;
                        const int wanted = bands_driven && !kBandsTargetDisabled
                            ? modulated_band_count(user_count,
                                                   composed.coords.value[kBandsT])
                            : user_count;
                        // Click-free: a count change crossfades through the
                        // flat response. The shape fades to flat over
                        // kBandsFadeSeconds at the old count, the count
                        // switches while nothing is shaped (so the switch
                        // is silent), and the shape fades back in at the new
                        // count. Every step on the way is a gain restage the
                        // renderer carries like an Intensity move. A switch
                        // straight across -- what a host's band-count lane
                        // does -- sprays broadband energy (measured -13 dB
                        // against a 2 kHz tone, test_modulation_freeze.cpp).
                        const double fade_step =
                            (static_cast<double>(out_slice.num_samples())
                             / (ctx.sample_rate > 0.0 ? ctx.sample_rate : sample_rate_))
                            / kBandsFadeSeconds;
                        // The route switched off also fades back to the
                        // user's count; a host band-count lane with no LFO
                        // on Bands switches straight across, as it always
                        // has (audio_bands_modulated_ tells them apart).
                        if (bands_driven) audio_bands_modulated_ = true;
                        if (audio_bands_playing_ <= 0
                            || (!bands_driven && !audio_bands_modulated_)) {
                            audio_bands_playing_ = wanted;
                            audio_bands_fade_ = 1.0f;
                        } else if (wanted != audio_bands_playing_) {
                            audio_bands_fade_ = std::max(0.0f, audio_bands_fade_
                                - static_cast<float>(fade_step));
                            if (audio_bands_fade_ <= 0.0f) audio_bands_playing_ = wanted;
                        } else if (audio_bands_fade_ < 1.0f) {
                            audio_bands_fade_ = std::min(1.0f, audio_bands_fade_
                                + static_cast<float>(fade_step));
                        } else if (!bands_driven) {
                            audio_bands_modulated_ = false;  // back home
                        }
                        automated.active_bands =
                            static_cast<std::uint32_t>(audio_bands_playing_);
                        audio_bands_shown_.store(bands_driven ? audio_bands_playing_ : 0,
                                                 std::memory_order_relaxed);
                    }
                    // In Spectr the viewport is a DSP input, not a camera:
                    // it sets the band↔frequency mapping the mask is built
                    // from. So when a morph moves the window, the audio owner
                    // has to derive the same window the editor drew, from the
                    // same two snapshots and the same morph value — otherwise
                    // an automated morph would be heard through the authored
                    // window and jump the moment automation stopped.
                    //
                    // This follows the MORPH PARAMETER only. The internal LFOs
                    // below deliberately do not sweep it: their rate reaches
                    // the strobe range, and remapping every band's frequency
                    // span per block is a different order of cost from the
                    // gain-only modulation they were built for.
                    // The internal LFOs reach the window only through the
                    // two viewport destinations, which modulate AROUND the
                    // window above (see apply_viewport_modulation) -- never
                    // the stored viewport lanes.
                    const auto& automated_viewport = audible_viewport;
                    automated.min_hz = automated_viewport.min_hz;
                    automated.max_hz = automated_viewport.max_hz;
                    automated.spacing =
                        pulp::signal::SpectralBandSpacing::logarithmic;
                    automated.edge_policy =
                        pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
                    automated.boundary_kernel =
                        pulp::signal::SpectralMaskBoundaryKernel::hard;
                    automated.transition_fraction = 0.0f;
                    automated.transition_frames = 0;
                    for (std::size_t band = 0;
                         band < automated.active_bands; ++band) {
                        automated.bands[band].gain_db =
                            audible.bands[band].gain_db;
                        automated.bands[band].muted =
                            audible.bands[band].muted;
                    }
                    // ── Intensity + Auto Gain (level_controls.hpp) ──────
                    // Intensity scales the COMPOSED shape -- morph, macros
                    // and LFOs included -- toward flat, once, here. Slewed
                    // per sub-block like an LFO level, so a jump restages
                    // in steps the IR crossfade can carry.
                    {
                        const double slice_seconds_level =
                            static_cast<double>(out_slice.num_samples())
                            / (ctx.sample_rate > 0.0 ? ctx.sample_rate
                                                     : sample_rate_);
                        const float intensity_goal = intensity_factor(
                            cursor.value(kParamIntensity));
                        audio_intensity_ = audio_intensity_primed_
                                && !level_plant("intensity-step")
                            ? slew_intensity(audio_intensity_, intensity_goal,
                                             slice_seconds_level)
                            : intensity_goal;
                        audio_intensity_primed_ = true;
                        // The Intensity destination pulls the slewed knob value
                        // toward flat. Auto Gain below still sees the knob
                        // alone, so it never cancels the LFO.
                        if (!level_plant("intensity-ignored"))
                            apply_intensity(automated, modulated_intensity(
                                audio_intensity_, composed.coords)
                                * audio_bands_fade_);

                        // Auto Gain compensates the shape the user DREW (the
                        // pre-LFO field, after morph and macros) at this
                        // Intensity and Mix. Never computed from the output:
                        // v1 weighs the shape against a fixed K-weighted pink
                        // reference; v2 (what AUTO runs) against the long-term
                        // spectrum of the material the mask is shaping
                        // (auto_gain_material.hpp). A shape edit retargets at
                        // once; v2's material movement arrives as per-sample
                        // events from the estimator's frame grid, applied in
                        // the gain loop below.
                        const bool auto_on = cursor.value(kParamAutoGain) >= 0.5f
                            && !level_plant("autogain-follow-output");
                        const int auto_model =
                            auto_gain_model_.load(std::memory_order_relaxed);
                        const float auto_mix = std::clamp(
                            cursor.value(kMix) / 100.0f, 0.0f, 1.0f);
                        // The drawn shape whether AUTO is on or not: v2's
                        // change detector runs while AUTO is off too.
                        pulp::signal::SpectralBandLayout shape = automated;
                        {
                            for (std::size_t band = 0;
                                 band < shape.active_bands; ++band) {
                                shape.bands[band].gain_db =
                                    host_field.bands[band].gain_db;
                                shape.bands[band].muted =
                                    host_field.bands[band].muted;
                            }
                            apply_intensity(shape, audio_intensity_);
                        }
                        float target_db = 0.0f;
                        bool retarget = false;
                        const bool changed = !auto_gain_last_valid_
                            || auto_on != auto_gain_last_enabled_
                            || auto_model != auto_gain_last_model_
                            || auto_mix != auto_gain_last_mix_
                            || renderer != auto_gain_last_renderer_
                            || !same_mask_layout_(auto_gain_last_shape_, shape);
                        if (auto_model == static_cast<int>(AutoGainModel::reference_v1)) {
                            if (auto_on)
                                target_db = auto_gain_reference_.compensation_db(
                                    shape, auto_mix);
                            retarget = target_db != auto_gain_target_db_;
                            // v1 runs the make-up; v2's estimator and change
                            // detector keep listening, so switching AUTO off
                            // and on (which converts to v2) starts current.
                            (void)auto_gain_material_.begin_slice(
                                shape, auto_mix, /*enabled=*/false, changed, renderer);
                        } else {
                            auto_gain_material_.set_latency(renderer->latency_samples());
                            retarget = auto_gain_material_.begin_slice(
                                shape, auto_mix, auto_on, changed, renderer);
                            target_db = auto_gain_material_.target_db();
                            // Material movement arrives only as the
                            // estimator's events (latency-aligned, applied at
                            // their own samples); the slice start retargets
                            // only for an edit or a prime.
                        }
                        auto_gain_last_shape_ = shape;
                        auto_gain_last_mix_ = auto_mix;
                        auto_gain_last_enabled_ = auto_on;
                        auto_gain_last_model_ = auto_model;
                        auto_gain_last_renderer_ = renderer;
                        auto_gain_last_valid_ = true;
                        if (!auto_gain_primed_) {
                            auto_gain_.set_immediate(
                                std::pow(10.0f, target_db * 0.05f));
                            auto_gain_target_db_ = target_db;
                            auto_gain_primed_ = true;
                        } else if (retarget) {
                            auto_gain_.set_ramp_time(
                                kAutoGainRampSeconds,
                                static_cast<float>(ctx.sample_rate > 0.0
                                    ? ctx.sample_rate : sample_rate_));
                            auto_gain_.set_target(
                                std::pow(10.0f, target_db * 0.05f));
                            auto_gain_target_db_ = target_db;
                        }
                    }
                    // Stage only a mask that is not already live, so a held
                    // automation value does not queue a redesign per block.
                    // Like the publication gate above this is about cost, not
                    // correctness: the swap carries the convolver's delay
                    // line, so an unchanged restage is bit-exact wherever the
                    // worker happens to finish.
                    if (!last_staged_layout_valid_
                        || !same_mask_layout_(last_staged_layout_, automated)) {
                        (void)renderer->set_layout_rt(automated);
                        if (switch_in != nullptr) (void)switch_in->set_layout_rt(automated);
                        last_staged_layout_ = automated;
                        last_staged_layout_valid_ = true;
                    }
                    // This path owns the live mask: a sync publish of the
                    // base mask asked for this block must not replace it.
                    renderer->claim_mask_this_block();
                    // The Mix destination pulls Mix toward dry (the freeze
                    // blend); the mixer's own ramp carries each block's move.
                    {
                        const float slice_mix = modulated_mix(
                            std::clamp(cursor.value(kMix) / 100.0f, 0.0f, 1.0f),
                            composed.coords);
                        renderer->set_mix(slice_mix);
                        if (switch_in != nullptr) switch_in->set_mix(slice_mix);
                    }
                    // The Output destination, in dB, at the end of this
                    // slice. Ramped from the previous slice's value across
                    // the samples below, so a running LFO is a smooth gain
                    // rather than a per-block step.
                    // The END of the slice, so consecutive slices meet:
                    // `composed` is evaluated at the slice's first sample,
                    // and ramping toward that would lag by a slice and steepen
                    // wherever slice lengths differ.
                    float output_mod_end_db = 0.0f;
                    {
                        const auto output_route = static_cast<std::size_t>(
                            ModulationTarget::Output);
                        const bool routed =
                            modulation_settings.routes[0][output_route].enabled
                            || modulation_settings.routes[1][output_route].enabled;
                        if (routed) {
                            const double slice_beats =
                                static_cast<double>(out_slice.num_samples())
                                * (ctx.tempo_bpm > 0.0 ? ctx.tempo_bpm : 120.0)
                                / (60.0 * (ctx.sample_rate > 0.0 ? ctx.sample_rate
                                                                 : sample_rate_));
                            const float wave_end = lfo_value(
                                audio_lfo_shape_fade_,
                                audio_modulation_phase_ + slice_beats / std::max(
                                    0.0625, static_cast<double>(
                                        modulation_settings.beats_per_cycle)));
                            const float wave2_end = lfo_value(
                                audio_lfo_2_shape_fade_,
                                audio_modulation_phase_2_ + slice_beats / std::max(
                                    0.0625, static_cast<double>(
                                        modulation_settings.lfo2_beats_per_cycle)));
                            output_mod_end_db = output_modulation_db(
                                modulation_coordinates(modulation_settings,
                                                       wave_end, wave2_end));
                        }
                    }
                    const float output_mod_start_db =
                        (audio_output_mod_primed_
                         && !modulation_plants_level_target_step())
                        ? audio_output_mod_db_ : output_mod_end_db;
                    audio_output_mod_db_ = output_mod_end_db;
                    audio_output_mod_primed_ = true;
                    const bool output_modulated =
                        output_mod_start_db != 0.0f || output_mod_end_db != 0.0f;

                    for (std::size_t channel = 0;
                         channel < out_slice.num_channels(); ++channel) {
                        input_channels_[channel] =
                            in_slice.channel(channel).data();
                        output_channels_[channel] =
                            out_slice.channel(channel).data();
                    }
                    const bool processed = render_through_(
                        renderer, switch_in,
                        input_channels_.data(), output_channels_.data(),
                        static_cast<int>(out_slice.num_samples()));
                    // v2's events due in this slice, in sample order: each
                    // starts at its own stream sample, whatever the slicing.
                    const std::int64_t auto_slice_start = auto_gain_material_.slice_start();
                    const float auto_rate = static_cast<float>(
                        ctx.sample_rate > 0.0 ? ctx.sample_rate : sample_rate_);
                    const auto take_auto_events = [&](std::int64_t up_to) {
                        while (const auto* due = auto_gain_material_.take_due(up_to)) {
                            auto_gain_.set_ramp_time(due->ramp_seconds, auto_rate);
                            auto_gain_.set_target(std::pow(10.0f, due->target_db * 0.05f));
                            auto_gain_target_db_ = due->target_db;
                        }
                    };
                    if (!processed) {
                        auto_gain_.skip(static_cast<int>(out_slice.num_samples()));
                        output_gain_.skip(static_cast<int>(out_slice.num_samples()));
                        for (std::size_t channel = 0;
                             channel < out_slice.num_channels(); ++channel) {
                            auto dst = out_slice.channel(channel);
                            std::fill(dst.begin(), dst.end(), 0.0f);
                        }
                    } else {
                        if (level_plant("autogain-follow-output")
                            && cursor.value(kParamAutoGain) >= 0.5f) {
                            // The rejected design: match the output's level
                            // to the input's, block by block.
                            double in_e = 0.0, out_e = 0.0;
                            for (std::size_t channel = 0;
                                 channel < out_slice.num_channels(); ++channel)
                                for (std::size_t sample = 0;
                                     sample < out_slice.num_samples(); ++sample) {
                                    const double a = input_channels_[channel][sample];
                                    const double b = output_channels_[channel][sample];
                                    in_e += a * a;
                                    out_e += b * b;
                                }
                            const float follow_db = (in_e > 0.0 && out_e > 0.0)
                                ? static_cast<float>(std::clamp(
                                      10.0 * std::log10(in_e / out_e), -24.0, 12.0))
                                : 0.0f;
                            auto_gain_.set_immediate(std::pow(10.0f, follow_db * 0.05f));
                            auto_gain_target_db_ = follow_db;
                        }
                        for (std::size_t sample = 0;
                             sample < out_slice.num_samples(); ++sample) {
                            const auto absolute_sample = static_cast<int32_t>(
                                block_offset + sample);
                            // Auto Gain sits before Output trim: the trim
                            // stays the user's last word on level. Exactly
                            // 1.0f when off and settled, an identity.
                            // Scheduled automation (events) is sample-exact,
                            // as before. A store write with no event (an
                            // editor knob, an AU set-parameter) rides the
                            // block path's 10 ms smoother: this path now runs
                            // whenever a level control is engaged, and it must
                            // not step where the block path would not.
                            const float trim_target = std::pow(
                                10.0f,
                                cursor.value_at(kOutputTrim, absolute_sample)
                                    * 0.05f);
                            float trim_gain = trim_target;
                            if (has_events) {
                                output_gain_.set_immediate(trim_target);
                            } else {
                                if (trim_target != output_gain_.target())
                                    output_gain_.set_target(trim_target);
                                trim_gain = output_gain_.next();
                            }
                            take_auto_events(auto_slice_start
                                             + static_cast<std::int64_t>(sample));
                            float gain = trim_gain * auto_gain_.next();
                            // The Output destination rides on top of Auto
                            // Gain and the trim: Auto Gain never sees it, and
                            // the trim plus the LFO stay inside the lane's
                            // range.
                            if (output_modulated) {
                                const float along =
                                    static_cast<float>(sample + 1)
                                    / static_cast<float>(out_slice.num_samples());
                                const float mod_db = output_mod_start_db
                                    + (output_mod_end_db - output_mod_start_db) * along;
                                const float trim_db =
                                    cursor.value_at(kOutputTrim, absolute_sample);
                                const float applied_db = std::clamp(
                                    trim_db + mod_db, kOutputTrimMinDb,
                                    kOutputTrimMaxDb) - trim_db;
                                gain *= std::pow(10.0f, applied_db * 0.05f);
                            }
                            for (std::size_t channel = 0;
                                 channel < out_slice.num_channels(); ++channel)
                                output_channels_[channel][sample] *= gain;
                        }
                    }
                    // A frame that completed on the slice's last sample moves
                    // the target from the next slice's first sample: the same
                    // sample however the host cut the stream.
                    take_auto_events(auto_slice_start
                                     + static_cast<std::int64_t>(out_slice.num_samples()));
                    const double sample_rate = ctx.sample_rate > 0.0
                        ? ctx.sample_rate : sample_rate_;
                    const double tempo = ctx.tempo_bpm > 0.0
                        ? ctx.tempo_bpm : 120.0;
                    const double beats_per_cycle = std::max(
                        0.0625, static_cast<double>(
                            modulation_settings.beats_per_cycle));
                    audio_modulation_phase_ +=
                        (static_cast<double>(out_slice.num_samples())
                         * tempo / (60.0 * sample_rate)) / beats_per_cycle;
                    audio_modulation_phase_ -=
                        std::floor(audio_modulation_phase_);
                    const double beats_per_cycle_2 = std::max(
                        0.0625, static_cast<double>(
                            modulation_settings.lfo2_beats_per_cycle));
                    audio_modulation_phase_2_ +=
                        (static_cast<double>(out_slice.num_samples())
                         * tempo / (60.0 * sample_rate)) / beats_per_cycle_2;
                    audio_modulation_phase_2_ -=
                        std::floor(audio_modulation_phase_2_);
                    const double slice_seconds =
                        static_cast<double>(out_slice.num_samples())
                        / sample_rate;
                    audio_lfo_shape_fade_ = advance_lfo_shape(
                        audio_lfo_shape_fade_, slice_seconds);
                    audio_lfo_2_shape_fade_ = advance_lfo_shape(
                        audio_lfo_2_shape_fade_, slice_seconds);
                    block_offset += out_slice.num_samples();
                });

            const auto last_sample = static_cast<int32_t>(
                output.num_samples() > 0 ? output.num_samples() - 1 : 0);
            audio_mix_percent_ = params.value_at(kMix, last_sample);
            audio_output_trim_db_ = params.value_at(kOutputTrim, last_sample);
            audio_intensity_percent_ = params.value_at(kParamIntensity, last_sample);
            audio_auto_gain_param_ = params.value_at(kParamAutoGain, last_sample);
            {
                const float applied = auto_gain_.current();
                auto_gain_applied_db_.store(
                    applied > 0.0f ? 20.0f * std::log10(applied) : 0.0f,
                    std::memory_order_relaxed);
            }

            const auto nc = output.num_channels();
            if (nc > 0 && nc <= 8) {
                const float* ptrs[8];
                for (std::size_t ch = 0; ch < nc; ++ch)
                    ptrs[ch] = output.channel(ch).data();
                bridge_.process(ptrs, static_cast<int>(nc),
                                static_cast<int>(output.num_samples()));
            }
            // Sampled BEFORE the block ran, so a write that lands while it is
            // running still reads as drift on the next one.
            audio_applied_surface_ = audio_surface_scratch_;
            audio_applied_surface_valid_ = true;
            return;
        }

        for (std::size_t channel = 0; channel < output.num_channels(); ++channel) {
            input_channels_[channel] = input.channel(channel).data();
            output_channels_[channel] = output.channel(channel).data();
        }
        renderer->set_mix(std::clamp(mix, 0.0f, 1.0f));
        if (switch_in != nullptr) switch_in->set_mix(std::clamp(mix, 0.0f, 1.0f));
        {
            // No LFO is running on this path, so nothing gates the freeze.
            const bool frozen = state().get_value(kParamFreeze) >= 0.5f;
            freeze_source_.set_frozen(frozen);
            freeze_effective_.store(frozen, std::memory_order_relaxed);
            freeze_gate_driven_.store(false, std::memory_order_relaxed);
            if (frozen && !freeze_engage_last_)
                freeze_engaged_length_index_.store(
                    freeze_modulated_length_index_.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
            freeze_engage_last_ = frozen;
            audio_bands_shown_.store(0, std::memory_order_relaxed);
            audio_preset_driven_.store(false, std::memory_order_relaxed);
            freeze_param_last_ = frozen;
            freeze_user_override_ = false;
            // Nor does any Hold for Length hold play on.
            freeze_hold_remaining_ = 0;
            freeze_hold_seconds_ = 0.0;
            freeze_hold_walk_ = 0;
        }
        audio_mix_percent_ = mix * 100.0f;
        audio_output_trim_db_ = out_trim_db;
        audio_intensity_percent_ = state().get_value(kParamIntensity);
        audio_auto_gain_param_ = state().get_value(kParamAutoGain);
        const bool processed = render_through_(
            renderer, switch_in,
            input_channels_.data(), output_channels_.data(),
            static_cast<int>(output.num_samples()));

        // The shared processor already mixed latency-aligned dry and wet.
        // Output trim remains a product-level post gain.
        if (target_output_gain != output_gain_.target())
            output_gain_.set_target(target_output_gain);
        if (!processed) {
            output_gain_.skip(static_cast<int>(output.num_samples()));
            for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
                auto dst = output.channel(ch);
                std::fill(dst.begin(), dst.end(), 0.0f);
            }
        } else {
            // Advance the gain once per frame, then apply that same value to
            // every channel so stereo/multichannel relationships stay exact.
            for (std::size_t sample = 0; sample < output.num_samples(); ++sample) {
                const float out_gain = output_gain_.next();
                for (std::size_t ch = 0; ch < output.num_channels(); ++ch)
                    output_channels_[ch][sample] *= out_gain;
            }
        }

        // Publish post-engine audio to the UI thread via VisualizationBridge.
        const auto nc = output.num_channels();
        if (nc > 0 && nc <= 8) {
            const float* ptrs[8];
            for (std::size_t ch = 0; ch < nc; ++ch) {
                ptrs[ch] = output.channel(ch).data();
            }
            bridge_.process(ptrs, static_cast<int>(nc),
                            static_cast<int>(output.num_samples()));
        }
        return;
    }

    // Invalid or unprepared audio geometry fails closed.
    for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
        auto dst = output.channel(ch);
        std::fill(dst.begin(), dst.end(), 0.0f);
    }
}

// ── Supplemental plugin state (pulp#625) ──────────────────────────────

namespace {

// Turn a FieldSnapshot into a JSON object. Symmetric with
// read_snapshot_() below.
choc::value::Value write_snapshot_(const FieldSnapshot& s) {
    using choc::value::createObject;
    using choc::value::createEmptyArray;

    auto obj = createObject("FieldSnapshot");
    obj.addMember("populated", s.populated);

    auto gains = createEmptyArray();
    auto mutes = createEmptyArray();
    for (const auto& b : s.field.bands) {
        gains.addArrayElement(static_cast<double>(b.gain_db));
        mutes.addArrayElement(b.muted);
    }
    obj.addMember("band_gain", gains);
    obj.addMember("band_mute", mutes);
    obj.addMember("view_min_hz", static_cast<double>(s.viewport.min_hz));
    obj.addMember("view_max_hz", static_cast<double>(s.viewport.max_hz));
    obj.addMember("layout_index", static_cast<int32_t>(layout_to_index(s.layout)));
    return obj;
}

} // namespace

std::vector<uint8_t> Spectr::serialize_plugin_state() const {
    using choc::value::createObject;
    using choc::value::createEmptyArray;

    auto root = createObject("SpectrPluginState");
    root.addMember("version", static_cast<int32_t>(kPluginStateVersion));

    std::lock_guard<std::mutex> lock(processing_state_mutex_);

    // Live band, viewport, layout, and mode values belong to StateStore and
    // are deliberately absent here. A morph is derived from the snapshot
    // bank; only indices edited after the morph need a sparse overlay, whose
    // values also live in StateStore.
    root.addMember("morph_derived", morph_derived_);
    auto morph_overrides = createEmptyArray();
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        if (morph_overrides_.test(i))
            morph_overrides.addArrayElement(static_cast<int32_t>(i));
    }
    root.addMember("morph_overrides", morph_overrides);

    // M8 — snapshot bank. Absent or empty on a v1 blob; new v2 writers
    // always include it so a round-trip preserves the A/B selection
    // across session reloads.
    auto snaps = createObject("SnapshotBank");
    snaps.addMember("active", static_cast<int32_t>(snapshots_.active));
    snaps.addMember("a", write_snapshot_(snapshots_.a));
    snaps.addMember("b", write_snapshot_(snapshots_.b));
    root.addMember("snapshots", snaps);

    // M9.5 — user patterns. PatternLibrary::export_json() emits only
    // user patterns (factory presets are rebuilt at construction) so
    // the blob stays compact and a session reload rebuilds factories
    // from code, not from the stored state. Embedded as a string
    // because the library owns its own envelope shape and versioning
    // — keeps the two serializers decoupled. Absent on a pre-9.5
    // writer; readers treat absence as "no user patterns".
    root.addMember("patterns_json", patterns_.export_json());

    // Internal-modulation destination selection. Every other LFO field is a
    // StateStore parameter and rides the base state blob; this one is editor
    // state with no parameter lane, so without it here a saved preset or
    // session silently loses the user's Targets choice. Absent on a writer
    // that predates the control; readers treat absence as
    // `kModulationTargetMaskUnset`, which reproduces that writer's behaviour
    // exactly — follow the kParamLfoTarget enum — rather than reading as an
    // empty selection that would silence modulation.
    //
    // Written from the parameter lanes it is derived from, not from the
    // reconciled copy: that copy lags a host's parameter writes until the
    // sync worker runs, so the same parameter values could otherwise save
    // two different blobs depending on when the host asked.
    root.addMember("modulation_target_mask",
                   static_cast<int32_t>(param_store_
                       ? modulation_from_store_().target_mask
                       : modulation_.target_mask));
    // Per-LFO routing lives in its own parameter lanes (4020..4055) and rides
    // the base blob. This marker only says those lanes are authoritative; a
    // blob without it predates them and is migrated from the single target
    // and the LFO Depth above. `modulation_target_mask` keeps being written (LFO 1's field
    // destinations) so an older build opening this session hears the
    // nearest thing it can express.
    // 2: each target's Depth is absolute (no LFO-level depth multiplies it).
    root.addMember("lfo_routing", static_cast<int32_t>(2));

    // Whether a morph also moves the viewport. A playback preference with no
    // parameter lane, so like the destination mask it would be silently lost
    // on reload without an entry here. Absent on a writer that predates the
    // switch; readers treat absence as ENABLED, which is what a fresh
    // instance does, so an old session opens behaving like a new one rather
    // than with a feature mysteriously off.
    root.addMember("morph_applies_viewport", morph_applies_viewport_);

    // "Keyboard shortcuts in DAW". Editor state with no parameter lane, like
    // the switch above, so it rides here or is lost on reload. Absent on a
    // writer that predates it; readers treat absence as OFF, the default, so
    // an old session keeps the host's keys where a new instance would.
    root.addMember("keyboard_shortcuts_in_daw", keyboard_shortcuts_in_daw_);
    // "Show tooltips". Absent on an older writer; readers treat absence as ON,
    // the default.
    root.addMember("show_tooltips", show_tooltips_);
    // "Ask before overriding modulation". Absent on an older writer; readers
    // treat absence as ON, the default.
    root.addMember("ask_before_override", ask_before_override_);
    // The Preset destination's neighbourhood, as the editor last resolved it,
    // so the target keeps playing when the session reopens. Absent: none.
    if (preset_neighbours_.valid) {
        auto preset = choc::value::createObject("SpectrPresetModulation");
        preset.addMember("centre", preset_centre_id_);
        preset.addMember("below", static_cast<std::int32_t>(preset_neighbours_.below));
        preset.addMember("above", static_cast<std::int32_t>(preset_neighbours_.above));
        auto names = choc::value::createEmptyArray();
        auto gains = choc::value::createEmptyArray();
        for (std::size_t i = 0; i < kPresetNeighbourCount; ++i) {
            names.addArrayElement(preset_names_[i]);
            auto row = choc::value::createEmptyArray();
            for (const float g : preset_neighbours_.gains[i])
                row.addArrayElement(static_cast<double>(g));
            gains.addArrayElement(row);
        }
        preset.addMember("names", names);
        preset.addMember("gains", gains);
        root.addMember("preset_modulation", preset);
    }
    // Level controls. `level_controls` marks a writer that knows Intensity
    // and Auto Gain: a session WITHOUT it predates them, and opens with Auto
    // Gain off so its level does not change on reload (the parameters
    // themselves ride the base blob). `editor_range_db` is the editor's
    // Range -- editor state with no parameter lane; absent reads as +-24.
    root.addMember("level_controls", static_cast<int32_t>(1));
    root.addMember("editor_range_db", static_cast<int32_t>(editor_range_db_));
    // Auto Gain: which computation AUTO runs (1 = v1, kept by a session that
    // saved AUTO on before v2 until AUTO is toggled; 2 = v2), and v2's
    // estimate of the material, so a reopened project, a bounce and a
    // playback from a locate all start at the level they ended at. The
    // estimate is band-compressed float32 (auto_gain_material.hpp), base64.
    root.addMember("auto_gain_model", static_cast<int32_t>(
        auto_gain_legacy_v1() ? 1 : static_cast<int>(AutoGainModel::material_v2)));
    {
        const auto bands = auto_gain_material_.saved_estimate();
        if (bands.valid) {
            constexpr auto n = pulp_candidate::signal::SpectrumBands::kBands;
            std::vector<float> packed;
            packed.reserve(static_cast<std::size_t>(4 * n));
            for (const auto* row : {&bands.ww, &bands.dd, &bands.re, &bands.im})
                packed.insert(packed.end(), row->begin(), row->end());
            auto estimate = choc::value::createObject("AutoGainEstimate");
            estimate.addMember("bands", static_cast<int32_t>(n));
            estimate.addMember("level_ms", bands.level_ms);
            estimate.addMember("f32", choc::base64::encodeToString(
                packed.data(), packed.size() * sizeof(float)));
            root.addMember("auto_gain_estimate", estimate);
        }
    }
    // Freeze's CUSTOM length (the one the Freeze Length parameter's
    // "Custom" selects; the parameter itself rides the base blob). Exact:
    // whole bars and the fraction's own text, never a float. The held sound
    // itself is not saved.
    {
        const auto custom = freeze_custom_length();
        auto length = choc::value::createObject("FreezeLength");
        length.addMember("bars", static_cast<int32_t>(custom.bars));
        length.addMember("fraction", std::string(fraction_of(custom).text));
        root.addMember("freeze_length", length);
    }

    // Macro membership: four arrays of canonical slot indices, shaped exactly
    // like `morph_overrides` above. The macro VALUES are StateStore
    // parameters and ride the base blob; only the membership is editor state
    // with no lane, so without it a reloaded session would restore four
    // macros that are worth something and drive nothing.
    //
    // NO VERSION BUMP. Like `modulation_target_mask` and
    // `morph_applies_viewport`, this is an OPTIONAL member whose absence has
    // a defined meaning — no macros assigned, which is exactly what a writer
    // predating the feature meant. An old session therefore opens with the
    // macros at 0 dB driving nothing, and a new session opened on an old
    // build drops the membership silently rather than being refused. Bumping
    // would buy nothing and would refuse blobs an older Spectr can render
    // perfectly well.
    auto macro_members = createEmptyArray();
    for (std::size_t m = 0; m < kMacroCount; ++m) {
        auto slots = createEmptyArray();
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            if (macro_members_[m].test(i))
                slots.addArrayElement(static_cast<int32_t>(i));
        }
        macro_members.addArrayElement(slots);
    }
    root.addMember("macro_members", macro_members);

    // Which realisation of the drawn magnitude this project was authored
    // through. Always written, by every writer, from the moment the mode
    // existed -- that is what makes its ABSENCE meaningful rather than
    // ambiguous. A blob without this member can only have come from a build
    // that had one renderer, so a reader knows it was authored linear-phase
    // without having to guess or consult a default. See the reader.
    //
    // Deliberately a token and not an integer: an integer written by a future
    // build that adds a third mode would land inside this build's enum range
    // and silently read as an existing mode. An unrecognised token cannot.
    //
    // Emitted unconditionally, INCLUDING when it equals the default. The
    // tempting economy -- omit when default, infer on read -- is exactly what
    // would make a future change of default silently rewrite the meaning of
    // every project already saved. The blob says what it is.
    root.addMember("render_mode",
                   std::string(render_mode_token(render_mode_)));
    // Mixing's GPU processing choice. Session state, not a host parameter.
    // Absent in older projects, which load with it off (the CPU renderer they
    // were mixed with).
    root.addMember("gpu_processing", gpu_processing_);


    auto json = choc::json::toString(root, /*useLineBreaks=*/false);
    return {json.begin(), json.end()};
}

namespace {

/// Test seam for the recall rule's negative control.
///
/// The rule below -- a project with no stored mode reopens linear phase --
/// is the whole reason this feature is safe to ship, and a rule nobody has
/// watched reject the defect it forbids is not a rule. This lets one ctest row
/// reinstate exactly that defect (absence adopting some other mode instead of
/// the authored one) so the contract can be seen going red. Read once per
/// process; unset in every shipping run, and deliberately not a compile-time
/// flag so the shipping binary is the one the control is proven against.
bool migration_plant_adopts_other_mode_() {
    static const bool planted = [] {
        const char* value = SPECTR_TEST_ENV("SPECTR_RENDER_MODE_PLANT");
        return value != nullptr
            && std::string_view(value) == "migration-adopts-other-mode";
    }();
    return planted;
}

void reset_supplemental_state_(SnapshotBank& bank, PatternLibrary& patterns) {
    bank = SnapshotBank{};
    patterns = PatternLibrary{};  // restores factories, drops user patterns
}

std::optional<float> read_band_gain_(const choc::value::ValueView& value) {
    double gain = 0.0;
    if      (value.isFloat64()) gain = value.getFloat64();
    else if (value.isInt64())   gain = static_cast<double>(value.getInt64());
    else if (value.isInt32())   gain = static_cast<double>(value.getInt32());
    else                        return std::nullopt;

    if (!std::isfinite(gain)) return std::nullopt;
    // Older supplemental-state versions allowed a wider gain range and did
    // not bump the schema when the Release-1 product range narrowed. Clamp in
    // double precision before narrowing so those sessions migrate safely and
    // huge-but-finite JSON numbers can never overflow to a float infinity.
    return static_cast<float>(std::clamp(
        gain, static_cast<double>(kBandGainMinDb),
        static_cast<double>(kBandGainMaxDb)));
}

std::optional<int> read_int_(const choc::value::ValueView& value) {
    if (value.isInt32()) return value.getInt32();
    if (value.isInt64()) {
        const auto number = value.getInt64();
        if (number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max()) return std::nullopt;
        return static_cast<int>(number);
    }
    if (value.isFloat64()) {
        const auto number = value.getFloat64();
        if (!std::isfinite(number)
            || number < static_cast<double>(std::numeric_limits<int>::min())
            || number > static_cast<double>(std::numeric_limits<int>::max()))
            return std::nullopt;
        return static_cast<int>(number);
    }
    return std::nullopt;
}

// Symmetric with write_snapshot_(). Returns true if `obj` was read
// into `dst` without error. An unpopulated slot (empty object, or
// `populated == false`) resets dst to default.
//
// Takes a ValueView (what `parent["key"]` returns) rather than a Value
// so callers don't have to copy the subtree out of the parent.
bool read_snapshot_(const choc::value::ValueView& obj, FieldSnapshot& dst) {
    if (!obj.isObject()) { dst = FieldSnapshot{}; return true; }

    FieldSnapshot staged{};
    staged.populated = false;

    if (obj.hasObjectMember("populated")) {
        const auto e = obj["populated"];
        staged.populated = e.isBool() ? e.getBool() : false;
    }
    if (obj.hasObjectMember("band_gain") && obj["band_gain"].isArray()) {
        auto arr = obj["band_gain"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto gain = read_band_gain_(arr[i]);
            if (!gain) return false;
            staged.field.bands[i].gain_db = *gain;
        }
    }
    if (obj.hasObjectMember("band_mute") && obj["band_mute"].isArray()) {
        auto arr = obj["band_mute"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto e = arr[i];
            staged.field.bands[i].muted = e.isBool() ? e.getBool() : false;
        }
    }
    if (obj.hasObjectMember("view_min_hz")) {
        const auto e = obj["view_min_hz"];
        if      (e.isFloat64()) staged.viewport.min_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   staged.viewport.min_hz = static_cast<float>(e.getInt64());
    }
    if (obj.hasObjectMember("view_max_hz")) {
        const auto e = obj["view_max_hz"];
        if      (e.isFloat64()) staged.viewport.max_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   staged.viewport.max_hz = static_cast<float>(e.getInt64());
    }
    if (!staged.viewport.valid()) staged.viewport = Viewport{};
    if (obj.hasObjectMember("layout_index")) {
        const auto parsed = read_int_(obj["layout_index"]);
        if (!parsed) return false;
        int idx = *parsed;
        idx = std::clamp(idx, 0, static_cast<int>(kLayoutCount) - 1);
        staged.layout = kLayoutValues[static_cast<std::size_t>(idx)];
    }

    dst = staged;
    return true;
}

} // namespace

bool Spectr::deserialize_plugin_state(std::span<const uint8_t> bytes) {
    // A loaded session starts the one-LFO Length walk again (see the Hold
    // for Length latch).
    freeze_hold_walk_reset_.store(true, std::memory_order_relaxed);
    // Empty span = legacy blob or caller signalling "reset to defaults"
    // per the pulp#625 hook contract.
    if (bytes.empty()) {
        BandField store_field{};
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            store_field.bands[i].gain_db = param_store_
                ? param_store_->get_value(band_gain_param_id(i)) : 0.0f;
            store_field.bands[i].muted = param_store_
                && param_store_->get_value(band_mute_param_id(i)) >= 0.5f;
        }
        const auto store_view = param_store_
            ? decode_viewport(param_store_->get_value(kParamViewportCenter),
                              param_store_->get_value(kParamViewportWidth))
            : Viewport{};
        const auto store_layout = param_store_
            ? layout_from_param_value(param_store_->get_value(kParamBandCount))
            : Layout::Bands32;
        {
            std::lock_guard<std::mutex> lock(processing_state_mutex_);
            field_ = store_field;
            viewport_ = store_view;
            layout_ = store_layout;
            reset_supplemental_state_(snapshots_, patterns_);
            if (param_store_) modulation_ = modulation_from_store_();
            else modulation_.target_mask = kModulationTargetMaskUnset;
            morph_applies_viewport_ = true;
            editor_range_db_ = kEditorRangeDefaultDb;
            auto_gain_model_.store(static_cast<int>(kAutoGainShippingModel),
                                   std::memory_order_relaxed);
            auto_gain_legacy_v1_.store(0, std::memory_order_relaxed);
            morph_derived_ = false;
            morph_overrides_.reset();
            for (auto& members : macro_members_) members.reset();
            // An empty payload is the host saying "reset to defaults", and it
            // is the ONE path here that means a fresh instance rather than a
            // restored project. It therefore takes the new-instance default,
            // not the pre-mode migration rule -- there is no project being
            // migrated. Applied below, outside this lock.
            render_mode_unknown_on_load_ = false;
            synced_field_ = field_;
            synced_viewport_ = viewport_;
            synced_layout_ = layout_;
            publish_processing_state_();
        }
        for (std::size_t slot = 0; param_store_ && slot < kSurfaceCacheSlots; ++slot) {
            applied_param_cache_[slot].store(
                param_store_->get_value(detail::surface_slot_param_id(slot)),
                std::memory_order_relaxed);
        }
        // Outside the lock, for the same reason as the main path below.
        (void)set_render_mode(kDefaultRenderMode);
        (void)set_gpu_processing(false);
        // A bare parameter blob predates the level controls by construction:
        // it opens at the level it was mixed at.
        if (param_store_) param_store_->set_value(kParamAutoGain, 0.0f);
        return true;
    }

    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    choc::value::Value root;
    try {
        root = choc::json::parse(text);
    } catch (...) {
        return false;
    }
    if (!root.isObject()) return false;

    // Version gate — accept v1 (legacy pre-M8), v2 (live state plus
    // snapshots), and v3 (parameter-owned live state plus supplemental
    // snapshots/patterns/morph derivation).
    if (!root.hasObjectMember("version")) return false;
    const auto parsed_version = read_int_(root["version"]);
    if (!parsed_version) return false;
    const int version = *parsed_version;
    if (version < 1 || version > kPluginStateVersion) return false;

    // Apply in a staging copy so a malformed payload leaves live state alone.
    BandField new_field{};
    Viewport  new_view{};
    Layout    new_layout = Layout::Bands32;

    if (version >= 3 && param_store_) {
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            new_field.bands[i].gain_db = param_store_->get_value(band_gain_param_id(i));
            new_field.bands[i].muted =
                param_store_->get_value(band_mute_param_id(i)) >= 0.5f;
        }
        new_view = decode_viewport(param_store_->get_value(kParamViewportCenter),
                                   param_store_->get_value(kParamViewportWidth));
        new_layout = layout_from_param_value(param_store_->get_value(kParamBandCount));
    }

    if (root.hasObjectMember("band_gain") && root["band_gain"].isArray()) {
        auto arr = root["band_gain"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto gain = read_band_gain_(arr[i]);
            if (!gain) return false;
            new_field.bands[i].gain_db = *gain;
        }
    }
    if (root.hasObjectMember("band_mute") && root["band_mute"].isArray()) {
        auto arr = root["band_mute"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto e = arr[i];
            new_field.bands[i].muted = e.isBool() ? e.getBool() : false;
        }
    }
    if (root.hasObjectMember("view_min_hz")) {
        const auto e = root["view_min_hz"];
        if      (e.isFloat64()) new_view.min_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   new_view.min_hz = static_cast<float>(e.getInt64());
    }
    if (root.hasObjectMember("view_max_hz")) {
        const auto e = root["view_max_hz"];
        if      (e.isFloat64()) new_view.max_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   new_view.max_hz = static_cast<float>(e.getInt64());
    }
    if (root.hasObjectMember("layout_index")) {
        const auto parsed = read_int_(root["layout_index"]);
        if (!parsed) return false;
        int idx = *parsed;
        idx = std::clamp(idx, 0, static_cast<int>(kLayoutCount) - 1);
        new_layout = kLayoutValues[static_cast<std::size_t>(idx)];
    }

    // Viewport sanity — fall back to defaults on garbage values.
    if (!new_view.valid()) new_view = Viewport{};

    // M8 — snapshot bank (version 2+). Absent or malformed resets the
    // bank to empty; a well-formed block round-trips exactly.
    SnapshotBank new_bank{};
    if (version >= 2 && root.hasObjectMember("snapshots") && root["snapshots"].isObject()) {
        const auto snaps = root["snapshots"];
        if (snaps.hasObjectMember("active")) {
            const auto parsed = read_int_(snaps["active"]);
            if (!parsed) return false;
            const int a = *parsed;
            new_bank.active = (a == 1) ? SnapshotBank::Slot::B : SnapshotBank::Slot::A;
        }
        if (snaps.hasObjectMember("a")
            && !read_snapshot_(snaps["a"], new_bank.a)) return false;
        if (snaps.hasObjectMember("b")
            && !read_snapshot_(snaps["b"], new_bank.b)) return false;
    }

    bool new_morph_derived = false;
    std::bitset<kMaxBands> new_morph_overrides;
    if (version >= 3) {
        if (root.hasObjectMember("morph_derived")) {
            const auto value = root["morph_derived"];
            if (!value.isBool()) return false;
            new_morph_derived = value.getBool();
        }
        if (root.hasObjectMember("morph_overrides")) {
            const auto values = root["morph_overrides"];
            if (!values.isArray()) return false;
            for (std::uint32_t i = 0; i < values.size(); ++i) {
                const auto parsed = read_int_(values[i]);
                if (!parsed || *parsed < 0
                    || *parsed >= static_cast<int>(kMaxBands)) return false;
                new_morph_overrides.set(static_cast<std::size_t>(*parsed));
            }
        }
    }

    // M9.5 — user patterns. Restore into a fresh library so the factory
    // presets are present regardless of what the blob carried, while user
    // IDs, names, order, and default remain exact. Swap-on-success.
    PatternLibrary new_patterns{};
    if (root.hasObjectMember("patterns_json")) {
        if (!root["patterns_json"].isString()) return false;
        const auto s = root["patterns_json"].getString();
        if (!new_patterns.restore_json(std::string_view(s))) return false;
    }

    // Destination selection. Absent on a writer that predates the Targets
    // control: the unset sentinel then reproduces that writer's semantics.
    // ── Render mode ────────────────────────────────────────────────────
    //
    // The recall rule, and it is a hard one: a project authored before this
    // mode existed reopens as linear_phase, ALWAYS, whatever a new instance
    // happens to default to. Absence here is not "no preference, use the
    // default" -- it is positive evidence about how the project was authored,
    // because linear phase was the only renderer that could have produced it.
    // Treating absence as the default would change both how an existing
    // session sounds and the latency it reports to its host, on reopen, with
    // no user action. That is the failure this rule exists to prevent, and it
    // is why kDefaultRenderMode is not consulted anywhere in this function.
    MaskRenderMode new_render_mode = MaskRenderMode::linear_phase;
    if (migration_plant_adopts_other_mode_())
        new_render_mode = MaskRenderMode::zero_latency;  // the forbidden defect
    if (root.hasObjectMember("render_mode")) {
        const auto& value = root["render_mode"];
        if (!value.isString()) return false;
        // An unrecognised mode is refused, not substituted. A project written
        // by a build with a third mode names a realisation this one does not
        // have; rendering it through a different one would change how it
        // sounds and what latency it reports, with nothing said. Failing
        // closed leaves the live state untouched and tells the caller.
        if (!render_mode_from_token(std::string(value.getString()),
                                    new_render_mode))
            return false;
    } else if (version >= 4) {
        // A v4 writer always emits the field, so its absence here is not an
        // old project -- it is a damaged one. Refusing it is what keeps the
        // absent case unambiguous for every version below: at v3 and under,
        // silence can only mean "written before more than one mode existed".
        return false;
    }

    bool new_gpu_processing = false;
    if (root.hasObjectMember("gpu_processing")) {
        const auto& flag = root["gpu_processing"];
        if (!flag.isBool()) return false;
        new_gpu_processing = flag.getBool();
    }

    bool new_morph_applies_viewport = true;
    if (root.hasObjectMember("morph_applies_viewport")) {
        const auto& flag = root["morph_applies_viewport"];
        if (!flag.isBool()) return false;
        new_morph_applies_viewport = flag.getBool();
    }

    bool new_keyboard_shortcuts_in_daw = false;
    if (root.hasObjectMember("keyboard_shortcuts_in_daw")) {
        const auto& flag = root["keyboard_shortcuts_in_daw"];
        if (!flag.isBool()) return false;
        new_keyboard_shortcuts_in_daw = flag.getBool();
    }

    // The Preset destination's neighbourhood; a malformed one is dropped
    // rather than failing the whole state (it is rebuilt on the next preset).
    PresetModulationNeighbours new_preset_neighbours{};
    std::array<std::string, kPresetNeighbourCount> new_preset_names{};
    std::string new_preset_centre;
    if (root.hasObjectMember("preset_modulation")) {
        const auto& preset = root["preset_modulation"];
        bool ok = preset.isObject() && preset.hasObjectMember("centre")
            && preset["centre"].isString() && preset.hasObjectMember("names")
            && preset["names"].isArray() && preset["names"].size() == kPresetNeighbourCount
            && preset.hasObjectMember("gains") && preset["gains"].isArray()
            && preset["gains"].size() == kPresetNeighbourCount;
        for (std::size_t i = 0; ok && i < kPresetNeighbourCount; ++i) {
            const auto& row = preset["gains"][static_cast<std::uint32_t>(i)];
            ok = row.isArray() && row.size() == kMaxBands && preset["names"][static_cast<std::uint32_t>(i)].isString();
            for (std::size_t b = 0; ok && b < kMaxBands; ++b) {
                const auto& v = row[static_cast<std::uint32_t>(b)];
                ok = v.isFloat32() || v.isFloat64() || v.isInt32() || v.isInt64();
                if (ok) new_preset_neighbours.gains[i][b] = std::clamp(
                    static_cast<float>(v.getWithDefault<double>(0.0)), kBandGainMinDb, kBandGainMaxDb);
            }
            if (ok) new_preset_names[i] = std::string(preset["names"][static_cast<std::uint32_t>(i)].getString());
        }
        if (ok) {
            new_preset_centre = std::string(preset["centre"].getString());
            new_preset_neighbours.below = std::clamp(
                static_cast<int>(preset["below"].getWithDefault<std::int64_t>(0)), 0, kPresetModulationSteps);
            new_preset_neighbours.above = std::clamp(
                static_cast<int>(preset["above"].getWithDefault<std::int64_t>(0)), 0, kPresetModulationSteps);
            new_preset_neighbours.valid = !new_preset_centre.empty();
        } else {
            new_preset_neighbours = {};
            new_preset_names = {};
        }
    }

    bool new_show_tooltips = true;
    if (root.hasObjectMember("show_tooltips")) {
        const auto& flag = root["show_tooltips"];
        if (!flag.isBool()) return false;
        new_show_tooltips = flag.getBool();
    }
    bool new_ask_before_override = true;
    if (root.hasObjectMember("ask_before_override")) {
        const auto& flag = root["ask_before_override"];
        if (!flag.isBool()) return false;
        new_ask_before_override = flag.getBool();
    }

    // Level controls (see serialize_plugin_state). A malformed Range falls
    // back to the default rather than losing the session: it is a view.
    const bool knows_level_controls = root.hasObjectMember("level_controls");
    // Auto Gain model (see serialize_plugin_state): absent means the session
    // predates v2, which matters only when it saved AUTO on.
    int saved_auto_gain_model = 0;
    if (root.hasObjectMember("auto_gain_model")) {
        const auto& m = root["auto_gain_model"];
        if (m.isInt32()) saved_auto_gain_model = m.getInt32();
        else if (m.isInt64()) saved_auto_gain_model = static_cast<int>(m.getInt64());
        else if (m.isFloat64()) saved_auto_gain_model = static_cast<int>(m.getFloat64());
    }
    pulp_candidate::signal::SpectrumBands saved_auto_gain_estimate{};
    if (root.hasObjectMember("auto_gain_estimate")) {
        const auto& e = root["auto_gain_estimate"];
        constexpr auto n = pulp_candidate::signal::SpectrumBands::kBands;
        std::vector<std::uint8_t> raw;
        if (e.isObject() && e.hasObjectMember("f32") && e.hasObjectMember("level_ms")
            && e.hasObjectMember("bands") && e["bands"].getWithDefault<int64_t>(0) == n
            && choc::base64::decodeToContainer(raw, e["f32"].getWithDefault<std::string>(""))
            && raw.size() == static_cast<std::size_t>(4 * n) * sizeof(float)) {
            std::vector<float> packed(static_cast<std::size_t>(4 * n));
            std::memcpy(packed.data(), raw.data(), raw.size());
            bool finite = true;
            for (const float v : packed) finite = finite && std::isfinite(v);
            const double level = e["level_ms"].getWithDefault<double>(0.0);
            if (finite && std::isfinite(level) && level > 0.0) {
                auto* rows = std::array<std::array<float, n>*, 4>{
                    &saved_auto_gain_estimate.ww, &saved_auto_gain_estimate.dd,
                    &saved_auto_gain_estimate.re, &saved_auto_gain_estimate.im}.data();
                for (int r = 0; r < 4; ++r)
                    std::copy_n(packed.begin() + static_cast<std::ptrdiff_t>(r * n), n,
                                rows[r]->begin());
                saved_auto_gain_estimate.level_ms = level;
                saved_auto_gain_estimate.valid = true;
            }
        }
    }
    int new_editor_range_db = kEditorRangeDefaultDb;
    if (root.hasObjectMember("editor_range_db")) {
        const auto& range = root["editor_range_db"];
        if (range.isInt32() && valid_editor_range_db(range.getInt32()))
            new_editor_range_db = range.getInt32();
        else if (range.isInt64()
                 && valid_editor_range_db(static_cast<int>(range.getInt64())))
            new_editor_range_db = static_cast<int>(range.getInt64());
        else if (range.isFloat64()
                 && valid_editor_range_db(static_cast<int>(range.getFloat64())))
            new_editor_range_db = static_cast<int>(range.getFloat64());
    }

    // Freeze's custom length. A wrongly TYPED member refuses the blob like
    // every other member here; a well-formed one naming a length this build
    // does not accept (a fraction outside the set, bars past the limit)
    // falls back to the default rather than losing the whole session.
    std::optional<FreezeLength> new_freeze_custom_length;
    if (root.hasObjectMember("freeze_length")) {
        const auto length = root["freeze_length"];
        if (!length.isObject() || !length.hasObjectMember("bars")
            || !length.hasObjectMember("fraction"))
            return false;
        const auto& bars = length["bars"];
        const auto& fraction = length["fraction"];
        if (!(bars.isInt32() || bars.isInt64()) || !fraction.isString()) return false;
        const auto bars_value = bars.isInt32() ? static_cast<std::int64_t>(bars.getInt32())
                                               : bars.getInt64();
        const auto made = bars_value < -1 || bars_value > kMaxLengthBars + 1
            ? std::nullopt
            : make_length(static_cast<int>(bars_value),
                          fraction_index_from_text(fraction.getString()));
        new_freeze_custom_length = made ? *made : kDefaultFreezeLength;
    }
    // A session from before the musical Length kept a Hold length in
    // seconds. Untouched (the old default) it opens at the new default,
    // 1 bar; set by the user, it opens at the musical length nearest those
    // seconds at the transport this instance last saw (120 BPM 4/4 when it
    // has seen none -- a session usually loads before playback starts).
    std::optional<FreezeLength> migrated_freeze_length;
    if (!new_freeze_custom_length && root.hasObjectMember("freeze_hold_seconds")) {
        const auto& seconds = root["freeze_hold_seconds"];
        double old_seconds = FreezeSource::kDefaultHoldSeconds;
        if (seconds.isFloat64()) old_seconds = seconds.getFloat64();
        else if (seconds.isInt32()) old_seconds = seconds.getInt32();
        else if (seconds.isInt64()) old_seconds = static_cast<double>(seconds.getInt64());
        else return false;
        if (!std::isfinite(old_seconds)) return false;
        migrated_freeze_length =
            std::abs(old_seconds - FreezeSource::kDefaultHoldSeconds) < 1.0e-9
                ? kDefaultFreezeLength
                : nearest_length(old_seconds, transport_tempo_bpm(),
                                 transport_time_sig_numerator(),
                                 transport_time_sig_denominator());
    }

    std::array<MacroMembership<kMaxBands>, kMacroCount> new_macro_members{};
    if (root.hasObjectMember("macro_members")) {
        const auto macros = root["macro_members"];
        if (!macros.isArray()) return false;
        // A blob written by a build with MORE macros than this one has is
        // truncated rather than refused: the extra macros are lanes this
        // build does not register, and dropping them loses nothing it could
        // have driven. A blob with fewer leaves the remainder empty.
        const auto n = std::min<std::uint32_t>(
            macros.size(), static_cast<std::uint32_t>(kMacroCount));
        for (std::uint32_t m = 0; m < n; ++m) {
            const auto slots = macros[m];
            if (!slots.isArray()) return false;
            for (std::uint32_t i = 0; i < slots.size(); ++i) {
                const auto parsed = read_int_(slots[i]);
                if (!parsed || *parsed < 0
                    || *parsed >= static_cast<int>(kMaxBands)) return false;
                new_macro_members[m].set(static_cast<std::size_t>(*parsed));
            }
        }
    }

    std::uint8_t new_target_mask = kModulationTargetMaskUnset;
    const bool has_lfo_routing = root.hasObjectMember("lfo_routing");
    int lfo_routing_version = 0;
    if (has_lfo_routing) {
        const auto parsed = read_int_(root["lfo_routing"]);
        if (!parsed) return false;
        lfo_routing_version = *parsed;
    }
    if (root.hasObjectMember("modulation_target_mask")) {
        const auto parsed_mask = read_int_(root["modulation_target_mask"]);
        if (!parsed_mask) return false;
        if (*parsed_mask >= 0 && *parsed_mask <= kModulationTargetMaskAll)
            new_target_mask = static_cast<std::uint8_t>(*parsed_mask);
        else if (*parsed_mask != kModulationTargetMaskUnset)
            return false;
    }

    if (version >= 3 && new_morph_derived) {
        const BandField param_field = new_field;
        const bool has_a = new_bank.has(SnapshotBank::Slot::A);
        const bool has_b = new_bank.has(SnapshotBank::Slot::B);
        const float t = param_store_ ? param_store_->get_value(kParamMorph) : 0.0f;
        if (has_a && has_b) {
            morph_fields(new_field, new_bank.a.field, new_bank.b.field, t);
        } else if (has_a || has_b) {
            new_field = has_a ? new_bank.a.field : new_bank.b.field;
        } else {
            new_morph_derived = false;
            new_morph_overrides.reset();
        }
        if (new_morph_derived) {
            for (std::size_t i = 0; i < kMaxBands; ++i) {
                if (new_morph_overrides.test(i))
                    new_field.bands[i] = param_field.bands[i];
            }
            // The derived viewport is re-derived for the same reason the
            // derived field is: morph never writes the viewport parameters,
            // so the store carries the last AUTHORED window, and taking it at
            // face value would reopen the session with the morphed bands
            // drawn inside the pre-morph window.
            if (new_morph_applies_viewport) {
                if (has_a && has_b)
                    new_view = morph_viewports(new_bank.a.viewport,
                                               new_bank.b.viewport, t);
                else
                    new_view = has_a ? new_bank.a.viewport
                                     : new_bank.b.viewport;
                if (!new_view.valid()) new_view = Viewport{};
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        field_ = new_field;
        viewport_  = new_view;
        snapshots_ = new_bank;
        patterns_  = std::move(new_patterns);
        layout_ = new_layout;
        morph_derived_ = new_morph_derived;
        morph_overrides_ = new_morph_overrides;
        morph_applies_viewport_ = new_morph_applies_viewport;
        keyboard_shortcuts_in_daw_ = new_keyboard_shortcuts_in_daw;
        show_tooltips_ = new_show_tooltips;
        ask_before_override_ = new_ask_before_override;
        preset_neighbours_ = new_preset_neighbours;
        preset_names_ = new_preset_names;
        preset_centre_id_ = new_preset_centre;
        editor_range_db_ = new_editor_range_db;
        if (new_freeze_custom_length)
            (void)set_freeze_custom_length(*new_freeze_custom_length);
        macro_members_ = new_macro_members;
        // Re-derive the LFO lanes from the restored parameters before the
        // mask rides along: the audio thread only honours a published mask
        // while the published target still matches the automation lane, so a
        // target left over from before the restore would make it discard the
        // selection this blob just carried.
        if (param_store_) {
            modulation_ = modulation_from_store_();
        } else {
            modulation_.target_mask = new_target_mask;
        }
        publish_processing_state_();
        if (version >= 3) {
            synced_field_ = field_;
            synced_viewport_ = viewport_;
            synced_layout_ = layout_;
        }
    }
    // Adopt the restored mode OUTSIDE the state lock: switching builds a
    // renderer, which publishes a layout and therefore takes that same lock.
    // If the instance is already running this rebuilds the renderer and tells
    // the host its delay compensation moved; if it is not, it records the mode
    // for the prepare that follows. A restore that cannot build the requested
    // renderer keeps the one it has rather than failing the whole project --
    // the bands are right either way, and a silent mode substitution is
    // reported through render_mode_unknown_on_load().
    // The GPU choice first, so a project that restores into Mixing builds the
    // renderer it was saved with once rather than twice.
    if (render_mode_ != MaskRenderMode::linear_phase
        || new_render_mode == MaskRenderMode::linear_phase)
        (void)set_gpu_processing(new_gpu_processing);
    if (!set_render_mode(new_render_mode)) {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        render_mode_unknown_on_load_ = true;
    }
    if (gpu_processing_ != new_gpu_processing) (void)set_gpu_processing(new_gpu_processing);
    // A session saved before Intensity and Auto Gain existed was mixed at the
    // level it plays at; Auto Gain must not change that on reload, whatever
    // the new-instance default is. Its Intensity lane is absent and keeps the
    // 100 % default, which is an identity.
    if (!knows_level_controls && param_store_)
        param_store_->set_value(kParamAutoGain, 0.0f);
    // A session that saved AUTO on before v2 existed (or saved v1 itself)
    // keeps the level it was mixed at: it runs v1 until the user switches
    // AUTO off and on again. Everything else runs v2.
    {
        const bool auto_on = param_store_ && param_store_->get_value(kParamAutoGain) >= 0.5f;
        const bool legacy = saved_auto_gain_model == static_cast<int>(AutoGainModel::reference_v1)
            || (saved_auto_gain_model == 0 && knows_level_controls && auto_on);
        auto_gain_model_.store(static_cast<int>(legacy ? AutoGainModel::reference_v1
                                                       : AutoGainModel::material_v2),
                               std::memory_order_relaxed);
        auto_gain_legacy_v1_.store(legacy ? 2 : 0, std::memory_order_relaxed);
    }
    if (saved_auto_gain_estimate.valid)
        auto_gain_material_.offer_saved_estimate(saved_auto_gain_estimate);

    if (version < 3) {
        // Migrate legacy supplemental live state into the new parameter-owned
        // representation. Future saves then emit only v3 supplemental data.
        // Host restore is listener-silent and may run off the UI thread; it
        // migrates values without synthesizing user gesture callbacks.
        sync_params_from_field(/*emit_gestures=*/false);
    }
    // The migrated Hold length lands on the Freeze Length parameter (its
    // preset, or Custom). Listener-silent like the rest of a restore.
    if (migrated_freeze_length && param_store_) {
        const int preset = preset_index_of(*migrated_freeze_length);
        if (preset < 0) (void)set_freeze_custom_length(*migrated_freeze_length);
        param_store_->set_value(kParamFreezeLength, static_cast<float>(
            preset < 0 ? kLengthPresetCustom : preset));
    }
    // Per-LFO routing arrived after 1.0.6. A blob without the marker was
    // written by a build where ONE target selection (the 4004 lane, or the
    // "Destinations" mask that overrode it) drove BOTH LFOs, and its routing
    // lanes hold whatever this instance had. Map that selection onto the
    // routing lanes -- "that one on" for each LFO, full amount, no viewport --
    // so the session sounds as it did. Listener-silent, like the rest of a
    // restore.
    // A blob from a development build that stored routing with amounts
    // RELATIVE to an LFO-level depth (`lfo_routing: 1`): fold that depth into
    // each target's Depth so it sounds as it was saved.
    if (lfo_routing_version == 1 && param_store_) {
        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
            const float depth = std::clamp(param_store_->get_value(
                lfo == 0 ? kParamLfoDepth : kParamLfo2Depth), 0.0f, 1.0f);
            for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
                const auto id = lfo_route_amount_param_id(lfo, t);
                param_store_->set_value(id, param_store_->get_value(id) * depth);
            }
        }
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        modulation_ = modulation_from_store_();
        publish_audio_modulation_state_();
    }
    if (!has_lfo_routing && param_store_) {
        ModulationSettings legacy;
        legacy.target = static_cast<ModulationTarget>(std::clamp(
            static_cast<int>(std::lround(param_store_->get_value(kParamLfoTarget))),
            0, 3));
        legacy.target_mask = new_target_mask;
        const std::uint8_t mask = resolve_modulation_target_mask(legacy);
        for (std::size_t lfo = 0; lfo < kRouteLfoCount; ++lfo) {
            // That session's LFO Depth becomes each enabled target's Depth;
            // a target it did not drive takes the default.
            const float depth = std::clamp(param_store_->get_value(
                lfo == 0 ? kParamLfoDepth : kParamLfo2Depth), 0.0f, 1.0f);
            for (std::size_t t = 0; t < kRouteTargetCount; ++t) {
                const bool on = ((mask >> t) & 1u) != 0;
                param_store_->set_value(lfo_route_enabled_param_id(lfo, t),
                                        on ? 1.0f : 0.0f);
                param_store_->set_value(lfo_route_amount_param_id(lfo, t),
                                        on ? depth : 0.5f);
            }
        }
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        modulation_ = modulation_from_store_();
        publish_audio_modulation_state_();
    }
    for (std::size_t slot = 0; param_store_ && slot < kSurfaceCacheSlots; ++slot) {
        applied_param_cache_[slot].store(
            param_store_->get_value(detail::surface_slot_param_id(slot)),
            std::memory_order_relaxed);
    }
    return true;
}

} // namespace spectr
