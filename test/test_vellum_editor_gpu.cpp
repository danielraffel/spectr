// The Vellum package editor on a real GPU host: a host-side parameter change
// must reach the painted frame, and a change to nothing must not.
#include <catch2/catch_test_macros.hpp>

#include <pulp/format/editor_host.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/vellum/editor.hpp>
#include <pulp/vellum/param_port_model.hpp>
#include <pulp/vellum_audio/audio_display_provider.hpp>
#include <vellum/view/plugin_view_host.hpp>

#include "spectr/spectr.hpp"
#include "spectr/vellum_editor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vellum/view/accessibility.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

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

struct Pixels {
    size_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

#ifdef __APPLE__
Pixels decode_png(const std::vector<uint8_t>& png) {
    Pixels out;
    CFDataRef data = CFDataCreate(nullptr, png.data(), static_cast<CFIndex>(png.size()));
    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    if (image) {
        out.width = CGImageGetWidth(image);
        out.height = CGImageGetHeight(image);
        out.rgba.assign(out.width * out.height * 4, 0);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            out.rgba.data(), out.width, out.height, 8, out.width * 4, space,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(out.width),
                                           static_cast<CGFloat>(out.height)), image);
        CGContextRelease(ctx);
        CGColorSpaceRelease(space);
        CGImageRelease(image);
    }
    if (source) CFRelease(source);
    CFRelease(data);
    return out;
}
#endif

// Root-relative rectangle of a view: bounds are in the parent's space.
::vellum::view::Rect absolute_bounds(const ::vellum::view::View& v) {
    auto r = v.bounds();
    for (const auto* p = v.parent(); p != nullptr; p = p->parent()) {
        r.x += p->bounds().x;
        r.y += p->bounds().y;
    }
    return r;
}

// Fraction of pixels inside `r` that differ between two frames of one size.
double changed_fraction(const Pixels& a, const Pixels& b, const ::vellum::view::Rect& r) {
    if (a.width != b.width || a.height != b.height) return 0.0;
    const size_t x0 = static_cast<size_t>(std::max(0.0f, std::floor(r.x)));
    const size_t y0 = static_cast<size_t>(std::max(0.0f, std::floor(r.y)));
    const size_t x1 = std::min(a.width, static_cast<size_t>(std::ceil(r.right())));
    const size_t y1 = std::min(a.height, static_cast<size_t>(std::ceil(r.bottom())));
    if (x1 <= x0 || y1 <= y0) return 0.0;
    size_t changed = 0;
    for (size_t y = y0; y < y1; ++y)
        for (size_t x = x0; x < x1; ++x) {
            const size_t i = (y * a.width + x) * 4;
            if (std::abs(int(a.rgba[i]) - int(b.rgba[i])) + std::abs(int(a.rgba[i + 1]) - int(b.rgba[i + 1])) +
                    std::abs(int(a.rgba[i + 2]) - int(b.rgba[i + 2])) > 12)
                ++changed;
        }
    return static_cast<double>(changed) / static_cast<double>((x1 - x0) * (y1 - y0));
}

// Fraction of pixels inside `r` that differ from the frame's corner pixel
// (the clear colour). A control that drew nothing, or drew into a 2 px box,
// reads near zero.
double painted_fraction(const Pixels& px, const ::vellum::view::Rect& r) {
    const size_t x0 = static_cast<size_t>(std::max(0.0f, std::floor(r.x)));
    const size_t y0 = static_cast<size_t>(std::max(0.0f, std::floor(r.y)));
    const size_t x1 = std::min(px.width, static_cast<size_t>(std::ceil(r.right())));
    const size_t y1 = std::min(px.height, static_cast<size_t>(std::ceil(r.bottom())));
    if (x1 <= x0 || y1 <= y0) return 0.0;
    size_t differing = 0;
    for (size_t y = y0; y < y1; ++y)
        for (size_t x = x0; x < x1; ++x) {
            const uint8_t* p = &px.rgba[(y * px.width + x) * 4];
            if (std::abs(int(p[0]) - px.rgba[0]) + std::abs(int(p[1]) - px.rgba[1]) +
                    std::abs(int(p[2]) - px.rgba[2]) > 12)
                ++differing;
        }
    return static_cast<double>(differing) / static_cast<double>((x1 - x0) * (y1 - y0));
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

TEST_CASE("Vellum audio displays change pixels after a published frame",
          "[vellum][editor][gpu][audio]") {
#if !defined(__APPLE__)
    SKIP("needs a GPU run by the lead");
#else
    pulp::format::HeadlessHost host(spectr::create_spectr);
    pulp::format::StateStoreParamPort port(host.state());
    pulp::vellum::ParamPortModel model(port);
    Context context(port);
    pulp::vellum_audio::AudioDisplayProvider provider({
        {"output", "linear", ::vellum::value::ChannelShape::meter, 0.0f},
        {"analyzer", "dBFS", ::vellum::value::ChannelShape::vector, -120.0f}});
    pulp::vellum::EditorEnv env{context, model, &provider};
    spectr::vellum_editor::RootView root(env);
    std::string error;
    REQUIRE(pulp::vellum::register_editor_dawn_bootstrap("com.pulp.spectr.audio.test", &error));
    ::vellum::view::PluginViewHost::Options options;
    options.size = {400, 300};
    options.use_gpu = true;
    auto gpu = ::vellum::view::PluginViewHost::create(root, options);
    REQUIRE(gpu != nullptr);
    if (!gpu->is_gpu_backed()) SKIP("needs a GPU run by the lead");
    root.set_bounds({0, 0, 400, 300});
    root.layout_children();
    root.editor_idle();
    REQUIRE(gpu->present_first_frame());
    const auto silence = gpu->capture_back_buffer_png();
    REQUIRE(root.audio_row() != nullptr);
    REQUIRE(root.audio_scope() != nullptr);
    REQUIRE(root.audio_meter() != nullptr);
    const auto row = absolute_bounds(*root.audio_row());
    // The displays sit in their own row: no bound control overlaps it.
    const auto controls = collect_values(root);
    REQUIRE(controls.size() == 66);
    std::size_t overlapping = 0;
    for (auto* control : controls) {
        auto* view = dynamic_cast<::vellum::view::View*>(control);
        REQUIRE(view != nullptr);
        const auto r = absolute_bounds(*view);
        if (r.x < row.right() && r.right() > row.x && r.y < row.bottom() && r.bottom() > row.y)
            ++overlapping;
    }
    CHECK(overlapping == 0);

    const std::array<float, 2> rms{0.25f, 0.25f};
    const std::array<float, 2> peak{0.5f, 0.5f};
    // A frame the scope's mapping accepts: fft_size / 2 + 1 bins, one peak.
    std::vector<float> bins(::spectr::kAnalyzerDisplayBins, -120.0f);
    bins[bins.size() / 8] = -3.0f;
    provider.publish_meter(0, rms, peak);
    provider.publish_scope(1, bins);
    root.editor_idle();
    REQUIRE(gpu->present_first_frame());
    const auto signal = gpu->capture_back_buffer_png();
    if (const char* dir = std::getenv("SPECTR_GPU_EDITOR_CAPTURE_DIR"))
        std::ofstream(std::string(dir) + "/audio-signal.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(signal.data()), static_cast<std::streamsize>(signal.size()));
    const auto quiet = decode_png(silence);
    const auto loud = decode_png(signal);
    // Each display changes inside its own box; a frame either one drops leaves
    // its box unchanged. Thresholds sit well below a drawn trace or bar and
    // well above anti-aliasing noise at the box edges.
    CHECK(changed_fraction(quiet, loud, absolute_bounds(*root.audio_scope())) > 0.01);
    CHECK(changed_fraction(quiet, loud, absolute_bounds(*root.audio_meter())) > 0.05);
    root.set_plugin_view_host(nullptr);
#endif
}

// Minimum edge a control may shrink to, and the fraction of its own box it
// must paint into. A knob's track ring and thumb cover far more than 5% of its
// square; the old single-column layout gave each knob a sliver that is
// near-empty.
TEST_CASE("Vellum editor lays every control out legibly and reflows on resize",
          "[vellum][editor][gpu][layout]") {
#if !defined(__APPLE__)
    SKIP("Vellum ships its GPU plug-in view host on macOS only");
#else
    constexpr float kMinEdge = 24.0f;
    constexpr double kMinPaintedFraction = 0.05;
    constexpr double kMinFramePaintedFraction = 0.05;

    pulp::format::HeadlessHost host(spectr::create_spectr);
    pulp::format::StateStoreParamPort port(host.state());
    pulp::vellum::ParamPortModel model(port);
    Context context(port);
    pulp::vellum::EditorEnv env{context, model, nullptr};
    spectr::vellum_editor::RootView root(env);

    std::string error;
    REQUIRE(pulp::vellum::register_editor_dawn_bootstrap("com.pulp.spectr.layout.test", &error));
    ::vellum::view::PluginViewHost::Options options;
    options.size = {400, 300};
    options.use_gpu = true;
    auto gpu = ::vellum::view::PluginViewHost::create(root, options);
    REQUIRE(gpu != nullptr);
    if (!gpu->is_gpu_backed())
        SKIP("no GPU adapter: the host fell back without a surface");

    const char* capture_dir = std::getenv("SPECTR_GPU_EDITOR_CAPTURE_DIR");
    // 14 model controls + 2 LFOs x 13 routes x {amount, Depth}.
    constexpr size_t kControls = 14 + 2 * 13 * 2;

    struct Size { float w, h; };
    for (const Size size : {Size{400, 300}, Size{800, 600}}) {
        CAPTURE(size.w, size.h);
        // The editor host resizes the surface and the root together.
        gpu->set_size(static_cast<uint32_t>(size.w), static_cast<uint32_t>(size.h));
        root.set_bounds({0, 0, size.w, size.h});
        root.layout_children();
        root.editor_idle();
        root.layout_children();
        REQUIRE(gpu->present_first_frame());

        const auto values = collect_values(root);
        REQUIRE(values.size() == kControls);
        const auto png = gpu->capture_back_buffer_png();
        REQUIRE_FALSE(png.empty());
        if (capture_dir)
            std::ofstream(std::string(capture_dir) + "/layout-" + std::to_string(int(size.w)) +
                              "x" + std::to_string(int(size.h)) + ".png",
                          std::ios::binary)
                .write(reinterpret_cast<const char*>(png.data()),
                       static_cast<std::streamsize>(png.size()));
        const Pixels px = decode_png(png);
        REQUIRE(px.width > 0);

        size_t bad_bounds = 0, unpainted = 0;
        for (auto* value : values) {
            const auto* view = dynamic_cast<const ::vellum::view::View*>(value);
            REQUIRE(view != nullptr);
            const auto r = absolute_bounds(*view);
            if (r.width < kMinEdge || r.height < kMinEdge || r.x < 0 || r.y < 0 ||
                r.right() > size.w + 0.5f || r.bottom() > size.h + 0.5f)
                ++bad_bounds;
            if (painted_fraction(px, r) < kMinPaintedFraction) ++unpainted;
        }
        CHECK(bad_bounds == 0);
        CHECK(unpainted == 0);

        const ::vellum::view::Rect whole{0, 0, float(px.width), float(px.height)};
        CHECK(painted_fraction(px, whole) > kMinFramePaintedFraction);
    }

    root.set_plugin_view_host(nullptr);
    gpu.reset();
#endif
}
