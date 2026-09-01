#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>
#include <pulp/view/editor_bridge.hpp>

#include <memory>
#include <string>
#include <vector>

using Catch::Approx;

TEST_CASE("#34 bridge mode_set records every visible mode control as a host gesture") {
    pulp::state::StateStore store;
    auto plugin = std::make_unique<spectr::Spectr>();
    plugin->set_state_store(&store);
    plugin->define_parameters(store);
    pulp::view::EditorBridge bridge;
    spectr::register_spectr_editor_handlers(
        bridge, *plugin, plugin->patterns(), plugin->editor_authority());

    std::vector<pulp::state::ParamID> begins;
    std::vector<pulp::state::ParamID> ends;
    store.set_gesture_callbacks(
        [&](pulp::state::ParamID id) { begins.push_back(id); },
        [&](pulp::state::ParamID id) { ends.push_back(id); });

    const struct Case { const char* kind; const char* value;
                        pulp::state::ParamID id; float expected; } cases[] = {
        {"motion", "precision", spectr::kParamMotionMode, 1.0f},
        {"analyzer", "off", spectr::kParamAnalyzerMode, 3.0f},
        {"edit", "glide", spectr::kParamEditMode, 4.0f},
        {"visualization", "response", spectr::kParamVisualization, 1.0f},
    };
    for (const auto& c : cases) {
        const auto response = bridge.dispatch_json(
            std::string{"{\"type\":\"mode_set\",\"payload\":{\"kind\":\""}
            + c.kind + "\",\"value\":\"" + c.value + "\"}}");
        REQUIRE(response.find("\"ok\": true") != response.npos);
        CHECK(store.get_value(c.id) == Approx(c.expected));
    }
    CHECK(begins == std::vector<pulp::state::ParamID>{3100, 3101, 3102, 3103});
    CHECK(ends == begins);
    CHECK(store.open_gesture_count() == 0);

    const auto before = begins.size();
    for (const auto* invalid : {
        R"({"type":"mode_set","payload":{"kind":"analyzer","value":"bogus"}})",
        R"({"type":"mode_set","payload":{"kind":"bogus","value":"peak"}})"}) {
        const auto response = bridge.dispatch_json(invalid);
        CHECK(response.find("invalid mode") != response.npos);
    }
    CHECK(begins.size() == before);
    CHECK(ends.size() == before);
}
