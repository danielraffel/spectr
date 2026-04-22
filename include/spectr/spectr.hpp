#pragma once

// Spectr — zoomable frequency-slicer audio effect.
//
// See README.md for a product summary and planning/ for the full design
// package. Milestone 1 (Foundation) layered the project; real DSP arrives in
// Milestone 2 (DSP truth spike). State registration (#625 gated) is
// Milestone 4.

#include <pulp/format/processor.hpp>
#include <pulp/view/visualization_bridge.hpp>
#include <memory>

#include "spectr/band_state.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/engine.hpp"
#include "spectr/viewport.hpp"

namespace spectr {

enum ParamIDs : pulp::state::ParamID {
    kMix          = 1,
    kOutputTrim   = 2,   ///< dB, [-24, +24]
    kResponseMode = 3,   ///< 0=Live, 1=Precision
    kEngineMode   = 4,   ///< 0=IIR, 1=FFT, 2=Hybrid
    kBandCount    = 5,   ///< 0=32, 1=40, 2=48, 3=56, 4=64
};

inline pulp::format::PluginDescriptor make_descriptor() {
    return {
        .name         = "Spectr",
        .manufacturer = "Pulp",
        .bundle_id    = "com.pulp.spectr",
        .version      = "1.0.0",
        .category     = pulp::format::PluginCategory::Effect,
    };
}

/// Top-level Spectr plugin. Owns the BandField, Viewport, and the active
/// SpectralEngine. Milestones 2+ fill in the engine impls and state
/// registration.
class Spectr : public pulp::format::Processor {
public:
    Spectr();
    ~Spectr() override;

    pulp::format::PluginDescriptor descriptor() const override;
    void define_parameters(pulp::state::StateStore& store) override;
    void prepare(const pulp::format::PrepareContext& ctx) override;
    void release() override;

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
    static constexpr int kPluginStateVersion = 1;

    // ── Editor view ────────────────────────────────────────────────────
    std::unique_ptr<pulp::view::View> create_view() override;
    void on_view_opened(pulp::view::View& view) override;
    pulp::format::ViewSize view_size() const override {
        // Matches the prototype's natural canvas size from screenshots.
        return {/*pref_w*/1320, /*pref_h*/860,
                /*min_w*/800,  /*min_h*/480,
                /*max_w*/0,    /*max_h*/0,
                /*aspect*/0.0};
    }

    // ── Accessors — primarily for tests and the UI layer ───────────────

    const BandField&  field()     const noexcept { return field_; }
    BandField&        field()           noexcept { return field_; }
    const Viewport&   viewport()  const noexcept { return viewport_; }
    Viewport&         viewport()        noexcept { return viewport_; }
    Layout            layout()    const noexcept { return layout_; }
    ResponseMode      response()  const noexcept { return response_mode_; }
    EngineKind        engine_kind() const noexcept { return engine_kind_; }

    void set_layout(Layout L);
    void set_response_mode(ResponseMode m) noexcept { response_mode_ = m; }
    void set_engine_kind(EngineKind k);

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

    BandField                        field_{};
    Viewport                         viewport_{};
    Layout                           layout_       = Layout::Bands32;
    ResponseMode                     response_mode_= ResponseMode::Precision;
    EngineKind                       engine_kind_  = EngineKind::Fft;
    std::unique_ptr<SpectralEngine>  engine_{};

    pulp::view::VisualizationBridge  bridge_{};

    void rebuild_engine_();
    void configure_bridge_(int num_channels);
};

inline std::unique_ptr<pulp::format::Processor> create_spectr() {
    return std::make_unique<Spectr>();
}

} // namespace spectr
