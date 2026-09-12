#pragma once

// Spectr — zoomable frequency-slicer audio effect.
//
// See README.md for a product summary and planning/ for the full design
// package. Milestone 1 (Foundation) layered the project; real DSP arrives in
// Milestone 2 (DSP truth spike). State registration (#625 gated) is
// Milestone 4.

#include <pulp/format/processor.hpp>
#include <pulp/format/background_task_lane.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>
#include <pulp/signal/smoothed_value.hpp>
#include <pulp/runtime/triple_buffer.hpp>
#include <pulp/view/ab_compare.hpp>
#include <pulp/view/visualization_bridge.hpp>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#if defined(SPECTR_NATIVE_EDITOR)
#include <filesystem>
#include <pulp/view/command_registry.hpp>
#include <pulp/view/editor_bridge.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/scripted_ui.hpp>
#include "spectr/editor_bridge.hpp"
#endif

#include "spectr/band_state.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/editor_authority.hpp"
#include "spectr/param_surface.hpp"
#include "spectr/pattern.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/viewport.hpp"
#include "spectr/editor_resize.hpp"
#include "spectr/modulation.hpp"

#ifndef SPECTR_FFT_SIZE
#define SPECTR_FFT_SIZE 8192
#endif
#ifndef SPECTR_ANALYSIS_HOP
#define SPECTR_ANALYSIS_HOP 2048
#endif

namespace spectr {

struct ProcessingStateSnapshot {
    BandField field{};
    Viewport viewport{};
    Layout layout = Layout::Bands32;
    SnapshotBank snapshots{};
};

struct AudioModulationState {
    ModulationSettings settings{};
    SnapshotBank snapshots{};
};
static_assert(std::is_trivially_copyable_v<AudioModulationState>,
              "audio modulation publication must remain allocation-free POD");

/// The band field the audio owner is actually rendering this block, after the
/// internal LFOs have been applied.
///
/// This exists so the editor can DRAW what the user hears. The modulated field
/// is computed on the audio thread and consumed by the mask processor; without
/// a publication the modulator is audible but invisible, and a control an LFO
/// is sweeping never moves. `sequence` advances once per processed block so a
/// UI tick can tell a fresh frame from a repeat; `active` is false whenever no
/// modulator is running, which is the editor's cue to fall back to canonical
/// state rather than freeze on the last modulated frame.
///
/// Strictly a display value. It never re-enters canonical state or a host
/// parameter lane — see `apply_internal_modulation`.
struct ModulatedFieldSnapshot {
    BandField     field{};
    std::uint64_t sequence = 0;
    bool          active   = false;

    // ── Display-time reconstruction inputs ──────────────────────────────
    //
    // `field` alone is a zero-order hold: the audio owner samples the LFO once
    // per processed block and the editor consumes it once per display frame.
    // Those two clocks are unrelated, so a 60 Hz consumer reading a ~190 Hz
    // producer advances three, four or five producer steps per painted frame
    // and the animation moves unevenly at every waveform -- the jitter is in
    // the resampling, not in the oscillator.
    //
    // Publishing the LFO's INPUTS as well lets the editor evaluate the same
    // pure `lfo_value` / `apply_internal_modulation` at its own frame time, so
    // the painted value is a continuous function of display time. The DSP is
    // not duplicated: the editor calls the identical functions the audio owner
    // does. `field` remains the audio owner's own last sample.
    BandField          pre_field{};   ///< post-morph, pre-LFO input field
    ModulationSettings settings{};
    /// The bank the audio owner composed against. Carried rather than read
    /// from `Spectr::snapshots()` on the consumer side: a capture lands in the
    /// control-thread bank immediately but only reaches the audio owner one
    /// publication later, and for that tick the two would disagree. Carrying
    /// it makes "the drawn field equals the audible one" exact instead of
    /// almost-always.
    SnapshotBank       snapshots{};
    float              host_morph = 0.0f;
    double             phase = 0.0;   ///< LFO 1 phase at `published_ns`
    double             phase_2 = 0.0; ///< LFO 2 phase at `published_ns`
    double             phase_per_second = 0.0;
    double             phase_2_per_second = 0.0;
    /// steady_clock nanoseconds at which `phase`/`phase_2` were sampled. Zero
    /// means the publication carries no usable clock and the consumer must
    /// fall back to `field` rather than extrapolate from an unknown origin.
    std::int64_t       published_ns = 0;
};
static_assert(std::is_trivially_copyable_v<ModulatedFieldSnapshot>,
              "modulated field publication must remain allocation-free POD");

/// Declare that this build's format gives the user no way to resize the
/// editor, so the editor must draw its own resize grip (see
/// `create_native_editor_`).
///
/// Opt-IN, default false, and asserted by exactly one format entry point:
/// `au_v2_entry.cpp`. AU v2 is the only format Spectr ships where the host
/// offers no resize affordance at all — `AUCocoaUIView` passes a size
/// plugin-ward once at creation and never again, and Logic's plugin window
/// exposes no grow area (no `AXGrowArea`, and it refuses `AXSize`). A
/// plugin-drawn corner grip is the mechanism there, gated on the wrapper type,
/// which is also the pattern JUCE recommends for exactly this asymmetry.
///
/// Every other surface already has a working affordance and must NOT get a
/// second one competing with it:
///   * VST3    — `IPlugView::checkSizeConstraint` / `onSize` (verified in REAPER)
///   * CLAP    — `gui_adjust_size` / `gui_set_size` (verified in REAPER)
///   * AU v3   — host-initiated resize through the plug-in window border
///   * Standalone — macOS owns the bottom-right corner of a resizable NSWindow
///     and consumes press and click there before the content view is asked
///     (measured: grip present, painted, correctly placed, and receiving
///     nothing at all while the window resized underneath it)
///
/// Declared by the entry point rather than sniffed at runtime.
/// `pulp::format::detect_host_type()` cannot answer this: it reports
/// `HostType::Standalone` by matching "pulp" in the process name, and this
/// product's standalone is "Spectr Native Preview". Pulp exposes no
/// wrapper-type enum to a `Processor`, so the linked entry point is the only
/// place that knows the answer.
void set_editor_owns_resize_grip(bool value);
bool editor_owns_resize_grip();

inline constexpr int kSpectralFftSize = SPECTR_FFT_SIZE;
inline constexpr int kSpectralAnalysisHop = SPECTR_ANALYSIS_HOP;
// SpectralFrameEngine reads through a fixed causal cursor of one complete FFT
// frame plus one analysis hop, keeping latency invariant to host block
// partitioning.
inline constexpr int kSpectralLatency =
    kSpectralFftSize + kSpectralAnalysisHop;
// VisualizationBridge publishes at most 4097 bins. Derive analyzer geometry
// from the product profile, but cap Maximum's 16384 processing FFT at 8192 so
// its upper spectrum is never silently truncated.
inline constexpr int kAnalyzerFftSize =
    kSpectralFftSize > 8192 ? 8192 : kSpectralFftSize;
inline constexpr int kAnalyzerAnalysisHopUncapped =
    (kSpectralAnalysisHop * kAnalyzerFftSize + kSpectralFftSize - 1)
        / kSpectralFftSize;
inline constexpr int kAnalyzerAnalysisHop =
    kAnalyzerAnalysisHopUncapped < 1 ? 1
    : (kAnalyzerAnalysisHopUncapped > kAnalyzerFftSize / 2
        ? kAnalyzerFftSize / 2 : kAnalyzerAnalysisHopUncapped);
// At 30 UI polls/s this drains 61,440 frames/s, enough to stay ahead of a
// 48 kHz stream while bounding each UI tick even in a refilling host.
inline constexpr int kAnalyzerMaxFramesPerPoll = 2048;
static_assert(kSpectralFftSize >= pulp::signal::kSpectralFrameEngineMinimumFftSize
              && kSpectralFftSize
                     <= pulp::signal::kSpectralFrameEngineMaximumFftSize
              && (kSpectralFftSize & (kSpectralFftSize - 1)) == 0
              && kSpectralAnalysisHop > 0
              && kSpectralAnalysisHop <= kSpectralFftSize / 2,
              "Spectr build selected unsupported Pulp spectral geometry");
static_assert(kAnalyzerFftSize / 2 + 1
                  <= pulp::view::SpectrumData::kMaxBins,
              "Spectr analyzer geometry exceeds Pulp spectrum capacity");

enum ParamIDs : pulp::state::ParamID {
    kMix          = 1,
    kOutputTrim   = 2,   ///< dB, [-24, +24]
};

inline pulp::format::PluginDescriptor make_descriptor() {
    return {
#if defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
        .name         = "Spectr Native Preview",
#else
        .name         = "Spectr",
#endif
        .manufacturer = "Pulp",
        // The CLAP adapter uses this as the plugin ID, which is the identity a
        // host persists in session state. It must differ from production or an
        // installed preview collides with it: a session saved against one can
        // resolve to the other. REAPER hides this by keying its cache on
        // filename, so the collision is invisible until a host keys by ID.
#if defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
        .bundle_id    = "com.pulp.spectr.native-preview",
#else
        .bundle_id    = "com.pulp.spectr",
#endif
        .version      = "1.0.0",
        .category     = pulp::format::PluginCategory::Effect,
    };
}

/// Top-level Spectr plugin. Owns the product state and Pulp's reusable
/// streaming spectral-mask processor.
class Spectr : public pulp::format::Processor
#if defined(SPECTR_NATIVE_EDITOR)
             , private pulp::view::CommandHandler
#endif
{
public:
    Spectr();
    ~Spectr() override;

    pulp::format::PluginDescriptor descriptor() const override;
    void define_parameters(pulp::state::StateStore& store) override;
    void prepare(const pulp::format::PrepareContext& ctx) override;
    void release() override;
    int latency_samples() const override;
    pulp::format::ViewSize view_size() const override;

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        pulp::midi::MidiBuffer& midi_in,
        pulp::midi::MidiBuffer& midi_out,
        const pulp::format::ProcessContext& ctx) override;

    // ── Supplemental plugin state (pulp#625 / PR#628 hooks) ─────────────
    //
    // Under V2 handoff §5.4, Spectr's richer state (canonical band field,
    // viewport bounds, analyzer/edit mode) rides through the host adapters
    // as an opaque versioned JSON payload alongside StateStore's flat
    // parameter blob. See planning/Spectr-V2-Pulp-Handoff.md §5.4.
    std::vector<uint8_t> serialize_plugin_state() const override;
    bool deserialize_plugin_state(std::span<const uint8_t> bytes) override;

    /// Supplemental-state schema version. Bump when the JSON shape changes
    /// in a non-backward-compatible way; deserialize rejects unknown
    /// versions.
    ///
    /// v2 (M8) extends v1 with an optional `snapshots` object holding the
    /// A/B snapshot bank. Absent `snapshots` is legal — reading a v1 blob
    /// is always a reset-to-default for the bank.
    static constexpr int kPluginStateVersion = 3;

    // ── Editor view ────────────────────────────────────────────────────
    std::unique_ptr<pulp::view::View> create_view() override;
    void on_view_opened(pulp::view::View& view) override;
    void on_view_resized(pulp::view::View& view, uint32_t w, uint32_t h) override;
    void on_view_closed(pulp::view::View& view) override;
#if defined(SPECTR_NATIVE_EDITOR)
    pulp::view::ScriptedUiSession* active_scripted_ui() override {
        return native_scripted_ui_.get();
    }
    const pulp::view::ScriptedUiSession* active_scripted_ui() const override {
        return native_scripted_ui_.get();
    }
    EditorRevision native_editor_revision() const noexcept {
        return editor_authority_.revision();
    }
#endif
    // ── Accessors — primarily for tests and the UI layer ───────────────

    const BandField&  field()     const noexcept { return field_; }
    BandField&        field()           noexcept { return field_; }
    /// Coherent copy for runtime readers. The reference accessors above are
    /// retained for construction-time setup and legacy tests; code that can
    /// overlap host automation must use this snapshot.
    ProcessingStateSnapshot processing_state_snapshot() const noexcept;
    void replace_field(const BandField& field) noexcept;
    bool replace_processing_state(const BandField& field,
                                  const Viewport& viewport,
                                  Layout layout) noexcept;
    void publish_field() noexcept;
    /// Publish one of the four editor mode controls to the host parameter
    /// surface. This is the editor-to-host lane only; host-to-editor mode
    /// observation remains owned by spectr#37.
    [[nodiscard]] bool set_editor_mode_param(pulp::state::ParamID id,
                                             float value) noexcept;
    const Viewport&   viewport()  const noexcept { return viewport_; }
    Viewport&         viewport()        noexcept { return viewport_; }
    Layout            layout()    const noexcept { return layout_; }
    EditorAuthority& editor_authority() noexcept { return editor_authority_; }
    const EditorAuthority& editor_authority() const noexcept { return editor_authority_; }

    void set_layout(Layout L);

    /// Analyze how many of the current layout's authored bands own at least
    /// one distinct bin at the compiled FFT geometry and current sample rate.
    /// Control-thread only; unavailable before prepare, and `out_resolution`
    /// is unchanged on failure.
    [[nodiscard]] bool spectral_resolution(
        pulp::signal::SpectralBandResolution& out_resolution) const noexcept;

    // ── Snapshot A/B + morph (Milestone 8) ──────────────────────────────
    //
    // Spectr tracks two kinds of A/B state:
    //
    //   - Flat StateStore params (Mix and Output): handled by
    //     pulp::view::ABCompare over the StateStore. Access via
    //     ab_compare().
    //   - Band-field + viewport + layout: held in snapshots_ below, with
    //     editor-local per-band morph via morph_fields(). Serialized in the
    //     plugin state blob so it survives session reload without exposing a
    //     misleading host-automation parameter.
    //
    // UI drives both in lockstep for the full A/B experience.

    const SnapshotBank& snapshots() const noexcept { return snapshots_; }
    SnapshotBank&       snapshots()       noexcept { return snapshots_; }

    /// Copy the current field + viewport + layout into the named slot.
    /// Marks the slot populated.
    void capture_snapshot(SnapshotBank::Slot slot) noexcept;

    /// Write the morph of A and B at t into `field_`. If either slot is
    /// unpopulated, falls back to the populated side (or leaves field_
    /// alone if neither slot has been captured). Does NOT touch viewport
    /// or layout — those aren't continuously morphed.
    void apply_morph_to_live(float t) noexcept;

    /// Accessor for the StateStore-level ABCompare. Lazily constructed
    /// the first time it's requested (after define_parameters has wired
    /// the store). Returns nullptr if the store isn't available yet.
    pulp::view::ABCompare* ab_compare() noexcept;

    // ── Host parameter surface sync (spectr#34) ──────────────────────────
    //
    // The StateStore owns the host-visible parameter values; field_/
    // viewport_/layout_ remain the DSP-facing canonical state. Two
    // control-thread directions keep them consistent:
    //
    //   apply_surface_params() — params → canonical state. Diffs the store
    //   against the applied-value cache; band gains/mutes, viewport, and
    //   band count apply directly, the morph parameter (when apply_morph)
    //   re-derives the field from the snapshot bank, and mode params advance
    //   the editor projection without republishing the DSP mask. Runs on
    //   the sync worker (spawned from process() on drift) and synchronously
    //   from prepare().
    //
    //   sync_params_from_field() — canonical state → params. Pushes only
    //   slots that changed since the last sync (delta against synced_*), so
    //   a paint gesture writes the swept bands and nothing else. UI edits
    //   thereby become host-visible value changes the adapter can record.
    //
    // Both directions are one-way per call site, so there is no echo loop:
    // the drain never pushes, and the push path marks the applied cache so
    // the next process()-side sweep sees no drift.
    bool apply_surface_params(bool apply_morph) noexcept;

    [[nodiscard]] EditorRevision host_automation_revision() const noexcept {
        return host_automation_revision_.load(std::memory_order_acquire);
    }
    [[nodiscard]] float editor_mode_param(
        pulp::state::ParamID id) const noexcept;
    [[nodiscard]] ModulationSettings modulation_settings() const noexcept;
    bool set_modulation_target_mask(std::uint8_t mask) noexcept;
    void sync_params_from_field(bool emit_gestures = true) noexcept;

    /// Paint-drag gesture epochs (EditorAuthority drives these from
    /// begin/end/cancel_band_edit on the UI thread). While an epoch is open,
    /// the first sync push of each parameter also opens a host gesture, and
    /// ending the epoch closes them all — one begin/end bracket per touched
    /// parameter per drag, at gesture rate, instead of per-event brackets.
    void begin_param_gesture_epoch() noexcept;
    void end_param_gesture_epoch() noexcept;

    // ── Pattern library ────────────────────────────────────────────────
    //
    // Each Spectr owns a PatternLibrary pre-populated with the factory
    // presets. User patterns come and go through the editor bridge's
    // load_pattern / (future) save_current paths. Persistence for
    // user patterns is a M9 follow-up — the library lives in memory
    // only for now; serialize_plugin_state doesn't yet pack it into
    // the supplemental blob.
    PatternLibrary&       patterns()       noexcept { return patterns_; }
    const PatternLibrary& patterns() const noexcept { return patterns_; }

    // ── Analyzer bridge — UI-thread read path ───────────────────────────
    //
    // Spectr publishes STFT + meter + waveform snapshots from the audio
    // thread through VisualizationBridge's TripleBuffers. UI/tests read
    // via these accessors; the reads are lock-free and always see the
    // latest complete frame.
    pulp::view::VisualizationBridge& bridge() noexcept { return bridge_; }
    const pulp::view::SpectrumData& read_spectrum() { return bridge_.read_spectrum(); }
    const pulp::view::WaveformData& read_waveform() { return bridge_.read_waveform(); }
    const pulp::signal::MultiChannelMeterData& read_meter() { return bridge_.read_meter(); }

    /// Latest post-LFO band field from the audio owner, for drawing only.
    /// Lock-free; always a complete frame. `active == false` means no
    /// modulator is running and the editor should draw canonical state.
    const ModulatedFieldSnapshot& read_modulated_field() {
        return modulated_field_publication_.read();
    }

private:
    double sample_rate_ = 48000.0;
    int    max_block_   = 512;
    int    channels_    = 1;

    static constexpr std::size_t kMaximumChannels = 64;

    BandField                              field_{};
    Viewport                               viewport_{};
    Layout                                 layout_ = Layout::Bands32;
    pulp::signal::SpectralMaskProcessor    mask_processor_{};
    pulp::signal::SmoothedValue<float>     output_gain_{1.0f};
    bool                                   processor_prepared_ = false;
    std::array<const float*, kMaximumChannels> input_channels_{};
    std::array<float*, kMaximumChannels>       output_channels_{};

    pulp::view::VisualizationBridge       bridge_{};
    SnapshotBank                          snapshots_{};
    PatternLibrary                        patterns_{};
    std::unique_ptr<pulp::view::ABCompare> ab_{};
    EditorAuthority                       editor_authority_;

    // ── spectr#34 parameter-surface sync state ───────────────────────────
    // Non-owning store handle so const paths (serialize) can read param
    // values and control paths can push without going through the const
    // state() accessor. Set in define_parameters.
    pulp::state::StateStore* param_store_ = nullptr;
    // Per-slot mirror of the store value at the last reconciliation, read on
    // the audio thread by the process() drift sweep and written by both sync
    // directions. Slot layout: 0..63 gains, 64..127 mutes, 128 morph,
    // 129 viewport center, 130 viewport width, 131 band count, then motion,
    // analyzer, edit, and visualization at 132..135, then internal LFO
    // enabled/shape/rate/depth/target at 136..140.
    static constexpr std::size_t kSurfaceCacheSlots = 145;
    static_assert(kSurfaceCacheSlots == detail::kSurfaceSlots);
    std::array<std::atomic<float>, kSurfaceCacheSlots> applied_param_cache_{};
    // Audio-owner baseline used when an adapter supplies an event queue after
    // already committing its end-of-block values to StateStore. ParamCursor
    // must begin from the values audible at the end of the previous block,
    // otherwise the pre-event slice would incorrectly jump to the final value.
    float audio_mix_percent_ = 100.0f;
    float audio_output_trim_db_ = 0.0f;
    // Settings/snapshots cross from serialized control writers to the audio
    // owner through a latest-value publication. Derived LFO values never flow
    // back into either canonical state or host parameter lanes.
    pulp::runtime::TripleBuffer<AudioModulationState>
        audio_modulation_publication_{};
    double audio_modulation_phase_ = 0.0;
    double audio_modulation_phase_2_ = 0.0;
    // Audio owner -> UI publication of the post-LFO band field, so the editor
    // can draw the modulation it is playing. Write-only on the audio thread,
    // read-only through read_modulated_field().
    pulp::runtime::TripleBuffer<ModulatedFieldSnapshot>
        modulated_field_publication_{};
    std::uint64_t modulated_field_sequence_ = 0;
    bool          modulated_field_was_active_ = false;
    // The most recent EditorAuthority revision caused specifically by host
    // parameter adoption. Views use this as a coalescing publication key so
    // automation redraws immediately without echoing every editor-originated
    // paint receipt back through a full hydration.
    std::atomic<EditorRevision> host_automation_revision_{0};
    // The canonical state as last pushed to the parameters — the sync delta
    // base. Morph updates it silently (morph moves the morph parameter only;
    // pushing 64 resulting band lanes per morph move would double-drive the
    // field on automation playback).
    BandField synced_field_{};
    Viewport  synced_viewport_{};
    Layout    synced_layout_ = Layout::Bands32;
    // Serializes every mutation (and the matching mask publish) of field_/
    // viewport_/layout_ across the control threads that now write them:
    // UI (EditorAuthority), the sync worker, and host state restore. The
    // audio thread never takes it — process() reads params and the applied
    // cache only. Param pushes happen OUTSIDE it: set_value fires listeners
    // that may read processor state back, and holding the lock there would
    // self-deadlock. Mutable so const readers (spectral_resolution) can
    // take a coherent snapshot.
    mutable std::mutex processing_state_mutex_;
    // Audio→worker lane: process() spawns on parameter drift, the worker
    // applies params → canonical state and republishes the mask (table
    // compilation is a control-thread operation).
    struct ParamSyncTask { std::uint64_t tag = 0; };
    pulp::format::BackgroundTaskLane<ParamSyncTask, 8> param_sync_lane_;
    ModulationSettings modulation_{};
    // Open paint-drag epoch (UI thread only): params already begin-gestured.
    std::vector<pulp::state::ParamID> epoch_gesture_params_{};
    bool param_gesture_epoch_open_ = false;

    // A morph derives non-overridden bands from the snapshot bank while the
    // sparse override mask identifies later band edits whose values live in
    // StateStore. This makes the v3 supplemental blob non-duplicating: it
    // stores only the derivation shape, never a second copy of host params.
    bool morph_derived_ = false;
    std::bitset<kMaxBands> morph_overrides_{};

    bool surface_params_drifted_() const noexcept;
    /// The modulation settings described by the current host parameter lanes.
    /// `target_mask` is left at the unset sentinel: the caller owns whatever
    /// explicit destination selection should ride along.
    ModulationSettings modulation_from_store_() const noexcept;
    void push_surface_param_(pulp::state::ParamID id, std::size_t slot,
                             float value, bool emit_gesture = true);
    static void param_sync_trampoline_(void* ctx, const ParamSyncTask&) noexcept;

#if defined(SPECTR_NATIVE_EDITOR)
    // Matches Pulp's framework-reserved `PLST` command in pending pulp#7712.
    // Keeping the consumer on the existing registry surface lets the current
    // SDK prove Spectr's handler before the host-side Cmd/Ctrl+, fallback lands.
    static constexpr pulp::view::CommandID kOpenSettingsCommand = 0x504C5354;
    std::vector<pulp::view::CommandID> commands() const override;
    bool perform_command(pulp::view::CommandID id) override;

    pulp::view::CommandRegistry native_command_registry_{};
    pulp::view::EditorBridge native_editor_bridge_{};
    bool native_editor_handlers_registered_ = false;
    std::unique_ptr<pulp::view::ScriptedUiSession> native_scripted_ui_{};
    std::filesystem::path native_package_path_{};
    pulp::view::View* native_editor_root_ = nullptr;
    // Latches once the SPECTR_SETTINGS_SCROLL fixture has positioned the
    // settings body, so the capture is not re-scrolled every frame.
    bool settings_fixture_scrolled_ = false;
    bool settings_fixture_dumped_ = false;
    bool settings_fixture_key_sent_ = false;
    bool live_capture_done_ = false;
    // Pointer fixtures. A drag is the only way to reach the states the status
    // overlay is judged in, and a screenshot cannot be taken mid-gesture, so
    // the fixture probes the banner's live text between the press and the
    // release rather than inferring "during" from a picture taken after it.
    bool drag_fixture_done_ = false;
    double drag_fixture_finished_ms_ = -1.0;
    std::size_t status_probe_next_ = 0;
    bool cursor_probe_done_ = false;
    // Stepped-drag fixture. A "fast drag" row is a claim about the samples
    // BETWEEN press and release, so this probe reads processing state after
    // every delivered move rather than only before and after the gesture: a
    // before/after pair cannot tell a drag that tracked the pointer from one
    // that jumped straight to its end point.
    bool gesture_probe_done_ = false;
    bool resize_fixture_applied_ = false;
    bool resize_request_sent_ = false;
    // Per-tick state trace for externally driven gestures. The AppKit drag
    // fixture runs over hundreds of frames and only its END state is visible
    // in a dump, but "an edge drag never moves the opposite trim" is a claim
    // about every frame in between -- an excursion that returns before the
    // last frame is exactly the defect and is invisible to a before/after
    // pair. Accumulated in memory and rewritten whole each tick so the file is
    // complete even if the process is killed rather than closed.
    std::string native_state_trace_{};
    int native_state_trace_tick_ = 0;
    pulp::view::View* native_resize_grip_ = nullptr;
    // Last host size reported to on_view_resized. Under a pinned viewport the
    // ROOT is constant at the authored box, so root bounds are useless as a
    // resize base — every drag would measure from 1320x860 and the grip could
    // only ever take one step. The host size is the thing that actually moves.
    std::uint32_t native_host_width_ = 0;
    std::uint32_t native_host_height_ = 0;
    // Editor size latched at grip mouse-down. The grip resolves a size from a
    // cumulative delta against the drag start, so the base must be sampled once
    // per drag rather than read live (reading live would compound).
    std::uint32_t native_resize_base_width_ = 0;
    std::uint32_t native_resize_base_height_ = 0;
    // Set when the host refuses a request mid-drag, so one refusal doesn't turn
    // into a rejected transaction per mouse-move for the rest of the gesture.
    bool native_resize_refused_ = false;
    // Last (w, h) handed to publish_native_layout_. Under a pinned viewport
    // every resize publishes the same authored box, so without this the whole
    // materialized restore + re-place pass re-runs per pointer event for a
    // result that cannot differ. 0 means "nothing published yet", which is the
    // state a freshly created editor must be in so the first pass still runs.
    std::uint32_t native_published_width_ = 0;
    std::uint32_t native_published_height_ = 0;
    pulp::view::FrameClock* native_frame_clock_ = nullptr;
    int native_frame_subscription_ = -1;
    float native_analyzer_elapsed_ = 0.0f;
    std::uint64_t native_analyzer_sequence_ = 0;
    // Last modulated-field sequence projected to the editor, so a UI tick
    // that finds no new audio frame does not re-dispatch the same overlay.
    std::uint64_t native_modulation_sequence_ = 0;
    // Scratch for the display-time LFO reconstruction. A member rather than a
    // local so a BandField is not built on the stack every frame.
    BandField     native_modulation_drawn_{};
    // Phases last painted, and how many consecutive ticks have seen no new
    // publication. Together they recognise the two no-ops: nothing new to
    // draw, and a producer that has gone quiet.
    double        native_modulation_drawn_phase_ = -1.0;
    double        native_modulation_drawn_phase_2_ = -1.0;
    int           native_modulation_stale_ticks_ = 0;
    EditorRevision native_host_automation_revision_ = 0;

    std::unique_ptr<pulp::view::View> create_native_editor_();
    void publish_native_layout_(std::uint32_t w, std::uint32_t h);
    void open_native_editor_(pulp::view::View& view);
    void close_native_editor_();
    bool tick_native_analyzer_(float dt);
    /// Draw what the modulators are playing: reconstruct the post-LFO field at
    /// this frame's time from the audio owner's published inputs and hand it
    /// to the editor. Display only -- it never re-enters canonical state.
    void publish_modulation_frame_();
    // Fixture-only. Writes the laid-out tree plus its depth sidecar under
    // SPECTR_DRAG_DUMP_PREFIX for one named stage of a gesture, so "during"
    // and "after" are two artifacts rather than one interpretation.
    void dump_fixture_stage_(const std::string& stage);
    // Pump the scripted runtime so a React state change queued by the event
    // just delivered has actually landed before the next stage is written.
    void settle_native_runtime_(int frames);
    static double fixture_now_ms_();
#endif

    [[nodiscard]] pulp::signal::SpectralBandLayout
        make_mask_layout_() const noexcept;
    void publish_audio_modulation_state_() noexcept;
    void publish_processing_state_() noexcept;
    void configure_bridge_(int num_channels);
};

inline std::unique_ptr<pulp::format::Processor> create_spectr() {
    return std::make_unique<Spectr>();
}

} // namespace spectr
