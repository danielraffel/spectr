// The Vellum package editor on a real GPU host: a host-side parameter change
// must reach the painted frame, and a change to nothing must not.
#include <catch2/catch_test_macros.hpp>

#include <pulp/format/editor_host.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/vellum/editor.hpp>
#include <pulp/vellum/param_port_model.hpp>
#include <vellum/view/plugin_view_host.hpp>

#include "spectr/spectr.hpp"
#include "spectr/vellum_editor.hpp"

#include <cmath>
#include <cstdint>
#include <vellum/view/accessibility.hpp>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {

class Context final : public pulp::format::EditorContext {
  public:
    explicit Context(pulp::format::ParamPort& params) : params_(params) {}
    pulp::format::ParamPort& params() override { return params_; }
    pulp::format::VisualPort* visuals() override { return nullptr; }
    bool request_resize(const pulp::format::EditorSize&) override { return false; }
    bool host_action(const pulp::format::HostActionRequest&) override { return false; }
    pulp::format::EditorHostKind host_kind() const override {
        return pulp::format::EditorHostKind::plugin;
    }

  private:
    pulp::format::ParamPort& params_;
};

std::vector<::vellum::view::AccessibilityValueInterface*> collect_values(::vellum::view::View& v) {
    std::vector<::vellum::view::AccessibilityValueInterface*> out;
    if (auto* value = dynamic_cast<::vellum::view::AccessibilityValueInterface*>(&v))
        out.push_back(value);
    for (size_t i = 0; i < v.child_count(); ++i)
        for (auto* child : collect_values(*v.child_at(i))) out.push_back(child);
    return out;
}

} // namespace

TEST_CASE("Vellum editor repaints a host Mix change on the GPU host", "[vellum][editor][gpu]") {
#if !defined(__APPLE__)
    SKIP("Vellum ships its GPU plug-in view host on macOS only");
#else
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto& store = host.state();
    pulp::format::StateStoreParamPort port(store);
    pulp::vellum::ParamPortModel model(port);
    Context context(port);
    pulp::vellum::EditorEnv env{context, model, nullptr};
    spectr::vellum_editor::RootView root(env);

    std::string error;
    REQUIRE(pulp::vellum::register_editor_dawn_bootstrap("com.pulp.spectr.test", &error));
    ::vellum::view::PluginViewHost::Options options;
    options.size = {400, 300};
    options.use_gpu = true;
    auto gpu = ::vellum::view::PluginViewHost::create(root, options);
    REQUIRE(gpu != nullptr);
    if (!gpu->is_gpu_backed())
        SKIP("no GPU adapter: the host fell back without a surface");

    root.set_bounds({0, 0, 400, 300});
    root.layout_children();
    root.editor_idle();
    REQUIRE(gpu->present_first_frame());
    const std::vector<uint8_t> before = gpu->capture_back_buffer_png();
    REQUIRE_FALSE(before.empty());
    if (const char* dir = std::getenv("SPECTR_GPU_EDITOR_CAPTURE_DIR"))
        std::ofstream(std::string(dir) + "/before.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(before.data()),
                   static_cast<std::streamsize>(before.size()));
    // Control: the capture is deterministic for one state.
    CHECK(gpu->capture_back_buffer_png() == before);

    const float mix = store.get_value(spectr::kMix);
    // Negative control: a host write of the current value changes nothing.
    store.set_value(spectr::kMix, mix);
    root.editor_idle();
    REQUIRE(gpu->present_first_frame());
    CHECK(gpu->capture_back_buffer_png() == before);

    const float target = mix > 50.0f ? 10.0f : 90.0f;
    store.set_value(spectr::kMix, target);
    root.editor_idle();
    REQUIRE(gpu->present_first_frame());
    const std::vector<uint8_t> after = gpu->capture_back_buffer_png();
    // Mix is the third control the editor script creates.
    const auto values = collect_values(root);
    REQUIRE(values.size() > 2);
    CHECK(std::abs(values[2]->get_current_value() - target / 100.0) < 0.01);
    REQUIRE_FALSE(after.empty());
    CHECK(after != before);

    // Tear down as pulp::vellum::Editor does: the root stops pointing at the
    // host before the host is freed.
    root.set_plugin_view_host(nullptr);
    gpu.reset();
#endif
}
