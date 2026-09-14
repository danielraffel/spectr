#include <spectr/mask_renderer.hpp>

#include <pulp/format/background_task_lane.hpp>
#include <pulp/signal/convolver.hpp>
#include <pulp/signal/convolver_messages.hpp>
#include <pulp/signal/dry_wet_mixer.hpp>
#include <pulp/signal/fir_design.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace spectr {
namespace {

/// The zero-latency renderer's internal render granularity, in samples.
///
/// Uniform overlap-save produces a block's output the moment a whole block of
/// input has arrived, so a renderer that must accept ANY block length from the
/// host buys that with exactly this many samples of input buffering — and no
/// more. The constant is deliberately a property of the MODE, not of the host:
/// tying it to the host's buffer size would make the same saved project report
/// a different latency on a different machine, which is the recall hazard the
/// contract exists to prevent. 64 samples is 1.33 ms at 48 kHz.
constexpr int kRenderBlock = 64;

/// Magnitudes below this are floored before the logarithm that the cepstral
/// reconstruction takes. A muted band therefore realises at -120 dB rather
/// than at an exact zero whose logarithm has no bound.
constexpr double kDesignMagnitudeFloor = 1.0e-6;

/// Crossfade applied when a redesigned impulse response replaces the live one,
/// so a mask edit is audibly continuous rather than a hard cut.
constexpr std::size_t kIrCrossfadeSamples = 512;

bool valid_config(const MaskRendererConfig& config) noexcept {
    if (config.channels <= 0 || config.channels > 8) return false;
    if (config.max_block <= 0) return false;
    if (!(config.sample_rate > 0.0)) return false;
    if (!(config.initial_mix >= 0.0f) || !(config.initial_mix <= 1.0f)) return false;
    if (config.mix_ramp_samples < 0) return false;
    const int grid = config.design_grid_size;
    if (grid < 256 || (grid & (grid - 1)) != 0) return false;
    return true;
}

// ── Linear phase ───────────────────────────────────────────────────────────

/// Today's realisation, behind the contract and otherwise unchanged: the
/// framework's WOLA mask processor, which multiplies each analysis frame by
/// the compiled table.
class LinearPhaseMaskRenderer final : public MaskRenderer {
public:
    [[nodiscard]] bool prepare(const MaskRendererConfig& config) override {
        if (!valid_config(config)) return false;
        if (config.analysis_hop <= 0
            || config.analysis_hop > config.design_grid_size / 2)
            return false;

        pulp::signal::SpectralMaskProcessorConfig c;
        c.frame.fft_size     = config.design_grid_size;
        c.frame.analysis_hop = config.analysis_hop;
        c.frame.channels     = config.channels;
        c.frame.max_block    = config.max_block;
        c.frame.window       = pulp::signal::WindowFunction::Type::hann;
        c.sample_rate        = static_cast<float>(config.sample_rate);
        c.initial_mix        = config.initial_mix;
        c.mix_ramp_samples   = config.mix_ramp_samples;
        c.mix_curve          = pulp::signal::MixCurve::Linear;
        if (!processor_.prepare(c)) return false;
        config_ = config;
        return true;
    }

    [[nodiscard]] bool prepared() const noexcept override {
        return processor_.prepared();
    }
    [[nodiscard]] int latency_samples() const noexcept override {
        return processor_.prepared()
                   ? processor_.latency_samples()
                   : mask_render_latency_samples(MaskRenderMode::linear_phase, config_);
    }
    [[nodiscard]] int maximum_tail_samples() const noexcept override {
        return processor_.maximum_tail_samples();
    }
    [[nodiscard]] bool publish_layout(const Layout& layout) override {
        if (!processor_.publish_layout(layout)) return false;
        generation_.fetch_add(1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool set_layout_rt(const Layout& layout) noexcept override {
        if (!processor_.set_layout_rt(layout)) return false;
        generation_.fetch_add(1, std::memory_order_release);
        return true;
    }
    void set_mix(float mix) noexcept override { processor_.set_mix(mix); }
    [[nodiscard]] bool process(const float* const* input, float* const* output,
                               int num_samples) noexcept override {
        return processor_.process(input, output, num_samples);
    }
    void reset() noexcept override { processor_.reset(); }
    [[nodiscard]] unsigned long long active_generation() const noexcept override {
        return generation_.load(std::memory_order_acquire);
    }

private:
    pulp::signal::SpectralMaskProcessor processor_{};
    MaskRendererConfig                  config_{};
    std::atomic<unsigned long long>     generation_{0};
};

// ── Zero latency ───────────────────────────────────────────────────────────

/// The same drawn magnitude, realised as a causal minimum-phase FIR and
/// applied by uniform partitioned convolution.
///
/// Design — the cepstral reconstruction and the partition spectra it implies —
/// runs on a worker. The audio thread only ever fills a render block, adopts a
/// finished impulse response at a block boundary, and convolves. Nothing on
/// the audio path reads a clock, allocates, or blocks: the schedule is
/// expressed entirely in samples, which is what makes a faster-than-real-time
/// bounce produce the same samples as real-time playback by construction
/// rather than by a flag the host may not set.
class ZeroLatencyMaskRenderer final : public MaskRenderer {
public:
    ~ZeroLatencyMaskRenderer() override { lane_.stop(); }

    [[nodiscard]] bool prepare(const MaskRendererConfig& config) override {
        if (!valid_config(config)) return false;

        lane_.stop();

        const int bins = config.design_grid_size / 2 + 1;
        // The renderer states its own grid requirement rather than inheriting
        // either framework capacity. Raising the grid is a configuration
        // change; it must fail closed here, never silently truncate.
        if (static_cast<std::size_t>(bins) > pulp::signal::kSpectralBandMaskMaximumBins)
            return false;
        if (static_cast<std::size_t>(config.design_grid_size)
            > pulp::signal::kMaximumMinimumPhaseFirSize)
            return false;

        config_ = config;
        channels_ = config.channels;

        convolvers_ = std::vector<pulp::signal::PartitionedConvolver>(
            static_cast<std::size_t>(channels_));
        swappers_ = std::vector<std::unique_ptr<pulp::signal::ConvolverIrSwapper>>();
        swappers_.reserve(static_cast<std::size_t>(channels_));
        for (int ch = 0; ch < channels_; ++ch)
            swappers_.push_back(std::make_unique<pulp::signal::ConvolverIrSwapper>());

        in_fifo_.assign(static_cast<std::size_t>(channels_ * kRenderBlock), 0.0f);
        out_fifo_.assign(static_cast<std::size_t>(channels_ * kRenderBlock), 0.0f);
        in_ptrs_.assign(static_cast<std::size_t>(channels_), nullptr);
        out_ptrs_.assign(static_cast<std::size_t>(channels_), nullptr);
        for (int ch = 0; ch < channels_; ++ch) {
            in_ptrs_[static_cast<std::size_t>(ch)] =
                in_fifo_.data() + static_cast<std::size_t>(ch * kRenderBlock);
            out_ptrs_[static_cast<std::size_t>(ch)] =
                out_fifo_.data() + static_cast<std::size_t>(ch * kRenderBlock);
        }
        fill_ = 0;

        design_magnitudes_.assign(static_cast<std::size_t>(bins), 0.0);
        design_taps_.assign(static_cast<std::size_t>(config.design_grid_size), 0.0f);

        mixer_.set_wet_latency(kRenderBlock);
        mixer_.set_mix(config.initial_mix);
        mixer_.set_curve(pulp::signal::MixCurve::Linear);
        mixer_.set_ramp_samples(config.mix_ramp_samples);
        mixer_.prepare(channels_, config.max_block);

        // Start from a unity magnitude so the very first blocks are a wire
        // rather than silence, then let the first published layout replace it.
        std::fill(design_magnitudes_.begin(), design_magnitudes_.end(), 1.0);
        if (!design_into_taps_()) return false;
        for (int ch = 0; ch < channels_; ++ch) {
            convolvers_[static_cast<std::size_t>(ch)].load_ir(
                design_taps_.data(), design_taps_.size(),
                static_cast<std::size_t>(kRenderBlock));
            convolvers_[static_cast<std::size_t>(ch)].set_crossfade(kIrCrossfadeSamples);
        }

        prepared_ = true;
        pending_generation_.store(0, std::memory_order_release);
        active_generation_.store(0, std::memory_order_release);
        next_generation_ = 1;
        rt_layout_pending_.store(false, std::memory_order_relaxed);

        return lane_.start(&ZeroLatencyMaskRenderer::handle_design_, this,
                           pulp::format::BackgroundTaskPolicy::Latest);
    }

    [[nodiscard]] bool prepared() const noexcept override { return prepared_; }

    [[nodiscard]] int latency_samples() const noexcept override {
        return kRenderBlock;
    }

    [[nodiscard]] int maximum_tail_samples() const noexcept override {
        // One render block of input buffering plus the whole designed impulse.
        return prepared_ ? kRenderBlock + config_.design_grid_size : 0;
    }

    [[nodiscard]] bool publish_layout(const Layout& layout) override {
        if (!prepared_) return false;
        // Control thread: design inline. Allocation and a few FFTs are
        // allowed here, and doing the work now means a state restore or a
        // prepare leaves a correct impulse staged before audio starts.
        std::lock_guard<std::mutex> guard(design_mutex_);
        return design_and_stage_(layout);
    }

    [[nodiscard]] bool set_layout_rt(const Layout& layout) noexcept override {
        if (!prepared_) return false;
        // Audio thread: copy the layout into prepared storage and mark it.
        // No design work, no allocation, no lock. The worker picks it up.
        rt_layout_ = layout;
        rt_layout_pending_.store(true, std::memory_order_release);
        return true;
    }

    void set_mix(float mix) noexcept override {
        if (std::isfinite(mix)) mixer_.set_mix(mix);
    }

    // SPECTR-RENDER-PATH BEGIN
    //
    // Everything between these markers runs on the audio thread. It must
    // contain no wall-clock read, no sleep, no thread handle and no lock: the
    // schedule here is expressed purely in samples, which is what makes an
    // offline bounce and real-time playback produce identical samples whether
    // or not the host tells the plugin which one it is. The markers are not
    // decoration — `tools/ci/check_render_path_clock.py` scans exactly this
    // region, and `test/test_mask_renderer.cpp` proves the scan can fail.
    [[nodiscard]] bool process(const float* const* input, float* const* output,
                               int num_samples) noexcept override {
        if (!prepared_ || input == nullptr || output == nullptr
            || num_samples <= 0 || num_samples > config_.max_block)
            return false;
        for (int ch = 0; ch < channels_; ++ch)
            if (input[ch] == nullptr || output[ch] == nullptr) return false;

        if (rt_layout_pending_.exchange(false, std::memory_order_acquire))
            (void)lane_.try_spawn(rt_layout_);

        mixer_.push_dry(input, channels_, num_samples);

        for (int i = 0; i < num_samples; ++i) {
            const auto slot = static_cast<std::size_t>(fill_);
            for (int ch = 0; ch < channels_; ++ch) {
                const auto base = static_cast<std::size_t>(ch * kRenderBlock);
                // Read the finished sample BEFORE overwriting the input slot,
                // so an in-place caller (output aliasing input) is safe.
                const float dry = input[ch][i];
                output[ch][i] = out_fifo_[base + slot];
                in_fifo_[base + slot] = dry;
            }
            if (++fill_ == kRenderBlock) {
                fill_ = 0;
                render_block_();
            }
        }

        mixer_.mix_wet(output, channels_, num_samples);
        return true;
    }

private:
    void render_block_() noexcept {
        bool swapped = false;
        for (int ch = 0; ch < channels_; ++ch) {
            auto& conv = convolvers_[static_cast<std::size_t>(ch)];
            if (conv.try_swap_ir(*swappers_[static_cast<std::size_t>(ch)]))
                swapped = true;
            conv.process(in_ptrs_[static_cast<std::size_t>(ch)],
                         out_ptrs_[static_cast<std::size_t>(ch)], kRenderBlock);
        }
        if (swapped)
            active_generation_.store(
                pending_generation_.load(std::memory_order_acquire),
                std::memory_order_release);
    }
    // SPECTR-RENDER-PATH END

public:
    void reset() noexcept override {
        if (!prepared_) return;
        std::fill(in_fifo_.begin(), in_fifo_.end(), 0.0f);
        std::fill(out_fifo_.begin(), out_fifo_.end(), 0.0f);
        fill_ = 0;
        for (auto& conv : convolvers_) conv.reset();
        mixer_.reset();
    }

    [[nodiscard]] unsigned long long active_generation() const noexcept override {
        return active_generation_.load(std::memory_order_acquire);
    }

private:
    /// Cepstral minimum-phase reconstruction of `design_magnitudes_` into
    /// `design_taps_`. Worker or control thread; never the audio thread.
    [[nodiscard]] bool design_into_taps_() {
        pulp::signal::MinimumPhaseFirOptions options;
        options.coefficient_count = static_cast<std::size_t>(config_.design_grid_size);
        options.log_magnitude_floor = kDesignMagnitudeFloor;
        const auto result =
            pulp::signal::reconstruct_minimum_phase_fir(design_magnitudes_, options);
        if (!result) return false;
        const auto count = std::min(design_taps_.size(), result.coefficients.size());
        for (std::size_t i = 0; i < count; ++i)
            design_taps_[i] = static_cast<float>(result.coefficients[i]);
        std::fill(design_taps_.begin() + static_cast<std::ptrdiff_t>(count),
                  design_taps_.end(), 0.0f);
        return true;
    }

    /// Compile a layout to a table, design its minimum-phase impulse, and
    /// stage it for the audio thread to adopt at its next block boundary.
    [[nodiscard]] bool design_and_stage_(const Layout& layout) {
        pulp::signal::SpectralMaskTable table;
        if (!pulp::signal::build_spectral_mask(
                layout, config_.design_grid_size,
                static_cast<float>(config_.sample_rate), table))
            return false;

        const auto bins = static_cast<std::size_t>(table.num_bins);
        if (bins != design_magnitudes_.size()) return false;
        for (std::size_t i = 0; i < bins; ++i)
            design_magnitudes_[i] = static_cast<double>(table.gain_linear[i]);

        if (!design_into_taps_()) return false;

        // Publish the generation BEFORE the impulse it describes becomes
        // visible, so the audio thread can never adopt an impulse whose
        // generation has not been written yet.
        const auto generation = next_generation_++;
        pending_generation_.store(generation, std::memory_order_release);

        bool staged_any = false;
        for (int ch = 0; ch < channels_; ++ch) {
            auto& swapper = *swappers_[static_cast<std::size_t>(ch)];
            swapper.drain_old();
            if (swapper.stage_ir(design_taps_.data(), design_taps_.size(),
                                 static_cast<std::size_t>(kRenderBlock)))
                staged_any = true;
        }
        return staged_any;
    }

    static void handle_design_(void* context, const Layout& layout) {
        auto* self = static_cast<ZeroLatencyMaskRenderer*>(context);
        std::lock_guard<std::mutex> guard(self->design_mutex_);
        (void)self->design_and_stage_(layout);
    }

    MaskRendererConfig config_{};
    bool               prepared_ = false;
    int                channels_ = 0;

    std::vector<pulp::signal::PartitionedConvolver>                     convolvers_;
    std::vector<std::unique_ptr<pulp::signal::ConvolverIrSwapper>>      swappers_;
    pulp::signal::DryWetMixer                                           mixer_{};

    std::vector<float>  in_fifo_;
    std::vector<float>  out_fifo_;
    std::vector<float*> in_ptrs_;
    std::vector<float*> out_ptrs_;
    int                 fill_ = 0;

    std::vector<double> design_magnitudes_;
    std::vector<float>  design_taps_;
    std::mutex          design_mutex_;
    unsigned long long  next_generation_ = 1;

    Layout            rt_layout_{};
    std::atomic<bool> rt_layout_pending_{false};

    std::atomic<unsigned long long> pending_generation_{0};
    std::atomic<unsigned long long> active_generation_{0};

    pulp::format::BackgroundTaskLane<Layout, 8> lane_;
};

} // namespace

int mask_render_latency_samples(MaskRenderMode mode,
                                const MaskRendererConfig& config) noexcept {
    switch (mode) {
    case MaskRenderMode::linear_phase:
        // This restates the frame engine's own causal bound, because the
        // answer has to be available BEFORE anything is prepared — that is
        // the whole point of the function. The restatement is not left to
        // drift: "Mask renderer latency depends on the mode and nothing else"
        // compares it against a prepared renderer's reported value, so if the
        // engine ever reclaims the hop term this fails loudly here rather
        // than silently mis-reporting to a host.
        return config.design_grid_size + config.analysis_hop;
    case MaskRenderMode::zero_latency:
        return kRenderBlock;
    }
    return 0;
}

std::unique_ptr<MaskRenderer> make_mask_renderer(MaskRenderMode mode) {
    switch (mode) {
    case MaskRenderMode::linear_phase:
        return std::make_unique<LinearPhaseMaskRenderer>();
    case MaskRenderMode::zero_latency:
        return std::make_unique<ZeroLatencyMaskRenderer>();
    }
    return nullptr;
}

} // namespace spectr
