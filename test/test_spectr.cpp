#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_harness.hpp>
#include "spectr/spectr.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

using Catch::Approx;

TEST_CASE("Spectr processes audio") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000, 512);

    pulp::audio::Buffer<float> in(2, 512), out(2, 512);

    const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> iv(in_ptrs, 2, 512);
    auto ov = out.view();
    constexpr std::size_t block_size = 512;
    const auto blocks = static_cast<std::size_t>(
        (spectr::kSpectralLatency + spectr::kSpectralFftSize
         + static_cast<int>(block_size) - 1)
        / static_cast<int>(block_size));
    for (std::size_t block = 0; block < blocks; ++block) {
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

    // Every supported profile has had enough time to clear startup latency
    // and emit settled in-band signal.
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
    CHECK(host.processor()->latency_samples() == spectr::kSpectralLatency);
    host.prepare(48000, 512);
    CHECK(host.processor()->latency_samples() == spectr::kSpectralLatency);
}

TEST_CASE("Spectr reports honest resolution for current product geometry") {
    const auto check_internal_counts = [](
        const pulp::signal::SpectralBandResolution& report) {
        std::uint32_t represented = 0;
        std::uint32_t viewport_bins = 0;
        for (std::uint32_t band = 0; band < report.active_bands; ++band) {
            represented += report.owned_bins[band] > 0 ? 1u : 0u;
            viewport_bins += report.owned_bins[band];
        }
        CHECK(report.represented_bands == represented);
        CHECK(report.viewport_bins == viewport_bins);
    };
    const auto expected_full_range_bands = [] {
        switch (spectr::kSpectralFftSize) {
            case 256:   return 19u;
            case 512:   return 22u;
            case 1024:  return 25u;
            case 2048:  return 28u;
            case 4096:  return 31u;
            case 8192:  return 32u;
            case 16384: return 32u;
        }
        return 0u;
    }();
    const auto expected_narrow_bands = [] {
        switch (spectr::kSpectralFftSize) {
            case 256:   return 0u;
            case 512:   return 1u;
            case 1024:  return 2u;
            case 2048:  return 3u;
            case 4096:  return 6u;
            case 8192:  return 11u;
            case 16384: return 21u;
        }
        return 0u;
    }();

    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000, 512);

    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);

    pulp::signal::SpectralBandResolution full_range;
    REQUIRE(plugin->spectral_resolution(full_range));
    CHECK(full_range.fft_size == spectr::kSpectralFftSize);
    CHECK(full_range.sample_rate == Approx(48000.0f));
    CHECK(full_range.active_bands == 32);
    CHECK(full_range.represented_bands == expected_full_range_bands);
    CHECK(full_range.fully_represented()
          == (expected_full_range_bands == full_range.active_bands));
    check_internal_counts(full_range);

    auto narrow_view = plugin->viewport();
    narrow_view.min_hz = 280.0f;
    narrow_view.max_hz = 340.0f;
    REQUIRE(plugin->replace_processing_state(
        plugin->field(), narrow_view, spectr::Layout::Bands64));

    pulp::signal::SpectralBandResolution narrow;
    REQUIRE(plugin->spectral_resolution(narrow));
    CHECK(narrow.effective_min_hz == Approx(280.0f));
    CHECK(narrow.effective_max_hz == Approx(340.0f));
    CHECK(narrow.active_bands == 64);
    CHECK(narrow.represented_bands == expected_narrow_bands);
    CHECK(narrow.viewport_bins == expected_narrow_bands);
    CHECK_FALSE(narrow.fully_represented());
    check_internal_counts(narrow);

    const auto valid_report = narrow;
    plugin->viewport().min_hz = 500.0f;
    plugin->viewport().max_hz = 400.0f;
    CHECK_FALSE(plugin->spectral_resolution(narrow));
    CHECK(narrow.fft_size == valid_report.fft_size);
    CHECK(narrow.active_bands == valid_report.active_bands);
    CHECK(narrow.represented_bands == valid_report.represented_bands);
    CHECK(narrow.viewport_bins == valid_report.viewport_bins);
}

TEST_CASE("Spectr zero-percent mix is delayed by the reported wet latency") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.state().set_value(spectr::kMix, 0.0f);
    host.prepare(48000, 512);

    constexpr int block_size = 512;
    constexpr int latency = spectr::kSpectralLatency;
    constexpr int blocks = (latency + block_size) / block_size;
    pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
    const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> iv(in_ptrs, 2, block_size);
    auto ov = out.view();
    std::vector<float> rendered;
    rendered.reserve(block_size * blocks);

    for (int block = 0; block < blocks; ++block) {
        std::fill(in.channel(0).begin(), in.channel(0).end(), 0.0f);
        std::fill(in.channel(1).begin(), in.channel(1).end(), 0.0f);
        if (block == 0) {
            in.channel(0)[0] = 1.0f;
            in.channel(1)[0] = -1.0f;
        }
        host.process(ov, iv);
        rendered.insert(rendered.end(), out.channel(0).begin(), out.channel(0).end());
    }

    REQUIRE(rendered.size() > latency);
    for (int sample = 0; sample < latency; ++sample)
        CHECK(rendered[static_cast<std::size_t>(sample)] == 0.0f);
    CHECK(rendered[latency] == Approx(1.0f));
}

TEST_CASE("Pulp ValidationHarness proves Spectr pass and exact mute audio") {
    pulp::format::ValidationHarness harness(spectr::create_spectr);
    harness.configure({.sample_rate = 48000.0,
                       .buffer_size = 512,
                       .input_channels = 2,
                       .output_channels = 2});
    harness.prepare();

    const int settle_blocks =
        (spectr::kSpectralLatency + spectr::kSpectralFftSize + 511) / 512;
    auto render_blocks = [&](int first_block) {
        std::vector<float> output;
        for (int block = first_block; block < first_block + settle_blocks; ++block) {
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
    const auto silenced = render_blocks(settle_blocks);
    for (const auto sample : silenced) CHECK(sample == 0.0f);
}

TEST_CASE("Spectr flat wet path reconstructs after documented startup taper") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000, 512);

    constexpr std::size_t block_size = 512;
    const auto preroll = static_cast<std::size_t>(
        spectr::kSpectralLatency + spectr::kSpectralFftSize);
    const auto material = static_cast<std::size_t>(spectr::kSpectralFftSize * 4);
    const auto total = preroll + material
                     + static_cast<std::size_t>(spectr::kSpectralLatency);
    const auto padded_total = ((total + block_size - 1) / block_size) * block_size;

    std::vector<float> source(padded_total, 0.0f);
    std::uint32_t state = 0x6d2b79f5u;
    for (std::size_t i = preroll; i < preroll + material; ++i) {
        state = state * 1664525u + 1013904223u;
        source[i] = (static_cast<float>((state >> 8) & 0x00ffffffu)
                   / static_cast<float>(0x00800000u) - 1.0f) * 0.25f;
    }

    pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
    const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> iv(in_ptrs, 2, block_size);
    auto ov = out.view();
    std::vector<float> rendered(padded_total, 0.0f);

    for (std::size_t offset = 0; offset < padded_total; offset += block_size) {
        std::copy_n(source.data() + offset, block_size, in.channel(0).data());
        for (std::size_t i = 0; i < block_size; ++i)
            in.channel(1)[i] = -in.channel(0)[i];
        host.process(ov, iv);
        std::copy_n(out.channel(0).data(), block_size, rendered.data() + offset);
    }

    double residual_energy = 0.0;
    double reference_energy = 0.0;
    for (std::size_t i = 0; i < material; ++i) {
        const auto expected = source[preroll + i];
        const auto actual = rendered[preroll
                                   + static_cast<std::size_t>(spectr::kSpectralLatency)
                                   + i];
        const auto residual = static_cast<double>(actual - expected);
        residual_energy += residual * residual;
        reference_energy += static_cast<double>(expected) * expected;
    }
    REQUIRE(reference_energy > 0.0);
    const auto residual_db = 10.0 * std::log10(residual_energy / reference_energy);
    INFO("latency=" << spectr::kSpectralLatency
         << " samples, relative residual=" << residual_db << " dB");
    CHECK(residual_db < -100.0);
}

TEST_CASE("Spectr output automation is smoothed and block-partition invariant") {
    const auto render_ramp = [](std::size_t block_size) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        host.state().set_value(spectr::kMix, 0.0f);
        host.state().set_value(spectr::kOutputTrim, 0.0f);
        host.prepare(48000, 512);

        pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
        std::fill(in.channel(0).begin(), in.channel(0).end(), 1.0f);
        std::fill(in.channel(1).begin(), in.channel(1).end(), -1.0f);
        const float* input_channels[] = {
            in.channel(0).data(), in.channel(1).data()};
        pulp::audio::BufferView<const float> input(
            input_channels, 2, block_size);
        auto output = out.view();

        const auto warmup_samples = static_cast<std::size_t>(
            spectr::kSpectralLatency + spectr::kSpectralFftSize);
        const auto warmup_blocks =
            (warmup_samples + block_size - 1) / block_size;
        for (std::size_t block = 0; block < warmup_blocks; ++block)
            host.process(output, input);

        host.state().set_value(spectr::kOutputTrim, -24.0f);
        std::vector<float> left;
        std::vector<float> right;
        left.reserve(512);
        right.reserve(512);
        for (std::size_t offset = 0; offset < 512; offset += block_size) {
            host.process(output, input);
            left.insert(left.end(), out.channel(0).begin(), out.channel(0).end());
            right.insert(right.end(), out.channel(1).begin(), out.channel(1).end());
        }
        return std::pair{std::move(left), std::move(right)};
    };

    const auto [one_block_left, one_block_right] = render_ramp(512);
    const auto [split_left, split_right] = render_ramp(64);
    REQUIRE(one_block_left.size() == 512);
    REQUIRE(split_left.size() == one_block_left.size());

    for (std::size_t i = 0; i < one_block_left.size(); ++i) {
        CHECK(one_block_left[i] == Approx(split_left[i]).margin(1.0e-6f));
        CHECK(one_block_right[i] == Approx(-one_block_left[i]).margin(1.0e-6f));
        CHECK(split_right[i] == Approx(-split_left[i]).margin(1.0e-6f));
    }
    CHECK(one_block_left.front() < 1.0f);
    CHECK(one_block_left.front() > one_block_left.back());
    CHECK(one_block_left.back()
          == Approx(std::pow(10.0f, -24.0f * 0.05f)).margin(1.0e-6f));
}

TEST_CASE("Spectr clears delayed audio history at a host reset boundary") {
    constexpr std::size_t block_size = 512;

    const auto peak_after_impulse = [](bool request_reset,
                                       bool request_transport_jump) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        host.state().set_value(spectr::kMix, 0.0f);
        host.state().set_value(spectr::kOutputTrim, 0.0f);
        host.prepare(48000, static_cast<int>(block_size));

        pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
        std::fill(in.channel(0).begin(), in.channel(0).end(), 0.0f);
        std::fill(in.channel(1).begin(), in.channel(1).end(), 0.0f);
        in.channel(0)[0] = 1.0f;
        in.channel(1)[0] = -1.0f;
        const float* input_channels[] = {
            in.channel(0).data(), in.channel(1).data()};
        pulp::audio::BufferView<const float> input(
            input_channels, 2, block_size);
        auto output = out.view();
        host.process(output, input);

        std::fill(in.channel(0).begin(), in.channel(0).end(), 0.0f);
        std::fill(in.channel(1).begin(), in.channel(1).end(), 0.0f);
        // This second impulse belongs to the new timeline. Resetting too late
        // would erase it along with the stale first impulse.
        in.channel(0)[0] = 0.25f;
        in.channel(1)[0] = -0.25f;
        const auto render_blocks = static_cast<std::size_t>(
            (spectr::kSpectralLatency + spectr::kSpectralFftSize
             + static_cast<int>(block_size) - 1)
            / static_cast<int>(block_size));
        float peak = 0.0f;
        for (std::size_t block = 0; block < render_blocks; ++block) {
            pulp::format::ProcessContext context;
            context.reset_requested = request_reset && block == 0;
            context.transport_jump = request_transport_jump && block == 0;
            host.process(output, input, context);
            if (block == 0) {
                std::fill(in.channel(0).begin(), in.channel(0).end(), 0.0f);
                std::fill(in.channel(1).begin(), in.channel(1).end(), 0.0f);
            }
            for (std::size_t sample = 0; sample < block_size; ++sample) {
                peak = std::max(peak, std::abs(out.channel(0)[sample]));
                CHECK(out.channel(1)[sample]
                      == Approx(-out.channel(0)[sample]).margin(1.0e-7f));
            }
        }
        return peak;
    };

    // The control proves the stale impulse would cross the product's latency
    // boundary. Both reset signals must erase it while preserving the smaller
    // first impulse from the new timeline.
    CHECK(peak_after_impulse(false, false) > 0.99f);
    CHECK(peak_after_impulse(true, false) == Approx(0.25f));
    CHECK(peak_after_impulse(false, true) == Approx(0.25f));
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
