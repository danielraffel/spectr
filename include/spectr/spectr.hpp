#pragma once

// Spectr — zoomable frequency-slicer audio effect.
//
// See README.md for a product summary and planning/ for the full design
// package. Milestone 1 (Foundation) layered the project; real DSP arrives in
// Milestone 2 (DSP truth spike). State registration (#625 gated) is
// Milestone 4.

#include <pulp/format/processor.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>
#include <pulp/signal/smoothed_value.hpp>
#include <pulp/view/ab_compare.hpp>
#include <pulp/view/visualization_bridge.hpp>
#include <array>
#include <memory>

#if defined(SPECTR_NATIVE_EDITOR)
#include <filesystem>
#include <pulp/view/editor_bridge.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/scripted_ui.hpp>
#include "spectr/editor_bridge.hpp"
#endif

#include "spectr/band_state.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/editor_authority.hpp"
#include "spectr/pattern.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/viewport.hpp"
#include "spectr/editor_resize.hpp"

#ifndef SPECTR_FFT_SIZE
#define SPECTR_FFT_SIZE 8192
#endif
#ifndef SPECTR_ANALYSIS_HOP
#define SPECTR_ANALYSIS_HOP 2048
#endif

namespace spectr {

/// Declare that this process's host already draws a native window-resize
/// affordance, so the editor must NOT add its own.
///
/// AU v2 has no host->plugin resize contract and Logic's plugin window has no
/// grow area, so a hosted editor owns its resize gesture (see the grip in
/// `create_native_editor_`). A standalone window is the opposite case: macOS
/// owns the bottom-right corner of a resizable NSWindow and consumes press and
/// click there before the content view is asked — measured, with the grip
/// present, painted, and receiving nothing while the window resized. A grip
/// there is a painted control that can never fire.
///
/// This is declared by the process entry point rather than sniffed.
/// `pulp::format::detect_host_type()` cannot answer it: it reports
/// `HostType::Standalone` by matching "pulp" in the process name, and this
/// product's standalone is "Spectr Native Preview".
void set_host_draws_native_resize(bool value);
bool host_draws_native_resize();

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
class Spectr : public pulp::format::Processor {
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
    static constexpr int kPluginStateVersion = 2;

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
    void replace_field(const BandField& field) noexcept;
    bool replace_processing_state(const BandField& field,
                                  const Viewport& viewport,
                                  Layout layout) noexcept;
    void publish_field() noexcept;
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

#if defined(SPECTR_NATIVE_EDITOR)
    pulp::view::EditorBridge native_editor_bridge_{};
    bool native_editor_handlers_registered_ = false;
    std::unique_ptr<pulp::view::ScriptedUiSession> native_scripted_ui_{};
    std::filesystem::path native_package_path_{};
    pulp::view::View* native_editor_root_ = nullptr;
    pulp::view::View* native_resize_grip_ = nullptr;
    // Editor size latched at grip mouse-down. `ResizableCorner` reports
    // cumulative deltas from the drag start, so the base must be sampled once
    // per drag rather than read live (reading live would compound).
    std::uint32_t native_resize_base_width_ = 0;
    std::uint32_t native_resize_base_height_ = 0;
    // Set when the host refuses a request mid-drag, so one refusal doesn't turn
    // into a rejected transaction per mouse-move for the rest of the gesture.
    bool native_resize_refused_ = false;
    pulp::view::FrameClock* native_frame_clock_ = nullptr;
    int native_frame_subscription_ = -1;
    float native_analyzer_elapsed_ = 0.0f;
    std::uint64_t native_analyzer_sequence_ = 0;

    std::unique_ptr<pulp::view::View> create_native_editor_();
    void open_native_editor_(pulp::view::View& view);
    void close_native_editor_();
    bool tick_native_analyzer_(float dt);
#endif

    [[nodiscard]] pulp::signal::SpectralBandLayout
        make_mask_layout_() const noexcept;
    void publish_processing_state_() noexcept;
    void configure_bridge_(int num_channels);
};

inline std::unique_ptr<pulp::format::Processor> create_spectr() {
    return std::make_unique<Spectr>();
}

} // namespace spectr
