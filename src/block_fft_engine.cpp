// Milestone 2 — Block-FFT engine (DSP truth spike).
//
// This is the first real spectral engine. It runs one FFT per process block
// (fft_size == block_size, rounded up to next power of two), applies the
// canonical band mask to each frequency bin, and returns to the time
// domain. No windowing, no overlap-add.
//
// Why this shape for the spike:
//   - Flat mask produces exact passthrough (the real→complex→inverse round
//     trip is lossless because FFT is orthogonal), so flat-state
//     transparency is unambiguous.
//   - Mute depth is unambiguous: zeroing a bin's mask truly removes that
//     bin from the reconstruction.
//   - Bin-to-band mapping via Viewport::band_for_hz is deterministic and
//     easy to test.
//
// Why this is NOT the final production engine:
//   - No windowing → circular-convolution artifacts at block boundaries
//     for non-flat masks. Acceptable for the spike's offline audition
//     tests; not acceptable for a DAW.
//   - No overlap-add. Same reason.
//   - Block size drives frequency resolution. Short DAW blocks (32, 64,
//     128) give coarse bin spacing. Production engines will use a fixed
//     STFT frame size (1024–2048) with overlap-add regardless of the host
//     block size.
//
// M11 polish turns this into a proper windowed STFT engine. For M2 the
// job is to prove Spectr's product truths, not to ship this code as-is.

#include "spectr/engine.hpp"

#include <pulp/signal/fft.hpp>

#include <algorithm>
#include <complex>
#include <cstring>
#include <vector>

namespace spectr {

namespace {

// Round up to next power of two, clamped to the Fft::size range (256..8192
// per the SDK — we extend down to 128 with the power-of-two guarantee for
// very small host blocks).
int round_up_pow2(int n, int floor_size = 128, int ceil_size = 8192) noexcept {
    int p = floor_size;
    while (p < n) p <<= 1;
    if (p > ceil_size) p = ceil_size;
    return p;
}

class BlockFftEngine final : public SpectralEngine {
public:
    explicit BlockFftEngine(EngineKind kind) : kind_(kind) {}

    void prepare(const EnginePrepare& p) override {
        sample_rate_ = p.sample_rate;
        max_block_   = p.max_block;

        // Pick an FFT size from the max block size. For the spike we want
        // fft_size >= block so each block fits in one transform.
        const int n = round_up_pow2(std::max(p.max_block, 128));
        if (n != fft_size_) {
            fft_       = pulp::signal::Fft(n);
            fft_size_  = n;
            num_bins_  = fft_size_ / 2 + 1;
            scratch_.assign(static_cast<std::size_t>(fft_size_), {0.0f, 0.0f});
            real_in_.assign(static_cast<std::size_t>(fft_size_), 0.0f);
        }
    }

    void release() override {}

    int latency_samples() const override {
        // This engine is block-synchronous: no look-ahead, no overlap.
        // Flat-mask passthrough is exact within the block, so we report 0.
        return 0;
    }

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        const BandField& field,
        const Viewport& view,
        Layout layout,
        ResponseMode /*mode*/) override
    {
        const auto chans   = std::min(output.num_channels(), input.num_channels());
        const auto n_in    = input.num_samples();
        const auto n_out   = std::min(output.num_samples(), n_in);
        const auto n_vis   = visible_count(layout);

        // Build the per-bin gain table once per block.
        // bin k corresponds to frequency k * sample_rate / fft_size.
        // Frequency → canonical band index via the Viewport+Layout.
        // Unused canonical slots above n_vis stay at 1.0 (we clamp to
        // in-range slots).
        bin_gain_.assign(static_cast<std::size_t>(num_bins_), 1.0f);
        const float freq_step = static_cast<float>(sample_rate_) / static_cast<float>(fft_size_);
        for (int k = 0; k < num_bins_; ++k) {
            const float hz = static_cast<float>(k) * freq_step;
            if (hz < view.min_hz || hz > view.max_hz) {
                // Outside the viewport: edge bands handle low-cut/high-cut.
                // Use the nearest visible band's gain.
                const std::size_t idx = (hz <= view.min_hz) ? 0 : n_vis - 1;
                bin_gain_[static_cast<std::size_t>(k)] = field.linear_gain(idx);
                continue;
            }
            const auto idx = view.band_for_hz(hz, n_vis);
            bin_gain_[static_cast<std::size_t>(k)] = field.linear_gain(idx);
        }

        for (std::size_t ch = 0; ch < chans; ++ch) {
            // Copy input into the real padding buffer; zero-pad any remainder.
            auto src = input.channel(ch);
            for (std::size_t i = 0; i < n_in && static_cast<int>(i) < fft_size_; ++i) {
                real_in_[i] = src[i];
            }
            for (std::size_t i = n_in; static_cast<int>(i) < fft_size_; ++i) {
                real_in_[i] = 0.0f;
            }

            // Forward transform.
            fft_.forward_real(real_in_.data(), scratch_.data());

            // Apply per-bin gain to the first half (DC .. Nyquist).
            for (int k = 0; k < num_bins_; ++k) {
                scratch_[static_cast<std::size_t>(k)] *= bin_gain_[static_cast<std::size_t>(k)];
            }
            // Enforce conjugate symmetry for k in (Nyquist .. fft_size-1):
            // scratch[fft_size - k] == conj(scratch[k]).
            for (int k = 1; k < num_bins_ - 1; ++k) {
                scratch_[static_cast<std::size_t>(fft_size_ - k)] = std::conj(scratch_[static_cast<std::size_t>(k)]);
            }
            // DC (k=0) and Nyquist (k=num_bins_-1) are already real; leave.

            // Inverse transform — in-place; real part is our output signal.
            fft_.inverse(scratch_.data());

            auto dst = output.channel(ch);
            for (std::size_t i = 0; i < n_out; ++i) {
                dst[i] = scratch_[i].real();
            }
        }

        // Zero extra output channels.
        for (std::size_t ch = chans; ch < output.num_channels(); ++ch) {
            auto dst = output.channel(ch);
            for (auto& s : dst) s = 0.0f;
        }
    }

private:
    EngineKind         kind_;
    double             sample_rate_ = 48000.0;
    int                max_block_   = 512;
    int                fft_size_    = 0;
    int                num_bins_    = 0;
    pulp::signal::Fft  fft_;
    std::vector<std::complex<float>> scratch_;
    std::vector<float> real_in_;
    std::vector<float> bin_gain_;
};

/// Milestone 1 stub used by engine kinds we haven't implemented yet.
class PassThroughEngine final : public SpectralEngine {
public:
    explicit PassThroughEngine(EngineKind kind) : kind_(kind) {}
    void prepare(const EnginePrepare&) override {}
    void release() override {}
    int  latency_samples() const override { return 0; }
    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        const BandField&, const Viewport&, Layout, ResponseMode) override
    {
        const auto chans = std::min(output.num_channels(), input.num_channels());
        const auto n = std::min(output.num_samples(), input.num_samples());
        for (std::size_t ch = 0; ch < chans; ++ch) {
            auto dst = output.channel(ch);
            auto src = input.channel(ch);
            for (std::size_t i = 0; i < n; ++i) dst[i] = src[i];
        }
        for (std::size_t ch = chans; ch < output.num_channels(); ++ch) {
            auto dst = output.channel(ch);
            for (auto& s : dst) s = 0.0f;
        }
    }
private:
    EngineKind kind_;
};

} // namespace

std::unique_ptr<SpectralEngine> make_engine(EngineKind kind) {
    switch (kind) {
        case EngineKind::Fft:
            return std::make_unique<BlockFftEngine>(kind);
        case EngineKind::Iir:
        case EngineKind::Hybrid:
        default:
            return std::make_unique<PassThroughEngine>(kind);
    }
}

} // namespace spectr
