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

    /// Publish one layout from the control thread. May allocate.
    [[nodiscard]] virtual bool publish_layout(const Layout& layout) = 0;

    /// Stage the latest layout from the single audio owner. Allocation-free
    /// and lock-free; the work the layout implies is done elsewhere and
    /// adopted at a boundary this renderer chooses.
    [[nodiscard]] virtual bool set_layout_rt(const Layout& layout) noexcept = 0;

    virtual void set_mix(float mix) noexcept = 0;

    /// Process one prepared planar block. `num_samples` may be any length up
    /// to the prepared `max_block`; the caller never learns the renderer's
    /// internal block granularity.
    [[nodiscard]] virtual bool process(const float* const* input,
                                       float* const* output,
                                       int num_samples) noexcept = 0;

    /// Clear streaming state, preserving the currently adopted magnitude.
    virtual void reset() noexcept = 0;

    /// Monotonic counter of the magnitude the renderer is currently
    /// realising. Advances when a newly published or staged layout has been
    /// adopted into the audio path. Diagnostic: a renderer whose adoption is
    /// staged through a worker may report the incoming generation up to one
    /// adoption boundary early while a burst of layouts is in flight.
    [[nodiscard]] virtual unsigned long long active_generation() const noexcept = 0;
};

/// Summary of the transition geometry one shaping pass actually realised.
///
/// The requested half-width is a ceiling, never a promise: an edge whose
/// neighbour is close, or which sits near DC or Nyquist, gets less. Reporting
/// what was realised is what makes "did this band get a transition at all?"
/// answerable without re-deriving the clamp rule at the call site.
struct TrackingTransitionGeometry {
    int edges_considered    = 0;  ///< Edges examined.
    int edges_shaped        = 0;  ///< Edges given a transition at least a bin wide.
    /// Realised half-widths, in bins, and FRACTIONAL: an edge sits between
    /// bins, so the width its clamps leave it is a fraction too. Reporting a
    /// rounded integer here would hide exactly the sub-bin detail the placement
    /// exists to carry.
    double narrowest_half_width = 0.0; ///< Smallest realised half-width.
    double widest_half_width    = 0.0; ///< Largest realised half-width.
};

/// Shape a transition into every drawn band edge of a compiled magnitude.
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
/// `half_width_bins` is a CEILING. Each edge's transition is clamped to half
/// the distance to its neighbouring edges and to the distance to the ends of
/// the array, so transitions can touch but never overlap, and an edge with no
/// room is left exactly as it was. A band too narrow to hold a transition
/// therefore degrades continuously back to the unshaped step rather than
/// trading its depth for a smear. The floor on "no room" is one whole bin
/// either side of the edge: anything narrower rewrites a single bin, which
/// moves a step rather than removing one.
///
/// Edges are placed at their exact FRACTIONAL position on the design grid, not
/// rounded to a whole bin. Under a continuous viewport drag a rounded edge
/// holds still and then jumps, and because the realisation is minimum phase
/// each whole-bin magnitude jump moves phase across the entire spectrum — a
/// staircase in phase, audible as pitch wobble. A fractional centre makes the
/// same drag a glide. The linear-phase mode has no such term: its phase is
/// identically zero however its edges are placed, which is why only this
/// realisation's own shaping step needs to carry the fraction.
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
    int half_width_bins,
    double magnitude_floor) noexcept;

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
