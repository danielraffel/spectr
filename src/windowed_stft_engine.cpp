#include "spectr/windowed_stft_engine.hpp"

#include <pulp/signal/fft.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace spectr {

namespace {

constexpr int   kFftSize   = 1024;
constexpr int   kHopSize   = kFftSize / 4;          // 75% overlap
constexpr int   kNumBins   = kFftSize / 2 + 1;
constexpr float kTwoPi     = 6.2831853071795864769f;

// Per-sample state for one audio channel. Two ring buffers:
//   - input_ring: rolling window of the last `kFftSize` input samples,
//     fed by process() and read by the analysis frame computation.
//   - output_ring: accumulator for overlap-added synthesis frames,
//     consumed one sample at a time as output.
struct ChannelState {
    std::vector<float> input_ring;
    std::vector<float> output_ring;
    int input_pos       = 0;  ///< next write position in input_ring (also = oldest sample)
    int output_pos      = 0;  ///< next read position in output_ring
    int samples_to_hop  = 0;  ///< countdown to next analysis frame
    int samples_filled  = 0;  ///< total input samples seen (before first real frame)
};

class WindowedStftEngine final : public SpectralEngine {
public:
    void prepare(const EnginePrepare& p) override {
        sample_rate_ = p.sample_rate;

        if (window_.empty()) {
            window_.resize(kFftSize);
            for (int i = 0; i < kFftSize; ++i) {
                // Symmetric Hann. Periodic would be arguably more
                // correct for STFT, but symmetric matches Pulp's
                // WindowFunction::hann and keeps the engine testable
                // against its helpers.
                window_[i] = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                                     / static_cast<float>(kFftSize - 1)));
            }
            // COLA constant for Hann² at 75% overlap is 1.5
            // (sum of w²[i + k*hop] over k in COLA range). Scaling
            // synthesis by 1/1.5 yields unity reconstruction.
            ola_scale_ = 1.0f / 1.5f;

            fft_     = pulp::signal::Fft(kFftSize);
            freq_.assign(kFftSize, {0.0f, 0.0f});
            windowed_.assign(kFftSize, 0.0f);
            bin_gain_.assign(kNumBins, 1.0f);
        }

        reset_channels_();  // fresh rings on every prepare
    }

    void release() override {}

    /// Latency = one full analysis window. The first output sample
    /// depends on the first full analysis frame, which is available
    /// only after kFftSize input samples have streamed in.
    int latency_samples() const override { return kFftSize; }

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        const BandField& field,
        const Viewport& view,
        Layout layout,
        ResponseMode /*mode*/) override
    {
        const auto chans = std::min(output.num_channels(), input.num_channels());
        const auto n_in  = input.num_samples();
        const auto n_out = std::min(output.num_samples(), n_in);
        const auto n_vis = visible_count(layout);

        // Recompute the per-bin gain table once per block. Stationary
        // within the block — the BandField doesn't change mid-buffer.
        build_bin_gain_(field, view, n_vis);

        ensure_channels_(chans);

        for (std::size_t ch = 0; ch < chans; ++ch) {
            auto src = input.channel(ch);
            auto dst = output.channel(ch);
            auto& s  = channel_state_[ch];

            for (std::size_t i = 0; i < n_out; ++i) {
                // Write input sample into the analysis ring.
                s.input_ring[s.input_pos] = src[i];
                s.input_pos = (s.input_pos + 1) % kFftSize;
                if (s.samples_filled < kFftSize) s.samples_filled++;

                // Hop countdown. Emit a new analysis + overlap-add when we
                // cross a hop boundary AND the ring holds a full window.
                if (s.samples_to_hop == 0 && s.samples_filled >= kFftSize) {
                    compute_and_overlap_add_(s);
                    s.samples_to_hop = kHopSize;
                }
                if (s.samples_to_hop > 0) s.samples_to_hop--;

                // Consume one overlap-added output sample.
                dst[i] = s.output_ring[s.output_pos];
                s.output_ring[s.output_pos] = 0.0f;  // clear after read
                s.output_pos = (s.output_pos + 1) % kFftSize;
            }
        }

        // Zero any output channels we didn't source from input.
        for (std::size_t ch = chans; ch < output.num_channels(); ++ch) {
            auto d = output.channel(ch);
            for (auto& x : d) x = 0.0f;
        }
    }

private:
    void reset_channels_() {
        for (auto& s : channel_state_) {
            std::fill(s.input_ring.begin(),  s.input_ring.end(),  0.0f);
            std::fill(s.output_ring.begin(), s.output_ring.end(), 0.0f);
            s.input_pos      = 0;
            s.output_pos     = 0;
            s.samples_to_hop = 0;
            s.samples_filled = 0;
        }
    }

    void ensure_channels_(std::size_t chans) {
        if (channel_state_.size() < chans) {
            const auto old = channel_state_.size();
            channel_state_.resize(chans);
            for (std::size_t i = old; i < chans; ++i) {
                channel_state_[i].input_ring.assign(kFftSize, 0.0f);
                channel_state_[i].output_ring.assign(kFftSize, 0.0f);
            }
        }
    }

    void build_bin_gain_(const BandField& field, const Viewport& view, std::size_t n_vis) {
        const float freq_step = static_cast<float>(sample_rate_)
                              / static_cast<float>(kFftSize);
        for (int k = 0; k < kNumBins; ++k) {
            const float hz = static_cast<float>(k) * freq_step;
            if (hz < view.min_hz || hz > view.max_hz) {
                const std::size_t idx = (hz <= view.min_hz) ? 0 : n_vis - 1;
                bin_gain_[k] = field.linear_gain(idx);
                continue;
            }
            const auto idx = view.band_for_hz(hz, n_vis);
            bin_gain_[k] = field.linear_gain(idx);
        }
    }

    void compute_and_overlap_add_(ChannelState& s) {
        // Read input_ring in chronological order starting at the
        // oldest sample (which is also the next write position).
        const int start = s.input_pos;
        for (int i = 0; i < kFftSize; ++i) {
            windowed_[i] = s.input_ring[(start + i) % kFftSize] * window_[i];
        }

        fft_.forward_real(windowed_.data(), freq_.data());

        // Apply per-bin gain. Keep conjugate symmetry so the inverse
        // produces a real time-domain signal.
        for (int k = 0; k < kNumBins; ++k) freq_[k] *= bin_gain_[k];
        for (int k = 1; k < kNumBins - 1; ++k) {
            freq_[kFftSize - k] = std::conj(freq_[k]);
        }

        fft_.inverse(freq_.data());

        // Overlap-add with synthesis windowing + COLA scale. The
        // output ring's current read position (s.output_pos) is the
        // sample that's about to be handed to the host; we accumulate
        // the new frame into positions [output_pos .. output_pos+N).
        for (int i = 0; i < kFftSize; ++i) {
            const int ring_i = (s.output_pos + i) % kFftSize;
            const float val  = freq_[i].real() * window_[i] * ola_scale_;
            s.output_ring[ring_i] += val;
        }
    }

    double                                sample_rate_ = 48000.0;
    pulp::signal::Fft                     fft_;
    std::vector<std::complex<float>>      freq_;
    std::vector<float>                    windowed_;
    std::vector<float>                    window_;
    std::vector<float>                    bin_gain_;
    float                                 ola_scale_   = 1.0f;
    std::vector<ChannelState>             channel_state_;
};

} // namespace

std::unique_ptr<SpectralEngine> make_windowed_stft_engine() {
    return std::make_unique<WindowedStftEngine>();
}

} // namespace spectr
