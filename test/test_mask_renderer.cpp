#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/mask_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using Catch::Approx;
using spectr::MaskRenderer;
using spectr::MaskRendererConfig;
using spectr::MaskRenderMode;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

MaskRendererConfig zero_latency_config(int channels = 2, int max_block = 512) {
    MaskRendererConfig config;
    config.design_grid_size = 8192;
    config.analysis_hop     = 2048;
    config.channels         = channels;
    config.max_block        = max_block;
    config.sample_rate      = kSampleRate;
    config.initial_mix      = 1.0f;
    config.mix_ramp_samples = 0;
    return config;
}

/// A Spectr band field: `bands` logarithmic bands across a window, with one
/// band optionally muted.
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
    for (std::uint32_t band = 0; band < layout.active_bands; ++band) {
        layout.bands[band].gain_db = 0.0f;
        layout.bands[band].muted =
            muted_band >= 0 && band == static_cast<std::uint32_t>(muted_band);
    }
    return layout;
}

/// The zoom v1 measured the mute defect on: 32 bands over 200-800 Hz, which is
/// what a user reaches for to remove one specific thing.
pulp::signal::SpectralBandLayout zoom_layout(int muted_band = -1) {
    return make_layout(200.0f, 800.0f, 32, muted_band);
}

/// Spectr's default field: 32 bands across the audible range. Its mid bands
/// are hundreds of hertz wide, so they own tens of design bins.
pulp::signal::SpectralBandLayout full_range_layout(int muted_band = -1) {
    return make_layout(20.0f, 20000.0f, 32, muted_band);
}

/// Geometric centre of one band of a logarithmic field.
double band_centre_hz(const pulp::signal::SpectralBandLayout& layout, int band) {
    const double lo = std::log(static_cast<double>(layout.min_hz));
    const double hi = std::log(static_cast<double>(layout.max_hz));
    const double step = (hi - lo) / static_cast<double>(layout.active_bands);
    return std::exp(lo + step * (static_cast<double>(band) + 0.5));
}

/// Least-squares projection of `signal` onto a tone at `hz`.
///
/// Deliberately NOT an FFT magnitude. Reading a -100 dB residual beside a 0 dB
/// fundamental is a window problem no ordinary window solves: Hann's first side
/// lobe is -31.5 dB and even flat-top only reaches about -93 dB, so a windowed
/// magnitude would be measuring its own leakage rather than the filter. A
/// projection onto the exact basis has no side lobes to leak through; its floor
/// is set by how much of the signal is genuinely NOT at this frequency, which
/// every test below states and proves with a control.
double project_tone_amplitude(const std::vector<float>& signal,
                              std::size_t begin, std::size_t count, double hz) {
    const double omega = 2.0 * kPi * hz / kSampleRate;
    double real = 0.0;
    double imag = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double phase = omega * static_cast<double>(i);
        const double value = static_cast<double>(signal[begin + i]);
        real += value * std::cos(phase);
        imag += value * std::sin(phase);
    }
    const double scale = 2.0 / static_cast<double>(count);
    return std::hypot(real * scale, imag * scale);
}

double amplitude_db(double measured, double reference) {
    if (!(reference > 0.0)) return 0.0;
    if (!(measured > 0.0)) return -300.0;
    return 20.0 * std::log10(measured / reference);
}

struct RenderResult {
    std::vector<float> left;
    std::vector<float> right;
};

/// Drive a renderer with a planar stimulus, chopped into blocks of
/// `block_size` (or into the sequence `chunks` when one is given).
RenderResult render(MaskRenderer& renderer, const std::vector<float>& stimulus,
                    int block_size, const std::vector<int>& chunks = {},
                    bool pace_with_wall_clock = false) {
    RenderResult result;
    result.left.assign(stimulus.size(), 0.0f);
    result.right.assign(stimulus.size(), 0.0f);

    std::vector<float> in_l, in_r, out_l, out_r;
    std::size_t position = 0;
    std::size_t chunk_index = 0;
    while (position < stimulus.size()) {
        int n = chunks.empty()
                    ? block_size
                    : chunks[chunk_index++ % chunks.size()];
        n = static_cast<int>(std::min<std::size_t>(
            static_cast<std::size_t>(n), stimulus.size() - position));
        in_l.assign(stimulus.begin() + static_cast<std::ptrdiff_t>(position),
                    stimulus.begin() + static_cast<std::ptrdiff_t>(position + n));
        in_r = in_l;
        out_l.assign(static_cast<std::size_t>(n), 0.0f);
        out_r.assign(static_cast<std::size_t>(n), 0.0f);
        const float* in[2] = {in_l.data(), in_r.data()};
        float* out[2] = {out_l.data(), out_r.data()};
        REQUIRE(renderer.process(in, out, n));
        std::copy(out_l.begin(), out_l.end(),
                  result.left.begin() + static_cast<std::ptrdiff_t>(position));
        std::copy(out_r.begin(), out_r.end(),
                  result.right.begin() + static_cast<std::ptrdiff_t>(position));
        position += static_cast<std::size_t>(n);
        if (pace_with_wall_clock)
            std::this_thread::sleep_for(std::chrono::microseconds(700));
    }
    return result;
}

std::vector<float> tone(std::size_t samples, double hz, float amplitude = 0.5f) {
    std::vector<float> out(samples, 0.0f);
    for (std::size_t i = 0; i < samples; ++i)
        out[i] = amplitude * static_cast<float>(
            std::sin(2.0 * kPi * hz * static_cast<double>(i) / kSampleRate));
    return out;
}

/// Wait for a staged design to reach the audio path. The renderer's adoption
/// boundary is a render block, so this pumps silence rather than sleeping on
/// a generation counter that only advances when audio runs.
void settle(MaskRenderer& renderer, unsigned long long target_generation) {
    std::vector<float> silence(512, 0.0f);
    for (int attempt = 0; attempt < 400; ++attempt) {
        if (renderer.active_generation() >= target_generation) return;
        (void)render(renderer, silence, 256);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace

// ── D5: latency is a function of the mode, and nothing else ────────────────

TEST_CASE("Mask renderer latency depends on the mode and nothing else",
          "[mask-renderer][latency][contract]") {
    // Two configurations that differ in every runtime-ish way a machine could
    // vary: channel count, host block size, sample rate. A saved project must
    // recall the same delay compensation on both.
    MaskRendererConfig a = zero_latency_config(/*channels=*/2, /*max_block=*/512);
    MaskRendererConfig b = zero_latency_config(/*channels=*/1, /*max_block=*/32);
    b.sample_rate = 44100.0;

    for (auto mode : {MaskRenderMode::linear_phase, MaskRenderMode::zero_latency}) {
        REQUIRE(spectr::mask_render_latency_samples(mode, a)
                == spectr::mask_render_latency_samples(mode, b));
    }

    // And the two modes must actually differ, or the test above is vacuous.
    REQUIRE(spectr::mask_render_latency_samples(MaskRenderMode::linear_phase, a)
            > spectr::mask_render_latency_samples(MaskRenderMode::zero_latency, a));

    // A prepared renderer reports exactly what the pure function promised.
    for (auto mode : {MaskRenderMode::linear_phase, MaskRenderMode::zero_latency}) {
        auto renderer = spectr::make_mask_renderer(mode);
        REQUIRE(renderer);
        REQUIRE(renderer->prepare(a));
        REQUIRE(renderer->latency_samples()
                == spectr::mask_render_latency_samples(mode, a));

        auto narrow = spectr::make_mask_renderer(mode);
        REQUIRE(narrow->prepare(b));
        REQUIRE(narrow->latency_samples() == renderer->latency_samples());
    }
}

TEST_CASE("Zero-latency mode reports a delay the audio actually has",
          "[mask-renderer][latency][contract]") {
    auto renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE(renderer->prepare(zero_latency_config()));
    REQUIRE(renderer->publish_layout(zoom_layout()));
    settle(*renderer, 1);
    renderer->reset();

    std::vector<float> impulse(4096, 0.0f);
    impulse[0] = 1.0f;
    const auto out = render(*renderer, impulse, 128);

    std::size_t first_nonzero = out.left.size();
    for (std::size_t i = 0; i < out.left.size(); ++i) {
        if (std::abs(out.left[i]) > 1.0e-7f) { first_nonzero = i; break; }
    }
    INFO("reported latency " << renderer->latency_samples()
         << ", first non-zero output sample " << first_nonzero);
    REQUIRE(first_nonzero
            == static_cast<std::size_t>(renderer->latency_samples()));

    // The control: the same search on an all-zero render must NOT find a
    // sample, so "found it at 64" is a reading of the audio and not of the
    // threshold.
    renderer->reset();
    const std::vector<float> silence(4096, 0.0f);
    const auto quiet = render(*renderer, silence, 128);
    const auto loudest = *std::max_element(
        quiet.left.begin(), quiet.left.end(),
        [](float x, float y) { return std::abs(x) < std::abs(y); });
    REQUIRE(std::abs(loudest) <= 1.0e-7f);
}

// ── D3: the render path is paced by samples, never by a clock ──────────────

TEST_CASE("Zero-latency output does not depend on how the host chops the stream",
          "[mask-renderer][offline][rt-safety]") {
    // An offline bounce differs from real-time playback in exactly two ways:
    // the host hands over different block lengths, and it hands them over
    // faster. This pins the first of those bit-exactly. Nothing in the render
    // path may make the output a function of the block boundary.
    const auto stimulus = tone(24000, 440.0);
    const auto layout = zoom_layout(/*muted_band=*/10);

    auto reference_renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE(reference_renderer->prepare(zero_latency_config()));
    REQUIRE(reference_renderer->publish_layout(layout));
    settle(*reference_renderer, 1);
    reference_renderer->reset();
    const auto reference = render(*reference_renderer, stimulus, 512);

    // 64 divides the render block; 37 and 129 do not; the ragged sequence
    // chops mid-block on purpose; 1 is the pathological limit.
    const std::vector<std::vector<int>> chunkings = {
        {64}, {37}, {129}, {512}, {1}, {17, 256, 3, 64, 101},
    };
    for (const auto& chunks : chunkings) {
        auto renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
        REQUIRE(renderer->prepare(zero_latency_config()));
        REQUIRE(renderer->publish_layout(layout));
        settle(*renderer, 1);
        renderer->reset();
        const auto candidate = render(*renderer, stimulus, 0, chunks);

        std::size_t mismatches = 0;
        double worst = 0.0;
        for (std::size_t i = 0; i < reference.left.size(); ++i) {
            if (reference.left[i] != candidate.left[i]) {
                ++mismatches;
                worst = std::max(worst, static_cast<double>(std::abs(
                    reference.left[i] - candidate.left[i])));
            }
        }
        std::string label;
        for (int c : chunks) label += std::to_string(c) + " ";
        INFO("chunking [" << label << "] mismatches=" << mismatches
             << " worst=" << worst);
        REQUIRE(mismatches == 0);
    }

    // Control: the comparison can tell two renders apart. Without this, a
    // broken comparison would report zero mismatches for every chunking and
    // read as a clean pass.
    auto different = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE(different->prepare(zero_latency_config()));
    REQUIRE(different->publish_layout(zoom_layout(/*muted_band=*/11)));
    settle(*different, 1);
    different->reset();
    const auto other = render(*different, stimulus, 512);
    std::size_t control_mismatches = 0;
    for (std::size_t i = 0; i < reference.left.size(); ++i)
        if (reference.left[i] != other.left[i]) ++control_mismatches;
    REQUIRE(control_mismatches > 0);
}

TEST_CASE("Zero-latency output does not depend on how fast the host calls",
          "[mask-renderer][offline][rt-safety]") {
    // The faster-than-real-time equivalence. One render as fast as the CPU
    // allows; one with real wall-clock time elapsing between every block. Any
    // clock read on the render path that reaches the output separates these.
    const auto stimulus = tone(8192, 300.0);
    const auto layout = zoom_layout(/*muted_band=*/10);

    auto fast = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE(fast->prepare(zero_latency_config()));
    REQUIRE(fast->publish_layout(layout));
    settle(*fast, 1);
    fast->reset();
    const auto tight = render(*fast, stimulus, 128);

    auto slow = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE(slow->prepare(zero_latency_config()));
    REQUIRE(slow->publish_layout(layout));
    settle(*slow, 1);
    slow->reset();
    const auto paced = render(*slow, stimulus, 128, {},
                              /*pace_with_wall_clock=*/true);

    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < tight.left.size(); ++i)
        if (tight.left[i] != paced.left[i]) ++mismatches;
    INFO("tight-loop vs wall-clock-paced mismatches: " << mismatches);
    REQUIRE(mismatches == 0);
}

// ── The defect: what a mute actually delivers ──────────────────────────────

TEST_CASE("Delivered mute depth, both modes, at the shipping design grid",
          "[mask-renderer][depth][audio]") {
    // The tolerance table. A mode that cannot state what it delivers is not
    // ready to be a mode, so these numbers are committed rather than described
    // — and they are measured through the renderer, not read off the table the
    // renderer was handed.
    //
    // What they show, and it is not what the v1 study predicted: depth tracks
    // how many design bins the drawn band OWNS, in BOTH modes. Minimum phase
    // removes the pre-smear and the 213 ms of delay; it does not make a band
    // narrower than the design grid removable. A hard zero across two bins is
    // realised as two deep notches AT those bins with the response recovering
    // between them, which is the grid speaking, not the phase. Raising the
    // grid is the lever, and that is a separate slice.
    struct Case {
        const char* name;
        pulp::signal::SpectralBandLayout (*layout)(int);
        int  band;
        double zero_latency_at_most_db;  ///< the claim this case gates
    };
    const Case cases[] = {
        // 1500-1861 Hz, 62 design bins wide: a mute here is categorical.
        {"full range, band 20 of 32", &full_range_layout, 20, -25.0},
        // 308-322 Hz, 2 design bins wide: grid-bound, and honestly so. The
        // bound is set by the WORSE of the two probe placements (-6.0 dB
        // off-bin against -11.5 dB on the band centre), because a user's
        // note lands where it lands.
        {"200-800 Hz zoom, band 10 of 32", &zoom_layout, 10, -5.0},
    };

    auto measure = [&](MaskRenderMode mode,
                       const pulp::signal::SpectralBandLayout& layout,
                       double probe_hz) {
        auto renderer = spectr::make_mask_renderer(mode);
        REQUIRE(renderer->prepare(zero_latency_config()));
        REQUIRE(renderer->publish_layout(layout));
        settle(*renderer, 1);
        renderer->reset();
        // Long enough that a 171 ms ring and a 213 ms latency are both behind
        // the measurement window.
        const auto stimulus = tone(96000, probe_hz);
        const auto out = render(*renderer, stimulus, 512);
        return project_tone_amplitude(out.left, 48000, 32768, probe_hz);
    };

    // Half a design bin. A band centre can land on a design bin, and a probe
    // that does gets a flattering reading from the linear-phase path, which
    // zeroes that bin outright. Every case is therefore measured twice: on the
    // band centre and deliberately off-bin.
    const double half_bin_hz = 0.5 * kSampleRate / 8192.0;

    for (const auto& c : cases) {
      for (double offset : {0.0, half_bin_hz}) {
        const auto field = c.layout(-1);
        const double probe_hz = band_centre_hz(field, c.band) + offset;
        const double neighbour_hz = band_centre_hz(field, c.band + 6);

        const double reference =
            measure(MaskRenderMode::zero_latency, field, probe_hz);
        REQUIRE(reference > 0.1);   // the unmuted path passes the probe

        // The instrument's detection floor, PROVEN rather than derived: the
        // identical projection over the identical window with the probe
        // removed entirely. Any reading at or below this is the instrument,
        // not the filter. Deriving a floor from the residual would assume the
        // residual is white, which is exactly false here — what is left after
        // a notch is a discrete tone, not noise.
        auto floor_renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
        REQUIRE(floor_renderer->prepare(zero_latency_config()));
        REQUIRE(floor_renderer->publish_layout(field));
        settle(*floor_renderer, 1);
        floor_renderer->reset();
        const std::vector<float> silence(96000, 0.0f);
        const auto quiet = render(*floor_renderer, silence, 512);
        const double floor_db = amplitude_db(
            std::max(project_tone_amplitude(quiet.left, 48000, 32768, probe_hz),
                     1.0e-12),
            reference);

        const auto muted = c.layout(c.band);
        const double zl_db =
            amplitude_db(measure(MaskRenderMode::zero_latency, muted, probe_hz),
                         reference);
        const double lp_db =
            amplitude_db(measure(MaskRenderMode::linear_phase, muted, probe_hz),
                         measure(MaskRenderMode::linear_phase, field, probe_hz));
        const double neighbour_db =
            amplitude_db(measure(MaskRenderMode::zero_latency, muted, neighbour_hz),
                         measure(MaskRenderMode::zero_latency, field, neighbour_hz));

        INFO(c.name << ", probe " << probe_hz << " Hz ("
             << (offset == 0.0 ? "band centre" : "half a design bin off") << ")\n"
             << "  detection floor          " << floor_db << " dB\n"
             << "  zero latency, muted      " << zl_db << " dB\n"
             << "  linear phase, muted      " << lp_db << " dB\n"
             << "  zero latency, neighbour  " << neighbour_db << " dB (control)");

        // Asserted FIRST: a gate that passes because the measurement cannot
        // resolve the failure is worse than no gate.
        REQUIRE(floor_db < -60.0);

        // The control: a band six steps away, measured by the identical
        // instrument in the identical render, must read essentially untouched.
        // If this collapsed too, the depth above would be the measurement
        // failing rather than the band being removed.
        REQUIRE(neighbour_db > -1.5);

        REQUIRE(zl_db < c.zero_latency_at_most_db);
        // And the linear-phase path is measured in the same run so the two are
        // comparable at a glance in the failure output; no ordering between
        // them is asserted, because which one is deeper depends on the grid
        // and on where the probe falls relative to a bin.
        REQUIRE(lp_db < 0.0);
      }
    }
}

TEST_CASE("A band narrower than the design grid is not removable in either mode",
          "[mask-renderer][depth][audio]") {
    // The defect, stated as a test rather than as a paragraph. At 64 bands
    // across 280-340 Hz the drawn bands are under 1 Hz wide and own ZERO bins
    // of the 5.86 Hz design grid, so muting one is inaudible — in both modes.
    // The UI promises resolution the DSP does not have. This test exists so
    // that a future grid change shows up as a failure here and has to be
    // acknowledged, rather than quietly improving an untested number.
    const auto field = make_layout(280.0f, 340.0f, 64);
    constexpr int kBand = 30;
    const double probe_hz = band_centre_hz(field, kBand);

    auto measure = [&](MaskRenderMode mode, int muted_band) {
        auto renderer = spectr::make_mask_renderer(mode);
        REQUIRE(renderer->prepare(zero_latency_config()));
        REQUIRE(renderer->publish_layout(make_layout(280.0f, 340.0f, 64, muted_band)));
        settle(*renderer, 1);
        renderer->reset();
        const auto stimulus = tone(96000, probe_hz);
        const auto out = render(*renderer, stimulus, 512);
        return project_tone_amplitude(out.left, 48000, 32768, probe_hz);
    };

    for (auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        const double open_amp = measure(mode, -1);
        REQUIRE(open_amp > 0.1);
        const double muted_db = amplitude_db(measure(mode, kBand), open_amp);
        INFO((mode == MaskRenderMode::zero_latency ? "zero latency" : "linear phase")
             << " at " << probe_hz << " Hz, 64 bands over 280-340 Hz: "
             << muted_db << " dB");
        // Under 3 dB is the honest reading of "nothing happened". If this ever
        // fails it is good news and the resolution disclosure in the UI has to
        // be revisited in the same change.
        REQUIRE(muted_db > -3.0);
    }
}

TEST_CASE("Zero-latency mode puts no smear before the event",
          "[mask-renderer][depth][audio]") {
    // Minimum phase is causal: a filter cannot respond to a transient it has
    // not heard. The linear-phase path answers a transient with an equal
    // amount of energy BEFORE it, which is what a listener hears as smearing.
    auto run = [&](MaskRenderMode mode) {
        auto renderer = spectr::make_mask_renderer(mode);
        REQUIRE(renderer->prepare(zero_latency_config()));
        REQUIRE(renderer->publish_layout(zoom_layout(/*muted_band=*/10)));
        settle(*renderer, 1);
        renderer->reset();
        std::vector<float> stimulus(65536, 0.0f);
        const std::size_t strike = 32768;
        stimulus[strike] = 1.0f;
        const auto out = render(*renderer, stimulus, 512);

        const auto latency = static_cast<std::size_t>(renderer->latency_samples());
        // Energy in the 1024 samples that arrive BEFORE the strike would have,
        // after removing the renderer's own reported delay.
        const std::size_t aligned = strike + latency;
        double pre = 0.0;
        for (std::size_t i = aligned - 1024; i < aligned; ++i)
            pre += static_cast<double>(out.left[i]) * out.left[i];
        double post = 0.0;
        for (std::size_t i = aligned; i < aligned + 1024; ++i)
            post += static_cast<double>(out.left[i]) * out.left[i];
        return std::pair<double, double>{pre, post};
    };

    const auto [zl_pre, zl_post] = run(MaskRenderMode::zero_latency);
    const auto [lp_pre, lp_post] = run(MaskRenderMode::linear_phase);

    INFO("zero latency: pre=" << zl_pre << " post=" << zl_post
         << "\nlinear phase: pre=" << lp_pre << " post=" << lp_post);

    // Control: both renderers actually responded to the strike. Without it a
    // silent renderer would score a perfect zero pre-ring.
    REQUIRE(zl_post > 1.0e-6);
    REQUIRE(lp_post > 1.0e-6);

    // Linear phase answers before the strike; minimum phase does not.
    REQUIRE(lp_pre > 1.0e-9);
    REQUIRE(zl_pre < lp_pre * 1.0e-3);
}

// ── D4: the design grid is a parameter, not a pinned constant ──────────────

TEST_CASE("The design grid is a renderer parameter and fails closed above its caps",
          "[mask-renderer][contract]") {
    for (int grid : {2048, 4096, 8192, 16384}) {
        auto config = zero_latency_config();
        config.design_grid_size = grid;
        config.analysis_hop = grid / 4;
        auto renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
        INFO("grid " << grid);
        REQUIRE(renderer->prepare(config));
        REQUIRE(renderer->publish_layout(zoom_layout()));
        // Latency is a property of the mode, so raising the grid must not
        // move it.
        REQUIRE(renderer->latency_samples()
                == spectr::mask_render_latency_samples(
                       MaskRenderMode::zero_latency, config));
    }

    // Above the framework's mask-table capacity the renderer refuses rather
    // than silently truncating the magnitude it was asked to realise.
    auto config = zero_latency_config();
    config.design_grid_size = 32768;
    config.analysis_hop = 8192;
    auto renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE_FALSE(renderer->prepare(config));
    REQUIRE_FALSE(renderer->prepared());
    // A refused prepare leaves nothing half-applied: the renderer must also
    // refuse the work that a prepared one would accept, rather than taking a
    // layout it will never realise.
    REQUIRE_FALSE(renderer->publish_layout(zoom_layout()));
    REQUIRE_FALSE(renderer->set_layout_rt(zoom_layout()));

    // Control: the same renderer object prepares fine at a grid that does fit,
    // so the refusal above is the cap and not a renderer that never prepares.
    REQUIRE(renderer->prepare(zero_latency_config()));
}

TEST_CASE("A staged redesign reaches the audio path and is reported",
          "[mask-renderer][contract]") {
    auto renderer = spectr::make_mask_renderer(MaskRenderMode::zero_latency);
    REQUIRE(renderer->prepare(zero_latency_config()));
    REQUIRE(renderer->publish_layout(zoom_layout()));
    settle(*renderer, 1);
    const auto first = renderer->active_generation();
    REQUIRE(first >= 1);

    // The audio-thread path: staging a layout must not design inline, but it
    // must still arrive.
    REQUIRE(renderer->set_layout_rt(zoom_layout(/*muted_band=*/5)));
    settle(*renderer, first + 1);
    REQUIRE(renderer->active_generation() > first);
}


// ── The swap handoff ───────────────────────────────────────────────────────
//
// A gesture republishes a layout while audio runs, so an impulse response is
// replaced underneath a running signal continuously. What that costs is a
// property of the HANDOFF, not of the mask change: a partitioned convolver
// carries a delay line of recent input spectra, and whether the incoming
// impulse inherits it or starts from silence decides whether recent material
// re-sounds. The rules below measure that directly.
//
// They deliberately do not measure the worst sample-to-sample step. That
// metric cannot see this defect: a step reads the signal's own slew, so an
// error smaller than the signal is invisible to it while being plainly
// audible, and it returns the same number whether a swap happened or not.

namespace {

/// Spectr's plant idiom: the named defect this file's swap rules forbid,
/// reinstated in the shipping binary by `SPECTR_SWAP_PLANT=history-reset`.
bool swap_planted() {
    const char* value = std::getenv("SPECTR_SWAP_PLANT");
    return value != nullptr && std::string(value) == "history-reset";
}

/// A negative-control row declares itself with this; the rule then REQUIREs
/// that its plant actually arrived. Without it the row is vacuously green the
/// moment the variable is stripped or misspelled.
void require_swap_plant_arrived(bool fired) {
    if (std::getenv("SPECTR_SWAP_PLANT_REQUIRED") == nullptr) return;
    INFO("this row is registered as a negative control, so its plant must fire; "
         "SPECTR_SWAP_PLANT="
         << (std::getenv("SPECTR_SWAP_PLANT") ? std::getenv("SPECTR_SWAP_PLANT")
                                              : "(unset)"));
    REQUIRE(fired);
}

/// A field with one band held at a gain, so the designed impulse has a TAIL.
/// A flat field designs to an impulse, whose swap costs nothing because there
/// is no history to lose -- it would score every rule below a perfect zero for
/// the wrong reason.
pulp::signal::SpectralBandLayout gain_layout(double gain_db, int band = 16) {
    auto layout = full_range_layout();
    layout.bands[static_cast<std::uint32_t>(band)].gain_db =
        static_cast<float>(gain_db);
    return layout;
}

/// Energy in `signal` that is NOT at `hz`, as an RMS, relative to `reference`.
///
/// Least-squares projection per window rather than an FFT magnitude, for the
/// reason `project_tone_amplitude` states: a windowed magnitude would measure
/// its own side lobes. A steady tone through a static mask leaves EXACTLY zero
/// here, which is what makes a non-zero reading attributable to the swap.
double non_tonal_residual(const std::vector<float>& signal, std::size_t begin,
                          std::size_t end, double hz, double reference,
                          std::size_t window = 512) {
    double sse = 0.0;
    std::size_t counted = 0;
    const double omega = 2.0 * kPi * hz / kSampleRate;
    for (std::size_t base = begin; base + window <= end; base += window) {
        double cc = 0, ss = 0, cs = 0, xc = 0, xs = 0, xx = 0;
        for (std::size_t i = 0; i < window; ++i) {
            const double phase = omega * static_cast<double>(base + i);
            const double c = std::cos(phase), s = std::sin(phase);
            const double v = static_cast<double>(signal[base + i]);
            cc += c * c; ss += s * s; cs += c * s;
            xc += v * c; xs += v * s; xx += v * v;
        }
        const double det = cc * ss - cs * cs;
        double residual = xx;
        if (std::abs(det) > 1e-12) {
            const double a = ( ss * xc - cs * xs) / det;
            const double b = (-cs * xc + cc * xs) / det;
            residual = xx - (a * xc + b * xs);
        }
        sse += residual > 0.0 ? residual : 0.0;
        counted += window;
    }
    if (counted == 0 || !(reference > 0.0)) return 0.0;
    return std::sqrt(sse / static_cast<double>(counted)) / reference;
}

constexpr float kGestureToneAmplitude = 0.25f;

struct Gesture {
    std::vector<float> out;
    double residual = 0.0;     ///< non-tonal energy injected during the gesture
    double achieved_db = 0.0;  ///< how much of the requested change arrived
};

/// Render a gesture: a steady tone at the dragged band's centre while the
/// layout is republished every `every_blocks` host blocks.
///
/// `span_db == 0` republishes a BIT-IDENTICAL layout, so everything it costs
/// is handoff cost and none of it is the mask changing.
///
/// Republishes through `publish_layout` rather than the worker, so the swap
/// cadence is exact and the rule does not depend on how a background thread
/// happened to be scheduled.
Gesture gesture(MaskRenderMode mode, int every_blocks, double span_db,
                double hold_db = -20.0, int band = 16, int host_block = 128) {
    const std::size_t lead = 24576, sweep = 48000, tail = 12288;
    const std::size_t total = lead + sweep + tail;
    const double hz = band_centre_hz(full_range_layout(), band);

    auto renderer = spectr::make_mask_renderer(mode);
    MaskRendererConfig config = zero_latency_config(2, host_block);
    REQUIRE(renderer->prepare(config));
    REQUIRE(renderer->publish_layout(gain_layout(hold_db, band)));

    const auto stimulus = tone(total, hz, kGestureToneAmplitude);
    Gesture result;
    result.out.assign(total, 0.0f);
    std::vector<float> in_l, in_r, blk_l, blk_r;
    int index = 0;
    for (std::size_t pos = 0; pos + static_cast<std::size_t>(host_block) <= total;
         pos += static_cast<std::size_t>(host_block), ++index) {
        if (every_blocks > 0 && pos >= lead && pos < lead + sweep
            && (index % every_blocks) == 0) {
            const double t = static_cast<double>(pos - lead)
                           / static_cast<double>(sweep);
            REQUIRE(renderer->publish_layout(
                gain_layout(hold_db + span_db * t, band)));
        }
        in_l.assign(stimulus.begin() + static_cast<std::ptrdiff_t>(pos),
                    stimulus.begin() + static_cast<std::ptrdiff_t>(pos + host_block));
        in_r = in_l;
        blk_l.assign(static_cast<std::size_t>(host_block), 0.0f);
        blk_r.assign(static_cast<std::size_t>(host_block), 0.0f);
        const float* in[2] = {in_l.data(), in_r.data()};
        float* out[2] = {blk_l.data(), blk_r.data()};
        REQUIRE(renderer->process(in, out, host_block));
        std::copy(blk_l.begin(), blk_l.end(),
                  result.out.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    result.residual = non_tonal_residual(result.out, lead, lead + sweep, hz,
                                         static_cast<double>(kGestureToneAmplitude));
    const double before = project_tone_amplitude(result.out, lead - 8192, 8192, hz);
    const double after  = project_tone_amplitude(result.out, lead + sweep - 8192, 8192, hz);
    result.achieved_db = amplitude_db(after, before);
    return result;
}

} // namespace

TEST_CASE("A redesign that changes nothing changes no sample",
          "[mask-renderer][audio]") {
    // The sharpest form of the rule. Republishing a layout the renderer is
    // ALREADY realising must cost the audio nothing at all -- and Spectr's
    // param-sync republishes on observed drift, most of which resolves to the
    // same mask, so this is the common case rather than a corner one.
    const bool plant = swap_planted();
    require_swap_plant_arrived(plant);

    const auto still  = gesture(MaskRenderMode::zero_latency, /*every=*/0, 0.0);
    const auto rerun  = gesture(MaskRenderMode::zero_latency, /*every=*/8, 0.0);

    // Control: the instrument can see a real change. Without this the rule is
    // satisfied equally by a renderer that works and by a harness measuring
    // nothing.
    const auto changed = gesture(MaskRenderMode::zero_latency, /*every=*/8, -40.0);
    INFO("control: a real 40 dB gesture must register");
    REQUIRE(changed.residual > 1.0e-4);
    REQUIRE(changed.achieved_db < -20.0);

    // Control: the unchanged run must still be driving the band it holds, or
    // the zero below is the zero of an unloaded renderer.
    REQUIRE(std::abs(rerun.achieved_db) < 1.0);

    // The floor this rule can resolve is the renderer's own single-precision
    // arithmetic, measured by the never-republished run rather than derived:
    // a static mask on a steady tone is a pure tone in exact arithmetic, so
    // whatever `still` reads IS the floor. The gate is a multiple of that
    // measured floor, not a constant chosen to pass.
    const double floor_ = still.residual;
    const double gate = std::max(4.0 * floor_, 1.0e-8);
    std::printf("\nunchanged-mask republication (Tracking)\n"
                "  never republished   residual %.3e   <- measured arithmetic floor\n"
                "  republished x%-3d    residual %.3e   (gate: <= %.3e)\n"
                "  a real 40 dB gesture residual %.3e   <- control, must register\n",
                still.residual, 375 / 8, rerun.residual, gate, changed.residual);

    if (plant) {
        // The planted handoff loses the delay line on every swap, so an
        // unchanged mask perturbs the audio anyway -- by orders of magnitude,
        // not by a margin.
        REQUIRE(rerun.residual > 100.0 * gate);
        return;
    }
    REQUIRE(floor_ < 1.0e-6);          // the floor itself must be a floor
    REQUIRE(rerun.residual <= gate);
}

TEST_CASE("A gesture costs no more artifact than the other mode",
          "[mask-renderer][audio]") {
    // Tracking replaces an impulse response; Mixing multiplies a table into a
    // WOLA frame. They are different realisations of the same drawn magnitude,
    // so Mixing's own artifact is the standard Tracking is held to -- an
    // absolute threshold would be a number chosen to pass.
    const bool plant = swap_planted();
    require_swap_plant_arrived(plant);

    struct Row { int every; };
    for (auto row : {Row{8}, Row{1}}) {
        const auto tracking = gesture(MaskRenderMode::zero_latency, row.every, -40.0);
        const auto mixing   = gesture(MaskRenderMode::linear_phase, row.every, -40.0);

        // Control: Mixing's residual is the denominator, so it must be real.
        // A zero there would make every ratio infinite or undefined.
        INFO("control: Mixing must register the gesture too");
        REQUIRE(mixing.residual > 1.0e-4);
        // Control: both modes must actually have moved the band.
        REQUIRE(tracking.achieved_db < -20.0);
        REQUIRE(mixing.achieved_db < -20.0);

        const double ratio = tracking.residual / mixing.residual;
        std::printf("\n40 dB gesture, republished every %d host block%s\n"
                    "  Tracking residual %.6f  (achieved %6.2f dB)\n"
                    "  Mixing   residual %.6f  (achieved %6.2f dB)\n"
                    "  ratio %.2f x   (gate: <= 2.5)\n",
                    row.every, row.every == 1 ? "" : "s",
                    tracking.residual, tracking.achieved_db,
                    mixing.residual, mixing.achieved_db, ratio);

        if (plant) {
            REQUIRE(ratio > 4.0);   // the planted handoff is far worse
        } else {
            REQUIRE(ratio <= 2.5);
        }
    }
}

TEST_CASE("A discrete mask change lands without a step the other mode would not make",
          "[mask-renderer][audio]") {
    // What the instantaneous handoff trades. Carrying the delay line means the
    // incoming impulse is correct from its first sample, so a large change
    // arrives as one step rather than as a smeared onset -- and the step is
    // what this rule bounds, against Mixing on the identical change.
    //
    // No plant row: reinstating the crossfade does NOT violate this rule. A
    // parallel fade is exactly the thing that smooths a boundary, so the
    // planted defect scores BETTER here while being far worse everywhere else.
    // Registering it as a negative control would be a vacuously green row.
    // The control this rule needs is an instrument one, below.
    constexpr int kHostBlock = 256;
    const std::size_t kTotal = 96000, kAt = 48000;

    auto worst_in = [](const std::vector<float>& x, std::size_t from, std::size_t to) {
        double w = 0.0;
        for (std::size_t i = from; i < to; ++i)
            w = std::max(w, std::abs(static_cast<double>(x[i])
                                     - static_cast<double>(x[i - 1])));
        return w;
    };

    // Render a mute landing mid-stream and return (output, steady slew).
    auto render_mute = [&](MaskRenderMode mode, int band, bool broadband) {
        auto renderer = spectr::make_mask_renderer(mode);
        REQUIRE(renderer->prepare(zero_latency_config(2, kHostBlock)));
        REQUIRE(renderer->publish_layout(full_range_layout()));
        std::vector<float> in(kTotal), out(kTotal, 0.0f), il, ir, bl, br;
        if (broadband) {
            unsigned int seed = 99991u;
            for (auto& v : in) {
                seed = seed * 1664525u + 1013904223u;
                v = 0.25f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
            }
        } else {
            in = tone(kTotal, 440.0, 0.5f);
        }
        bool fired = false;
        for (std::size_t pos = 0; pos + kHostBlock <= kTotal; pos += kHostBlock) {
            if (pos >= kAt && !fired) {
                REQUIRE(renderer->publish_layout(full_range_layout(band)));
                fired = true;
            }
            il.assign(in.begin() + static_cast<std::ptrdiff_t>(pos),
                      in.begin() + static_cast<std::ptrdiff_t>(pos + kHostBlock));
            ir = il;
            bl.assign(kHostBlock, 0.0f); br.assign(kHostBlock, 0.0f);
            const float* ip[2] = {il.data(), ir.data()};
            float* op[2] = {bl.data(), br.data()};
            REQUIRE(renderer->process(ip, op, kHostBlock));
            std::copy(bl.begin(), bl.end(), out.begin() + static_cast<std::ptrdiff_t>(pos));
        }
        // Control: the mute has to have reached the audio. Without it every
        // reading below is the ratio of an unchanged stream to itself -- 1.00,
        // and the rule passes having observed nothing.
        REQUIRE(fired);
        return out;
    };

    auto step_ratio = [&](const std::vector<float>& out) {
        const double steady = worst_in(out, 16000, 40000);
        REQUIRE(steady > 0.0);
        return worst_in(out, kAt, kAt + 8000) / steady;
    };

    // ── instrument control ────────────────────────────────────────────────
    // Displace one sample of a clean render by a known multiple of the steady
    // slew and require the metric to report it. This proves the metric can see
    // a large step at all; without it, the small numbers below are equally
    // consistent with a metric that reads small no matter what.
    {
        auto clean = render_mute(MaskRenderMode::linear_phase, 12, /*broadband=*/true);
        const double steady = worst_in(clean, 16000, 40000);
        clean[kAt + 100] += static_cast<float>(6.0 * steady);
        INFO("instrument control: an injected 6x step must be reported");
        REQUIRE(step_ratio(clean) >= 5.0);
    }

    // ── the rule ──────────────────────────────────────────────────────────
    double tracking = 0.0, mixing = 0.0;
    for (int band : {4, 12, 20, 28, 30}) {
        tracking = std::max(tracking,
                            step_ratio(render_mute(MaskRenderMode::zero_latency, band, true)));
        mixing = std::max(mixing,
                          step_ratio(render_mute(MaskRenderMode::linear_phase, band, true)));
    }

    // The narrow case, measured rather than asserted away: a sustained pure
    // tone beside the muted band is the one stimulus where this step is large,
    // because the whole signal is a single coherent sinusoid whose phase the
    // minimum-phase redesign rotates in one sample. Program material with
    // content either side of the band has slew of its own for the step to sit
    // inside, which is what the broadband rule above measures. Bounded loosely
    // so a REGRESSION past today's behaviour is caught while the upstream fix
    // -- carrying the delay line across a FADED swap, which would need both at
    // once -- would simply pass.
    const double tone_case = step_ratio(render_mute(MaskRenderMode::zero_latency, 17, false));

    std::printf("\nmute one band mid-stream\n"
                "  broadband  Tracking %.2f   Mixing %.2f   (gate: Tracking <= 2.5)\n"
                "  pure tone beside the muted band, Tracking %.2f  (known cost, gate <= 30)\n",
                tracking, mixing, tone_case);

    REQUIRE(mixing > 0.0);             // control: the reference is real
    REQUIRE(tracking <= 2.5);
    REQUIRE(tone_case <= 30.0);
}
