#include <spectr/mask_renderer.hpp>

#include <pulp/format/background_task_lane.hpp>
#include <pulp/signal/convolver.hpp>
#include <pulp/signal/convolver_messages.hpp>
#include <pulp/signal/dry_wet_mixer.hpp>
#include <pulp/signal/fir_design.hpp>
#include <pulp/runtime/trace.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string_view>
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

/// Half-width, in design bins, of the transition shaped into each drawn band
/// edge before the minimum-phase reconstruction. At the shipping 8192-point
/// grid and 48 kHz this is 8 x 5.86 Hz = 46.9 Hz of reach into the band from
/// each of its two edges -- and the same reach OUTWARD into each neighbour,
/// which is what the width ultimately costs.
///
/// 8 is the knee on three axes at once, measured through the renderer on a
/// muted band 20 of the default field. Depth arrives here: -118.3 dB, within
/// 1.2 dB of the best any width reaches, against -34.8 dB unshaped. Coverage
/// has already saturated by 4 bins and stays at 100 %, so it cannot pick a
/// width on its own. What breaks the tie is the cost outside the band, and it
/// turns sharply right here -- worst out-of-band deviation, measured over a
/// region held FIXED so every width is judged on the same ground:
///
///     K       4      6      8     12     16     24
///     leak  0.02   0.02   0.14   9.96  19.82  31.95   dB
///
/// One step past 8 buys 1.1 dB of depth for seventy times the leak, because
/// the transition starts reaching past the neighbouring edge. 8 is the widest
/// transition that still keeps its cost inside the band it belongs to.
constexpr int kTrackingTransitionHalfWidthBins = 8;

/// Crossfade requested of the convolver when a redesigned impulse response
/// replaces the live one. Zero -- the swap is instantaneous, and that is what
/// makes a drag continuous rather than what breaks it.
///
/// The convolver offers a parallel crossfade, and taking it looks like the
/// obviously safer choice. It is not, because of what the two swap paths do
/// with the INPUT DELAY LINE. A partitioned convolver holds a ring of the
/// spectra of recent input blocks; it depends on the audio, never on the
/// impulse, so it is the same history whichever impulse is live. The
/// instantaneous path moves that ring into the incoming impulse, which is what
/// makes the swap continuous. The crossfade path does not: it installs the
/// incoming impulse with a ZEROED ring and relies on the outgoing impulse
/// rendering in parallel to cover the gap.
///
/// Here the ring is `design_grid_size / kRenderBlock` = 128 partitions of 64
/// samples -- 8192 samples, 170 ms at 48 kHz. For that long after a faded swap
/// the live impulse convolves a history that is mostly silence, reproducing
/// only its own first partitions while the missing tail re-enters as the ring
/// refills.
///
/// A LONGER FADE DOES NOT RESCUE IT, which is the counter-intuitive part and
/// the reason this is a structural mismatch rather than a tuning error. While
/// the fade runs the output is a blend of a correct signal -- the outgoing
/// impulse, rendering from its real history -- and an incorrect one, so the
/// blend is wrong wherever the incoming side contributes at all. Swept on the
/// bare convolver with a bit-identical impulse, a fade EQUAL to the impulse
/// still disturbs the output for the impulse's whole length (8192 taps against
/// an 8192-sample fade: 170.2 ms; 32768 against 32768: 682.3 ms). It comes
/// clean only when the fade is tens of times the impulse, long enough for the
/// ring to refill while the outgoing side still dominates the blend -- a
/// 512-tap impulse needs a 32768-sample fade. No fade a gesture could tolerate
/// is in that range. A zero fade is exactly correct at every impulse length.
///
/// It compounds under a drag, because the convolver refuses a swap while a
/// fade is in flight. The swap rate is therefore pinned to one per fade, and a
/// held gesture never lets the live impulse accumulate more than one fade's
/// worth of the 8192 samples it needs -- so the whole gesture renders as a
/// perpetually re-onsetting, truncated convolution, which is heard as recent
/// material repeating rather than as a click. A deep attenuation is formed by
/// cancellation across the WHOLE impulse, so the same starvation is why a
/// gesture in a zoomed field reached only -18 dB of a drawn -40.
///
/// Measured through the convolver with a BIT-IDENTICAL impulse swapped into a
/// running 8192-tap response, so every reading is handoff cost and nothing
/// else: under a 512-sample fade one swap disturbs the output for 161.9 ms
/// with a peak error 0.82x the signal, and swaps at gesture cadence leave it
/// wrong continuously at 0.58x the signal. With no fade the same swaps are
/// bit-exact -- zero deviation, at any cadence.
///
/// What the instantaneous path gives up is the smooth boundary on a LARGE
/// change. It is a real cost and it is bounded: because the delay line is
/// carried, the incoming impulse's output is correct from its first sample, so
/// a big change lands as one step rather than as 93 ms of wrong output. A step
/// once per discrete action is the cheaper of the two.
///
/// A DRAG IS NOT FREE, THOUGH. The tempting inference -- consecutive designs
/// differ by a fraction of a dB, so there is no step to make -- does not hold.
/// Each swap steps the output by the change in the complex response, and
/// because the reconstruction is minimum phase a gain change carries a phase
/// change with it, so even a sub-dB redesign steps.
/// Broadband splatter above 5 kHz on a 704 Hz tone through a 40 dB band move,
/// republished every 8 ms, relative to the same gesture in the other mode:
///
///     gesture     20     40     80    160    320   dB/s
///     vs Mixing  237x   404x   594x   935x  2479x
///
/// It is linear in dB/s -- the rate the band is moved, not the swap cadence --
/// so a slow drag hides it and a quick one does not. A ZOOM is the worse case:
/// it rewrites min_hz/max_hz, so every band edge moves at once, and at the same
/// speed it measures 41x a single-band drag. The artifact is uniform across a
/// gesture relative to the LOCAL signal level (0.92-1.00 first-60ms vs
/// last-60ms); a downward drag only sounds front-loaded because the band it is
/// attenuating is loudest at the start.
///
/// Coalescing or rate-limiting the redesign is NOT the mitigation it looks
/// like: at a fixed 160 dB/s, dropping from 95 swaps to 5 makes it 18x WORSE,
/// because the per-swap step grows faster than the count falls.
///
/// The cure is a crossfade, and it is blocked on the convolver rather than on
/// this constant. It becomes available once a swap can fade from the SAME
/// retained input history instead of installing the incoming impulse cold.
/// Measured end to end against that convolver, a 128-sample (2.7 ms) fade puts
/// a band drag back on the arithmetic floor and 192 samples (4 ms) does the
/// same for a zoom -- an 896x and 1656x reduction, with the delivered magnitude
/// unchanged at -39.67 dB. Both sit inside the 384-sample gap between redesigns
/// at a 120 Hz pointer, which matters because the convolver refuses a swap
/// while a fade is in flight: a 512-sample fade drops 33 swaps to 26.
///
/// Until that lands in a cut SDK this must stay 0. Against the CURRENT one a
/// non-zero value reinstates the cold start measured above -- a 64-sample fade
/// scores 0.1095 non-tonal residual where the instantaneous path scores 0.0017,
/// 66x worse -- so the fade and the SDK repin are one change, never two.
constexpr std::size_t kIrCrossfadeSamples = 0;

/// Test seam for this rule's negative control.
///
/// A gate nobody has watched go red is not a gate, and the defect this one
/// forbids lives in a constant -- so the control has to be able to put the
/// constant back. This reinstates exactly the pre-fix value, in the shipping
/// binary, so the contract is proven against the code that actually ships
/// rather than against a compile-time variant of it. Read once per process and
/// unset in every shipping run.
std::size_t ir_swap_crossfade_samples() {
    static const std::size_t samples = [] {
        const char* value = std::getenv("SPECTR_SWAP_PLANT");
        return (value != nullptr && std::string_view(value) == "history-reset")
                   ? std::size_t{512}
                   : kIrCrossfadeSamples;
    }();
    return samples;
}

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
    [[nodiscard]] int design_grid_size() const noexcept override {
        return config_.design_grid_size;
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

        // Past this point the previous prepared state is gone, so every
        // remaining failure leaves the renderer UNPREPARED rather than
        // half-applied — which is what the contract promises and what
        // `prepared()` then reports.
        prepared_ = false;
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
            convolvers_[static_cast<std::size_t>(ch)].set_crossfade(
                ir_swap_crossfade_samples());
        }

        pending_generation_.store(0, std::memory_order_release);
        active_generation_.store(0, std::memory_order_release);
        next_generation_ = 1;
        rt_layout_pending_.store(false, std::memory_order_relaxed);

        // Last, and only once everything it depends on exists. A renderer
        // that reported itself prepared without a design worker would accept
        // a staged layout from the audio thread and silently never realise
        // it, which is worse than refusing to prepare at all.
        if (!lane_.start(&ZeroLatencyMaskRenderer::handle_design_, this,
                         pulp::format::BackgroundTaskPolicy::Latest))
            return false;
        prepared_ = true;
        return true;
    }

    [[nodiscard]] bool prepared() const noexcept override { return prepared_; }

    [[nodiscard]] int latency_samples() const noexcept override {
        return kRenderBlock;
    }

    [[nodiscard]] int maximum_tail_samples() const noexcept override {
        // One render block of input buffering plus the whole designed impulse.
        return prepared_ ? kRenderBlock + config_.design_grid_size : 0;
    }
    [[nodiscard]] int design_grid_size() const noexcept override {
        return config_.design_grid_size;
    }

    [[nodiscard]] bool publish_layout(const Layout& layout) override {
        if (!prepared_) return false;
        // Control thread: design inline. Allocation and a few FFTs are
        // allowed here, and doing the work now means a state restore or a
        // prepare leaves a correct impulse staged before audio starts.
        PULP_TRACE_SCOPE_NAMED("state", "redesign filter bank (UI thread)");
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
        {
            PULP_TRACE_SCOPE_NAMED("state", "compile band mask table");
            if (!pulp::signal::build_spectral_mask(
                    layout, config_.design_grid_size,
                    static_cast<float>(config_.sample_rate), table))
                return false;
        }
        PULP_TRACE_COUNTER("state", "active bands",
                           static_cast<int64_t>(table.active_bands));

        const auto bins = static_cast<std::size_t>(table.num_bins);
        if (bins != design_magnitudes_.size()) return false;
        for (std::size_t i = 0; i < bins; ++i)
            design_magnitudes_[i] = static_cast<double>(table.gain_linear[i]);

        // Shape the drawn edges before reconstructing. This is the zero-latency
        // realisation's own step: the table, the layout and every other mode
        // are untouched by it, so the linear-phase path keeps realising the
        // drawn magnitude exactly as authored.
        {
            PULP_TRACE_SCOPE_NAMED("state", "shape band-edge transitions");
            (void)shape_tracking_transitions(
                design_magnitudes_,
                std::span<const float>(
                    table.band_edges_hz.data(),
                    static_cast<std::size_t>(table.active_bands) + 1u),
                config_.sample_rate
                    / static_cast<double>(config_.design_grid_size),
                kTrackingTransitionHalfWidthBins,
                kDesignMagnitudeFloor);
        }

        {
            PULP_TRACE_SCOPE_NAMED("state",
                                   "design minimum-phase impulse (FFT)");
            if (!design_into_taps_()) return false;
        }

        // Publish the generation BEFORE the impulse it describes becomes
        // visible, so the audio thread can never adopt an impulse whose
        // generation has not been written yet.
        const auto generation = next_generation_++;
        pending_generation_.store(generation, std::memory_order_release);

        bool staged_any = false;
        for (int ch = 0; ch < channels_; ++ch) {
            auto& swapper = *swappers_[static_cast<std::size_t>(ch)];
            {
                PULP_TRACE_SCOPE_NAMED("state", "free retired impulse");
                swapper.drain_old();
            }
            PULP_TRACE_SCOPE_NAMED("state", "stage impulse for audio thread");
            if (swapper.stage_ir(design_taps_.data(), design_taps_.size(),
                                 static_cast<std::size_t>(kRenderBlock)))
                staged_any = true;
        }
        return staged_any;
    }

    static void handle_design_(void* context, const Layout& layout) {
        PULP_TRACE_SCOPE_NAMED("state",
                               "redesign filter bank (worker, audio-driven)");
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

TrackingTransitionGeometry shape_tracking_transitions(
    std::span<double> magnitudes,
    std::span<const float> band_edges_hz,
    double bin_width_hz,
    int half_width_bins,
    double magnitude_floor) noexcept {
    TrackingTransitionGeometry geometry{};

    const auto num_bins  = static_cast<std::ptrdiff_t>(magnitudes.size());
    const auto num_edges = static_cast<std::ptrdiff_t>(band_edges_hz.size());
    constexpr std::ptrdiff_t kMaximumEdges =
        static_cast<std::ptrdiff_t>(pulp::signal::kSpectralBandMaskMaximumBands) + 1;
    if (num_bins < 2 || num_edges < 2 || num_edges > kMaximumEdges
        || !(bin_width_hz > 0.0) || half_width_bins <= 0
        || !(magnitude_floor > 0.0))
        return geometry;

    // Edge frequencies onto the design grid, clamped into the array. A
    // non-finite or out-of-range edge lands on a valid bin rather than
    // indexing out of bounds; a degenerate layout then simply shapes nothing,
    // because every clamp below collapses to zero width.
    std::array<std::ptrdiff_t, static_cast<std::size_t>(kMaximumEdges)> edge_bin{};
    for (std::ptrdiff_t e = 0; e < num_edges; ++e) {
        const double hz = static_cast<double>(band_edges_hz[static_cast<std::size_t>(e)]);
        const double bin = std::isfinite(hz) ? std::round(hz / bin_width_hz) : 0.0;
        edge_bin[static_cast<std::size_t>(e)] = static_cast<std::ptrdiff_t>(
            std::clamp(bin, 0.0, static_cast<double>(num_bins - 1)));
    }

    // Two passes. The plateau either side of an edge is sampled from the
    // UNSHAPED magnitude for every edge before anything is written, so the
    // result cannot depend on the order edges are visited even where two
    // transitions meet exactly at a midpoint.
    struct Shaping {
        std::ptrdiff_t centre = 0;
        std::ptrdiff_t half   = 0;
        double         low    = 0.0;   ///< log magnitude entering the edge
        double         high   = 0.0;   ///< log magnitude leaving it
    };
    std::array<Shaping, static_cast<std::size_t>(kMaximumEdges)> shaping{};
    std::ptrdiff_t shaped = 0;

    for (std::ptrdiff_t e = 0; e < num_edges; ++e) {
        const auto centre = edge_bin[static_cast<std::size_t>(e)];
        auto half = static_cast<std::ptrdiff_t>(half_width_bins);
        // Half the distance to each neighbouring edge: two transitions may
        // touch at the midpoint between their edges, never overlap.
        if (e > 0)
            half = std::min(half,
                            (centre - edge_bin[static_cast<std::size_t>(e - 1)]) / 2);
        if (e + 1 < num_edges)
            half = std::min(half,
                            (edge_bin[static_cast<std::size_t>(e + 1)] - centre) / 2);
        // And the ends of the array.
        half = std::min(half, centre);
        half = std::min(half, num_bins - 1 - centre);

        ++geometry.edges_considered;
        if (half <= 0) continue;   // no room: this edge stays the drawn step

        const auto at_log = [&](std::ptrdiff_t bin) {
            return std::log(std::max(magnitudes[static_cast<std::size_t>(bin)],
                                     magnitude_floor));
        };
        auto& s = shaping[static_cast<std::size_t>(shaped++)];
        s.centre = centre;
        s.half   = half;
        s.low    = at_log(centre - half);
        s.high   = at_log(centre + half);

        ++geometry.edges_shaped;
        geometry.widest_half_width =
            std::max(geometry.widest_half_width, static_cast<int>(half));
        geometry.narrowest_half_width =
            geometry.edges_shaped == 1
                ? static_cast<int>(half)
                : std::min(geometry.narrowest_half_width, static_cast<int>(half));
    }

    for (std::ptrdiff_t i = 0; i < shaped; ++i) {
        const auto& s = shaping[static_cast<std::size_t>(i)];
        const auto span = static_cast<double>(2 * s.half);
        for (std::ptrdiff_t bin = s.centre - s.half; bin <= s.centre + s.half; ++bin) {
            const double x = static_cast<double>(bin - (s.centre - s.half)) / span;
            // Cubic smoothstep: continuous in value AND slope at both ends, so
            // the shaping does not put a fresh first-derivative break where it
            // just removed a step. The choice is deliberately not load-bearing
            // — across linear through 7th-order smoothstep the realised depth
            // moves by about 3 dB, against the tens of dB the WIDTH is worth —
            // so this is the simplest curve with that continuity, not a tuned
            // one.
            const double shape = x * x * (3.0 - 2.0 * x);
            magnitudes[static_cast<std::size_t>(bin)] =
                std::exp(s.low + (s.high - s.low) * shape);
        }
    }

    return geometry;
}

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
