#include <catch2/catch_test_macros.hpp>

#include <pulp/audio/buffer.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/host/plugin_slot.hpp>
#include <pulp/midi/buffer.hpp>

#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

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
    CHECK(parameters.size() == 3);
    for (const std::string_view expected : {
             "Mix", "Output"}) {
        CHECK(std::ranges::any_of(parameters, [expected](const auto& parameter) {
            return parameter.name == expected && !parameter.flags.is_bypass;
        }));
    }
    CHECK_FALSE(std::ranges::any_of(parameters, [](const auto& parameter) {
        return parameter.name == "Morph";
    }));
    CHECK(std::ranges::count_if(parameters, [](const auto& parameter) {
        return parameter.flags.is_bypass;
    }) == 1);
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

TEST_CASE("Pulp host loads and processes the built Spectr CLAP artifact") {
    check_built_artifact(SPECTR_TEST_CLAP_PATH,
                         pulp::host::PluginFormat::CLAP);
}

TEST_CASE("Pulp host loads and processes the built Spectr VST3 artifact") {
    check_built_artifact(SPECTR_TEST_VST3_PATH,
                         pulp::host::PluginFormat::VST3);
}
