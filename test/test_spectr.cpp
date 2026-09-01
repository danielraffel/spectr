#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/format/validation_harness.hpp>
#include "spectr/spectr.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
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

TEST_CASE("Spectr isolates three nonadjacent stereo frequency islands") {
    if constexpr (spectr::kSpectralFftSize < 8192) {
        SUCCEED("exact six-tone island oracle requires the Balanced or Maximum profile");
        return;
    }
    constexpr double pi = 3.14159265358979323846;
    constexpr int sample_rate = 48000;
    constexpr int block_size = 512;
    constexpr int analysis_size = spectr::kSpectralFftSize;
    constexpr int bin_scale = analysis_size / 8192;
    constexpr double amplitude = 0.04;
    const std::array<int, 3> kept_bins{
        52 * bin_scale, 205 * bin_scale, 597 * bin_scale};
    const std::array<int, 3> rejected_bins{
        102 * bin_scale, 376 * bin_scale, 1195 * bin_scale};
    const auto hz_for_bin = [](int bin) {
        return static_cast<double>(bin) * sample_rate / analysis_size;
    };

    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(sample_rate, block_size);
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);

    spectr::BandField islands;
    for (auto& band : islands.bands) band.muted = true;
    std::array<std::size_t, kept_bins.size()> kept_bands{};
    for (std::size_t i = 0; i < kept_bins.size(); ++i) {
        kept_bands[i] = plugin->viewport().band_for_hz(
            static_cast<float>(hz_for_bin(kept_bins[i])), 32);
        islands.bands[kept_bands[i]].muted = false;
    }
    CHECK(kept_bands == std::array<std::size_t, 3>{12, 18, 23});
    REQUIRE(plugin->replace_processing_state(
        islands, spectr::Viewport{}, spectr::Layout::Bands32));

    const auto total_samples = static_cast<std::size_t>(
        spectr::kSpectralLatency + spectr::kSpectralFftSize * 4);
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

    pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
    const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> iv(in_ptrs, 2, block_size);
    auto ov = out.view();
    for (std::size_t offset = 0; offset < padded_samples; offset += block_size) {
        std::copy_n(source_left.data() + offset, block_size, in.channel(0).data());
        for (int i = 0; i < block_size; ++i)
            in.channel(1)[i] = -0.6f * in.channel(0)[i];
        host.process(ov, iv);
        std::copy_n(out.channel(0).data(), block_size,
                    rendered_left.data() + offset);
        std::copy_n(out.channel(1).data(), block_size,
                    rendered_right.data() + offset);
    }

    const auto output_start = padded_samples - analysis_size;
    const auto source_start = output_start
                            - static_cast<std::size_t>(spectr::kSpectralLatency);
    const auto projection = [=](const std::vector<float>& signal,
                                std::size_t start,
                                int bin) {
        std::complex<double> sum{};
        for (int i = 0; i < analysis_size; ++i) {
            const auto phase = -2.0 * pi * bin * i / analysis_size;
            sum += static_cast<double>(signal[start + static_cast<std::size_t>(i)])
                 * std::complex<double>(std::cos(phase), std::sin(phase));
        }
        return sum * (2.0 / analysis_size);
    };

    for (const auto bin : kept_bins) {
        const auto input = projection(source_left, source_start, bin);
        const auto left = projection(rendered_left, output_start, bin);
        const auto right = projection(rendered_right, output_start, bin);
        INFO("retained FFT bin " << bin);
        CHECK(std::abs(left) / std::abs(input) == Approx(1.0).margin(0.02));
        const auto stereo_ratio = right / left;
        CHECK(stereo_ratio.real() == Approx(-0.6).margin(0.002));
        CHECK(stereo_ratio.imag() == Approx(0.0).margin(0.002));
    }
    for (const auto bin : rejected_bins) {
        const auto input = projection(source_left, source_start, bin);
        const auto left = projection(rendered_left, output_start, bin);
        const auto right = projection(rendered_right, output_start, bin);
        INFO("rejected FFT bin " << bin);
        CHECK(std::abs(left) / std::abs(input) < 1.0e-5);
        CHECK(std::abs(right) / std::abs(input) < 1.0e-5);
    }
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
                                       bool request_transport_jump,
                                       bool ordinary_loop_wrap) {
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
            context.ordinary_loop_wrap = ordinary_loop_wrap && block == 0;
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
    // boundary. Explicit reset and an unexpected seek erase it while preserving
    // the smaller new-timeline impulse; an ordinary cycle wrap keeps history.
    CHECK(peak_after_impulse(false, false, false) > 0.99f);
    CHECK(peak_after_impulse(true, false, false) == Approx(0.25f));
    CHECK(peak_after_impulse(false, true, false) == Approx(0.25f));
    CHECK(peak_after_impulse(false, true, true) > 0.99f);
}

TEST_CASE("Spectr ordinary loop wrap preserves exact continuous source time") {
    constexpr std::size_t block_size = 512;
    constexpr std::size_t num_blocks = 64;
    constexpr std::size_t wrap_block = 40;

    pulp::format::HeadlessHost uninterrupted(spectr::create_spectr);
    pulp::format::HeadlessHost looped(spectr::create_spectr);
    uninterrupted.state().set_value(spectr::kMix, 100.0f);
    looped.state().set_value(spectr::kMix, 100.0f);
    uninterrupted.prepare(48000, static_cast<int>(block_size));
    looped.prepare(48000, static_cast<int>(block_size));

    pulp::audio::Buffer<float> in(2, block_size);
    pulp::audio::Buffer<float> reference_out(2, block_size);
    pulp::audio::Buffer<float> looped_out(2, block_size);
    const float* input_channels[] = {
        in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> input(
        input_channels, 2, block_size);
    auto reference_view = reference_out.view();
    auto looped_view = looped_out.view();

    // A polarity/amplitude-coded stream makes a dropped, duplicated, reordered,
    // or newly zero-filled region observable rather than relying on one tone.
    std::uint32_t state = 0x9e3779b9u;
    for (std::size_t block = 0; block < num_blocks; ++block) {
        for (std::size_t sample = 0; sample < block_size; ++sample) {
            state = state * 1664525u + 1013904223u;
            const float value = (static_cast<float>((state >> 8) & 0xffffu)
                                 / 32768.0f - 1.0f) * 0.2f;
            in.channel(0)[sample] = value;
            in.channel(1)[sample] = -0.625f * value;
        }

        uninterrupted.process(reference_view, input);
        pulp::format::ProcessContext context;
        if (block == wrap_block) {
            context.transport_jump = true;
            context.ordinary_loop_wrap = true;
        }
        looped.process(looped_view, input, context);

        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (std::size_t sample = 0; sample < block_size; ++sample) {
                CHECK(looped_out.channel(channel)[sample]
                      == Approx(reference_out.channel(channel)[sample])
                             .margin(1.0e-7f));
            }
        }
    }
}

TEST_CASE("Spectr imported editor has bounded proportional sizing") {
    spectr::Spectr plugin;
    const auto size = plugin.view_size();

    // The authored layout remains 1320x860, but the host initially opens it at
    // 75% scale so the editor fits comfortably on smaller displays.
    CHECK(size.preferred_width == 990);
    CHECK(size.preferred_height == 645);
    CHECK(size.min_width == 792);
    CHECK(size.min_height == 516);
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
