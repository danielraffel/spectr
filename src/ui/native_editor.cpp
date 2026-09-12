#include "spectr/spectr.hpp"

#include "spectr/editor_bridge.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/runtime/trace.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/format/plugin_descriptor.hpp>
#include <cstdio>
#include <pulp/view/script_event_dispatch.hpp>
#include <pulp/view/buttons.hpp>
#include <pulp/view/hover_cursor.hpp>
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
#include <chrono>
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

// Name a View::CursorStyle so a probe result reads as the cursor a person
// would see, not an enum ordinal that silently shifts when the enum grows.
const char* cursor_style_name(pulp::view::View::CursorStyle style) {
    using CS = pulp::view::View::CursorStyle;
    switch (style) {
        case CS::default_: return "default";
        case CS::pointer: return "pointer";
        case CS::crosshair: return "crosshair";
        case CS::text: return "text";
        case CS::grab: return "grab";
        case CS::grabbing: return "grabbing";
        case CS::not_allowed: return "not-allowed";
        case CS::invisible: return "invisible";
        case CS::horizontal_resize: return "horizontal-resize";
        case CS::vertical_resize: return "vertical-resize";
        case CS::top_left_resize: return "top-left-resize";
        case CS::top_right_resize: return "top-right-resize";
        case CS::bottom_left_resize: return "bottom-left-resize";
        case CS::bottom_right_resize: return "bottom-right-resize";
        case CS::multi_directional_resize: return "multi-directional-resize";
        case CS::alias: return "alias";
        case CS::copy: return "copy";
        case CS::zoom_in: return "zoom-in";
        case CS::zoom_out: return "zoom-out";
        case CS::context_menu: return "context-menu";
    }
    return "?";
}

// The NSCursor a given style resolves to on macOS, transcribed from
// pulp::view::mac_geometry::set_ns_cursor_for_style. The probe runs headless,
// so it cannot ask AppKit what is on screen; naming the mapped NSCursor keeps
// the last hop auditable instead of implied.
const char* ns_cursor_name(pulp::view::View::CursorStyle style) {
    using CS = pulp::view::View::CursorStyle;
    switch (style) {
        case CS::default_: return "arrowCursor";
        case CS::pointer: return "pointingHandCursor";
        case CS::crosshair: return "crosshairCursor";
        case CS::text: return "IBeamCursor";
        case CS::grab: return "openHandCursor";
        case CS::grabbing: return "closedHandCursor";
        case CS::not_allowed: return "operationNotAllowedCursor";
        case CS::invisible: return "hidden";
        case CS::horizontal_resize: return "resizeLeftRightCursor";
        case CS::vertical_resize: return "resizeUpDownCursor";
        case CS::top_left_resize:
        case CS::bottom_right_resize: return "_windowResizeNorthWestSouthEastCursor";
        case CS::top_right_resize:
        case CS::bottom_left_resize: return "_windowResizeNorthEastSouthWestCursor";
        case CS::multi_directional_resize: return "openHandCursor";
        case CS::alias: return "dragLinkCursor";
        case CS::copy: return "dragCopyCursor";
        case CS::zoom_in: return "zoomInCursor";
        case CS::zoom_out: return "zoomOutCursor";
        case CS::context_menu: return "contextualMenuCursor";
    }
    return "?";
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
#if defined(SPECTR_ENABLE_PERF_FIXTURES)
    // Test-only performance fixture: arm LFO 1 over every band so the
    // modulation overlay is the workload under measurement. The audio owner
    // advances the phase inside process(), so a capture that wants motion must
    // also keep audio live (PULP_SCREENSHOT_KEEP_AUDIO=1).
    if (const auto* fixture = std::getenv("SPECTR_LFO_PERF_FIXTURE");
        fixture && std::string_view{fixture} == "1") {
        const auto* shape = std::getenv("SPECTR_LFO_PERF_SHAPE");
        state().set_value(kParamBandCount, 64.0f);
        state().set_value(kParamLfoShape,
                          shape ? static_cast<float>(std::atof(shape)) : 0.0f);
        state().set_value(kParamLfoRate, 4.0f);
        state().set_value(kParamLfoDepth, 1.0f);
        state().set_value(kParamLfoTarget, 0.0f);
        state().set_value(kParamLfoEnabled, 1.0f);
    }
#endif
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
    // Subscribing does not, by itself, wake an idle render loop. The host reads
    // FrameClock::has_active_subscribers() only at the bottom of a frame it had
    // already decided to render, and FrameClock::subscribe() performs no
    // invalidation -- so a subscriber added while the loop is parked is never
    // observed, and the loop stays parked. Kick it once here; from the next
    // rendered frame on, the subscriber count keeps it alive on its own.
    view.request_repaint();
}

double Spectr::fixture_now_ms_() {
    using clock = std::chrono::steady_clock;
    static const auto origin = clock::now();
    return std::chrono::duration<double, std::milli>(clock::now() - origin)
        .count();
}

void Spectr::settle_native_runtime_(int frames) {
    if (!native_scripted_ui_ || !native_scripted_ui_->bridge()) return;
    try {
        native_scripted_ui_->bridge()->load_script(
            "if (typeof globalThis.__pulpRuntimeSettle__ === 'function') "
            "globalThis.__pulpRuntimeSettle__(" + std::to_string(frames) + ");",
            "spectr-fixture-settle");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[fixture] settle failed: %s\n", error.what());
    }
}

void Spectr::dump_fixture_stage_(const std::string& stage) {
    const auto* prefix = std::getenv("SPECTR_DRAG_DUMP_PREFIX");
    if (prefix == nullptr || *prefix == '\0' || native_editor_root_ == nullptr)
        return;
    pulp::view::LayoutTreeSnapshotOptions options;
    options.surface = "standalone";
    options.viewport_width = native_editor_root_->bounds().width;
    options.viewport_height = native_editor_root_->bounds().height;
    const std::string base = std::string(prefix) + "." + stage;
    std::ofstream out(base + ".layout.json");
    out << pulp::view::dump_layout_tree(*native_editor_root_, options);
    out.close();
    std::vector<int> depths;
    collect_depths(*native_editor_root_, 0, depths);
    std::ofstream depth_out(base + ".depths.json");
    depth_out << '[';
    for (std::size_t i = 0; i < depths.size(); ++i)
        depth_out << (i ? "," : "") << depths[i];
    depth_out << "]\n";
    std::fprintf(stderr, "[fixture] stage %s -> %s.layout.json (t=%.0fms)\n",
                 stage.c_str(), base.c_str(), fixture_now_ms_());
}

void Spectr::publish_modulation_frame_() {
    // Modulation overlay. The audio owner publishes the post-LFO band field
    // once per processed block; drawing it is what makes an LFO assigned to a
    // control visibly animate that control.
    //
    // Deliberately a DISTINCT message rather than a reuse of
    // processing_state_live: that one's handler writes the canonical target
    // refs as well as the paint refs, and a later commit would republish those
    // derived LFO values to native as a real host edit -- the modulator would
    // ratchet its own baseline. modulation_frame touches paint only.
    const auto& modulated = read_modulated_field();
    if (!modulated.active
        && modulated.sequence == native_modulation_sequence_) return;
    PULP_TRACE_SCOPE_NAMED("state", "spectr_modulation_frame");
    const bool fresh = modulated.sequence != native_modulation_sequence_;
    native_modulation_sequence_ = modulated.sequence;

    // Two bounds for one hazard: a publication the audio owner has stopped
    // refreshing is no longer evidence about what is being played, and
    // extrapolating one animates a modulator that may already have stopped.
    // One stale tick of tolerance covers a display refreshing faster than the
    // audio owner publishes; past that the overlay holds exactly what it last
    // drew. The wall-clock bound is the backstop for a producer whose sequence
    // keeps moving while its clock does not.
    constexpr int    kMaximumStaleTicks = 1;
    constexpr double kMaximumExtrapolationSeconds = 0.25;
    native_modulation_stale_ticks_ =
        fresh ? 0 : native_modulation_stale_ticks_ + 1;
    if (native_modulation_stale_ticks_ > kMaximumStaleTicks) return;

    // Evaluate the LFO at THIS frame's time rather than repainting the audio
    // owner's last block sample. The publication cadence is the audio block
    // rate and the consumption cadence is the display's; they are unrelated
    // clocks, so a held sample advances three, four or five producer steps per
    // painted frame and the motion visibly judders -- the jitter is in the
    // resampling, not in the oscillator, which is why no waveform escapes it.
    // Extrapolating the published phase makes the painted value a continuous
    // function of display time.
    //
    // This calls the same pure `lfo_value` / `apply_internal_modulation` the
    // audio owner calls, over the inputs the audio owner published, so no DSP
    // is duplicated and the drawn field cannot drift from the audible one.
    const bool reconstructable =
        modulated.active && modulated.published_ns != 0;
    double phase_1 = modulated.phase;
    double phase_2 = modulated.phase_2;
    if (reconstructable) {
        const auto now_ns = std::chrono::duration_cast<
            std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        const double elapsed = std::clamp(
            static_cast<double>(now_ns - modulated.published_ns) * 1e-9,
            0.0, kMaximumExtrapolationSeconds);
        const auto advance = [](double phase, double per_second,
                                double seconds) {
            const double next = phase + per_second * seconds;
            return next - std::floor(next);
        };
        phase_1 = advance(phase_1, modulated.phase_per_second, elapsed);
        phase_2 = advance(phase_2, modulated.phase_2_per_second, elapsed);
    }
    // Once the clamp above pins the phase, every further tick would rebuild
    // and dispatch byte-identical numbers. Nothing changed, so nothing is sent.
    const bool unchanged = !fresh
        && phase_1 == native_modulation_drawn_phase_
        && phase_2 == native_modulation_drawn_phase_2_;
    native_modulation_drawn_phase_ = phase_1;
    native_modulation_drawn_phase_2_ = phase_2;
    if (unchanged) return;

    const BandField* drawn = &modulated.field;
    if (reconstructable) {
        native_modulation_drawn_ = apply_internal_modulation(
            modulated.pre_field, modulated.snapshots, modulated.host_morph,
            modulated.settings,
            lfo_value(modulated.settings.shape, phase_1));
        if (modulated.settings.lfo2_enabled) {
            ModulationSettings second = modulated.settings;
            second.enabled = true;
            second.shape = modulated.settings.lfo2_shape;
            second.beats_per_cycle = modulated.settings.lfo2_beats_per_cycle;
            second.depth = modulated.settings.lfo2_depth;
            native_modulation_drawn_ = apply_internal_modulation(
                native_modulation_drawn_, modulated.snapshots,
                modulated.host_morph, second,
                lfo_value(second.shape, phase_2));
        }
        drawn = &native_modulation_drawn_;
    }

    const auto visible = visible_count(layout());
    // Smoothness instrumentation. A held tick (the painted value did not
    // advance) and an over-long jump are both visible stutter, and neither
    // appears in frame timing -- the frame was painted, on time, carrying the
    // wrong value. Compiles away outside a PULP_TRACING=ON build.
    PULP_TRACE_COUNTER("state", "spectr_mod_seq",
                       static_cast<double>(modulated.sequence));
    PULP_TRACE_COUNTER("state", "spectr_mod_active",
                       modulated.active ? 1.0 : 0.0);
    {
        double sum = 0.0;
        for (std::size_t band = 0; band < visible; ++band)
            sum += static_cast<double>(drawn->bands[band].gain_db);
        PULP_TRACE_COUNTER("state", "spectr_mod_mean_db",
                           visible > 0
                               ? sum / static_cast<double>(visible) : 0.0);
    }

    auto gains = choc::value::createEmptyArray();
    auto muted = choc::value::createEmptyArray();
    for (std::size_t band = 0; band < visible; ++band) {
        gains.addArrayElement(static_cast<double>(drawn->bands[band].gain_db));
        muted.addArrayElement(drawn->bands[band].muted);
    }
    auto payload = choc::value::createObject("SpectrModulationFrame");
    payload.addMember("active", modulated.active);
    payload.addMember("sequence",
                      static_cast<std::int64_t>(modulated.sequence));
    payload.addMember("n_visible", static_cast<std::int32_t>(visible));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", muted);
    try {
        native_scripted_ui_->bridge()->dispatch_native_message(
            "__spectrPublishNativeMessage",
            "modulation_frame",
            payload,
            "spectr-modulation-frame",
            "spectr-native-modulation-frame");
    } catch (const std::exception& error) {
        pulp::runtime::log_error(
            "[Spectr native] modulation frame rejected: {}", error.what());
    }
}

bool Spectr::tick_native_analyzer_(float dt) {
    if (!native_scripted_ui_ || !native_scripted_ui_->bridge()) return false;

    // Host-resize fixture. `on_view_resized` is the one entry point a host uses
    // to report a new editor frame, so driving it directly exercises the same
    // code a window drag reaches -- including the pinned-viewport branch that
    // forces the root back to the authored box. Applied once, before any other
    // fixture, so the layout dump, the cursor probe and the gesture probe below
    // all describe the resized editor rather than a mix of two sizes.
    //
    // What this does NOT reach: the window-space -> design-space pointer
    // transform the GPU host applies under a pin. That lives in WindowHost and
    // needs a real window resize, which a headless capture cannot perform. A
    // row resting on input mapping at scale != 1 is not closed by this.
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

    // LIVE-WINDOW capture, as distinct from the offscreen composite.
    //
    // Every capture in this harness has gone through view::render_to_png, which
    // composites the view tree directly and never touches the window server's
    // invalidation path. A control that is laid out, has ink in that composite,
    // and still fails to repaint in a real window is invisible to it -- which
    // is why hand testing found repaint defects that nine detectors passed.
    //
    // WindowHost::capture_png() reads the host surface instead, and
    // supports_compositor_capture() says whether those pixels came from the
    // VISIBLE compositor or from a deterministic back buffer. That flag is
    // reported rather than assumed: a back-buffer capture has the same blind
    // spot as render_to_png, so treating it as a live capture would reintroduce
    // exactly the error this exists to remove.
    if (!live_capture_done_) {
        if (const auto* out = std::getenv("SPECTR_LIVE_CAPTURE");
            out != nullptr && *out != '\0' && native_editor_root_ != nullptr) {
            auto* host = native_editor_root_->window_host();
            if (host == nullptr) {
                std::fprintf(stderr, "[live-capture] no WindowHost — cannot "
                                     "capture a live surface\n");
                live_capture_done_ = true;
            } else {
                const auto png = host->capture_png();
                std::fprintf(stderr,
                             "[live-capture] bytes=%zu compositor=%s\n",
                             png.size(),
                             host->supports_compositor_capture() ? "yes"
                                                                 : "NO (back buffer)");
                if (!png.empty()) {
                    std::ofstream f(out, std::ios::binary);
                    f.write(reinterpret_cast<const char*>(png.data()),
                            static_cast<std::streamsize>(png.size()));
                }
                live_capture_done_ = true;
            }
        }
    }

    // Key-driven rows -- Escape closing a modal, a chord opening a manager --
    // cannot be reached by clicking, and a capture that cannot reach a state
    // cannot review it. `SPECTR_KEY` accepts `[mod+]*key`, e.g. `escape` or
    // `cmd+shift+p`; modifiers are cmd, meta, ctrl, alt, shift.
    //
    // Delivery mirrors a macOS host, in the host's own order: the focused-view
    // path first (`View::on_key_event`), then the additive script fan-out via
    // `script_events::dispatch_key_for_root` -- the same entry point the
    // plugin editor host calls (and the sibling of the standalone host's
    // `dispatch_global_key`). That second route is what carries a key into the
    // materialized JS runtime, where it is matched against the bridge's
    // registered-shortcut table and, if unclaimed, dispatched as a DOM
    // `keydown`. Both results are reported so a row can say which answered.
    if (!settings_fixture_key_sent_ && settings_fixture_scrolled_) {
        if (const auto* key = std::getenv("SPECTR_KEY");
            key != nullptr && native_editor_root_ != nullptr) {
            std::string spec{key};
            uint16_t mods = 0;
            for (;;) {
                const auto plus = spec.find('+');
                if (plus == std::string::npos || plus + 1 >= spec.size()) break;
                const std::string mod = spec.substr(0, plus);
                if (mod == "cmd")        mods |= pulp::view::kModCmd;
                else if (mod == "meta")  mods |= pulp::view::kModMeta;
                else if (mod == "ctrl")  mods |= pulp::view::kModCtrl;
                else if (mod == "alt")   mods |= pulp::view::kModAlt;
                else if (mod == "shift") mods |= pulp::view::kModShift;
                else break;
                spec.erase(0, plus + 1);
            }
            pulp::view::KeyCode code = pulp::view::KeyCode::unknown;
            if (spec == "escape") code = pulp::view::KeyCode::escape;
            else if (spec == "enter") code = pulp::view::KeyCode::enter;
            else if (spec == "tab") code = pulp::view::KeyCode::tab;
            else if (spec == "up") code = pulp::view::KeyCode::up;
            else if (spec == "down") code = pulp::view::KeyCode::down;
            else if (spec.size() == 1) {
                // Printable single character. Letters fold to lower case: the
                // host reports the physical key, and the bridge's own matcher
                // folds 'A'-'Z' the same way.
                char c = spec[0];
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                code = static_cast<pulp::view::KeyCode>(c);
            }
            if (code == pulp::view::KeyCode::unknown) {
                std::fprintf(stderr, "[key-fixture] unknown key '%s'\n", key);
            } else {
                pulp::view::KeyEvent down; down.key = code;
                down.modifiers = mods; down.is_down = true;
                pulp::view::KeyEvent up;   up.key = code;
                up.modifiers = mods;   up.is_down = false;
                const bool view_handled = native_editor_root_->on_key_event(down);
                const bool script_handled =
                    pulp::view::script_events::dispatch_key_for_root(
                        *native_editor_root_, static_cast<int>(code), mods,
                        /*is_down=*/true);
                pulp::view::script_events::dispatch_key_for_root(
                    *native_editor_root_, static_cast<int>(code), mods,
                    /*is_down=*/false);
                native_editor_root_->on_key_event(up);
                std::fprintf(stderr,
                             "[key-fixture] %s mods=%u view_handled=%s "
                             "script_handled=%s\n",
                             key, static_cast<unsigned>(mods),
                             view_handled ? "yes" : "no",
                             script_handled ? "yes" : "no");
            }
            settings_fixture_key_sent_ = true;
        }
    }

    // ── Cursor probe ────────────────────────────────────────────────────
    //
    // CUR-1..4 are about a cursor a person SEES. The previous close rested on
    // a test named "cursors reach the shipping runtime" -- a value arriving in
    // a View slot, which is not the same claim and is why those rows were
    // reopened. This reproduces the shipping resolution instead, verbatim from
    // the macOS host:
    //
    //   mouseMoved:  rootView->simulate_hover(pt);
    //                style = hover_cursor_at(root, pt);   // hit_test->cursor()
    //                set_ns_cursor_for_style(style);
    //   mouseDown:   dragTarget = hit_test(pt); deliver_mouse_down(...);
    //                set_ns_cursor_for_style(dragTarget->cursor());
    //   mouseDragged: deliver_mouse_drag(...);
    //                set_ns_cursor_for_style(dragTarget->cursor());
    //
    // so what this records is the style the host would hand AppKit, not a slot
    // somebody wrote. The ancestor chain is recorded alongside it because CSS
    // `cursor` inherits and View::cursor() does not: a wrapper holding
    // `crosshair` while the hit child holds `default` is exactly the shape of
    // a cursor that reaches the runtime and never reaches the screen, and
    // without the chain that case is indistinguishable from nothing being set.
    // Latched, unlike the layout dump. The dump is idempotent, but a pointer
    // probe MUTATES the app it measures: a press with no release leaves the
    // surface's pointer mode set, and the next tick's hover then reads the
    // cursor of a drag that is still notionally in progress. That is exactly
    // how the first run reported `grabbing` at every point including the plot
    // -- an instrument artefact that looked like a uniform app defect.
    //
    // A COMPLETED drag mutates it too, which is the subtler trap: the JS sets
    // `cursor` on pointerup, that value persists in the View slot, and a later
    // hover over the same view reports it rather than re-resolving -- because
    // a buttonless move delivers no JS pointermove on this host. Measured at
    // (660,60) on the home screen: `crosshair` when probed cold, `grab` when
    // probed after a drag, same hit view either way. So order drag points LAST
    // in SPECTR_CURSOR_POINTS, or treat a hover that follows one as unsound.
    if (settings_fixture_scrolled_ && !cursor_probe_done_) {
        const auto* probe_out = std::getenv("SPECTR_CURSOR_PROBE");
        const auto* probe_points = std::getenv("SPECTR_CURSOR_POINTS");
        if (probe_out != nullptr && *probe_out != '\0'
            && probe_points != nullptr && *probe_points != '\0'
            && native_editor_root_ != nullptr) {
            auto& root = *native_editor_root_;
            std::ostringstream out;
            out << "{\"schema\":\"spectr-cursor-probe-v1\""
                << ",\"viewport\":{\"w\":" << root.bounds().width
                << ",\"h\":" << root.bounds().height << "}"
                << ",\"probes\":[";
            bool first = true;
            std::string_view rest{probe_points};
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
                // "x,y" is a hover; "x,y>x2,y2" presses at the first point and
                // drags to the second, which is the only way to reach a
                // grabbing cursor -- it exists only while a button is held.
                std::string a_part = coords;
                std::string b_part;
                if (const auto gt = coords.find('>'); gt != std::string::npos) {
                    a_part = coords.substr(0, gt);
                    b_part = coords.substr(gt + 1);
                }
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
                if (!parse_point(a_part, a)) continue;
                const bool is_drag = !b_part.empty() && parse_point(b_part, b);

                using CS = pulp::view::View::CursorStyle;
                CS resolved = CS::default_;
                std::string hit_id = "<none>";
                std::string phase = is_drag ? "drag" : "hover";
                std::vector<std::pair<std::string, CS>> chain;
                bool hit_missing = false;

                if (is_drag) {
                    // mouseDown then mouseDragged, exactly as the host orders
                    // them. The captured target -- not a fresh hit_test at the
                    // drag point -- is what the host reads the style from, so a
                    // probe that re-hit-tests mid-drag would measure a
                    // different mechanism than the one that runs.
                    pulp::view::ViewCapture capture;
                    capture.set(root.hit_test(a));
                    auto* target = capture.live_in(root);
                    if (target == nullptr) {
                        hit_missing = true;
                    } else {
                        pulp::view::deliver_mouse_down(root, target, a, 0, 1);
                        if (auto* live = capture.live_in(root)) {
                            pulp::view::deliver_mouse_drag(root, live, b, 0, 1);
                        }
                        if (auto* live = capture.live_in(root)) {
                            resolved = live->cursor();
                            hit_id = live->id();
                            for (auto* v = live; v != nullptr; v = v->parent())
                                chain.emplace_back(v->id(), v->cursor());
                        } else {
                            hit_missing = true;
                        }
                        // Release before reading anything else. The style is
                        // sampled above, while the button is still notionally
                        // down, because `grabbing` exists only during a drag --
                        // but leaving the press un-released poisons every later
                        // probe in the same run.
                        if (auto* live = capture.live_in(root)) {
                            pulp::view::MouseUpHost up_host;
                            pulp::view::deliver_mouse_up(root, live, b, 0, 1,
                                                         up_host);
                        }
                    }
                } else {
                    // The shipping macOS hover path, in the host's own order:
                    //
                    //   mouseMoved:  rootView->simulate_hover(pt);
                    //                style = hover_cursor_at(root, pt);
                    //                set_ns_cursor_for_style(style);
                    //
                    // `hover_cursor_at` is Pulp's own factoring of the rule
                    // (`hit_test(p) ? target->cursor() : default_`), called BY
                    // `window_host_mac.mm`'s `resolveHoverCursorAt:` and by
                    // `pulp_plugin_apply_hover_cursor`. Calling the library
                    // function rather than open-coding the hit test is what
                    // keeps this probe from drifting away from the host.
                    //
                    // The order matters and is not cosmetic. `simulate_hover`
                    // runs first because it is what flips `hovered_` and fires
                    // `on_hover_move`, which may restyle the tree; resolving
                    // before it would read the PREVIOUS frame's cursor. That
                    // is the same staleness AppKit's own `-cursorUpdate:` pass
                    // exhibits — it resolves from the hit view's slot without
                    // delivering a hover sample, so it shows the slot's prior
                    // value until the next `-mouseMoved:` lands.
                    //
                    // `hover_cursor_at` collapses "nothing under the pointer"
                    // into `default_`, which is the one thing this probe must
                    // NOT do: a dead probe point and "the app shows an arrow
                    // here" have to read differently. So the hit is taken
                    // separately, from a hit test run AFTER the hover sample,
                    // and reported as `hit:false` when it misses.
                    root.simulate_hover(a);
                    pulp::view::View* hover_target = root.hit_test(a);
                    const auto hover_style =
                        pulp::view::hover_cursor_at(root, a);
                    struct { pulp::view::View* target;
                             pulp::view::View::CursorStyle style; }
                        hover{hover_target, hover_style};
                    if (hover.target == nullptr) {
                        hit_missing = true;
                    } else {
                        resolved = hover.style;
                        hit_id = hover.target->id();
                        for (auto* v = hover.target; v != nullptr; v = v->parent())
                            chain.emplace_back(v->id(), v->cursor());
                    }
                }

                out << (first ? "" : ",") << "\n {\"name\":\"" << name << "\""
                    << ",\"phase\":\"" << phase << "\""
                    << ",\"point\":{\"x\":" << a.x << ",\"y\":" << a.y << "}";
                if (is_drag)
                    out << ",\"to\":{\"x\":" << b.x << ",\"y\":" << b.y << "}";
                // A miss is reported as a miss. Falling back to "default" here
                // would make "nothing was under the pointer" -- a broken probe
                // -- read identically to "the app shows an arrow there", which
                // is the exact confusion this whole exercise exists to reject.
                out << ",\"hit\":" << (hit_missing ? "false" : "true")
                    << ",\"hit_id\":\"" << hit_id << "\""
                    << ",\"resolved_cursor\":\""
                    << (hit_missing ? "<no-hit>" : cursor_style_name(resolved))
                    << "\",\"ns_cursor\":\""
                    << (hit_missing ? "<no-hit>" : ns_cursor_name(resolved))
                    << "\",\"ancestor_cursors\":[";
                for (std::size_t i = 0; i < chain.size(); ++i) {
                    out << (i ? "," : "") << "{\"id\":\"" << chain[i].first
                        << "\",\"cursor\":\"" << cursor_style_name(chain[i].second)
                        << "\"}";
                }
                out << "]}";
                first = false;
            }
            out << "\n]}\n";
            std::ofstream file(probe_out);
            file << out.str();
            cursor_probe_done_ = true;
        }
    }

    // ── Pointer fixtures ────────────────────────────────────────────────
    //
    // The status overlay is judged in states only a DRAG reaches, and a
    // screenshot cannot be taken in the middle of a gesture. So the press,
    // each move, and the release are delivered separately through the same
    // pointer_dispatch verbs a window host calls, and the laid-out tree is
    // written between them. "Updates while dragging" is then a comparison of
    // two trees captured before the release, not an inference from a picture
    // taken after it.
    //
    // The tree is the instrument rather than `textContent`, deliberately: the
    // editor writes the live label straight onto the DOM node, and a value that
    // reads back from the shim proves nothing about what the native Label
    // paints. Only the snapshot carries the painted string.
    if (!drag_fixture_done_ && settings_fixture_scrolled_
        && native_editor_root_ != nullptr) {
        if (const auto* spec = std::getenv("SPECTR_DRAG");
            spec != nullptr && *spec != '\0') {
            float coords[5] = {0, 0, 0, 0, 0};
            {
                std::string_view rest{spec};
                for (int i = 0; i < 5 && !rest.empty(); ++i) {
                    const auto comma = rest.find(',');
                    coords[i] = std::strtof(std::string(rest.substr(0, comma)).c_str(),
                                            nullptr);
                    if (comma == std::string_view::npos) break;
                    rest.remove_prefix(comma + 1);
                }
            }
            const int steps = std::max(1, static_cast<int>(coords[4]));
            const pulp::view::Point start{coords[0], coords[1]};
            const pulp::view::Point finish{coords[2], coords[3]};
            auto* target = native_editor_root_->hit_test(start);
            std::fprintf(stderr,
                         "[drag] start=(%.1f,%.1f) end=(%.1f,%.1f) steps=%d "
                         "target=%s\n",
                         start.x, start.y, finish.x, finish.y, steps,
                         target ? (target->id().empty() ? "<anonymous>"
                                                        : target->id().c_str())
                                : "NONE");
            if (target != nullptr) {
                dump_fixture_stage_("press-before");
                pulp::view::deliver_mouse_down(*native_editor_root_, target,
                                               start, 0, 1, true);
                settle_native_runtime_(4);
                dump_fixture_stage_("press");
                for (int i = 1; i <= steps; ++i) {
                    const float t = static_cast<float>(i) / steps;
                    const pulp::view::Point p{
                        start.x + (finish.x - start.x) * t,
                        start.y + (finish.y - start.y) * t};
                    pulp::view::deliver_mouse_drag(*native_editor_root_, target,
                                                   p, 0);
                    settle_native_runtime_(4);
                    dump_fixture_stage_("move" + std::to_string(i));
                }
                pulp::view::deliver_mouse_up(*native_editor_root_, target,
                                             finish, 0, 1,
                                             pulp::view::MouseUpHost{});
                settle_native_runtime_(4);
                dump_fixture_stage_("release");
            }
            drag_fixture_finished_ms_ = fixture_now_ms_();
            std::fprintf(stderr, "[drag] released at t=%.0fms\n",
                         drag_fixture_finished_ms_);
        }
        drag_fixture_done_ = true;
    }

    // Timed probes after the gesture. The overlay's hold and its clean
    // disappearance are both TIME properties, so they are read on the wall
    // clock from the same process, at offsets the caller names.
    if (drag_fixture_done_ && drag_fixture_finished_ms_ >= 0.0) {
        if (const auto* offsets = std::getenv("SPECTR_STATUS_PROBE_MS");
            offsets != nullptr && *offsets != '\0') {
            std::vector<double> wanted;
            std::string_view rest{offsets};
            while (!rest.empty()) {
                const auto comma = rest.find(',');
                const auto one = rest.substr(0, comma);
                if (!one.empty())
                    wanted.push_back(std::strtod(std::string(one).c_str(), nullptr));
                if (comma == std::string_view::npos) break;
                rest.remove_prefix(comma + 1);
            }
            const double elapsed = fixture_now_ms_() - drag_fixture_finished_ms_;
            while (status_probe_next_ < wanted.size()
                   && elapsed >= wanted[status_probe_next_]) {
                char tag[64];
                std::snprintf(tag, sizeof(tag), "t%.0f",
                              wanted[status_probe_next_]);
                dump_fixture_stage_(tag);
                std::fprintf(stderr, "[status-probe] %s actual=%.0fms\n", tag,
                             elapsed);
                ++status_probe_next_;
            }
        }
    }

    // Ask the HOST for a different editor size, which in the standalone
    // reaches WindowHost::request_content_size and resizes the real NSWindow.
    // This is the only headless way to get a window whose size differs from
    // the authored design box, and therefore the only way to exercise the
    // pinned-viewport window-space -> design-space pointer transform at a
    // scale other than 1. A capture at the default size cannot see a
    // transform bug at all, because there the transform is the identity.
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

    // Per-tick state trace. SPECTR_STATE_OUT records where a gesture ENDED;
    // this records the path it took. COR-1's claim -- an edge drag never moves
    // the opposite trim -- is false if the opposite trim moves at any point,
    // even briefly, so it can only be judged frame by frame.
    if (settings_fixture_scrolled_) {
        if (const auto* trace_path = std::getenv("SPECTR_STATE_TRACE");
            trace_path != nullptr && *trace_path != '\0') {
            const auto snap = processing_state_snapshot();
            const auto n = visible_count(snap.layout);
            int lo = -1, hi = -1, changed = 0;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (snap.field.bands[i].gain_db == 0.0f
                    && !snap.field.bands[i].muted) continue;
                if (lo < 0) lo = static_cast<int>(i);
                hi = static_cast<int>(i);
                ++changed;
            }
            std::ostringstream line;
            line << "{\"t\":" << native_state_trace_tick_++
                 << ",\"host_w\":" << native_host_width_
                 << ",\"host_h\":" << native_host_height_
                 << ",\"min_hz\":" << snap.viewport.min_hz
                 << ",\"max_hz\":" << snap.viewport.max_hz
                 << ",\"n_changed\":" << changed
                 << ",\"lo\":" << lo << ",\"hi\":" << hi << "}\n";
            native_state_trace_ += line.str();
            std::ofstream file(trace_path);
            file << native_state_trace_;
        }
    }

    // Final processing state, rewritten every tick for the same reason the
    // layout dump is: the file that survives is the one nearest the shutter.
    // This is what an externally driven gesture -- PULP_TEST_POINTER_DRAG,
    // which enters through AppKit and therefore through the host's pointer
    // transform -- leaves behind. The stepped probe below cannot answer that:
    // it injects in root coordinates and so is blind to the transform by
    // construction.
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
    // band behind, that a viewport pan keeps its span. None of those can be
    // read from a before/after pair, and none can be read from a picture --
    // the picture is taken after the release. So this delivers a real gesture
    // through the host's own verbs and reads the plugin's processing state
    // after EVERY delivered sample.
    //
    // deliver_mouse_down / deliver_mouse_drag / deliver_mouse_up with a
    // ViewCapture is the exact sequence PulpMetalView runs (window_host_mac.mm
    // mouseDown:/mouseDragged:/mouseUp:), so a target that claims the drag
    // keeps it here the same way it would under a real pointer. The one thing
    // this deliberately does not reproduce is the host's PointerCoalescer,
    // which merges motion between presented frames: every sample here is
    // delivered. That makes this the ACCURACY instrument and not the latency
    // one -- coalescing can only ever remove samples, and the row's accuracy
    // claim has to hold for the samples that do arrive.
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

            // One state sample. gain_db is emitted for every visible band on
            // every sample on purpose: "which bands did this move touch" is
            // the whole of the accuracy question, and a summary statistic
            // (count changed, a hash) cannot answer "which".
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
                // press delivered to nothing produces a perfectly well-formed
                // sample list in which nothing changes, which reads exactly
                // like a control that ignores the drag.
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
    publish_modulation_frame_();

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
