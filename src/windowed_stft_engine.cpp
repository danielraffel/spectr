#include "spectr/windowed_stft_engine.hpp"

#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_frame_engine.hpp>

#include <algorithm>
#include <array>
#include <complex>

namespace spectr {

namespace {

constexpr int kFftSize = 1024;
constexpr int kHopSize = kFftSize / 4;
constexpr std::size_t kMaximumChannels = 64;

class WindowedStftEngine final : public SpectralEngine {
public:
    void prepare(const EnginePrepare& p) override {
        prepared_channels_ = std::clamp(p.channels, 1,
                                        static_cast<int>(kMaximumChannels));
        pulp::signal::SpectralFrameEngineConfig config;
        config.fft_size = kFftSize;
        config.analysis_hop = kHopSize;
        config.channels = prepared_channels_;
        config.max_block = std::max(p.max_block, 1);
        config.window = pulp::signal::WindowFunction::Type::hann;
        engine_.prepare(config);
        prepared_ = true;
    }

    void release() override {
        engine_.reset();
        prepared_ = false;
    }

    int latency_samples() const override {
        return prepared_ ? engine_.latency_samples() : kFftSize + kHopSize;
    }

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        const BandField& /*field*/,
        const Viewport& /*view*/,
        Layout /*layout*/,
        ResponseMode /*mode*/,
        const pulp::signal::SpectralMaskTable& mask) override
    {
        const auto channels = std::min(output.num_channels(), input.num_channels());
        const auto samples = std::min(output.num_samples(), input.num_samples());
        if (!prepared_ || channels != static_cast<std::size_t>(prepared_channels_)
            || samples == 0) {
            zero_output_(output);
            return;
        }

        for (std::size_t channel = 0; channel < channels; ++channel) {
            input_channels_[channel] = input.channel(channel).data();
            output_channels_[channel] = output.channel(channel).data();
        }

        bool mask_ok = true;
        engine_.process(input_channels_.data(), output_channels_.data(),
                        static_cast<int>(samples),
            [&](std::complex<float>* const* frames, int bins) {
                if (!pulp::signal::apply_spectral_mask(
                        frames, prepared_channels_, bins, mask)) {
                    mask_ok = false;
                    for (int channel = 0; channel < prepared_channels_; ++channel)
                        std::fill_n(frames[channel], bins, std::complex<float>{});
                }
            });

        if (!mask_ok) zero_output_(output);
        for (std::size_t channel = channels; channel < output.num_channels(); ++channel)
            std::fill(output.channel(channel).begin(), output.channel(channel).end(), 0.0f);
    }

private:
    static void zero_output_(pulp::audio::BufferView<float>& output) noexcept {
        for (std::size_t channel = 0; channel < output.num_channels(); ++channel)
            std::fill(output.channel(channel).begin(), output.channel(channel).end(), 0.0f);
    }

    pulp::signal::SpectralFrameEngine engine_;
    std::array<const float*, kMaximumChannels> input_channels_{};
    std::array<float*, kMaximumChannels> output_channels_{};
    int prepared_channels_ = 0;
    bool prepared_ = false;
};

} // namespace

std::unique_ptr<SpectralEngine> make_windowed_stft_engine() {
    return std::make_unique<WindowedStftEngine>();
}

} // namespace spectr
