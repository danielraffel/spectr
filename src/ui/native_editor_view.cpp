#include "spectr/ui/native_editor_view.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>

#include "spectr_editor_assets_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include <choc/javascript/choc_javascript.h>

namespace spectr {

namespace {

// Format a float into the JS source as a finite literal. NaN / Inf
// would otherwise serialize as the bare word "nan" / "inf" which is
// a syntax error in JS.
std::string js_number(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

} // namespace

NativeEditorView::NativeEditorView(Spectr& plugin)
    : plugin_(plugin) {
    // Root container for the JS-created widget tree. The bridge attaches
    // every JS-created widget under this view, so the editor's outermost
    // <View> from editor.tsx becomes our first child.
    flex().direction = pulp::view::FlexDirection::column;

    // Construct the bridge over the editor's StateStore so the JS side
    // can read/write Spectr's parameters (Mix, Output, Response, Engine,
    // Bands, Morph) directly via setValue / __dispatch__.
    bridge_ = std::make_unique<pulp::view::WidgetBridge>(
        engine_, *this, plugin_.state());

    // Diagnostic logger so JS-side console.log lands on stderr. Pulp's
    // QuickJS path doesn't ship a console binding by default; without
    // this the bundle's diagnostic logging is silent.
    engine_.register_function("__spectrLog",
        [](choc::javascript::ArgumentList args) {
            std::string line;
            for (size_t i = 0; i < args.numArgs; ++i) {
                if (i) line += ' ';
                if (args[i] && args[i]->isString()) {
                    line += args[i]->getString();
                } else if (args[i]) {
                    line += choc::json::toString(*args[i]);
                }
            }
            std::fprintf(stderr, "[spectr-js] %s\n", line.c_str());
            return choc::value::Value();
        });

    // Register a JS-callable "__spectrumTick" that invokes our C++ tick().
    // Combined with editor.tsx's requestAnimationFrame loop, this makes
    // the analyzer push driven by Pulp's host frame clock — no separate
    // Timer thread required. service_frame_callbacks() is already pumped
    // by the framework's per-frame UI loop (see threejs-native-demo).
    engine_.register_function("__spectrumTick",
        [this](choc::javascript::ArgumentList) {
            tick();
            return choc::value::Value();
        });

#ifdef SPECTR_NATIVE_EDITOR_JS_EMBEDDED
    // editor.js is built by `cd native-react && npm run build` and
    // embedded by CMake's pulp_add_binary_data step. The pointer +
    // size are exposed in spectr_editor_assets_data.hpp as
    // spectr_editor::editor_js / editor_js_size.
    const std::string js_source(
        reinterpret_cast<const char*>(spectr_editor::editor_js),
        spectr_editor::editor_js_size);
    bridge_->load_script(js_source);
    pulp::runtime::log_info("Spectr native editor: loaded editor.js ({} bytes)",
                            js_source.size());
#else
    // Build was configured with SPECTR_NATIVE_EDITOR=OFF or editor.js
    // wasn't packed. The view exists but renders nothing — Spectr's
    // create_view() should not have returned this in that case.
    pulp::runtime::log_warn("Spectr native editor: editor.js not embedded "
                            "(SPECTR_NATIVE_EDITOR is OFF). Empty editor.");
#endif
}

NativeEditorView::~NativeEditorView() = default;

void NativeEditorView::paint(pulp::canvas::Canvas& canvas) {
    // Diagnostic — log first few paint passes to confirm framework
    // is calling us. If silent, NativeEditorView isn't getting
    // attached/painted by the standalone window. Without paint()
    // running, nothing pumps service_frame_callbacks, so rAF
    // callbacks queue but never drain.
    static int paint_count = 0;
    paint_count++;
    if (paint_count <= 5 || paint_count % 60 == 0) {
        std::fprintf(stderr, "[NativeEditorView::paint] #%d bounds=(%d,%d) "
                     "children=%zu\n",
                     paint_count, static_cast<int>(bounds().width),
                     static_cast<int>(bounds().height), child_count());
        // Probe first child's bounds to see if Yoga laid them out.
        if (child_count() > 0) {
            auto* c = child_at(0);
            if (c) {
                auto cb = c->bounds();
                std::fprintf(stderr, "    child[0] bounds=(%d,%d,%dx%d) "
                             "children=%zu\n",
                             static_cast<int>(cb.x), static_cast<int>(cb.y),
                             static_cast<int>(cb.width),
                             static_cast<int>(cb.height), c->child_count());
            }
        }
    }
    // Pump JS-side requestAnimationFrame callbacks so loops driven
    // through the bridge actually fire (FilterBank's draw RAF, our
    // spectrum-tick RAF, etc.). Without this they queue but never run.
    if (bridge_) {
        bridge_->service_frame_callbacks();
        // Self-perpetuating frame loop. Any work the JS side queued
        // during service_frame_callbacks (effects firing, rAF
        // callbacks) needs ANOTHER paint to drain. Call the JS-side
        // `layout()` global which internally triggers
        // WidgetBridge::request_repaint (which is private to C++).
        // Without this loop, useEffect's MessageChannel → setTimeout
        // → __requestFrame__ chain stalls after one frame and React
        // effects never fire.
        // TODO(spectr#28): make conditional on actual pending work.
        engine_.evaluate("if (typeof layout === 'function') layout();void 0");
    }
    pulp::view::View::paint(canvas);
}

void NativeEditorView::update_params() {
    if (!bridge_) return;
    auto& s = plugin_.state();
    // Normalize each param to 0..1 for the Knob/Fader value prop. Using
    // ParamRange::normalize would be cleaner but we'd have to look up
    // each ParamInfo by id; for v0 we hand-normalize since we know the
    // ranges from spectr.cpp::define_parameters().
    const float mix      = s.get_value(kMix) / 100.0f;          // 0..100 → 0..1
    const float output   = (s.get_value(kOutputTrim) + 24.0f) / 48.0f; // -24..24 → 0..1
    const float response = s.get_value(kResponseMode) / 1.0f;   // 0..1
    const float engine   = s.get_value(kEngineMode) / 2.0f;     // 0..2 → 0..1
    const float bands    = s.get_value(kBandCount) / 4.0f;      // 0..4 → 0..1
    const float morph    = s.get_value(kMorph);                  // already 0..1

    std::string js;
    js += "setValue('mix', " + js_number(mix) + ");";
    js += "setValue('output', " + js_number(output) + ");";
    js += "setValue('response', " + js_number(response) + ");";
    js += "setValue('engine', " + js_number(engine) + ");";
    js += "setValue('bands', " + js_number(bands) + ");";
    js += "setValue('morph', " + js_number(morph) + ");";
    js += "void 0;";  // CHOC QuickJS circular-ref guard — see auto-memory feedback_choc_quickjs_circular_refs.md
    engine_.evaluate(js);
}

void NativeEditorView::tick() {
    if (!bridge_) return;
    // Pull latest spectrum frame (TripleBuffer atomic read; lock-free).
    const auto& sd = plugin_.read_spectrum();
    if (sd.num_bins <= 0) return;

    // Spectr's analyzer reports magnitudes in dB. Map to a [0,1] display
    // band: dynamic range -90 dB (silent) → 0 dB (full scale). The
    // React Spectrum widget expects 0..1 normalized magnitudes per the
    // intrinsic prop type.
    constexpr float kMinDb = -90.0f;
    constexpr float kMaxDb = 0.0f;
    constexpr int   kMaxOutBins = 256;  // keep JS payload bounded
    const int n = std::min(sd.num_bins, kMaxOutBins);
    std::array<float, kMaxOutBins> normalized{};
    for (int i = 0; i < n; ++i) {
        const float db = sd.magnitude_db[static_cast<std::size_t>(i)];
        const float t  = (db - kMinDb) / (kMaxDb - kMinDb);
        normalized[static_cast<std::size_t>(i)] = std::clamp(t, 0.0f, 1.0f);
    }
    update_spectrum(normalized.data(), static_cast<std::size_t>(n));
}

void NativeEditorView::update_spectrum(const float* magnitudes, std::size_t n) {
    if (!bridge_ || magnitudes == nullptr || n == 0) return;
    // Build a JS array literal from the magnitudes. For a 64-band frame
    // this is ~600 bytes of JS source per call — fine for the typical
    // 30–60 Hz UI update rate. If we ever push wider frames per video
    // frame, switch to a typed-array transfer or a native buffer
    // shared via setSpectrumDataNative (which the bridge would need
    // a new register_function for).
    std::string js;
    js.reserve(16 + n * 10);
    js += "setSpectrumData('spectrum', [";
    for (std::size_t i = 0; i < n; ++i) {
        if (i) js += ',';
        js += js_number(magnitudes[i]);
    }
    js += "]);void 0;";
    engine_.evaluate(js);
}

} // namespace spectr
