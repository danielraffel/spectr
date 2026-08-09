#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_harness.hpp>
#include "spectr/spectr.hpp"
#include <algorithm>
#include <cmath>

using Catch::Approx;

TEST_CASE("Spectr processes audio") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000, 512);

    pulp::audio::Buffer<float> in(2, 512), out(2, 512);

    const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> iv(in_ptrs, 2, 512);
    auto ov = out.view();
    for (std::size_t block = 0; block < 8; ++block) {
        for (std::size_t i = 0; i < 512; ++i) {
            const auto sample = block * 512 + i;
            const float value = 0.5f * std::sin(
                2.0 * 3.14159265358979323846 * 997.0
                * static_cast<double>(sample) / 48000.0);
            in.channel(0)[i] = value;
            in.channel(1)[i] = value;
        }
        host.process(ov, iv);
    }

    // The production WOLA path has startup latency; after eight blocks the
    // in-band signal must have emerged.
    float peak = 0.0f;
    for (const auto sample : out.channel(0)) peak = std::max(peak, std::abs(sample));
    REQUIRE(peak > 0.1f);
}

TEST_CASE("Spectr has correct descriptor") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto desc = host.descriptor();

    REQUIRE(desc.name == "Spectr");
    REQUIRE(desc.manufacturer == "Pulp");
    REQUIRE(desc.category == pulp::format::PluginCategory::Effect);
}

TEST_CASE("Spectr reports production WOLA latency for host PDC") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    CHECK(host.processor()->latency_samples() == 1280);
    host.prepare(48000, 512);
    CHECK(host.processor()->latency_samples() == 1280);
}

TEST_CASE("Pulp ValidationHarness proves Spectr pass and exact mute audio") {
    pulp::format::ValidationHarness harness(spectr::create_spectr);
    harness.configure({.sample_rate = 48000.0,
                       .buffer_size = 512,
                       .input_channels = 2,
                       .output_channels = 2});
    harness.prepare();

    auto render_blocks = [&](int first_block) {
        std::vector<float> output;
        for (int block = first_block; block < first_block + 8; ++block) {
            std::vector<float> input(512 * 2);
            for (int sample = 0; sample < 512; ++sample) {
                const auto absolute = block * 512 + sample;
                const float value = 0.4f * std::sin(
                    2.0 * 3.14159265358979323846 * 997.0
                    * static_cast<double>(absolute) / 48000.0);
                input[static_cast<std::size_t>(sample * 2)] = value;
                input[static_cast<std::size_t>(sample * 2 + 1)] = -value;
            }
            output = harness.process_buffer(input, 2, 512);
        }
        return output;
    };

    const auto passed = render_blocks(0);
    float pass_peak = 0.0f;
    for (const auto sample : passed) pass_peak = std::max(pass_peak, std::abs(sample));
    REQUIRE(pass_peak > 0.1f);

    auto* processor = dynamic_cast<spectr::Spectr*>(harness.host().processor());
    REQUIRE(processor != nullptr);
    spectr::BandField muted;
    for (auto& band : muted.bands) band.muted = true;
    processor->replace_field(muted);
    const auto silenced = render_blocks(8);
    for (const auto sample : silenced) CHECK(sample == 0.0f);
}

TEST_CASE("Spectr imported editor has bounded proportional sizing") {
    spectr::Spectr plugin;
    const auto size = plugin.view_size();

    CHECK(size.preferred_width == 1320);
    CHECK(size.preferred_height == 860);
    CHECK(size.min_width == 800);
    CHECK(size.min_height == 521);
    CHECK(size.max_width == 2640);
    CHECK(size.max_height == 1720);
    CHECK(size.aspect_ratio == Approx(1320.0 / 860.0));
}

TEST_CASE("Spectr state round-trip") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000, 512);

    // Change a parameter
    host.state().set_value(spectr::kMix, 50.0f);

    // Save and restore
    auto data = host.save_state();
    host.state().set_value(spectr::kMix, 0.0f);
    REQUIRE(host.load_state(data));
    REQUIRE(host.state().get_value(spectr::kMix) == Approx(50.0f));
}
