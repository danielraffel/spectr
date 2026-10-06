// Freeze through the shared GPU renderer: the held wet source must reach the
// output exactly as it does through the CPU linear-phase renderer, delayed by
// the shared renderer's additional latency, on the GPU path and on the CPU
// fallback path, across a mix change and a realtime reset.
//
// Each renderer gets its own FreezeSource, fed identical input and frozen at
// the same sample, so the two sources are identical and any difference is the
// renderer's. A third, unfrozen oracle run is the instrument's positive
// control: the frozen reference must differ from it after the input changes,
// or this test could not see a dropped source at all.
//
// SPECTR_FREEZE_PLANT_DROP_SOURCE=1 installs no source on the shared renderer
// (what a renderer that ignores set_wet_source does). It must fail.
#define main partition_controls_main
#include "test_shared_spectral_partitions.cpp"
#undef main
#include <spectr/experimental/shared_spectral_renderer.hpp>
#include <spectr/freeze_source.hpp>
#include <cstdlib>

namespace {
float signal_at(unsigned i, unsigned ch, unsigned switch_at, unsigned& rng) {
    constexpr double pi = 3.14159265358979323846;
    const double t = double(i) / 48000.0;
    rng = rng * 1664525u + 1013904223u;
    const float noise = (float(rng >> 8) / 16777216.f - .5f) * 0.02f;
    if (i < switch_at)
        return float(0.3 * std::sin(2 * pi * 440.0 * t) + 0.15 * std::sin(2 * pi * 1320.0 * t + ch))
               + noise;
    // After the switch the live input is a different sound entirely, so a
    // held output and a live one cannot agree by accident.
    return float(0.25 * std::sin(2 * pi * 3100.0 * t)) + noise;
}
}

int freeze_case(bool cpu_only, double hold_seconds, bool plant) {
    using Renderer = spectr::experimental::SharedSpectralMaskRenderer;
    constexpr int grid = 8192;
    spectr::MaskRendererConfig config{.design_grid_size = grid, .analysis_hop = grid / 4,
        .channels = 2, .max_block = 512, .sample_rate = 48000, .initial_mix = 1.f,
        .mix_ramp_samples = 64};
    auto renderer = std::make_unique<Renderer>(cpu_only);
    auto oracle = spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    auto live = spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    if (!renderer->prepare(config) || !oracle->prepare(config) || !live->prepare(config)) return 90;
    spectr::MaskRenderer::Layout layout;
    layout.active_bands = 2;
    layout.transition_frames = 0;
    layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    layout.bands[0].gain_db = -3;
    layout.bands[1].gain_db = -9;
    if (!renderer->publish_layout(layout) || !oracle->publish_layout(layout)
        || !live->publish_layout(layout)) return 91;
    renderer->reset(); oracle->reset(); live->reset();

    spectr::FreezeSource gpu_source, cpu_source;
    if (!gpu_source.prepare(48000.0, 2) || !cpu_source.prepare(48000.0, 2)) return 92;
    gpu_source.set_hold_seconds(hold_seconds);
    cpu_source.set_hold_seconds(hold_seconds);
    if (!oracle->set_wet_source(&cpu_source)) return 93;
    if (!plant && !renderer->set_wet_source(&gpu_source)) {
        std::cout << "shared renderer refused the freeze source\n";
        return 94;
    }

    const auto added = Renderer::additional_latency(config);
    constexpr unsigned total = 48000 * 3, freeze_at = 28800, switch_at = 62400,
                       mix_at = 81111, reset_at = 100003;
    std::vector<float> input(total * 2), actual(total * 2), reference(total * 2), unfrozen(total * 2);
    unsigned rng = 7;
    for (unsigned i = 0; i < total; ++i)
        for (unsigned ch = 0; ch < 2; ++ch) input[ch * total + i] = signal_at(i, ch, switch_at, rng);

    unsigned pos = 0, part = 0;
    double error = 0;
    callback_allocations = 0;
    constexpr unsigned parts[] = {31, 127, 512, 128, 1, 256};
    while (pos < total) {
        if (pos == freeze_at) { gpu_source.set_frozen(true); cpu_source.set_frozen(true); }
        if (pos == mix_at) { renderer->set_mix(.7f); oracle->set_mix(.7f); live->set_mix(.7f); }
        if (pos == reset_at) {
            guard_allocations = true; renderer->reset(); guard_allocations = false;
            oracle->reset(); live->reset();
        }
        unsigned n = std::min(parts[part++ % 6], total - pos);
        for (auto event : {freeze_at, mix_at, reset_at}) if (pos < event) n = std::min(n, event - pos);
        const float* in[] = {input.data() + pos, input.data() + total + pos};
        float* out[] = {actual.data() + pos, actual.data() + total + pos};
        float* ref[] = {reference.data() + pos, reference.data() + total + pos};
        float* liv[] = {unfrozen.data() + pos, unfrozen.data() + total + pos};
        if (!oracle->process(in, ref, n) || !live->process(in, liv, n)) return 95;
        guard_allocations = true;
        const bool ok = renderer->process(in, out, n);
        guard_allocations = false;
        if (!ok || callback_allocations) return 96;
        for (unsigned ch = 0; ch < 2; ++ch)
            for (unsigned i = pos; i < pos + n; ++i) {
                const unsigned epoch = i >= reset_at ? reset_at : 0;
                const double expected = i < epoch + added ? 0 : reference[ch * total + i - added];
                const double residual = std::abs(double(actual[ch * total + i]) - expected);
                if (!std::isfinite(residual)) return 97;
                error = std::max(error, residual);
            }
        pos += n;
        // Functional pacing so the worker completes GPU hops; not a deadline test.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Positive control: after the input changes, the frozen CPU reference must
    // be far from the unfrozen one, or a dropped source would look identical.
    double held_vs_live = 0, held_rms = 0;
    unsigned counted = 0;
    for (unsigned i = switch_at + 9600; i < reset_at; ++i) {
        const double d = reference[i] - unfrozen[i];
        held_vs_live += d * d;
        held_rms += double(reference[i]) * reference[i];
        ++counted;
    }
    held_vs_live = std::sqrt(held_vs_live / counted);
    held_rms = std::sqrt(held_rms / counted);
    const auto snapshot = renderer->snapshot();
    std::cout << "freeze cpu_only=" << cpu_only << " hold_seconds=" << hold_seconds
              << " plant=" << plant << " extra_latency=" << added
              << " max_error=" << error << " held_rms=" << held_rms
              << " held_vs_unfrozen_rms=" << held_vs_live
              << " held_audible=" << cpu_source.hold_audible()
              << " gpu=" << snapshot.gpu_delivered << " cpu=" << snapshot.cpu_fallback
              << " state=" << unsigned(snapshot.state) << " lost=" << snapshot.lost_records
              << " callback_allocations=" << callback_allocations << '\n';
    if (error >= 1e-4) return 103;  // the held output differs from the CPU renderer's
    if (!cpu_source.hold_audible() || !gpu_source.hold_audible()) return 98;
    if (held_vs_live < 0.02) return 99;  // the instrument cannot see a dropped source
    if (snapshot.lost_records) return 100;
    if (cpu_only ? (snapshot.gpu_delivered != 0 || !snapshot.cpu_fallback)
                 : (snapshot.gpu_delivered == 0 || snapshot.state != Renderer::ProviderState::SharedReady))
        return 101;
    if (!renderer->release()) return 102;
    return 0;
}

int main() {
    const bool plant = std::getenv("SPECTR_FREEZE_PLANT_DROP_SOURCE") != nullptr;
    // Spectral hold (below the loop threshold) and an audio loop.
    for (double hold : {0.1, 0.5})
        for (bool cpu_only : {false, true}) {
            const auto rc = freeze_case(cpu_only, hold, plant);
            if (rc) { std::cerr << "freeze_failure=" << rc << '\n'; return rc; }
        }
    return 0;
}
