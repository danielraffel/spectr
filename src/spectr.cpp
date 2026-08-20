#include "spectr/spectr.hpp"
#if !defined(SPECTR_NATIVE_EDITOR)
#include "spectr/ui/editor_view.hpp"
#endif

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>
#include <pulp/runtime/log.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <limits>
#include <string>
#include <string_view>

namespace spectr {

Spectr::Spectr() : editor_authority_(*this) {
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
    native_command_registry_.add_handler(this);
#endif
}

Spectr::~Spectr() {
#if defined(SPECTR_NATIVE_EDITOR)
    native_command_registry_.remove_handler(this);
#endif
}

pulp::format::PluginDescriptor Spectr::descriptor() const {
    return make_descriptor();
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
        publish_processing_state_();
        // The morph moves the morph PARAMETER only — pushing the 64 resulting
        // band values as parameter writes would flood the host per slider
        // move and double-drive the field on automation playback (the morph
        // lane would recompute what the band lanes replay). The synced mirror
        // still advances, so a subsequent band edit pushes only its own delta.
        synced_field_ = field_;
        morph_derived_ = true;
        morph_overrides_.reset();
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
    if (!viewport.valid()) return false;
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        field_ = field;
        viewport_ = viewport;
        layout_ = layout;
        publish_processing_state_();
    }
    sync_params_from_field();
    return true;
}

void Spectr::publish_field() noexcept {
    // DSP publish only; callers that mutate field_ decide whether the change
    // also reaches the host parameters (sync_params_from_field). Morph is
    // the deliberate exception — see apply_morph_to_live.
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    publish_processing_state_();
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
    for (std::size_t i = 0; i < mask_layout.active_bands; ++i) {
        mask_layout.bands[i].gain_db = field_.bands[i].gain_db;
        mask_layout.bands[i].muted = field_.bands[i].muted;
    }
    return mask_layout;
}

bool Spectr::spectral_resolution(
    pulp::signal::SpectralBandResolution& out_resolution) const noexcept {
    if (!processor_prepared_) return false;
    // make_mask_layout_ reads field_/viewport_/layout_; hold the same lock
    // the writers (UI, sync worker, restore) serialize against.
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return pulp::signal::analyze_spectral_band_resolution(
        make_mask_layout_(), kSpectralFftSize,
        static_cast<float>(sample_rate_), out_resolution);
}

void Spectr::publish_processing_state_() noexcept {
    auto mask_layout = make_mask_layout_();

    if (!processor_prepared_) return;
    if (!mask_processor_.publish_layout(mask_layout)) {
        // Invalid control state fails closed; never leave a stale audible
        // table active after a rejected geometry update.
        for (auto& band : mask_layout.bands) band.muted = true;
        mask_layout.min_hz = 20.0f;
        mask_layout.max_hz = std::min(20000.0f,
                                     static_cast<float>(sample_rate_ * 0.5));
        (void)mask_processor_.publish_layout(mask_layout);
    }
}

pulp::view::ABCompare* Spectr::ab_compare() noexcept {
    // Constructed in define_parameters once the StateStore reference is
    // live. Callers that invoke this before define_parameters get a
    // nullptr — don't dereference without checking.
    return ab_.get();
}

void Spectr::prepare(const pulp::format::PrepareContext& ctx) {
    // Re-prepare may overlap a parameter-sync task launched by the previous
    // process cycle. Join it before rebuilding mask_processor_: the worker can
    // publish a compiled layout, and publish_layout() must never race
    // mask_processor_.prepare(). The lane is restarted after the new engine
    // and its initial publication are ready.
    param_sync_lane_.stop();

    sample_rate_ = ctx.sample_rate;
    max_block_   = ctx.max_buffer_size;
    channels_    = std::max(1, ctx.output_channels);

    pulp::signal::SpectralMaskProcessorConfig config;
    config.frame.fft_size = kSpectralFftSize;
    config.frame.analysis_hop = kSpectralAnalysisHop;
    config.frame.channels = channels_;
    config.frame.max_block = std::max(max_block_, 1);
    config.frame.window = pulp::signal::WindowFunction::Type::hann;
    config.sample_rate = static_cast<float>(sample_rate_);
    config.initial_mix = std::clamp(state().get_value(kMix) / 100.0f, 0.0f, 1.0f);
    config.mix_ramp_samples = 64;
    config.mix_curve = pulp::signal::MixCurve::Linear;
    processor_prepared_ = channels_ <= static_cast<int>(kMaximumChannels)
                       && mask_processor_.prepare(config);
    output_gain_.set_ramp_time(0.01f, static_cast<float>(sample_rate_));
    output_gain_.set_immediate(std::pow(
        10.0f, state().get_value(kOutputTrim) * 0.05f));
    // spectr#34: adopt any parameter state written before prepare (a host
    // may restore a session before audio starts). Morph is excluded — the
    // restored field already encodes it; re-deriving would erase post-morph
    // tweaks. The publish below covers whatever the apply changed.
    apply_surface_params(/*apply_morph=*/false);
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        publish_processing_state_();
    }
    // Audio→worker lane for host-automation adoption (see the drift sweep
    // in process()). Restart cleanly across re-prepare.
    if (!param_sync_lane_.start(&Spectr::param_sync_trampoline_, this,
                                pulp::format::BackgroundTaskPolicy::Latest)) {
        pulp::runtime::log_error(
            "[Spectr] parameter sync worker failed to start; host automation "
            "of the band surface will not reach the DSP");
    }
    configure_bridge_(ctx.output_channels);
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
        view.set_bounds({0.0f, 0.0f, static_cast<float>(kEditorDesignWidth),
                         static_cast<float>(kEditorDesignHeight)});
        view.layout_children();
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
    view.set_bounds({0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)});
    view.layout_children();
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

void Spectr::configure_bridge_(int num_channels) {
    pulp::view::VisualizationConfig c;
    c.fft_size         = kAnalyzerFftSize;
    c.hop_size         = kAnalyzerAnalysisHop;
    c.window           = pulp::signal::WindowFunction::Type::hann;
    c.num_channels     = std::max(1, num_channels);
    c.sample_rate      = static_cast<float>(sample_rate_);
    c.capture_waveform = true;
    c.waveform_length  = 1024;
    c.max_frames_per_poll = kAnalyzerMaxFramesPerPoll;
    bridge_.configure(c);
}

void Spectr::release() {
    // Join the sync worker BEFORE touching the mask processor: an in-flight
    // apply publishes into it.
    param_sync_lane_.stop();
    mask_processor_ = {};
    processor_prepared_ = false;
    bridge_.reset();
}

int Spectr::latency_samples() const {
    // Each Release build has one measured, fixed-latency WOLA geometry.
    // Return the prepared engine's exact value when available and the same
    // deterministic contract before prepare so adapters can query early.
    return processor_prepared_ ? mask_processor_.latency_samples()
                               : kSpectralLatency;
}

void Spectr::set_layout(Layout L) {
    {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        layout_ = L;
        publish_processing_state_();
    }
    sync_params_from_field();
}

void Spectr::process(
    pulp::audio::BufferView<float>& output,
    const pulp::audio::BufferView<const float>& input,
    pulp::midi::MidiBuffer& /*midi_in*/,
    pulp::midi::MidiBuffer& /*midi_out*/,
    const pulp::format::ProcessContext& ctx)
{
    // spectr#34: host-side parameter writes (automation playback, generic
    // controls) land in the store between blocks. On any drift, hand the
    // adoption to the sync worker — mask-table compilation is a
    // control-thread operation and never runs here. One lock-free spawn per
    // block at most; the lane's Latest policy coalesces bursts.
    if (processor_prepared_ && surface_params_drifted_())
        param_sync_lane_.try_spawn(ParamSyncTask{});

    // Sync the two continuously automatable audio controls each block.
    const float mix        = state().get_value(kMix) / 100.0f;
    const float out_trim_db= state().get_value(kOutputTrim);
    const float target_output_gain = std::pow(10.0f, out_trim_db * 0.05f);

    // An explicit reset or unexpected seek is a hard DSP-history boundary.
    // Preserve the continuously hot WOLA/dry-delay history across an ordinary
    // host cycle wrap so looping does not emit a fresh startup gap.
#if defined(SPECTR_NATIVE_N1_SDK_COMPAT)
    // The installed 0.803.0 Forge SDK predates should_reset_stream_history().
    // For this standalone-only N1 scaffold, honor explicit resets and avoid
    // treating ordinary loop wraps as cold starts. The shipping format graph
    // continues to compile against the newer, precise helper below.
    const bool should_reset_stream_history = ctx.reset_requested;
#else
    const bool should_reset_stream_history = ctx.should_reset_stream_history();
#endif
    if (should_reset_stream_history) {
        if (processor_prepared_)
            mask_processor_.reset();
        output_gain_.set_immediate(target_output_gain);
    }

    if (processor_prepared_
        && output.num_channels() == static_cast<std::size_t>(channels_)
        && input.num_channels() == static_cast<std::size_t>(channels_)
        && output.num_samples() == input.num_samples()) {
        for (std::size_t channel = 0; channel < output.num_channels(); ++channel) {
            input_channels_[channel] = input.channel(channel).data();
            output_channels_[channel] = output.channel(channel).data();
        }
        mask_processor_.set_mix(std::clamp(mix, 0.0f, 1.0f));
        const bool processed = mask_processor_.process(
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

    auto json = choc::json::toString(root, /*useLineBreaks=*/false);
    return {json.begin(), json.end()};
}

namespace {

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
            morph_derived_ = false;
            morph_overrides_.reset();
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

    if (version >= 3 && new_morph_derived) {
        const BandField param_field = new_field;
        const bool has_a = new_bank.has(SnapshotBank::Slot::A);
        const bool has_b = new_bank.has(SnapshotBank::Slot::B);
        if (has_a && has_b) {
            const float t = param_store_ ? param_store_->get_value(kParamMorph) : 0.0f;
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
        publish_processing_state_();
        if (version >= 3) {
            synced_field_ = field_;
            synced_viewport_ = viewport_;
            synced_layout_ = layout_;
        }
    }
    if (version < 3) {
        // Migrate legacy supplemental live state into the new parameter-owned
        // representation. Future saves then emit only v3 supplemental data.
        // Host restore is listener-silent and may run off the UI thread; it
        // migrates values without synthesizing user gesture callbacks.
        sync_params_from_field(/*emit_gestures=*/false);
    }
    for (std::size_t slot = 0; param_store_ && slot < kSurfaceCacheSlots; ++slot) {
        applied_param_cache_[slot].store(
            param_store_->get_value(detail::surface_slot_param_id(slot)),
            std::memory_order_relaxed);
    }
    return true;
}

} // namespace spectr
