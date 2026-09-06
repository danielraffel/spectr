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
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view)) {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      " text='%s' fs=%.2f own=%d ih=%.2f iw=%.2f",
                      std::string(label->text()).c_str(),
                      label->font_size(), label->has_own_font_size() ? 1 : 0,
                      label->intrinsic_height(), label->intrinsic_width());
        text = buf;
    }
    const auto& fx = view.flex();
    std::printf("%s%s [%.1f,%.1f %.1fx%.1f] vis=%d children=%zu"
                " grow=%.2f shrink=%.2f pref=%.1fx%.1f min_h=%.1f max_h=%.1f"
                " pad_t=%.1f pad=%.1f dir=%d IH=%.1f IW=%.1f dh=%.1f/%d abs=%d"
                " basis=%.1f dbasis=%.1f/%d%s\n",
                indent.c_str(), typeid(view).name(), bounds.x, bounds.y,
                bounds.width, bounds.height, view.visible() ? 1 : 0,
                view.child_count(),
                fx.flex_grow, fx.flex_shrink,
                fx.preferred_width, fx.preferred_height,
                fx.min_height, fx.max_height,
                fx.padding_top, fx.padding,
                static_cast<int>(fx.direction),
                view.intrinsic_height(), view.intrinsic_width(),
                fx.dim_height.value, static_cast<int>(fx.dim_height.unit),
                static_cast<int>(view.position()), fx.flex_basis,
                fx.dim_flex_basis.value, static_cast<int>(fx.dim_flex_basis.unit),
                text.c_str());
    for (std::size_t index = 0; index < view.child_count(); ++index)
        dump_tree(*view.child_at(index), depth + 1, max_depth);
}

// Ancestor chain of a label, innermost first. A clipped label is almost never
// clipped by itself: the constraint lives in some ancestor's box, and printing
// only the label measures the wrong thing.
void dump_label_chain(const pulp::view::View& root, std::string_view text) {
    const pulp::view::Label* label = find_label(root, text);
    if (label == nullptr) {
        std::printf("[CHAIN] label '%s' NOT FOUND\n", std::string(text).c_str());
        return;
    }
    std::printf("[CHAIN] '%s' fs=%.2f iw=%.2f ih=%.2f\n",
                std::string(label->text()).c_str(), label->font_size(),
                label->intrinsic_width(), label->intrinsic_height());
    int depth = 0;
    for (const pulp::view::View* node = label; node != nullptr;
         node = node->parent(), ++depth) {
        const auto b = node->bounds();
        const auto& fx = node->flex();
        std::printf("  [%d] %-28s [%.1f,%.1f %.1fx%.1f] IW=%.1f grow=%.2f"
                    " shrink=%.2f basis=%.1f dbasis=%.1f/%d pref_w=%.1f"
                    " dim_w=%.1f/%d min_w=%.1f max_w=%.1f dir=%d children=%zu\n",
                    depth, typeid(*node).name(), b.x, b.y, b.width, b.height,
                    node->intrinsic_width(), fx.flex_grow, fx.flex_shrink,
                    fx.flex_basis, fx.dim_flex_basis.value,
                    static_cast<int>(fx.dim_flex_basis.unit),
                    fx.preferred_width, fx.dim_width.value,
                    static_cast<int>(fx.dim_width.unit),
                    fx.min_width, fx.max_width,
                    static_cast<int>(fx.direction), node->child_count());
        if (depth >= 6) break;
    }
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
    // Non-throwing existence check. require_reachable ASSERTS presence, which
    // is wrong for a control that is legitimately absent while its disclosure
    // is closed.
    bool is_mounted(std::string_view selector) {
        try {
            eval("(() => { if (!document.querySelector(" + js_string(selector)
                     + ")) throw new Error('absent'); })();",
                 "spectr-native-shot-ismounted");
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

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

    // Dump the settings body's own children. A correct viewport with nothing
    // painted means the clip is no longer the problem and the children are --
    // so measure them rather than infer from a blank picture.
    void report_settings_children() {
        eval("(() => { const body = document.querySelector('[data-spectr-settings-body]'); "
             "if (!body) { console.log('[shot] children: NO settings body'); return; } "
             "const br = body.getBoundingClientRect(); "
             "const kids = Array.from(body.children).slice(0, 8).map((n, i) => { "
             "  const r = n.getBoundingClientRect(); const cs = getComputedStyle(n); "
             "  return i + ':' + n.tagName + ' [' + r.x.toFixed(0) + ',' + r.y.toFixed(0) "
             "    + ' ' + r.width.toFixed(0) + 'x' + r.height.toFixed(0) + ']' "
             "    + ' disp=' + cs.display + ' vis=' + cs.visibility + ' op=' + cs.opacity; }); "
             "console.log('[shot] body [' + br.x.toFixed(0) + ',' + br.y.toFixed(0) + ' ' "
             "  + br.width.toFixed(0) + 'x' + br.height.toFixed(0) + '] childCount=' "
             "  + body.children.length + ' :: ' + kids.join('  |  ')); })();",
             "spectr-native-shot-children");
    }

    // Report every leaf text node whose content does not fit its own box.
    // Truncated labels ("SNAPSHO"), colliding chips and off-centre glyphs are
    // all one symptom -- a box sized from a text measurement that disagrees
    // with what is painted -- so enumerate them rather than eyeballing a PNG.
    void report_text_fit(const char* label) {
        eval(std::string("(() => { const rows = []; let leaves = 0; "
             "for (const n of document.querySelectorAll('*')) { "
             "  if (n.children.length) continue; "
             "  const t = (n.textContent || '').trim(); if (!t) continue; "
             "  leaves++; "
             "  if (n.scrollWidth > n.clientWidth + 1) rows.push("
             "    JSON.stringify(t.slice(0, 16)) + ' ' + n.scrollWidth + '>' + n.clientWidth); } "
             "console.log('[shot] textfit ") + label +
             "' + ' clipped=' + rows.length + '/' + leaves + ' :: ' + rows.slice(0, 14).join('  |  ')); "
             "const probe = []; "
             "for (const n of document.querySelectorAll('*')) { "
             "  if (n.children.length) continue; "
             "  const t = (n.textContent || '').trim(); "
             "  if (!/^(SNAPSHOT|SPECTRAL|CLEAR|BANK|MORPH|NONE)/.test(t)) continue; "
             "  const cs = getComputedStyle(n); "
             "  probe.push(JSON.stringify(t.slice(0,12)) + ' ls=' + cs.letterSpacing "
             "    + ' fam=' + (cs.fontFamily||'').slice(0,22) + ' px=' + cs.fontSize "
             "    + ' w=' + n.getBoundingClientRect().width.toFixed(1) "
             "    + ' sw=' + n.scrollWidth); } "
             "console.log('[shot] typeprobe :: ' + probe.slice(0, 10).join('  |  ')); })();",
             "spectr-native-shot-textfit");
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

        if (std::getenv("SPECTR_PROBE_TEXT") != nullptr)
            dump_label_chain(*rig.root, std::getenv("SPECTR_PROBE_TEXT"));
        rig.report_text_fit("home");
        rig.report_settings_children();
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
        // Target/Targets are now part of each LFO's disclosure: they do not
        // exist while their LFO is off. That is the intended state, so probe
        // for them instead of asserting them, and say which state we captured.
        // Treating "absent" as a failure would make the correct behaviour red.
        const bool targets_mounted = rig.is_mounted(
            "[data-spectr-modulation-target=\"bank\"]");
        std::printf("modulation targets mounted with LFO off: %s%s\n",
                    targets_mounted ? "yes" : "no",
                    targets_mounted ? "  (expected: collapsed)" : "  (collapsed, as intended)");
        if (targets_mounted) {
        rig.require_reachable("[data-spectr-modulation-target=\"bank\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"snapshot-a\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"snapshot-b\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"morph\"]");
        rig.require_reachable("[data-spectr-modulation-select=\"all\"]");
        rig.require_reachable("[data-spectr-modulation-select=\"none\"]");
        }

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

        // The DOM shim reports 0x0 rects and undefined styles, so it cannot
        // explain a blank panel. Walk the NATIVE children of the body instead:
        // this distinguishes "laid out somewhere sane but not painted" from
        // "laid out at zero size or off-viewport".
        {
            const auto& sb = scroll->bounds();
            std::printf("native body bounds=[%.1f,%.1f %.1fx%.1f] scroll_y=%.1f children=%zu\n",
                        sb.x, sb.y, sb.width, sb.height, scroll->scroll_y(),
                        scroll->child_count());
            const std::size_t shown = scroll->child_count() < 6 ? scroll->child_count() : 6;
            for (std::size_t i = 0; i < shown; ++i) {
                const auto* kid = scroll->child_at(i);
                if (kid == nullptr) { std::printf("  child %zu: null\n", i); continue; }
                const auto& r = kid->bounds();
                std::printf("  child %zu: [%.1f,%.1f %.1fx%.1f] visible=%s children=%zu\n",
                            i, r.x, r.y, r.width, r.height,
                            kid->visible() ? "yes" : "NO", kid->child_count());
            }
        }

        float modulation_y = 0.0f;
        content_offset(*modulation, *scroll, modulation_y);
        std::printf("settings body: viewport=%.1fx%.1f content=%.1f  "
                    "MODULATION group [%.1f,%.1f %.1fx%.1f] at content y=%.1f\n",
                    scroll->bounds().width, scroll->bounds().height,
                    scroll->content_size().height, group->bounds().x,
                    group->bounds().y, group->bounds().width,
                    group->bounds().height, modulation_y);
        if (std::getenv("SPECTR_SHOT_DUMP") != nullptr) {
            {
                using namespace pulp::view;
                auto probe = std::make_unique<ScrollView>();
                probe->set_bounds({0, 0, 466, 531});
                probe->flex().padding_top = 50.0f;
                auto content_owned = std::make_unique<View>();
                View* pc = content_owned.get();
                pc->flex().flex_grow = 0.0f;
                pc->flex().flex_shrink = 0.0f;
                pc->flex().padding_top = 50.0f;
                pc->flex().flex_basis = 0.0f;
                auto group_owned = std::make_unique<View>();
                View* pg = group_owned.get();
                auto lab_owned = std::make_unique<Label>("APPEARANCE");
                Label* pl = lab_owned.get();
                pl->set_font_size(9.0f);
                pg->add_child(std::move(lab_owned));
                pc->add_child(std::move(group_owned));
                probe->add_child(std::move(content_owned));
                probe->layout_children();
                std::printf("[PROBE] content=%.1fx%.1f group=%.1fx%.1f label=%.1fx%.1f "
                            "label_ih=%.1f content_IH=%.1f\n",
                            pc->bounds().width, pc->bounds().height,
                            pg->bounds().width, pg->bounds().height,
                            pl->bounds().width, pl->bounds().height,
                            pl->intrinsic_height(), pc->intrinsic_height());
            }
            std::printf("--- settings body subtree ---\n");
            dump_tree(*scroll, 0, 9);
            {
                pulp::view::View* relayout_root = scroll;
                while (relayout_root->parent()) relayout_root = relayout_root->parent();
                relayout_root->layout_children();
                std::printf("--- settings body subtree AFTER forced relayout ---\n");
                dump_tree(*scroll, 0, 9);
            }
            std::printf("--- end subtree ---\n");
        }

        // Scroll the shipping body as far as it will go toward MODULATION.
        const float wanted = modulation_y - 24.0f;
        scroll->set_scroll(0.0f, wanted < 0.0f ? 0.0f : wanted);
        settle(rig.clock, 24);
        std::printf("scrolled shipping settings body to y=%.1f\n",
                    scroll->scroll_y());
        capture(rig, dir, prefix + "03-settings-SHIPPING-scrolled", backend, scale);

        // ── The MODULATION disclosure, driven on the SHIPPING panel ───────
        //
        // Target selection is progressive disclosure: the destination chips do
        // not exist until their LFO is enabled, so a capture taken with the
        // LFOs off cannot show them. Drive the real toggles with real clicks
        // and re-scroll after each one, because enabling an LFO grows the
        // scroll content and the group moves.
        // `anchor` is re-found on every call: enabling an LFO re-renders the
        // group, so a View* captured before the click can be stale, and the
        // node we want in view moves as the content grows. Scrolling to the
        // MODULATION heading is right while the section is short, but once
        // LFO 2 is open its destination chips sit below the fold -- anchoring
        // there is what made three target states capture byte-identically.
        const auto show_modulation = [&](const std::string& name,
                                         const char* anchor) {
            settle(rig.clock, 24);
            const pulp::view::Label* node = find_label(*rig.root, anchor);
            if (node == nullptr)
                throw std::runtime_error(std::string("anchor '") + anchor
                                         + "' not found for " + name);
            auto* owner = owning_scroll_view(*node);
            if (owner == nullptr)
                throw std::runtime_error(std::string("anchor '") + anchor
                                         + "' is outside any ScrollView");
            float y = 0.0f;
            if (!content_offset(*node, *owner, y))
                throw std::runtime_error(std::string("anchor '") + anchor
                                         + "' is not under its ScrollView");
            const float want = y - 24.0f;
            owner->set_scroll(0.0f, want < 0.0f ? 0.0f : want);
            settle(rig.clock, 24);
            std::printf("%s: anchor '%s' at content y=%.1f, scrolled to %.1f\n",
                        name.c_str(), anchor, y, owner->scroll_y());
            capture(rig, dir, prefix + name, backend, scale);
        };

        rig.activate("[data-spectr-modulation-lfo]");
        show_modulation("04-settings-MODULATION-lfo1-expanded", "MODULATION");

        rig.activate("[data-spectr-modulation-lfo2]");
        show_modulation("05-settings-MODULATION-lfo2-expanded", "MODULATION");

        // Only NOW may the destination chips be asserted. Requiring them
        // before this point would make the correct collapsed state red.
        rig.require_reachable("[data-spectr-modulation-target=\"bank\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"snapshot-a\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"snapshot-b\"]");
        rig.require_reachable("[data-spectr-modulation-target=\"morph\"]");
        rig.require_reachable("[data-spectr-modulation-select=\"all\"]");
        rig.require_reachable("[data-spectr-modulation-select=\"none\"]");

        rig.activate("[data-spectr-modulation-select=\"none\"]");
        rig.report_target_state("NONE");
        show_modulation("06-settings-MODULATION-targets-none", "Targets");

        rig.activate("[data-spectr-modulation-target=\"morph\"]");
        rig.report_target_state("MORPH");
        show_modulation("07-settings-MODULATION-targets-morph-only", "Targets");

        rig.activate("[data-spectr-modulation-select=\"all\"]");
        rig.report_target_state("ALL");
        show_modulation("08-settings-MODULATION-targets-all", "Targets");

        // Back to the collapsed state, proving the disclosure closes as well
        // as it opens -- a one-way drive would hide a stuck-open bug.
        rig.activate("[data-spectr-modulation-lfo2]");
        rig.activate("[data-spectr-modulation-lfo]");
        show_modulation("09-settings-MODULATION-collapsed-again", "MODULATION");
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
