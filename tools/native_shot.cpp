// Headless native-editor screenshot harness.
//
// Mounts the SHIPPING native editor — the materialized runtime package
// (materialized-document.runtime.json + runtime.js) embedded in
// spectr_native_assets — through the same Processor::create_view() /
// ScriptedUiSession path the plugin uses in a host, drives it to a named
// surface, and rasterises the resulting Pulp view tree.
//
// Why this exists: the browser design source (resources/editor.html) has no
// Settings MODULATION group at all. That group is injected by the native patch
// layer, so a browser capture cannot show it and cannot be used to review it.
// This binary is the only way to see the shipping native surface without a DAW.
//
// The editor root sets requires_gpu_host(), so the honest default is the
// offscreen Dawn+Skia surface (ScreenshotBackend::gpu). The CPU Skia raster
// backend is selectable for comparison; both are Skia.
//
// Captures are floored by analyze_screenshot_content(), which catches a wholly
// dead frame. That floor is measured over the ENTIRE image and is therefore NOT
// able to certify that a modal's body painted: a Spectr frame clears it on the
// dimmed editor behind the modal alone. Read the per-capture statistics this
// prints, and look at the image.

#include "spectr/spectr.hpp"

#include <pulp/audio/buffer.hpp>
#include <pulp/format/format.hpp>
#include <pulp/midi/buffer.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/screenshot_compare.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/ui_components.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widget_bridge.hpp>
#include <pulp/view/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr float kDesignWidth = 1320.0f;
constexpr float kDesignHeight = 860.0f;

int g_failures = 0;

void settle(pulp::view::FrameClock& clock, int frames) {
    for (int frame = 0; frame < frames; ++frame) clock.tick(1.0f / 60.0f);
}

std::string js_string(std::string_view value) {
    std::string result{"\""};
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') result.push_back('\\');
        result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

const pulp::view::Label* find_label(const pulp::view::View& view,
                                    std::string_view text) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
        label != nullptr && label->text() == text)
        return label;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        if (const auto* match = find_label(*view.child_at(index), text))
            return match;
    return nullptr;
}

// The ScrollView that actually owns a node, not merely the first one in tree
// order. The editor has several; picking by tree order silently measures the
// wrong viewport.
pulp::view::ScrollView* owning_scroll_view(const pulp::view::View& view) {
    for (auto* node = const_cast<pulp::view::View*>(&view); node != nullptr;
         node = node->parent())
        if (auto* scroll = dynamic_cast<pulp::view::ScrollView*>(node))
            return scroll;
    return nullptr;
}

void collect_scroll_views(pulp::view::View& view,
                          std::vector<pulp::view::ScrollView*>& out) {
    if (auto* scroll = dynamic_cast<pulp::view::ScrollView*>(&view))
        out.push_back(scroll);
    for (std::size_t index = 0; index < view.child_count(); ++index)
        collect_scroll_views(*view.child_at(index), out);
}

// Height the modal panel leaves for its body: the panel's own height minus the
// body's offset inside it. Derived from the live tree rather than hardcoded, so
// a panel resize does not silently invalidate the override below.
float panel_height_for(const pulp::view::View& body) {
    const auto* panel = body.parent();
    if (panel == nullptr) return 0.0f;
    return panel->bounds().height - body.bounds().y - 24.0f;
}

void dump_tree(const pulp::view::View& view, int depth, int max_depth) {
    if (depth > max_depth) return;
    const auto bounds = view.bounds();
    std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    std::string text;
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view))
        text = " text='" + std::string(label->text()) + "'";
    std::printf("%s%s [%.1f,%.1f %.1fx%.1f] vis=%d children=%zu%s\n",
                indent.c_str(), typeid(view).name(), bounds.x, bounds.y,
                bounds.width, bounds.height, view.visible() ? 1 : 0,
                view.child_count(), text.c_str());
    for (std::size_t index = 0; index < view.child_count(); ++index)
        dump_tree(*view.child_at(index), depth + 1, max_depth);
}

// Offset of `view` in `ancestor`'s CONTENT space. ScrollView::paint_all
// translates its children by (-scroll_x, -scroll_y) rather than rewriting their
// bounds, so this sum is scroll-independent and can be used to compute the
// scroll offset that brings a node into the viewport.
bool content_offset(const pulp::view::View& view,
                    const pulp::view::View& ancestor,
                    float& out_y) {
    float y = 0.0f;
    for (const auto* node = &view; node != nullptr; node = node->parent()) {
        if (node == &ancestor) {
            out_y = y;
            return true;
        }
        y += node->bounds().y;
    }
    return false;
}

struct Rig {
    pulp::state::StateStore store;
    spectr::Spectr processor;
    std::unique_ptr<pulp::view::View> root;
    pulp::view::FrameClock clock;
    pulp::view::ScriptedUiSession* session = nullptr;

    Rig() {
        processor.set_state_store(&store);
        processor.define_parameters(store);
        pulp::format::PrepareContext prepare;
        prepare.sample_rate = 48000.0;
        prepare.max_buffer_size = 256;
        prepare.input_channels = 2;
        prepare.output_channels = 2;
        processor.prepare(prepare);

        root = processor.create_view();
        if (!root) throw std::runtime_error("create_view() returned null");
        root->set_bounds({0, 0, kDesignWidth, kDesignHeight});
        root->set_frame_clock(&clock);
        root->layout_children();
        processor.on_view_opened(*root);
        session = processor.active_scripted_ui();
        if (session == nullptr || session->bridge() == nullptr)
            throw std::runtime_error(
                "no scripted UI session - the materialized editor failed closed");
        settle(clock, 24);
    }

    ~Rig() {
        if (root) processor.on_view_closed(*root);
    }

    pulp::view::WidgetBridge& bridge() { return *session->bridge(); }

    // The materialized runtime lays its panels out from the host size it is
    // TOLD, via publish_native_layout_ -> __spectrResizeNativeEditor. A rig that
    // only mounts the view never delivers that, and the Settings body then
    // commits at a stub height with its content clipped away — a modal shell
    // over an empty box. Deliver a host size explicitly, as a host does.
    void resize(float width, float height) {
        processor.on_view_resized(*root, width, height);
        settle(clock, 24);
    }

    // Print the runtime's own layout receipt, so the geometry claim in this
    // report comes from the shipping runtime rather than from inference.
    void print_layout_receipt() {
        eval("(() => { const r = globalThis.__spectrResponsiveLayoutReceipt__; "
             "console.log('[shot] layout receipt: ' + JSON.stringify(r)); })();",
             "spectr-native-shot-receipt");
    }

    // Run JS in the shipping runtime. Throws with the runtime's own message.
    void eval(const std::string& script, const char* name) {
        bridge().load_script(script, name);
    }

    // Drive a real control through the importer's semantic activation seam and
    // then drain the Promise jobs and React commits a host would service.
    void activate(std::string_view selector, std::string_view event = "click") {
        eval("(() => { if (!globalThis.__pulpActivateMaterializedElement__("
                 + js_string(selector) + "," + js_string(event) + ",null)) "
                 "throw new Error('activation failed: " + std::string(selector) + "'); "
                 "if (typeof globalThis.__pulpRuntimeSettle__ === 'function') "
                 "globalThis.__pulpRuntimeSettle__(8); })();",
             "spectr-native-shot-activate");
        settle(clock, 16);
        root->layout_children();
        settle(clock, 8);
    }

    // Assert a selector is mounted AND actually reachable: no display:none
    // ancestor, and a non-degenerate rendered box. A mounted-but-hidden control
    // is exactly the MOD-1 defect, and a source-text check cannot see it.
    void require_reachable(std::string_view selector) {
        eval("(() => { const el = document.querySelector(" + js_string(selector) + "); "
                 "if (!el) throw new Error('not mounted: " + std::string(selector) + "'); "
                 "for (let n = el; n; n = n.parentElement) { "
                 "  const s = (n.style && n.style.display) || ''; "
                 "  if (s === 'none') throw new Error('hidden ancestor: " + std::string(selector) + "'); } "
                 "const r = el.getBoundingClientRect ? el.getBoundingClientRect() : null; "
                 "if (!r || r.width <= 0 || r.height <= 0) "
                 "  throw new Error('zero box: " + std::string(selector) + "'); })();",
             "spectr-native-shot-reachable");
    }

    // Read the modulation targets' own pressed state back out of the shipping
    // runtime. Two identical renders are ambiguous by themselves: either the
    // click never changed anything, or it changed state that the native paint
    // does not reflect. This separates them.
    void report_target_state(const char* label) {
        eval(std::string("(() => { const q = (s) => { const e = "
             "document.querySelector(s); return e ? String("
             "e.getAttribute('aria-pressed')) : 'absent'; }; "
             "console.log('[shot] targets after ") + label + "'"
             " + ' bank=' + q('[data-spectr-modulation-target=\"bank\"]')"
             " + ' a=' + q('[data-spectr-modulation-target=\"snapshot-a\"]')"
             " + ' b=' + q('[data-spectr-modulation-target=\"snapshot-b\"]')"
             " + ' morph=' + q('[data-spectr-modulation-target=\"morph\"]')"
             " + ' ALL=' + q('[data-spectr-modulation-select=\"all\"]')"
             " + ' NONE=' + q('[data-spectr-modulation-select=\"none\"]')); })();",
             "spectr-native-shot-target-state");
    }

    // Push a tone through the DSP so the analyzer surfaces carry real data
    // rather than a resting floor.
    void feed_tone(int chunks) {
        constexpr int block = 256;
        constexpr double sample_rate = 48000.0;
        constexpr double pi = 3.14159265358979323846;
        std::vector<float> in0(block), in1(block), out0(block), out1(block);
        const float* inputs[2]{in0.data(), in1.data()};
        float* outputs[2]{out0.data(), out1.data()};
        pulp::midi::MidiBuffer midi_in, midi_out;
        pulp::format::ProcessContext context;
        context.sample_rate = sample_rate;
        context.num_samples = block;
        for (int chunk = 0; chunk < chunks; ++chunk) {
            for (int sample = 0; sample < block; ++sample) {
                const auto index = static_cast<double>(chunk * block + sample);
                const auto value = static_cast<float>(
                    0.5 * std::sin(2.0 * pi * 1000.0 * index / sample_rate)
                    + 0.25 * std::sin(2.0 * pi * 220.0 * index / sample_rate));
                in0[sample] = value;
                in1[sample] = value;
            }
            pulp::audio::BufferView<const float> input(inputs, 2, block);
            pulp::audio::BufferView<float> output(outputs, 2, block);
            processor.process(output, input, midi_in, midi_out, context);
            clock.tick(1.0f / 30.0f);
        }
    }
};

// Capture and gate. A written file is not a result: the SDK content floor is
// the oracle that separates "rendered" from "a dark empty frame".
void capture_view_tree(pulp::view::View& root,
                       std::uint32_t width,
                       std::uint32_t height,
                       const std::filesystem::path& dir,
                       const std::string& name,
                       pulp::view::ScreenshotBackend backend,
                       float scale,
                       bool require_floor) {
    const auto png = pulp::view::render_to_png(root, width, height, scale, backend);
    if (png.empty()) {
        std::fprintf(stderr, "FAIL %s: backend produced no bytes\n", name.c_str());
        ++g_failures;
        return;
    }
    const auto stats = pulp::view::analyze_screenshot_content(png);
    const auto path = dir / (name + ".png");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()),
              static_cast<std::streamsize>(png.size()));
    out.close();

    // The whole-frame floor is deliberately lenient and is measured over the
    // ENTIRE capture. A Spectr frame passes it on the dimmed editor behind a
    // modal alone, so it can NOT certify that a modal's body painted. It is
    // kept as a floor against a wholly dead frame; the per-image note in the
    // report is what says whether the region of interest actually rendered.
    const bool ok = !require_floor || stats.passes_content_floor();
    std::printf("%s %s  %ux%u  colors=%u lum_sd=%.2f nonbg=%.3f opaque=%.3f  %s\n",
                ok ? "OK  " : "FAIL", name.c_str(), stats.width, stats.height,
                stats.unique_colors, stats.luminance_stddev,
                stats.non_background_coverage, stats.opaque_coverage,
                path.string().c_str());
    if (!ok) ++g_failures;
}

void capture(Rig& rig,
             const std::filesystem::path& dir,
             const std::string& name,
             pulp::view::ScreenshotBackend backend,
             float scale) {
    capture_view_tree(*rig.root, static_cast<std::uint32_t>(kDesignWidth),
                      static_cast<std::uint32_t>(kDesignHeight), dir, name,
                      backend, scale, true);
}

const char* backend_name(pulp::view::ScreenshotBackend backend) {
    switch (backend) {
        case pulp::view::ScreenshotBackend::gpu: return "gpu (Dawn + Skia offscreen)";
        case pulp::view::ScreenshotBackend::skia: return "skia (CPU Skia raster)";
        case pulp::view::ScreenshotBackend::coregraphics: return "coregraphics";
        case pulp::view::ScreenshotBackend::auto_select: return "auto";
        default: return "default";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path dir = "spectr-native-shots";
    auto backend = pulp::view::ScreenshotBackend::gpu;
    float scale = 2.0f;
    std::string prefix;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg{argv[index]};
        if (arg.rfind("--out=", 0) == 0) {
            dir = std::string(arg.substr(6));
        } else if (arg.rfind("--backend=", 0) == 0) {
            const auto value = arg.substr(10);
            if (value == "gpu") backend = pulp::view::ScreenshotBackend::gpu;
            else if (value == "skia") backend = pulp::view::ScreenshotBackend::skia;
            else if (value == "coregraphics") backend = pulp::view::ScreenshotBackend::coregraphics;
            else {
                std::fprintf(stderr, "unknown backend: %.*s\n",
                             static_cast<int>(value.size()), value.data());
                return 2;
            }
        } else if (arg.rfind("--scale=", 0) == 0) {
            scale = std::stof(std::string(arg.substr(8)));
        } else if (arg.rfind("--prefix=", 0) == 0) {
            prefix = std::string(arg.substr(9));
        } else {
            std::fprintf(stderr,
                         "usage: %s [--out=DIR] [--backend=gpu|skia|coregraphics] "
                         "[--scale=N] [--prefix=STR]\n",
                         argv[0]);
            return 2;
        }
    }

    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) {
        std::fprintf(stderr, "cannot create %s: %s\n", dir.string().c_str(),
                     error.message().c_str());
        return 2;
    }

    std::printf("backend: %s   scale: %.1f   gpu_capture_available: %s\n",
                backend_name(backend), scale,
                pulp::view::has_gpu_capture() ? "yes" : "no");
    if (backend == pulp::view::ScreenshotBackend::gpu
        && !pulp::view::has_gpu_capture()) {
        std::fprintf(stderr,
                     "FAIL: --backend=gpu requested but this build has no GPU "
                     "capture (Skia/Dawn off). Refusing to silently substitute "
                     "another backend.\n");
        return 1;
    }

    try {
        Rig rig;
        rig.resize(kDesignWidth, kDesignHeight);
        rig.feed_tone(96);
        settle(rig.clock, 24);

        capture(rig, dir, prefix + "01-home", backend, scale);

        rig.activate("[data-spectr-settings-open]");
        rig.require_reachable("[data-spectr-settings-panel]");
        rig.print_layout_receipt();
        capture(rig, dir, prefix + "02-settings-SHIPPING", backend, scale);

        // The shipping Settings panel is a SINGLE SCROLL: the materialized asset
        // has no tab rail, and MODULATION is a group in the body. Reachability,
        // not mere presence, is the assertion that matters -- MOD-1 shipped
        // these same controls mounted behind a display:none ancestor, where
        // every static source-text check still passed.
        rig.require_reachable("[data-spectr-modulation-target=\"bank\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"snapshot-a\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"snapshot-b\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"morph\"]");
        rig.require_reachable("[data-spectr-modulation-select=\"all\"]");
        rig.require_reachable("[data-spectr-modulation-select=\"none\"]");

        const pulp::view::Label* modulation = nullptr;
        for (int attempt = 0; attempt < 64; ++attempt) {
            modulation = find_label(*rig.root, "MODULATION");
            if (modulation != nullptr) break;
            settle(rig.clock, 8);
        }
        if (modulation == nullptr)
            throw std::runtime_error(
                "no native MODULATION label - the group did not materialize");
        auto* group = const_cast<pulp::view::View*>(modulation->parent());
        if (group == nullptr)
            throw std::runtime_error("MODULATION label has no group parent");
        auto* scroll = owning_scroll_view(*modulation);
        if (scroll == nullptr)
            throw std::runtime_error("MODULATION is not inside any ScrollView");

        std::vector<pulp::view::ScrollView*> scrolls;
        collect_scroll_views(*rig.root, scrolls);
        std::printf("scroll views in tree: %zu\n", scrolls.size());
        for (const auto* candidate : scrolls)
            std::printf("  scroll view: bounds=%.1fx%.1f content=%.1fx%.1f\n",
                        candidate->bounds().width, candidate->bounds().height,
                        candidate->content_size().width,
                        candidate->content_size().height);

        float modulation_y = 0.0f;
        content_offset(*modulation, *scroll, modulation_y);
        std::printf("settings body: viewport=%.1fx%.1f content=%.1f  "
                    "MODULATION group [%.1f,%.1f %.1fx%.1f] at content y=%.1f\n",
                    scroll->bounds().width, scroll->bounds().height,
                    scroll->content_size().height, group->bounds().x,
                    group->bounds().y, group->bounds().width,
                    group->bounds().height, modulation_y);
        if (std::getenv("SPECTR_SHOT_DUMP") != nullptr) {
            std::printf("--- settings body subtree ---\n");
            dump_tree(*scroll, 0, 3);
            std::printf("--- end subtree ---\n");
        }

        // Scroll the shipping body as far as it will go toward MODULATION.
        const float wanted = modulation_y - 24.0f;
        scroll->set_scroll(0.0f, wanted < 0.0f ? 0.0f : wanted);
        settle(rig.clock, 24);
        std::printf("scrolled shipping settings body to y=%.1f\n",
                    scroll->scroll_y());
        capture(rig, dir, prefix + "03-settings-SHIPPING-scrolled", backend, scale);

        // ── Diagnostic 1: give the body the viewport its panel implies ─────
        //
        // The body ScrollView commits at a stub 50px viewport inside a 679px
        // panel, so the shipping frames above show an empty modal. Forcing only
        // the viewport height -- changing nothing else -- makes the groups that
        // DO lay out correctly visible, which separates "the content is
        // missing" from "the content is clipped".
        const float forced = panel_height_for(*scroll) > 0.0f
                                 ? panel_height_for(*scroll)
                                 : 529.0f;
        scroll->flex().preferred_height = forced;
        scroll->flex().preferred_width = scroll->bounds().width;
        rig.root->layout_children();
        settle(rig.clock, 16);
        scroll->set_scroll(0.0f, 0.0f);
        settle(rig.clock, 16);
        std::printf("forced settings viewport to %.1f (actual %.1f)\n", forced,
                    scroll->bounds().height);
        capture(rig, dir, prefix + "04-DIAGNOSTIC-forced-viewport-settings",
                backend, scale);

        // ── Diagnostic 2: the MODULATION group, rendered as its own root ────
        //
        // The group is laid out correctly (its own box and children are sane);
        // it is its ZERO-HEIGHT parent wrapper that clips it away. Rendering the
        // group's own subtree therefore shows the real native widgets at their
        // real size WITHOUT distorting the surrounding layout, which is what
        // forcing the ancestors' heights does. These frames are a component
        // slice and are named so -- they are not evidence of how the group
        // currently presents inside the panel.
        const auto group_width =
            static_cast<std::uint32_t>(group->bounds().width > 1.0f
                                           ? group->bounds().width
                                           : 466.0f);
        const auto group_height =
            static_cast<std::uint32_t>(group->bounds().height > 1.0f
                                           ? group->bounds().height
                                           : 214.0f);
        const auto slice = [&](const std::string& name) {
            settle(rig.clock, 12);
            capture_view_tree(*group, group_width, group_height, dir,
                              prefix + name, backend, scale, false);
        };
        rig.report_target_state("mount");
        slice("05-MODULATION-GROUP-SLICE-default");

        rig.activate("[data-spectr-modulation-select=\"none\"]");
        rig.report_target_state("NONE");
        slice("06-MODULATION-GROUP-SLICE-targets-none");

        rig.activate("[data-spectr-modulation-target=\"morph\"]");
        rig.report_target_state("MORPH");
        slice("07-MODULATION-GROUP-SLICE-targets-morph-only");

        rig.activate("[data-spectr-modulation-select=\"all\"]");
        rig.report_target_state("ALL");
        slice("08-MODULATION-GROUP-SLICE-targets-all");
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "FAIL: %s\n", failure.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d capture(s) failed the content floor\n", g_failures);
        return 1;
    }
    std::printf("all captures passed the content floor\n");
    return 0;
}
