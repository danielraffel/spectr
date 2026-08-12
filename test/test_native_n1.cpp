#include "spectr/spectr.hpp"

#include <catch2/catch_test_macros.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/canvas_widget.hpp>
#include <pulp/view/scripted_ui.hpp>

TEST_CASE("native N1 mounts live QuickJS widgets without an editor fallback",
          "[native-n1]") {
    spectr::Spectr processor;
    pulp::state::StateStore store;
    processor.set_state_store(&store);
    processor.define_parameters(store);

    const auto size = processor.view_size();
    REQUIRE(size.preferred_width == 1320);
    REQUIRE(size.preferred_height == 860);
    REQUIRE(size.min_width == 1320);
    REQUIRE(size.max_width == 1320);

    auto root = processor.create_view();
    REQUIRE(root != nullptr);
    REQUIRE(root->requires_gpu_host());

    auto* session = processor.active_scripted_ui();
    REQUIRE(session != nullptr);
    REQUIRE(session->bridge() != nullptr);

    auto* canvas = dynamic_cast<pulp::view::CanvasWidget*>(
        session->bridge()->widget("spectr-analyzer-canvas"));
    REQUIRE(canvas != nullptr);
    REQUIRE(canvas->command_count() > 10);
    for (int band = 0; band < 32; ++band) {
        REQUIRE(session->bridge()->widget(
            "spectr-band-" + std::to_string(band)) != nullptr);
    }

    processor.on_view_closed(*root);
    REQUIRE(processor.active_scripted_ui() == nullptr);
}
