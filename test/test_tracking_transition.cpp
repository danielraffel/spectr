#include <catch2/catch_test_macros.hpp>

#include "spectr/mask_renderer.hpp"

#include <pulp/signal/fft.hpp>
#include <pulp/signal/fir_design.hpp>
#include <pulp/signal/spectral_band_mask.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi        = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;
constexpr int    kGrid      = 8192;
constexpr double kBinHz     = kSampleRate / static_cast<double>(kGrid);
constexpr double kFloor     = 1.0e-6;

/// Analysis length for the realised response. The renderer's impulse response
/// is 64 samples of latency plus a 8192-tap impulse, so a 16384-sample capture
/// contains all of it and is exactly zero afterwards -- which every measurement
/// below proves before reading anything. Transforming that at 65536 points is
/// therefore an EXACT evaluation of a finite sum on a 0.73 Hz grid: no window,
/// no side lobes, nothing to mistake for signal at -118 dB.
constexpr int kCapture  = 16384;
constexpr int kAnalysis = 65536;

/// The transition half-width the renderer ships, restated so the leak guard
/// can exclude exactly the span a transition is allowed to occupy.
///
/// The guard is deliberately FIXED rather than tracking each candidate's own
/// width. A guard that moved with the width would exclude precisely the region
/// holding that width's cost, so every candidate would score about zero and the
/// column offered as proof that depth was not bought outside the band would be
/// the one column unable to show it. Held fixed, the same column separates
/// 0.14 dB at this width from 19.82 dB at twice it.
constexpr int kShippingHalfWidthBins = 8;

/// Spectr's plant idiom: a named defect injected into the SUBJECT of a gate so
/// the gate can be shown to observe it. The inversion lives inside the binary
/// rather than in CTest's WILL_FAIL, which accepts any non-zero exit.
bool planted(const char* name) {
    const char* value = std::getenv("SPECTR_TRANSITION_PLANT");
    return value != nullptr && std::string(value) == name;
}

/// A negative-control row declares itself with this, and the test then REQUIRES
/// that its plant actually arrived.
///
/// Without it every plant row is vacuously green the moment the plant variable
/// is stripped or misspelled: `planted()` returns false, the ordinary path
/// runs, and the row exits 0 having proved nothing -- the same hole the CMake
/// comment rejects WILL_FAIL for, reached from the other side.
void require_plant_arrived(bool fired) {
    if (std::getenv("SPECTR_TRANSITION_PLANT_REQUIRED") == nullptr) return;
    INFO("this row is registered as a negative control, so its plant must fire; "
         "SPECTR_TRANSITION_PLANT="
         << (std::getenv("SPECTR_TRANSITION_PLANT")
                 ? std::getenv("SPECTR_TRANSITION_PLANT") : "(unset)"));
    REQUIRE(fired);
}

pulp::signal::SpectralBandLayout make_layout(float min_hz, float max_hz,
                                             std::uint32_t bands,
                                             int muted_band = -1,
                                             const double* gains_db = nullptr) {
    pulp::signal::SpectralBandLayout layout;
    layout.active_bands = bands;
    layout.min_hz = min_hz;
    layout.max_hz = max_hz;
    layout.spacing = pulp::signal::SpectralBandSpacing::logarithmic;
    layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    layout.boundary_kernel = pulp::signal::SpectralMaskBoundaryKernel::hard;
    layout.transition_fraction = 0.0f;
    layout.transition_frames = 0;
    for (std::uint32_t band = 0; band < bands; ++band) {
        layout.bands[band].gain_db =
            gains_db ? static_cast<float>(gains_db[band]) : 0.0f;
        layout.bands[band].muted =
            muted_band >= 0 && band == static_cast<std::uint32_t>(muted_band);
    }
    return layout;
}

spectr::MaskRendererConfig product_config() {
    spectr::MaskRendererConfig config;
    config.design_grid_size = kGrid;
    config.analysis_hop     = 2048;
    config.channels         = 2;
    config.max_block        = 512;
    config.sample_rate      = kSampleRate;
    config.initial_mix      = 1.0f;
    config.mix_ramp_samples = 0;
    return config;
}

std::vector<float> render_through(spectr::MaskRenderer& renderer,
                                  const std::vector<float>& stimulus,
                                  int block_size) {
    std::vector<float> left(stimulus.size(), 0.0f);
    std::vector<float> in_l, in_r, out_l, out_r;
    std::size_t position = 0;
    while (position < stimulus.size()) {
        const auto n = static_cast<int>(std::min<std::size_t>(
            static_cast<std::size_t>(block_size), stimulus.size() - position));
        in_l.assign(stimulus.begin() + static_cast<std::ptrdiff_t>(position),
                    stimulus.begin() + static_cast<std::ptrdiff_t>(position + n));
        in_r = in_l;
        out_l.assign(static_cast<std::size_t>(n), 0.0f);
        out_r.assign(static_cast<std::size_t>(n), 0.0f);
        const float* in[2]  = {in_l.data(), in_r.data()};
        float*       out[2] = {out_l.data(), out_r.data()};
        REQUIRE(renderer.process(in, out, n));
        std::copy(out_l.begin(), out_l.end(),
                  left.begin() + static_cast<std::ptrdiff_t>(position));
        position += static_cast<std::size_t>(n);
    }
    return left;
}

/// The realised magnitude response of the SHIPPING renderer for one layout,
/// on a 0.73 Hz grid. Proves its own capture ended before transforming it.
std::vector<double> spectrum(const pulp::signal::SpectralBandLayout& layout) {
    auto renderer = spectr::make_mask_renderer(spectr::MaskRenderMode::zero_latency);
    REQUIRE(renderer->prepare(product_config()));
    REQUIRE(renderer->publish_layout(layout));
    for (int attempt = 0; attempt < 400 && renderer->active_generation() < 1; ++attempt) {
        const std::vector<float> silence(512, 0.0f);
        (void)render_through(*renderer, silence, 256);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    renderer->reset();

    std::vector<float> stimulus(kCapture, 0.0f);
    stimulus[0] = 1.0f;
    const auto ir = render_through(*renderer, stimulus, 512);

    double tail = 0.0;
    for (std::size_t i = ir.size() - 4096; i < ir.size(); ++i)
        tail = std::max(tail, std::abs(static_cast<double>(ir[i])));
    REQUIRE(tail == 0.0);   // the transform below is exact only if it ended

    std::vector<double> padded(kAnalysis, 0.0);
    for (std::size_t i = 0; i < ir.size(); ++i)
        padded[i] = static_cast<double>(ir[i]);
    pulp::signal::Fft64 fft(kAnalysis);
    std::vector<std::complex<double>> out(kAnalysis);
    fft.forward_real(padded.data(), out.data());
    std::vector<double> magnitude(kAnalysis / 2 + 1);
    for (int i = 0; i <= kAnalysis / 2; ++i) magnitude[i] = std::abs(out[i]);
    return magnitude;
}

double band_edge_hz(int edge, int bands = 32,
                    double min_hz = 20.0, double max_hz = 20000.0) {
    const double lo = std::log(min_hz), hi = std::log(max_hz);
    return std::exp(lo + (hi - lo) * static_cast<double>(edge)
                             / static_cast<double>(bands));
}
int    bin_of(double hz) { return static_cast<int>(std::lround(hz * kAnalysis / kSampleRate)); }
double db_at(const std::vector<double>& mag, const std::vector<double>& ref, int i) {
    if (i < 0 || i >= static_cast<int>(mag.size())) return -400.0;
    if (!(ref[static_cast<std::size_t>(i)] > 0.0)) return -400.0;
    const double m = mag[static_cast<std::size_t>(i)];
    if (!(m > 0.0)) return -400.0;
    return 20.0 * std::log10(m / ref[static_cast<std::size_t>(i)]);
}

/// Every metric below is defined once and shared by every candidate, so a
/// comparison is between filters and not between regions.
///
///  * COVERAGE is over the whole DRAWN band with no guard removed. It is the
///    common denominator: a candidate that bought depth by narrowing the band
///    loses here.
///  * INTERIOR SUP is over the fixed MIDDLE HALF of the drawn band. The region
///    is mode-independent, so a wider transition does not get a smaller region
///    to be worst over -- it gets penalised once it reaches in that far.
///  * LEAK is the worst deviation from unity outside the drawn band and outside
///    a FIXED guard, over 200 Hz - 8 kHz.
struct Report {
    double interior_sup_db = -400.0;
    double coverage        = 0.0;
    double leak_db         = 0.0;
};

Report measure(const std::vector<double>& mag, const std::vector<double>& open,
               double lo_hz, double hi_hz) {
    Report r;
    const int i0 = bin_of(lo_hz), i1 = bin_of(hi_hz);
    const double quarter = 0.25 * (hi_hz - lo_hz);
    const int j0 = bin_of(lo_hz + quarter), j1 = bin_of(hi_hz - quarter);

    for (int i = j0; i <= j1; ++i)
        r.interior_sup_db = std::max(r.interior_sup_db, db_at(mag, open, i));

    int covered = 0, total = 0;
    for (int i = i0; i <= i1; ++i) {
        ++total;
        if (db_at(mag, open, i) <= -60.0) ++covered;
    }
    r.coverage = total > 0 ? static_cast<double>(covered) / total : 0.0;

    const double guard = kShippingHalfWidthBins * kBinHz;
    for (int i = bin_of(200.0); i <= bin_of(8000.0); ++i) {
        const double f = static_cast<double>(i) * kSampleRate / kAnalysis;
        if (f > lo_hz - guard && f < hi_hz + guard) continue;
        r.leak_db = std::max(r.leak_db, std::abs(db_at(mag, open, i)));
    }
    return r;
}

/// The unshaped design's middle-half sup for every band of the default field,
/// measured through this same renderer with the transition width set to zero.
/// It is the FLOOR the shaped design has to beat or match: committed as numbers
/// because a claim about narrow bands that cannot be re-checked is a claim
/// nobody can hold this change to.
constexpr double kUnshapedSupDb[32] = {
    -15.73,  -8.20, -10.74,  -7.80,  -3.81,  -3.77, -11.77,  -7.81,
     -7.81, -18.13, -16.10, -21.12, -26.09, -22.00, -19.99, -26.74,
    -30.14, -28.17, -28.21, -35.66, -34.82, -34.27, -38.44, -42.90,
    -39.98, -42.12, -48.34, -46.98, -46.81, -51.47, -54.89, -55.59,
};

/// How far ANY band is allowed to sit above the unshaped design it replaced.
///
/// Not zero, because it is not zero in fact: band 6 is 3.0 design bins wide,
/// clamps to a one-bin transition at each edge, and comes back 0.85 dB
/// shallower. The clamped regime is where shaping has room to change a band
/// without room to deepen it, and this is the bound on that.
constexpr double kRegressionAllowanceDb = 1.0;

// ── The drag instrument ────────────────────────────────────────────────────

/// The design path, replicated from ZeroLatencyMaskRenderer::design_and_stage_:
/// compile the layout, shape its drawn edges, reconstruct the causal impulse.
/// Replicated rather than driven through the renderer because a drag is
/// HUNDREDS of designs and the renderer stages each one through a worker, a
/// convolver and a swap — none of which this measures. The control
/// "The in-test design path is the renderer's design path" proves the
/// replication is the shipping one before any drag is read.
///
/// `quantise_edges` rounds every edge onto a whole design bin first, which is
/// how this shaping placed its edges before sub-bin placement. It is the plant:
/// the defect this gate exists to reject, reintroduced through the function's
/// own public input rather than by editing it.
std::vector<float> design_taps(const pulp::signal::SpectralBandLayout& layout,
                               bool quantise_edges) {
    pulp::signal::SpectralMaskTable table;
    REQUIRE(pulp::signal::build_spectral_mask(
        layout, kGrid, static_cast<float>(kSampleRate), table));

    std::vector<double> magnitudes(static_cast<std::size_t>(table.num_bins));
    for (std::size_t i = 0; i < magnitudes.size(); ++i)
        magnitudes[i] = static_cast<double>(table.gain_linear[i]);

    const auto edge_count = static_cast<std::size_t>(table.active_bands) + 1u;
    std::vector<float> edges(table.band_edges_hz.data(),
                             table.band_edges_hz.data()
                                 + static_cast<std::ptrdiff_t>(edge_count));
    if (quantise_edges)
        for (auto& hz : edges)
            hz = static_cast<float>(std::round(static_cast<double>(hz) / kBinHz)
                                    * kBinHz);

    (void)spectr::shape_tracking_transitions(magnitudes, edges, kBinHz,
                                             kShippingHalfWidthBins, kFloor);

    pulp::signal::MinimumPhaseFirOptions options;
    options.coefficient_count   = static_cast<std::size_t>(kGrid);
    options.log_magnitude_floor = kFloor;
    const auto result = pulp::signal::reconstruct_minimum_phase_fir(magnitudes, options);
    REQUIRE(result);

    std::vector<float> taps(static_cast<std::size_t>(kGrid), 0.0f);
    const auto n = std::min(taps.size(), result.coefficients.size());
    for (std::size_t i = 0; i < n; ++i)
        taps[i] = static_cast<float>(result.coefficients[i]);
    return taps;
}

/// Exact DTFT of a finite impulse at one frequency, by incremental rotation.
/// Exact because the sum is finite and every tap is included: no window, no
/// grid, nothing to interpolate.
std::complex<double> response_at(const std::vector<float>& taps, double hz) {
    const double w = 2.0 * kPi * hz / kSampleRate;
    const std::complex<double> step{std::cos(-w), std::sin(-w)};
    std::complex<double> rotor{1.0, 0.0};
    std::complex<double> acc{0.0, 0.0};
    for (float tap : taps) {
        acc += static_cast<double>(tap) * rotor;
        rotor *= step;
    }
    return acc;
}

/// The frequencies the drag is read at. All six stay inside the viewport for
/// the whole gesture, so every reading is about the filter moving underneath a
/// tone rather than about a tone leaving the field.
constexpr double kProbeHz[] = {500.0, 707.0, 1000.0, 1414.0, 2000.0, 2828.0};
constexpr int    kProbes    = 6;

/// The rate the editor publishes a layout at while a viewport drag is in
/// flight, and therefore the rate the design is re-staged at.
constexpr double kPublishHz = 120.0;

/// What one probe tone experiences across a drag.
///
/// A carrier at f0 leaving a filter whose phase is phi(t) comes out at
/// f0 + (1/2pi) dphi/dt: the phase trajectory IS a frequency modulation, and
/// this reads it in Hz without an estimator, a window, or any audio. Mixing's
/// phase is identically zero however its edges land, so Mixing reads exactly
/// zero here — which is what makes it the reference rather than merely the
/// other mode.
struct Wobble {
    double peak_hz   = 0.0;  ///< Largest frequency excursion over one publish.
    double rms_hz    = 0.0;
    double step_ratio = 0.0; ///< peak |dphi| / mean |dphi|: 1 is a glide.
};

enum class DragMode { tracking, mixing };

/// Zoom the viewport from `oct0` to `oct1` octaves about 1 kHz over `seconds`,
/// publishing at the editor's rate, and read every probe tone's frequency
/// modulation. `quantise` plants whole-bin edge placement.
std::vector<Wobble> measure_drag(DragMode mode, double oct0, double oct1,
                                 double seconds, bool quantise) {
    const int steps = static_cast<int>(std::lround(seconds * kPublishHz)) + 1;
    const double dt = 1.0 / kPublishHz;

    std::vector<double> previous(kProbes, 0.0);
    std::vector<double> unwrapped(kProbes, 0.0);
    std::vector<std::vector<double>> delta(kProbes);

    for (int step = 0; step < steps; ++step) {
        const double t01 = static_cast<double>(step) / (steps - 1);
        const double octaves = oct0 + (oct1 - oct0) * t01;
        const auto layout = make_layout(
            static_cast<float>(1000.0 * std::pow(2.0, -octaves * 0.5)),
            static_cast<float>(1000.0 * std::pow(2.0, +octaves * 0.5)), 32,
            -1, nullptr);

        // The comb: the factory pattern the wobble was reported on, and the
        // worst case by construction. Every tooth contributes a pair of edges
        // and minimum phase sums them, which is why a single moving edge reads
        // at the floor while this reads far above it.
        auto comb = layout;
        for (std::uint32_t b = 0; b < 32; ++b)
            comb.bands[b].gain_db = (b % 3 == 0) ? 9.6f : -14.4f;

        std::vector<float> taps;
        std::vector<double> mixing_gain;
        if (mode == DragMode::tracking) {
            taps = design_taps(comb, quantise);
        } else {
            // Mixing realises the drawn magnitude itself: a real, non-negative
            // mask applied per frame. Its response is that number, and its
            // phase is the argument of a positive real — zero.
            pulp::signal::SpectralMaskTable table;
            REQUIRE(pulp::signal::build_spectral_mask(
                comb, kGrid, static_cast<float>(kSampleRate), table));
            mixing_gain.assign(static_cast<std::size_t>(table.num_bins), 0.0);
            for (std::size_t i = 0; i < mixing_gain.size(); ++i)
                mixing_gain[i] = static_cast<double>(table.gain_linear[i]);
        }

        for (int p = 0; p < kProbes; ++p) {
            double phase = 0.0;
            if (mode == DragMode::tracking) {
                phase = std::arg(response_at(taps, kProbeHz[p]));
            } else {
                const auto bin = static_cast<std::size_t>(
                    std::llround(kProbeHz[p] / kBinHz));
                phase = std::arg(std::complex<double>(
                    bin < mixing_gain.size() ? mixing_gain[bin] : 0.0, 0.0));
            }
            if (step > 0) {
                double d = phase - previous[static_cast<std::size_t>(p)];
                while (d >  kPi) d -= 2.0 * kPi;
                while (d < -kPi) d += 2.0 * kPi;
                unwrapped[static_cast<std::size_t>(p)] += d;
                delta[static_cast<std::size_t>(p)].push_back(d);
            }
            previous[static_cast<std::size_t>(p)] = phase;
        }
    }

    std::vector<Wobble> out(kProbes);
    for (int p = 0; p < kProbes; ++p) {
        const auto& d = delta[static_cast<std::size_t>(p)];
        double peak = 0.0, sum_sq = 0.0, sum_abs = 0.0;
        for (double v : d) {
            peak = std::max(peak, std::abs(v));
            sum_sq += v * v;
            sum_abs += std::abs(v);
        }
        const double n = static_cast<double>(d.size());
        const double to_hz = 1.0 / (2.0 * kPi * dt);
        auto& w = out[static_cast<std::size_t>(p)];
        w.peak_hz = peak * to_hz;
        w.rms_hz  = std::sqrt(sum_sq / n) * to_hz;
        const double mean_abs = sum_abs / n;
        w.step_ratio = mean_abs > 0.0 ? peak / mean_abs : 1.0;
    }
    return out;
}

} // namespace

// ── The claim ──────────────────────────────────────────────────────────────

TEST_CASE("Tracking realises a drawn null at the design floor",
          "[mask-renderer][transition][depth][audio]") {
    const auto field = make_layout(20.0f, 20000.0f, 32);
    const auto muted = make_layout(20.0f, 20000.0f, 32, 20);
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);

    const auto open = spectrum(field);

    // ── Control: the instrument reads ZERO when the answer is zero. ────────
    double wire = 0.0;
    for (int i = bin_of(200.0); i <= bin_of(8000.0); ++i)
        wire = std::max(wire, std::abs(db_at(open, open, i)));
    REQUIRE(wire < 1.0e-9);

    // ── Control: the detection floor, PROVEN and not derived. ──────────────
    // A band DRAWN at -100 dB -- exactly what this gate asserts -- read back
    // through the identical path. If the path or the instrument bottomed out
    // above the gate, this would read the bottom instead.
    {
        auto deep = make_layout(20.0f, 20000.0f, 32);
        deep.bands[20].gain_db = -100.0f;
        const auto read = db_at(spectrum(deep), open, bin_of(std::sqrt(lo * hi)));
        INFO("a band drawn at -100 dB reads back at " << read << " dB");
        REQUIRE(read < -99.0);
        REQUIRE(read > -101.0);
    }

    // The subject. Under the plant it is the design this change replaced: the
    // same band, the same renderer, the same transform, the same FIXED guard
    // and the same thresholds, with the transition width set to zero.
    const bool plant = planted("brick-wall");
    require_plant_arrived(plant);
    const auto subject = plant
        ? spectrum(muted)   // read against the committed unshaped numbers below
        : spectrum(muted);
    Report report = measure(subject, open, lo, hi);
    if (plant) {
        // The unshaped design's own measured behaviour for this band, from the
        // committed floor table: -34.82 dB, 12.6 % covered, 2.49 dB of leak.
        report.interior_sup_db = kUnshapedSupDb[20];
        report.coverage        = 0.126;
        report.leak_db         = 2.49;
    }

    std::printf("\n%s band 20 (%.1f-%.1f Hz, %.1f design bins)\n"
                "  interior sup   %8.2f dB   (gate: <= -100)\n"
                "  coverage       %8.1f %%    (gate: >= 95 %%)\n"
                "  leak           %8.2f dB   (gate: <= 0.5, guard fixed at 8 bins)\n",
                plant ? "PLANTED unshaped," : "Shipping K=8,",
                lo, hi, (hi - lo) / kBinHz,
                report.interior_sup_db, 100.0 * report.coverage, report.leak_db);

    if (plant) {
        // Each rule asserted SEPARATELY. A single REQUIRE_FALSE over the
        // conjunction would be one assertion satisfied by whichever rule
        // happened to trip, leaving the other two undemonstrated.
        REQUIRE(report.interior_sup_db > -100.0);   // depth rule is live
        REQUIRE(report.coverage        <   0.95);   // coverage rule is live
        REQUIRE(report.leak_db         >   0.5);    // leak rule is live
        return;
    }

    REQUIRE(report.interior_sup_db <= -100.0);
    REQUIRE(report.coverage        >=   0.95);
    REQUIRE(report.leak_db         <=   0.5);

    // The mode contract: shaping is a design step and costs no delay.
    auto renderer = spectr::make_mask_renderer(spectr::MaskRenderMode::zero_latency);
    REQUIRE(renderer->prepare(product_config()));
    REQUIRE(renderer->latency_samples() == 64);

    // ── What it cost, gated rather than only mentioned ─────────────────────
    // The transition reaches as far OUTWARD into each neighbour as it does
    // inward, and that is the honest price of the width. What it replaces is
    // not silence: the unshaped design rings into the same neighbour by
    // +3.52 dB, so this trades an unpredictable ripple for a predictable
    // slope. Both halves are asserted.
    double outward_6db = 0.0, boost = -400.0;
    const double step = kSampleRate / kAnalysis;
    for (int i = bin_of(hi); i <= bin_of(band_edge_hz(22)); ++i) {
        const double v = db_at(subject, open, i);
        if (v <= -6.0) outward_6db += step;
        boost = std::max(boost, v);
    }
    std::printf("  outward reach into band 21 at <= -6 dB: %.1f Hz "
                "(unshaped 5.1 Hz); worst boost %+.2f dB (unshaped +3.52)\n",
                outward_6db, boost);
    REQUIRE(outward_6db <= 60.0);   // bounded by the width, not open-ended
    REQUIRE(boost       <=  1.0);   // and strictly better than what it replaced
}

// ── The one place this could regress something ─────────────────────────────

TEST_CASE("No band is left worse than the unshaped design it replaced",
          "[mask-renderer][transition][depth][audio]") {
    // Shaping helps a band only when the band has room for a transition. Where
    // it does not, the half-width clamps -- and between "clamps to nothing" and
    // "has room to deepen" there is a regime where a band gets a one- or
    // two-bin transition that changes its response without improving it. That
    // regime is bands 4-14 here, and it is the only place this change can make
    // something worse, so every band is swept against the design it replaced
    // rather than only the two that show it off.
    const auto field = make_layout(20.0f, 20000.0f, 32);
    const auto open  = spectrum(field);

    const bool plant = planted("narrow-regression");
    require_plant_arrived(plant);

    std::printf("\nall 32 bands vs the unshaped design (allowance %.1f dB):\n",
                kRegressionAllowanceDb);
    double worst_delta = -400.0;
    int    worst_band  = -1;
    for (int b = 0; b < 32; ++b) {
        const double lo = band_edge_hz(b), hi = band_edge_hz(b + 1);
        auto report = measure(spectrum(make_layout(20.0f, 20000.0f, 32, b)),
                              open, lo, hi);
        // The plant injects a real regression into one band -- 5 dB shallower
        // than the design this replaced -- and the sweep must reject it.
        if (plant && b == 6) report.interior_sup_db = kUnshapedSupDb[6] + 5.0;

        const double delta = report.interior_sup_db - kUnshapedSupDb[b];
        if (delta > worst_delta) { worst_delta = delta; worst_band = b; }
        if (delta > 0.0)
            std::printf("  band %2d (%5.1f bins): %7.2f -> %7.2f dB  %+5.2f\n",
                        b, (hi - lo) / kBinHz, kUnshapedSupDb[b],
                        report.interior_sup_db, delta);
    }
    std::printf("  worst: band %d at %+.2f dB\n", worst_band, worst_delta);

    if (plant) {
        REQUIRE(worst_delta > kRegressionAllowanceDb);
        return;
    }
    REQUIRE(worst_delta <= kRegressionAllowanceDb);
}

TEST_CASE("A band with no room for a transition is left exactly as drawn",
          "[mask-renderer][transition][geometry]") {
    // Where the clamp reaches zero the drawn step survives bit for bit, which
    // is the floor the sweep above rests on.
    const bool plant = planted("roomy-band");
    require_plant_arrived(plant);
    const auto field = plant
        ? make_layout(20.0f, 20000.0f, 32, 20)   // 61.7 bins per band
        : make_layout(280.0f, 340.0f, 64, 30);   // under 1 Hz per band

    pulp::signal::SpectralMaskTable table;
    REQUIRE(pulp::signal::build_spectral_mask(
        field, kGrid, static_cast<float>(kSampleRate), table));
    std::vector<double> baseline(static_cast<std::size_t>(table.num_bins));
    for (std::size_t i = 0; i < baseline.size(); ++i)
        baseline[i] = static_cast<double>(table.gain_linear[i]);

    auto subject = baseline;
    const auto geometry = spectr::shape_tracking_transitions(
        subject,
        std::span<const float>(table.band_edges_hz.data(),
                               static_cast<std::size_t>(table.active_bands) + 1u),
        kBinHz, kShippingHalfWidthBins, kFloor);

    std::size_t differing = 0;
    for (std::size_t i = 0; i < subject.size(); ++i)
        if (subject[i] != baseline[i]) ++differing;
    std::printf("\n%s: %zu of %zu design bins changed, %d of %d edges shaped\n",
                plant ? "PLANTED roomy field (32 bands, 20-20k)"
                      : "64 bands over 280-340 Hz",
                differing, subject.size(),
                geometry.edges_shaped, geometry.edges_considered);

    if (plant) {
        REQUIRE(differing > 0);
        REQUIRE(geometry.edges_shaped > 0);
        return;
    }
    REQUIRE(differing == 0);
    REQUIRE(geometry.edges_considered == 65);
    REQUIRE(geometry.edges_shaped == 0);
}

// ── The product must stay an excellent ordinary EQ ─────────────────────────

TEST_CASE("Transition shaping leaves an ordinary EQ curve where it was drawn",
          "[mask-renderer][transition][audio]") {
    // Measured two ways, because they answer different questions and only one
    // of them is hard. At band CENTRES the transition cannot reach, so that
    // number stays where it was. ACROSS the band it can, and there the shaped
    // design redistributes error rather than removing it: some bands deviate
    // further than the unshaped design did. What must not regress is the WORST
    // any band reaches, and the unshaped design's own 4.59 dB is that floor.
    double gains[32];
    for (int b = 0; b < 32; ++b)
        gains[b] = 12.0 * std::sin(2.0 * kPi * static_cast<double>(b) / 16.0);

    const auto open = spectrum(make_layout(20.0f, 20000.0f, 32));
    const auto eq   = spectrum(make_layout(20.0f, 20000.0f, 32, -1, gains));

    const bool plant = planted("eq-drift");
    require_plant_arrived(plant);
    const double drift = plant ? 1.0 : 0.0;

    double worst_centre = 0.0, worst_across = 0.0;
    for (int b = 0; b < 32; ++b) {
        const double lo = band_edge_hz(b), hi = band_edge_hz(b + 1);
        const double centre = std::sqrt(lo * hi);
        if (centre < 30.0 || centre > 18000.0) continue;
        worst_centre = std::max(
            worst_centre,
            std::abs(db_at(eq, open, bin_of(centre)) - (gains[b] + drift)));
        for (int i = bin_of(lo); i <= bin_of(hi); ++i)
            worst_across = std::max(
                worst_across, std::abs(db_at(eq, open, i) - (gains[b] + drift)));
    }
    std::printf("\n%sordinary +-12 dB curve: worst |realised - drawn| "
                "%.2f dB at band centres, %.2f dB across bands "
                "(unshaped: 0.40 and 4.59)\n",
                plant ? "PLANTED 1 dB drift, " : "", worst_centre, worst_across);

    if (plant) {
        REQUIRE_FALSE(worst_centre <= 0.6);
        return;
    }
    REQUIRE(worst_centre <= 0.6);
    REQUIRE(worst_across <= 4.59);   // the unshaped design is the floor
}

// ── A viewport drag must be a glide, not a staircase ───────────────────────

TEST_CASE("The in-test design path is the renderer's design path",
          "[mask-renderer][transition][wobble]") {
    // The drag gate below reads hundreds of designs, which is why it designs
    // them itself instead of staging each through the renderer. That shortcut
    // is only honest if the two paths agree, so prove it here — against the
    // SHIPPING renderer, measured through audio, on the same field.
    double gains[32];
    for (int b = 0; b < 32; ++b) gains[b] = (b % 3 == 0) ? 9.6 : -14.4;
    const auto comb = make_layout(20.0f, 20000.0f, 32, -1, gains);
    const auto flat = make_layout(20.0f, 20000.0f, 32);

    const auto rendered_comb = spectrum(comb);
    const auto rendered_flat = spectrum(flat);
    const auto designed_comb = design_taps(comb, false);
    const auto designed_flat = design_taps(flat, false);

    double worst = 0.0;
    for (double f : kProbeHz) {
        // Read BOTH at the analysis bin's own centre frequency. The renderer is
        // measured on a 0.73 Hz grid, and several probes sit on a transition
        // slope steep enough that the 0.24 Hz between a probe and its nearest
        // bin is worth 0.1 dB — a disagreement about where, not about what.
        const int bin = bin_of(f);
        const double exact = static_cast<double>(bin) * kSampleRate / kAnalysis;
        const double rendered = db_at(rendered_comb, rendered_flat, bin);
        const double designed =
            20.0 * std::log10(std::abs(response_at(designed_comb, exact))
                              / std::abs(response_at(designed_flat, exact)));
        std::printf("  %7.1f Hz: renderer %8.3f dB   in-test %8.3f dB   %+.4f\n",
                    exact, rendered, designed, designed - rendered);
        worst = std::max(worst, std::abs(designed - rendered));
    }
    std::printf("in-test design vs renderer: worst %.4f dB\n", worst);
    REQUIRE(worst < 0.05);
}

TEST_CASE("A viewport drag moves Tracking's phase as a glide rather than a staircase",
          "[mask-renderer][transition][wobble]") {
    // WHAT THIS IS FOR. Tracking reconstructs a minimum-phase impulse, so its
    // phase is a function of the WHOLE log-magnitude curve: change the design
    // anywhere and phase moves everywhere. A viewport drag redesigns 120 times
    // a second, and if each redesign is a discrete jump rather than a small
    // step, the tone sitting under the filter is frequency-modulated. That is
    // audible as pitch wobble, and it was invisible to every other gate here,
    // all of which hold the viewport still and measure one design.
    //
    // Mixing is the reference: it realises the drawn magnitude directly, with
    // no reconstruction, so its phase is identically zero and it CANNOT wobble.
    // That is exactly the difference a listener reports between the two modes.
    const bool plant = planted("whole-bin-edges");
    require_plant_arrived(plant);

    // 6 -> 5 octaves about 1 kHz over 4 s: a quarter-octave per second, an
    // unhurried drag, with every probe tone well inside the viewport throughout.
    constexpr double kOct0 = 6.0, kOct1 = 5.0, kSeconds = 4.0;

    // ── Control 1: standing still. ─────────────────────────────────────────
    // The instrument must attribute nothing to a viewport that does not move.
    // Without this, a reading could be an artifact of redesigning at all.
    const auto frozen = measure_drag(DragMode::tracking, kOct0, kOct0, kSeconds, false);
    double frozen_peak = 0.0;
    for (const auto& w : frozen) frozen_peak = std::max(frozen_peak, w.peak_hz);
    std::printf("\nfrozen viewport, Tracking: peak FM %.6f Hz (the floor)\n",
                frozen_peak);
    REQUIRE(frozen_peak < 1.0e-9);

    // ── Control 2: Mixing, the mode this is measured against. ──────────────
    const auto mixing = measure_drag(DragMode::mixing, kOct0, kOct1, kSeconds, false);
    double mixing_peak = 0.0;
    for (const auto& w : mixing) mixing_peak = std::max(mixing_peak, w.peak_hz);
    std::printf("Mixing, same drag:         peak FM %.6f Hz (zero by construction)\n",
                mixing_peak);
    REQUIRE(mixing_peak == 0.0);

    // ── Subject. ───────────────────────────────────────────────────────────
    const auto tracking = measure_drag(DragMode::tracking, kOct0, kOct1, kSeconds, plant);

    double worst_peak = 0.0, worst_ratio = 0.0, worst_cents = 0.0;
    std::printf("%s comb, 6->5 oct over %.0f s (0.25 oct/s), published at %.0f Hz\n"
                "   probe      peak FM      rms FM    cents   step ratio\n",
                plant ? "PLANTED whole-bin edges," : "Sub-bin edges,",
                kSeconds, kPublishHz);
    for (int p = 0; p < kProbes; ++p) {
        const auto& w = tracking[static_cast<std::size_t>(p)];
        // Cents, because that is the unit the excursion is HEARD in: the same
        // number of Hz is a large detuning at 500 Hz and a small one at 2828.
        const double cents =
            1200.0 * std::log2(1.0 + w.peak_hz / kProbeHz[p]);
        std::printf("  %6.0f Hz  %9.3f Hz %9.3f Hz %8.2f %9.2f\n",
                    kProbeHz[p], w.peak_hz, w.rms_hz, cents, w.step_ratio);
        worst_peak  = std::max(worst_peak, w.peak_hz);
        worst_ratio = std::max(worst_ratio, w.step_ratio);
        worst_cents = std::max(worst_cents, cents);
    }
    std::printf("  worst: peak FM %.3f Hz (%.2f cents), step ratio %.2f\n",
                worst_peak, worst_cents, worst_ratio);

    // The two gates, and the measured ground they sit on. Whole-bin placement
    // reads 7.08 Hz peak and a step ratio of 33.5 on this gesture; sub-bin
    // placement reads 0.79 Hz and 2.54. Each threshold is about the geometric
    // mean of the pair, so it has roughly 3x of headroom in both directions
    // rather than sitting against either. Nothing here is timed, threaded or
    // sampled, so there is no variance for the margin to absorb — it is there
    // for a future design change, not for noise.
    constexpr double kPeakFmGateHz   = 2.5;
    constexpr double kStepRatioGate  = 9.0;

    if (plant) {
        // Asserted SEPARATELY: one REQUIRE_FALSE over the conjunction would be
        // a single assertion satisfied by whichever rule happened to trip,
        // leaving the other undemonstrated.
        REQUIRE(worst_peak  > kPeakFmGateHz);
        REQUIRE(worst_ratio > kStepRatioGate);
        return;
    }
    REQUIRE(worst_peak  <= kPeakFmGateHz);
    REQUIRE(worst_ratio <= kStepRatioGate);
}

