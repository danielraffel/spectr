#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/state/store.hpp>
#include <pulp/view/editor_bridge.hpp>

#include <cstdio>
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

// ── Editor edits of the plain parameters record in host automation ──────────
//
// The recorder is what a format adapter sees for one parameter: the store's
// gesture callbacks and its inline value listener, in order. A host in Touch,
// Latch or Write keys on the begin/end bracket, so a value with no bracket
// around it is an edit the host cannot record.

namespace {

struct ParamEditRecorder {
    std::vector<std::string> events;
    pulp::state::ListenerToken token;
    pulp::state::ParamID watched = 0;
    explicit ParamEditRecorder(pulp::state::StateStore& store) {
        store.set_gesture_callbacks(
            [this](pulp::state::ParamID id) {
                if (id == watched) events.emplace_back("begin");
            },
            [this](pulp::state::ParamID id) {
                if (id == watched) events.emplace_back("end");
            });
        token = store.add_audio_listener([this](pulp::state::ParamID id, float value) {
            if (id != watched) return;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "set %g", static_cast<double>(value));
            events.emplace_back(buf);
        });
    }
    std::string take() {
        std::string out;
        for (const auto& e : events) out += (out.empty() ? "" : ", ") + e;
        events.clear();
        return out;
    }
};

struct BridgeRig {
    pulp::state::StateStore store;
    std::unique_ptr<spectr::Spectr> plugin = std::make_unique<spectr::Spectr>();
    pulp::view::EditorBridge bridge;
    BridgeRig() {
        plugin->set_state_store(&store);
        plugin->define_parameters(store);
        spectr::register_spectr_editor_handlers(
            bridge, *plugin, plugin->patterns(), plugin->editor_authority());
    }
    std::string send(const std::string& type, const std::string& payload) {
        return bridge.dispatch_json("{\"type\":\"" + type + "\",\"payload\":"
                                    + payload + "}");
    }
};

bool ok(const std::string& response) {
    return response.find("\"ok\": true") != response.npos;
}

}  // namespace

TEST_CASE("an editor click on any LFO control is one complete host gesture",
          "[modulation][automation][gesture]") {
    BridgeRig rig;
    ParamEditRecorder recorder(rig.store);
    const struct Case { pulp::state::ParamID id; const char* value; const char* set; } cases[] = {
        {spectr::kParamLfoEnabled,  "1",    "set 1"},
        {spectr::kParamLfoShape,    "2",    "set 2"},
        {spectr::kParamLfoRate,     "0.5",  "set 0.5"},
        {spectr::kParamLfoDepth,    "0.37", "set 0.37"},
        {spectr::kParamLfoTarget,   "3",    "set 3"},
        {spectr::kParamLfo2Enabled, "1",    "set 1"},
        {spectr::kParamLfo2Shape,   "3",    "set 3"},
        {spectr::kParamLfo2Rate,    "8",    "set 8"},
        {spectr::kParamLfo2Depth,   "0.5",  "set 0.5"},
        {spectr::kMix,        "40", "set 40"},
        {spectr::kOutputTrim, "-6", "set -6"},
    };
    for (const auto& c : cases) {
        INFO("param " << c.id);
        recorder.watched = c.id;
        REQUIRE(ok(rig.send("param_edit", "{\"id\":" + std::to_string(c.id)
                                          + ",\"value\":" + c.value + "}")));
        CHECK(recorder.take() == std::string("begin, ") + c.set + ", end");
    }
    CHECK(rig.store.open_gesture_count() == 0);

    // Control: the old route is visible to the recorder as exactly what it is,
    // a value with no gesture around it -- so the brackets above are the
    // product's, not the recorder's.
    recorder.watched = spectr::kParamLfoDepth;
    REQUIRE(ok(rig.send("param_set", "{\"id\":4003,\"value\":0.25}")));
    CHECK(recorder.take() == "set 0.25");
}

TEST_CASE("an editor drag of an LFO slider is ONE host gesture, however long",
          "[modulation][automation][gesture]") {
    BridgeRig rig;
    ParamEditRecorder recorder(rig.store);
    recorder.watched = spectr::kParamLfoDepth;
    REQUIRE(ok(rig.send("param_gesture_begin", "{\"id\":4003}")));
    for (const char* v : {"0.1", "0.2", "0.3"})
        REQUIRE(ok(rig.send("param_edit", std::string("{\"id\":4003,\"value\":") + v + "}")));
    // A second press report while the drag is open does not stack a bracket.
    REQUIRE(ok(rig.send("param_gesture_begin", "{\"id\":4003}")));
    REQUIRE(ok(rig.send("param_gesture_end", "{\"id\":4003}")));
    CHECK(recorder.take() == "begin, set 0.1, set 0.2, set 0.3, end");
    // A release reported twice cannot unbalance the host.
    REQUIRE(ok(rig.send("param_gesture_end", "{\"id\":4003}")));
    CHECK(recorder.take().empty());
    CHECK(rig.store.open_gesture_count() == 0);

    // A drag on one lane leaves a click on another its own complete bracket.
    REQUIRE(ok(rig.send("param_gesture_begin", "{\"id\":4003}")));
    CHECK(recorder.take() == "begin");
    recorder.watched = spectr::kParamLfoShape;
    REQUIRE(ok(rig.send("param_edit", "{\"id\":4001,\"value\":1}")));
    CHECK(recorder.take() == "begin, set 1, end");
    // The editor going away mid-drag closes what it opened.
    rig.plugin->editor_authority().reset_transient_state();
    CHECK(rig.store.open_gesture_count() == 0);
}

TEST_CASE("param_edit refuses parameters that own a different editor route",
          "[modulation][automation][gesture]") {
    BridgeRig rig;
    std::vector<pulp::state::ParamID> begins;
    rig.store.set_gesture_callbacks(
        [&](pulp::state::ParamID id) { begins.push_back(id); },
        [](pulp::state::ParamID) {});
    for (const auto id : {spectr::kParamFreeze, spectr::kParamFreezeLength,
                          spectr::band_gain_param_id(0),
                          spectr::kParamMorph, spectr::kParamEditMode,
                          spectr::macro_param_id(0), pulp::state::ParamID{4005}}) {
        INFO("param " << id);
        const auto before = rig.store.get_value(id);
        CHECK_FALSE(ok(rig.send("param_edit", "{\"id\":" + std::to_string(id)
                                              + ",\"value\":1}")));
        CHECK_FALSE(ok(rig.send("param_gesture_begin",
                                "{\"id\":" + std::to_string(id) + "}")));
        CHECK(rig.store.get_value(id) == before);
    }
    CHECK(begins.empty());
    CHECK_FALSE(ok(rig.send("param_edit", "{\"id\":4003,\"value\":\"x\"}")));
    CHECK_FALSE(ok(rig.send("param_edit", "{\"id\":4003}")));
}
