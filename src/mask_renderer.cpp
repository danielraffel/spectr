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

/// Width, in design bins, of the transition shaped into each drawn band edge
/// before the minimum-phase reconstruction. At the shipping 8192-point grid and
/// 48 kHz this is 8 x 5.86 Hz = 46.9 Hz, and it reaches that far into the
/// QUIETER band only -- the band being attenuated. The louder side keeps every
/// bin it was drawn with.
///
/// 8 was chosen under the SYMMETRIC placement this replaced, on the reach
/// OUTWARD into the neighbour: coverage saturates by 4 bins and cannot pick a
/// width on its own, while leak turned sharply right here --
///
///     K          4      6      8     12     16     24
///     leak    0.06   0.03   0.06   8.52  18.38  30.82   dB
///
/// -- so one step past 8 bought a few dB of depth for a hundred and forty
/// times the leak, and 8.5 dB of error OUTSIDE a band is plainly audible where
/// a few dB inside a muted one is not.
///
/// THAT REASON NO LONGER PINS IT. Those numbers are the symmetric geometry's,
/// where the reach outward equals the whole width; under the placement below
/// the reach outward is a QUARTER of it, and the same column re-measured on
/// this geometry stays at or under 0.02 dB all the way to K = 24. Width is now
/// nearly free on the axis that chose this value.
///
/// It is left at 8 anyway, because the sweep that freed it found no width
/// worth moving to. Widening deepens the mute and costs COVERAGE, which falls
/// about 1.2 points per bin (90.3 % here, 88.9 % at 9, 85.4 % at 12, 80.6 % at
/// 16 against an 88 % floor), and the one cell that clears every gate -- 9 --
/// clears the detection-floor control by where a single probe bin's ripple
/// happened to fall rather than by realising the depth it asserts. The full
/// table and its controls are in test_tracking_transition_sweep.cpp; read it
/// before moving this number.
constexpr int kTrackingTransitionWidthBins = 8;

/// How much of each transition is allowed to sit OUTSIDE the quieter band, as a
/// percentage of the width above -- i.e. how far it reaches back into the band
/// the user kept.
///
/// Zero puts the whole roll-off inside the attenuated band, so a kept band comes
/// back exactly as wide as it was drawn. That is not free. The response has to
/// travel from unity to the floor somewhere, and every bin of that travel is
/// taken either from the band being kept or from the band being attenuated --
/// and the total distance it travels is what lets the cepstrum decay before it
/// wraps, so the same axis sets the realised depth. Measured through the
/// product on the sub-bin design, kept width at the band nearest 1 kHz against
/// muted band 20's interior sup and coverage:
///
///     outside   kept: 32 full / decade / 64 full    depth      coverage
///        0 %        100 %    101 %    100 %       -70.58 dB     86.0 %
///       25 %         96 %     87 %     93 %       -81.26 dB     90.3 %
///       50 %         91 %     66 %     81 %       -94.56 dB     93.5 %
///      100 %         79 %     23 %     56 %      -109.56 dB    100.0 %
///
/// 100 % is the symmetric placement this replaced, and the row reproduces it
/// exactly -- which is what proves this function is the same function with the
/// asymmetry turned off.
///
/// The axis is monotonic and there is no free lunch on it, so 25 % is chosen as
/// the only setting that clears the kept-width floor (85 %) and the depth and
/// coverage gates at once. It clears the latter two by 1.26 dB and 2.3 points.
/// That is thinner than a gate wants to be, and it is thin because BOTH costs
/// on this axis now land at once: sub-bin edge placement spends about 8.7 dB of
/// depth to buy a glide under a viewport drag, and asymmetric placement spends
/// depth to buy the kept band back. Either alone leaves about 10 dB of room;
/// together they leave 1.26.
///
/// The width above was the remaining lever and it has now been swept across
/// both axes at once. It does buy depth -- 9 bins reads -89.07 dB here, seven
/// dB of margin instead of one -- and the two axes separate cleanly: kept
/// width is a function of THIS number alone (the decade case holds 86-87 % at
/// 25 % for every width from 8 to 16, and 64-66 % at 50 % for every one of
/// them), while depth and coverage are functions of the width alone. What
/// width cannot buy is the thing actually blocking: a band DRAWN at -100 dB is
/// realised across its middle half to within 2.6 dB under the symmetric
/// placement and to within 19.9 dB at 9 bins, so the control that reads it
/// passes there on one bin's ripple. Realising it honestly needs 13 bins,
/// which costs coverage; restoring coverage needs 45 % or more outside, which
/// costs the decade band two thirds of its width. The three do not meet.
///
/// The third axis -- how finely an edge may be positioned -- was then swept the
/// same way and is no better: see `kTrackingEdgeQuantumBins`, where the whole
/// frontier is printed. What finally moved was neither geometry axis but the
/// control that was blocking: it asserted the renderer could REALISE -100 dB,
/// which is a fidelity claim, where its purpose is only to show the -80 dB
/// depth gate sits above the measurement's detection floor. That purpose is met
/// here with 7.7 dB to spare, and the control now states and measures it. The
/// derivation is written beside the control in test_tracking_transition.cpp.
constexpr int kTransitionOutsidePct = 25;

/// Grid that each band edge's fractional position is snapped to before a
/// transition is placed about it, in design bins. Zero is the exact position.
///
/// This is the third geometry axis and the only one with a term on BOTH sides
/// of the trade. A coarser grid deepens the mute -- whole-bin edges put every
/// transition shoulder exactly on a bin, so the cepstrum sees a shape it can
/// represent and decays further before it wraps -- and it is also precisely
/// what makes a viewport drag a staircase instead of a glide, because an edge
/// that can only sit on grid points holds still and then jumps.
///
/// So it is a Pareto question, not a threshold: depth inside a muted band is
/// inaudible, pitch wobble under a drag plainly is not, and a coarser grid buys
/// the first with the second. Swept end to end, both halves measured in every
/// cell (depth as muted band 20's interior supremum, wobble as the worst peak
/// frequency excursion over the same 6->5 octave drag the gate uses):
///
///     q (bins)    0    1/16   1/8    1/4    3/8    1/2    5/8    3/4      1
///     depth   -81.3  -81.1  -81.5  -82.3  -83.0  -81.7  -86.0  -86.4  -89.2 dB
///     wobble   0.78   2.29   4.19   8.02  10.43  14.07  17.05  20.55  23.92 cents
///
/// THERE IS NO KNEE, and the frontier is concave the wrong way. Wobble is
/// nearly linear in the quantum from the very first step, while depth is flat
/// to within the statistic's own ripple until 5/8 of a bin and only arrives in
/// full at a whole one. Every cell that buys 3 dB or more costs 17 cents or
/// more -- two thirds of the 23 cents sub-bin placement was introduced to
/// remove, for a third of the 8 dB it cost. The only quantum that keeps the
/// drag gate green is a sixteenth of a bin, and it reads 0.15 dB SHALLOWER
/// than exact placement while tripling the wobble: strictly worse on both
/// axes. Exact placement is the Pareto point.
///
/// Zero, therefore, and it is a measured choice rather than an untried default.
constexpr double kTrackingEdgeQuantumBins = 0.0;

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
                kTrackingTransitionWidthBins,
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
    int width_bins,
    double magnitude_floor) noexcept {
    return shape_tracking_transitions(magnitudes, band_edges_hz, bin_width_hz,
                                      width_bins, magnitude_floor,
                                      kTransitionOutsidePct,
                                      kTrackingEdgeQuantumBins);
}

TrackingTransitionGeometry shape_tracking_transitions(
    std::span<double> magnitudes,
    std::span<const float> band_edges_hz,
    double bin_width_hz,
    int width_bins,
    double magnitude_floor,
    int outside_pct,
    double edge_quantum_bins) noexcept {
    TrackingTransitionGeometry geometry{};

    const auto num_bins  = static_cast<std::ptrdiff_t>(magnitudes.size());
    const auto num_edges = static_cast<std::ptrdiff_t>(band_edges_hz.size());
    constexpr std::ptrdiff_t kMaximumEdges =
        static_cast<std::ptrdiff_t>(pulp::signal::kSpectralBandMaskMaximumBands) + 1;
    if (num_bins < 3 || num_edges < 2 || num_edges > kMaximumEdges
        || !(bin_width_hz > 0.0) || width_bins <= 0
        || !(magnitude_floor > 0.0) || outside_pct < 0 || outside_pct > 100
        || !(edge_quantum_bins >= 0.0))
        return geometry;

    // Edge frequencies onto the design grid, clamped into the array, and kept
    // FRACTIONAL. A non-finite or out-of-range edge lands on a valid position
    // rather than indexing out of bounds; a degenerate layout then simply
    // shapes nothing, because every clamp below collapses to zero width.
    //
    // Sub-bin placement is load-bearing, and it is the only thing separating
    // this from a staircase. Rounding an edge to a whole bin makes the shaped
    // magnitude a step function OF THE VIEWPORT: a drag that moves an edge by
    // less than 5.86 Hz changes nothing at all, then changes by a whole bin at
    // once. Magnitude alone would forgive that -- one bin either way is
    // inaudible. The minimum-phase reconstruction does not: it derives phase
    // from the whole log-magnitude curve, so a one-bin magnitude step moves
    // phase ACROSS THE SPECTRUM, and a continuous drag therefore emits a
    // sequence of global phase jumps, audible as pitch wobble. Carrying the
    // fraction makes the same drag a glide, and costs nothing else: the same
    // edge loop, the same smoothstep, the same clamps.
    //
    // It composes with the asymmetric placement below because the two are
    // about different things. The fraction decides WHERE the boundary is; the
    // placement decides which side of it pays for the ramp. A fractional
    // boundary is simply placed asymmetrically about.
    //
    // TWO positions are kept per edge, and the difference matters. `edge_bin`
    // is where the transition is PLACED, snapped to the caller's quantum.
    // `exact_bin` is where the drawn step actually IS, and it is never snapped,
    // because the magnitude array was compiled from the exact edge: the bin the
    // compiler assigned to the upper band is a fact about the table, not about
    // this function's geometry. Reading the step from a snapped position asks
    // the table a question about a boundary it does not have, and the answer is
    // silently wrong in one direction only -- a snapped edge that lands just
    // INSIDE the upper band finds the same value either side of it, concludes
    // there is no step, and leaves the drawn edge unshaped. Measured: at a
    // quantum of 3/8 bin the default field left one of muted band 20's two
    // edges unshaped and the band read its unshaped -34.6 dB, and the same
    // desync cost a kept band a third of its width at a quantum of a
    // SIXTEENTH of a bin. At zero quantum the two positions are identical, so
    // this costs the shipping design nothing.
    std::array<double, static_cast<std::size_t>(kMaximumEdges)> edge_bin{};
    std::array<double, static_cast<std::size_t>(kMaximumEdges)> exact_bin{};
    for (std::ptrdiff_t e = 0; e < num_edges; ++e) {
        const double hz = static_cast<double>(band_edges_hz[static_cast<std::size_t>(e)]);
        const double exact = std::isfinite(hz) ? hz / bin_width_hz : 0.0;
        double bin = exact;
        // Snapped BEFORE the clamp, so a quantum never pushes an edge out of
        // the array. Zero is the exact position; `1.0` is the whole-bin
        // placement this design replaced.
        //
        // The snap is checked rather than trusted, for the same reason the edge
        // frequency above is: an infinite quantum, or one small enough that
        // `bin / q` overflows, produces a NON-FINITE position, and a non-finite
        // position survives the clamp below (neither comparison holds against a
        // NaN) to reach `ceil` and a cast, which is undefined. A quantum that
        // cannot be honoured leaves the edge exact, which is the same answer
        // this function gives every other input it cannot use.
        if (edge_quantum_bins > 0.0) {
            const double snapped =
                std::round(bin / edge_quantum_bins) * edge_quantum_bins;
            if (std::isfinite(snapped)) bin = snapped;
        }
        edge_bin[static_cast<std::size_t>(e)] =
            std::clamp(bin, 0.0, static_cast<double>(num_bins - 1));
        exact_bin[static_cast<std::size_t>(e)] =
            std::clamp(exact, 0.0, static_cast<double>(num_bins - 1));
    }
    // An out-of-range or non-finite edge can land out of order once clamped; a
    // non-monotonic table would give a negative span below, so make it
    // monotonic and let the zero-width clamp drop the degenerate edges.
    for (std::ptrdiff_t e = 1; e < num_edges; ++e) {
        edge_bin[static_cast<std::size_t>(e)] = std::max(
            edge_bin[static_cast<std::size_t>(e)],
            edge_bin[static_cast<std::size_t>(e - 1)]);
        exact_bin[static_cast<std::size_t>(e)] = std::max(
            exact_bin[static_cast<std::size_t>(e)],
            exact_bin[static_cast<std::size_t>(e - 1)]);
    }

    // Two passes. Every plateau is read from the UNSHAPED magnitude before
    // anything is written, so the result cannot depend on the order edges are
    // visited even where one band carries a transition at each of its ends.
    struct Shaping {
        double lo        = 0.0;   ///< fractional position the ramp starts at
        double hi        = 0.0;   ///< fractional position it ends at
        bool   rightward = false; ///< true when the UPPER band is the quieter one
        double kept_lin  = 0.0;   ///< the louder plateau, in linear magnitude
        double quiet_lin = 0.0;   ///< the quieter plateau
    };
    std::array<Shaping, static_cast<std::size_t>(kMaximumEdges)> shaping{};
    std::ptrdiff_t shaped = 0;

    for (std::ptrdiff_t e = 0; e < num_edges; ++e) {
        ++geometry.edges_considered;
        const double centre = edge_bin[static_cast<std::size_t>(e)];

        // Which bins the drawn step sits between. `first_above` is the first
        // bin belonging to the upper band, so the bin below it is the last of
        // the lower one.
        //
        // CEIL, and deliberately no search for the step. Placing a transition
        // on the wrong side of the drawn step by one bin would take a bin off
        // the band the user kept -- the defect this placement exists to remove,
        // returned at an eighth the size and far harder to see -- so this has
        // to be right rather than approximately right. A ROUNDED edge could not
        // guarantee it, because it names the nearest bin, which sits either
        // side; the compiler assigns a bin to the upper band exactly when the
        // bin's own frequency reaches the edge, so `ceil` of the exact
        // fractional position names that first upper bin by construction.
        //
        // Searching outward for the nearest change instead -- which is what a
        // rounded position needs -- is actively wrong here. At the field's
        // lowest edge with the bottom band attenuated, the table holds the same
        // value on both sides (that band's gain runs down to DC), so a search
        // walks up to the NEXT edge and reports a step belonging to a different
        // boundary, while the geometry below still refers to this one. It then
        // reads the whole DC side as the attenuated band's room, clears the
        // floor, and ramps into the 0.8-bin bottom band that has no room for a
        // transition at all. Equal plateaus mean no step; that is the answer,
        // not a reason to go looking elsewhere.
        const auto first_above = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(
                std::ceil(exact_bin[static_cast<std::size_t>(e)])),
            1, num_bins - 1);

        const double below = magnitudes[static_cast<std::size_t>(first_above - 1)];
        const double above = magnitudes[static_cast<std::size_t>(first_above)];
        // No step, nothing to shape. Two bands drawn at the same gain share a
        // boundary the reconstruction never sees, so putting a ramp there would
        // carve a notch into flat ground -- which is what makes two ADJACENT
        // mutes cost nothing extra at the edge they share.
        if (below == above) continue;

        // The ramp descends into the QUIETER side, so the louder side keeps
        // every bin the user drew.
        const bool rightward = above < below;

        // The room either side, measured between fractional edge positions and
        // running to the ends of the array at the field's outer edges.
        const double lower_room = centre
            - (e > 0 ? edge_bin[static_cast<std::size_t>(e - 1)] : 0.0);
        const double upper_room =
            (e + 1 < num_edges ? edge_bin[static_cast<std::size_t>(e + 1)]
                               : static_cast<double>(num_bins))
            - centre;
        const double quiet_room = rightward ? upper_room : lower_room;
        const double loud_room  = rightward ? lower_room : upper_room;

        // HALF the quieter band, never more. Each of a band's two ends may
        // carry a transition inward, so half is what keeps them from meeting --
        // and it leaves at least half of every muted band at full depth no
        // matter which of its ends are shaped.
        const double width = std::min(static_cast<double>(width_bins),
                                      quiet_room * 0.5);

        // A transition needs a whole bin of the quieter band to be a transition
        // at all: below that it rewrites a single bin, which is a step in a new
        // place rather than a ramp. The integer grid used to supply this floor
        // as a side effect of truncation, and it is load-bearing -- without it
        // the lowest band of the default field (0.8 bins wide) gets a
        // transition it has no room to hold and comes back several dB
        // shallower. Stated outright now that neither the centre nor the width
        // rounds.
        if (!(width >= 1.0)) continue;   // no room: this edge stays the drawn step

        // The part of the ramp permitted outside the quieter band, clamped the
        // same way as the part inside it so a transition can never reach past
        // the far edge of either band it touches.
        const double outside =
            std::min(width * static_cast<double>(outside_pct) / 100.0,
                     loud_room * 0.5);

        auto& sh = shaping[static_cast<std::size_t>(shaped++)];
        sh.rightward = rightward;
        sh.lo        = centre - (rightward ? outside : width);
        sh.hi        = centre + (rightward ? width   : outside);
        sh.kept_lin  = std::max(rightward ? below : above, magnitude_floor);
        sh.quiet_lin = std::max(rightward ? above : below, magnitude_floor);

        ++geometry.edges_shaped;
        geometry.widest_width = std::max(geometry.widest_width, width);
        geometry.narrowest_width =
            geometry.edges_shaped == 1
                ? width
                : std::min(geometry.narrowest_width, width);
    }

    for (std::ptrdiff_t i = 0; i < shaped; ++i) {
        const auto& sh = shaping[static_cast<std::size_t>(i)];
        const double span = sh.hi - sh.lo;
        const double kept_log  = std::log(sh.kept_lin);
        const double quiet_log = std::log(sh.quiet_lin);
        const auto lowest = std::max<std::ptrdiff_t>(
            0, static_cast<std::ptrdiff_t>(std::ceil(sh.lo)));
        const auto highest = std::min<std::ptrdiff_t>(
            num_bins - 1, static_cast<std::ptrdiff_t>(std::floor(sh.hi)));
        for (std::ptrdiff_t bin = lowest; bin <= highest; ++bin) {
            // Clamped because the shoulders fall BETWEEN bins: the first and
            // last bin inside the transition sit just inside it, so t is near 0
            // and near 1 rather than exactly at them. That is precisely what
            // makes the shape glide -- as the boundary slides by a fraction of
            // a bin, every written value moves by a fraction of a step, and a
            // bin entering or leaving the span does so at the plateau value it
            // already held.
            const double t =
                std::clamp((static_cast<double>(bin) - sh.lo) / span, 0.0, 1.0);
            // t runs low-bin to high-bin; x runs LOUD to QUIET. They are the
            // same axis when the upper band is the quieter one and opposite
            // when it is not, which is the whole of the asymmetry at write
            // time.
            const double x = sh.rightward ? t : 1.0 - t;
            // Written as the plateau's own value at either extreme rather than
            // through exp(log(v)), which is not required to round-trip. A bin
            // that sits exactly on a shoulder must come back BIT-identical to
            // the level it was drawn at, or "the louder side keeps its bins"
            // becomes a statement about floating point.
            if (x <= 0.0) {
                magnitudes[static_cast<std::size_t>(bin)] = sh.kept_lin;
                continue;
            }
            if (x >= 1.0) {
                magnitudes[static_cast<std::size_t>(bin)] = sh.quiet_lin;
                continue;
            }
            // Cubic smoothstep: continuous in value AND slope at both ends, so
            // the shaping does not put a fresh first-derivative break where it
            // just removed a step. The choice is deliberately not load-bearing
            // — across linear through 7th-order smoothstep the realised depth
            // moves by about 3 dB, against the tens of dB the WIDTH is worth —
            // so this is the simplest curve with that continuity, not a tuned
            // one.
            const double shape = x * x * (3.0 - 2.0 * x);
            magnitudes[static_cast<std::size_t>(bin)] =
                std::exp(kept_log + (quiet_log - kept_log) * shape);
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
