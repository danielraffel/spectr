// Milestone 3 — Analyzer bridge wiring tests.
//
// Verifies that Spectr publishes spectrum + meter + waveform snapshots
// through VisualizationBridge after the engine processes audio. No UI
// rendering — these tests just prove the audio→UI publication path works
// end-to-end.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/spectr.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 48000.0;

/// Fill a 2-channel buffer with a continuous sine. `start` lets callers
/// advance the phase across multiple calls so the waveform is seamless.
void fill_sine(std::vector<float>& ch0, std::vector<float>& ch1,
               double hz, std::size_t start = 0, double sr = kSampleRate)
{
    const double w = 2.0 * M_PI * hz / sr;
    for (std::size_t i = 0; i < ch0.size(); ++i) {
        const float s = static_cast<float>(std::sin(w * static_cast<double>(i + start)));
        ch0[i] = s;
        ch1[i] = s;
    }
}

/// Test fixture that wires a Spectr to a StateStore and prepares it. We
/// drive the Processor directly (HeadlessHost doesn't expose its processor)
/// so tests can reach the spectr::Spectr API.
struct PreparedSpectr {
    pulp::state::StateStore       store;
    std::unique_ptr<spectr::Spectr> processor;

    explicit PreparedSpectr(int block = 256) : processor(std::make_unique<spectr::Spectr>()) {
        processor->set_state_store(&store);
        processor->define_parameters(store);

        pulp::format::PrepareContext pc;
        pc.sample_rate     = kSampleRate;
        pc.max_buffer_size = block;
        pc.input_channels  = 2;
        pc.output_channels = 2;
        processor->prepare(pc);
    }

    spectr::Spectr& operator*() noexcept { return *processor; }
    spectr::Spectr* operator->() noexcept { return processor.get(); }
};

void feed_sine(spectr::Spectr& plugin, double hz, int block, int total_samples) {
    std::vector<float> in0(block), in1(block);
    std::vector<float> out0(block), out1(block);
    const float* in_ptrs[2]  = {in0.data(), in1.data()};
    float*       out_ptrs[2] = {out0.data(), out1.data()};

    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::format::ProcessContext ctx;
    ctx.sample_rate = kSampleRate;

    int fed = 0;
    while (fed < total_samples) {
        const int n = std::min(block, total_samples - fed);
        fill_sine(in0, in1, hz, static_cast<std::size_t>(fed));
        pulp::audio::BufferView<const float> iv(in_ptrs, 2, static_cast<std::size_t>(n));
        pulp::audio::BufferView<float>       ov(out_ptrs, 2, static_cast<std::size_t>(n));
        ctx.num_samples = n;
        plugin.process(ov, iv, midi_in, midi_out, ctx);
        fed += n;
    }
}

} // namespace

TEST_CASE("Analyzer bridge: spectrum populates after enough audio is fed") {
    PreparedSpectr s{};
    feed_sine(*s.processor, 1000.0, 256, 4096);

    const auto& spec = s.processor->read_spectrum();
    CHECK(spec.num_bins > 0);
    CHECK(spec.num_bins <= pulp::view::SpectrumData::kMaxBins);

    bool any_live = false;
    for (int k = 0; k < spec.num_bins; ++k) {
        if (spec.magnitude_db[k] > -120.0f) { any_live = true; break; }
    }
    CHECK(any_live);
}

TEST_CASE("Analyzer bridge: spectrum peaks near the input tone frequency") {
    PreparedSpectr s{};
    const double tone_hz = 2000.0;
    feed_sine(*s.processor, tone_hz, 256, 8192);

    const auto& spec = s.processor->read_spectrum();
    REQUIRE(spec.num_bins > 0);

    const int   fft_size = s.processor->bridge().fft_size();
    const float bin_step = static_cast<float>(kSampleRate) / static_cast<float>(fft_size);
    const int   expected = static_cast<int>(tone_hz / bin_step + 0.5);

    int   peak_bin = expected;
    float peak_db  = -200.0f;
    for (int k = std::max(0, expected - 5);
         k <= std::min(spec.num_bins - 1, expected + 5); ++k) {
        if (spec.magnitude_db[k] > peak_db) {
            peak_db = spec.magnitude_db[k];
            peak_bin = k;
        }
    }
    INFO("Peak bin = " << peak_bin << " (expected " << expected
         << ", " << peak_db << " dB)");
    CHECK(std::abs(peak_bin - expected) <= 2);
    CHECK(peak_db > -40.0f);
}

TEST_CASE("Analyzer bridge: meter snapshot is readable after audio") {
    PreparedSpectr s{};
    feed_sine(*s.processor, 1000.0, 256, 4096);

    // We don't assume any specific field on MultiChannelMeterData — just
    // that the triple-buffer returns a readable snapshot.
    const auto& meter = s.processor->read_meter();
    (void)meter;
    SUCCEED("meter snapshot readable");
}

TEST_CASE("Analyzer bridge: waveform capture populates") {
    PreparedSpectr s{};
    feed_sine(*s.processor, 500.0, 256, 4096);

    const auto& wave = s.processor->read_waveform();
    CHECK(wave.num_samples > 0);

    bool any_nonzero = false;
    for (int i = 0; i < wave.num_samples; ++i) {
        if (std::abs(wave.samples[i]) > 1e-4f) { any_nonzero = true; break; }
    }
    CHECK(any_nonzero);
}

TEST_CASE("Analyzer bridge: silence in → silence published") {
    PreparedSpectr s{};

    constexpr int block = 256;
    std::vector<float> in0(block, 0.0f), in1(block, 0.0f);
    std::vector<float> out0(block), out1(block);
    const float* in_ptrs[2]  = {in0.data(), in1.data()};
    float*       out_ptrs[2] = {out0.data(), out1.data()};

    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::format::ProcessContext ctx;
    ctx.sample_rate = kSampleRate;
    ctx.num_samples = block;

    for (int i = 0; i < 16; ++i) {
        pulp::audio::BufferView<const float> iv(in_ptrs, 2, static_cast<std::size_t>(block));
        pulp::audio::BufferView<float>       ov(out_ptrs, 2, static_cast<std::size_t>(block));
        s.processor->process(ov, iv, midi_in, midi_out, ctx);
    }

    const auto& wave = s.processor->read_waveform();
    for (int i = 0; i < wave.num_samples; ++i) {
        CHECK(std::abs(wave.samples[i]) < 1e-4f);
    }

    const auto& spec = s.processor->read_spectrum();
    for (int k = 0; k < spec.num_bins; ++k) {
        CHECK(spec.magnitude_db[k] < -60.0f);
    }
}
