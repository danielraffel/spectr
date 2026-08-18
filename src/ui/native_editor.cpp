#include "spectr/spectr.hpp"

#include "spectr/editor_bridge.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/view/buttons.hpp>
#include <pulp/view/view.hpp>

#include <choc/text/choc_JSON.h>

#include "spectr_native_assets_data.hpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
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
std::atomic<bool> g_host_draws_native_resize{false};
}  // namespace

void set_host_draws_native_resize(bool value) {
    g_host_draws_native_resize.store(value, std::memory_order_relaxed);
}

bool host_draws_native_resize() {
    return g_host_draws_native_resize.load(std::memory_order_relaxed);
}

namespace {

constexpr float kPublishPeriodSeconds = 1.0f / 30.0f;

// Editor-owned resize affordance. The bottom bar keeps a fixed ~20pt gutter to
// the right of the help button at every declared size (measured: the button
// ends at x=1300 of 1320, and at x=2620 of 2640 — the gutter does NOT scale
// with the editor). Size + inset must therefore stay under that gutter so the
// grip's left edge clears the button on the x-axis; at 14 + 3 it clears by 3pt
// at the tightest size. `editor resize grip overlaps no other control` in
// test_native_state_parity.cpp checks that at all four sizes — an earlier
// 16 + 6 pass validated only at the preferred size and landed on the help
// button at the authored 1320x860 capture.
constexpr float kResizeGripSize = 14.0f;
constexpr float kResizeGripInset = 3.0f;
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

// `ResizableCorner` reports cumulative deltas from the drag start but does not
// announce the drag start itself, so the owner cannot latch the size the deltas
// are relative to. Surface that one edge.
class EditorResizeGrip : public pulp::view::ResizableCorner {
public:
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
            const float inset = 2.0f + static_cast<float>(i) * 4.5f;
            canvas.set_stroke_color(pulp::canvas::Color::rgba8(
                190, 200, 214, static_cast<uint8_t>(hovered_ ? 235 : 150)));
            canvas.stroke_line(w - inset, h - 1.0f, w - 1.0f, h - inset);
        }
    }

    void on_mouse_enter() override { hovered_ = true; }
    void on_mouse_leave() override { hovered_ = false; }

    void on_mouse_down(pulp::view::Point pos) override {
        if (on_drag_begin) on_drag_begin();
        pulp::view::ResizableCorner::on_mouse_down(pos);
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

std::unique_ptr<pulp::view::View> Spectr::create_native_editor_() {
    auto root = std::make_unique<pulp::view::View>();
    root->set_theme(pulp::view::Theme::dark());
    root->flex().direction = pulp::view::FlexDirection::column;
    root->set_requires_gpu_host(true);

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
    // Only where the host provides no resize affordance of its own. In a
    // standalone window macOS owns these exact pixels and consumes press and
    // click before the content view is asked, so a grip here is a painted
    // control that can never fire — see set_host_draws_native_resize().
    // DISABLED. The editor does not own a resize affordance.
    //
    // The grip was a workaround for AU v2 having no host->plugin resize
    // contract, and it fails in both directions: in a standalone macOS owns
    // the window corner and consumes the events before the view sees them,
    // and in Logic grabbing it disrupts the editor. Beyond that it is the
    // wrong shape of fix — resizing belongs to the window corner, not to
    // chrome inside the plug-in UI.
    //
    // Corner resize already works in VST3, CLAP and AU v3, which have real
    // host-driven resize contracts. AU v2 is the one format without one, and
    // the answer there is to ship AU v3 rather than to draw our own handle.
    // Kept behind a constant rather than deleted so the wiring (and its
    // tests) survive for the AU v3 work.
    constexpr bool kEditorOwnsResizeGrip = false;
    if (kEditorOwnsResizeGrip && !host_draws_native_resize()) {
    auto grip = std::make_unique<EditorResizeGrip>();
    grip->set_position(pulp::view::View::Position::absolute);
    grip->set_right(kResizeGripInset);
    grip->set_bottom(kResizeGripInset);
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
    grip->on_resize = [this](float dx, float dy) {
        if (native_resize_refused_ || native_resize_base_width_ == 0) return;
        const auto target = resolve_editor_resize(
            native_resize_base_width_, native_resize_base_height_, dx, dy);
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
    if (native_frame_subscription_ >= 0) return;
    native_frame_clock_ = view.frame_clock();
    if (!native_frame_clock_) return;
    native_frame_subscription_ = native_frame_clock_->subscribe(
        [this](float dt) { return tick_native_analyzer_(dt); });
}

bool Spectr::tick_native_analyzer_(float dt) {
    if (!native_scripted_ui_ || !native_scripted_ui_->bridge()) return false;
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
