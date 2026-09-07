#include "spectr/spectr.hpp"

#include "spectr/editor_bridge.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/runtime/trace.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/format/plugin_descriptor.hpp>
#include <cstdio>
#include <pulp/view/buttons.hpp>
#include <pulp/view/input_events.hpp>
#include <pulp/view/layout_snapshot.hpp>
#include <pulp/view/pointer_dispatch.hpp>
#include <pulp/view/ui_components.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/window_host.hpp>

#include <choc/text/choc_JSON.h>

#include "spectr_native_assets_data.hpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace spectr {

namespace {
std::atomic<bool> g_editor_owns_resize_grip{false};
}  // namespace

void set_editor_owns_resize_grip(bool value) {
    g_editor_owns_resize_grip.store(value, std::memory_order_relaxed);
}

bool editor_owns_resize_grip() {
    return g_editor_owns_resize_grip.load(std::memory_order_relaxed);
}

namespace {

constexpr float kPublishPeriodSeconds = 1.0f / 30.0f;

// Editor-owned resize affordance, laid into the bottom bar rather than floated
// in the corner behind it — the arrangement Arturia's Efx FRAGMENTS uses in
// Logic, where the grip lines sit at the far right of the plug-in's own bottom
// chrome strip, in line with that strip's controls.
//
// Under the pinned viewport (see make_editor_view_size) the bottom bar's
// geometry is CONSTANT, which is what makes fixed placement correct here: the
// materialized layout always runs at the authored 1320x860 box, so the
// `width < 1200` compact branch that used to move these controls is now
// unreachable and the gutter can no longer change under the grip. Authored
// geometry, read off applySpectrResponsiveLayout:
//   * bar        y = 860 - 56 = 804, height 56, full width
//   * gear / "?" 26x26 at y = 804 + 15.5, the help button ending at x = 1300
//   * gutter     x = 1300 .. 1320, i.e. 20pt
// The full 20pt gutter is the hit target. This keeps the adjacent help button
// untouched while making the corner much easier to acquire than the original
// 14pt target. The painted diagonals remain inset within this box, so the grip
// gains usability without becoming visually heavy. It is flush to the
// plug-in content's bottom-right corner, matching the conventional AU resize
// affordance. `editor resize grip overlaps no other control` pins clearance
// from the adjacent controls.
constexpr float kResizeGripSize = 20.0f;
constexpr float kResizeGripInset = 0.0f;
constexpr float kResizeGripBottomInset = 0.0f;
// The materialized DesignIR tree is a sandwich: `__pulp_materialized_surface__`
// paints at z = -20000 and `__pulp_materialized_behavior__` takes interaction at
// z = +20000. Native chrome that must own its own rect has to clear the
// behaviour layer, not merely the surface — at any z below it the grip paints
// but never receives the press, because `View::hit_test` resolves the
// full-bleed behaviour node first. The SDK does not export that value, so the
// relationship is pinned by `editor resize grip outranks the behaviour layer`
// in test_native_state_parity.cpp: if the materializer ever raises its z, that
// test fails loudly instead of the grip going quietly dead.
constexpr int kResizeGripZIndex = 30000;
constexpr std::size_t kVisibleAnalyzerPointCount = 321;
constexpr std::size_t kOverviewAnalyzerPointCount = 121;
constexpr float kAnalyzerCeilingDb = 24.0f;

// `ResizableCorner` supplies the shape (non-focusable, wants mouse input) but
// NOT the platform arithmetic. Pulp's generic `ResizableCorner` consumes
// host-normalized movement deltas when they are available, so its cumulative
// `on_resize` delta remains invariant while Logic changes the editor and window
// sizes underneath the captured pointer.
class EditorResizeGrip : public pulp::view::ResizableCorner {
public:
    EditorResizeGrip() {
        // Pulp maps this through the shared macOS cursor helper, including in
        // embedded AU editors, so the larger hit target also gets the expected
        // diagonal resize feedback.
        set_cursor(pulp::view::View::CursorStyle::bottom_right_resize);
    }

    std::function<void()> on_drag_begin;

    // ResizableCorner strokes with the theme's `control.border`, which on this
    // near-black background is effectively invisible — instrumenting the live
    // standalone showed paint firing with correct bounds while the corner read
    // as empty in a screenshot. A grip nobody can see is a grip nobody drags,
    // so draw it explicitly at a contrast that survives the dark theme.
    void paint(pulp::canvas::Canvas& canvas) override {
        const float w = bounds().width, h = bounds().height;
        canvas.set_line_width(1.5f);
        for (int i = 0; i < 3; ++i) {
            const float inset = 2.0f + static_cast<float>(i) * 6.0f;
            canvas.set_stroke_color(pulp::canvas::Color::rgba8(
                190, 200, 214, static_cast<uint8_t>(hovered_ ? 235 : 150)));
            canvas.stroke_line(w - inset, h - 1.0f, w - 1.0f, h - inset);
        }
    }

    void on_mouse_enter() override { hovered_ = true; }
    void on_mouse_leave() override { hovered_ = false; }

    void on_mouse_down(pulp::view::Point pos) override {
        pulp::view::ResizableCorner::on_mouse_down(pos);
        if (on_drag_begin) on_drag_begin();
    }

private:
    bool hovered_ = false;
};
struct EmbeddedFile {
    const char* relative_path;
    const unsigned char* data;
    std::size_t size;
};

const std::array kEmbeddedFiles{
    EmbeddedFile{"runtime.js", spectr_native::runtime_js, spectr_native::runtime_js_size},
    EmbeddedFile{"materialized-document.runtime.json", spectr_native::materialized_document_runtime_json, spectr_native::materialized_document_runtime_json_size},
    EmbeddedFile{"design.js", spectr_native::design_js, spectr_native::design_js_size},
};

std::filesystem::path package_path_for(const void* instance) {
    std::error_code ec;
    auto directory = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    std::ostringstream name;
#if defined(_WIN32)
    const auto process_id = _getpid();
#else
    const auto process_id = getpid();
#endif
    name << "spectr-native-materialized-" << process_id << '-' << instance;
    return directory / name.str();
}

// True when every embedded file is already on disk at exactly its embedded
// size. Sizes only -- this runs on the editor-open path where the host is
// blocked, so it must stay a handful of stat() calls and never read content.
// A regenerated package that happened to preserve every file's size byte for
// byte would defeat it, which is why the caller ALSO gates on a stamp that
// changes with the build.
bool embedded_package_is_current(const std::filesystem::path& path) {
    std::error_code ec;
    for (const auto& file : kEmbeddedFiles) {
        const auto size = std::filesystem::file_size(path / file.relative_path, ec);
        if (ec || size != file.size) return false;
    }
    return true;
}

// The materialized package is ~8 MB across runtime.js, the materialized
// document, design.js and the assets, and this used to rewrite all of it with
// `trunc` on EVERY editor open. In a plug-in host that write is synchronous
// inside `uiViewForAudioUnit:`, so Logic sat blocked on it before the editor
// could paint a single frame -- the visible symptom being a two-second sequence
// of Logic's grey placeholder, then the unpainted NSView's white, then the
// cleared GPU surface's black, before the UI finally appeared. Rewriting
// identical bytes on the one path where the host is stalled is pure cost.
//
// Skip when the package on disk already matches this build. The stamp carries
// the file count and total byte count, so adding, removing or resizing any file
// invalidates it; the per-file size check then catches a partial or interrupted
// previous write. Anything unexpected falls through to the full write, so the
// worst case is the old behaviour rather than a stale editor.
bool write_embedded_package(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path / "assets", ec);
    if (ec) return false;

    std::size_t total = 0;
    for (const auto& file : kEmbeddedFiles) total += file.size;
    const auto stamp = std::to_string(kEmbeddedFiles.size()) + ':'
                       + std::to_string(total);
    const auto stamp_path = path / ".package-stamp";

    {
        std::ifstream existing(stamp_path, std::ios::binary);
        std::string found;
        if (existing && std::getline(existing, found) && found == stamp
            && embedded_package_is_current(path)) {
            return true;
        }
    }

    for (const auto& file : kEmbeddedFiles) {
        std::ofstream stream(path / file.relative_path,
                             std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(reinterpret_cast<const char*>(file.data),
                     static_cast<std::streamsize>(file.size));
        if (!stream.good()) return false;
    }

    // Written last: a stamp is only meaningful once every file it describes is
    // on disk, so an interrupted write leaves no stamp and the next open
    // rewrites rather than trusting a partial package.
    std::ofstream stamp_out(stamp_path, std::ios::binary | std::ios::trunc);
    if (!stamp_out) return false;
    stamp_out << stamp << '\n';
    return stamp_out.good();
}

bool finite_spectrum(const pulp::view::SpectrumData& spectrum) noexcept {
    if (spectrum.num_bins < 2 || spectrum.fft_size < 2
        || !std::isfinite(spectrum.sample_rate) || spectrum.sample_rate <= 0.0f
        || !std::isfinite(spectrum.floor_db) || spectrum.floor_db >= 0.0f)
        return false;
    for (int bin = 0; bin < spectrum.num_bins; ++bin)
        if (!std::isfinite(spectrum.magnitude_db[static_cast<std::size_t>(bin)]))
            return false;
    return true;
}

std::vector<float> analyzer_trace(const pulp::view::SpectrumData& spectrum,
                                  float min_hz,
                                  float max_hz,
                                  std::size_t point_count) {
    std::vector<float> result(point_count, spectrum.floor_db);
    const auto bin_hz = spectrum.sample_rate / static_cast<float>(spectrum.fft_size);
    const auto last_bin = spectrum.num_bins - 1;
    const auto log_min = std::log(min_hz);
    const auto log_span = std::log(max_hz) - log_min;
    for (std::size_t point = 0; point < point_count; ++point) {
        const auto lower_hz = std::exp(log_min
            + static_cast<float>(point) / static_cast<float>(point_count) * log_span);
        const auto upper_hz = std::exp(log_min
            + static_cast<float>(point + 1) / static_cast<float>(point_count) * log_span);
        const auto first = std::clamp(static_cast<int>(std::ceil(lower_hz / bin_hz)),
                                      0, last_bin);
        const auto last = std::clamp(static_cast<int>(std::floor(upper_hz / bin_hz)),
                                     0, last_bin);
        float peak = spectrum.floor_db;
        if (first <= last) {
            for (int bin = first; bin <= last; ++bin)
                peak = std::max(peak,
                    spectrum.magnitude_db[static_cast<std::size_t>(bin)]);
        } else {
            const auto center_hz = std::sqrt(lower_hz * upper_hz);
            const auto position = std::clamp(center_hz / bin_hz,
                0.0f, static_cast<float>(last_bin));
            const auto left = static_cast<int>(std::floor(position));
            const auto right = std::min(left + 1, last_bin);
            const auto mix = position - static_cast<float>(left);
            peak = spectrum.magnitude_db[static_cast<std::size_t>(left)]
                + (spectrum.magnitude_db[static_cast<std::size_t>(right)]
                   - spectrum.magnitude_db[static_cast<std::size_t>(left)]) * mix;
        }
        result[point] = std::clamp(peak, spectrum.floor_db, kAnalyzerCeilingDb);
    }
    return result;
}

void append_trace(std::ostringstream& js,
                  std::string_view name,
                  float min_hz,
                  float max_hz,
                  std::span<const float> values) {
    js << name << ":{min_hz:" << min_hz << ",max_hz:" << max_hz
       << ",magnitude_db:[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) js << ',';
        js << values[index];
    }
    js << "]}";
}

} // namespace

namespace {
// The settings body is the only scroll container in the panel, but the editor
// tree carries others, so collect them all and let the caller skip any that
// cannot scroll.
void collect_depths(const pulp::view::View& view, int depth, std::vector<int>& out) {
    out.push_back(depth);
    for (const auto* child : view.sorted_children_by_z_index())
        collect_depths(*child, depth + 1, out);
}

void collect_settings_scroll_views(pulp::view::View& view,
                                   std::vector<pulp::view::ScrollView*>& out) {
    if (auto* scroll = dynamic_cast<pulp::view::ScrollView*>(&view))
        out.push_back(scroll);
    for (auto* child : view.sorted_children_by_z_index())
        collect_settings_scroll_views(*child, out);
}
} // namespace

std::vector<pulp::view::CommandID> Spectr::commands() const {
    return {kOpenSettingsCommand};
}

bool Spectr::perform_command(pulp::view::CommandID id) {
    if (id != kOpenSettingsCommand || !native_scripted_ui_
        || !native_scripted_ui_->bridge()) {
        return false;
    }
    try {
        native_scripted_ui_->bridge()->load_script(
            "(() => { if (!globalThis.__pulpActivateMaterializedElement__("
            "'[data-spectr-settings-open]', 'click', null)) "
            "throw new Error('settings trigger missing'); "
            "if (typeof globalThis.__pulpRuntimeSettle__ === 'function') "
            "globalThis.__pulpRuntimeSettle__(8); })();",
            "spectr-open-settings-command");
        return true;
    } catch (const std::exception& error) {
        pulp::runtime::log_error(
            "[Spectr native] open-settings command failed: {}", error.what());
        return false;
    }
}

std::unique_ptr<pulp::view::View> Spectr::create_native_editor_() {
    // Logic may retain a detached AUv2 NSView and ask the same Processor for a
    // replacement editor before that retained view is deallocated. In that
    // ordering `on_view_closed(old_root)` has not run yet. Spectr has one
    // materialized runtime and one analyzer frame-clock subscription, so the
    // replacement must explicitly supersede the detached runtime here. If we
    // leave the old subscription installed, open_native_editor_ sees a valid
    // subscription id and never subscribes the replacement root; audio keeps
    // processing while the visible analyzer remains frozen.
    //
    // A later close notification for the retained predecessor is harmless:
    // on_view_closed() identity-checks against native_editor_root_ and cannot
    // tear down the replacement.
    if (native_editor_root_ != nullptr) close_native_editor_();

    // A fresh editor has published nothing, so the first publish_native_layout_
    // must run even though the size it is handed has not changed since the last
    // editor. Reset here rather than on close: create is the one path both the
    // normal and the fail-closed construction share.
    native_published_width_ = 0;
    native_published_height_ = 0;
    // Test-only performance fixture: seed the worst supported layout before
    // the authored document mounts. The external harness still drives the
    // gesture through AppKit/WindowHost; this avoids making dropdown geometry
    // part of a Bands rendering benchmark.
    if (const auto* fixture = std::getenv("SPECTR_BANDS_PERF_FIXTURE");
        fixture && std::string_view{fixture} == "1") {
        state().set_value(kParamBandCount, 64.0f);
    }
    auto root = std::make_unique<pulp::view::View>();
    root->set_theme(pulp::view::Theme::dark());
    root->flex().direction = pulp::view::FlexDirection::column;
    root->set_requires_gpu_host(true);
    pulp::view::route_global_keys(*root, native_command_registry_);

    native_package_path_ = package_path_for(this);
    if (native_package_path_.empty()
        || !write_embedded_package(native_package_path_)) {
        pulp::runtime::log_error(
            "[Spectr native] materialized editor package could not be written; editor is fail-closed");
        native_editor_root_ = root.get();
        return root;
    }

    pulp::view::ScriptedUiOptions options;
    options.script_path = native_package_path_ / "runtime.js";
    options.enable_hot_reload = false;
    options.enable_theme_reload = false;
    options.enable_runtime_import = true;
    native_scripted_ui_ = std::make_unique<pulp::view::ScriptedUiSession>(
        *root, state(), std::move(options));

    if (!native_editor_handlers_registered_) {
        register_spectr_editor_handlers(
            native_editor_bridge_, *this, patterns(), editor_authority());
        native_editor_bridge_.add_handler(
            "spectral_resolution_request",
            [this](const choc::value::ValueView&) {
                pulp::signal::SpectralBandResolution report;
                if (!spectral_resolution(report))
                    return pulp::view::EditorBridge::err_response(
                        "spectral resolution unavailable");
                auto payload = choc::value::createObject("SpectrEditorResolution");
                payload.addMember("represented_bands",
                    static_cast<std::int32_t>(report.represented_bands));
                payload.addMember("active_bands",
                    static_cast<std::int32_t>(report.active_bands));
                payload.addMember("fully_represented", report.fully_represented());
                payload.addMember("fft_size", static_cast<std::int32_t>(report.fft_size));
                payload.addMember("sample_rate", static_cast<double>(report.sample_rate));
                payload.addMember("min_hz", static_cast<double>(viewport().min_hz));
                payload.addMember("max_hz", static_cast<double>(viewport().max_hz));
                return pulp::view::EditorBridge::ok_response(payload);
            });
        native_editor_handlers_registered_ = true;
    }
    native_editor_bridge_.attach_native_runtime(
        *native_scripted_ui_, "__spectrEditorDispatch");

    std::string error;
    if (!native_scripted_ui_->load(&error)) {
        pulp::runtime::log_error(
            "[Spectr native] materialized QuickJS load failed: {}; editor is fail-closed",
            error);
        native_editor_bridge_.detach_native_runtime(
            *native_scripted_ui_, "__spectrEditorDispatch");
        native_scripted_ui_.reset();
        std::error_code ec;
        std::filesystem::remove_all(native_package_path_, ec);
        native_package_path_.clear();
    } else if (auto* bridge = native_scripted_ui_->bridge()) {
        std::ifstream stream(native_package_path_ / "design.js", std::ios::binary);
        std::string design((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
        try {
            bridge->set_script_base_dir(native_package_path_);
            bridge->load_script(design, "spectr-materialized-design");
            bridge->load_script(
                "if (typeof globalThis.__pulpApplyMaterializedVisualAuthority__ === 'function') "
                "globalThis.__pulpApplyMaterializedVisualAuthority__(); "
                "if (typeof globalThis.__pulpBindMaterializedCanvases__ === 'function') "
                "globalThis.__pulpBindMaterializedCanvases__();",
                "spectr-materialized-bind");
            // The standalone host can screenshot, but it has no way to drive a
            // control before the capture, so the Settings modal could not be
            // photographed in the shipping app at all. Settings renders empty
            // on an SDK without the retained-scroll flex fix, and that is the
            // one surface the whole-image content floor cannot judge, so the
            // capture has to be reachable without a human at the window.
            if (const auto* open_settings = std::getenv("SPECTR_OPEN_SETTINGS");
                open_settings && std::string_view{open_settings} == "1") {
                bridge->load_script(
                    "if (!globalThis.__pulpActivateMaterializedElement__("
                    "'[data-spectr-settings-open]', 'click', null)) "
                    "throw new Error('settings trigger missing'); "
                    "globalThis.__pulpRuntimeSettle__(16);",
                    "spectr-open-settings-fixture");
            }
            // A capture can only photograph what a click has already revealed,
            // and the states worth reviewing -- an expanded LFO, a selected
            // target -- exist only after one. Selectors are comma-separated and
            // applied in order; a miss throws rather than producing a capture
            // of the state we did not reach, which would be indistinguishable
            // from a passing capture of a broken one.
            if (const auto* clicks = std::getenv("SPECTR_CLICK");
                clicks != nullptr && *clicks != '\0') {
                std::string script;
                std::string_view remaining{clicks};
                while (!remaining.empty()) {
                    const auto comma = remaining.find(',');
                    const auto selector = remaining.substr(0, comma);
                    if (!selector.empty()) {
                        script += "if (!globalThis.__pulpActivateMaterializedElement__('";
                        script += std::string(selector);
                        script += "', 'click', null)) throw new Error('no element: ";
                        script += std::string(selector);
                        script += "'); globalThis.__pulpRuntimeSettle__(8); ";
                    }
                    if (comma == std::string_view::npos) break;
                    remaining.remove_prefix(comma + 1);
                }
                if (!script.empty())
                    bridge->load_script(script, "spectr-click-fixture");
            }

            // Keys go through the runtime's own listener path, not a native
            // KeyEvent. The editor registers window.addEventListener('keydown',
            // ...), so a native event delivered to the View tree is simply not
            // where the handler is -- dispatching one and reading handled=false
            // measured the wrong thing rather than the app.
            // A raw eval hook. Distinguishing "the reconciler never handed
            // the style over" from "the style was handed over and did not take
            // effect" needs one direct write from JS, and guessing between
            // those two has already cost this session three wrong theories.
            if (const auto* script = std::getenv("SPECTR_EVAL");
                script != nullptr && *script != '\0') {
                bridge->load_script(std::string(script), "spectr-eval-fixture");
            }

            if (const auto* key = std::getenv("SPECTR_KEY_JS");
                key != nullptr && *key != '\0') {
                // POSITIVE CONTROL, not decoration. A dispatch that reaches
                // no listener looks exactly like a key the app ignores, so the
                // fixture registers its own listener first and reports whether
                // that one fired. Without this line a "the modal stayed open"
                // result cannot be told from "the event never arrived".
                                std::string js =
                    "(() => { const targets = []; "
                    "if (typeof document !== 'undefined') targets.push(document); "
                    "if (typeof window !== 'undefined' && window !== document) targets.push(window); "
                    "let controlFired = 0; "
                    "for (const t of targets) if (typeof t.addEventListener === 'function') "
                    "t.addEventListener('keydown', () => { controlFired++; }, true); "
                    "const ev = { type: 'keydown', key: '";
                js += key;
                js += "', code: '";
                js += key;
                js += "', bubbles: true, cancelable: true, "
                      "preventDefault() { this.defaultPrevented = true; }, "
                      "stopPropagation() {} }; "
                      "for (const t of targets) if (typeof t.dispatchEvent === 'function') "
                      "t.dispatchEvent(ev); "
                      "const guard = (typeof document !== 'undefined' && document.querySelector) "
                      "? document.querySelector(\"[data-pulp-popup-active='true']\") : 'no-qs'; "
                      "console.log('[key-control] targets=' + targets.length + "
                      "' listeners_fired=' + controlFired + ' popup_active_guard=' + guard); "
                      "if (typeof globalThis.__pulpRuntimeSettle__ === 'function') "
                      "globalThis.__pulpRuntimeSettle__(12); })();";
                bridge->load_script(js, "spectr-key-js-fixture");
            }
            if (const auto* fixture = std::getenv("SPECTR_BANDS_PERF_FIXTURE");
                fixture && std::string_view{fixture} == "1") {
                bridge->load_script(
                    "globalThis.__pulpActivateMaterializedElement__("
                    "'[data-spectr-menu-root=\\\"bands\\\"] [data-spectr-menu-trigger]',"
                    "'click', null);"
                    "globalThis.__pulpRuntimeSettle__(4);"
                    "globalThis.__pulpActivateMaterializedElement__("
                    "'[data-spectr-band-count=\\\"64\\\"]','click',null);"
                    "globalThis.__pulpRuntimeSettle__(8);",
                    "spectr-bands-perf-fixture-layout");
            }
        } catch (const std::exception& error) {
            pulp::runtime::log_error(
                "[Spectr native] DesignIR materialization failed: {}; editor is fail-closed",
                error.what());
            native_editor_bridge_.detach_native_runtime(
                *native_scripted_ui_, "__spectrEditorDispatch");
            native_scripted_ui_.reset();
        }
    }

    // ── Editor-owned resize grip ────────────────────────────────────────
    //
    // AU v2 has no host->plugin resize contract. `AUCocoaUIBase` declares only
    // `interfaceVersion` and `uiViewForAudioUnit:withSize:` (host->plugin at
    // creation only), and Logic's AU plugin window reports no AXGrowArea and
    // refuses a host-side resize outright. Resizable AU v2 editors therefore
    // draw their own grip and push a size at the host; JUCE's AU wrapper does
    // exactly this in `resizeHostWindow()`, and reverts host-driven parent
    // resizes in `parentSizeChanged()`. `Processor::request_editor_resize` is
    // Pulp's equivalent, and this grip is the gesture that drives it.
    //
    // The grip is a native, unregistered child of the editor root rather than a
    // scripted widget, for two reasons worth recording:
    //   * Realm teardown is ownership-classified.
    //     `WidgetBridge::clear_quarantined_realm` seeds the root's direct
    //     children with `inherited_this_realm = false` and only flips it for
    //     nodes registered in `owned_widgets_`, so an unregistered native child
    //     is never retired with the realm. This is NOT a claim that
    //     Generous-Corp/pulp#7648 is resolved — that issue still needs
    //     re-verification on its own terms; it is why this particular placement
    //     is safe.
    //   * `View::hit_test` walks children topmost-first, so a last-added,
    //     high-z grip owns its own rect without stealing hits from the
    //     scripted tree beneath it.
    //
    // Failure mode is deliberately inert: the grip only REQUESTS a size. The
    // editor's own geometry changes solely through `on_view_resized`, which
    // fires when the host actually applied the new frame. If the host refuses,
    // nothing here moves, so the internal size and the host window cannot
    // disagree.
    // Only where the format gives the user no resize affordance of its own,
    // which today means AU v2 alone — see set_editor_owns_resize_grip(). It is
    // opt-in, so every other format (and the standalone, where macOS owns these
    // exact pixels and consumes press and click before the content view is
    // asked) gets nothing here by default.
    if (editor_owns_resize_grip()) {
    auto grip = std::make_unique<EditorResizeGrip>();
    grip->set_position(pulp::view::View::Position::absolute);
    grip->set_right(kResizeGripInset);
    grip->set_bottom(kResizeGripBottomInset);
    grip->flex().preferred_width = kResizeGripSize;
    grip->flex().preferred_height = kResizeGripSize;
    grip->set_z_index(kResizeGripZIndex);
    grip->on_drag_begin = [this] {
        // Measure from the HOST size, not the root. Under a pinned viewport the
        // root is constant at the authored box, so basing the drag on root
        // bounds makes every gesture start from the same number and the grip
        // can only ever take a single step.
        native_resize_base_width_ = native_host_width_ > 0
            ? native_host_width_ : kEditorPreferredWidth;
        native_resize_base_height_ = native_host_height_ > 0
            ? native_host_height_ : kEditorPreferredHeight;
        native_resize_refused_ = false;
    };
    grip->on_resize = [this](float movement_x, float movement_y) {
        if (native_resize_refused_ || native_resize_base_width_ == 0) return;
        const auto target = resolve_editor_resize(
            native_resize_base_width_, native_resize_base_height_,
            movement_x, movement_y);
        // Skip the round trip while the drag still resolves to the size the
        // host is already at; otherwise a slow drag opens one host transaction
        // per mouse-move that changes nothing. Compared against the host size
        // for the same reason the base is: the root does not move under a pin.
        if (native_host_width_ == target.width
            && native_host_height_ == target.height) {
            return;
        }
        if (!request_editor_resize(target.width, target.height)) {
            // One log per gesture, not per mouse-move.
            native_resize_refused_ = true;
            pulp::runtime::log_info(
                "[Spectr native] host refused editor resize to {}x{}; "
                "keeping the current editor size",
                target.width, target.height);
        }
    };
    native_resize_grip_ = grip.get();
    root->add_child(std::move(grip));
    }

    native_editor_root_ = root.get();
    return root;
}

void Spectr::open_native_editor_(pulp::view::View& view) {
    if (&view != native_editor_root_ || !native_scripted_ui_) return;
    const auto bounds = view.bounds();
    const auto width = bounds.width > 0.0f
        ? static_cast<uint32_t>(std::lround(bounds.width))
        : kEditorPreferredWidth;
    const auto height = bounds.height > 0.0f
        ? static_cast<uint32_t>(std::lround(bounds.height))
        : kEditorPreferredHeight;
    on_view_resized(view, width, height);
    native_host_automation_revision_ = host_automation_revision();
    if (native_frame_subscription_ >= 0) return;
    native_frame_clock_ = view.frame_clock();
    if (!native_frame_clock_) return;
    native_frame_subscription_ = native_frame_clock_->subscribe(
        [this](float dt) { return tick_native_analyzer_(dt); });
}

bool Spectr::tick_native_analyzer_(float dt) {
    if (!native_scripted_ui_ || !native_scripted_ui_->bridge()) return false;

    // Host-resize fixture. `on_view_resized` is the one entry point a host
    // uses to report a new editor frame, so calling it directly runs the same
    // code a window drag reaches -- including the pinned-viewport branch that
    // forces the root back to the authored box. Applied before any other
    // fixture so the layout dump and the gesture probe below both describe one
    // size rather than a mix of two.
    //
    // This does NOT reach the window-space -> design-space pointer transform
    // the GPU host applies under a pin; that needs the window itself to move,
    // which is what SPECTR_REQUEST_RESIZE below is for.
    if (!resize_fixture_applied_ && native_editor_root_ != nullptr) {
        if (const auto* spec = std::getenv("SPECTR_RESIZE");
            spec != nullptr && *spec != '\0') {
            const std::string text{spec};
            const auto x = text.find('x');
            if (x != std::string::npos) {
                const auto w = static_cast<std::uint32_t>(
                    std::strtoul(text.substr(0, x).c_str(), nullptr, 10));
                const auto h = static_cast<std::uint32_t>(
                    std::strtoul(text.substr(x + 1).c_str(), nullptr, 10));
                if (w > 0 && h > 0) {
                    on_view_resized(*native_editor_root_, w, h);
                    std::fprintf(stderr,
                                 "[resize-fixture] host=%ux%u root=%gx%g\n",
                                 w, h, native_editor_root_->bounds().width,
                                 native_editor_root_->bounds().height);
                }
            }
        }
        resize_fixture_applied_ = true;
    }

    // The About block and its Status Info description sit below the fold, and
    // a capture of the unscrolled panel cannot show whether they truncate or
    // whether the header stays put while they move. Scrolling at mount is too
    // early -- the settings body measures 466x0 with no content until Yoga has
    // run -- so wait for a laid-out scroll container and act once.
    {
        if (const auto* scroll_to = std::getenv("SPECTR_SETTINGS_SCROLL");
            scroll_to != nullptr) {
            std::vector<pulp::view::ScrollView*> found;
            if (native_editor_root_ != nullptr)
                collect_settings_scroll_views(*native_editor_root_, found);
            for (auto* scroll : found) {
                const float max_y = scroll->content_size().height
                                  - scroll->bounds().height;
                if (max_y <= 0.0f) continue;
                scroll->set_scroll(scroll->scroll_x(),
                                   max_y * std::strtof(scroll_to, nullptr));
                // Deliberately NOT latched. A click that expands a disclosure
                // grows the content after the first scroll, so a one-shot
                // scroll lands short and the rows it was meant to reveal stay
                // below the fold — reporting them as laid out while no viewer
                // could see them. Re-applying the same fraction each frame is
                // idempotent and self-corrects as the content grows.
                settings_fixture_scrolled_ = true;
            }
        } else {
            settings_fixture_scrolled_ = true;
        }
    }

    // Prove the LFO controls reach DSP state, not just paint. A waveform chip
    // that highlights while ModulationSettings still says Sine looks identical
    // in every screenshot, so the picture cannot answer this and the parameter
    // has to be read back.
    if (const auto* mod_dump = std::getenv("SPECTR_MODULATION_DUMP");
        mod_dump != nullptr && settings_fixture_scrolled_) {
        const auto m = modulation_settings();
        const auto shape_name = [](spectr::LfoShape shape) {
            switch (shape) {
                case spectr::LfoShape::Sine: return "Sine";
                case spectr::LfoShape::Triangle: return "Triangle";
                case spectr::LfoShape::Square: return "Square";
                case spectr::LfoShape::Saw: return "Saw";
            }
            return "?";
        };
        std::ofstream out(mod_dump);
        out << "{\"enabled\":" << (m.enabled ? "true" : "false")
            << ",\"shape\":\"" << shape_name(m.shape) << "\""
            << ",\"beats_per_cycle\":" << m.beats_per_cycle
            << ",\"depth\":" << m.depth
            << ",\"lfo2_enabled\":" << (m.lfo2_enabled ? "true" : "false")
            << ",\"lfo2_shape\":\"" << shape_name(m.lfo2_shape) << "\""
            << ",\"lfo2_beats_per_cycle\":" << m.lfo2_beats_per_cycle
            << ",\"lfo2_depth\":" << m.lfo2_depth
            << ",\"target_mask\":" << static_cast<int>(
                   spectr::resolve_modulation_target_mask(m))
            << "}\n";
    }

    // Key-driven rows -- Escape closing a modal, arrows moving a highlight --
    // cannot be reached by clicking, and a capture that cannot reach a state
    // cannot review it. Delivered as a real KeyEvent through the same
    // on_key_event path a window host uses, not as a synthetic JS shortcut,
    // because the row is about what the app does with a key press.
    if (!settings_fixture_key_sent_ && settings_fixture_scrolled_) {
        if (const auto* key = std::getenv("SPECTR_KEY");
            key != nullptr && native_editor_root_ != nullptr) {
            const std::string_view name{key};
            pulp::view::KeyCode code = pulp::view::KeyCode::unknown;
            if (name == "escape") code = pulp::view::KeyCode::escape;
            else if (name == "enter") code = pulp::view::KeyCode::enter;
            else if (name == "tab") code = pulp::view::KeyCode::tab;
            else if (name == "up") code = pulp::view::KeyCode::up;
            else if (name == "down") code = pulp::view::KeyCode::down;
            if (code == pulp::view::KeyCode::unknown) {
                std::fprintf(stderr, "[key-fixture] unknown key '%s'\n", key);
            } else {
                pulp::view::KeyEvent down; down.key = code; down.is_down = true;
                pulp::view::KeyEvent up;   up.key = code;   up.is_down = false;
                const bool handled = native_editor_root_->on_key_event(down);
                native_editor_root_->on_key_event(up);
                // handled=no is NOT evidence that the app ignores the key.
                // The editor is a materialized JS tree and nothing has been
                // shown to forward a native KeyEvent into it, so a false here
                // is equally consistent with the event never arriving. Any row
                // resting on this must treat it as inconclusive until a host
                // key path into the runtime is demonstrated.
                std::fprintf(stderr,
                             "[key-fixture] %s handled=%s (a 'no' does not "
                             "prove the app ignores it -- no native->runtime "
                             "key route has been demonstrated)\n",
                             key, handled ? "yes" : "no");
            }
            settings_fixture_key_sent_ = true;
        }
    }

    // Ask the HOST for a different editor size. In the standalone this
    // reaches WindowHost::request_content_size and resizes the real NSWindow,
    // which is the only headless way to get a window whose size differs from
    // the authored design box -- and therefore the only way to exercise the
    // pinned-viewport window-space -> design-space pointer transform at a
    // scale other than the one the app happens to launch at. Refusals are
    // reported: a request the host declines must not read as a resize that
    // happened and changed nothing.
    if (!resize_request_sent_ && settings_fixture_scrolled_) {
        if (const auto* spec = std::getenv("SPECTR_REQUEST_RESIZE");
            spec != nullptr && *spec != '\0') {
            const std::string text{spec};
            const auto x = text.find('x');
            if (x != std::string::npos) {
                const auto w = static_cast<std::uint32_t>(
                    std::strtoul(text.substr(0, x).c_str(), nullptr, 10));
                const auto h = static_cast<std::uint32_t>(
                    std::strtoul(text.substr(x + 1).c_str(), nullptr, 10));
                if (w > 0 && h > 0) {
                    const bool accepted = request_editor_resize(w, h);
                    std::fprintf(stderr,
                                 "[request-resize] asked=%ux%u accepted=%s\n",
                                 w, h, accepted ? "yes" : "no");
                }
            }
        }
        resize_request_sent_ = true;
    }

    // Processing state, rewritten every tick for the same reason the layout
    // dump is: the file that survives is the one nearest the shutter. This is
    // what an EXTERNALLY driven gesture leaves behind -- PULP_TEST_POINTER_DRAG
    // enters through AppKit and therefore through the host's pointer
    // transform. The stepped probe below cannot answer that: it injects in
    // root coordinates and so is blind to the transform by construction.
    if (settings_fixture_scrolled_) {
        if (const auto* state_out = std::getenv("SPECTR_STATE_OUT");
            state_out != nullptr && *state_out != '\0') {
            const auto snap = processing_state_snapshot();
            const auto n = visible_count(snap.layout);
            std::ostringstream out;
            out << "{\"schema\":\"spectr-state-v1\""
                << ",\"host\":{\"w\":" << native_host_width_
                << ",\"h\":" << native_host_height_ << "}"
                << ",\"root\":{\"w\":"
                << (native_editor_root_ ? native_editor_root_->bounds().width : 0.0f)
                << ",\"h\":"
                << (native_editor_root_ ? native_editor_root_->bounds().height : 0.0f)
                << "},\"min_hz\":" << snap.viewport.min_hz
                << ",\"max_hz\":" << snap.viewport.max_hz
                << ",\"n_visible\":" << n
                << ",\"gain_db\":[";
            for (std::uint32_t i = 0; i < n; ++i)
                out << (i ? "," : "") << snap.field.bands[i].gain_db;
            out << "],\"muted\":[";
            for (std::uint32_t i = 0; i < n; ++i)
                out << (i ? "," : "")
                    << (snap.field.bands[i].muted ? "true" : "false");
            out << "]}\n";
            std::ofstream file(state_out);
            file << out.str();
        }
    }

    // ── Stepped-gesture probe ───────────────────────────────────────────
    //
    // COR-1..3 are claims about what happens DURING a drag: that a minimap
    // edge never moves the opposite trim, that a fast band sweep leaves no
    // band behind, that a viewport pan keeps its span. None can be read from a
    // before/after pair, and none can be read from a picture -- the picture is
    // taken after the release. So this delivers a real gesture through the
    // host's own verbs and reads the plugin's processing state after EVERY
    // delivered sample.
    //
    // deliver_mouse_down / deliver_mouse_drag / deliver_mouse_up with a
    // ViewCapture is the sequence PulpMetalView runs (window_host_mac.mm
    // mouseDown:/mouseDragged:/mouseUp:), so a target that claims the drag
    // keeps it here the way it would under a real pointer. The one thing this
    // deliberately does not reproduce is the host's PointerCoalescer, which
    // merges motion between presented frames: every sample here is delivered.
    // That makes this the ACCURACY instrument and not the latency one --
    // coalescing can only ever remove samples, and the accuracy claim has to
    // hold for the samples that do arrive.
    if (settings_fixture_scrolled_ && !gesture_probe_done_) {
        const auto* gesture_out = std::getenv("SPECTR_GESTURE_OUT");
        const auto* gesture_spec = std::getenv("SPECTR_GESTURES");
        if (gesture_out != nullptr && *gesture_out != '\0'
            && gesture_spec != nullptr && *gesture_spec != '\0'
            && native_editor_root_ != nullptr) {
            auto& root = *native_editor_root_;
            std::ostringstream out;
            out << "{\"schema\":\"spectr-gesture-probe-v1\""
                << ",\"viewport\":{\"w\":" << root.bounds().width
                << ",\"h\":" << root.bounds().height << "}"
                << ",\"host\":{\"w\":" << native_host_width_
                << ",\"h\":" << native_host_height_ << "}"
                << ",\"gestures\":[";

            // gain_db is emitted for every visible band on every sample on
            // purpose: "which bands did this move touch" is the whole of the
            // accuracy question, and a summary statistic cannot answer "which".
            const auto emit_sample = [this, &out](const char* phase,
                                                  pulp::view::Point pt) {
                const auto snap = processing_state_snapshot();
                const auto n = visible_count(snap.layout);
                out << "\n   {\"phase\":\"" << phase << "\""
                    << ",\"x\":" << pt.x << ",\"y\":" << pt.y
                    << ",\"min_hz\":" << snap.viewport.min_hz
                    << ",\"max_hz\":" << snap.viewport.max_hz
                    << ",\"n_visible\":" << n
                    << ",\"gain_db\":[";
                for (std::uint32_t i = 0; i < n; ++i)
                    out << (i ? "," : "") << snap.field.bands[i].gain_db;
                out << "],\"muted\":[";
                for (std::uint32_t i = 0; i < n; ++i)
                    out << (i ? "," : "")
                        << (snap.field.bands[i].muted ? "true" : "false");
                out << "]}";
            };

            bool first_gesture = true;
            std::string_view rest{gesture_spec};
            while (!rest.empty()) {
                const auto semi = rest.find(';');
                const auto spec = rest.substr(0, semi);
                if (semi == std::string_view::npos) rest = {};
                else rest.remove_prefix(semi + 1);
                if (spec.empty()) continue;
                const auto eq = spec.find('=');
                if (eq == std::string_view::npos) continue;
                const std::string name{spec.substr(0, eq)};
                std::string coords{spec.substr(eq + 1)};
                int steps = 24;
                if (const auto at = coords.find('@'); at != std::string::npos) {
                    steps = std::atoi(coords.substr(at + 1).c_str());
                    coords = coords.substr(0, at);
                }
                if (steps < 1) steps = 1;
                const auto gt = coords.find('>');
                if (gt == std::string::npos) continue;
                const auto parse_point = [](const std::string& text,
                                            pulp::view::Point& value) {
                    const auto comma = text.find(',');
                    if (comma == std::string::npos) return false;
                    value.x = std::strtof(text.substr(0, comma).c_str(), nullptr);
                    value.y = std::strtof(text.substr(comma + 1).c_str(), nullptr);
                    return true;
                };
                pulp::view::Point a{};
                pulp::view::Point b{};
                if (!parse_point(coords.substr(0, gt), a)) continue;
                if (!parse_point(coords.substr(gt + 1), b)) continue;

                out << (first_gesture ? "" : ",")
                    << "\n  {\"name\":\"" << name << "\""
                    << ",\"from\":{\"x\":" << a.x << ",\"y\":" << a.y << "}"
                    << ",\"to\":{\"x\":" << b.x << ",\"y\":" << b.y << "}"
                    << ",\"steps\":" << steps;
                first_gesture = false;

                pulp::view::ViewCapture capture;
                capture.set(root.hit_test(a));
                auto* target = capture.live_in(root);
                // A miss is reported as a miss and the gesture is skipped. A
                // press delivered to nothing produces a well-formed sample
                // list in which nothing changes, which reads exactly like a
                // control that ignores the drag.
                out << ",\"hit\":" << (target == nullptr ? "false" : "true")
                    << ",\"hit_id\":\""
                    << (target == nullptr ? std::string{"<none>"} : target->id())
                    << "\",\"samples\":[";
                if (target == nullptr) {
                    out << "]}";
                    continue;
                }
                emit_sample("pre", a);
                pulp::view::deliver_mouse_down(root, target, a, 0, 1);
                out << ",";
                emit_sample("down", a);
                for (int i = 1; i <= steps; ++i) {
                    const float t = static_cast<float>(i)
                                  / static_cast<float>(steps);
                    const pulp::view::Point pt{a.x + (b.x - a.x) * t,
                                               a.y + (b.y - a.y) * t};
                    auto* live = capture.live_in(root);
                    if (live == nullptr) break;
                    pulp::view::deliver_mouse_drag(root, live, pt, 0, 1);
                    out << ",";
                    emit_sample("move", pt);
                }
                if (auto* live = capture.live_in(root)) {
                    pulp::view::MouseUpHost up_host;
                    pulp::view::deliver_mouse_up(root, live, b, 0, 1, up_host);
                }
                out << ",";
                emit_sample("up", b);
                out << "]}";
            }
            out << "\n ]}\n";
            std::ofstream file(gesture_out);
            file << out.str();
            gesture_probe_done_ = true;
        }
    }

    // Dump the laid-out tree from the SAME process that produces the
    // screenshot, and rewrite it every tick so the final file is the one
    // nearest the shutter. Latching it wrote the tree at the FIRST settled
    // frame while the PNG lands ~100 frames later, which reopens the very
    // seam this was meant to close: the picture and the measurement then
    // describe different instants.
    if (settings_fixture_scrolled_) {
        if (const auto* dump = std::getenv("SPECTR_LAYOUT_DUMP");
            dump != nullptr && native_editor_root_ != nullptr) {
            pulp::view::LayoutTreeSnapshotOptions options;
            options.surface = "standalone";
            options.viewport_width = native_editor_root_->bounds().width;
            options.viewport_height = native_editor_root_->bounds().height;
            std::ofstream out(dump);
            out << pulp::view::dump_layout_tree(*native_editor_root_, options);
            out.close();
            // dump_layout_tree emits pre-order nodes with no depth, and a
            // consumer that infers ancestry from rect containment gets it
            // WRONG for any node that escapes its parent's bounds -- which is
            // every scrolled row. Without this sidecar a chip scrolled far
            // below a clipping modal resolves to no clipping ancestor at all
            // and reads as on-screen.
            std::vector<int> depths;
            collect_depths(*native_editor_root_, 0, depths);
            std::string depth_path{dump};
            const auto suffix = depth_path.rfind(".layout.json");
            depth_path = suffix == std::string::npos
                ? depth_path + ".depths.json"
                : depth_path.substr(0, suffix) + ".depths.json";
            std::ofstream depth_out(depth_path);
            depth_out << '[';
            for (std::size_t i = 0; i < depths.size(); ++i)
                depth_out << (i ? "," : "") << depths[i];
            depth_out << "]\n";
        }
    }


#if defined(SPECTR_ENABLE_PERF_FIXTURES)
    // Test-only: forces an extra host-automation projection every tick to
    // stress the live-state dispatch path under load. Gated by
    // SPECTR_ENABLE_PERF_FIXTURES (CMakeLists.txt, default OFF) so this
    // getenv check -- and the SPECTR_AUTOMATION_PERF_FIXTURE literal itself
    // -- do not exist in a shipping binary.
    const bool automation_perf_fixture = [] {
        const auto* value = std::getenv("SPECTR_AUTOMATION_PERF_FIXTURE");
        return value != nullptr && std::string_view{value} == "1";
    }();
#else
    constexpr bool automation_perf_fixture = false;
#endif
    const auto host_revision = host_automation_revision();
    const auto projection_revision = automation_perf_fixture
        ? native_host_automation_revision_ + 1
        : host_revision;
    if (automation_perf_fixture
        || host_revision != native_host_automation_revision_) {
        PULP_TRACE_SCOPE_NAMED("state", "spectr_host_automation_project");
        const auto payload = [&] {
            PULP_TRACE_SCOPE_NAMED(
                "state", "spectr_host_automation_snapshot");
            return make_editor_live_state_payload(*this, projection_revision);
        }();
        try {
            {
                PULP_TRACE_SCOPE_NAMED(
                    "state", "spectr_host_automation_dispatch");
                native_scripted_ui_->bridge()->dispatch_native_message(
                    "__spectrPublishNativeMessage",
                    "processing_state_live",
                    payload,
                    "spectr-processing-state-live",
                    "spectr-native-host-automation-live");
            }
            native_host_automation_revision_ = projection_revision;
        } catch (const std::exception& error) {
            pulp::runtime::log_error(
                "[Spectr native] host automation hydration rejected: {}",
                error.what());
        }
    }
    native_analyzer_elapsed_ += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    if (native_analyzer_elapsed_ < kPublishPeriodSeconds) return true;
    native_analyzer_elapsed_ = std::fmod(native_analyzer_elapsed_, kPublishPeriodSeconds);

    bridge_.poll();
    const auto& spectrum = read_spectrum();
    if (!finite_spectrum(spectrum)
        || spectrum.sequence_number == native_analyzer_sequence_)
        return true;
    native_analyzer_sequence_ = spectrum.sequence_number;

    const auto nyquist = spectrum.sample_rate * 0.5f;
    const auto visible_min = std::clamp(viewport().min_hz, 1.0f, nyquist);
    const auto visible_max = std::min(viewport().max_hz, nyquist);
    const auto overview_min = 20.0f;
    const auto overview_max = std::min(20000.0f, nyquist);
    if (!(visible_max > visible_min) || !(overview_max > overview_min)) return true;
    const auto visible = analyzer_trace(spectrum, visible_min, visible_max,
                                        kVisibleAnalyzerPointCount);
    const auto overview = analyzer_trace(spectrum, overview_min, overview_max,
                                         kOverviewAnalyzerPointCount);
    std::ostringstream js;
    js << "if (typeof globalThis.__spectrPublishNativeMessage === 'function') "
          "globalThis.__spectrPublishNativeMessage('analyzer_frame',{"
          "schema_version:1,epoch:" << spectrum.epoch
       << ",sequence_number:" << spectrum.sequence_number
       << ",dropped_frames:" << spectrum.dropped_frames
       << ",source_channels:" << spectrum.source_channels
       << ",fft_size:" << spectrum.fft_size
       << ",sample_rate:" << spectrum.sample_rate
       << ",floor_db:" << spectrum.floor_db
       << ",ceiling_db:" << kAnalyzerCeilingDb << ',';
    append_trace(js, "visible", visible_min, visible_max, visible);
    js << ',';
    append_trace(js, "overview", overview_min, overview_max, overview);
    js << "},'spectr-analyzer-frame');";
    try {
        native_scripted_ui_->bridge()->load_script(js.str(), "spectr-native-analyzer");
    } catch (const std::exception& error) {
        pulp::runtime::log_error(
            "[Spectr native N1] analyzer publication rejected: {}", error.what());
    }
    return true;
}

void Spectr::close_native_editor_() {
    if (native_frame_subscription_ >= 0 && native_frame_clock_)
        native_frame_clock_->unsubscribe(native_frame_subscription_);
    native_frame_subscription_ = -1;
    native_frame_clock_ = nullptr;
    native_analyzer_elapsed_ = 0.0f;
    native_analyzer_sequence_ = 0;
    native_host_automation_revision_ = host_automation_revision();
    editor_authority().reset_transient_state();
    native_editor_root_ = nullptr;
    if (native_scripted_ui_) {
        native_editor_bridge_.detach_native_runtime(
            *native_scripted_ui_, "__spectrEditorDispatch");
    }
    native_scripted_ui_.reset();
    if (!native_package_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(native_package_path_, ec);
        native_package_path_.clear();
    }
}

} // namespace spectr
