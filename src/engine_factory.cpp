#include "spectr/engine.hpp"

#include <algorithm>
#include <cstring>

namespace spectr {

namespace {

/// Milestone 1 stub engine: passes input → output unchanged regardless of
/// BandField / Viewport / Layout / ResponseMode. Milestone 2 replaces the IIR
/// and FFT variants with real impls; the Hybrid variant arrives after.
class PassThroughEngine final : public SpectralEngine {
public:
    explicit PassThroughEngine(EngineKind kind) : kind_(kind) {}

    void prepare(const EnginePrepare& p) override {
        sample_rate_ = p.sample_rate;
        max_block_   = p.max_block;
        layout_      = p.layout;
        viewport_    = p.viewport;
    }

    void release() override {}

    int latency_samples() const override { return 0; }

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        const BandField& /*field*/,
        const Viewport& /*view*/,
        Layout /*layout*/,
        ResponseMode /*mode*/) override
    {
        const auto chans = std::min(output.num_channels(), input.num_channels());
        const auto n = std::min(output.num_samples(), input.num_samples());
        for (std::size_t ch = 0; ch < chans; ++ch) {
            auto dst = output.channel(ch);
            auto src = input.channel(ch);
            for (std::size_t i = 0; i < n; ++i) dst[i] = src[i];
        }
        // Zero any extra output channels.
        for (std::size_t ch = chans; ch < output.num_channels(); ++ch) {
            auto dst = output.channel(ch);
            for (auto& s : dst) s = 0.0f;
        }
    }

private:
    EngineKind kind_;
    double     sample_rate_ = 48000.0;
    int        max_block_   = 512;
    Layout     layout_      = Layout::Bands32;
    Viewport   viewport_{};
};

} // namespace

std::unique_ptr<SpectralEngine> make_engine(EngineKind kind) {
    // Milestone 1: all three kinds return the pass-through stub. Milestone 2
    // replaces Iir and Fft with real engines; Hybrid arrives after.
    return std::make_unique<PassThroughEngine>(kind);
}

} // namespace spectr
