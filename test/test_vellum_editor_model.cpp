#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/editor_host.hpp>
#include <pulp/vellum/param_port_model.hpp>

#include <algorithm>
#include <vector>

namespace {

class TestPort final : public pulp::format::ParamPort {
  public:
    TestPort()
        : params_{{100, "Mix", "", pulp::state::ParamRange::linear(0.0f, 1.0f, 0.5f)},
                   {101, "Mode", "", pulp::state::ParamRange::linear(0.0f, 2.0f, 0.0f, 1.0f),
                    0, {}, {}, pulp::state::ParamRate::ControlRate, 0.0f,
                    pulp::state::ParamDesignation::None, false, pulp::state::ParamKind::Enum,
                    {"A", "B", "C"}},
                   {102, "Freeze", "", pulp::state::ParamRange::linear(0.0f, 1.0f, 0.0f, 1.0f),
                    0, {}, {}, pulp::state::ParamRate::ControlRate, 0.0f,
                    pulp::state::ParamDesignation::None, false, pulp::state::ParamKind::Toggle,
                    {"Off", "On"}}},
          values_{0.5f, 0.0f, 0.0f} {}

    std::span<const pulp::state::ParamInfo> params() const override { return params_; }
    const pulp::state::ParamInfo* info(pulp::state::ParamID id) const override {
        const auto it = std::find_if(params_.begin(), params_.end(),
                                     [id](const auto& p) { return p.id == id; });
        return it == params_.end() ? nullptr : &*it;
    }
    float get(pulp::state::ParamID id) const override { return values_[index(id)]; }
    void begin(pulp::state::ParamID id) override { ++begin_count_[index(id)]; }
    void set(pulp::state::ParamID id, float value) override {
        values_[index(id)] = value;
        for (auto& listener : listeners_)
            if (listener) listener(id, value);
    }
    void end(pulp::state::ParamID id) override { ++end_count_[index(id)]; }
    pulp::format::ParamSubscription add_listener(Listener listener) override {
        listeners_.push_back(std::move(listener));
        const auto slot = listeners_.size() - 1;
        return pulp::format::ParamSubscription([this, slot] { listeners_[slot] = {}; });
    }

    int begins(pulp::state::ParamID id) const { return begin_count_[index(id)]; }
    int ends(pulp::state::ParamID id) const { return end_count_[index(id)]; }

  private:
    std::size_t index(pulp::state::ParamID id) const {
        return static_cast<std::size_t>(id - 100);
    }
    std::vector<pulp::state::ParamInfo> params_;
    std::vector<float> values_;
    int begin_count_[3]{};
    int end_count_[3]{};
    std::vector<Listener> listeners_;
};

} // namespace

TEST_CASE("Vellum editor model follows controls and restores state") {
    TestPort port;
    {
        pulp::vellum::ParamPortModel model(port);
        const auto mix = model.resolve("Mix");
        const auto mode = model.resolve("Mode");
        const auto freeze = model.resolve("Freeze");
        REQUIRE(mix.valid());
        REQUIRE(mode.valid());
        REQUIRE(freeze.valid());
        CHECK(model.value_count(mix) == 0);
        CHECK(model.value_count(mode) == 3);
        CHECK(model.value_count(freeze) == 2);

        model.begin(mix);
        model.set_normalized(mix, 0.75);
        model.set_normalized(mode, 1.0);
        model.set_normalized(freeze, 1.0);
        model.end(mix);
        CHECK(port.get(100) == Catch::Approx(0.75f));
        CHECK(port.get(101) == Catch::Approx(2.0f));
        CHECK(port.get(102) == Catch::Approx(1.0f));
        CHECK(port.begins(100) == 1);
        CHECK(port.ends(100) == 1);

        // A host/state restore arrives through the same port and is observed by the model.
        int observed = 0;
        auto subscription = model.subscribe([&](auto handle, double value) {
            if (handle == mix && value == Catch::Approx(0.25)) ++observed;
        });
        port.set(100, 0.25f);
        CHECK(model.normalized(mix) == Catch::Approx(0.25));
        CHECK(observed == 1);

        // A closing editor owns the open drag and closes it on model teardown.
        model.begin(mode);
        model.set_normalized(mode, 0.5);
        CHECK(port.begins(101) == 1);
        CHECK(port.ends(101) == 0);
    }
    CHECK(port.ends(101) == 1);
}
