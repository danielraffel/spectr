// The two geometry axes of the tracking transition, swept together.
//
// WHY THIS EXISTS. `kTrackingTransitionWidthBins` and `kTransitionOutsidePct`
// are the only two numbers that decide how a drawn band edge is realised, and
// each was chosen against a base the other has since moved. The placement axis
// was swept alone and the width axis has never been swept at all, so "width
// buys depth back without giving up kept width" was a hypothesis with nothing
// under it. This measures the product across both at once.
//
// HIDDEN. Every case here is tagged `[.]`, so `catch_discover_tests` does not
// register it and no CI lane runs it. It is an instrument, not a gate: it
// prints a table and asserts only the things that make the table readable.
// The gates it informs live in test_tracking_transition.cpp.

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
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kGrid       = 8192;
constexpr double kBinHz      = kSampleRate / static_cast<double>(kGrid);
constexpr double kFloor      = 1.0e-6;
constexpr int    kCapture    = 16384;
constexpr int    kAnalysis   = 65536;

/// The leak guard is held FIXED at the shipping width for every candidate, for
/// the reason the gate file states: a guard that moved with the candidate would
/// exclude exactly the region holding that candidate's cost, so every row would
/// score about zero and the column offered as proof would be the one column
/// unable to show anything.
constexpr int kShippingWidthBins = 8;

pulp::signal::SpectralBandLayout make_layout(float min_hz, float max_hz,
                                             std::uint32_t bands,
                                             int muted_band = -1) {
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
        layout.bands[band].gain_db = 0.0f;
        layout.bands[band].muted =
            muted_band >= 0 && band == static_cast<std::uint32_t>(muted_band);
    }
    return layout;
}

pulp::signal::SpectralBandLayout make_island(float min_hz, float max_hz,
                                             std::uint32_t bands,
                                             std::uint32_t kept) {
    auto layout = make_layout(min_hz, max_hz, bands);
    for (std::uint32_t b = 0; b < bands; ++b) layout.bands[b].muted = (b != kept);
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

// ── The two ways to read the realised response ─────────────────────────────

/// Through the SHIPPING RENDERER: publish the layout, capture its impulse
/// response through the convolver, transform it. Exactly what the gate file
/// does, and the only reading that is the product by construction. It is also
/// far too slow to sweep with, and it can only ever read the compiled-in
/// constants.
std::vector<double> spectrum_via_renderer(
    const pulp::signal::SpectralBandLayout& layout) {
    auto renderer = spectr::make_mask_renderer(spectr::MaskRenderMode::zero_latency);
    REQUIRE(renderer->prepare(product_config()));
    REQUIRE(renderer->publish_layout(layout));
    for (int attempt = 0; attempt < 400 && renderer->active_generation() < 1; ++attempt) {
        const std::vector<float> silence(512, 0.0f);
        std::vector<float> l(512, 0.0f), r(512, 0.0f);
        const float* in[2]  = {silence.data(), silence.data()};
        float*       out[2] = {l.data(), r.data()};
        (void)renderer->process(in, out, 256);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    renderer->reset();

    std::vector<float> ir(kCapture, 0.0f);
    {
        std::vector<float> in_l, in_r, out_l, out_r;
        std::vector<float> stimulus(kCapture, 0.0f);
        stimulus[0] = 1.0f;
        std::size_t position = 0;
        while (position < stimulus.size()) {
            const auto n = static_cast<int>(
                std::min<std::size_t>(512, stimulus.size() - position));
            in_l.assign(stimulus.begin() + static_cast<std::ptrdiff_t>(position),
                        stimulus.begin() + static_cast<std::ptrdiff_t>(position + n));
            in_r = in_l;
            out_l.assign(static_cast<std::size_t>(n), 0.0f);
            out_r.assign(static_cast<std::size_t>(n), 0.0f);
            const float* in[2]  = {in_l.data(), in_r.data()};
            float*       out[2] = {out_l.data(), out_r.data()};
            REQUIRE(renderer->process(in, out, n));
            std::copy(out_l.begin(), out_l.end(),
                      ir.begin() + static_cast<std::ptrdiff_t>(position));
            position += static_cast<std::size_t>(n);
        }
    }
    double tail = 0.0;
    for (std::size_t i = ir.size() - 4096; i < ir.size(); ++i)
        tail = std::max(tail, std::abs(static_cast<double>(ir[i])));
    REQUIRE(tail == 0.0);

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

/// Through the DESIGN PATH ONLY, at a chosen (width, placement): compile the
/// mask table, shape it with the SHIPPING function, reconstruct, and transform
/// the float32 taps the renderer would have staged. It skips only the convolver
/// -- which applies the taps and does not change them -- and the case
/// "the design path is the renderer's" below proves that skip costs nothing at
/// the depths this sweep reads.
/// Exact edge placement -- the grid the product ships, and the value every row
/// this file published was measured at. Named rather than written as a bare 0.0
/// so those rows keep reproducing whatever the shipping quantum becomes.
constexpr double kExactEdges = 0.0;

std::vector<double> spectrum_via_design(
    const pulp::signal::SpectralBandLayout& layout, int width_bins,
    int outside_pct, double edge_quantum_bins = kExactEdges) {
    pulp::signal::SpectralMaskTable table;
    REQUIRE(pulp::signal::build_spectral_mask(
        layout, kGrid, static_cast<float>(kSampleRate), table));

    std::vector<double> magnitudes(static_cast<std::size_t>(table.num_bins));
    for (std::size_t i = 0; i < magnitudes.size(); ++i)
        magnitudes[i] = static_cast<double>(table.gain_linear[i]);

    const auto edge_count = static_cast<std::size_t>(table.active_bands) + 1u;
    const std::span<const float> edges(table.band_edges_hz.data(), edge_count);

    (void)spectr::shape_tracking_transitions(magnitudes, edges, kBinHz,
                                             width_bins, kFloor, outside_pct,
                                             edge_quantum_bins);

    pulp::signal::MinimumPhaseFirOptions options;
    options.coefficient_count   = static_cast<std::size_t>(kGrid);
    options.log_magnitude_floor = kFloor;
    const auto result = pulp::signal::reconstruct_minimum_phase_fir(magnitudes, options);
    REQUIRE(result);

    // float32, because that is the precision the renderer stages. Reading the
    // double coefficients here would flatter every deep null by measuring an
    // impulse the product never convolves with.
    std::vector<double> padded(kAnalysis, 0.0);
    const auto n = std::min<std::size_t>(static_cast<std::size_t>(kGrid),
                                         result.coefficients.size());
    for (std::size_t i = 0; i < n; ++i)
        padded[i] = static_cast<double>(static_cast<float>(result.coefficients[i]));

    pulp::signal::Fft64 fft(kAnalysis);
    std::vector<std::complex<double>> out(kAnalysis);
    fft.forward_real(padded.data(), out.data());
    std::vector<double> magnitude(kAnalysis / 2 + 1);
    for (int i = 0; i <= kAnalysis / 2; ++i) magnitude[i] = std::abs(out[i]);
    return magnitude;
}

// ── Metrics, defined exactly as the gate file defines them ─────────────────

double band_edge_hz(int edge, int bands = 32,
                    double min_hz = 20.0, double max_hz = 20000.0) {
    const double lo = std::log(min_hz), hi = std::log(max_hz);
    return std::exp(lo + (hi - lo) * static_cast<double>(edge)
                             / static_cast<double>(bands));
}
int bin_of(double hz) {
    return static_cast<int>(std::lround(hz * kAnalysis / kSampleRate));
}
double db_at(const std::vector<double>& mag, const std::vector<double>& ref, int i) {
    if (i < 0 || i >= static_cast<int>(mag.size())) return -400.0;
    if (!(ref[static_cast<std::size_t>(i)] > 0.0)) return -400.0;
    const double m = mag[static_cast<std::size_t>(i)];
    if (!(m > 0.0)) return -400.0;
    return 20.0 * std::log10(m / ref[static_cast<std::size_t>(i)]);
}

struct Report {
    double interior_sup_db = -400.0;   ///< worst point over the MIDDLE HALF
    double deepest_db      = -400.0;   ///< the DEEPEST point anywhere in the band
    double on_grid_sup_db  = -400.0;   ///< middle half, DESIGN-GRID points only
    double coverage        = 0.0;
    double leak_db         = 0.0;
};

Report measure(const std::vector<double>& mag, const std::vector<double>& open,
               double lo_hz, double hi_hz) {
    Report r;
    const int i0 = bin_of(lo_hz), i1 = bin_of(hi_hz);
    const double quarter = 0.25 * (hi_hz - lo_hz);
    const int j0 = bin_of(lo_hz + quarter), j1 = bin_of(hi_hz - quarter);

    for (int i = j0; i <= j1; ++i) {
        const double v = db_at(mag, open, i);
        r.interior_sup_db = std::max(r.interior_sup_db, v);
        // The analysis grid is EIGHT TIMES the design grid, so one analysis bin
        // in eight lands exactly on a design point -- the one place a
        // frequency-sampled filter is exact by construction. Reading only those
        // is the flattering measurement, and it is printed beside the real one
        // so the gap is visible rather than assumed.
        if (i % 8 == 0) r.on_grid_sup_db = std::max(r.on_grid_sup_db, v);
    }
    // The optimistic reading, printed beside the real one: the single deepest
    // point anywhere in the band. A mute is deepest in its middle and shallowest
    // where the transition enters it, so quoting this instead of the interior
    // supremum reports the best bin of the band as if it were the band.
    r.deepest_db = 400.0;
    for (int i = i0; i <= i1; ++i)
        r.deepest_db = std::min(r.deepest_db, db_at(mag, open, i));

    int covered = 0, total = 0;
    for (int i = i0; i <= i1; ++i) {
        ++total;
        if (db_at(mag, open, i) <= -60.0) ++covered;
    }
    r.coverage = total > 0 ? static_cast<double>(covered) / total : 0.0;

    const double guard = kShippingWidthBins * kBinHz;
    for (int i = bin_of(200.0); i <= bin_of(8000.0); ++i) {
        const double f = static_cast<double>(i) * kSampleRate / kAnalysis;
        if (f > lo_hz - guard && f < hi_hz + guard) continue;
        r.leak_db = std::max(r.leak_db, std::abs(db_at(mag, open, i)));
    }
    return r;
}

double surviving_hz(const std::vector<double>& mag,
                    const std::vector<double>& open,
                    double lo_hz, double hi_hz) {
    const int i0 = bin_of(lo_hz), i1 = bin_of(hi_hz);
    const int mid = (i0 + i1) / 2;
    if (db_at(mag, open, mid) <= -3.0) return 0.0;
    int lo = mid, hi = mid;
    while (lo > i0 && db_at(mag, open, lo - 1) > -3.0) --lo;
    while (hi < i1 && db_at(mag, open, hi + 1) > -3.0) ++hi;
    return static_cast<double>(hi - lo + 1) * kSampleRate / kAnalysis;
}

struct KeptCase { const char* label; float lo, hi; std::uint32_t bands, kept; };
const KeptCase kKeptCases[3] = {
    {"32 full",  20.0f, 20000.0f, 32, 20},
    {"decade",  300.0f,  3000.0f, 32, 20},
    {"64 full",  20.0f, 20000.0f, 64, 40},
};

} // namespace

// ── The harness has to be the product before it can report on it ───────────

TEST_CASE("sweep: the design path reads what the renderer reads",
          "[.][sweep][transition]") {
    // The sweep reads 32 cells and cannot afford the renderer, so it reads the
    // design path instead. That is only honest if the two agree AT THE DEPTHS
    // THIS SWEEP REPORTS -- which is a different claim from the existing
    // equivalence control, whose comb field never goes below -25 dB. A deep
    // null is exactly where a float32 convolver could disagree, so it is
    // measured here rather than assumed from the shallower case.
    const auto open_r = spectrum_via_renderer(make_layout(20.0f, 20000.0f, 32));
    const auto open_d = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);

    struct Row { const char* label; pulp::signal::SpectralBandLayout layout; };
    const std::vector<Row> rows{
        {"muted band 20", make_layout(20.0f, 20000.0f, 32, 20)},
        {"island kept 20", make_island(20.0f, 20000.0f, 32, 20)},
    };
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);

    std::printf("\n  %-16s %12s %12s %10s\n",
                "field", "renderer", "design", "delta");
    double worst = 0.0;
    for (const auto& row : rows) {
        const auto r = measure(spectrum_via_renderer(row.layout), open_r, lo, hi);
        const auto d = measure(spectrum_via_design(row.layout, 8, 25), open_d, lo, hi);
        std::printf("  %-16s %9.2f dB %9.2f dB %9.3f\n", row.label,
                    r.interior_sup_db, d.interior_sup_db,
                    d.interior_sup_db - r.interior_sup_db);
        worst = std::max(worst, std::abs(d.interior_sup_db - r.interior_sup_db));
    }
    std::printf("  worst interior-sup disagreement: %.3f dB\n", worst);
    REQUIRE(worst < 0.5);

    // And the row the sweep is FOR: the shipping cell has to reproduce the
    // number the rebase published, or the instrument is reading something else.
    const auto ship = measure(
        spectrum_via_design(make_layout(20.0f, 20000.0f, 32, 20), 8, 25),
        open_d, lo, hi);
    std::printf("  shipping cell (K=8, 25%%): interior sup %.2f dB "
                "(rebase published -81.26)\n", ship.interior_sup_db);
    REQUIRE(ship.interior_sup_db < -80.0);
    REQUIRE(ship.interior_sup_db > -83.0);
}

TEST_CASE("sweep: the width axis is live", "[.][sweep][transition]") {
    // A sweep that reports a flat column because its own axis is not wired is
    // the failure this project has already had once. Two widths at the same
    // placement must give two different designs; if they do not, every row
    // below is the same row printed eight times.
    const auto layout = make_layout(20.0f, 20000.0f, 32, 20);
    const auto open   = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);

    const auto a = measure(spectrum_via_design(layout,  4, 25), open, lo, hi);
    const auto b = measure(spectrum_via_design(layout, 16, 25), open, lo, hi);
    std::printf("\n  K=4 -> %.2f dB, K=16 -> %.2f dB, separation %.2f dB\n",
                a.interior_sup_db, b.interior_sup_db,
                std::abs(b.interior_sup_db - a.interior_sup_db));
    REQUIRE(std::abs(b.interior_sup_db - a.interior_sup_db) > 3.0);

    // And the placement axis, the same way. The rebase published three points
    // on it at K=8; two of them are reproduced here.
    const auto p0   = measure(spectrum_via_design(layout, 8,   0), open, lo, hi);
    const auto p100 = measure(spectrum_via_design(layout, 8, 100), open, lo, hi);
    std::printf("  0%% -> %.2f dB (published -70.58), "
                "100%% -> %.2f dB (published -109.56)\n",
                p0.interior_sup_db, p100.interior_sup_db);
    REQUIRE(p0.interior_sup_db   > -73.0);
    REQUIRE(p0.interior_sup_db   < -68.0);
    REQUIRE(p100.interior_sup_db < -107.0);
    REQUIRE(p100.interior_sup_db > -112.0);
}

// ── The table ──────────────────────────────────────────────────────────────

TEST_CASE("sweep: width x placement", "[.][sweep][transition]") {
    static const int kWidths[]   = {2, 3, 4, 6, 8, 12, 16, 24};
    static const int kOutside[]  = {0, 25, 50, 100};

    const double lo = band_edge_hz(20), hi = band_edge_hz(21);
    const auto muted = make_layout(20.0f, 20000.0f, 32, 20);

    // A flat field carries no step, so shaping never touches it and the open
    // reference is the same for every cell. Computed once per case, at the
    // shipping cell, and reused -- which is only sound BECAUSE it is
    // step-free, so assert that rather than trusting it.
    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    {
        const auto other = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 24, 0);
        double worst = 0.0;
        for (std::size_t i = 0; i < open32.size(); ++i)
            worst = std::max(worst, std::abs(open32[i] - other[i]));
        REQUIRE(worst == 0.0);   // a flat field is invariant under the geometry
    }

    std::vector<std::vector<double>> opens(3);
    for (int c = 0; c < 3; ++c)
        opens[static_cast<std::size_t>(c)] = spectrum_via_design(
            make_layout(kKeptCases[c].lo, kKeptCases[c].hi, kKeptCases[c].bands),
            8, 25);

    // The #142 detection-floor control: a band DRAWN at -100 dB, read back
    // through the identical path at the band's geometric centre. It is the
    // sharpest constraint in the suite and the easiest to leave out of a table.
    auto deep_layout = make_layout(20.0f, 20000.0f, 32);
    deep_layout.bands[20].gain_db = -100.0f;
    const int deep_bin = bin_of(std::sqrt(lo * hi));

    std::printf("\n"
        "  K   out   interior   on-grid  deepest   cover    leak   "
        "kept: 32full decade 64full   -100dB ctl\n");

    for (int w : kWidths) {
        for (int pct : kOutside) {
            const auto r = measure(spectrum_via_design(muted, w, pct), open32, lo, hi);
            const double deep =
                db_at(spectrum_via_design(deep_layout, w, pct), open32, deep_bin);

            double kept_pct[3];
            for (int c = 0; c < 3; ++c) {
                const auto& kc = kKeptCases[c];
                const double klo = band_edge_hz(static_cast<int>(kc.kept),
                                                static_cast<int>(kc.bands), kc.lo, kc.hi);
                const double khi = band_edge_hz(static_cast<int>(kc.kept) + 1,
                                                static_cast<int>(kc.bands), kc.lo, kc.hi);
                const auto island = spectrum_via_design(
                    make_island(kc.lo, kc.hi, kc.bands, kc.kept), w, pct);
                kept_pct[c] = 100.0
                    * surviving_hz(island, opens[static_cast<std::size_t>(c)], klo, khi)
                    / (khi - klo);
            }

            std::printf("  %2d  %3d%%  %8.2f  %8.2f %8.2f  %5.1f%%  %6.2f   "
                        "%7.0f%% %6.0f%% %6.0f%%   %8.2f\n",
                        w, pct, r.interior_sup_db, r.on_grid_sup_db, r.deepest_db,
                        100.0 * r.coverage, r.leak_db,
                        kept_pct[0], kept_pct[1], kept_pct[2], deep);
            std::fflush(stdout);
        }
    }
    std::printf("\n  gates: interior <= -80 dB | cover >= 88%% | leak <= 0.5 dB | "
                "kept >= 85%% (all three) | -100dB ctl in (-101,-99)\n");
}

// ── Every gate the suite actually holds, on one cell ────────────────────────

namespace {

/// The committed unshaped floor from the gate file: what each band of the
/// default field measures with no transition at all. A candidate that leaves
/// any band above its own row by more than the allowance has made something
/// worse, which is the one way this change can regress a user.
constexpr double kUnshapedSupDb[32] = {
    -15.73,  -8.20, -10.74,  -7.80,  -3.81,  -3.77, -11.77,  -7.81,
     -7.81, -18.13, -16.10, -21.12, -26.09, -22.00, -19.99, -26.74,
    -30.14, -28.17, -28.21, -35.66, -34.82, -34.27, -38.44, -42.90,
    -39.98, -42.12, -48.34, -46.98, -46.81, -51.47, -54.89, -55.59,
};
constexpr double kRegressionAllowanceDb = 1.0;

struct Verdict {
    double interior = 0.0, coverage = 0.0, leak = 0.0, ctl = 0.0;
    double kept[3]  = {0, 0, 0};
    double worst_regression = 0.0;   ///< worst band's excess over unshaped
    int    worst_band = -1;
    bool   pass = false;
};

Verdict evaluate(int width, int pct, const std::vector<double>& open32,
                 const std::vector<std::vector<double>>& opens,
                 bool with_regression) {
    Verdict v;
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);
    const auto r = measure(
        spectrum_via_design(make_layout(20.0f, 20000.0f, 32, 20), width, pct),
        open32, lo, hi);
    v.interior = r.interior_sup_db;
    v.coverage = 100.0 * r.coverage;
    v.leak     = r.leak_db;

    auto deep_layout = make_layout(20.0f, 20000.0f, 32);
    deep_layout.bands[20].gain_db = -100.0f;
    v.ctl = db_at(spectrum_via_design(deep_layout, width, pct), open32,
                  bin_of(std::sqrt(lo * hi)));

    for (int c = 0; c < 3; ++c) {
        const auto& kc = kKeptCases[c];
        const double klo = band_edge_hz(static_cast<int>(kc.kept),
                                        static_cast<int>(kc.bands), kc.lo, kc.hi);
        const double khi = band_edge_hz(static_cast<int>(kc.kept) + 1,
                                        static_cast<int>(kc.bands), kc.lo, kc.hi);
        v.kept[c] = 100.0
            * surviving_hz(spectrum_via_design(
                               make_island(kc.lo, kc.hi, kc.bands, kc.kept),
                               width, pct),
                           opens[static_cast<std::size_t>(c)], klo, khi)
            / (khi - klo);
    }

    if (with_regression) {
        for (int band = 0; band < 32; ++band) {
            const double blo = band_edge_hz(band), bhi = band_edge_hz(band + 1);
            const auto br = measure(
                spectrum_via_design(make_layout(20.0f, 20000.0f, 32, band),
                                    width, pct),
                open32, blo, bhi);
            const double excess = br.interior_sup_db - kUnshapedSupDb[band];
            if (excess > v.worst_regression) {
                v.worst_regression = excess;
                v.worst_band = band;
            }
        }
    }

    v.pass = v.interior <= -80.0 && v.coverage >= 88.0 && v.leak <= 0.5
             && v.kept[0] >= 85.0 && v.kept[1] >= 85.0 && v.kept[2] >= 85.0
             && v.ctl < -99.0 && v.ctl > -101.0;
    return v;
}

} // namespace

TEST_CASE("sweep: fine grid with every gate adjudicated", "[.][sweep][transition]") {
    // The coarse table shows the two binding constraints pulling opposite ways
    // on the width axis: the -100 dB control needs a WIDER transition to be
    // realisable at all, and coverage needs a NARROWER one. This resolves the
    // region between them, where a cell clearing both would have to live.
    static const int kWidths[]  = {8, 9, 10, 11, 12, 13, 14, 16};
    static const int kOutside[] = {25, 30, 35, 40, 50};

    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    std::vector<std::vector<double>> opens(3);
    for (int c = 0; c < 3; ++c)
        opens[static_cast<std::size_t>(c)] = spectrum_via_design(
            make_layout(kKeptCases[c].lo, kKeptCases[c].hi, kKeptCases[c].bands),
            8, 25);

    std::printf("\n  K  out   interior  cover   leak   32full decade 64full"
                "   -100ctl   verdict\n");
    int passes = 0;
    for (int w : kWidths) {
        for (int pct : kOutside) {
            const auto v = evaluate(w, pct, open32, opens, false);
            if (v.pass) ++passes;
            std::printf("  %2d %3d%%  %8.2f %5.1f%% %6.2f   %5.0f%% %5.0f%% %5.0f%%"
                        "  %8.2f   %s\n",
                        w, pct, v.interior, v.coverage, v.leak,
                        v.kept[0], v.kept[1], v.kept[2], v.ctl,
                        v.pass ? "PASS" : "fail");
            std::fflush(stdout);
        }
    }
    std::printf("\n  cells clearing every gate: %d of %d\n",
                passes, static_cast<int>(std::size(kWidths) * std::size(kOutside)));
}

TEST_CASE("sweep: how steady is the -100 dB control" , "[.][sweep][transition]") {
    // The control is ONE probe bin at the band's geometric centre, and the
    // coarse sweep reads it non-monotonically in width: -100.34 at K=9,
    // -101.82 at K=10, -100.40 at K=11. A gate that swings 1.5 dB between
    // adjacent widths is not reading a trend, it is reading ripple -- so a cell
    // that passes it may be passing by where its ripple landed. This prints the
    // whole middle half so the spread is visible instead of inferred.
    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    auto deep = make_layout(20.0f, 20000.0f, 32);
    deep.bands[20].gain_db = -100.0f;
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);
    const int probe = bin_of(std::sqrt(lo * hi));

    std::printf("\n   K  out    probe   mid-half sup   mid-half inf   spread\n");
    for (int w : {8, 9, 10, 11, 12, 13, 14, 16}) {
        for (int pct : {25}) {
            const auto mag = spectrum_via_design(deep, w, pct);
            const auto r = measure(mag, open32, lo, hi);
            std::printf("  %2d %3d%%  %7.2f   %10.2f   %12.2f   %6.2f\n",
                        w, pct, db_at(mag, open32, probe),
                        r.interior_sup_db, r.deepest_db,
                        r.interior_sup_db - r.deepest_db);
            std::fflush(stdout);
        }
    }
}

TEST_CASE("sweep: the placement neighbourhood of the candidate",
          "[.][sweep][transition]") {
    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    std::vector<std::vector<double>> opens(3);
    for (int c = 0; c < 3; ++c)
        opens[static_cast<std::size_t>(c)] = spectrum_via_design(
            make_layout(kKeptCases[c].lo, kKeptCases[c].hi, kKeptCases[c].bands),
            8, 25);

    std::printf("\n  K  out   interior  cover   leak   32full decade 64full"
                "   -100ctl   verdict\n");
    for (int w : {9, 10}) {
        for (int pct : {20, 22, 25, 26, 27, 28}) {
            const auto v = evaluate(w, pct, open32, opens, false);
            std::printf("  %2d %3d%%  %8.2f %5.1f%% %6.2f   %5.0f%% %5.0f%% %5.0f%%"
                        "  %8.2f   %s\n",
                        w, pct, v.interior, v.coverage, v.leak,
                        v.kept[0], v.kept[1], v.kept[2], v.ctl,
                        v.pass ? "PASS" : "fail");
            std::fflush(stdout);
        }
    }
}

TEST_CASE("sweep: the candidate against every band's unshaped floor",
          "[.][sweep][transition]") {
    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    std::vector<std::vector<double>> opens(3);
    for (int c = 0; c < 3; ++c)
        opens[static_cast<std::size_t>(c)] = spectrum_via_design(
            make_layout(kKeptCases[c].lo, kKeptCases[c].hi, kKeptCases[c].bands),
            8, 25);
    for (int w : {9}) {
        const auto v = evaluate(w, 25, open32, opens, true);
        std::printf("\n  K=%d 25%%: worst regression %+.2f dB at band %d "
                    "(allowance %.2f)\n", w, v.worst_regression, v.worst_band,
                    kRegressionAllowanceDb);
    }
}

TEST_CASE("sweep: was the -100 dB control ever reading more than one bin",
          "[.][sweep][transition]") {
    // Before recommending a cell on the strength of this control, find out what
    // it was measuring on the geometry that SHIPPED. If `main`'s own cell also
    // reads a wide spread, then "one probe bin on a rippled band" is what this
    // control has always been, and a candidate is not newly guilty of it.
    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    auto deep = make_layout(20.0f, 20000.0f, 32);
    deep.bands[20].gain_db = -100.0f;
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);
    const int probe = bin_of(std::sqrt(lo * hi));

    struct Cell { const char* what; int w, pct; };
    const Cell cells[] = {
        {"main (#149, symmetric)",  8, 100},
        {"#146 merged (shipping)",  8,  25},
        {"candidate",               9,  25},
        {"genuinely realised",     13,  25},
    };
    std::printf("\n  %-24s  K  out    probe   mid-half sup   mid-half inf  spread\n",
                "cell");
    for (const auto& c : cells) {
        const auto mag = spectrum_via_design(deep, c.w, c.pct);
        const auto r = measure(mag, open32, lo, hi);
        std::printf("  %-24s %2d %3d%%  %7.2f   %10.2f   %12.2f  %6.2f\n",
                    c.what, c.w, c.pct, db_at(mag, open32, probe),
                    r.interior_sup_db, r.deepest_db,
                    r.interior_sup_db - r.deepest_db);
        std::fflush(stdout);
    }
}

// ── The third axis: how finely a band edge is allowed to be positioned ──────

TEST_CASE("sweep: the edge-quantum axis is live", "[.][sweep][transition]") {
    // Same discipline as the width and placement axes: two quanta must give two
    // different designs, or every row of the table below is one row printed
    // eleven times. The separation to look for is the one sub-bin placement
    // paid for its glide -- exact against whole-bin -- about 8 dB.
    const auto layout = make_layout(20.0f, 20000.0f, 32, 20);
    const auto open   = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    const double lo = band_edge_hz(20), hi = band_edge_hz(21);

    const auto exact = measure(spectrum_via_design(layout, 8, 25, 0.0), open, lo, hi);
    const auto whole = measure(spectrum_via_design(layout, 8, 25, 1.0), open, lo, hi);
    std::printf("\n  exact -> %.2f dB, whole-bin -> %.2f dB, separation %.2f dB\n",
                exact.interior_sup_db, whole.interior_sup_db,
                std::abs(whole.interior_sup_db - exact.interior_sup_db));
    REQUIRE(std::abs(whole.interior_sup_db - exact.interior_sup_db) > 3.0);

    // And the arm that makes the axis a QUANTISATION rather than a second
    // placement knob: at a non-zero quantum the shaped design must HOLD STILL
    // as the edge slides within one step, and at zero it must not. That
    // standing-still is the whole mechanism -- it is what makes a drag a
    // staircase, and it is what a coarser grid is being bought with.
    //
    // Measured on a synthetic two-band field rather than a compiled layout, so
    // the edge can be moved by a known sub-quantum amount instead of by
    // whatever a viewport happens to produce. The function is pure, so this is
    // the same function the product calls.
    auto shaped_at = [](double edge_bin_pos, double q, bool shape = true) {
        // The step sits where the COMPILER would have put it for this edge:
        // `ceil(edge)` is the first bin of the upper band, so an edge anywhere
        // in (256, 257] compiles to a step at 257. That is the real situation
        // the placement lives in -- the array's step moves only at integer
        // crossings while the edge itself moves continuously -- and a field
        // whose step disagreed with its edge would be measuring the desync
        // above rather than the placement.
        std::vector<double> m(512, 1.0);
        for (std::size_t i = 257; i < m.size(); ++i) m[i] = kFloor;
        const float e[3] = {0.0f, static_cast<float>(edge_bin_pos * kBinHz),
                            static_cast<float>(512.0 * kBinHz)};
        if (shape)
            (void)spectr::shape_tracking_transitions(
                m, std::span<const float>(e, 3), kBinHz, 8, kFloor, 25, q);
        return m;
    };
    const auto moved_exact = shaped_at(256.30, 0.0) != shaped_at(256.40, 0.0);
    const auto moved_half  = shaped_at(256.30, 0.5) != shaped_at(256.40, 0.5);
    std::printf("  a 0.1-bin edge move changes the design: exact %s, q=0.5 %s\n",
                moved_exact ? "yes" : "no", moved_half ? "yes" : "no");
    REQUIRE(moved_exact);    // the instrument can see a sub-bin move at all
    REQUIRE(!moved_half);    // and a quantum of half a bin absorbs it

    pulp::signal::SpectralMaskTable table;
    REQUIRE(pulp::signal::build_spectral_mask(
        layout, kGrid, static_cast<float>(kSampleRate), table));
    const auto edge_count = static_cast<std::size_t>(table.active_bands) + 1u;

    // The step probe must stay on the EXACT edge while the placement snaps.
    // Reading it from the snapped position instead leaves an edge unshaped
    // whenever the snap lands just inside the upper band, which is silent: the
    // band comes back at its drawn depth and every other gate still passes.
    for (double q : {0.0, 0.0625, 0.375, 0.5, 0.625, 1.0}) {
        std::vector<double> m(static_cast<std::size_t>(table.num_bins));
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = static_cast<double>(table.gain_linear[i]);
        const auto g = spectr::shape_tracking_transitions(
            m, std::span<const float>(table.band_edges_hz.data(), edge_count),
            kBinHz, 8, kFloor, 25, q);
        std::printf("  q=%.4f: %lld of %lld edges shaped\n", q,
                    static_cast<long long>(g.edges_shaped),
                    static_cast<long long>(g.edges_considered));
        REQUIRE(g.edges_shaped == 2);   // a one-muted-band field has two steps
    }

    // A quantum the arithmetic cannot honour must leave the edge exact rather
    // than reach `ceil` with a NaN. Infinity and a denormal both drive
    // `round(bin / q) * q` non-finite; the clamp does not catch that, because
    // neither comparison holds against a NaN. Read as "identical to exact",
    // which is a stronger claim than "did not crash".
    const auto exact_design = shaped_at(256.30, 0.0);
    const auto unshaped     = shaped_at(256.30, 0.0, false);
    // Without this the two expectations below could be the same vector and
    // either claim would be satisfiable by the other.
    REQUIRE(exact_design != unshaped);

    // Infinity and a denormal PASS the argument guard -- both are >= 0 -- so
    // each must fall back to exact placement.
    for (double q : {std::numeric_limits<double>::infinity(), 1.0e-300})
        REQUIRE(shaped_at(256.30, q) == exact_design);
    // A NaN or a negative quantum is rejected by the guard, so the field comes
    // back untouched, which is what every other unusable argument does.
    for (double q : {std::numeric_limits<double>::quiet_NaN(), -1.0})
        REQUIRE(shaped_at(256.30, q) == unshaped);
}

TEST_CASE("sweep: the edge quantum against depth",
          "[.][sweep][transition]") {
    // Sub-bin edge placement spends depth to buy a glide. This asks what a
    // COARSER grid than exact returns of that depth -- one half of a Pareto
    // question whose other half (what the same grid costs in wobble) is
    // measured in the gate file, because that is where the drag instrument and
    // its controls live.
    //
    // Width and placement are held at the shipping cell throughout, so every
    // row differs from every other in exactly one thing.
    static const double kQuanta[] = {
        0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.5, 0.625, 0.75,
        0.875, 1.0,
    };

    const double lo = band_edge_hz(20), hi = band_edge_hz(21);
    const auto muted = make_layout(20.0f, 20000.0f, 32, 20);

    const auto open32 = spectrum_via_design(make_layout(20.0f, 20000.0f, 32), 8, 25);
    std::vector<std::vector<double>> opens(3);
    for (int c = 0; c < 3; ++c)
        opens[static_cast<std::size_t>(c)] = spectrum_via_design(
            make_layout(kKeptCases[c].lo, kKeptCases[c].hi, kKeptCases[c].bands),
            8, 25);

    auto deep_layout = make_layout(20.0f, 20000.0f, 32);
    deep_layout.bands[20].gain_db = -100.0f;
    const int deep_bin = bin_of(std::sqrt(lo * hi));

    std::printf("\n"
        "    q   interior   on-grid  deepest   cover    leak   "
        "kept: 32full decade 64full   -100dB ctl   ctl sup\n");

    for (double q : kQuanta) {
        const auto r = measure(spectrum_via_design(muted, 8, 25, q), open32, lo, hi);
        const auto deep_mag = spectrum_via_design(deep_layout, 8, 25, q);
        const double deep = db_at(deep_mag, open32, deep_bin);
        // The control's own supremum over the same middle half, printed beside
        // the single probe bin it is read at. The width sweep found those two
        // 5 dB apart at the candidate cell, which is how a cell passes the
        // control on ripple rather than on realisation.
        const double deep_sup = measure(deep_mag, open32, lo, hi).interior_sup_db;

        double kept_pct[3];
        for (int c = 0; c < 3; ++c) {
            const auto& kc = kKeptCases[c];
            const double klo = band_edge_hz(static_cast<int>(kc.kept),
                                            static_cast<int>(kc.bands), kc.lo, kc.hi);
            const double khi = band_edge_hz(static_cast<int>(kc.kept) + 1,
                                            static_cast<int>(kc.bands), kc.lo, kc.hi);
            const auto island = spectrum_via_design(
                make_island(kc.lo, kc.hi, kc.bands, kc.kept), 8, 25, q);
            kept_pct[c] = 100.0
                * surviving_hz(island, opens[static_cast<std::size_t>(c)], klo, khi)
                / (khi - klo);
        }

        std::printf("  %5.4f  %8.2f  %8.2f %8.2f  %5.1f%%  %6.2f   "
                    "%7.0f%% %6.0f%% %6.0f%%   %9.2f  %8.2f\n",
                    q, r.interior_sup_db, r.on_grid_sup_db, r.deepest_db,
                    100.0 * r.coverage, r.leak_db,
                    kept_pct[0], kept_pct[1], kept_pct[2], deep, deep_sup);
        std::fflush(stdout);
    }
    std::printf("\n  gates: interior <= -80 dB | cover >= 88%% | leak <= 0.5 dB | "
                "kept >= 85%% (all three) | -100dB ctl in (-101,-99)\n");
}
