#include "spectr/spectr.hpp"

#include <pulp/runtime/trace.hpp>
#include <pulp/format/param_processing.hpp>
#if !defined(SPECTR_NATIVE_EDITOR)
#include "spectr/ui/editor_view.hpp"
#endif

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>
#include <pulp/runtime/log.hpp>

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
#include <string_view>

namespace spectr {

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

void Spectr::publish_audio_modulation_state_() noexcept {
    // All callers serialize through processing_state_mutex_. TripleBuffer
    // therefore has one logical writer and process() remains its sole reader.
    audio_modulation_publication_.write(AudioModulationState{
        modulation_, snapshots_, morph_applies_viewport_, morph_derived_,
        morph_derived_ ? morph_overrides_.to_ullong() : 0ull});
}

void Spectr::publish_processing_state_() noexcept {
    publish_audio_modulation_state_();
    auto mask_layout = make_mask_layout_();

    if (!processor_prepared_) return;
    if (!renderer_) return;
    // Republishing a mask the renderer is already realising is not free: it
    // queues a redesign that is crossfaded in at whichever block the worker
    // finishes on, and crossfading an impulse response with an identical copy
    // of itself is not a bit-exact identity. The sync worker observes drift
    // often and most of it resolves to the same mask, so without this gate an
    // offline bounce is not reproducible run to run.
    if (last_published_layout_valid_
        && same_mask_layout_(last_published_layout_, mask_layout))
        return;
    if (!renderer_->publish_layout(mask_layout)) {
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

std::unique_ptr<MaskRenderer> Spectr::build_renderer_(MaskRenderMode mode) {
    auto renderer = make_mask_renderer(mode);
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
    last_published_layout_ = layout;
    last_published_layout_valid_ = true;
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
    if (mode == render_mode_) return true;

    // Nothing is prepared yet (a host restoring a project before audio starts,
    // or a test). Record the mode; prepare() builds the matching renderer.
    if (!processor_prepared_) {
        render_mode_ = mode;
        return true;
    }

    // Build the replacement to completion BEFORE retiring the live one. A
    // switch that cannot be prepared must leave the running mode untouched
    // rather than drop the instance into silence.
    auto replacement = build_renderer_(mode);
    if (!replacement) return false;

    MaskRenderer* incoming = replacement.get();
    std::unique_ptr<MaskRenderer> outgoing = std::move(renderer_);
    renderer_ = std::move(replacement);
    render_mode_ = mode;

    // Publish to the audio thread. From here process() renders through the new
    // mode; the old object is still alive and still valid for any call already
    // inside it.
    active_renderer_.store(incoming, std::memory_order_release);

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

    // The host's delay compensation is now wrong by the difference between the
    // two modes. This is the whole reason the switch is observable to a host.
    flag_latency_changed();
    flag_tail_changed();
    return true;
}

void Spectr::prepare(const pulp::format::PrepareContext& ctx) {
    // Re-prepare may overlap a parameter-sync task launched by the previous
    // process cycle. Join it before rebuilding the renderer: the worker can
    // publish a compiled layout, and publish_layout() must never race
    // MaskRenderer::prepare(). The lane is restarted after the new engine
    // and its initial publication are ready.
    param_sync_lane_.stop();

    sample_rate_ = ctx.sample_rate;
    max_block_   = ctx.max_buffer_size;
    channels_    = std::max(1, ctx.output_channels);

    // No audio thread can be running across a prepare, so the previous
    // renderer and anything a mode switch parked are free to go now.
    active_renderer_.store(nullptr, std::memory_order_release);
    renderer_.reset();
    drain_retired_renderers_();

    if (channels_ <= static_cast<int>(kMaximumChannels))
        renderer_ = build_renderer_(render_mode_);
    // The renderer arrives realising the layout build_renderer_ published, so
    // seed the audio thread's cache with it rather than leaving it empty --
    // an empty cache makes the first block restage a mask that is already live.
    last_staged_layout_ = last_published_layout_;
    last_staged_layout_valid_ = last_published_layout_valid_;
    processor_prepared_ = renderer_ != nullptr;
    active_renderer_.store(renderer_.get(), std::memory_order_release);
    // The mask processor was just re-prepared, so nothing this thread applied
    // before survives into it.
    audio_applied_surface_valid_ = false;
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
    active_renderer_.store(nullptr, std::memory_order_release);
    renderer_.reset();
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
    return mask_render_latency_samples(render_mode_, renderer_config_());
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
    MaskRenderer* const renderer = active_renderer_.load(std::memory_order_acquire);

    // spectr#34: host-side parameter writes (automation playback, generic
    // controls) land in the store between blocks. On any drift, hand the
    // adoption to the sync worker — mask-table compilation is a
    // control-thread operation and never runs here. One lock-free spawn per
    // block at most; the lane's Latest policy coalesces bursts.
    const auto surface_drift = processor_prepared_
        ? sample_surface_drift_() : SurfaceDrift{};
    if (surface_drift.worker)
        param_sync_lane_.try_spawn(ParamSyncTask{});

    // Sync the two continuously automatable audio controls each block.
    const float mix        = state().get_value(kMix) / 100.0f;
    const float out_trim_db= state().get_value(kOutputTrim);
    const float target_output_gain = std::pow(10.0f, out_trim_db * 0.05f);

    // An explicit reset or unexpected seek is a hard DSP-history boundary.
    // Preserve the continuously hot WOLA/dry-delay history across an ordinary
    // host cycle wrap so looping does not emit a fresh startup gap.
    const bool should_reset_stream_history = ctx.should_reset_stream_history();
    if (should_reset_stream_history) {
        if (processor_prepared_ && renderer)
            renderer->reset();
        output_gain_.set_immediate(target_output_gain);
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
        if (has_events || modulation_enabled || modulated_field_was_active_
            || surface_drift.audio) {
            std::array<pulp::format::ParamSnapshotEntry,
                       kSurfaceCacheSlots + 2> initial{};
            initial[0] = {kMix, audio_mix_percent_};
            initial[1] = {kOutputTrim, audio_output_trim_db_};
            for (std::size_t slot = 0; slot < kSurfaceCacheSlots; ++slot) {
                initial[slot + 2] = {
                    detail::surface_slot_param_id(slot),
                    applied_param_cache_[slot].load(std::memory_order_relaxed)};
            }

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
                    // An explicit destination selection is editor state and
                    // only reaches this thread through the published snapshot,
                    // one control-thread pass behind the automation lane. When
                    // the lane has moved past the target the selection was
                    // reconciled against, the automated enum wins immediately
                    // rather than being swallowed until that pass lands.
                    modulation_settings.target_mask =
                        modulation_settings.target
                                == audio_modulation.settings.target
                            ? audio_modulation.settings.target_mask
                            : kModulationTargetMaskUnset;
                    modulation_settings.lfo2_enabled =
                        cursor.value(kParamLfo2Enabled) >= 0.5f;
                    modulation_settings.lfo2_shape = static_cast<LfoShape>(
                        std::clamp(static_cast<int>(std::lround(
                            cursor.value(kParamLfo2Shape))), 0, 3));
                    modulation_settings.lfo2_beats_per_cycle = std::clamp(
                        cursor.value(kParamLfo2Rate), 0.25f, 16.0f);
                    modulation_settings.lfo2_depth = std::clamp(
                        cursor.value(kParamLfo2Depth), 0.0f, 1.0f);
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
                    const float wave = lfo_value(
                        modulation_settings.shape, audio_modulation_phase_);
                    BandField audible = apply_internal_modulation(
                        host_field, audio_modulation.snapshots, host_morph,
                        modulation_settings, wave);
                    if (modulation_settings.lfo2_enabled) {
                        const float wave2 = lfo_value(
                            modulation_settings.lfo2_shape,
                            audio_modulation_phase_2_);
                        ModulationSettings second = modulation_settings;
                        second.enabled = true;
                        second.shape = modulation_settings.lfo2_shape;
                        second.beats_per_cycle =
                            modulation_settings.lfo2_beats_per_cycle;
                        second.depth = modulation_settings.lfo2_depth;
                        audible = apply_internal_modulation(
                            audible, audio_modulation.snapshots, host_morph,
                            second, wave2);
                    }

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
                        (modulation_settings.enabled
                         && modulation_settings.depth > 0.0f)
                        || (modulation_settings.lfo2_enabled
                            && modulation_settings.lfo2_depth > 0.0f);
                    // While running, every block is a new frame. On the falling
                    // edge one last frame carries active=false, which is the
                    // editor's cue to release the overlay and draw canonical
                    // state instead of freezing on the final modulated value.
                    if (modulation_active || modulated_field_was_active_) {
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
                                slot.pre_field = host_field;
                                slot.settings  = modulation_settings;
                                slot.snapshots = audio_modulation.snapshots;
                                slot.host_morph = host_morph;
                                slot.phase     = phase_1;
                                slot.phase_2   = phase_2;
                                slot.phase_per_second   = rate_1;
                                slot.phase_2_per_second = rate_2;
                                slot.published_ns = published_ns;
                            });
                    }
                    modulated_field_was_active_ = modulation_active;

                    pulp::signal::SpectralBandLayout automated;
                    automated.active_bands = static_cast<std::uint32_t>(
                        visible_count(automated_layout));
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
                    const auto authored_viewport = decode_viewport(
                        cursor.value(kParamViewportCenter),
                        cursor.value(kParamViewportWidth));
                    // Gated on `morph_derived` for the same reason the bands
                    // are, and it has to be the SAME gate: the window and the
                    // shape drawn inside it must come from one derivation, or
                    // the mask is built for a window the bands were never
                    // mapped to.
                    const auto automated_viewport =
                        (morph_has_both
                         && audio_modulation.morph_derived
                         && audio_modulation.morph_applies_viewport)
                        ? morph_viewports(
                              audio_modulation.snapshots.a.viewport,
                              audio_modulation.snapshots.b.viewport,
                              host_morph)
                        : authored_viewport;
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
                    // Stage only a mask that is not already live. An
                    // unchanged restage is not a no-op inside the renderer:
                    // it queues a redesign that is crossfaded in at whichever
                    // block the worker finishes on, and a crossfade between
                    // an impulse response and an identical copy of itself
                    // does not reproduce it bit-for-bit. Without this gate two
                    // identical offline renders differed, which would break a
                    // null test and any bit-exact ratchet.
                    if (!last_staged_layout_valid_
                        || !same_mask_layout_(last_staged_layout_, automated)) {
                        (void)renderer->set_layout_rt(automated);
                        last_staged_layout_ = automated;
                        last_staged_layout_valid_ = true;
                    }
                    renderer->set_mix(std::clamp(
                        cursor.value(kMix) / 100.0f, 0.0f, 1.0f));

                    for (std::size_t channel = 0;
                         channel < out_slice.num_channels(); ++channel) {
                        input_channels_[channel] =
                            in_slice.channel(channel).data();
                        output_channels_[channel] =
                            out_slice.channel(channel).data();
                    }
                    const bool processed = renderer->process(
                        input_channels_.data(), output_channels_.data(),
                        static_cast<int>(out_slice.num_samples()));
                    if (!processed) {
                        for (std::size_t channel = 0;
                             channel < out_slice.num_channels(); ++channel) {
                            auto dst = out_slice.channel(channel);
                            std::fill(dst.begin(), dst.end(), 0.0f);
                        }
                    } else {
                        for (std::size_t sample = 0;
                             sample < out_slice.num_samples(); ++sample) {
                            const auto absolute_sample = static_cast<int32_t>(
                                block_offset + sample);
                            const float gain = std::pow(
                                10.0f,
                                cursor.value_at(kOutputTrim, absolute_sample)
                                    * 0.05f);
                            for (std::size_t channel = 0;
                                 channel < out_slice.num_channels(); ++channel)
                                output_channels_[channel][sample] *= gain;
                        }
                    }
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
                    block_offset += out_slice.num_samples();
                });

            const auto last_sample = static_cast<int32_t>(
                output.num_samples() > 0 ? output.num_samples() - 1 : 0);
            audio_mix_percent_ = params.value_at(kMix, last_sample);
            audio_output_trim_db_ = params.value_at(kOutputTrim, last_sample);

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
        audio_mix_percent_ = mix * 100.0f;
        audio_output_trim_db_ = out_trim_db;
        const bool processed = renderer->process(
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
    root.addMember("modulation_target_mask",
                   static_cast<int32_t>(modulation_.target_mask));

    // Whether a morph also moves the viewport. A playback preference with no
    // parameter lane, so like the destination mask it would be silently lost
    // on reload without an entry here. Absent on a writer that predates the
    // switch; readers treat absence as ENABLED, which is what a fresh
    // instance does, so an old session opens behaving like a new one rather
    // than with a feature mysteriously off.
    root.addMember("morph_applies_viewport", morph_applies_viewport_);

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
        const char* value = std::getenv("SPECTR_RENDER_MODE_PLANT");
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
            morph_derived_ = false;
            morph_overrides_.reset();
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

    bool new_morph_applies_viewport = true;
    if (root.hasObjectMember("morph_applies_viewport")) {
        const auto& flag = root["morph_applies_viewport"];
        if (!flag.isBool()) return false;
        new_morph_applies_viewport = flag.getBool();
    }

    std::uint8_t new_target_mask = kModulationTargetMaskUnset;
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
        // Re-derive the LFO lanes from the restored parameters before the
        // mask rides along: the audio thread only honours a published mask
        // while the published target still matches the automation lane, so a
        // target left over from before the restore would make it discard the
        // selection this blob just carried.
        if (param_store_) {
            const std::uint8_t restored_mask = new_target_mask;
            modulation_ = modulation_from_store_();
            modulation_.target_mask = restored_mask;
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
    if (!set_render_mode(new_render_mode)) {
        std::lock_guard<std::mutex> lock(processing_state_mutex_);
        render_mode_unknown_on_load_ = true;
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
