#include <catch2/catch_test_macros.hpp>

#include <pulp/audio/buffer.hpp>
#include <pulp/host/plugin_slot.hpp>
#include <pulp/midi/buffer.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

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
    CHECK(slot->info().name == "Spectr");
    const auto parameters = slot->parameters();
    CHECK(parameters.size() == 7);
    for (const std::string_view expected : {
             "Mix", "Output", "Response", "Engine", "Bands", "Morph"}) {
        CHECK(std::ranges::any_of(parameters, [expected](const auto& parameter) {
            return parameter.name == expected && !parameter.flags.is_bypass;
        }));
    }
    CHECK(std::ranges::count_if(parameters, [](const auto& parameter) {
        return parameter.flags.is_bypass;
    }) == 1);
    CHECK(slot->has_editor());

    std::vector<float> left(512), right(512), out_left(512), out_right(512);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {out_left.data(), out_right.data()};
    auto input = pulp::audio::BufferView<const float>(inputs, 2, 512);
    auto output = pulp::audio::BufferView<float>(outputs, 2, 512);
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::host::ParameterEventQueue parameter_events;

    float final_peak = 0.0f;
    for (int block = 0; block < 8; ++block) {
        for (int sample = 0; sample < 512; ++sample) {
            const auto absolute = block * 512 + sample;
            const float value = 0.4f * std::sin(
                2.0 * 3.14159265358979323846 * 997.0
                * static_cast<double>(absolute) / 48000.0);
            left[static_cast<std::size_t>(sample)] = value;
            right[static_cast<std::size_t>(sample)] = -value;
        }
        slot->process(output, input, midi_in, midi_out, parameter_events, 512);
        final_peak = 0.0f;
        for (const auto value : out_left)
            final_peak = std::max(final_peak, std::abs(value));
    }
    CHECK(final_peak > 0.1f);

    const auto state = slot->save_state();
    REQUIRE_FALSE(state.empty());
    CHECK(slot->restore_state(state));
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
