#include <spectr/forge_adapter.hpp>

#include <catch2/catch_test_macros.hpp>

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
