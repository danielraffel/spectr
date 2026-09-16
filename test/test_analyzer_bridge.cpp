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

constexpr int settled_samples() noexcept {
    return spectr::kSpectralLatency + spectr::kSpectralFftSize + 4096;
}

/// Fill a 2-channel buffer with a continuous sine. `start` lets callers
/// advance the phase across multiple calls so the waveform is seamless.
void fill_sine(std::vector<float>& ch0, std::vector<float>& ch1,
               double hz, std::size_t start = 0, double sr = kSampleRate,
               float amplitude = 1.0f)
{
    const double w = 2.0 * M_PI * hz / sr;
    for (std::size_t i = 0; i < ch0.size(); ++i) {
        const float s = amplitude * static_cast<float>(
            std::sin(w * static_cast<double>(i + start)));
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

void feed_sine(spectr::Spectr& plugin, double hz, int block, int total_samples,
               float amplitude = 1.0f) {
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
        fill_sine(in0, in1, hz, static_cast<std::size_t>(fed),
                  kSampleRate, amplitude);
        pulp::audio::BufferView<const float> iv(in_ptrs, 2, static_cast<std::size_t>(n));
        pulp::audio::BufferView<float>       ov(out_ptrs, 2, static_cast<std::size_t>(n));
        ctx.num_samples = n;
        plugin.process(ov, iv, midi_in, midi_out, ctx);
        fed += n;
    }
}

float peak_near_bin(const pulp::view::SpectrumData& spectrum,
                    int expected_bin, int radius = 2) {
    float peak_db = spectrum.floor_db;
    for (int bin = std::max(0, expected_bin - radius);
         bin <= std::min(spectrum.num_bins - 1, expected_bin + radius); ++bin)
        peak_db = std::max(peak_db, spectrum.magnitude_db[bin]);
    return peak_db;
}

void drain_analyzer(spectr::Spectr& plugin) {
    for (int poll = 0; poll < 64 && plugin.bridge().poll(); ++poll) {}
}

} // namespace

TEST_CASE("Analyzer bridge: spectrum populates after enough audio is fed") {
    PreparedSpectr s{};
    CHECK(s.processor->bridge().fft_size() == spectr::kAnalyzerFftSize);
    CHECK(s.processor->bridge().num_bins()
          <= pulp::view::SpectrumData::kMaxBins);
    feed_sine(*s.processor, 1000.0, 256, settled_samples());
    drain_analyzer(*s.processor);

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
    feed_sine(*s.processor, tone_hz, 256, settled_samples());
    drain_analyzer(*s.processor);

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

TEST_CASE("Analyzer bridge: post-DSP spectrum preserves peak-amplitude dBFS",
          "[analyzer][dbfs][oracle]") {
    // Select a bin-centred tone so spectral leakage cannot hide normalization,
    // coherent-gain, or single-sided scaling errors.  The default flat mask,
    // unity output trim, and 100% mix must preserve its amplitude through the
    // same post-engine signal path that feeds the native editor.
    constexpr int kToneBin = 256;
    const double tone_hz = static_cast<double>(kToneBin) * kSampleRate
                         / static_cast<double>(spectr::kAnalyzerFftSize);
    constexpr std::array<float, 4> kExpectedDb{
        0.0f, -6.0f, -30.0f, -60.0f};

    for (const float expected_db : kExpectedDb) {
        CAPTURE(expected_db, tone_hz);
        PreparedSpectr s{};
        const float amplitude = std::pow(10.0f, expected_db / 20.0f);
        feed_sine(*s.processor, tone_hz, 256, settled_samples(), amplitude);
        drain_analyzer(*s.processor);

        const auto& spectrum = s.processor->read_spectrum();
        REQUIRE(spectrum.num_bins > kToneBin);
        CHECK(peak_near_bin(spectrum, kToneBin)
              == Approx(expected_db).margin(0.15f));
    }
}

TEST_CASE("Analyzer bridge: Maximum profile retains the upper spectrum") {
    if constexpr (spectr::kSpectralFftSize > 8192) {
        PreparedSpectr s{};
        constexpr double tone_hz = 18000.0;
        feed_sine(*s.processor, tone_hz, 256, settled_samples());
        drain_analyzer(*s.processor);

        const auto& spec = s.processor->read_spectrum();
        REQUIRE(spec.num_bins > 0);
        const float bin_step = static_cast<float>(kSampleRate)
                             / static_cast<float>(spectr::kAnalyzerFftSize);
        const int expected = static_cast<int>(tone_hz / bin_step + 0.5);
        REQUIRE(expected < spec.num_bins);

        float peak_db = -200.0f;
        for (int k = std::max(0, expected - 5);
             k <= std::min(spec.num_bins - 1, expected + 5); ++k)
            peak_db = std::max(peak_db, spec.magnitude_db[k]);
        CHECK(peak_db > -40.0f);
    } else {
        SUCCEED("processing FFT fits the analyzer publication capacity");
    }
}

TEST_CASE("Analyzer bridge: meter snapshot is readable after audio") {
    PreparedSpectr s{};
    feed_sine(*s.processor, 1000.0, 256, settled_samples());

    // We don't assume any specific field on MultiChannelMeterData — just
    // that the triple-buffer returns a readable snapshot.
    const auto& meter = s.processor->read_meter();
    (void)meter;
    SUCCEED("meter snapshot readable");
}

TEST_CASE("Analyzer bridge: waveform capture populates") {
    PreparedSpectr s{};
    feed_sine(*s.processor, 500.0, 256, settled_samples());
    drain_analyzer(*s.processor);

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
    drain_analyzer(*s.processor);

    const auto& wave = s.processor->read_waveform();
    for (int i = 0; i < wave.num_samples; ++i) {
        CHECK(std::abs(wave.samples[i]) < 1e-4f);
    }

    const auto& spec = s.processor->read_spectrum();
    REQUIRE(spec.floor_db == Approx(-120.0f));
    for (int k = 0; k < spec.num_bins; ++k) {
        CHECK(spec.magnitude_db[k] == Approx(spec.floor_db).margin(0.001f));
    }
}

// ── The output-level readout ────────────────────────────────────────────────
//
// The product claim is "this is the level Spectr handed the host", and the one
// thing that can make it false is reading the meter from the wrong side of the
// output trim. `bridge_.process()` is fed the post-trim buffer in both render
// paths, so these gates read the trim BACK out of the published level: a
// change of the trim alone, with the input untouched, has to move the readout
// by exactly that many dB.
//
// Every assertion here is a NUMBER, not a flag. A readout suite that checks
// "a level is published" passes on a meter wired to the pre-trim buffer, which
// is the whole defect class.

namespace {

/// Feed a settled tone at `amplitude` with `trim_db` in force and report the
/// published reading. Mix is left at its default (fully wet) and the mask flat,
/// so the only thing between the tone and the meter is the trim.
spectr::Spectr::OutputLevelReading level_after_tone(float amplitude,
                                                    float trim_db) {
    PreparedSpectr s{};
    s.store.set_value(spectr::kOutputTrim, trim_db);
    feed_sine(*s.processor, 1000.0, 256, settled_samples(), amplitude);
    drain_analyzer(*s.processor);
    return s.processor->read_output_level();
}

} // namespace

TEST_CASE("output level: the readout is measured after the output trim",
          "[output-level]") {
    // -12 dBFS in. The tone is a sine, so its sample peak IS its amplitude,
    // which makes the expected dBFS figure exact rather than approximate.
    constexpr float kAmplitude = 0.25119f;   // -12 dBFS
    constexpr float kInputDb   = -12.0f;

    const auto unity = level_after_tone(kAmplitude, 0.0f);
    CAPTURE(unity.peak_db, unity.trim_db, unity.over);
    REQUIRE(std::isfinite(unity.peak_db));
    CHECK(unity.trim_db == Approx(0.0f).margin(1.0e-6f));
    CHECK(unity.peak_db == Approx(kInputDb).margin(0.35f));
    CHECK_FALSE(unity.over);

    // The load-bearing pair. Nothing about the input changed; only the trim.
    // A meter reading the pre-trim buffer returns the SAME number for both,
    // which is exactly what this difference refuses.
    for (const float trim_db : {-9.0f, -3.0f, 6.0f, 9.0f}) {
        const auto trimmed = level_after_tone(kAmplitude, trim_db);
        CAPTURE(trim_db, trimmed.peak_db, trimmed.trim_db);
        REQUIRE(std::isfinite(trimmed.peak_db));
        CHECK(trimmed.trim_db == Approx(trim_db).margin(1.0e-6f));
        CHECK(trimmed.peak_db - unity.peak_db == Approx(trim_db).margin(0.35f));
    }
}

TEST_CASE("output level: a trim that pushes past full scale reads over",
          "[output-level]") {
    // -12 dBFS in with +18 dB of trim is +6 dBFS out. Spectr clips nothing
    // itself -- it is float end to end -- so the sample values really do pass
    // 1.0 and this is the condition anything fixed-point downstream distorts
    // under. That is what the editor labels OVER.
    constexpr float kAmplitude = 0.25119f;

    const auto hot = level_after_tone(kAmplitude, 18.0f);
    CAPTURE(hot.peak_db, hot.over);
    REQUIRE(std::isfinite(hot.peak_db));
    CHECK(hot.peak_db == Approx(6.0f).margin(0.35f));
    CHECK(hot.over);

    // NEGATIVE CONTROL, same stimulus. The identical tone with a trim that
    // leaves headroom must NOT report over -- otherwise "over" would be a
    // constant and the gate above would pass on a stuck flag.
    const auto safe = level_after_tone(kAmplitude, 6.0f);
    CAPTURE(safe.peak_db, safe.over);
    CHECK(safe.peak_db == Approx(-6.0f).margin(0.35f));
    CHECK_FALSE(safe.over);
}

TEST_CASE("output level: the editor's trim control writes kOutputTrim",
          "[output-level]") {
    // The editor's Output control reaches the DSP through the flat `param_set`
    // verb, which takes a raw parameter id -- and the editor carries that id as
    // the literal 2, because JS has no access to this enum. Renumbering
    // kOutputTrim would therefore leave the control writing some OTHER
    // parameter, with the panel, the readout and every JS gate still green: the
    // slider would move, the meter would not follow it, and nothing would say
    // why. Pin the two together so the renumbering fails here instead.
    STATIC_REQUIRE(static_cast<int>(spectr::kOutputTrim) == 2);

    // And prove the write actually lands, through the same StateStore path the
    // bridge handler uses -- otherwise the constant above would be pinning an
    // id nothing reads.
    PreparedSpectr s{};
    s.store.set_value(static_cast<pulp::state::ParamID>(2), 7.5f);
    CHECK(s.processor->read_output_level().trim_db == Approx(7.5f));
}

TEST_CASE("output level: digital silence reads as -inf, never as 0 dBFS",
          "[output-level]") {
    // A meter that reports 0 for silence cannot be told apart from one
    // reporting full scale, and the editor prints "--" only because this is
    // non-finite. Feeding a real, settled silence is the point: the readout
    // has to survive the path, not just an unprepared default.
    PreparedSpectr s{};
    s.store.set_value(spectr::kOutputTrim, 0.0f);
    feed_sine(*s.processor, 1000.0, 256, settled_samples(), 0.0f);
    drain_analyzer(*s.processor);

    const auto silent = s.processor->read_output_level();
    CAPTURE(silent.peak_db, silent.over);
    CHECK_FALSE(std::isfinite(silent.peak_db));
    CHECK(silent.peak_db < 0.0f);
    CHECK_FALSE(silent.over);

    // CONTROL on the instrument: the same fixture with real audio in it has to
    // publish a finite level, or "not finite" above would be a fact about the
    // fixture rather than about silence.
    PreparedSpectr loud{};
    loud.store.set_value(spectr::kOutputTrim, 0.0f);
    feed_sine(*loud.processor, 1000.0, 256, settled_samples(), 0.5f);
    drain_analyzer(*loud.processor);
    CHECK(std::isfinite(loud.processor->read_output_level().peak_db));
}
