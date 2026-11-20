#include <algorithm>
#include <cmath>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <pulp/vellum_audio/audio_display_provider.hpp>
#include <pulp/format/headless.hpp>
#include "spectr/spectr.hpp"

#include <array>

TEST_CASE("Spectr Vellum audio display keeps meter and scope frames coherent") {
    using Provider = pulp::vellum_audio::AudioDisplayProvider;
    Provider provider({{"output", "linear", ::vellum::value::ChannelShape::meter, 0.0f},
                       {"analyzer", "dBFS", ::vellum::value::ChannelShape::vector, -120.0f}});
    const std::array<float, 2> rms{0.25f, 0.125f};
    const std::array<float, 2> peak{0.5f, 0.25f};
    const std::array<float, 5> bins{-120.0f, -80.0f, -3.0f, -80.0f, -120.0f};
    pulp::vellum_audio::AudioFrameInfo info;
    info.sample_rate = 48000.0;
    info.epoch = 7;
    info.sequence = 11;
    info.dropped_frames = 2;
    provider.publish_meter(0, rms, peak, info);
    provider.publish_scope(1, bins, info);

    std::array<float, 2> got_rms{};
    std::array<float, 2> got_peak{};
    pulp::vellum_audio::AudioFrameInfo got_info;
    REQUIRE(provider.read_meter(0, got_rms, got_peak, got_info) == 2);
    CHECK(got_rms[0] == Catch::Approx(0.25f));
    CHECK(got_peak[1] == Catch::Approx(0.25f));
    CHECK(got_info.sequence == 11);
    CHECK(got_info.dropped_frames == 2);

    std::array<float, 5> got_bins{};
    REQUIRE(provider.read_vector(1, got_bins, got_info) == 5);
    CHECK(got_bins[2] == Catch::Approx(-3.0f));
    CHECK(got_info.epoch == 7);
}

TEST_CASE("Spectr Vellum audio display returns neutral data before first publish") {
    using Provider = pulp::vellum_audio::AudioDisplayProvider;
    Provider provider({{"output", "linear", ::vellum::value::ChannelShape::meter, 0.0f}});
    std::array<float, 2> rms{1.0f, 1.0f};
    std::array<float, 2> peak{1.0f, 1.0f};
    pulp::vellum_audio::AudioFrameInfo info;
    CHECK(provider.read_meter(0, rms, peak, info) == 0);
    CHECK(provider.publish_count(0) == 0);
}

TEST_CASE("Spectr Vellum audio display holds the last frame when the feed is stale") {
    using Provider = pulp::vellum_audio::AudioDisplayProvider;
    Provider provider({{"output", "linear", ::vellum::value::ChannelShape::meter, 0.0f}});
    const std::array<float, 1> rms{0.25f};
    const std::array<float, 1> peak{0.5f};
    provider.publish_meter(0, rms, peak);
    std::array<float, 1> got_rms{};
    std::array<float, 1> got_peak{};
    REQUIRE(provider.read_meter(0, got_rms, got_peak) == 1);
    CHECK(got_rms[0] == Catch::Approx(0.25f));
    CHECK(got_peak[0] == Catch::Approx(0.5f));
}

TEST_CASE("Spectr publishes post-engine levels for a known sine") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.state().set_value(spectr::kParamAutoGain, 0.0f);
    host.prepare(48000.0, 512);
    pulp::audio::Buffer<float> input(2, 512), output(2, 512);
    const float* input_channels[] = {input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> input_view(input_channels, 2, 512);
    for (int block = 0; block < 20; ++block) {
        for (std::size_t i = 0; i < 512; ++i) {
            const float value = 0.5f * std::sin(
                2.0 * 3.14159265358979323846 * 997.0 * (block * 512 + i) / 48000.0);
            input.channel(0)[i] = value;
            input.channel(1)[i] = value;
        }
        auto output_view = output.view();
        host.process(output_view, input_view);
    }
    auto* plugin = host.processor_as<spectr::Spectr>();
    REQUIRE(plugin != nullptr);
    auto* provider = plugin->audio_display_provider();
    REQUIRE(provider != nullptr);
    std::array<float, 2> rms{};
    std::array<float, 2> peak{};
    REQUIRE(provider->read_meter(0, rms, peak) == 2);
    CHECK(rms[0] > 0.01f);
    CHECK(peak[0] > rms[0]);
    plugin->drain_analyzer();
    std::array<float, pulp::vellum_audio::AudioDisplayProvider::kMaxScopeSamples> bins{};
    const auto count = provider->read_vector(1, bins);
    // The scope maps exactly kAnalyzerDisplayBins; any other count is dropped.
    REQUIRE(count == spectr::kAnalyzerDisplayBins);
    const auto peak_bin = static_cast<std::size_t>(
        std::distance(bins.begin(), std::max_element(bins.begin(), bins.begin() + count)));
    const double bin_hz = 48000.0 / spectr::kAnalyzerDisplayFftSize;
    CHECK(std::abs(static_cast<double>(peak_bin) * bin_hz - 997.0) <= bin_hz);
}

TEST_CASE("Spectr meters every channel of an eight-channel bus") {
    // Eight channels fill the dataplane meter frame exactly; every channel
    // reaches the display provider.
    constexpr std::size_t kChannels = 8;
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.state().set_value(spectr::kParamAutoGain, 0.0f);
    host.prepare(48000.0, 256, static_cast<int>(kChannels), static_cast<int>(kChannels));
    pulp::audio::Buffer<float> input(kChannels, 256), output(kChannels, 256);
    std::array<const float*, kChannels> input_channels{};
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        input_channels[ch] = input.channel(ch).data();
        for (std::size_t i = 0; i < 256; ++i)
            input.channel(ch)[i] = 0.25f * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 48000.0);
    }
    pulp::audio::BufferView<const float> input_view(input_channels.data(), kChannels, 256);
    for (int block = 0; block < 8; ++block) {
        auto output_view = output.view();
        host.process(output_view, input_view);
    }
    auto* plugin = host.processor_as<spectr::Spectr>();
    REQUIRE(plugin != nullptr);
    auto* provider = plugin->audio_display_provider();
    REQUIRE(provider != nullptr);
    constexpr auto kFrame = pulp::vellum_audio::AudioDisplayProvider::kMaxMeterChannels;
    std::array<float, kFrame> rms{};
    std::array<float, kFrame> peak{};
    REQUIRE(provider->read_meter(0, rms, peak) == kChannels);
    for (std::size_t ch = 0; ch < kChannels; ++ch)
        CHECK(rms[ch] > 0.01f);
}

TEST_CASE("Analyzer bins pool to a display frame and keep a narrow peak") {
    // 9 bins (fft 16) to 5 bins (fft 8): bin 0 kept, each output the max of two.
    const std::array<float, 9> in{-1.0f, -90.0f, -80.0f, -70.0f, -3.0f, -60.0f, -60.0f, -50.0f, -40.0f};
    std::array<float, 5> out{};
    REQUIRE(spectr::pool_analyzer_bins(in, out) == 5);
    CHECK(out[0] == -1.0f);
    CHECK(out[1] == -80.0f);
    CHECK(out[2] == -3.0f);
    CHECK(out[3] == -60.0f);
    CHECK(out[4] == -40.0f);
    // Sizes that are not a power-of-two reduction are refused.
    std::array<float, 4> odd{};
    CHECK(spectr::pool_analyzer_bins(in, odd) == 0);
    // The display frame fits the scope and is one of Spectr's FFT reductions.
    STATIC_REQUIRE(spectr::kAnalyzerDisplayBins <=
                   pulp::vellum_audio::AudioDisplayProvider::kMaxScopeSamples);
    STATIC_REQUIRE(spectr::kAnalyzerFftSize % spectr::kAnalyzerDisplayFftSize == 0);
}

TEST_CASE("Spectr meter frames carry a new epoch after a re-prepare") {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000.0, 256);
    pulp::audio::Buffer<float> input(2, 256), output(2, 256);
    for (std::size_t i = 0; i < 256; ++i)
        input.channel(0)[i] = input.channel(1)[i] = 0.25f;
    const float* input_channels[] = {input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> input_view(input_channels, 2, 256);
    auto run = [&] {
        auto output_view = output.view();
        host.process(output_view, input_view);
    };
    auto* plugin = host.processor_as<spectr::Spectr>();
    REQUIRE(plugin != nullptr);
    run();
    pulp::vellum_audio::AudioFrameInfo first{};
    REQUIRE(plugin->audio_display_provider()->read_info(0, first));
    run();
    pulp::vellum_audio::AudioFrameInfo same{};
    REQUIRE(plugin->audio_display_provider()->read_info(0, same));
    CHECK(same.epoch == first.epoch);
    host.prepare(44100.0, 256);
    run();
    pulp::vellum_audio::AudioFrameInfo after{};
    REQUIRE(plugin->audio_display_provider()->read_info(0, after));
    CHECK(after.epoch != first.epoch);
}

TEST_CASE("Spectr publishes output meters on every block while an LFO modulates") {
    // Modulation sends process() down its per-event path; the meters must
    // publish there too, on every block, or they freeze while an LFO runs.
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.state().set_value(spectr::kParamAutoGain, 0.0f);
    host.state().set_value(spectr::kParamLfoEnabled, 1.0f);
    host.prepare(48000.0, 256);
    pulp::audio::Buffer<float> input(2, 256), output(2, 256);
    for (std::size_t i = 0; i < 256; ++i)
        input.channel(0)[i] = input.channel(1)[i] = 0.25f;
    const float* input_channels[] = {input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> input_view(input_channels, 2, 256);
    auto* plugin = host.processor_as<spectr::Spectr>();
    REQUIRE(plugin != nullptr);
    std::uint32_t last = 0;
    for (int block = 0; block < 6; ++block) {
        auto output_view = output.view();
        host.process(output_view, input_view);
        pulp::vellum_audio::AudioFrameInfo info{};
        REQUIRE(plugin->audio_display_provider()->read_info(0, info));
        CHECK(info.sequence > last);
        last = info.sequence;
    }
}
