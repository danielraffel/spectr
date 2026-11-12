#include <spectr/forge_adapter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <pulp/host/signal_graph.hpp>
#include <pulp/format/headless.hpp>

#include <cmath>
#include <vector>

TEST_CASE("Spectr Forge adapter returns the product Processor", "[forge][adapter]") {
    auto processor = spectr::forge_adapter::create_processor();
    REQUIRE(processor != nullptr);
    CHECK(processor->descriptor().name == "Spectr");
    CHECK(processor->descriptor().category == pulp::format::PluginCategory::Effect);
}

TEST_CASE("Spectr Forge adapter keeps the ordinary product controls", "[forge][adapter]") {
    auto processor = spectr::forge_adapter::create_processor();
    REQUIRE(processor != nullptr);
    pulp::state::StateStore store;
    processor->define_parameters(store);
    CHECK(store.all_params().size() > 0);
}

TEST_CASE("Spectr Forge adapter renders through a SignalGraph ProcessorNode",
          "[forge][adapter][graph][audio]") {
    constexpr double sample_rate = 48000.0;
    constexpr int block_size = 256;
    constexpr int block_count = 24;

    auto graph_processor = spectr::forge_adapter::create_processor();
    REQUIRE(graph_processor != nullptr);
    auto* graph_spectr = dynamic_cast<spectr::Spectr*>(graph_processor.get());
    REQUIRE(graph_spectr != nullptr);
    // Use Tracking so this receipt does not depend on an optional GPU provider
    // or asynchronous shared-renderer delivery.
    REQUIRE(graph_spectr->set_render_mode(spectr::MaskRenderMode::zero_latency));
    auto instance = pulp::format::ProcessorNodeInstance::create(std::move(graph_processor));
    REQUIRE(instance);

    pulp::host::SignalGraph graph;
    const auto input = graph.add_input_node(2, "Spectr input");
    const auto node = graph.add_processor_node(instance, "Spectr");
    const auto output = graph.add_output_node(2, "Spectr output");
    REQUIRE(input != 0);
    REQUIRE(node != 0);
    REQUIRE(output != 0);
    REQUIRE(graph.connect(input, 0, node, 0));
    REQUIRE(graph.connect(input, 1, node, 1));
    REQUIRE(graph.connect(node, 0, output, 0));
    REQUIRE(graph.connect(node, 1, output, 1));
    REQUIRE(graph.prepare(sample_rate, block_size));

    // Independent host oracle: a separately constructed Spectr instance is
    // driven by Pulp's HeadlessHost, so the comparison covers the ProcessorNode
    // route and executor rather than duplicating Spectr's DSP implementation.
    pulp::format::HeadlessHost oracle(spectr::create_spectr);
    auto* oracle_spectr = oracle.processor_as<spectr::Spectr>();
    REQUIRE(oracle_spectr != nullptr);
    REQUIRE(oracle_spectr->set_render_mode(spectr::MaskRenderMode::zero_latency));
    oracle.prepare(sample_rate, block_size);

    std::vector<float> graph_left(block_size), graph_right(block_size);
    std::vector<float> oracle_left(block_size), oracle_right(block_size);
    std::vector<float> input_left(block_size), input_right(block_size);
    float* graph_outputs[] = {graph_left.data(), graph_right.data()};
    const float* graph_inputs[] = {input_left.data(), input_right.data()};
    float* oracle_outputs[] = {oracle_left.data(), oracle_right.data()};
    const auto frames = static_cast<std::size_t>(block_size * block_count);
    double dot = 0.0;
    double graph_energy = 0.0;
    double oracle_energy = 0.0;
    for (int block = 0; block < block_count; ++block) {
        for (int i = 0; i < block_size; ++i) {
            const auto n = static_cast<double>(block * block_size + i);
            input_left[static_cast<std::size_t>(i)] =
                static_cast<float>(0.25 * std::sin(0.013 * n) + 0.1 * std::sin(0.031 * n));
            input_right[static_cast<std::size_t>(i)] =
                static_cast<float>(0.2 * std::sin(0.019 * n));
        }
        pulp::audio::BufferView<const float> input_view(graph_inputs, 2, block_size);
        pulp::audio::BufferView<float> graph_view(graph_outputs, 2, block_size);
        pulp::audio::BufferView<float> oracle_view(oracle_outputs, 2, block_size);
        graph.process(graph_view, input_view, block_size);
        oracle.process(oracle_view, input_view);
        for (int i = 0; i < block_size; ++i) {
            const auto l = static_cast<std::size_t>(i);
            REQUIRE(std::isfinite(graph_left[l]));
            REQUIRE(std::isfinite(graph_right[l]));
            REQUIRE(std::isfinite(oracle_left[l]));
            REQUIRE(std::isfinite(oracle_right[l]));
            dot += static_cast<double>(graph_left[l]) * oracle_left[l];
            dot += static_cast<double>(graph_right[l]) * oracle_right[l];
            graph_energy += static_cast<double>(graph_left[l]) * graph_left[l];
            graph_energy += static_cast<double>(graph_right[l]) * graph_right[l];
            oracle_energy += static_cast<double>(oracle_left[l]) * oracle_left[l];
            oracle_energy += static_cast<double>(oracle_right[l]) * oracle_right[l];
        }
    }
    REQUIRE(frames == static_cast<std::size_t>(block_size * block_count));
    REQUIRE(graph_energy > 1.0e-8);
    REQUIRE(oracle_energy > 1.0e-8);
    CHECK(dot / std::sqrt(graph_energy * oracle_energy) > 0.98);
    oracle.release();
}

TEST_CASE("Spectr Forge adapter reports an explicit GPU capability negative",
          "[forge][adapter][gpu][negative]") {
    auto processor = spectr::forge_adapter::create_processor();
    REQUIRE(processor != nullptr);
    auto* spectr_processor = dynamic_cast<spectr::Spectr*>(processor.get());
    REQUIRE(spectr_processor != nullptr);
    const auto status = spectr_processor->gpu_audio_status();
    if (!spectr::Spectr::gpu_processing_available()) {
        CHECK(status.availability != spectr::GpuAudioStatus::Availability::Available);
        CHECK_FALSE(status.delivery.has_value());
        // The control setter records the user's preference before prepare;
        // capability refusal is represented by the status projection and the
        // absence of a delivery, not by rejecting that deferred preference.
        CHECK(spectr_processor->set_gpu_processing(true));
        CHECK_FALSE(spectr_processor->gpu_audio_status().delivery.has_value());
    } else {
        CHECK(status.availability != spectr::GpuAudioStatus::Availability::NotBuilt);
    }
}
