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
#include <pulp/view/overlay_dismissal.hpp>
#include <pulp/view/pointer_dispatch.hpp>
#include <pulp/view/ui_components.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/window_host.hpp>

#include <choc/text/choc_JSON.h>

#include "spectr_native_assets_data.hpp"

// ── Band context menu, addressed from the SHIPPING standalone ───────────
//
// A row is found by the text painted on it and pressed at the pixels that
// text occupies, because that is the only thing a human can do. Every other
// way this menu has ever been driven -- SPECTR_CLICK, and every ctest over
// it -- resolves a row by CSS selector and never consults `hit_test`, so it
// drives rows a pointer cannot reach and reports them working.
namespace spectr_menu_probe {

pulp::view::View* nearest_clickable(pulp::view::View* view) {
    for (auto* node = view; node != nullptr; node = node->parent())
        if (node->on_click) return node;
    return nullptr;
}

void root_origin_of(const pulp::view::View& view, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    for (const auto* node = &view; node != nullptr; node = node->parent()) {
        x += node->bounds().x;
        y += node->bounds().y;
    }
}

const pulp::view::Label* find_label_if(
        const pulp::view::View& view,
        bool (*match)(const std::string&, const std::string&),
        const std::string& needle, bool include_hidden = false) {
    if (!include_hidden && !view.visible()) return nullptr;
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
        label != nullptr && match(label->text(), needle))
        return label;
    for (std::size_t i = 0; i < view.child_count(); ++i)
        if (const auto* hit = find_label_if(*view.child_at(i), match, needle, include_hidden))
            return hit;
    return nullptr;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size()
        && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The group header the menu always draws, "BAND <n>", used to scope every
// row lookup to the menu rather than to an editor that also paints the words
// "Solo" and "Level" elsewhere.
bool is_band_header(const std::string& text, const std::string&) {
    if (text.rfind("BAND ", 0) != 0 || text.size() < 6) return false;
    for (std::size_t i = 5; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

// Parent hops from the active overlay to the node whose subtree carries the
// band menu's header, or -1 when the menu is not up. Reported per step so a
// scoping miss is visible instead of reading as a closed menu.
inline int menu_scope_depth = -1;

pulp::view::View* menu_container(pulp::view::View& root, int& band_number) {
    band_number = -1;
    menu_scope_depth = -1;
    auto* overlay = root.interaction().active_overlay;
    if (overlay == nullptr) return nullptr;
    // A submenu panel (`Macros`, `Modulation`) takes the active-overlay slot
    // while the band menu stays mounted underneath it -- Escape has to be
    // pressed twice to get back to the editor, which is how you can tell the
    // menu is still there. So the header that identifies the menu is NOT in
    // the top overlay; it is in an ancestor. Reading only the top overlay
    // made every row behind a submenu report `menu-absent`, which is
    // indistinguishable from the menu having closed, and it blinded this
    // probe to both submenus.
    //
    // The scope returned stays the TOP overlay: that is where a press lands
    // and where the submenu's own rows are. Only the identification walks up.
    // Requiring an active overlay first is what keeps the walk honest -- a
    // closed menu returns before it, so a stray hidden "BAND n" label
    // elsewhere in the editor can never make a closed menu read as open.
    int depth = 0;
    for (auto* node = overlay; node != nullptr; node = node->parent(), ++depth) {
        const auto* header =
            find_label_if(*node, is_band_header, std::string{}, true);
        if (header == nullptr) continue;
        band_number = std::atoi(header->text().c_str() + 5);
        menu_scope_depth = depth;
        return overlay;
    }
    return nullptr;
}

struct RowAim {
    bool found = false;
    pulp::view::View* row = nullptr;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float cx = 0.0f, cy = 0.0f;
};

RowAim aim_row(pulp::view::View& scope, const std::string& suffix) {
    RowAim aim;
    const auto* label = find_label_if(scope, ends_with, suffix);
    if (label == nullptr) return aim;
    auto* live = const_cast<pulp::view::Label*>(label);
    aim.found = true;
    aim.row = nearest_clickable(live);
    root_origin_of(*live, aim.x, aim.y);
    aim.w = live->bounds().width;
    aim.h = live->bounds().height;
    aim.cx = aim.x + aim.w * 0.5f;
    aim.cy = aim.y + aim.h * 0.5f;
    return aim;
}

std::string json_escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if (static_cast<unsigned char>(c) < 0x20) out += " ";
        else out.push_back(c);
    }
    return out;
}

}  // namespace spectr_menu_probe


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
    EmbeddedFile{"help-content.js", spectr_native::help_content_js, spectr_native::help_content_js_size},
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
// Negative-control plant for the group-drag mute contract.
//
// A test that asserts "a group drag leaves mute alone" is worth nothing until
// it has been shown FAILING against the defect, and the only honest way to
// show that is to put the defect back in the shipping asset rather than to
// simulate its symptom. This reverts exactly the load-bearing line of the fix
// -- the group branch's commit -- in the materialized document as it is
// written to the package the editor loads, which restores the real pre-fix
// path: an offset committed through the DRAW commit, which reads a finite
// gain over a muted band as "unmute it".
//
// It aborts rather than continuing when the anchor is not found exactly once.
// A plant that silently failed to apply would leave the control measuring a
// healthy app and reporting a clean pass, which is the one outcome a negative
// control must never produce.
bool plant_once(std::string& document, const char* from, const char* to,
                const char* what) {
    const std::string needle{from};
    const std::size_t at = document.find(needle);
    if (at == std::string::npos) {
        std::fprintf(stderr, "[plant] FATAL: anchor %s absent; the control "
                             "would measure a healthy app\n", from);
        return false;
    }
    if (document.find(needle, at + needle.size()) != std::string::npos) {
        std::fprintf(stderr, "[plant] FATAL: anchor %s occurs more than once\n",
                     from);
        return false;
    }
    document.replace(at, needle.size(), to);
    std::fprintf(stderr, "[plant] %s\n", what);
    return true;
}

// The two ways a group drag can be wrong, each plantable on its own.
//
// UNMUTE is the defect the user reported: the offset goes through the DRAW
// commit, which reads a finite gain over a muted band as "unmute it".
//
// FREEZE is the other wrong answer, and the reason the mute assertion alone is
// not enough. It holds the mute and discards the offset, so the muted bands
// stay silent and stay put while the rest of the selection moves -- which is
// what the draw path does with `unmuteOnDraw` off, and which is invisible in
// any screenshot because a muted band draws no bar to be in the wrong place.
bool plant_group_drag_unmute(std::string& document) {
    return plant_once(document, "commitGroupOffset(map);",
                      "commitDrawnGains(map);",
                      "group drag routed back through the draw commit "
                      "(pre-fix behaviour)");
}

bool plant_group_drag_freeze(std::string& document) {
    return plant_once(document,
                      "mutedGainDbRef.current[index] = clamp(value, -1, 1) * 24;",
                      "mutedGainDbRef.current[index] = "
                      "mutedGainDbRef.current[index];",
                      "group drag holds mute but discards the offset "
                      "(frozen-member behaviour)");
}

bool write_embedded_package(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path / "assets", ec);
    if (ec) return false;

    const bool plant_unmute =
        std::getenv("SPECTR_GROUP_DRAG_UNMUTE_PLANT") != nullptr;
    const bool plant_freeze =
        std::getenv("SPECTR_GROUP_DRAG_FREEZE_PLANT") != nullptr;
    const bool plant = plant_unmute || plant_freeze;

    std::size_t total = 0;
    for (const auto& file : kEmbeddedFiles) total += file.size;
    const auto stamp = std::to_string(kEmbeddedFiles.size()) + ':'
                       + std::to_string(total);
    const auto stamp_path = path / ".package-stamp";

    // A planted package must never be served from the stamp fast path: its
    // bytes differ from the build's by construction, so the stamp describes a
    // package that is not the one on disk.
    if (!plant) {
        std::ifstream existing(stamp_path, std::ios::binary);
        std::string found;
        if (existing && std::getline(existing, found) && found == stamp
            && embedded_package_is_current(path)) {
            return true;
        }
    }

    for (const auto& file : kEmbeddedFiles) {
        std::string body(reinterpret_cast<const char*>(file.data), file.size);
        if (plant
            && std::string_view{file.relative_path}
                   == "materialized-document.runtime.json") {
            if (plant_unmute && !plant_group_drag_unmute(body)) return false;
            if (plant_freeze && !plant_group_drag_freeze(body)) return false;
        }
        std::ofstream stream(path / file.relative_path,
                             std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(body.data(),
                     static_cast<std::streamsize>(body.size()));
        if (!stream.good()) return false;
    }

    // A planted package's bytes do not match the stamp this build would write,
    // so it gets none: the next ordinary open rewrites the real asset instead
    // of inheriting the plant.
    if (plant) {
        std::error_code remove_ec;
        std::filesystem::remove(stamp_path, remove_ec);
        return true;
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
    return {kOpenSettingsCommand, kUndoCommand, kRedoCommand};
}

bool Spectr::perform_command(pulp::view::CommandID id) {
    if ((id != kOpenSettingsCommand && id != kUndoCommand && id != kRedoCommand)
        || !native_scripted_ui_ || !native_scripted_ui_->bridge()) {
        return false;
    }
    try {
        if (id == kUndoCommand || id == kRedoCommand) {
            // Routed through the DOCUMENT's own bridge wrapper, not straight
            // at the EditorBridge handler.
            //
            // Measured on the shipping standalone: `performKeyEquivalent:`
            // offers a Command chord to `rootView->on_global_key` first and
            // returns without any script fan-out when that claims it, so
            // Cmd+Z is answered HERE and the document's keydown listener --
            // and with it the `window.pulp.postMessage('undo')` wrapper --
            // never runs. That wrapper is the only thing that turns an undo
            // response into `emit('processing_state_live', ...)`, and a
            // history replay lands via `replace_processing_state`, which does
            // not advance `host_automation_revision_`, so the editor's own
            // live-publication tick does not fire for it either. Calling the
            // handler directly therefore undid the authority's state and left
            // the screen exactly as it was, which is indistinguishable from
            // Cmd+Z doing nothing -- and is what was reported. Clicking the
            // menu's own Undo row worked the whole time, because that row
            // goes through the wrapper.
            //
            // Going through the wrapper means the chord and the row are one
            // path rather than two that have to be kept in agreement.
            //
            // The command is consumed either way, even when history is empty,
            // so the host never treats Cmd/Ctrl+Z as its own project-removal
            // command.
            native_scripted_ui_->bridge()->load_script(
                id == kUndoCommand
                    ? "(() => { window.pulp.postMessage('undo', {}); "
                      "if (typeof globalThis.__pulpRuntimeSettle__ === "
                      "'function') globalThis.__pulpRuntimeSettle__(8); })();"
                    : "(() => { window.pulp.postMessage('redo', {}); "
                      "if (typeof globalThis.__pulpRuntimeSettle__ === "
                      "'function') globalThis.__pulpRuntimeSettle__(8); })();",
                id == kUndoCommand ? "spectr-undo-command"
                                   : "spectr-redo-command");
            return true;
        }
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
            // The help overlay's copy. Loaded here rather than inlined into
            // the materialized document so the text is editable without
            // patching a checked-in one-line artifact. It assigns one string
            // and reads nothing, so the order relative to the bind script
            // below does not matter; the overlay reads the global when the
            // reader opens it, which is long after both. A missing or empty
            // asset is not fatal -- the panel says the content did not load
            // rather than painting an empty box.
            {
                std::ifstream help_stream(native_package_path_ / "help-content.js",
                                          std::ios::binary);
                std::string help((std::istreambuf_iterator<char>(help_stream)),
                                 std::istreambuf_iterator<char>());
                if (!help.empty())
                    bridge->load_script(help, "spectr-help-content");
                else
                    pulp::runtime::log_error(
                        "[Spectr native] help-content.js was empty; the help "
                        "overlay will report that its content did not load");
            }
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
            // Make `console.error` say what went wrong.
            //
            // An Error's own properties are NON-ENUMERABLE, so whatever the
            // bridge does to serialise one collapses to `{}` -- which is what
            // `script-ui[error] {}` has been printing for every JS failure in
            // this editor. The payload is byte-identical for a benign probe
            // and for a throw that kills the whole editor subtree, so the log
            // could not tell them apart and three separate root causes were
            // found only by re-deriving them from behaviour.
            //
            // Flattening the arguments HERE, before they reach the bridge,
            // makes the message and the stack survive. It is installed
            // unconditionally and before any fixture script, because the
            // errors worth reading are the ones nobody set out to catch.
            static constexpr const char* kErrorShim = R"JS(
(() => {
  if (typeof console === 'undefined' || console.__spectrErrorShim) return;
  const orig = console.error ? console.error.bind(console) : null;
  const fmt = (v) => {
    try {
      if (v instanceof Error)
        return (v.name || 'Error') + ': ' + (v.message || '')
             + (v.stack ? '\n' + v.stack : '');
      if (typeof v === 'string') return v;
      if (v && typeof v === 'object') {
        const own = Object.getOwnPropertyNames(v);
        if (own.length) return '{' + own.map((k) => {
          let s; try { s = String(v[k]); } catch (e) { s = '<throws>'; }
          return k + ': ' + s;
        }).join(', ') + '}';
      }
      return String(v);
    } catch (e) { return '<unformattable>'; }
  };
  console.__spectrErrorShim = true;
  console.error = function () {
    const text = Array.prototype.map.call(arguments, fmt).join(' ');
    if (orig) orig(text);
  };
})();
)JS";
            bridge->load_script(kErrorShim, "spectr-console-error-shim");

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

#if defined(SPECTR_ENABLE_PERF_FIXTURES)
    // ── Per-frame gesture-perf fixture ──────────────────────────────────
    //
    // SPECTR_GESTURE_PERF=<mods>,<x0>,<y0>,<x1>,<y1>,<steps>
    //   mods   none | cmd | cmd-shift | shift   (anything else disables)
    //   x,y    normalized to the editor root box, top-left origin
    //   steps  drag samples; the gesture emits steps + 1 events
    //
    // Why this exists rather than the window host's own pointer drive: that
    // drive synthesises every NSEvent with `modifierFlags:0`, so it can
    // express a band drag but never a Command-held marquee. Marquee is a
    // separate branch of the editor's pointer handler, not a variant of the
    // drag, so without modifiers it cannot be measured at all.
    //
    // One sample per frame, through the same pointer_dispatch verbs a window
    // host calls, so the JS handler, the React render it schedules, the
    // native commit and the canvas repaint all run exactly as they do under a
    // human pointer. What it does NOT reach is the window-space ->
    // design-space transform in WindowHost; that cost is per-event and
    // identical for every mods value, so it cancels in an A/B.
    // Latched off once the gesture and its readback are behind us, so the
    // frames AFTER the measured window carry none of this fixture's parsing.
    if (!gesture_perf_done_ && native_editor_root_ != nullptr) {
        if (const auto* spec = std::getenv("SPECTR_GESTURE_PERF");
            spec != nullptr && *spec != '\0') {
            std::uint16_t mods = 0;
            float pt[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int steps = 0;
            bool valid = true;
            {
                std::string_view rest{spec};
                const auto take = [&rest]() -> std::string {
                    const auto comma = rest.find(',');
                    std::string field{rest.substr(0, comma)};
                    rest.remove_prefix(comma == std::string_view::npos
                                           ? rest.size()
                                           : comma + 1);
                    return field;
                };
                const std::string name = take();
                if (name == "none") mods = 0;
                else if (name == "cmd") mods = pulp::view::kModCmd;
                else if (name == "shift") mods = pulp::view::kModShift;
                else if (name == "cmd-shift")
                    mods = pulp::view::kModCmd | pulp::view::kModShift;
                else valid = false;
                for (int i = 0; i < 4 && valid; ++i) {
                    const std::string field = take();
                    if (field.empty()) { valid = false; break; }
                    pt[i] = std::strtof(field.c_str(), nullptr);
                    if (!(pt[i] >= 0.0f && pt[i] <= 1.0f)) valid = false;
                }
                const std::string steps_field = take();
                steps = std::atoi(steps_field.c_str());
                if (steps < 1) valid = false;
            }
            // Refuse rather than guess: a typo that silently dragged off-plot
            // would produce a plausible idle trace with no gesture in it.
            if (!valid) {
                if (gesture_perf_tick_ < 0)
                    std::fprintf(stderr,
                                 "[gesture-perf] refusing unparseable spec '%s'\n",
                                 spec);
                gesture_perf_tick_ = 0;
            } else {
                // Enough frames for the document to mount, the band count to
                // settle and the analyzer to start publishing, so the first
                // sample is not competing with first-frame work.
                constexpr int kWarmupFrames = 90;
                const int tick = gesture_perf_tick_ < 0 ? 0 : gesture_perf_tick_;
                gesture_perf_tick_ = tick + 1;
                const int sample = tick - kWarmupFrames;
                // Readback AFTER the measured window. A gain dump proves the
                // marquee did not drag; it cannot prove it SELECTED. Without
                // this, "marquee changed nothing" and "the fixture never
                // reached the marquee branch" produce identical evidence.
                if (sample == steps + 1 && native_scripted_ui_
                    && native_scripted_ui_->bridge()) {
                    try {
                        native_scripted_ui_->bridge()->load_script(
                            "(() => { const h = globalThis.__spectrTestHooks; "
                            "if (!h || typeof h.renderState !== 'function') { "
                            "console.log('[gesture-perf] selection=<no-hook>'); return; } "
                            "const s = h.renderState().selection; "
                            "console.log('[gesture-perf] selection=' + s.length "
                            "+ ' [' + s.join(',') + ']'); })();",
                            "spectr-gesture-perf-readback");
                    } catch (const std::exception& error) {
                        std::fprintf(stderr, "[gesture-perf] readback failed: %s\n",
                                     error.what());
                    }
                    gesture_perf_done_ = true;
                }
                if (sample >= 0 && sample <= steps) {
                    const auto bounds = native_editor_root_->bounds();
                    const float t = static_cast<float>(sample)
                                  / static_cast<float>(steps);
                    const pulp::view::Point p{
                        (pt[0] + (pt[2] - pt[0]) * t) * bounds.width,
                        (pt[1] + (pt[3] - pt[1]) * t) * bounds.height};
                    PULP_TRACE_SCOPE_NAMED("js", "gesture perf sample");
                    if (sample == 0) {
                        gesture_perf_target_ = native_editor_root_->hit_test(p);
                        std::fprintf(stderr,
                                     "[gesture-perf] mods=0x%x start=(%.1f,%.1f) "
                                     "steps=%d root=%gx%g target=%s\n",
                                     static_cast<unsigned>(mods), p.x, p.y, steps,
                                     bounds.width, bounds.height,
                                     gesture_perf_target_
                                         ? (gesture_perf_target_->id().empty()
                                                ? "<anonymous>"
                                                : gesture_perf_target_->id().c_str())
                                         : "NONE");
                        if (gesture_perf_target_ != nullptr)
                            pulp::view::deliver_mouse_down(*native_editor_root_,
                                                           gesture_perf_target_,
                                                           p, mods, 1, true);
                    } else if (gesture_perf_target_ != nullptr) {
                        if (sample == steps) {
                            pulp::view::deliver_mouse_up(
                                *native_editor_root_, gesture_perf_target_, p,
                                mods, 1, pulp::view::MouseUpHost{});
                            std::fprintf(stderr,
                                         "[gesture-perf] released after %d samples\n",
                                         steps);
                            gesture_perf_target_ = nullptr;
                        } else {
                            pulp::view::deliver_mouse_drag(*native_editor_root_,
                                                           gesture_perf_target_,
                                                           p, mods);
                        }
                    }
                }
            }
        }
    }
#endif

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
    // SPECTR_GESTURES is a `;`-separated list of
    //     name=[mods:]x0,y0>x1,y1[@steps]
    // in root coordinates, where mods is `-`-joined from
    // none|shift|cmd|alt|ctrl. Gestures run in order against one live editor,
    // with the runtime settled between them, so a sequence that depends on
    // what the previous gesture selected or muted is expressible.
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

            int gesture_probe_settle_frames = 8;
            if (const auto* frames = std::getenv("SPECTR_GESTURES_SETTLE"))
                gesture_probe_settle_frames = std::max(1, std::atoi(frames));
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
                // Optional modifier prefix: `name=mods:x0,y0>x1,y1@steps`.
                // Without it this probe delivered every press with
                // modifierFlags 0, which expresses a band drag and nothing
                // else -- a Shift-held mute brush and a Command-held marquee
                // are separate branches of the editor's pointer handler, not
                // variants of the drag, so no amount of driving the drag
                // reaches them. Coordinates carry commas and never a colon,
                // so the split is unambiguous.
                std::uint16_t mods = 0;
                bool mods_ok = true;
                if (const auto colon = coords.find(':');
                    colon != std::string::npos) {
                    // Copied, not viewed: reassigning `coords` below frees
                    // the buffer a string_view into it would still point at.
                    const std::string mod_names = coords.substr(0, colon);
                    coords = coords.substr(colon + 1);
                    std::string_view names{mod_names};
                    while (!names.empty() && mods_ok) {
                        const auto dash = names.find('-');
                        const auto one = names.substr(0, dash);
                        names = dash == std::string_view::npos
                            ? std::string_view{} : names.substr(dash + 1);
                        if (one == "none") continue;
                        else if (one == "shift") mods |= pulp::view::kModShift;
                        else if (one == "cmd") mods |= pulp::view::kModCmd;
                        else if (one == "alt") mods |= pulp::view::kModAlt;
                        else if (one == "ctrl") mods |= pulp::view::kModCtrl;
                        else mods_ok = false;
                    }
                }
                // Refuse rather than guess. A typo'd modifier silently
                // delivered as mods=0 would drive the PLAIN branch and
                // produce a well-formed sample list in which the gesture
                // under test never ran -- which reads exactly like a control.
                if (!mods_ok) {
                    std::fprintf(stderr,
                                 "[gesture-probe] refusing unknown modifier in "
                                 "'%s'\n", std::string(spec).c_str());
                    continue;
                }
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
                    << ",\"steps\":" << steps
                    << ",\"mods\":" << static_cast<unsigned>(mods);
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
                pulp::view::deliver_mouse_down(root, target, a, mods, 1);
                out << ",";
                emit_sample("down", a);
                for (int i = 1; i <= steps; ++i) {
                    const float t = static_cast<float>(i)
                                  / static_cast<float>(steps);
                    const pulp::view::Point pt{a.x + (b.x - a.x) * t,
                                               a.y + (b.y - a.y) * t};
                    auto* live = capture.live_in(root);
                    if (live == nullptr) break;
                    pulp::view::deliver_mouse_drag(root, live, pt, mods, 1);
                    out << ",";
                    emit_sample("move", pt);
                }
                if (auto* live = capture.live_in(root)) {
                    pulp::view::MouseUpHost up_host;
                    pulp::view::deliver_mouse_up(root, live, b, mods, 1, up_host);
                }
                out << ",";
                emit_sample("up", b);
                // A gesture SEQUENCE needs the runtime to settle between its
                // members, for two independent reasons. The editor's press
                // handler reads `selection` out of a React closure, so a
                // marquee's `setSelection` reaches the NEXT press only after a
                // commit re-binds the handler. And the field these samples
                // read is published through a queue, so the last sample of a
                // gesture is read before its own commit lands. Settling and
                // then taking one more sample makes the post-gesture reading
                // authoritative instead of one frame stale.
                settle_native_runtime_(gesture_probe_settle_frames);
                out << ",";
                emit_sample("settled", b);
                out << "]}";
            }
            out << "\n ]}\n";
            std::ofstream file(gesture_out);
            file << out.str();
            gesture_probe_done_ = true;
        }
    }

    // ── Band-menu scenario runner (standalone) ──────────────────────────
    //
    // The band context menu could not be driven by ANY existing harness in
    // this repo: the gesture probe above is single-tick and left-button only,
    // so it can neither open the menu (that needs a context press) nor wait
    // the frames a React commit and a relayout take. Every gate over this menu
    // therefore drove it by CSS selector, which never consults `hit_test` --
    // and a selector happily "presses" a row no pointer can reach, which is
    // exactly this menu's failure mode.
    //
    // This runs one step per `SPECTR_MENU_SCENARIO_DELAY` ticks in the real
    // shipping standalone, and after every step records what a human could
    // see and what the DSP actually got: whether the menu is mounted, the
    // container's rect, every row's painted rect and whether a press at that
    // rect's centre resolves inside that same row, plus the band field, the
    // edit-mode parameter and the viewport.
    //
    // Steps (semicolon-separated, `name=kind[:arg]`):
    //   rpress:x,y     a context press -- the call the platform host makes on
    //                  a right-click, which is the only way this menu opens
    //   press:x,y      a left click at root coordinates (down then up)
    //   row:LABEL      a left click at the painted centre of the row whose
    //                  label ENDS WITH LABEL, resolved from the live tree at
    //                  that moment. The honest driver: it is where the words
    //                  the user is reading actually are.
    //   key:SPEC       a KEY PRESS, delivered to script the way the macOS
    //                  standalone delivers one: `[mod+]*key`, e.g. `escape`
    //                  or `cmd+z`. This is NOT the `escape` step below. That
    //                  one calls `route_escape_to_active_overlay` directly,
    //                  so it exercises the native overlay slot and NOTHING
    //                  in JS -- an editor whose own key handling is dead
    //                  passes it. This step calls
    //                  `script_events::dispatch_global_key`, which is the
    //                  first thing `window_host_mac.mm`'s `keyDown:` does
    //                  (and, for a Command chord, what its
    //                  `performKeyEquivalent:` does), and is the only route
    //                  by which a key reaches the document's own listeners.
    //                  It deliberately does not then run the native Escape
    //                  policy, so a dismissal it observes is the document's.
    //   escape         the host's own Escape route for an active overlay
    //   outside:x,y    the host's own outside-press route
    //   param:ID=V     a host parameter write (arrangement, never a verdict).
    //                  NOTE: parameters the processor applies on the audio
    //                  thread (band gain/mute, band count) do NOT land in a
    //                  headless run -- no audio device means `process()` never
    //                  runs. The step still records "written", so use `drag`
    //                  to arrange a level and read `n_visible` before trusting
    //                  a layout change.
    //   resize:w,h     a host window resize, through `on_view_resized`
    //   wait           nothing at all -- the ambient control
    //
    // An unrecognised verb records `unknown-step` and changes nothing. A
    // scenario that emits one is measuring less than it claims, so
    // `tools/menu_scenario_check.py` refuses to report a verdict when any
    // step comes back that way.
    if (settings_fixture_scrolled_ && !menu_scenario_done_
        && native_editor_root_ != nullptr) {
        const auto* spec = std::getenv("SPECTR_MENU_SCENARIO");
        const auto* out_path = std::getenv("SPECTR_MENU_SCENARIO_OUT");
        if (spec != nullptr && *spec != '\0'
            && out_path != nullptr && *out_path != '\0') {
            if (menu_scenario_steps_.empty()) {
                std::string_view rest{spec};
                while (!rest.empty()) {
                    const auto semi = rest.find(';');
                    const auto one = rest.substr(0, semi);
                    if (semi == std::string_view::npos) rest = {};
                    else rest.remove_prefix(semi + 1);
                    if (!one.empty()) menu_scenario_steps_.emplace_back(one);
                }
                menu_scenario_json_ = "{\"schema\":\"spectr-menu-scenario-v1\","
                                      "\"steps\":[";
                if (const auto* delay = std::getenv("SPECTR_MENU_SCENARIO_DELAY"))
                    menu_scenario_delay_ = std::max(1, std::atoi(delay));
            }
            if (++menu_scenario_tick_ >= menu_scenario_delay_) {
                menu_scenario_tick_ = 0;
                auto& root = *native_editor_root_;
                const std::string step = menu_scenario_steps_[menu_scenario_index_];
                const auto eq = step.find('=');
                const std::string name = eq == std::string::npos
                    ? step : step.substr(0, eq);
                const std::string body = eq == std::string::npos
                    ? std::string{} : step.substr(eq + 1);
                const auto colon = body.find(':');
                const std::string kind = colon == std::string::npos
                    ? body : body.substr(0, colon);
                const std::string arg = colon == std::string::npos
                    ? std::string{} : body.substr(colon + 1);

                const auto point_of = [](const std::string& text,
                                         pulp::view::Point& pt) {
                    const auto comma = text.find(',');
                    if (comma == std::string::npos) return false;
                    pt.x = std::strtof(text.substr(0, comma).c_str(), nullptr);
                    pt.y = std::strtof(text.substr(comma + 1).c_str(), nullptr);
                    return true;
                };
                // The platform host's OWN press sequence, in its order
                // (window_host_mac.mm -mouseDown:/-mouseUp:), because any
                // shortcut here measures the shortcut. Two parts of it are
                // load-bearing and easy to omit:
                //
                //   * a press inside the active overlay is routed by
                //     route_press_to_active_overlay and delivered with
                //     bubble=FALSE, so it never reaches the ancestors the
                //     overlay is mounted inside. A driver that skips this and
                //     hit-tests directly bubbles the press into the spectrum
                //     surface underneath and measures a defect the host does
                //     not have.
                //   * the CLICK is fired by mouseUp's MouseUpHost::fire_click.
                //     With a default-constructed host nothing fires, every row
                //     reads inert, and the run looks like a product failure.
                const auto click_at = [&root](pulp::view::Point pt) {
                    pulp::view::ViewCapture capture;
                    std::string route = "hit-test";
                    bool bubble = true;
                    const auto overlay_press =
                        pulp::view::route_press_to_active_overlay(root, pt);
                    if (overlay_press.routing
                        == pulp::view::OverlayPressRouting::routed) {
                        capture.set(overlay_press.target);
                        bubble = false;
                        route = "overlay-routed";
                    } else if (overlay_press.consume_press) {
                        return std::string{"dismiss-consumed-press"};
                    } else {
                        capture.set(root.hit_test(pt));
                    }
                    auto* target = capture.live_in(root);
                    if (target == nullptr) return route + ":no-target";
                    if (!pulp::view::deliver_mouse_down(root, target, pt, 0, 1,
                                                       bubble))
                        capture.reset();
                    auto* live = capture.live_in(root);
                    if (live == nullptr) return route + ":unmounted-on-down";
                    std::string clicked{"<none>"};
                    pulp::view::MouseUpHost up_host;
                    up_host.fire_click =
                        [&clicked](const std::function<void()>& handler,
                                   const std::string& id, std::uint16_t) {
                            clicked = id.empty() ? std::string{"<anon>"} : id;
                            if (handler) handler();
                        };
                    pulp::view::deliver_mouse_up(root, live, pt, 0, 1, up_host);
                    return route + ":click=" + clicked;
                };

                std::string action = kind;
                std::string detail;
                float press_x = -1.0f, press_y = -1.0f;
                bool attributable = false;
                if (kind == "rpress") {
                    pulp::view::Point pt{};
                    if (point_of(arg, pt)) {
                        const auto res = pulp::view::route_context_press(root, pt);
                        press_x = pt.x; press_y = pt.y;
                        detail = res.handled ? "handled" : "not-handled";
                    } else detail = "bad-arg";
                } else if (kind == "press") {
                    pulp::view::Point pt{};
                    if (point_of(arg, pt)) {
                        press_x = pt.x; press_y = pt.y;
                        detail = click_at(pt);
                    } else detail = "bad-arg";
                } else if (kind == "row") {
                    int number = -1;
                    auto* scope = spectr_menu_probe::menu_container(root, number);
                    if (scope == nullptr) detail = "menu-absent";
                    else {
                        const auto aim = spectr_menu_probe::aim_row(*scope, arg);
                        if (!aim.found || aim.row == nullptr) detail = "row-absent";
                        else if (aim.w <= 0.0f || aim.h <= 0.0f) detail = "zero-area";
                        else {
                            press_x = aim.cx; press_y = aim.cy;
                            auto* hit = root.hit_test(
                                pulp::view::Point{aim.cx, aim.cy});
                            attributable =
                                spectr_menu_probe::nearest_clickable(hit) == aim.row;
                            detail = click_at(
                                pulp::view::Point{aim.cx, aim.cy});
                        }
                    }
                } else if (kind == "key") {
                    // Same spec grammar as the SPECTR_KEY fixture above.
                    std::string spec = arg;
                    uint16_t key_mods = 0;
                    bool bad_mod = false;
                    for (;;) {
                        const auto plus = spec.find('+');
                        if (plus == std::string::npos || plus + 1 >= spec.size())
                            break;
                        const std::string mod = spec.substr(0, plus);
                        if (mod == "cmd")        key_mods |= pulp::view::kModCmd;
                        else if (mod == "meta")  key_mods |= pulp::view::kModMeta;
                        else if (mod == "ctrl")  key_mods |= pulp::view::kModCtrl;
                        else if (mod == "alt")   key_mods |= pulp::view::kModAlt;
                        else if (mod == "shift") key_mods |= pulp::view::kModShift;
                        else { bad_mod = true; break; }
                        spec.erase(0, plus + 1);
                    }
                    pulp::view::KeyCode code = pulp::view::KeyCode::unknown;
                    if (spec == "escape") code = pulp::view::KeyCode::escape;
                    else if (spec == "enter") code = pulp::view::KeyCode::enter;
                    else if (spec == "tab") code = pulp::view::KeyCode::tab;
                    else if (spec.size() == 1) {
                        char c = spec[0];
                        if (c >= 'A' && c <= 'Z')
                            c = static_cast<char>(c - 'A' + 'a');
                        code = static_cast<pulp::view::KeyCode>(c);
                    }
                    if (bad_mod || code == pulp::view::KeyCode::unknown) {
                        detail = "bad-arg";
                    } else {
                        // The macOS standalone's order, not a convenient one.
                        // `performKeyEquivalent:` offers the chord to the
                        // root hook FIRST and, when that claims it, returns
                        // without any script fan-out -- so a chord the native
                        // CommandRegistry owns never reaches the document's
                        // own keydown listener at all. Reproducing that order
                        // is the whole point: it is what decides which half
                        // of a key path is answering.
                        pulp::view::KeyEvent down;
                        down.key = code;
                        down.modifiers = key_mods;
                        down.is_down = true;
                        const bool root_claimed =
                            root.on_global_key && root.on_global_key(down);
                        if (!root_claimed) {
                            pulp::view::script_events::dispatch_global_key(
                                static_cast<int>(code), key_mods,
                                /*is_down=*/true);
                            pulp::view::script_events::dispatch_global_key(
                                static_cast<int>(code), key_mods,
                                /*is_down=*/false);
                        }
                        detail = root_claimed ? "root" : "script";
                    }
                } else if (kind == "escape") {
                    const auto res =
                        pulp::view::route_escape_to_active_overlay(root);
                    detail = res == pulp::view::OverlayEscapeResult::overlay
                        ? "overlay"
                        : (res == pulp::view::OverlayEscapeResult::none
                               ? "none" : "modal");
                } else if (kind == "outside") {
                    pulp::view::Point pt{};
                    if (point_of(arg, pt)) {
                        press_x = pt.x; press_y = pt.y;
                        const auto res =
                            pulp::view::route_press_to_active_overlay(root, pt);
                        detail = res.routing
                                     == pulp::view::OverlayPressRouting::dismissed
                            ? "dismissed" : "not-dismissed";
                    } else detail = "bad-arg";
                } else if (kind == "param") {
                    const auto assign = arg.find('=');
                    if (assign != std::string::npos && param_store_ != nullptr) {
                        param_store_->set_value(
                            static_cast<pulp::state::ParamID>(
                                std::atoi(arg.substr(0, assign).c_str())),
                            static_cast<float>(
                                std::atof(arg.substr(assign + 1).c_str())));
                        detail = "written";
                    } else detail = "bad-arg";
                } else if (kind == "drag") {
                    // A real band drag, delivered through the same verbs the
                    // host runs. This is the ONLY way to arrange a band level
                    // here: a host-parameter write reaches the field through
                    // apply_parameters on the AUDIO thread, and a headless
                    // run opens no audio device, so process() never runs and
                    // the write is invisible. Arranging through the editor is
                    // also the more faithful arrangement.
                    const auto gt = arg.find('>');
                    int dsteps = 16;
                    std::string coords = arg;
                    if (const auto at = coords.find('@'); at != std::string::npos) {
                        dsteps = std::max(1, std::atoi(coords.substr(at + 1).c_str()));
                        coords = coords.substr(0, at);
                    }
                    pulp::view::Point a{}, b{};
                    const auto gt2 = coords.find('>');
                    if (gt == std::string::npos || !point_of(coords.substr(0, gt2), a)
                        || !point_of(coords.substr(gt2 + 1), b)) {
                        detail = "bad-arg";
                    } else {
                        pulp::view::ViewCapture capture;
                        capture.set(root.hit_test(a));
                        auto* target = capture.live_in(root);
                        if (target == nullptr) detail = "no-target";
                        else {
                            pulp::view::deliver_mouse_down(root, target, a, 0, 1);
                            for (int i = 1; i <= dsteps; ++i) {
                                const float t = static_cast<float>(i)
                                              / static_cast<float>(dsteps);
                                auto* live = capture.live_in(root);
                                if (live == nullptr) break;
                                pulp::view::deliver_mouse_drag(
                                    root, live,
                                    pulp::view::Point{a.x + (b.x - a.x) * t,
                                                      a.y + (b.y - a.y) * t},
                                    0, 1);
                            }
                            if (auto* live = capture.live_in(root)) {
                                pulp::view::MouseUpHost up_host;
                                pulp::view::deliver_mouse_up(root, live, b, 0, 1,
                                                             up_host);
                            }
                            detail = "dragged";
                        }
                    }
                } else if (kind == "wheel") {
                    // The real zoom gesture, through the host's own wheel verb.
                    // arg is "x,y,dy[,count]".
                    float wx = 0.0f, wy = 0.0f, dy = 0.0f;
                    int count = 1;
                    {
                        std::vector<std::string> parts;
                        std::string cur;
                        for (char c : arg) {
                            if (c == ',') { parts.push_back(cur); cur.clear(); }
                            else cur.push_back(c);
                        }
                        parts.push_back(cur);
                        if (parts.size() >= 3) {
                            wx = std::strtof(parts[0].c_str(), nullptr);
                            wy = std::strtof(parts[1].c_str(), nullptr);
                            dy = std::strtof(parts[2].c_str(), nullptr);
                            if (parts.size() >= 4)
                                count = std::max(1, std::atoi(parts[3].c_str()));
                            pulp::view::WheelHost wheel_host;
                            for (int i = 0; i < count; ++i)
                                pulp::view::deliver_mouse_wheel(
                                    root, {wx, wy}, 0.0f, dy, wheel_host);
                            detail = "wheeled";
                        } else detail = "bad-arg";
                    }
                } else if (kind == "neutral") {
                    // Arrangement primitive: every band back to 0 dB and
                    // unmuted, through the host parameters. Never a verdict.
                    if (param_store_ != nullptr) {
                        for (std::size_t i = 0; i < kMaxBands; ++i) {
                            param_store_->set_value(band_gain_param_id(i), 0.0f);
                            param_store_->set_value(band_mute_param_id(i), 0.0f);
                        }
                        detail = "neutralised";
                    } else detail = "no-store";
                } else if (kind == "resize") {
                    // A host window resize, through the same entry point the
                    // platform host calls -- `on_view_resized` -- so the whole
                    // tree re-solves exactly as it does when a person drags the
                    // window corner. arg is "w,h".
                    //
                    // The scenario has emitted this verb since the reopen
                    // sequence was written, to answer the other half of the
                    // field report ("i can't tell if it's fixed after resizing
                    // explicitly or not"). Nothing implemented it, so both
                    // steps recorded `unknown-step`, the window never moved,
                    // and the two reopens after them measured the unresized
                    // window while reading as though a resize had happened.
                    pulp::view::Point size{};
                    if (point_of(arg, size) && size.x > 0.0f && size.y > 0.0f) {
                        // BOTH axes, before and after. "resized" on its own is a
                        // claim about the CALL, and the two outcomes a reader
                        // has to tell apart look identical from the verb name:
                        // a resize that never arrived, and a resize the pinned
                        // design viewport deliberately absorbs.
                        //
                        // It is the second one here. Under the pin the HOST
                        // owns the scale and `on_view_resized` holds the root
                        // at the authored box at every host size, so the host
                        // figure moves and the root figure does not. That is
                        // why these two steps cannot answer "is the menu
                        // repaired by resizing" -- under a pin the menu's
                        // design-space geometry is resize-invariant by
                        // construction, so there is nothing for a resize to
                        // repair or to break.
                        const auto before = root.bounds();
                        const auto host_before_w = native_host_width_;
                        const auto host_before_h = native_host_height_;
                        on_view_resized(root,
                                        static_cast<std::uint32_t>(size.x),
                                        static_cast<std::uint32_t>(size.y));
                        const auto after = root.bounds();
                        std::ostringstream note;
                        note << "resized host " << host_before_w << "x"
                             << host_before_h << "->" << native_host_width_
                             << "x" << native_host_height_ << " root "
                             << before.width << "x" << before.height << "->"
                             << after.width << "x" << after.height;
                        detail = note.str();
                    } else detail = "bad-arg";
                } else if (kind == "wait" || kind.empty()) {
                    detail = "ambient";
                } else {
                    detail = "unknown-step";
                }

                // ── the reading ──
                int band_number = -1;
                auto* scope = spectr_menu_probe::menu_container(root, band_number);
                const auto snap = processing_state_snapshot();
                const auto n = visible_count(snap.layout);
                std::ostringstream js;
                js << (menu_scenario_index_ ? ",\n  " : "\n  ")
                   << "{\"step\":\"" << spectr_menu_probe::json_escape(name)
                   << "\",\"kind\":\"" << spectr_menu_probe::json_escape(kind)
                   << "\",\"arg\":\"" << spectr_menu_probe::json_escape(arg)
                   << "\",\"result\":\"" << spectr_menu_probe::json_escape(detail)
                   << "\",\"press\":[" << press_x << "," << press_y << "]"
                   << ",\"attributable\":" << (attributable ? "true" : "false")
                   << ",\"menu_mounted\":" << (scope != nullptr ? "true" : "false")
                   << ",\"menu_band\":" << band_number
                   << ",\"menu_scope_depth\":"
                   << spectr_menu_probe::menu_scope_depth;
                if (scope != nullptr) {
                    float mx = 0.0f, my = 0.0f;
                    spectr_menu_probe::root_origin_of(*scope, mx, my);
                    js << ",\"menu_rect\":[" << mx << "," << my << ","
                       << scope->bounds().width << "," << scope->bounds().height
                       << "],\"menu_children\":" << scope->child_count()
                       << ",\"rows\":[";
                    bool first_row = true;
                    std::vector<pulp::view::View*> seen;
                    std::function<void(pulp::view::View&)> walk =
                        [&](pulp::view::View& v) {
                            if (!v.visible()) return;
                            if (const auto* label =
                                    dynamic_cast<const pulp::view::Label*>(&v);
                                label != nullptr && !label->text().empty()) {
                                float lx = 0.0f, ly = 0.0f;
                                spectr_menu_probe::root_origin_of(v, lx, ly);
                                const auto box = v.bounds();
                                auto* own = spectr_menu_probe::nearest_clickable(
                                    const_cast<pulp::view::View*>(&v));
                                bool self = false;
                                std::string covered_by;
                                if (own != nullptr && box.width > 0.0f
                                    && box.height > 0.0f) {
                                    auto* hit = root.hit_test(pulp::view::Point{
                                        lx + box.width * 0.5f,
                                        ly + box.height * 0.5f});
                                    auto* winner =
                                        spectr_menu_probe::nearest_clickable(hit);
                                    self = winner == own;
                                    // NAME what wins the row's own centre. A
                                    // bare `owns_own_centre:false` says a row
                                    // is unreachable but not what is over it,
                                    // and guessing that from y-bands has
                                    // already produced two wrong theories.
                                    if (!self) {
                                        const auto* who =
                                            winner == nullptr ? nullptr
                                            : spectr_menu_probe::find_label_if(
                                                  *winner,
                                                  [](const std::string& t,
                                                     const std::string&) {
                                                      return !t.empty();
                                                  },
                                                  std::string{}, true);
                                        covered_by = winner == nullptr
                                            ? "<nothing-clickable>"
                                            : (who == nullptr ? "<unlabelled>"
                                                              : who->text());
                                    }
                                }
                                js << (first_row ? "" : ",")
                                   << "{\"label\":\""
                                   << spectr_menu_probe::json_escape(label->text())
                                   << "\",\"rect\":[" << lx << "," << ly << ","
                                   << box.width << "," << box.height << "]"
                                   << ",\"pressable\":" << (own != nullptr && own->enabled()
                                                              ? "true" : "false")
                                   << ",\"owns_own_centre\":"
                                   << (self ? "true" : "false")
                                   << ",\"covered_by\":\""
                                   << spectr_menu_probe::json_escape(covered_by)
                                   << "\"}";
                                first_row = false;
                            }
                            for (std::size_t i = 0; i < v.child_count(); ++i)
                                walk(*v.child_at(i));
                        };
                    walk(*scope);
                    js << "]";
                }
                const auto modulation = modulation_settings();
                js << ",\"lfo1_enabled\":" << (modulation.enabled ? "true" : "false")
                   << ",\"lfo2_enabled\":" << (modulation.lfo2_enabled ? "true" : "false")
                   << ",\"lfo_target\":" << static_cast<int>(modulation.target)
                   << ",\"lfo_target_mask\":" << static_cast<int>(resolve_modulation_target_mask(modulation))
                   << ",\"n_visible\":" << n
                   << ",\"edit_mode\":"
                   << (param_store_ != nullptr
                           ? param_store_->get_value(kParamEditMode) : -1.0f)
                   << ",\"min_hz\":" << snap.viewport.min_hz
                   << ",\"max_hz\":" << snap.viewport.max_hz
                   << ",\"gain_db\":[";
                for (std::uint32_t i = 0; i < n; ++i)
                    js << (i ? "," : "") << snap.field.bands[i].gain_db;
                js << "],\"undo_depth\":" << editor_authority().undo_depth()
                   << ",\"redo_depth\":" << editor_authority().redo_depth()
                   << ",\"macros\":[";
                for (std::size_t m = 0; m < kMacroCount; ++m) {
                    js << (m ? "," : "") << "[";
                    const auto members = macro_members(m);
                    bool first = true;
                    for (std::uint32_t i = 0; i < n; ++i) {
                        if (!members.test(i)) continue;
                        js << (first ? "" : ",") << i;
                        first = false;
                    }
                    js << "]";
                }
                js << "],\"muted\":[";
                for (std::uint32_t i = 0; i < n; ++i)
                    js << (i ? "," : "")
                       << (snap.field.bands[i].muted ? "true" : "false");
                js << "]}";
                menu_scenario_json_ += js.str();

                if (++menu_scenario_index_ >= menu_scenario_steps_.size()) {
                    menu_scenario_json_ += "\n ]}\n";
                    std::ofstream file(out_path);
                    file << menu_scenario_json_;
                    file.close();
                    menu_scenario_done_ = true;
                    pulp::runtime::log_info(
                        "Spectr: band-menu scenario complete ({} steps) -> {}",
                        menu_scenario_steps_.size(), out_path);
                }
            }
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

    // ── Output level ────────────────────────────────────────────────────
    //
    // Published BEFORE the spectrum guard below on purpose. That guard drops
    // the tick when the analyzer sequence has not advanced, which is exactly
    // the silent/stopped case -- and a level readout that stops updating when
    // the signal stops is a readout that lies about the signal stopping.
    // This costs one triple-buffer read per tick and touches no audio thread.
    {
        const auto level = read_output_level();
        // Publish only a CHANGED reading. A meter that republishes an
        // unchanged one 30 times a second is another per-frame script
        // evaluation and another React commit for a number that did not
        // move -- the same cost the hover readout's 700ms throttle and the
        // zoom readout's live guard were both written to avoid. It is also
        // load bearing for the test fleet: an unconditional per-tick
        // publication perturbed settle timing enough to revert a native
        // bounds write, which turned `Spectr-preset-operations-negative-
        // control` from a working plant into a silent pass.
        //
        // Compared at the resolution the editor PRINTS (0.1 dB), so
        // dither-level movement below the last displayed digit is not a
        // change. The hold only rises, so a still signal publishes nothing.
        const auto quantised = std::isfinite(level.peak_db)
            ? std::round(level.peak_db * 10.0f)
            : std::numeric_limits<float>::lowest();
        const bool moved = quantised != native_output_level_peak_
            || level.over != native_output_level_over_
            || level.trim_db != native_output_level_trim_db_;
        native_output_level_peak_ = quantised;
        native_output_level_over_ = level.over;
        native_output_level_trim_db_ = level.trim_db;

        // Only the publication is skipped, never the rest of the tick: the
        // analyzer frame below has its own cadence and its own guard.
        if (moved) {
            std::ostringstream meter;
            meter << "if (typeof globalThis.__spectrPublishNativeMessage === "
                     "'function') globalThis.__spectrPublishNativeMessage("
                     "'output_meter',{schema_version:1,peak_db:"
                  << (std::isfinite(level.peak_db)
                          ? std::to_string(level.peak_db)
                          : std::string("null"))
                  << ",over:" << (level.over ? "true" : "false")
                  << ",trim_db:" << level.trim_db
                  << "},'spectr-output-meter');";
            try {
                native_scripted_ui_->bridge()->load_script(
                    meter.str(), "spectr-native-output-meter");
            } catch (const std::exception& error) {
                pulp::runtime::log_error(
                    "[Spectr native] output meter publication rejected: {}",
                    error.what());
            }
        }
    }

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
    // Forget the last published level, so reopening the editor republishes
    // rather than sitting at "--" until the reading happens to move.
    native_output_level_peak_ = std::numeric_limits<float>::max();
    native_output_level_over_ = false;
    native_output_level_trim_db_ = std::numeric_limits<float>::max();
    native_host_automation_revision_ = host_automation_revision();
    editor_authority().reset_transient_state();
#if defined(SPECTR_ENABLE_PERF_FIXTURES)
    // The gesture-perf fixture holds a raw View* into the tree being torn
    // down, and its tick counter would otherwise resume mid-gesture against a
    // freshly mounted editor. Both are dropped with the tree they belong to.
    gesture_perf_target_ = nullptr;
    gesture_perf_tick_ = -1;
    gesture_perf_done_ = false;
#endif
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
