#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/audio/buffer.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/format/quirk_apply.hpp>
#include <pulp/host/plugin_slot.hpp>
#include <pulp/midi/buffer.hpp>

#include "spectr/spectr.hpp"
#include "spectr/param_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::pair<std::uint32_t, std::string>> expected_host_parameters() {
    std::vector<std::pair<std::uint32_t, std::string>> expected{
        {pulp::format::kSynthesizedBypassParamId, "Bypass"},
        {spectr::kMix, "Mix"},
        {spectr::kOutputTrim, "Output"},
    };
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band) {
        char name[32];
        std::snprintf(name, sizeof(name), "Band %02zu Gain", band + 1);
        expected.emplace_back(spectr::band_gain_param_id(band), name);
    }
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band) {
        char name[32];
        std::snprintf(name, sizeof(name), "Band %02zu Mute", band + 1);
        expected.emplace_back(spectr::band_mute_param_id(band), name);
    }
    expected.insert(expected.end(), {
        {spectr::kParamMorph, "A/B Morph"},
        {spectr::kParamViewportCenter, "Viewport Center"},
        {spectr::kParamViewportWidth, "Viewport Width"},
        {spectr::kParamBandCount, "Band Count"},
        {spectr::kParamMotionMode, "Motion Mode"},
        {spectr::kParamAnalyzerMode, "Analyzer Mode"},
        {spectr::kParamEditMode, "Edit Mode"},
        {spectr::kParamVisualization, "Visualization"},
    });
    return expected;
}

void check_host_parameter_contract(
    const std::vector<pulp::host::HostParamInfo>& parameters) {
    const auto expected = expected_host_parameters();
    REQUIRE(expected.size() == spectr::kSurfaceParamCount + 1);
    REQUIRE(parameters.size() == expected.size());

    std::vector<std::uint32_t> actual_ids;
    actual_ids.reserve(parameters.size());
    for (const auto& parameter : parameters)
        actual_ids.push_back(parameter.id);
    std::ranges::sort(actual_ids);
    CHECK(std::ranges::adjacent_find(actual_ids) == actual_ids.end());

    for (const auto& [id, name] : expected) {
        const auto it = std::ranges::find(parameters, id,
            &pulp::host::HostParamInfo::id);
        INFO("expected host parameter " << name << " id=" << id);
        REQUIRE(it != parameters.end());
        CHECK(it->name == name);
        CHECK(it->flags.is_bypass
              == (id == pulp::format::kSynthesizedBypassParamId));
        if (!it->flags.is_bypass) {
            CHECK(it->flags.automatable);
            CHECK_FALSE(it->flags.read_only);
            CHECK_FALSE(it->flags.hidden);
        }
    }

    // The static surface stays complete when the current layout shows only
    // 32 bands. Hidden slots are host-visible state lanes whose DSP effect is
    // deliberately inert until their band enters the visible layout.
    CHECK(std::ranges::find(parameters, spectr::band_gain_param_id(63),
          &pulp::host::HostParamInfo::id) != parameters.end());
    CHECK(std::ranges::find(parameters, spectr::band_mute_param_id(63),
          &pulp::host::HostParamInfo::id) != parameters.end());
}

std::vector<std::uint8_t> make_all_muted_state() {
    pulp::format::HeadlessHost author(spectr::create_spectr);
    author.prepare(48000.0, 512);
    auto* processor = dynamic_cast<spectr::Spectr*>(author.processor());
    REQUIRE(processor != nullptr);
    spectr::BandField muted;
    for (auto& band : muted.bands) band.muted = true;
    processor->replace_field(muted);
    auto state = author.save_state();
    REQUIRE_FALSE(state.empty());
    return state;
}

std::vector<std::uint8_t> make_three_island_state() {
    pulp::format::HeadlessHost author(spectr::create_spectr);
    author.prepare(48000.0, 512);
    auto* processor = dynamic_cast<spectr::Spectr*>(author.processor());
    REQUIRE(processor != nullptr);
    spectr::BandField islands;
    for (auto& band : islands.bands) band.muted = true;
    for (const float hz : {304.6875f, 1201.171875f, 3498.046875f})
        islands.bands[processor->viewport().band_for_hz(hz, 32)].muted = false;
    REQUIRE(processor->replace_processing_state(
        islands, spectr::Viewport{}, spectr::Layout::Bands32));
    auto state = author.save_state();
    REQUIRE_FALSE(state.empty());
    return state;
}

void check_three_islands(pulp::host::PluginSlot& slot) {
    if constexpr (SPECTR_FFT_SIZE < 8192) return;

    constexpr double pi = 3.14159265358979323846;
    constexpr int sample_rate = 48000;
    constexpr int block_size = 512;
    constexpr int analysis_size = SPECTR_FFT_SIZE;
    constexpr int bin_scale = analysis_size / 8192;
    constexpr double amplitude = 0.04;
    const std::array<int, 3> kept_bins{
        52 * bin_scale, 205 * bin_scale, 597 * bin_scale};
    const std::array<int, 3> rejected_bins{
        102 * bin_scale, 376 * bin_scale, 1195 * bin_scale};
    const auto hz_for_bin = [](int bin) {
        return static_cast<double>(bin) * sample_rate / analysis_size;
    };
    REQUIRE(slot.restore_state(make_three_island_state()));

    const auto total_samples = static_cast<std::size_t>(
        SPECTR_EXPECTED_LATENCY + SPECTR_FFT_SIZE * 4);
    const auto padded_samples =
        ((total_samples + block_size - 1) / block_size) * block_size;
    std::vector<float> source_left(padded_samples, 0.0f);
    std::vector<float> rendered_left(padded_samples, 0.0f);
    std::vector<float> rendered_right(padded_samples, 0.0f);
    for (std::size_t sample = 0; sample < padded_samples; ++sample) {
        double value = 0.0;
        for (const auto bin : kept_bins)
            value += amplitude * std::sin(
                2.0 * pi * hz_for_bin(bin) * sample / sample_rate);
        for (const auto bin : rejected_bins)
            value += amplitude * std::sin(
                2.0 * pi * hz_for_bin(bin) * sample / sample_rate);
        source_left[sample] = static_cast<float>(value);
    }

    std::vector<float> left(block_size), right(block_size);
    std::vector<float> out_left(block_size), out_right(block_size);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {out_left.data(), out_right.data()};
    auto input = pulp::audio::BufferView<const float>(inputs, 2, block_size);
    auto output = pulp::audio::BufferView<float>(outputs, 2, block_size);
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::host::ParameterEventQueue parameter_events;
    for (std::size_t offset = 0; offset < padded_samples; offset += block_size) {
        std::copy_n(source_left.data() + offset, block_size, left.data());
        for (int sample = 0; sample < block_size; ++sample)
            right[sample] = -0.6f * left[sample];
        slot.process(output, input, midi_in, midi_out, parameter_events, block_size);
        std::copy_n(out_left.data(), block_size, rendered_left.data() + offset);
        std::copy_n(out_right.data(), block_size, rendered_right.data() + offset);
    }

    const auto output_start = padded_samples - analysis_size;
    const auto source_start = output_start
                            - static_cast<std::size_t>(SPECTR_EXPECTED_LATENCY);
    const auto projection = [=](const std::vector<float>& signal,
                                std::size_t start,
                                int bin) {
        std::complex<double> sum{};
        for (int sample = 0; sample < analysis_size; ++sample) {
            const auto phase = -2.0 * pi * bin * sample / analysis_size;
            sum += static_cast<double>(
                       signal[start + static_cast<std::size_t>(sample)])
                 * std::complex<double>(std::cos(phase), std::sin(phase));
        }
        return sum * (2.0 / analysis_size);
    };
    for (const auto bin : kept_bins) {
        const auto input_bin = projection(source_left, source_start, bin);
        const auto left_bin = projection(rendered_left, output_start, bin);
        const auto right_bin = projection(rendered_right, output_start, bin);
        INFO("built artifact retained FFT bin " << bin);
        CHECK(std::abs(left_bin) / std::abs(input_bin)
              == Catch::Approx(1.0).margin(0.02));
        const auto stereo_ratio = right_bin / left_bin;
        CHECK(stereo_ratio.real() == Catch::Approx(-0.6).margin(0.002));
        CHECK(stereo_ratio.imag() == Catch::Approx(0.0).margin(0.002));
    }
    for (const auto bin : rejected_bins) {
        const auto input_bin = projection(source_left, source_start, bin);
        INFO("built artifact rejected FFT bin " << bin);
        CHECK(std::abs(projection(rendered_left, output_start, bin))
                  / std::abs(input_bin) < 1.0e-5);
        CHECK(std::abs(projection(rendered_right, output_start, bin))
                  / std::abs(input_bin) < 1.0e-5);
    }
}

void check_built_artifact(const std::filesystem::path& bundle,
                          pulp::host::PluginFormat format) {
    namespace fs = std::filesystem;
    REQUIRE(fs::exists(bundle));

    pulp::host::PluginInfo info;
    info.name = "Spectr";
    info.path = bundle.string();
    info.format = format;
    auto slot = pulp::host::PluginSlot::load(info);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->is_loaded());
    REQUIRE(slot->prepare(48000.0, 512));
    CHECK(slot->latency_samples() == SPECTR_EXPECTED_LATENCY);
    CHECK(slot->info().name == "Spectr");
    const auto parameters = slot->parameters();
    check_host_parameter_contract(parameters);
    for (const auto& parameter : parameters) {
        INFO("parameter " << parameter.name << " id=" << parameter.id
             << " default=" << parameter.default_value
             << " current=" << slot->get_parameter(parameter.id));
        CHECK(slot->get_parameter(parameter.id)
              == Catch::Approx(parameter.default_value));
    }
    CHECK(slot->has_editor());

    constexpr int block_size = 512;
    constexpr int settle_samples = SPECTR_EXPECTED_LATENCY + SPECTR_FFT_SIZE;
    constexpr int settle_blocks =
        (settle_samples + block_size - 1) / block_size;
    static_assert(settle_blocks > 0,
                  "built-artifact settling must process at least one block");

    INFO("artifact profile: fft=" << SPECTR_FFT_SIZE
         << ", latency=" << SPECTR_EXPECTED_LATENCY
         << ", settle_blocks=" << settle_blocks);

    std::vector<float> left(block_size), right(block_size);
    std::vector<float> out_left(block_size), out_right(block_size);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {out_left.data(), out_right.data()};
    auto input = pulp::audio::BufferView<const float>(inputs, 2, block_size);
    auto output = pulp::audio::BufferView<float>(outputs, 2, block_size);
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::host::ParameterEventQueue parameter_events;

    // Clear both fixed WOLA latency and one complete FFT frame before judging
    // the pass path. This remains valid for Live, Balanced, and Maximum builds.
    float final_peak = 0.0f;
    for (int block = 0; block < settle_blocks; ++block) {
        for (int sample = 0; sample < block_size; ++sample) {
            const auto absolute = block * block_size + sample;
            const float value = 0.4f * std::sin(
                2.0 * 3.14159265358979323846 * 997.0
                * static_cast<double>(absolute) / 48000.0);
            left[static_cast<std::size_t>(sample)] = value;
            right[static_cast<std::size_t>(sample)] = -value;
        }
        slot->process(output, input, midi_in, midi_out, parameter_events,
                      block_size);
        final_peak = 0.0f;
        for (const auto value : out_left)
            final_peak = std::max(final_peak, std::abs(value));
    }
    CHECK(final_peak > 0.1f);

    const auto state = slot->save_state();
    REQUIRE_FALSE(state.empty());
    CHECK(slot->restore_state(state));

    check_three_islands(*slot);

    // Cross the real format boundary with authored structured Spectr state,
    // then prove categorical -infinity is exact zero in the built artifact.
    // The same profile-derived budget clears frame-boundary adoption, fixed
    // latency, and one complete WOLA frame after the state change. Testing the
    // final block avoids mistaking expected transition history for leakage from
    // a zero-valued spectral mask.
    REQUIRE(slot->restore_state(make_all_muted_state()));
    for (int block = 0; block < settle_blocks; ++block) {
        for (int sample = 0; sample < block_size; ++sample) {
            const auto absolute =
                (block + settle_blocks) * block_size + sample;
            const float value = 0.4f * std::sin(
                2.0 * 3.14159265358979323846 * 997.0
                * static_cast<double>(absolute) / 48000.0);
            left[static_cast<std::size_t>(sample)] = value;
            right[static_cast<std::size_t>(sample)] = -value;
        }
        slot->process(output, input, midi_in, midi_out, parameter_events,
                      block_size);
    }
    for (const auto value : out_left) CHECK(value == 0.0f);
    for (const auto value : out_right) CHECK(value == 0.0f);
    slot->release();
}

} // namespace

#if defined(SPECTR_HAVE_TEST_CLAP)
TEST_CASE("Pulp host loads and processes the built Spectr CLAP artifact") {
    check_built_artifact(SPECTR_TEST_CLAP_PATH,
                         pulp::host::PluginFormat::CLAP);
}
#endif

#if defined(SPECTR_HAVE_TEST_VST3)
TEST_CASE("Pulp host loads and processes the built Spectr VST3 artifact") {
    check_built_artifact(SPECTR_TEST_VST3_PATH,
                         pulp::host::PluginFormat::VST3);
}
#endif

#if defined(SPECTR_HAVE_WEBVIEW_REFERENCE_TEST_CLAP)
TEST_CASE("Pulp host loads and processes the frozen Spectr WebView CLAP artifact") {
    check_built_artifact(SPECTR_WEBVIEW_REFERENCE_TEST_CLAP_PATH,
                         pulp::host::PluginFormat::CLAP);
}
#endif

#if defined(SPECTR_HAVE_WEBVIEW_REFERENCE_TEST_VST3)
TEST_CASE("Pulp host loads and processes the frozen Spectr WebView VST3 artifact") {
    check_built_artifact(SPECTR_WEBVIEW_REFERENCE_TEST_VST3_PATH,
                         pulp::host::PluginFormat::VST3);
}
#endif
