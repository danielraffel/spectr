#pragma once

/// @file mask_renderer.hpp
/// The one contract between Spectr's band field and the audio it produces.
///
/// A renderer turns a compiled `SpectralMaskTable` — a magnitude on a
/// frequency grid — into audio. Two realisations of the same drawn magnitude
/// ship: a linear-phase one (spectral multiply through a WOLA frame engine)
/// and a near-zero-latency one (minimum-phase FIR through partitioned
/// convolution). Which one is live is a saved, prepare-time mode.
///
/// The contract deliberately exposes **no partition geometry, no engine type
/// and no thread identity**. Presets, host automation, snapshots, morph and
/// the internal LFOs all speak in layouts and tables, so they never learn
/// which realisation is running and cannot come to mean different things in
/// different modes.
///
/// `latency_samples()` is a function of the configured mode alone. It is not
/// derived from the host block size, the machine, or any runtime measurement,
/// so a project recalls with the same delay compensation everywhere.

#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>
#include "spectr/upstream/spectral_mask_processor.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

namespace spectr {

/// The two shipped realisations of a drawn magnitude.
enum class MaskRenderMode {
    /// Spectral multiply through a windowed overlap-add frame engine. The
    /// drawn magnitude is realised with symmetric (pre and post) smear and a
    /// latency of one analysis frame plus one hop.
    linear_phase = 0,
    /// Minimum-phase FIR through partitioned convolution. The drawn magnitude
    /// is realised with no pre-smear and a fixed, small latency.
    zero_latency = 1,
};

/// Geometry and mixing a renderer is prepared against.
///
/// `design_grid_size` is the renderer's own parameter, not the analysis FFT
/// size and not either of the framework's fixed capacities. A renderer checks
/// that its configured grid fits the geometry it needs and otherwise fails to
/// prepare; raising the grid later is a configuration change, not a rewrite.
struct MaskRendererConfig {
    int    design_grid_size  = 8192;   ///< FFT points the magnitude is sampled on.
    int    analysis_hop      = 2048;   ///< Linear-phase only; ignored otherwise.
    int    channels          = 2;
    int    max_block         = 512;
    double sample_rate       = 48000.0;
    float  initial_mix       = 1.0f;
    int    mix_ramp_samples  = 64;
};

/// Table in, audio out.
class MaskRenderer {
public:
    using Layout = pulp::signal::SpectralBandLayout;
    using Table  = pulp::signal::SpectralMaskTable;

    virtual ~MaskRenderer() = default;

    using EffectiveFrameObserver = void (*)(void*, const Table&, std::uint64_t) noexcept;
    // Experimental capture, installed while stopped. Only the linear renderer
    // built with the shared fixture supports it; the table is callback-borrowed.
    virtual bool set_effective_frame_observer(void*, EffectiveFrameObserver) noexcept {
        return false;
    }

    /// Build a complete replacement state. Allocates. Control thread only.
    /// A failed prepare leaves the renderer unprepared; it never half-applies.
    [[nodiscard]] virtual bool prepare(const MaskRendererConfig& config) = 0;

    [[nodiscard]] virtual bool prepared() const noexcept = 0;

    /// Delay from input to output, in samples. Fixed for the life of a
    /// prepare and determined by the mode, never by the host or the machine.
    [[nodiscard]] virtual int latency_samples() const noexcept = 0;

    /// Conservative flush bound: how long after the last input sample the
    /// renderer can still emit non-zero output.
    [[nodiscard]] virtual int maximum_tail_samples() const noexcept = 0;

    /// FFT points the magnitude this renderer realises is sampled on.
    ///
    /// The product's resolution disclosure ("how many of the drawn bands can
    /// this geometry actually distinguish?") has to be computed on the grid
    /// the LIVE renderer designs against, not on a constant. Both shipped
    /// realisations sample the same grid today, so a constant happens to give
    /// the right answer -- but only by coincidence, and the disclosure would
    /// start lying the moment a realisation changed its grid. Asking the
    /// renderer makes it follow instead.
    [[nodiscard]] virtual int design_grid_size() const noexcept = 0;

    /// The linear magnitude this realisation would apply for @p layout, per
    /// bin of the design grid (design_grid_size() / 2 + 1 values into @p out),
    /// computed exactly as its own design step computes it -- the compiled
    /// table, plus whatever edge shaping the realisation adds. Auto Gain v2
    /// weighs THIS against the material's spectrum, so its make-up follows
    /// the response the listener actually hears rather than the drawn steps.
    ///
    /// Pure with respect to the renderer's state: no allocation, no lock, no
    /// clock; @p scratch is the caller's (a table is too large for an audio
    /// thread's stack). Returns false, writing nothing, if the layout does not
    /// compile or @p out is too short. The default is the table itself, which
    /// is what a realisation that applies the table per STFT bin realises.
    /// > 0 when this realisation reconstructs a MINIMUM-PHASE impulse from the
    /// realised magnitude, with this magnitude floor; 0 when its wet leg is
    /// zero-phase against the latency-aligned dry leg. Auto Gain v2 needs the
    /// phase only below 100 % Mix, where wet and dry interfere.
    [[nodiscard]] virtual double minimum_phase_floor() const noexcept { return 0.0; }

    [[nodiscard]] virtual bool realised_magnitude(const Layout& layout, double sample_rate,
                                                  Table& scratch,
                                                  std::span<double> out) const noexcept {
        const int grid = design_grid_size();
        if (grid <= 0 || out.size() < static_cast<std::size_t>(grid / 2 + 1)) return false;
        if (!pulp::signal::build_spectral_mask(layout, grid, static_cast<float>(sample_rate),
                                               scratch))
            return false;
        for (int bin = 0; bin < scratch.num_bins; ++bin)
            out[static_cast<std::size_t>(bin)] =
                static_cast<double>(scratch.gain_linear[static_cast<std::size_t>(bin)]);
        return true;
    }

    /// Publish one layout from the control thread. May allocate.
    [[nodiscard]] virtual bool publish_layout(const Layout& layout) = 0;

    /// Requests are ordered by when they were ASKED for, not by which design
    /// finishes first. A staged-layout handoff and every publish take a
    /// request ordinal; a design whose ordinal is older than the last one
    /// staged is dropped as superseded. Without this a parameter sync and an
    /// audio-thread layout asked for in the same block race to be adopted,
    /// and the winner depends on the scheduler.
    ///
    /// Reserve an ordinal now for a publish that happens later (the param
    /// sync reserves at spawn, on the audio thread). Lock-free. Zero means
    /// this realisation does not order requests.
    [[nodiscard]] virtual std::uint64_t reserve_request_ordinal() noexcept { return 0; }

    /// `publish_layout` at a reserved ordinal. Sets `*superseded` when a newer
    /// request had already been staged, in which case nothing was staged.
    [[nodiscard]] virtual bool publish_layout_at(const Layout& layout,
                                                 std::uint64_t /*ordinal*/,
                                                 bool* superseded) {
        if (superseded) *superseded = false;
        return publish_layout(layout);
    }

    /// Stage the latest layout from the single audio owner. Allocation-free
    /// and lock-free; the work the layout implies is done elsewhere and
    /// adopted at a boundary this renderer chooses.
    [[nodiscard]] virtual bool set_layout_rt(const Layout& layout) noexcept = 0;

    virtual void set_mix(float mix) noexcept = 0;

    /// A time-domain source for the WET path only: each block, `process()`
    /// hands the source the live input and realises the mask over what the
    /// source writes, while the dry leg of the mix stays the live input. This
    /// is how a freeze replaces what the mask shapes without owning a
    /// renderer, so the same held source serves either realisation and
    /// survives a switch between them.
    ///
    /// Non-owning. Install while the audio thread cannot be inside this
    /// renderer (before it is published, or from the audio thread itself);
    /// the source must outlive its installation. Returns false when this
    /// realisation cannot take a source, in which case nothing is installed.
    using WetSource = pulp::signal::SpectralWetSourceStageT<float>;
    [[nodiscard]] virtual bool set_wet_source(WetSource* /*source*/) noexcept {
        return false;
    }

    /// Process one prepared planar block. `num_samples` may be any length up
    /// to the prepared `max_block`; the caller never learns the renderer's
    /// internal block granularity.
    [[nodiscard]] virtual bool process(const float* const* input,
                                       float* const* output,
                                       int num_samples) noexcept = 0;

    /// Clear streaming state, preserving the currently adopted magnitude.
    virtual void reset() noexcept = 0;

    /// OFFLINE RENDERS ONLY. Block the calling audio thread until every layout
    /// already handed to the design worker is realised and waiting for the
    /// next `process()` to adopt.
    ///
    /// A realisation that designs on a worker adopts a staged layout at the
    /// first render block after that worker finishes. A real-time host paces
    /// its callbacks, so the worker finishes between them; an offline render
    /// does not wait, so on a loaded machine the adoption lands a
    /// load-dependent number of blocks late and the worker's Latest lane
    /// coalesces intermediate layouts away. Calling this at each host block
    /// of an offline render reproduces the paced schedule exactly. Never call
    /// it on a real-time block: it may sleep. Returns false when the wait gave
    /// up (worker stopped, or `deadline` passed). The caller owns the budget:
    /// the processor gives every host block one, so a worker that is starved
    /// -- or a host whose offline flag outlived its bounce -- costs a bounded
    /// wait rather than a stalled callback. Realisations that adopt
    /// synchronously have nothing to wait for.
    virtual bool await_staged_designs(
        std::chrono::steady_clock::time_point /*deadline*/) noexcept {
        return true;
    }

    /// Audio thread, once per host block before process(). True when the host
    /// renders this block offline: a realisation whose output is computed on a
    /// worker may then wait, bounded, for that output instead of substituting
    /// its stand-in. False for every realtime block, which never waits.
    virtual void set_offline_block(bool /*offline*/) noexcept {}

    /// Audio thread. While true, `process()` keeps a staged layout until
    /// `flush_design_handoff()` instead of
    /// handing it to the design worker at once. The processor defers for the
    /// whole of a host block and flushes at its end, so a design can never
    /// finish -- on a call the scheduler preempted -- between two render
    /// blocks of the host block that staged it. Every layout is then adopted
    /// at a host-block boundary, the same one in a paced real-time render and
    /// in an offline bounce, rather than wherever the worker happened to land.
    virtual void defer_design_handoff(bool /*defer*/) noexcept {}

    /// Audio thread, lock-free: hand a deferred staged layout to the worker.
    virtual void flush_design_handoff() noexcept {}

    /// Audio thread, lock-free: the audio path drove the mask this block
    /// (it staged a layout, or left its last one live on purpose). The next
    /// `flush_design_handoff()` then counts as a request even with nothing
    /// staged, so a control-thread publish asked for earlier in the block --
    /// a parameter sync's base mask -- is superseded instead of replacing the
    /// layout the audio path believes is live.
    virtual void claim_mask_this_block() noexcept {}

    /// Monotonic counter of the magnitude the renderer is currently
    /// realising. Advances when a newly published or staged layout has been
    /// adopted into the audio path. Diagnostic: a renderer whose adoption is
    /// staged through a worker may report the incoming generation up to one
    /// adoption boundary early while a burst of layouts is in flight.
    [[nodiscard]] virtual unsigned long long active_generation() const noexcept = 0;
};

/// Summary of the transition geometry one shaping pass actually realised.
///
/// The requested width is a ceiling, never a promise: an edge whose quieter
/// band is narrow gets less, and an edge with no step at all gets none.
/// Reporting what was realised is what makes "did this band get a transition at
/// all?" answerable without re-deriving the clamp rule at the call site.
struct TrackingTransitionGeometry {
    int edges_considered = 0;  ///< Edges examined.
    int edges_shaped     = 0;  ///< Edges given a transition at least a bin wide.
    /// Realised transition widths, measured INSIDE the quieter band, in bins,
    /// and FRACTIONAL: an edge sits between bins, so the width its clamps leave
    /// it is a fraction too. Reporting a rounded integer here would hide
    /// exactly the sub-bin detail the placement exists to carry.
    double narrowest_width = 0.0; ///< Smallest realised width.
    double widest_width    = 0.0; ///< Largest realised width.
};

/// Shape a transition into every drawn band edge of a compiled magnitude,
/// placing the whole of it INSIDE the quieter of the two bands that meet there.
///
/// The zero-latency realisation reconstructs a causal impulse by taking the
/// LOG of this magnitude and returning through a transform of the design
/// grid's own size. A drawn band edge is a step, and the cepstrum of a step
/// decays too slowly to fit in that many points: the part that does not fit
/// wraps, and the reconstructed magnitude comes back with the null partly
/// filled in. It is an aliasing error, not a truncation one -- the full
/// impulse is retained -- so raising the tap count does not fix it, and
/// measurement puts it at about 11 dB against the tens of dB below.
///
/// Interpolating across the step in the log domain -- the domain the
/// reconstruction actually reads -- is what makes the cepstrum decay fast
/// enough to fit, at an unchanged tap count, an unchanged latency and an
/// unchanged render cost.
///
/// WHERE the interpolation is placed decides who pays for it, and that is not
/// a free choice. Spread symmetrically about the edge, a transition subtracts
/// from the LOUDER band as much as from the quieter one, so a band a user drew
/// and asked to keep comes back narrower than they drew it -- by a fixed span
/// in Hz, which is most of a narrow band and little of a wide one. Placed
/// almost entirely inside the quieter band, the louder side holds its drawn
/// level to within a quarter of a transition of the boundary, and nearly all
/// the cost lands on the band that was being attenuated anyway.
///
/// That cost is real and is the trade this placement buys: a muted band's
/// full-depth region is shortened by the transition at each of its ends, so a
/// mute is shallower near its own edges than in its middle. It is bounded by
/// the clamp below and it is what the depth gates measure.
///
/// `width_bins` is a CEILING. Each edge's transition is clamped to HALF the
/// room available in its quieter band, so the transitions entering a band from
/// its two ends can meet but never overlap, and at least half of every
/// attenuated band keeps its full drawn depth. The floor on "no room" is one
/// whole bin inside the quieter band: anything narrower rewrites a single bin,
/// which moves a step rather than removing one, so a band with less than that
/// to give keeps the drawn step unchanged. Narrow bands therefore degrade
/// continuously back to the unshaped design rather than trading depth for a
/// smear.
///
/// An edge whose two sides were drawn at the same gain carries no step and is
/// left alone, so two adjacent bands muted together cost nothing at the
/// boundary they share.
///
/// Edges are placed at their exact FRACTIONAL position on the design grid, not
/// rounded to a whole bin. Under a continuous viewport drag a rounded edge
/// holds still and then jumps, and because the realisation is minimum phase
/// each whole-bin magnitude jump moves phase across the entire spectrum — a
/// staircase in phase, audible as pitch wobble. A fractional boundary makes the
/// same drag a glide, and it is orthogonal to the placement above: the fraction
/// decides WHERE the boundary is, the placement decides which side of it pays.
/// The linear-phase mode has no such term: its phase is identically zero
/// however its edges are placed, which is why only this realisation's own
/// shaping step needs to carry the fraction.
///
/// How coarse that fraction may be is a swept axis rather than an assumption:
/// a grid step buys realised depth in the muted band and charges pitch wobble
/// under a drag, and the whole frontier -- both halves measured in every cell
/// -- is printed at `kTrackingEdgeQuantumBins` in the implementation. It has no
/// knee: exact placement is the Pareto point, so this ships with no grid at
/// all. The explicit form below takes the grid as an argument, which is how
/// that was measured and how the drag gate's negative control is built.
///
/// Shaping happens in the log domain against `magnitude_floor`, which must be
/// the same floor the reconstruction is given: a transition that ran to a
/// different floor than the one the reconstruction applies would put a second
/// step back exactly where this removed one.
///
/// Pure, allocation-free and independent of any renderer state, so the shaped
/// magnitude can be measured directly instead of only inferred from audio.
/// Design-side only; it is not part of the `MaskRenderer` contract and says
/// nothing about how a realisation applies the result.
TrackingTransitionGeometry shape_tracking_transitions(
    std::span<double> magnitudes,
    std::span<const float> band_edges_hz,
    double bin_width_hz,
    int width_bins,
    double magnitude_floor) noexcept;

/// The same shaping with every geometry axis stated explicitly instead of taken
/// from the shipping constants. `outside_pct` is the percentage of each
/// transition allowed to sit outside the quieter band; `edge_quantum_bins` is
/// the grid each edge's fractional position is snapped to, in design bins, with
/// zero meaning the exact position. The five-argument form above is exactly
/// this one called with the shipping values of both.
///
/// It exists so a sweep of the geometry measures THE SHIPPING FUNCTION at every
/// point rather than a re-implementation of it. A sweep whose rows come from a
/// copy is a model, and a model agrees with the product exactly where it was
/// written to agree; the only way to know a row is true of the product is for
/// the product to have produced it.
///
/// `edge_quantum_bins == 1.0` is the whole-bin placement this design replaced,
/// so the negative control that proves the drag gate can see a staircase is
/// this function at that argument rather than a rounding step written beside
/// it. A control implemented in the test can only ever demonstrate that the
/// test's own arithmetic moves the gate.
TrackingTransitionGeometry shape_tracking_transitions(
    std::span<double> magnitudes,
    std::span<const float> band_edges_hz,
    double bin_width_hz,
    int width_bins,
    double magnitude_floor,
    int outside_pct,
    double edge_quantum_bins) noexcept;

/// The zero-latency (Tracking) renderer's fixed block, which is also the whole
/// of its reported latency. Named here so a caller outside the renderer -- a
/// built-artifact test reading what a host is told -- derives the figure
/// instead of retyping it.
inline constexpr int kZeroLatencyRenderBlock = 64;

/// Latency of a mode, before anything is prepared.
///
/// The one function every caller — the processor, its tests, and the host
/// report — uses to answer "how much delay does this mode cost?". It takes a
/// geometry and a mode and nothing else: no device, no block size, no
/// measurement. This is what makes a saved project recall identically on a
/// different machine.
[[nodiscard]] int mask_render_latency_samples(MaskRenderMode mode,
                                              const MaskRendererConfig& config) noexcept;

/// Construct the renderer for a mode. Returns null only on an unknown mode.
[[nodiscard]] std::unique_ptr<MaskRenderer> make_mask_renderer(MaskRenderMode mode);

} // namespace spectr

/// Layouts staged from an audio thread that no design worker has finished
/// with yet, summed over every zero-latency renderer in the process.
///
/// The zero-latency realisation designs on a worker and adopts at its next
/// render block, so how many blocks a staged layout trails the audio that
/// asked for it depends on how soon that worker is scheduled. A real-time host
/// paces the audio callback, so the worker keeps up; a consumer that renders
/// blocks back to back (an offline harness) outruns it, and on a loaded machine
/// it can outrun it by a different number of blocks each run -- the worker's
/// Latest lane then coalesces the intermediate layouts away, so a ramp arrives
/// late and in coarser steps. A harness waits for this to read zero after each
/// block to render as a paced host would hear it.
///
/// Zero means every staged layout's impulse is staged for adoption at the next
/// render block. Read-only and lock-free; exported from the AU bundle so an
/// in-process host can reach it. It is a process-wide total, so it is only
/// exact while one renderer is being driven.
extern "C" std::uint64_t spectr_mask_design_backlog_v1() noexcept;
