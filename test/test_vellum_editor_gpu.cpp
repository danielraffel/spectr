// Render the materialized Spectr editor through the package's GPU host.
#include "vellum_editor_harness.hpp"

#include <pulp/format/headless.hpp>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#endif

#ifdef __APPLE__
namespace {

bool metal_available() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        const bool available = device != nil;
        [device release];
        return available;
    }
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
            CGBitmapInfo(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
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


struct GpuEditor {
    pulp::format::HeadlessHost audio{spectr::create_spectr};
    std::optional<pulp::format::StateStoreParamPort> port;
    std::optional<spectr_test::VellumContext> context;
    vellum::view::FrameClock clock;
    std::unique_ptr<pulp::format::Editor> editor;
    spectr::Spectr* processor = nullptr;
    spectr::vellum_editor::RootView* root = nullptr;
    vellum::view::PluginViewHost* gpu = nullptr;

    GpuEditor() {
        audio.prepare(48000.0, 512);
        processor = audio.processor_as<spectr::Spectr>();
        REQUIRE(processor != nullptr);
        port.emplace(audio.state());
        context.emplace(*port);
        auto* factory = pulp::vellum::generated::SpectrVellumEditor();
        editor = factory->create(*processor, *context, pulp::format::EditorRole::editor);
        REQUIRE(editor != nullptr);
        root = dynamic_cast<spectr::vellum_editor::RootView*>(processor->native_editor_root());
        REQUIRE(root != nullptr);
        gpu = root->plugin_view_host();
        if (!gpu || !gpu->is_gpu_backed())
            SKIP("needs a GPU run by the lead");
        root->set_frame_clock(&clock);
        root->editor_opened();
        settle();
    }

    vellum::view::WidgetBridge& bridge() {
        REQUIRE(processor->native_scripted_ui() != nullptr);
        auto* bridge = processor->native_scripted_ui()->bridge();
        REQUIRE(bridge != nullptr);
        return *bridge;
    }

    void settle() {
        for (int frame = 0; frame < 20; ++frame) {
            editor->idle();
            clock.tick(1.0f / 60.0f);
        }
    }

    Pixels capture() {
        REQUIRE(gpu->present_first_frame());
        const auto png = gpu->capture_back_buffer_png();
        REQUIRE_FALSE(png.empty());
        auto pixels = decode_png(png);
        REQUIRE_FALSE(pixels.rgba.empty());
        return pixels;
    }

    // The document's client rect uses design coordinates. Convert to the
    // captured back buffer so the checks work at either backing scale.
    vellum::view::Rect pixels_for(const char* selector, const Pixels& image) {
        const auto result = spectr_test::evaluate(bridge(),
            std::string("(() => {const r = document.querySelector('") + selector
            + "').getBoundingClientRect(); return JSON.stringify([r.x, r.y, r.width, r.height]);})()");
        const auto rect = choc::json::parse(result);
        const float sx = float(image.width) / spectr::kEditorDesignWidth;
        const float sy = float(image.height) / spectr::kEditorDesignHeight;
        auto num = [](const choc::value::ValueView& v) {
            return v.isInt() ? float(v.getInt64()) : float(v.getFloat64());
        };
        return {num(rect[0]) * sx, num(rect[1]) * sy,
                num(rect[2]) * sx, num(rect[3]) * sy};
    }
};

bool changed_in(const Pixels& before, const Pixels& after, const vellum::view::Rect& r) {
    REQUIRE(before.width == after.width);
    REQUIRE(before.height == after.height);
    const auto x0 = std::max(0, int(std::floor(r.x)));
    const auto y0 = std::max(0, int(std::floor(r.y)));
    const auto x1 = std::min(int(before.width), int(std::ceil(r.right())));
    const auto y1 = std::min(int(before.height), int(std::ceil(r.bottom())));
    REQUIRE(x1 > x0);
    REQUIRE(y1 > y0);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto i = (y * before.width + x) * 4;
            if (std::abs(int(before.rgba[i]) - after.rgba[i])
              + std::abs(int(before.rgba[i + 1]) - after.rgba[i + 1])
              + std::abs(int(before.rgba[i + 2]) - after.rgba[i + 2]) > 12)
                return true;
        }
    return false;
}

} // namespace
#endif

TEST_CASE("Vellum editor repaints a host Mix change on the GPU host", "[vellum][editor][gpu]") {
#ifndef __APPLE__
    SKIP("needs a GPU run by the lead");
#else
    if (!metal_available()) SKIP("needs a GPU run by the lead");
    GpuEditor h;
    const auto before = h.capture();
    const auto bounds = h.pixels_for("[data-spectr-mix]", before);
    auto& store = h.audio.state();
    store.set_value(spectr::kMix, store.get_value(spectr::kMix));
    h.settle();
    CHECK_FALSE(changed_in(before, h.capture(), bounds));

    store.set_value(spectr::kMix, 37.0f);
    h.settle();
    CHECK(spectr_test::evaluate(h.bridge(),
        "document.querySelector('[data-spectr-mix]').getAttribute('aria-valuenow')") == "37");
    CHECK(changed_in(before, h.capture(), bounds));
#endif
}

TEST_CASE("Vellum editor paints the output meter and analyzer after processing audio",
          "[vellum][editor][gpu][audio]") {
#ifndef __APPLE__
    SKIP("needs a GPU run by the lead");
#else
    if (!metal_available()) SKIP("needs a GPU run by the lead");
    GpuEditor h;
    const auto silence = h.capture();
    const auto meter = h.pixels_for("[data-spectr-output-peak]", silence);
    const auto analyzer = h.pixels_for("canvas", silence);
    pulp::audio::Buffer<float> input(2, 512), output(2, 512);
    const float* channels[] = {input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> input_view(channels, 2, 512);
    for (int block = 0; block < 20; ++block) {
        for (std::size_t i = 0; i < 512; ++i) {
            const float sample = 0.5f * std::sin(
                2.0 * std::numbers::pi * 997.0 * (block * 512 + i) / 48000.0);
            input.channel(0)[i] = sample;
            input.channel(1)[i] = sample;
        }
        auto output_view = output.view();
        h.audio.process(output_view, input_view);
    }
    h.settle();
    const auto signal = h.capture();
    CHECK(changed_in(silence, signal, meter));
    CHECK(changed_in(silence, signal, analyzer));
#endif
}

TEST_CASE("Vellum editor keeps authored layout while the surface resizes",
          "[vellum][editor][gpu][layout]") {
#ifndef __APPLE__
    SKIP("needs a GPU run by the lead");
#else
    if (!metal_available()) SKIP("needs a GPU run by the lead");
    GpuEditor h;
    const auto before = h.capture();
    const auto first = h.pixels_for("[data-spectr-mix]", before);
    REQUIRE(h.editor->set_size({1320, 860, 1.0}));
    h.settle();
    const auto after = h.capture();
    const auto second = h.pixels_for("[data-spectr-mix]", after);
    CHECK(h.root->bounds().width == 1320.0f);
    CHECK(h.root->bounds().height == 860.0f);
    CHECK(after.width > before.width);
    CHECK(after.height > before.height);
    CHECK(second.width > first.width);
    CHECK(second.height > first.height);
#endif
}
