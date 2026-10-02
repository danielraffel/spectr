#include "spectr/spectr.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "appearance_detectors.hpp"

#include <pulp/canvas/recording_canvas.hpp>
#include <pulp/state/store.hpp>
#include <pulp/runtime/trace.hpp>
#include <pulp/view/canvas_widget.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/tracing_badge.hpp>
#include <pulp/view/hover_cursor.hpp>
#include <pulp/view/input_events.hpp>
#include <pulp/view/overlay_dismissal.hpp>
#include <pulp/view/pointer_dispatch.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/ui_components.hpp>
#include <pulp/view/window_host.hpp>
#include <pulp/view/widgets.hpp>
#include <pulp/view/widgets/svg_rect.hpp>
#include <pulp/view/widget_bridge.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <thread>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pulp::view::Point;
using pulp::view::View;

struct ScopedTemporaryDirectory {
    std::filesystem::path path;
#if defined(_WIN32)
    std::optional<std::wstring> previous_temp;
    std::optional<std::wstring> previous_tmp;
#else
    std::optional<std::string> previous_tmpdir;
#endif

    ScopedTemporaryDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch().count()
            ^ static_cast<std::int64_t>(std::random_device{}());
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = base
                / ("spectr-native-state-storage-" + std::to_string(nonce)
                   + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path = candidate;
                break;
            }
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "could not create isolated native storage", candidate, error);
        }
        if (path.empty())
            throw std::runtime_error("could not allocate isolated native storage");

        if (!install_environment()) {
            restore_environment();
            std::filesystem::remove(path);
            throw std::runtime_error("could not isolate native temporary storage");
        }
        if (std::filesystem::weakly_canonical(
                std::filesystem::temp_directory_path())
            != std::filesystem::weakly_canonical(path)) {
            restore_environment();
            std::filesystem::remove(path);
            throw std::runtime_error("native temporary storage isolation failed");
        }
    }

    bool install_environment() {
#if defined(_WIN32)
        if (const auto* value = ::_wgetenv(L"TEMP")) previous_temp = value;
        if (const auto* value = ::_wgetenv(L"TMP")) previous_tmp = value;
        return ::_wputenv_s(L"TEMP", path.c_str()) == 0
            && ::_wputenv_s(L"TMP", path.c_str()) == 0;
#else
        if (const auto* value = std::getenv("TMPDIR"))
            previous_tmpdir = value;
        return ::setenv("TMPDIR", path.c_str(), 1) == 0;
#endif
    }

    ~ScopedTemporaryDirectory() {
        restore_environment();
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    void restore_environment() const noexcept {
#if defined(_WIN32)
        ::_wputenv_s(L"TEMP", previous_temp ? previous_temp->c_str() : L"");
        ::_wputenv_s(L"TMP", previous_tmp ? previous_tmp->c_str() : L"");
#else
        if (previous_tmpdir) ::setenv("TMPDIR", previous_tmpdir->c_str(), 1);
        else ::unsetenv("TMPDIR");
#endif
    }
};

struct PatternStoragePoison {
    struct Snapshot {
        std::filesystem::path path;
        std::string poison;
    };

    ScopedTemporaryDirectory temporary_directory;
    Snapshot patterns;
    Snapshot default_id;

    static std::optional<std::string> read(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    }

    static void write(const std::filesystem::path& path, std::string_view value) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("could not seed native storage poison");
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        if (!stream) throw std::runtime_error("could not write native storage poison");
    }

    static std::string pattern_poison() {
        std::string gains{"["};
        for (int index = 0; index < 128; ++index) {
            if (index != 0) gains += ',';
            gains += '1';
        }
        gains += ']';
        return std::string{"[{\"id\":\"user:browser-poison\","
                           "\"name\":\"BROWSER POISON\","
                           "\"source\":\"user\",\"gains\":"}
            + gains + "}]";
    }

    PatternStoragePoison()
        : patterns{temporary_directory.path / "pulp-storage"
                       / "spectr.patterns.v1.dat",
                   pattern_poison()},
          default_id{temporary_directory.path / "pulp-storage"
                         / "spectr.defaultPatternId.v1.dat",
                     "user:browser-poison"} {
        write(patterns.path, patterns.poison);
        write(default_id.path, default_id.poison);
    }

    void require_unchanged() const {
        REQUIRE(read(patterns.path) == std::optional<std::string>{patterns.poison});
        REQUIRE(read(default_id.path)
                == std::optional<std::string>{default_id.poison});
    }
};

Point root_point(const View& view, float local_x, float local_y) {
    auto* target = const_cast<View*>(&view);
    auto* root = target;
    while (root->parent()) root = root->parent();
    const auto p0 = pulp::view::point_to_local({0.0f, 0.0f}, target, root);
    const auto px = pulp::view::point_to_local({1.0f, 0.0f}, target, root);
    const auto py = pulp::view::point_to_local({0.0f, 1.0f}, target, root);
    const float a = px.x - p0.x;
    const float b = py.x - p0.x;
    const float c = px.y - p0.y;
    const float d = py.y - p0.y;
    const float determinant = a * d - b * c;
    REQUIRE(std::abs(determinant) > 1.0e-6f);
    const float x = local_x - p0.x;
    const float y = local_y - p0.y;
    return {(d * x - b * y) / determinant,
            (-c * x + a * y) / determinant};
}

void settle(pulp::view::FrameClock& clock, int frames = 10) {
    for (int frame = 0; frame < frames; ++frame) clock.tick(1.0f / 60.0f);
}

const pulp::view::Label* find_label(const View& view, std::string_view text) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
        label != nullptr && label->text() == text)
        return label;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        if (const auto* match = find_label(*view.child_at(index), text))
            return match;
    return nullptr;
}

// Some menu rows paint their keyboard shortcut inside the same label as the
// caption, so an exact match on the caption stops finding the row the moment a
// shortcut is added to it. Match the caption the row is named for and let the
// shortcut ride along.
const pulp::view::Label* find_label_prefix(const View& view,
                                           std::string_view prefix) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
        label != nullptr && label->text().rfind(prefix, 0) == 0)
        return label;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        if (const auto* match = find_label_prefix(*view.child_at(index), prefix))
            return match;
    return nullptr;
}

pulp::view::ScrollView* owning_scroll_view(const View& view) {
    for (auto* node = const_cast<View*>(&view); node != nullptr;
         node = node->parent())
        if (auto* scroll = dynamic_cast<pulp::view::ScrollView*>(node))
            return scroll;
    return nullptr;
}

// Offset of `view` inside `ancestor` in UNSCROLLED content space, which is what
// ScrollView::set_scroll takes.
bool content_offset(const View& view, const View& ancestor, float& out_y) {
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

const View* find_sized_descendant(const View& view, float width, float height) {
    for (std::size_t index = 0; index < view.child_count(); ++index) {
        const auto* child = view.child_at(index);
        const auto bounds = child->bounds();
        if (std::abs(bounds.width - width) < 0.1f
            && std::abs(bounds.height - height) < 0.1f)
            return child;
        if (const auto* match = find_sized_descendant(*child, width, height))
            return match;
    }
    return nullptr;
}

void collect_svg_rects(const View& view,
                       std::vector<const pulp::view::SvgRectWidget*>& result) {
    if (const auto* rect = dynamic_cast<const pulp::view::SvgRectWidget*>(&view))
        result.push_back(rect);
    for (std::size_t index = 0; index < view.child_count(); ++index)
        collect_svg_rects(*view.child_at(index), result);
}

struct NativeEditorRig {
    pulp::state::StateStore store;
    spectr::Spectr processor;
    std::unique_ptr<View> root;
    pulp::view::FrameClock clock;
    pulp::view::ScriptedUiSession* session = nullptr;

    explicit NativeEditorRig(std::span<const std::uint8_t> state = {}) {
        processor.set_state_store(&store);
        processor.define_parameters(store);
        pulp::format::PrepareContext prepare;
        prepare.sample_rate = 48000.0;
        prepare.max_buffer_size = 256;
        prepare.input_channels = 2;
        prepare.output_channels = 2;
        processor.prepare(prepare);
        if (!state.empty()) REQUIRE(processor.deserialize_plugin_state(state));
        open();
    }

    ~NativeEditorRig() { close(); }

    void open() {
        REQUIRE(root == nullptr);
        root = processor.create_view();
        REQUIRE(root != nullptr);
        root->set_bounds({0, 0, 1320, 860});
        root->set_frame_clock(&clock);
        root->layout_children();
        processor.on_view_opened(*root);
        session = processor.active_scripted_ui();
        REQUIRE(session != nullptr);
        REQUIRE(session->bridge() != nullptr);
        settle(clock, 16);
    }

    void close() {
        if (!root) return;
        processor.on_view_closed(*root);
        root.reset();
        session = nullptr;
    }

    pulp::view::WidgetBridge& bridge() {
        REQUIRE(session != nullptr);
        REQUIRE(session->bridge() != nullptr);
        return *session->bridge();
    }

    // Drive a host-size change. Under the pinned (proportional) contract the
    // ROOT deliberately does NOT track the host: it stays at the authored box
    // and the host maps it onto the surface with one uniform scale. Asserting
    // root==host here is what the responsive contract required; asserting it
    // now would forbid the very behaviour the pin exists to provide.
    void resize(float width, float height) {
        REQUIRE(root != nullptr);
        processor.on_view_resized(*root, width, height);
        settle(clock, 16);
        if (pulp::format::should_pin_design_viewport(processor.view_size())) {
            CHECK(root->bounds().width
                  == Catch::Approx(spectr::kEditorDesignWidth));
            CHECK(root->bounds().height
                  == Catch::Approx(spectr::kEditorDesignHeight));
            return;
        }
        CHECK(root->bounds().width == Catch::Approx(width));
        CHECK(root->bounds().height == Catch::Approx(height));
    }
};

// Root-space rectangle, so paint geometry and hit geometry can be compared in
// one coordinate space regardless of where a view sits in the tree.
struct RootRect {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
};

RootRect root_rect(const View& view) {
    const auto bounds = view.bounds();
    const auto origin = root_point(view, 0.0f, 0.0f);
    return {origin.x, origin.y, origin.x + bounds.width,
            origin.y + bounds.height};
}

bool chain_interactive(const View& view) {
    for (const auto* node = &view; node != nullptr; node = node->parent())
        if (!node->visible() || !node->enabled()) return false;
    return true;
}

const View* nearest_click_target(const View* view) {
    while (view != nullptr && !view->on_click) view = view->parent();
    return view;
}

void collect_click_targets(const View& view, std::vector<const View*>& out) {
    // Canvas surfaces own pointer gestures, but they are not button controls:
    // their authored bounds intentionally cover the graph and may overlap the
    // editor resize grip. Canvas dispatch is covered by the dedicated N1
    // gesture test; keep this button/resize matrix scoped to click controls.
    if (view.on_click && chain_interactive(view)
        && dynamic_cast<const pulp::view::CanvasWidget*>(&view) == nullptr)
        out.push_back(&view);
    for (std::size_t index = 0; index < view.child_count(); ++index)
        collect_click_targets(*view.child_at(index), out);
}

// What the user aims at: the control's own border box unioned with every
// visible descendant box AND every shaped glyph run inside it. A label whose
// run is wider than the box it lives in is painted ink with no hit region
// behind it, which reads as "the button only works in part of the button".
RootRect painted_extent(const View& control) {
    auto extent = root_rect(control);
    const std::function<void(const View&)> walk = [&](const View& view) {
        if (!view.visible()) return;
        // An empty caption paints no ink, and its layout box can be far wider
        // than the control it sits in (a switch's unused text slot measures
        // 150x32 inside a 40x20 track). It is not something the user sees.
        if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
            label != nullptr && label->text().empty() && view.child_count() == 0)
            return;
        const auto box = root_rect(view);
        extent.left = std::min(extent.left, box.left);
        extent.top = std::min(extent.top, box.top);
        extent.right = std::max(extent.right, box.right);
        extent.bottom = std::max(extent.bottom, box.bottom);
        if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view)) {
            for (const auto& line : label->cached_line_boxes()) {
                const auto run = root_point(view, line.left, line.top);
                extent.left = std::min(extent.left, run.x);
                extent.top = std::min(extent.top, run.y);
                extent.right = std::max(extent.right, run.x + line.width);
                extent.bottom = std::max(extent.bottom, run.y + line.height);
            }
        }
        for (std::size_t index = 0; index < view.child_count(); ++index)
            walk(*view.child_at(index));
    };
    for (std::size_t index = 0; index < control.child_count(); ++index)
        walk(*control.child_at(index));
    return extent;
}

std::string describe_control(const View& view) {
    const auto box = root_rect(view);
    std::ostringstream out;
    out << (view.id().empty() ? std::string("<anonymous>") : view.id())
        << " root(" << box.left << ',' << box.top << " -> " << box.right << ','
        << box.bottom << ')';
    return out.str();
}

// Sample the whole hit box, not just the middle: the reported failures were all
// at an edge of a painted control.
std::vector<Point> box_probe_points(const View& control) {
    const auto bounds = control.bounds();
    const std::array<std::pair<float, float>, 9> fractions{{
        {0.5f, 0.5f}, {0.02f, 0.06f}, {0.98f, 0.06f}, {0.02f, 0.94f},
        {0.98f, 0.94f}, {0.02f, 0.5f}, {0.98f, 0.5f}, {0.5f, 0.06f},
        {0.5f, 0.94f},
    }};
    std::vector<Point> points;
    points.reserve(fractions.size());
    for (const auto& [fx, fy] : fractions)
        points.push_back(
            root_point(control, bounds.width * fx, bounds.height * fy));
    return points;
}

// Count native "click" dispatches by wrapping the one global the widget bridge
// actually calls. Wrapping the React callback registry instead proves nothing:
// the bridge holds each JS callback directly and never consults that map.
void install_click_dispatch_counter(NativeEditorRig& rig) {
    rig.bridge().load_script(R"js((() => {
      globalThis.__spectrClickDispatchCount = 0;
      if (globalThis.__spectrClickDispatchWrapped) return;
      if (typeof globalThis.__dispatch__ !== 'function')
        throw new Error('widget bridge __dispatch__ global is missing');
      globalThis.__spectrClickDispatchWrapped = true;
      const inner = globalThis.__dispatch__;
      globalThis.__dispatch__ = function (id, event, payload) {
        if (event === 'click')
          globalThis.__spectrClickDispatchCount =
            (globalThis.__spectrClickDispatchCount || 0) + 1;
        return inner.call(this, id, event, payload);
      };
    })();)js", "spectr-native-click-dispatch-counter");
}

std::string js_string(std::string_view value);

// The bridge has no evaluate-with-result seam, so read the counter back the way
// the rest of this file reads runtime state: throw it and parse the message.
int click_dispatch_count(NativeEditorRig& rig) {
    try {
        rig.bridge().load_script(
            "throw new Error('CLICKS:' + globalThis.__spectrClickDispatchCount);",
            "spectr-native-click-dispatch-read");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        const auto marker = message.find("CLICKS:");
        if (marker != std::string::npos)
            return std::atoi(message.c_str() + marker + 7);
    }
    FAIL("click dispatch counter was not readable");
    return -1;
}

std::string runtime_string(NativeEditorRig& rig, std::string_view expression,
                           std::string_view label) {
    const std::string marker = "SPECTR_RUNTIME_VALUE:";
    try {
        rig.bridge().load_script(
            "throw new Error(" + js_string(marker) + " + String("
                + std::string(expression) + "));",
            std::string(label));
    } catch (const std::exception& error) {
        const std::string message = error.what();
        const auto offset = message.find(marker);
        if (offset != std::string::npos)
            return message.substr(offset + marker.size());
    }
    FAIL("runtime string was not readable for " << label);
    return {};
}

// The responsive layer refuses to write a non-finite box and records it. An
// empty list is the contract: the bridge coerces bad geometry silently (a
// mistyped metrics key snaps a control to 0, to its flow position, or to
// auto-size), so this is the only signal that the layout arithmetic held.
void require_no_rejected_layout_boxes(NativeEditorRig& rig) {
    try {
        rig.bridge().load_script(
            "if ((globalThis.__spectrResponsiveLayoutRejects__ || []).length)"
            " throw new Error('responsive layout rejected non-finite boxes: '"
            " + JSON.stringify(globalThis.__spectrResponsiveLayoutRejects__));",
            "spectr-native-layout-reject-contract");
    } catch (const std::exception& error) {
        FAIL(error.what());
    }
}

void native_click_found_label(NativeEditorRig& rig,
                              const pulp::view::Label* label) {
    REQUIRE(label != nullptr);
    auto* click_target = const_cast<View*>(static_cast<const View*>(label));
    while (click_target != nullptr && !click_target->on_click)
        click_target = click_target->parent();
    REQUIRE(click_target != nullptr);
    const auto bounds = click_target->bounds();
    REQUIRE(bounds.width > 0.0f);
    REQUIRE(bounds.height > 0.0f);
    const auto point = root_point(*click_target,
                                  bounds.width * 0.5f,
                                  bounds.height * 0.5f);
    // State-atlas popup replay may retain a presentational label from the
    // prior generation while native hit testing resolves the live popup row.
    // Both are valid DOM-style targets as long as the resolved hit chain has
    // the click owner; assert that contract instead of pointer identity.
    auto* hit = rig.root->hit_test(point);
    REQUIRE(hit != nullptr);
    auto* hit_click_target = hit;
    while (hit_click_target != nullptr && !hit_click_target->on_click)
        hit_click_target = hit_click_target->parent();
    REQUIRE(hit_click_target != nullptr);
    rig.root->simulate_click(point);
    settle(rig.clock, 12);
}

void native_click_label(NativeEditorRig& rig, std::string_view text) {
    INFO("native_click_label text := " << text);
    native_click_found_label(rig, find_label(*rig.root, text));
}

void native_click_label_prefix(NativeEditorRig& rig, std::string_view prefix) {
    INFO("native_click_label_prefix prefix := " << prefix);
    native_click_found_label(rig, find_label_prefix(*rig.root, prefix));
}

void feed_tone(NativeEditorRig& rig) {
    constexpr int block = 256;
    constexpr double sample_rate = 48000.0;
    std::vector<float> in0(block), in1(block), out0(block), out1(block);
    const float* inputs[2]{in0.data(), in1.data()};
    float* outputs[2]{out0.data(), out1.data()};
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::format::ProcessContext context;
    context.sample_rate = sample_rate;
    context.num_samples = block;
    for (int chunk = 0; chunk < 96; ++chunk) {
        for (int sample = 0; sample < block; ++sample) {
            const auto index = chunk * block + sample;
            const auto value = static_cast<float>(std::sin(
                2.0 * 3.14159265358979323846 * 1000.0 * index / sample_rate));
            in0[sample] = value;
            in1[sample] = value;
        }
        pulp::audio::BufferView<const float> input(inputs, 2, block);
        pulp::audio::BufferView<float> output(outputs, 2, block);
        rig.processor.process(output, input, midi_in, midi_out, context);
        rig.clock.tick(1.0f / 30.0f);
    }
}

std::optional<std::filesystem::path> atlas_directory() {
    const auto* value = std::getenv("SPECTR_NATIVE_STATE_ATLAS_DIR");
    if (value == nullptr || *value == '\0') return std::nullopt;
    std::error_code error;
    std::filesystem::create_directories(value, error);
    REQUIRE_FALSE(error);
    return std::filesystem::path(value);
}

void capture(NativeEditorRig& rig,
             const std::optional<std::filesystem::path>& directory,
             std::string_view name,
             int width = 1320,
             int height = 860) {
    if (!directory) return;
    const auto path = *directory / (std::string(name) + ".png");
    REQUIRE(pulp::view::render_to_file(
        *rig.root, width, height, path.string(), 2.0f,
        pulp::view::ScreenshotBackend::gpu));
}

std::string js_string(std::string_view value) {
    std::string result{"\""};
    for (const char ch : value) {
        if (ch == '\\' || ch == '\"') result.push_back('\\');
        result.push_back(ch);
    }
    result.push_back('\"');
    return result;
}

void activate(NativeEditorRig& rig, std::string_view selector,
              std::string_view event = "click",
              std::string_view event_data = "null") {
    // This test enters through the importer's semantic driver rather than the
    // native pointer dispatcher. Its canonical settle seam drains the Promise
    // jobs and React commits that a real host services after pointer dispatch.
    const auto script = std::string{"(() => { if (!globalThis.__pulpActivateMaterializedElement__("}
        + js_string(selector) + "," + js_string(event) + "," + std::string(event_data)
        + ")) throw new Error('native semantic activation failed: ' + "
        + js_string(selector) + "); "
        + "if (typeof globalThis.__pulpRuntimeSettle__ === 'function') "
          "globalThis.__pulpRuntimeSettle__(8); })();";
    rig.bridge().load_script(script, "spectr-native-state-activation");
    settle(rig.clock);
}

// Spectr's sliders paint their own track and thumb, so they answer a pointer
// press on that track rather than an <input> value change. Press one at a
// fraction of its own measured width and let the widget derive the value the
// way a person dragging it would, instead of asserting a value straight into
// the handler and proving nothing about the control.
std::string slider_press_at(
    double ratio,
    std::string_view selector = "[data-spectr-setting-slider]") {
    return std::string{
        "(() => {"
        " const node = globalThis.__pulpFindMaterializedElement__("}
        + js_string(selector) + ");"
        " const box = node && node.getBoundingClientRect"
        " ? node.getBoundingClientRect() : null;"
        " if (!box || !(box.width > 0)) throw new Error("
        "'slider has no layout box to press: ' + " + js_string(selector) + ");"
        " return { clientX: box.left + box.width * "
        + std::to_string(ratio)
        + ", clientY: box.top + box.height * 0.5, pointerId: 1, button: 0 };"
          " })()";
}

// A pointer at a header knob's centre, @p dy points below it (negative is up),
// optionally with Shift held for the fine drag.
std::string knob_point(std::string_view selector, double dy, bool shift = false) {
    return std::string{
        "(() => {"
        " const node = globalThis.__pulpFindMaterializedElement__("}
        + js_string(selector) + ");"
        " const box = node && node.getBoundingClientRect"
        " ? node.getBoundingClientRect() : null;"
        " if (!box || !(box.width > 0)) throw new Error("
        "'knob has no layout box to press: ' + " + js_string(selector) + ");"
        " return { clientX: box.left + box.width * 0.5, clientY: box.top"
        " + box.height * 0.5 + (" + std::to_string(dy) + "), pointerId: 1,"
        " button: 0, shiftKey: " + (shift ? "true" : "false") + " };"
        " })()";
}

void require_state(NativeEditorRig& rig, std::string_view id) {
    const auto script = std::string{R"js((() => {
      const d = globalThis.__pulpMaterializedMetadataDiagnostics__;
      const expected = )js"} + js_string(id) + R"js(;
      if (!d || d.state_id !== expected || d.layout_node_miss !== 0
          || d.text_node_miss !== 0 || d.text_content_mismatch !== 0
          || d.text_target_miss !== 0 || d.paint_node_miss !== 0
          || d.paint_unsupported !== 0)
        throw new Error('materialized state mismatch ' + expected + ': ' + JSON.stringify(d));
    })();)js";
    rig.bridge().load_script(script, "spectr-native-state-contract");
}

void require_home(NativeEditorRig& rig) {
    require_state(rig, "");
}

void require_app_state(NativeEditorRig& rig, std::string_view expression,
                       std::string_view message) {
    const auto script = std::string{"(() => { const s = globalThis.__spectrTestHooks?.appState?.(); "}
        + "if (!s || !(" + std::string(expression) + ")) throw new Error("
        + js_string(message) + " + ': ' + JSON.stringify(s)); })();";
    rig.bridge().load_script(script, "spectr-native-app-state-contract");
}

// Drive real audio blocks through the processor. The internal LFO's phase only
// advances inside Spectr::process(), so a UI-only settle() leaves the modulator
// frozen: the drawn bank can only move if audio is actually running underneath
// it. Block size and sample rate match NativeEditorRig's prepare() contract.
void feed_audio_blocks(NativeEditorRig& rig, int blocks) {
    constexpr int kBlock = 256;
    constexpr double kSampleRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;
    std::array<float, kBlock> in0{}, in1{}, out0{}, out1{};
    const float* inputs[2]{in0.data(), in1.data()};
    float* outputs[2]{out0.data(), out1.data()};
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::format::ProcessContext context;
    context.sample_rate = kSampleRate;
    context.num_samples = kBlock;
    for (int block = 0; block < blocks; ++block) {
        for (int sample = 0; sample < kBlock; ++sample) {
            const auto index = static_cast<double>(block * kBlock + sample);
            const auto value = static_cast<float>(
                0.5 * std::sin(2.0 * kPi * 1000.0 * index / kSampleRate));
            in0[static_cast<std::size_t>(sample)] = value;
            in1[static_cast<std::size_t>(sample)] = value;
        }
        pulp::audio::BufferView<const float> input(inputs, 2, kBlock);
        pulp::audio::BufferView<float> output(outputs, 2, kBlock);
        rig.processor.process(output, input, midi_in, midi_out, context);
    }
}

// Sample the drawn bank and the canonical target across a span of the
// modulator, leaving the readings in `globalThis.__spectrLfoSamples`.
void sample_modulated_bank(NativeEditorRig& rig, int samples, int blocks_each) {
    rig.bridge().load_script("globalThis.__spectrLfoSamples = [];",
                             "spectr-native-lfo-visual-reset");
    for (int index = 0; index < samples; ++index) {
        feed_audio_blocks(rig, blocks_each);
        settle(rig.clock, 2);
        rig.bridge().load_script(R"js((() => {
          const state = globalThis.__spectrTestHooks?.renderState?.();
          if (!state) throw new Error('native render-state hook missing');
          globalThis.__spectrLfoSamples.push({
            drawn: Array.from(state.gains).slice(0, 8),
            canonical: Array.from(state.targetGains).slice(0, 8),
          });
        })();)js", "spectr-native-lfo-visual-sample");
    }
}

void require_runtime_contract(NativeEditorRig& rig,
                              std::string_view expression,
                              std::string_view message) {
    // Report WHY, not just that it mismatched. An absent receipt and a wrong
    // receipt read identically as "undefined" through JSON.stringify, and they
    // have opposite causes: the first means the resize hook was never invoked
    // (the guarded call in publish_native_layout_ is a silent no-op when the
    // symbol is missing), the second means the layout pass produced the wrong
    // numbers. Carry the hook's typeof and the runtime's own rejected-box list
    // so the failure names its own cause.
    const auto script = std::string{"(() => { if (!("}
        + std::string(expression) + ")) throw new Error("
        + js_string(message) + " + ': ' + JSON.stringify("
        + "globalThis.__spectrResponsiveLayoutReceipt__)"
        + " + ' hook=' + (typeof globalThis.__spectrResizeNativeEditor)"
        + " + ' rejects=' + JSON.stringify("
        + "globalThis.__spectrResponsiveLayoutRejects__)); })();";
    rig.bridge().load_script(script, "spectr-native-responsive-contract");
}


// Wait for a runtime predicate rather than assuming a fixed frame budget.
// A bare `settle(clock, N)` encodes ONE engine's commit latency: QuickJS is an
// interpreter, and measurably needs 2-4x more host frames than JIT-compiled JSC
// to finish the same React commit (16 and 32 frames fail, 64 pass). A budget
// tuned under JSC therefore fails under QuickJS even though the element does
// mount, which reads as a product defect and is not one. Polling keeps the
// assertion about BEHAVIOUR - does it commit - instead of about speed, and it
// also removes the pre-existing race this call site already warned about on a
// heavily loaded host. Raising the constant would have hidden both.
void settle_until_contract(NativeEditorRig& rig,
                           std::string_view expression,
                           std::string_view message,
                           int max_frames = 240,
                           int poll_frames = 8) {
    for (int waited = 0; waited + poll_frames <= max_frames; waited += poll_frames) {
        settle(rig.clock, poll_frames);
        try {
            require_runtime_contract(rig, expression, message);
            return;
        } catch (const std::exception&) {
            // Not committed yet; keep servicing host frames until the budget ends.
        }
    }
    // Budget exhausted - run once more unguarded so the real diagnostic surfaces.
    require_runtime_contract(rig, expression, message);
}


// Re-issue an activation until its EFFECT is observable.
//
// settle_until_contract alone assumes the click landed and only the React
// commit is pending. That is not the failure mode here. The rename-start
// control mounts unconditionally, so waiting for its PRESENCE proves nothing
// about the row-selection commit that has to land first; a click delivered
// against the pre-selection render runs the handler on a row that is about to
// be replaced, the input never mounts, and no amount of further waiting can
// recover it -- the budget just expires. Observed ~1 run in 3.
//
// The handler is idempotent (onClick={() => setEditName(true)}), so driving it
// again is safe and is the only thing that actually recovers.
void activate_until_contract(NativeEditorRig& rig, std::string_view selector,
                             std::string_view expression,
                             std::string_view message, int attempts = 4) {
    for (int attempt = 0; attempt + 1 < attempts; ++attempt) {
        activate(rig, selector);
        try {
            settle_until_contract(rig, expression, message, 64, 8);
            return;
        } catch (const std::exception&) {
            // Effect not observable yet; the click most likely raced the commit
            // that owns its target. Fall through and drive it again.
        }
    }
    // Final attempt unguarded, on the full budget, so the real diagnostic
    // surfaces rather than a generic "retries exhausted".
    activate(rig, selector);
    settle_until_contract(rig, expression, message);
}

// C++-side counterpart to settle_until_contract: wait for an OBSERVABLE EFFECT
// instead of assuming one host frame is enough. Driving a DOM event and then
// asserting processor state on the next line assumes the React commit and the
// resulting processor mutation both land synchronously; they do not. Under
// JIT-compiled JSC that assumption held often enough to look deterministic.
template <typename Predicate>
void settle_until(NativeEditorRig& rig, Predicate&& predicate,
                  int max_frames = 240, int poll_frames = 8) {
    for (int waited = 0; waited + poll_frames <= max_frames; waited += poll_frames) {
        if (predicate()) return;
        settle(rig.clock, poll_frames);
    }
}

std::vector<Point> snapshot_hit_points(const View& button,
                                       std::string_view text,
                                       bool capture_button) {
    const auto bounds = button.bounds();
    REQUIRE(bounds.width >= 34.0f);
    REQUIRE(bounds.height >= 24.0f);
    std::vector<Point> points;
    points.reserve(7);

    if (capture_button) {
        const auto* dot = find_sized_descendant(button, 6.0f, 6.0f);
        REQUIRE(dot != nullptr);
        REQUIRE(dot->pointer_events() == View::PointerEvents::none);
        points.push_back(root_point(*dot,
            dot->bounds().width * 0.5f, dot->bounds().height * 0.5f));
        const auto* label = find_label(button, text);
        REQUIRE(label != nullptr);
        INFO("snapshot button " << button.id() << " glyph " << text);
        if (!label->cached_line_boxes().empty()) {
            const auto& line = label->cached_line_boxes().front();
            points.push_back(root_point(*label,
                line.left + line.width * 0.5f, line.top + line.height * 0.5f));
        } else {
            // A Settings ScrollView reparent can retire cached glyph runs on
            // the retained home button while its live bounds remain valid.
            // Sample the label bounds in that lifecycle case; the surrounding
            // seven-point matrix still proves the real button hit target.
            REQUIRE(label->bounds().width > 0.0f);
            REQUIRE(label->bounds().height > 0.0f);
            points.push_back(root_point(*label,
                label->bounds().width * 0.5f, label->bounds().height * 0.5f));
        }
    } else {
        const auto* label = find_label(button, text);
        REQUIRE(label != nullptr);
        INFO("snapshot button " << button.id() << " glyph " << text);
        REQUIRE(label->bounds().width > 0.0f);
        REQUIRE(label->bounds().height > 0.0f);
        // The exact captured glyph run is "▸ A/B". Sample the visible icon
        // and final slot glyph separately, rather than blank right padding.
        points.push_back(root_point(*label,
            label->cached_line_boxes().empty()
              ? label->bounds().width * 0.25f
              : label->cached_line_boxes().front().left + 3.0f,
            label->cached_line_boxes().empty()
              ? label->bounds().height * 0.5f
              : label->cached_line_boxes().front().top
                + label->cached_line_boxes().front().height * 0.5f));
        points.push_back(root_point(*label,
            label->cached_line_boxes().empty()
              ? label->bounds().width * 0.75f
              : label->cached_line_boxes().front().left
                + label->cached_line_boxes().front().width - 3.0f,
            label->cached_line_boxes().empty()
              ? label->bounds().height * 0.5f
              : label->cached_line_boxes().front().top
                + label->cached_line_boxes().front().height * 0.5f));
    }

    points.push_back(root_point(button, bounds.width * 0.5f, bounds.height * 0.5f));
    constexpr float inset = 1.5f;
    points.push_back(root_point(button, inset, inset));
    points.push_back(root_point(button, bounds.width - inset, inset));
    points.push_back(root_point(button, inset, bounds.height - inset));
    points.push_back(root_point(button, bounds.width - inset, bounds.height - inset));
    return points;
}

void click_each_point_exactly_once(NativeEditorRig& rig, View& button,
                                   std::string_view glyph_text,
                                   bool capture_button) {
    REQUIRE(button.pointer_events() == View::PointerEvents::box_only);
    rig.root->layout_children();
    const auto button_id = button.id();
    const auto points = snapshot_hit_points(button, glyph_text, capture_button);
    REQUIRE(points.size() == 7);
    for (std::size_t index = 0; index < points.size(); ++index) {
        // Each click publishes state and may synchronously replace the React
        // host view. Never retain or dereference the previous generation.
        auto* current = rig.bridge().widget(button_id);
        REQUIRE(current != nullptr);
        INFO("semantic point index " << index << " on " << button_id);
        auto* target = rig.root->hit_test(points[index]);
        REQUIRE(target != nullptr);
        REQUIRE(target == current);
        const auto revision = rig.processor.native_editor_revision();
        rig.root->simulate_click(points[index]);
        settle(rig.clock, 12);
        REQUIRE(rig.processor.native_editor_revision() == revision + 1);
    }
}

std::vector<std::uint8_t> corrupt_first_gain(std::span<const std::uint8_t> bytes) {
    std::string json(bytes.begin(), bytes.end());
    const auto key = json.find("\"band_gain\"");
    REQUIRE(key != std::string::npos);
    const auto open = json.find('[', key);
    REQUIRE(open != std::string::npos);
    const auto value = open + 1;
    const auto end = json.find_first_of(",]", value);
    REQUIRE(end != std::string::npos);
    json.replace(value, end - value, "1e999");
    return {json.begin(), json.end()};
}

std::vector<std::uint8_t> corrupt_first_pattern_gain(
    std::span<const std::uint8_t> bytes) {
    std::string json(bytes.begin(), bytes.end());
    const auto key = json.find("\\\"gain_db\\\"");
    REQUIRE(key != std::string::npos);
    const auto open = json.find('[', key);
    REQUIRE(open != std::string::npos);
    const auto value = open + 1;
    const auto end = json.find_first_of(",]", value);
    REQUIRE(end != std::string::npos);
    json.replace(value, end - value, "1e999");
    return {json.begin(), json.end()};
}

} // namespace

TEST_CASE("native editor advertises proportional host-corner resizing",
          "[native-n1][resize]") {
    pulp::state::StateStore store;
    spectr::Spectr processor;
    processor.set_state_store(&store);
    processor.define_parameters(store);

    const auto size = processor.view_size();
    // Proportional-only contract: the editor opens at the AUTHORED box so the
    // pin renders at scale 1.0 (layout exactly as designed, type at its
    // authored size), and the policy stays Automatic so
    // should_pin_design_viewport() engages in every format. The previous
    // contract opened at 990x645 with viewport_policy=Responsive, which
    // short-circuited the pin and made the root reflow at the host size.
    CHECK(size.preferred_width == 990);
    CHECK(size.preferred_height == 645);
    CHECK(size.min_width == 792);
    CHECK(size.min_height == 516);
    CHECK(size.max_width == 2640);
    CHECK(size.max_height == 1720);
    CHECK(size.aspect_ratio == Catch::Approx(1320.0 / 860.0));
    CHECK(size.design_width == 1320);
    CHECK(size.design_height == 860);
    CHECK(size.viewport_policy == pulp::format::ViewportPolicy::Automatic);
    CHECK(pulp::format::should_pin_design_viewport(size));
    CHECK(pulp::format::should_lock_view_aspect(size));

    NativeEditorRig rig;
    const auto directory = atlas_directory();
    struct ResizeCase {
        int width;
        int height;
        std::string_view image;
    };
    // PROPORTIONAL-ONLY CONTRACT.
    //
    // The layout receipt is asserted to be BYTE-FOR-BYTE THE SAME at every host
    // size, and to always describe the authored 1320x860 box. That invariance is
    // the assertion: it is what "even proportional scaling, no cropping, no
    // reflow" means at the layout layer. The host varies from 792x516 to
    // 2640x1720 across these cases; the layout does not move.
    //
    // This deliberately replaces a per-size expectation table
    // (compact-two-row/authored/expanded, bottom_height 96 vs 56, graph_height
    // tracking the host). That table encoded the OPPOSITE contract: the layout
    // MODE changed with the window, so the bottom rail switched between one and
    // two rows and the brand subtitle disappeared as you dragged. Every "the
    // layout is different at size X" report traced back to that reflow. Under a
    // pinned design viewport there is nothing to reflow — the host applies one
    // uniform scale to a constant layout — so a receipt that still varied with
    // the host size would now be evidence of a BUG, not of correctness.
    for (const auto& sample : std::array<ResizeCase, 4>{
             ResizeCase{792, 516, "minimum-home"},
             ResizeCase{990, 645, "preferred-home"},
             ResizeCase{1320, 860, "authored-home"},
             ResizeCase{2640, 1720, "enlarged-home"},
         }) {
        rig.resize(sample.width, sample.height);
        require_runtime_contract(
            rig,
            "(() => { const r = globalThis.__spectrResponsiveLayoutReceipt__; "
            "return r && r.schema === 'spectr-responsive-layout-v1'"
            " && r.width === 1320 && r.height === 860"
            " && r.mode === 'authored'"
            " && r.design_transform === 'none' && r.top_height === 44"
            " && r.bottom_height === 56"
            " && r.graph_height === 760"
            " && r.focus_order.length > 0"
            " && r.typography_scale === 1"
            "; })()",
            "layout moved with the host size under a pinned viewport");
        capture(rig, directory, sample.image, sample.width, sample.height);
    }

    rig.resize(792, 516);
    for (const auto text : {"CLEAR", "SCULPT ▾", "PEAK ▾"}) {
        CAPTURE(text);
        const auto* label = find_label(*rig.root, text);
        REQUIRE(label != nullptr);
        CHECK(label->font_size() >= 10.0f);
        CHECK_FALSE(label->cached_line_boxes().empty());
        auto* target = label->parent();
        while (target != nullptr && !target->on_click) target = target->parent();
        REQUIRE(target != nullptr);
        CHECK(target->bounds().height >= 24.0f);
    }
    const auto* selected_preset = find_label(*rig.root, "PRESET… ▾");
    if (selected_preset == nullptr)
        selected_preset = find_label(*rig.root, "FLAT ▾");
    if (selected_preset == nullptr)
        selected_preset = find_label(*rig.root, "PRESETS ▾");
    REQUIRE(selected_preset != nullptr);
    CHECK(selected_preset->font_size() >= 10.0f);
    auto* preset_target = selected_preset->parent();
    while (preset_target != nullptr && !preset_target->on_click)
        preset_target = preset_target->parent();
    REQUIRE(preset_target != nullptr);
    CHECK(preset_target->bounds().height >= 24.0f);

    activate(rig, "[data-spectr-settings-open]");
    require_runtime_contract(
        rig,
        // The panel remains pinned to the authored viewport. The appended
        // Feedback and exact build-information groups make the live content
        // genuinely taller, so the native ScrollView exposes that real extent.
        //
        // The invariant here is the AUTHORED BOX (520x679) plus a real,
        // child-derived, scroll-reachable extent -- the numeric band on
        // content_height is a sanity window around that extent, not a pinned
        // value. It widened by one row's worth when MODULATION gained the
        // Viewport switch; a row legitimately added to the panel moves this
        // number, and the assertions either side of it are what actually hold
        // the contract.
        //
        // THE NUMBER TRACKS THE SETTINGS GROUP COUNT. It moved 1500.98 ->
        // 1660.98 when the LATENCY group was added, and 1660.98 -> 1820.58
        // when Appearance gained the Modulation look row -- each exactly one
        // group's 160px, with the authored box, scroll reachability and skin
        // all unchanged -- and 1820.58 -> 1960.58 when FEEDBACK gained the
        // plug-in-only "Keyboard shortcuts in DAW" row, whose label wraps,
        // and 1960.58 -> 1846.58 when the MOTION group (the hidden LIVE /
        // PRECISION choice) left, and 1846.58 -> 1960.58 when the FREEZE group
        // (Hold length) arrived, and 1960.58 -> 1846.58 when it left again for
        // the header's LENGTH control, and 1846.58 -> 2044.58 when Appearance
        // gained Display (BARS / RESPONSE / BOTH, out of the header) and
        // Structure gained Range. If you add a group and this fails, that is the window
        // doing its job, not a bug to route around.
        //
        // Re-CENTRE it on the new extent rather than raising the ceiling. A
        // window whose top is pushed up every time the panel grows passes
        // forever and catches nothing; keeping the same +/-80 margin either
        // side is what leaves it able to fail in BOTH directions, which is the
        // only reason to have a numeric band here at all.
        "(() => { const s = globalThis.__spectrResponsiveLayoutReceipt__?.settings; "
        "return s && s.width === 520 && s.height === 679"
        " && s.content_height > 1965 && s.content_height < 2125"
        " && s.scroll_reachable === true"
        " && s.native_scroll_view === true"
        " && s.authored_skin === true; })()",
        "settings panel did not keep its authored geometry under the pin");
    capture(rig, directory, "minimum-settings", 792, 516);
    // Title and close action are separate native targets so the close glyph has
    // its own hover/pressed hit state without reshaping the heading text.
    const auto* settings_title = find_label(*rig.root, "SETTINGS");
    REQUIRE(settings_title != nullptr);
    // The title lives in the fixed header; the body is the sole native
    // ScrollView and is therefore not an ancestor of that label.
    std::function<pulp::view::ScrollView*(View&)> find_settings_scroll =
        [&](View& node) -> pulp::view::ScrollView* {
          if (auto* scroll = dynamic_cast<pulp::view::ScrollView*>(&node))
              return scroll;
          for (std::size_t index = 0; index < node.child_count(); ++index)
              if (auto* found = find_settings_scroll(*node.child_at(index)))
                  return found;
          return nullptr;
        };
    auto* scroll_view = find_settings_scroll(*rig.root);
    REQUIRE(scroll_view != nullptr);
    // The fixed shell owns the skin; the body ScrollView intentionally owns
    // scrolling/content extent and need not duplicate the shell background or
    // border.
    CHECK(scroll_view->content_size().height
          > scroll_view->bounds().height + 0.5f);
    // The fixed shell reserves its header/tabs; the body viewport is the
    // remaining authored height (531px in the current 679px shell). The band
    // tracks typography drift while still catching a collapsed or unreserved
    // viewport.
    CHECK(scroll_view->bounds().height == Catch::Approx(531.0f).margin(3.0f));
    scroll_view->set_scroll(0.0f, 728.0f);
    settle(rig.clock, 4);
    CHECK(scroll_view->scroll_y() > 0.0f);
    const auto* rulers_label = find_label(*rig.root, "Rulers");
    REQUIRE(rulers_label != nullptr);
    const auto rulers_point = root_point(
        *rulers_label, rulers_label->bounds().width * 0.5f,
        rulers_label->bounds().height * 0.5f);
    // After scrolling, descendants may legitimately have a negative root-space
    // y while remaining reachable inside the body viewport.
    CHECK(std::isfinite(rulers_point.y));
    capture(rig, directory, "minimum-settings-bottom", 792, 516);
}

TEST_CASE("native settings command and minimap cursors reach the shipping runtime",
          "[native-n1][state-parity][commands][cursor]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    require_runtime_contract(
        rig,
        "globalThis.__spectrBandCountCenteringReceipt__?.trigger?.top === 3.5"
        " && globalThis.__spectrBandCountCenteringReceipt__.trigger.height === 20"
        " && Math.abs(globalThis.__spectrBandCountCenteringReceipt__.trigger.left"
        " - 9.484375) < 0.001",
        "band trigger text was not optically centered");
    const auto* trigger_label = find_label(*rig.root, "32 BANDS ▾");
    REQUIRE(trigger_label != nullptr);
    CAPTURE(trigger_label->id(), trigger_label->parent()->id());
    REQUIRE(trigger_label->cached_line_boxes().size() == 1);
    CHECK(trigger_label->cached_line_boxes().front().left
          == Catch::Approx(9.484375f).margin(0.01f));
    CHECK(trigger_label->cached_line_boxes().front().top
          == Catch::Approx(3.5f).margin(0.01f));

    REQUIRE(static_cast<bool>(rig.root->on_global_key));
    const auto comma = static_cast<pulp::view::KeyCode>(',');
    CHECK_FALSE(rig.root->on_global_key({
        .key = comma, .modifiers = pulp::view::kModNone, .is_down = true}));
    require_home(rig);
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma, .modifiers = primary_modifier, .is_down = true}));
    settle(rig.clock, 16);
    require_state(rig, "settings");
    activate(rig, "[data-spectr-settings-close]");
    require_home(rig);

    // Spectral resolution remains a diagnostic bridge contract. It must not
    // leak into the normal product chrome as a cryptic RES counter.
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-spectr-resolution]')"
        " && !Array.from(document.querySelectorAll('span'))"
        ".some(node => /^RES\\s+\\d+\\/\\d+$/.test(node.textContent.trim()))",
        "diagnostic spectral resolution leaked into product chrome");

    View* surface = nullptr;
    const std::function<void(View&)> find_surface = [&](View& candidate) {
        if (candidate.cursor() == View::CursorStyle::crosshair
            && candidate.on_dom_pointer_event) {
            REQUIRE(surface == nullptr);
            surface = &candidate;
        }
        for (std::size_t index = 0; index < candidate.child_count(); ++index)
            find_surface(*candidate.child_at(index));
    };
    find_surface(*rig.root);
    REQUIRE(surface != nullptr);
    CHECK(surface->cursor() == View::CursorStyle::crosshair);
    // `buttons` is the held-button mask the pointer event carries. A hover is
    // buttons:0; only a move that continues a press is buttons:1. Passing 1 for
    // a plain move asserts the drag cursor and proves nothing about hover.
    const auto dispatch_minimap = [&](std::string_view event,
                                      std::string_view hit,
                                      int buttons) {
        const auto script = std::string{R"js((() => {
          const selector = '[data-spectr-filter-surface]';
          const surface = document.querySelector(selector);
          if (!surface) throw new Error('filter surface missing');
          const desired = )js"} + js_string(hit) + R"js(;
          const state = globalThis.__spectrTestHooks?.renderState?.();
          if (!state) throw new Error('filter state missing');
          const fullMin = Math.log10(20);
          const fullSpan = Math.log10(20000) - fullMin;
          const innerX = 56, innerWidth = surface.clientWidth - 112;
          const left = (state.view.lmin - fullMin) / fullSpan;
          const right = (state.view.lmax - fullMin) / fullSpan;
          const windowX = innerX + (left + right) * 0.5 * innerWidth;
          const miniY = Array.from({length: surface.clientHeight}, (_, y) => y)
            .find(y => globalThis.__spectrTestHooks.minimapHit(windowX, y) === 'window');
          if (!Number.isFinite(miniY)) throw new Error('minimap y missing');
          const x = desired === 'left' ? innerX + left * innerWidth
            : desired === 'right' ? innerX + right * innerWidth
            : desired === 'track' ? innerX + left * 0.45 * innerWidth
            : windowX;
          const point = {x, y: miniY};
          if (globalThis.__spectrTestHooks.minimapHit(point.x, point.y) !== desired)
            throw new Error('minimap hit missing: ' + desired);
          if (!globalThis.__pulpActivateMaterializedElement__(selector, )js"
            + js_string(event) + R"js(, {
                clientX: point.x, clientY: point.y, pointerId: 71,
                button: 0, buttons: )js" + std::to_string(buttons) + R"js(
              })) throw new Error('minimap cursor activation failed');
          if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
            globalThis.__pulpRuntimeSettle__(4);
        })();)js";
        rig.bridge().load_script(script, "spectr-native-minimap-cursor");
        settle(rig.clock, 4);
    };

    // Hovering an edge with no button held must show the resize affordance.
    dispatch_minimap("pointermove", "left", 0);
    CHECK(surface->cursor() == View::CursorStyle::horizontal_resize);
    dispatch_minimap("pointermove", "right", 0);
    CHECK(surface->cursor() == View::CursorStyle::horizontal_resize);
    // Hovering the window body offers the grab affordance before any press.
    dispatch_minimap("pointermove", "window", 0);
    CHECK(surface->cursor() == View::CursorStyle::grab);
    dispatch_minimap("pointerdown", "window", 1);
    CHECK(surface->cursor() == View::CursorStyle::grabbing);
    dispatch_minimap("pointermove", "window", 1);
    CHECK(surface->cursor() == View::CursorStyle::grabbing);
    dispatch_minimap("pointerup", "window", 0);
    CHECK(surface->cursor() == View::CursorStyle::grab);
    activate(rig, "[data-spectr-filter-surface]", "pointermove",
             R"js({clientX:660,clientY:430,pointerId:72,button:0,buttons:0})js");
    CHECK(surface->cursor() == View::CursorStyle::crosshair);

    rig.bridge().load_script(R"js((() => {
      const selector = '[data-spectr-filter-surface]';
      const surface = document.querySelector(selector);
      const hooks = globalThis.__spectrTestHooks;
      const fire = (type, x, y, pointerId, buttons) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
          clientX: x, clientY: y, pointerId, button: 0, buttons
        })) throw new Error('minimap perf activation failed: ' + type);
      };
      const gesture = (hit, delta, pointerId) => {
        const before = hooks.renderState();
        const fullMin = Math.log10(20);
        const fullSpan = Math.log10(20000) - fullMin;
        const innerX = 56, innerWidth = surface.clientWidth - 112;
        const left = (before.view.lmin - fullMin) / fullSpan;
        const right = (before.view.lmax - fullMin) / fullSpan;
        const x = hit === 'left' ? innerX + left * innerWidth
          : hit === 'right' ? innerX + right * innerWidth
          : innerX + (left + right) * 0.5 * innerWidth;
        const y = Array.from({length: surface.clientHeight}, (_, candidate) => candidate)
          .find(candidate => hooks.minimapHit(x, candidate) === hit);
        if (!Number.isFinite(y)) throw new Error('minimap perf hit missing: ' + hit);
        const postCount = globalThis.__spectrNativeDispatchTrace.filter(
          entry => entry.type === 'processing_state_set').length;
        fire('pointerdown', x, y, pointerId, 1);
        fire('pointermove', x + delta, y, pointerId, 1);
        if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
          globalThis.__pulpRuntimeSettle__(2);
        const during = hooks.renderState();
        if (during.view.lmin === before.view.lmin
            && during.view.lmax === before.view.lmax)
          throw new Error(hit + ' did not update the live viewport');
        if (during.reactView.lmin !== before.reactView.lmin
            || during.reactView.lmax !== before.reactView.lmax)
          throw new Error(hit + ' reconciled React before release');
        if (globalThis.__spectrNativeDispatchTrace.filter(
              entry => entry.type === 'processing_state_set').length <= postCount)
          throw new Error(hit + ' did not publish native viewport state');
        fire('pointerup', x + delta, y, pointerId, 0);
        if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
          globalThis.__pulpRuntimeSettle__(4);
        const released = hooks.renderState();
        if (Math.abs(released.reactView.lmin - released.view.lmin) > 1e-9
            || Math.abs(released.reactView.lmax - released.view.lmax) > 1e-9)
          throw new Error(hit + ' release lost the final React viewport');
      };
      gesture('left', 40, 74);
      gesture('right', -40, 75);
      gesture('window', 35, 76);
    })();)js", "spectr-native-minimap-react-budget");
    settle(rig.clock, 4);

    // Pointer dragging the minimap window uses the same rigid endpoint clamp
    // as horizontal trackpad panning. Repeated motion beyond an endpoint must
    // be absorbed rather than moving the opposite trim or changing the span.
    rig.bridge().load_script(R"js((() => {
      const selector = '[data-spectr-filter-surface]';
      const surface = document.querySelector(selector);
      const hooks = globalThis.__spectrTestHooks;
      const fullMin = Math.log10(20), fullMax = Math.log10(20000);
      const fullSpan = fullMax - fullMin;
      const innerX = 56, innerWidth = surface.clientWidth - 112;
      const near = (a, b) => Math.abs(a - b) < 1e-9;
      const fire = (type, x, y, pointerId, buttons) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
          clientX: x, clientY: y, pointerId, button: 0, buttons
        })) throw new Error('minimap endpoint drag activation failed: ' + type);
      };
      const dragToEndpoint = (direction, pointerId) => {
        const before = hooks.renderState();
        const span = before.view.lmax - before.view.lmin;
        const left = (before.view.lmin - fullMin) / fullSpan;
        const right = (before.view.lmax - fullMin) / fullSpan;
        const x = innerX + (left + right) * 0.5 * innerWidth;
        const y = Array.from({length: surface.clientHeight}, (_, candidate) => candidate)
          .find(candidate => hooks.minimapHit(x, candidate) === 'window');
        if (!Number.isFinite(y))
          throw new Error('minimap endpoint window hit missing');
        const firstDelta = direction * 100000;
        fire('pointerdown', x, y, pointerId, 1);
        fire('pointermove', x + firstDelta, y, pointerId, 1);
        const atEndpoint = hooks.renderState();
        if ((direction > 0 && !near(atEndpoint.view.lmax, fullMax))
            || (direction < 0 && !near(atEndpoint.view.lmin, fullMin))
            || !near(atEndpoint.view.lmax - atEndpoint.view.lmin, span))
          throw new Error('pointer drag changed viewport width at endpoint');
        fire('pointermove', x + firstDelta * 2, y, pointerId, 1);
        const held = hooks.renderState();
        if (!near(held.view.lmin, atEndpoint.view.lmin)
            || !near(held.view.lmax, atEndpoint.view.lmax))
          throw new Error('pointer endpoint overscroll bounced opposite trim');
        fire('pointerup', x + firstDelta * 2, y, pointerId, 0);
        if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
          globalThis.__pulpRuntimeSettle__(4);
        const released = hooks.renderState();
        if (!near(released.reactView.lmin, released.view.lmin)
            || !near(released.reactView.lmax, released.view.lmax))
          throw new Error('pointer endpoint release lost the final viewport');
      };
      dragToEndpoint(1, 77);
      dragToEndpoint(-1, 78);
    })();)js", "spectr-native-minimap-pointer-endpoint-invariant");
    settle(rig.clock, 4);

    // Product-acceptance gate: a horizontal two-finger gesture pans the
    // selected minimap window as one rigid body. Endpoint overscroll must be
    // absorbed instead of resizing the opposite trim.
    rig.bridge().load_script(R"js((() => {
      const selector = '[data-spectr-filter-surface]';
      const surface = document.querySelector(selector);
      const hooks = globalThis.__spectrTestHooks;
      const fullMin = Math.log10(20), fullMax = Math.log10(20000);
      const x = surface.clientWidth * 0.5;
      const wheel = (deltaX) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, 'wheel', {
          clientX: x, clientY: surface.clientHeight * 0.5,
          deltaX, deltaY: 0, preventDefault() {}
        })) throw new Error('horizontal minimap wheel activation failed');
      };
      const spanOf = (state) => state.view.lmax - state.view.lmin;
      const near = (a, b) => Math.abs(a - b) < 1e-9;

      const initialSpan = spanOf(hooks.renderState());
      wheel(100000);
      const atRight = hooks.renderState();
      if (!near(atRight.view.lmax, fullMax)
          || !near(spanOf(atRight), initialSpan))
        throw new Error('right endpoint pan changed viewport width');
      wheel(100000);
      const heldRight = hooks.renderState();
      if (!near(heldRight.view.lmin, atRight.view.lmin)
          || !near(heldRight.view.lmax, atRight.view.lmax))
        throw new Error('right endpoint overscroll bounced opposite trim');

      wheel(-100000);
      const atLeft = hooks.renderState();
      if (!near(atLeft.view.lmin, fullMin)
          || !near(spanOf(atLeft), initialSpan))
        throw new Error('left endpoint pan changed viewport width');
      wheel(-100000);
      const heldLeft = hooks.renderState();
      if (!near(heldLeft.view.lmin, atLeft.view.lmin)
          || !near(heldLeft.view.lmax, atLeft.view.lmax))
        throw new Error('left endpoint overscroll bounced opposite trim');
    })();)js", "spectr-native-minimap-horizontal-endpoint-invariant");
    settle(rig.clock, 4);

    rig.bridge().load_script(R"js((() => {
      const selector = '[data-spectr-filter-surface]';
      const hooks = globalThis.__spectrTestHooks;
      const before = hooks.renderState().reactGains.slice();
      const fire = (type, x, y, buttons) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
          clientX: x, clientY: y, pointerId: 73, button: 0, buttons
        })) throw new Error('band drag activation failed: ' + type);
      };
      fire('pointerdown', 660, 430, 1);
      fire('pointermove', 700, 365, 1);
      if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
        globalThis.__pulpRuntimeSettle__(2);
      const during = hooks.renderState();
      if (during.targetGains.every((value, index) => value === before[index]))
        throw new Error('band drag did not update live target');
      if (during.reactGains.some((value, index) => value !== before[index]))
        throw new Error('band drag reconciled React state before release');
      const status = document.querySelector('[data-spectr-status-text]');
      if (!status || !status.textContent.includes('BAND'))
        throw new Error('band drag did not update live hover status');
      fire('pointerup', 700, 365, 0);
      if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
        globalThis.__pulpRuntimeSettle__(4);
      const released = hooks.renderState();
      // The release does not re-render to refresh React's `gains`. That copy
      // is a mirror nothing on screen reads -- the live projection leaves it
      // stale the same way -- and every setter writes it from the ref, so a
      // render here only re-applied the captured document. What must hold is
      // the drawn result, and that no release render happened.
      if (released.targetGains.every((value, index) => value === before[index]))
        throw new Error('band release lost the drawn target');
      if (released.reactGains.some((value, index) => value !== before[index]))
        throw new Error('band release re-rendered React gains');

      // A transient leave between related drag/hover updates must not flash an
      // empty banner, and its stale clear timer must not erase the replacement.
      let repaintSignals = 0;
      window.addEventListener('resize', () => { repaintSignals += 1; });
      fire('pointerleave', 700, 365, 0);
      if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
        globalThis.__pulpRuntimeSettle__(4);
      fire('pointermove', 720, 350, 0);
      if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
        globalThis.__pulpRuntimeSettle__(12);
      if (!status.textContent.includes('BAND'))
        throw new Error('stale status clear erased replacement hover');

      // The materialized test clock intentionally does not advance wall-clock
      // timers. The source contract separately pins the eventual hide's resize
      // invalidation; this executed assertion covers cancellation of the stale
      // clear while the replacement remains immediately truthful.
      if (repaintSignals != 0)
        throw new Error('replacement hover caused an intermediate blank repaint');
    })();)js", "spectr-native-band-drag-react-budget");
    settle(rig.clock, 4);
}

TEST_CASE("native history commands consume Logic undo and redo chords",
          "[native-n1][state-parity][commands][history]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    REQUIRE(static_cast<bool>(rig.root->on_global_key));
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    CHECK(rig.root->on_global_key({
        .key = pulp::view::KeyCode::z,
        .modifiers = primary_modifier,
        .is_down = true,
    }));
    CHECK(rig.root->on_global_key({
        .key = pulp::view::KeyCode::z,
        .modifiers = static_cast<std::uint16_t>(primary_modifier
                                                 | pulp::view::kModShift),
        .is_down = true,
    }));
}

TEST_CASE("native host automation projects through the compact live frame lane",
          "[native-n1][state-parity][host-automation-live]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    rig.bridge().load_script(R"js((() => {
      const hooks = globalThis.__spectrTestHooks;
      const before = hooks?.renderState?.();
      if (!before) throw new Error('native render-state hook missing');
      globalThis.__spectrHostAutomationReactBefore = {
        gains: before.reactGains.slice(),
        view: { ...before.reactView },
      };
    })();)js", "spectr-native-host-automation-live-publish");

    for (std::size_t index = 0; index < 32; ++index) {
        rig.store.set_value(
            spectr::band_gain_param_id(index), static_cast<float>(index) - 16.0f);
        rig.store.set_value(
            spectr::band_mute_param_id(index), index == 7 ? 1.0f : 0.0f);
    }
    const auto [viewport_center, viewport_width] =
        spectr::encode_viewport({220.0f, 8800.0f});
    rig.store.set_value(spectr::kParamViewportCenter, viewport_center);
    rig.store.set_value(spectr::kParamViewportWidth, viewport_width);
    rig.store.set_value(spectr::kParamMotionMode, 1.0f);
    rig.store.set_value(spectr::kParamAnalyzerMode, 2.0f);
    rig.store.set_value(spectr::kParamEditMode, 3.0f);
    rig.store.set_value(spectr::kParamVisualization, 1.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);

    rig.bridge().load_script(R"js((() => {
      const state = globalThis.__spectrTestHooks?.renderState?.();
      const before = globalThis.__spectrHostAutomationReactBefore;
      if (!state || !before) throw new Error('native live-state receipt missing');
      const expected = -16 / 24;
      if (Math.abs(state.targetGains[0] - expected) > 1e-9
          || state.targetGains[7] !== -Infinity)
        throw new Error('compact live-state did not update target gains');
      // Muting remains categorical in target state, and the render projection
      // carries it as the SAME categorical sentinel rather than as a finite
      // gain. The finite band must never be eased.
      //
      // `renderState().gains` is `renderGainsRef`, which is one layer BEFORE
      // the painter's normalisation. The live-state projection writes
      // `state.muted[i] ? -Infinity : clamp(value, -1.02, 1.02)`, and it is
      // `drawBands` that turns a non-finite render gain into the 0 dB line
      // (`Number.isFinite(v) ? clamp(macroAdjustedGain(v, i), -1.02, 1.02)
      // : 0`). Reading the ref and expecting the painter's 0 compared two
      // different layers, and the sentinel is load-bearing at this one: the
      // ease loop keys the mute-collapse animation off `isMuted(rg[i])`, so a
      // 0 here would read as an UNMUTED render gain and re-enter the collapse
      // on the next frame.
      if (Math.abs(state.gains[0] - expected) > 1e-9
          || state.gains[7] !== -Infinity)
        throw new Error('compact live-state did not draw current values directly: gain0=' +
          state.gains[0] + ', gain7=' + state.gains[7] + ', expected=' + expected);
      // The half the sentinel check cannot make on its own: a projection that
      // simply dropped the muted band -- or handed back an unpopulated array --
      // would also not be a finite eased gain. The pre-mute dB has to survive
      // alongside it, because that is what an unmute restores.
      if (state.mutedGainDb.length !== state.gains.length)
        throw new Error('compact live-state truncated the muted-gain record: '
          + state.mutedGainDb.length + ' vs ' + state.gains.length);
      if (!Number.isFinite(state.mutedGainDb[7]))
        throw new Error('compact live-state lost band 7 pre-mute dB: '
          + state.mutedGainDb[7]);
      if (Math.abs(state.view.lmin - Math.log10(220)) > 1e-5
          || Math.abs(state.view.lmax - Math.log10(8800)) > 1e-5)
        throw new Error('compact live-state did not update the viewport');
      if (state.reactGains.some((value, index) => value !== before.gains[index]))
        throw new Error('compact live-state reconciled React gains');
      if (state.reactView.lmin !== before.view.lmin
          || state.reactView.lmax !== before.view.lmax)
        throw new Error('compact live-state reconciled the React viewport');
    })();)js", "spectr-native-host-automation-live-contract");

    // The control for the sentinel assertion above, executed through the
    // product rather than asserted about it.
    //
    // `gains[7] !== -Infinity` is only evidence that the projection honours
    // `state.muted` if the SAME band, through the SAME path, reads finite when
    // it is not muted. A projection that had stopped reading `muted` and
    // hard-wired the sentinel -- or one that produced `-Infinity` for some
    // unrelated reason, an unpopulated array being the obvious one -- passes
    // the assertion above and fails here.
    rig.store.set_value(spectr::band_mute_param_id(7), 0.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);
    rig.bridge().load_script(R"js((() => {
      const state = globalThis.__spectrTestHooks?.renderState?.();
      if (!state) throw new Error('native live-state receipt missing');
      if (!Number.isFinite(state.gains[7]))
        throw new Error('unmuted band 7 still projects the mute sentinel: gain7='
          + state.gains[7]);
      if (state.targetGains[7] !== -9 / 24)
        throw new Error('unmuted band 7 did not restore its target gain: target7='
          + state.targetGains[7]);
    })();)js", "spectr-native-host-automation-live-unmuted-control");
    storage.require_unchanged();
}

TEST_CASE("native host automation compact live frame hydrates mode fields into app state",
          "[native-n1][state-parity][host-automation-live]") {
    // The sibling "compact live frame lane" test above proves the narrowed
    // live-state payload (make_editor_live_state_payload) hydrates band gains
    // and the viewport. It never checks the same narrowed payload's mode
    // fields (analyzer_mode / edit_mode / visualization_mode / motion_mode)
    // against the runtime's own app-state surface, so a regression that drops
    // those four fields from the compact projection -- while leaving gains and
    // viewport intact -- would pass every existing test in this file.
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Confirm the defaults first. Every target value below must differ from
    // its default so a later match can only be explained by the compact
    // live-state payload actually driving the transition, not a coincidental
    // default. Defaults come straight from param_surface.cpp's ParamInfo
    // ranges, not from assumption: motion=Live(0), analyzer=Peak(0),
    // edit=Sculpt(0), visualization=Both(2, the range's declared default is
    // its max, not index 0).
    require_app_state(rig,
        "s.editMode === 'sculpt' && s.analyzerMode === 'peak' "
        "&& s.visualizationMode === 'both' && s.settings "
        "&& s.settings.motionMode === 'live'",
        "expected default edit/analyzer/visualization/motion modes before any "
        "host automation");

    rig.store.set_value(spectr::kParamMotionMode, 1.0f);
    rig.store.set_value(spectr::kParamAnalyzerMode, 2.0f);
    rig.store.set_value(spectr::kParamEditMode, 3.0f);
    rig.store.set_value(spectr::kParamVisualization, 1.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);

    // dispatch_native_message on this tick carries ONLY
    // make_editor_live_state_payload's narrow field set (see
    // src/editor_bridge.cpp) -- no full processing_state_hydrate message is
    // sent on a plain automation tick. A match here can only be explained by
    // that narrowed payload's mode fields reaching app state.
    require_app_state(rig,
        "s.editMode === 'flare' && s.analyzerMode === 'both' "
        "&& s.visualizationMode === 'response' && s.settings "
        "&& s.settings.motionMode === 'precision'",
        "compact live-state did not hydrate edit/analyzer/visualization/motion mode");
    storage.require_unchanged();
}

TEST_CASE("an enabled LFO visibly modulates the drawn bank without moving canonical state",
          "[native-n1][state-parity][modulation-visual]") {
    // Spectr::process() computes the modulated `audible` BandField on the
    // audio thread and hands it to mask_processor_ alone, so the LFO is
    // audible but invisible: the band controls it modulates never redraw.
    // "Applying an LFO to a control animates that control" is the product
    // contract, and the two halves of it are separable -- the drawn value has
    // to move, and canonical state has to stay exactly where the host put it,
    // because a derived LFO value that reached canonical state would be
    // republished to native as a real edit.
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Positive control on the same instrument and the same target, before any
    // modulation: a host-automation gain change MUST move the same `drawn`
    // reading this test later asserts on. Without it a flat reading below is
    // ambiguous between "the LFO never reaches the render refs" and "the
    // sampler cannot see the render refs at all".
    // Bands default to 0 dB, so this reads the resting bank; apply_surface_params
    // returns "something changed", and re-applying an unchanged default is a
    // legitimate false, not a failure.
    sample_modulated_bank(rig, 1, 1);
    for (std::size_t index = 0; index < spectr::kMaxBands; ++index)
        rig.store.set_value(spectr::band_gain_param_id(index), -12.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);
    rig.bridge().load_script(R"js((() => {
      const state = globalThis.__spectrTestHooks?.renderState?.();
      const before = globalThis.__spectrLfoSamples?.[0];
      if (!state || !before) throw new Error('positive control sampling failed');
      const moved = Math.abs(state.gains[0] - before.drawn[0]);
      if (!(moved > 0.10))
        throw new Error('the drawn-bank instrument cannot see a host gain change: '
          + before.drawn[0] + ' -> ' + state.gains[0]);
    })();)js", "spectr-native-lfo-visual-positive-control");

    // Canonical: every band flat at 0 dB, none muted. A whole-bank LFO adds
    // wave * depth * 12 dB, so a full-depth sine swings the drawn bank across
    // +/-12 dB -- half the +/-24 dB span the render refs carry normalized.
    for (std::size_t index = 0; index < spectr::kMaxBands; ++index) {
        rig.store.set_value(spectr::band_gain_param_id(index), 0.0f);
        rig.store.set_value(spectr::band_mute_param_id(index), 0.0f);
    }
    rig.store.set_value(spectr::kParamLfoEnabled, 1.0f);
    rig.store.set_value(spectr::kParamLfoShape,
                        static_cast<float>(spectr::LfoShape::Sine));
    // 0.25 beats/cycle is the fastest the audio owner accepts. At 120 bpm and
    // 48 kHz that advances the phase 0.0427 per 256-sample block, so the six
    // blocks between samples below cover ~92 degrees: a sine starting at zero
    // reaches nearly full excursion inside the first sample interval.
    rig.store.set_value(spectr::kParamLfoRate, 0.25f);
    rig.store.set_value(spectr::kParamLfoDepth, 1.0f);
    rig.store.set_value(spectr::kParamLfoTarget,
                        static_cast<float>(spectr::ModulationTarget::WholeBank));
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);

    sample_modulated_bank(rig, 6, 6);

    rig.bridge().load_script(R"js((() => {
      const samples = globalThis.__spectrLfoSamples;
      if (!Array.isArray(samples) || samples.length < 6)
        throw new Error('modulation sampling produced no readings');
      const spreadOf = (key, band) => {
        const values = samples.map(sample => sample[key][band]);
        return Math.max(...values) - Math.min(...values);
      };
      const drawn = spreadOf('drawn', 0);
      // Canonical is pinned at 0 dB for every band, so the drawn bank can only
      // move if the modulated field reached the render refs.
      if (!(drawn > 0.10))
        throw new Error('enabled LFO did not move the drawn bank: spread=' + drawn
          + ' samples=' + JSON.stringify(samples.map(s => s.drawn[0])));
      // Every band shares one whole-bank offset, so a modulation lane that only
      // reached band 0 would be a partial fix, not the contract.
      for (let band = 0; band < 8; ++band) {
        if (!(spreadOf('drawn', band) > 0.10))
          throw new Error('band ' + band + ' did not follow the whole-bank LFO: spread='
            + spreadOf('drawn', band));
      }
      // The other half: derived LFO values must never become canonical, or the
      // editor's own publication effect would write them back to native as a
      // host edit and the modulator would ratchet its own baseline.
      for (let band = 0; band < 8; ++band) {
        for (const sample of samples) {
          if (Math.abs(sample.canonical[band]) > 1e-6)
            throw new Error('LFO leaked into canonical state at band ' + band
              + ': ' + sample.canonical[band]);
        }
      }
    })();)js", "spectr-native-lfo-visual-contract");

    // Negative control on the same instrument and the same target: with the
    // modulator off, the identical sampling cadence must read a flat bank. A
    // spread that survives this is the sampler moving the value, not the LFO.
    rig.store.set_value(spectr::kParamLfoEnabled, 0.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);
    // Switching off is a level ramp (spectr::kLfoLevelSlewSeconds), not a
    // step, so let it run out before reading what "off" draws: 256-sample
    // blocks at 48 kHz, plus the falling-edge pass.
    feed_audio_blocks(rig, static_cast<int>(std::ceil(
        spectr::kLfoLevelSlewSeconds * 48000.0 / 256.0)) + 2);
    settle(rig.clock, 4);
    sample_modulated_bank(rig, 6, 6);
    rig.bridge().load_script(R"js((() => {
      const samples = globalThis.__spectrLfoSamples;
      if (!Array.isArray(samples) || samples.length < 6)
        throw new Error('control sampling produced no readings');
      for (let band = 0; band < 8; ++band) {
        const values = samples.map(sample => sample.drawn[band]);
        const spread = Math.max(...values) - Math.min(...values);
        if (!(spread < 1e-6))
          throw new Error('disabled LFO still moved the drawn bank at band '
            + band + ': spread=' + spread);
      }
    })();)js", "spectr-native-lfo-visual-control");
    storage.require_unchanged();
}

// The overlay has to be smooth, not merely present. An LFO assigned to a
// control should sweep it the way host automation playback sweeps a knob, so
// the editor must draw EVERY audio frame it is handed rather than a decimated
// subset: a throttled overlay reads as a stepping, juddering control even
// though the audio underneath is continuous. One audio block per reading is
// the finest grain the publication has, so a reading that repeats means a
// frame was dropped between the audio owner and the paint refs.
TEST_CASE("the modulation overlay tracks every audio frame without decimation",
          "[native-n1][state-parity][modulation-visual]") {
    PatternStoragePoison storage;
    NativeEditorRig      rig;
    require_home(rig);

    for (std::size_t index = 0; index < spectr::kMaxBands; ++index) {
        rig.store.set_value(spectr::band_gain_param_id(index), 0.0f);
        rig.store.set_value(spectr::band_mute_param_id(index), 0.0f);
    }
    rig.store.set_value(spectr::kParamLfoEnabled, 1.0f);
    rig.store.set_value(spectr::kParamLfoShape,
                        static_cast<float>(spectr::LfoShape::Sine));
    rig.store.set_value(spectr::kParamLfoRate, 0.25f);
    rig.store.set_value(spectr::kParamLfoDepth, 1.0f);
    rig.store.set_value(spectr::kParamLfoTarget,
                        static_cast<float>(spectr::ModulationTarget::WholeBank));
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);

    // One block per reading advances the phase 0.0427 of a cycle, so 24
    // readings trace slightly more than one full sine.
    sample_modulated_bank(rig, 24, 1);

    rig.bridge().load_script(R"js((() => {
      const samples = globalThis.__spectrLfoSamples;
      if (!Array.isArray(samples) || samples.length !== 24)
        throw new Error('fine-grained sampling produced ' + (samples || []).length
          + ' readings');
      const trace = samples.map(sample => sample.drawn[0]);

      // No decimation: a 30 Hz throttle over this span would collapse the trace
      // onto a handful of repeated values.
      const distinct = new Set(trace.map(value => value.toFixed(6))).size;
      if (distinct < 18)
        throw new Error('the drawn bank repeats -- the overlay is decimated: '
          + distinct + ' distinct of ' + trace.length + ' [' + trace.join(',') + ']');

      // No jumps: a full-depth sine normalized to +/-0.5 moves at most
      // 0.5 * 2*PI * 0.0427 ~= 0.134 per block. A larger step means readings
      // were skipped and the control would visibly snap.
      let worst = 0;
      for (let index = 1; index < trace.length; ++index)
        worst = Math.max(worst, Math.abs(trace[index] - trace[index - 1]));
      if (!(worst < 0.20))
        throw new Error('the drawn bank jumped ' + worst
          + ' between adjacent frames [' + trace.join(',') + ']');

      // A sine, not noise: one cycle turns twice, so allow a little slack for
      // where the trace starts and ends but reject a jittering signal.
      let turns = 0;
      for (let index = 2; index < trace.length; ++index) {
        const previous = trace[index - 1] - trace[index - 2];
        const current  = trace[index]     - trace[index - 1];
        if (previous !== 0 && current !== 0 && Math.sign(previous) !== Math.sign(current))
          ++turns;
      }
      if (turns > 4)
        throw new Error('the drawn bank reverses ' + turns
          + ' times in one cycle -- not a smooth sweep [' + trace.join(',') + ']');

      // And still display-only across the whole sweep.
      for (const sample of samples)
        for (let band = 0; band < 8; ++band)
          if (Math.abs(sample.canonical[band]) > 1e-6)
            throw new Error('modulation leaked into canonical state at band '
              + band + ': ' + sample.canonical[band]);
    })();)js", "spectr-native-lfo-visual-smoothness");
    storage.require_unchanged();
}

TEST_CASE("native semantic popup navigation owns one visible highlight and selection",
          "[native-n1][state-parity][dropdown]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto directory = atlas_directory();
    capture(rig, directory, "band-header-closed");

    const auto* band_label = find_label(*rig.root, "32 BANDS ▾");
    // The peer: the LENGTH dropdown, the header's other menu trigger (the BOTH
    // tab it used to be measured against is in Settings now).
    const auto* peer_label = find_label(*rig.root, "1 bar");
    REQUIRE(band_label != nullptr);
    REQUIRE(peer_label != nullptr);
    const auto clickable_ancestor = [](const View* view) {
        while (view != nullptr && !view->on_click) view = view->parent();
        return view;
    };
    const auto* band_button = clickable_ancestor(band_label);
    const auto* peer_button = clickable_ancestor(peer_label);
    REQUIRE(band_button != nullptr);
    REQUIRE(peer_button != nullptr);
    const auto band_top = root_point(*band_button, 0.0f, 0.0f);
    const auto band_bottom = root_point(
        *band_button, 0.0f, band_button->bounds().height);
    const auto peer_top = root_point(*peer_button, 0.0f, 0.0f);
    const auto peer_bottom = root_point(
        *peer_button, 0.0f, peer_button->bounds().height);
    // Different heights (22 vs 24), one line: the centres agree to the half
    // point every header control is held to.
    CHECK(0.5f * (band_top.y + band_bottom.y)
          == Catch::Approx(0.5f * (peer_top.y + peer_bottom.y)).margin(0.5f));

    const auto dispatch = [&](pulp::view::KeyCode key) {
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(key), pulp::view::kModNone, true));
        settle(rig.clock, 6);
    };
    rig.bridge().load_script(R"js((() => {
      const trigger = document.querySelector(
        '[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      if (!trigger) {
        const buttons = Array.from(document.querySelectorAll('button')).map(
          button => ({ text: button.textContent.trim(),
            trigger: button.getAttribute('data-spectr-menu-trigger'),
            parentRoot: button.parentElement &&
              button.parentElement.getAttribute('data-spectr-menu-root') }));
        throw new Error('band header trigger missing: ' + JSON.stringify(buttons));
      }
      globalThis.__spectrBandHeaderBefore = {
        trigger: trigger.getBoundingClientRect(),
        separator: Array.from(document.querySelectorAll('span'))
          .map(span => ({ text: span.textContent.trim(), rect: span.getBoundingClientRect() }))
          .filter(entry => entry.text === '·'
            && entry.rect.left >= trigger.getBoundingClientRect().right - 0.5)
          .sort((a, b) => a.rect.left - b.rect.left)[0]?.rect,
        // The header line's reference chip (the BOTH tab it used to be is in
        // Settings now): PEAK, whose box is centred on the controls' line.
        peer: document.querySelector('[data-spectr-output-peak]')?.getBoundingClientRect(),
        zoom: Array.from(document.querySelectorAll('span')).find(
          span => span.textContent.trim().endsWith('× ZOOM'))?.getBoundingClientRect()
      };
    })();)js", "spectr-native-band-header-before-open");
    const auto focus_and_open = [&] {
        rig.bridge().load_script(
            "document.querySelector('[data-spectr-menu-root=\"bands\"] "
            "[data-spectr-menu-trigger]').focus()",
            "spectr-native-focus-band-trigger");
        dispatch(pulp::view::KeyCode::down);
    };

    focus_and_open();
    capture(rig, directory, "band-header-open");
    REQUIRE(rig.root->interaction().active_overlay != nullptr);
    REQUIRE(rig.root->interaction().active_overlay->overlay_consumes_outside_click());
    require_runtime_contract(
        rig,
        "globalThis.__spectrBandCountCenteringReceipt__?.options?.length === 5"
        " && globalThis.__spectrBandCountCenteringReceipt__.options.every("
        "entry => entry.top === 6.5 && Math.abs(entry.left - 15.5) < 0.001)",
        "band popup option text was not optically centered");
    const auto* option_label = find_label(*rig.root, "32");
    REQUIRE(option_label != nullptr);
    REQUIRE(option_label->cached_line_boxes().size() == 1);
    CHECK(option_label->cached_line_boxes().front().left
          == Catch::Approx(15.5f).margin(0.01f));
    CHECK(option_label->cached_line_boxes().front().top
          == Catch::Approx(6.5f).margin(0.01f));
    rig.bridge().load_script(R"js((() => {
      const trigger = document.querySelector(
        '[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      const popup = document.querySelector(
        '[data-spectr-menu-root="bands"] [data-spectr-menu-options]');
      const options = Array.from(document.querySelectorAll(
        '[data-spectr-menu-root="bands"] [data-spectr-menu-options] button'));
      if (!trigger || !popup || options.length !== 5)
        throw new Error('band popup geometry subjects missing');
      const triggerRect = trigger.getBoundingClientRect();
      const popupRect = popup.getBoundingClientRect();
      const rects = options.map(option => option.getBoundingClientRect());
      const before = globalThis.__spectrBandHeaderBefore;
      if (triggerRect.width < 80 || rects.some(rect => rect.width < 43))
        throw new Error('band geometry trigger=' + triggerRect.width
          + ' options=' + rects.map(rect => rect.width).join(','));
      for (let index = 1; index < rects.length; ++index) {
        if (rects[index].left < rects[index - 1].right - 0.5)
          throw new Error('band options overlap at ' + index + ': '
            + rects.map(rect => rect.left + '..' + rect.right).join(','));
      }
      if (popupRect.width < rects.reduce((sum, rect) => sum + rect.width, 0) - 1)
        throw new Error('band popup clips option row');
      if (!before
          || Math.abs(triggerRect.left - before.trigger.left) > 0.5
          || Math.abs(triggerRect.width - before.trigger.width) > 0.5)
        throw new Error('band popup reflowed its header');
      if (!before.peer
          || Math.abs((triggerRect.top + triggerRect.bottom) / 2
                      - (before.peer.top + before.peer.bottom) / 2) > 0.5)
        throw new Error('band trigger missed segmented-control rail: trigger='
          + triggerRect.top + '..' + triggerRect.bottom + ' peer='
          + before.peer?.top + '..' + before.peer?.bottom);
      if (!before.separator || triggerRect.right > before.separator.left - 3.5)
        throw new Error('band trigger failed to reserve separator gap: triggerRight='
          + triggerRect.right + ' separatorLeft=' + before.separator?.left);
      if (!before.zoom || triggerRect.right > before.zoom.left + 0.5)
        throw new Error('band trigger overlaps the zoom readout: triggerRight='
          + triggerRect.right + ' zoomLeft=' + before.zoom?.left);
      if (popupRect.top < triggerRect.bottom - 0.5)
        throw new Error('band popup does not overlay below its trigger');
      if (popupRect.right > globalThis.innerWidth + 0.5)
        throw new Error('band popup clipped at the app edge');
    })();)js", "spectr-native-band-menu-geometry");
    settle(rig.clock, 4);
    require_runtime_contract(
        rig,
        "document.querySelector('[data-pulp-popup-active=\"true\"]')?.textContent.trim() === '32'",
        "ArrowDown did not open with a visible authoritative highlight");
    dispatch(pulp::view::KeyCode::down);
    require_runtime_contract(
        rig,
        "document.querySelector('[data-pulp-popup-active=\"true\"]')?.textContent.trim() === '40'",
        "ArrowDown did not paint the same authoritative highlight as pointer hover");
    dispatch(pulp::view::KeyCode::enter);
    require_app_state(rig, "s.settings.bandCount === 40",
                      "Enter did not invoke the highlighted option callback");
    REQUIRE(spectr::visible_count(rig.processor.layout()) == 40);
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-spectr-menu-root=\"bands\"] [data-spectr-menu-options]')",
        "Enter selected but did not close the popup");
    require_runtime_contract(
        rig,
        "(() => { const text = document.querySelector('[data-spectr-menu-root=\"bands\"] "
        "[data-spectr-menu-trigger] .tnum')?.textContent || ''; "
        "if (!text.includes('40')) throw new Error('stale band trigger: ' + text); return true; })()",
        "Enter selected but did not update the trigger immediately");

    focus_and_open();
    dispatch(pulp::view::KeyCode::escape);
    REQUIRE(spectr::visible_count(rig.processor.layout()) == 40);
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-spectr-menu-root=\"bands\"] [data-spectr-menu-options]')",
        "Escape did not dismiss without changing selection");

    focus_and_open();
    REQUIRE(rig.root->interaction().active_overlay != nullptr);
    REQUIRE(rig.root->interaction().active_overlay->overlay_consumes_outside_click());
    pulp::view::View::dismiss_active_overlay(*rig.root);
    settle(rig.clock, 10);
    REQUIRE(rig.root->interaction().active_overlay == nullptr);
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-spectr-menu-root=\"bands\"] [data-spectr-menu-options]')",
        "native overlay dismissal did not close the authored popup");

    focus_and_open();
    const pulp::view::Point outside{1200.0f, 430.0f};
    auto* outside_target = rig.root->hit_test(outside);
    REQUIRE(outside_target != nullptr);
    REQUIRE(pulp::view::transfer_input_focus(*rig.root, outside_target));
    REQUIRE(pulp::view::deliver_mouse_down(*rig.root, outside_target, outside,
                                           /*modifiers=*/0,
                                           /*click_count=*/1,
                                           /*bubble=*/true));
    settle(rig.clock, 10);
    REQUIRE(spectr::visible_count(rig.processor.layout()) == 40);
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-spectr-menu-root=\"bands\"] [data-spectr-menu-options]')",
        "outside pointer did not dismiss without changing selection");
    storage.require_unchanged();
}

TEST_CASE("native selected tabs inherit hover through their label ancestry",
          "[native-n1][state-parity][hover]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // The selected visualization tab is Settings > Display's "Both" chip
    // (the header's BARS / RESPONSE / BOTH tabs moved there).
    rig.root->layout_children();
    settle(rig.clock, 4);
#if defined(__APPLE__)
    constexpr auto settings_modifier = pulp::view::kModCmd;
#else
    constexpr auto settings_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = static_cast<pulp::view::KeyCode>(','),
        .modifiers = settings_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    require_state(rig, "settings");
    const auto* label = find_label(*rig.root, "Both");
    REQUIRE(label != nullptr);
    const View* button = nearest_click_target(label);
    REQUIRE(button != nullptr);
    CHECK(button->opacity() == Catch::Approx(1.0f));
    require_app_state(rig, "s.visualizationMode === 'both'",
                      "the selected visualization chip was not Both");

    const auto point = root_point(
        *label, label->bounds().width * 0.5f, label->bounds().height * 0.5f);
    const View* feedback_owner = rig.root->hit_test(point);
    std::string ancestry;
    for (const View* node = feedback_owner; node != nullptr; node = node->parent()) {
        if (!ancestry.empty()) ancestry += " <- ";
        ancestry += node->id();
        ancestry += "{role=" + std::to_string(static_cast<int>(node->access_role()));
        ancestry += ",default=" + std::to_string(node->default_hover_feedback());
        ancestry += ",click=" + std::to_string(static_cast<bool>(node->on_click));
        ancestry += "}";
    }
    INFO("hover ancestry: " << ancestry);
    while (feedback_owner != nullptr &&
           !feedback_owner->default_hover_feedback()) {
        feedback_owner = feedback_owner->parent();
    }
    REQUIRE(feedback_owner != nullptr);
    CHECK(feedback_owner->access_role() == View::AccessRole::button);
    capture(rig, atlas_directory(), "selected-tab-resting");

    rig.root->simulate_hover(point);
    settle(rig.clock, 4);
    CHECK(button->is_hovered());
    CHECK(feedback_owner->is_hovered());
    capture(rig, atlas_directory(), "selected-tab-hover");
    require_app_state(rig, "s.visualizationMode === 'both'",
                      "hover changed the selected visualization tab");
    storage.require_unchanged();
}

TEST_CASE("every native dropdown dismisses by Escape and outside press",
          "[native-n1][state-parity][dropdown][dismissal]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    const std::array<std::string_view, 6> menus{
        "bands", "edit", "analyzer", "overflow", "pattern", "length"};
    const pulp::view::Point outside{660.0f, 430.0f};
    for (const auto menu : menus) {
        INFO("menu=" << menu);
        const auto root = std::string{"[data-spectr-menu-root=\""}
            + std::string(menu) + "\"]";
        const auto trigger = root + " [data-spectr-menu-trigger]";
        const auto options = root + " [data-spectr-menu-options]";

        const auto open_from_keyboard = [&] {
            rig.bridge().load_script(
                "document.querySelector(" + js_string(trigger) + ").focus()",
                "spectr-native-focus-menu-trigger");
            REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
                *rig.root, static_cast<int>(pulp::view::KeyCode::down),
                pulp::view::kModNone, true));
            settle(rig.clock, 8);
        };
        open_from_keyboard();
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
        REQUIRE(rig.root->interaction().active_overlay->overlay_consumes_outside_click());
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::escape),
            pulp::view::kModNone, true));
        settle(rig.clock, 8);
        require_runtime_contract(
            rig, "!document.querySelector(" + js_string(options) + ")",
            "Escape left a dropdown open");

        open_from_keyboard();
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
        CAPTURE(describe_control(*rig.root->interaction().active_overlay));
        REQUIRE_FALSE(rig.root->interaction().active_overlay->overlay_contains(outside));
        rig.root->simulate_click(outside);
        settle(rig.clock, 8);
        require_runtime_contract(
            rig, "!document.querySelector(" + js_string(options) + ")",
            "outside press left a dropdown open");

        // The same Pulp-owned active state must drive keyboard traversal and
        // pointer hover for every Spectr popup, not just the band selector.
        // Keeping this in the all-menu loop prevents a semantically incomplete
        // imported dropdown from silently falling back to click-only behavior.
        open_from_keyboard();
        require_runtime_contract(
            rig,
            "(() => { const popup = document.querySelector("
                + js_string(options)
                + "); const items = popup ? Array.from(document.querySelectorAll("
                + js_string(options + " button") + ")) : []; "
                  "const seed = Math.max(0, items.findIndex((n) => n.getAttribute('aria-selected') === 'true')); "
                  "return items.length >= 3 "
                  "&& document.querySelector('[data-pulp-popup-active=\"true\"]') === items[seed]; })()",
            "ArrowDown did not open the dropdown with one visible highlight (on the "
            "selected row, where a menu marks one: LENGTH opens on 1 bar, its fifth)");
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::down),
            pulp::view::kModNone, true));
        settle(rig.clock, 4);
        require_runtime_contract(
            rig,
            "(() => { const items = Array.from(document.querySelectorAll("
                + js_string(options + " button") + ")); "
                  "const seed = Math.max(0, items.findIndex((n) => n.getAttribute('aria-selected') === 'true')); "
                  "return document.querySelector('[data-pulp-popup-active=\"true\"]') === items[seed + 1]; })()",
            "ArrowDown did not move the authoritative dropdown highlight");
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::up),
            pulp::view::kModNone, true));
        settle(rig.clock, 4);
        require_runtime_contract(
            rig,
            "(() => { const items = Array.from(document.querySelectorAll("
                + js_string(options + " button") + ")); "
                  "const seed = Math.max(0, items.findIndex((n) => n.getAttribute('aria-selected') === 'true')); "
                  "return document.querySelector('[data-pulp-popup-active=\"true\"]') === items[seed]; })()",
            "ArrowUp did not move the authoritative dropdown highlight");
        const auto hover_point = runtime_string(
            rig,
            "(() => { const rect = Array.from(document.querySelectorAll("
                + js_string(options + " button")
                + "))[2].getBoundingClientRect(); "
                  "return (rect.left + rect.width / 2) + ','"
                  " + (rect.top + rect.height / 2); })()",
            "spectr-native-hover-menu-option-point");
        const auto comma = hover_point.find(',');
        REQUIRE(comma != std::string::npos);
        rig.root->simulate_hover({
            std::stof(hover_point.substr(0, comma)),
            std::stof(hover_point.substr(comma + 1))});
        settle(rig.clock, 4);
        require_runtime_contract(
            rig,
            "(() => { const items = Array.from(document.querySelectorAll("
                + js_string(options + " button") + ")); const hovered = items[2]; "
                  "return document.querySelector('[data-pulp-popup-active=\"true\"]') === hovered "
                  // The fill the row PAINTS, read from its native view: Pulp's
                  // popup owner writes it through the element's style, a menu
                  // that owns its highlight (LENGTH) through its React style.
                  "&& (typeof getBackground === 'function' "
                  "    ? String(getBackground(hovered._id)).indexOf('rgba(120,180,255,0.18') === 0 "
                  "    : hovered.style.backgroundColor === 'rgba(120,180,255,0.18)'); })()",
            "pointer hover did not move and visibly paint the dropdown highlight");
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::enter),
            pulp::view::kModNone, true));
        settle(rig.clock, 8);
        require_runtime_contract(
            rig, "!document.querySelector(" + js_string(options) + ")",
            "Return did not select the highlighted option and close the dropdown");
    }
    storage.require_unchanged();
}

TEST_CASE("native settings modal dismisses by Escape and outside press",
          "[native-n1][state-parity][settings][dismissal]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    // Match the production host boundary: the first frame lays out the mounted
    // document before a user can invoke the modal command.
    rig.root->layout_children();
    settle(rig.clock, 4);
    const auto comma = static_cast<pulp::view::KeyCode>(',');
    CHECK_FALSE(rig.root->on_global_key({
        .key = comma,
        .modifiers = pulp::view::kModNone,
        .is_down = true}));
    require_home(rig);

    const auto open_settings = [&] {
#if defined(__APPLE__)
        constexpr auto primary_modifier = pulp::view::kModCmd;
#else
        constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
        REQUIRE(rig.root->on_global_key({
            .key = comma,
            .modifiers = primary_modifier,
            .is_down = true}));
        settle(rig.clock, 16);
        require_state(rig, "settings");
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
    };

    INFO("phase=open-for-escape");
    open_settings();
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-pulp-popup-active=\"true\"]')",
        "Settings opened with a stale dropdown highlight claim");
    REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
        *rig.root, static_cast<int>(pulp::view::KeyCode::escape),
        pulp::view::kModNone, true));
    REQUIRE(rig.root->interaction().active_overlay == nullptr);
    settle(rig.clock, 8);
    require_home(rig);

    INFO("phase=open-for-outside");
    open_settings();
    REQUIRE(rig.root->interaction().active_overlay->overlay_consumes_outside_click());
    const auto* title = find_label(*rig.root, "SETTINGS");
    REQUIRE(title != nullptr);
    const auto title_rect = root_rect(*title);
    const pulp::view::Point title_point{
        (title_rect.left + title_rect.right) * 0.5f,
        (title_rect.top + title_rect.bottom) * 0.5f};
    CAPTURE(describe_control(*rig.root->interaction().active_overlay));
    REQUIRE(rig.root->interaction().active_overlay->overlay_contains(title_point));
    const auto inside = pulp::view::route_press_to_active_overlay(
        *rig.root, title_point);
    REQUIRE(inside.routing == pulp::view::OverlayPressRouting::routed);
    REQUIRE(inside.target != nullptr);
    REQUIRE(static_cast<bool>(rig.root->interaction().active_overlay
                                  ->on_overlay_dismissed));
    require_runtime_contract(
        rig,
        "(() => { const panel = document.querySelector('[data-spectr-settings-panel]');"
        " const key = panel && panel.__pulpId + ':dismiss';"
        " return !!key && globalThis.__pulpReactEventCallbacks__.has(key); })()",
        "settings panel lost its native dismiss callback");
    settle(rig.clock, 8);
    INFO("phase=inside-positive-control");
    require_state(rig, "settings");

    // The authored panel occupies x=400..920. This point is on the modal
    // scrim, proving the outside path rather than reusing the close button.
    const auto field_before_outside = rig.processor.field();
    rig.root->simulate_click({1200.0f, 430.0f});
    settle(rig.clock, 8);
    INFO("phase=outside-dismissal");
    require_home(rig);
    for (std::size_t index = 0; index < field_before_outside.bands.size(); ++index) {
        CHECK(rig.processor.field().bands[index].gain_db
              == Catch::Approx(field_before_outside.bands[index].gain_db));
        CHECK(rig.processor.field().bands[index].muted
              == field_before_outside.bands[index].muted);
    }
    storage.require_unchanged();
}

TEST_CASE("remaining native modal panels share Escape and outside dismissal",
          "[native-n1][state-parity][modal-dismissal]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto exercise = [&](std::string_view name,
                              const auto& open,
                              std::string_view panel_selector) {
        const auto require_open = [&] {
            CAPTURE(name);
            open();
            settle(rig.clock, 12);
            require_runtime_contract(
                rig,
                "!!document.querySelector(" + js_string(panel_selector) + ")",
                std::string{name} + " did not open");
            REQUIRE(rig.root->interaction().active_overlay != nullptr);
            REQUIRE(rig.root->interaction().active_overlay
                        ->overlay_consumes_outside_click());
            REQUIRE(static_cast<bool>(rig.root->interaction().active_overlay
                                          ->on_overlay_dismissed));
            require_runtime_contract(
                rig,
                "(() => { const panel = document.querySelector("
                    + js_string(panel_selector)
                    + "); const key = panel && panel.__pulpId + ':dismiss';"
                      " return !!key && globalThis.__pulpReactEventCallbacks__.has(key); })()",
                std::string{name} + " lost its native dismiss callback");

            const auto center = runtime_string(
                rig,
                "(() => { const r = document.querySelector("
                    + js_string(panel_selector)
                    + ").getBoundingClientRect(); return (r.left + r.width / 2)"
                      " + ',' + (r.top + r.height / 2); })()",
                "spectr-native-modal-panel-center");
            const auto comma = center.find(',');
            REQUIRE(comma != std::string::npos);
            const pulp::view::Point inside{
                std::stof(center.substr(0, comma)),
                std::stof(center.substr(comma + 1))};
            REQUIRE(rig.root->interaction().active_overlay->overlay_contains(inside));
            const auto routing = pulp::view::route_press_to_active_overlay(
                *rig.root, inside);
            REQUIRE(routing.routing == pulp::view::OverlayPressRouting::routed);
            settle(rig.clock, 4);
            require_runtime_contract(
                rig,
                "!!document.querySelector(" + js_string(panel_selector) + ")",
                std::string{name} + " treated an inside press as outside");
        };

        INFO("phase=escape " << name);
        require_open();
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::escape),
            pulp::view::kModNone, true));
        settle(rig.clock, 8);
        require_runtime_contract(
            rig,
            "!document.querySelector(" + js_string(panel_selector) + ")",
            std::string{name} + " ignored Escape");

        INFO("phase=outside " << name);
        require_open();
        const pulp::view::Point outside{12.0f, 12.0f};
        REQUIRE_FALSE(
            rig.root->interaction().active_overlay->overlay_contains(outside));
        const auto field_before_outside = rig.processor.field();
        rig.root->simulate_click(outside);
        settle(rig.clock, 8);
        require_runtime_contract(
            rig,
            "!document.querySelector(" + js_string(panel_selector) + ")",
            std::string{name} + " ignored native outside dismissal");
        require_home(rig);
        for (std::size_t index = 0; index < field_before_outside.bands.size(); ++index) {
            CHECK(rig.processor.field().bands[index].gain_db
                  == Catch::Approx(field_before_outside.bands[index].gain_db));
            CHECK(rig.processor.field().bands[index].muted
                  == field_before_outside.bands[index].muted);
        }
    };

    exercise("pattern manager", [&] {
        activate(rig,
                 "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
        activate(rig, "[data-spectr-pattern-manage]");
    }, "[data-spectr-pattern-manager-panel]");

    exercise("save preset", [&] {
        activate(rig,
                 "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
        activate(rig, "[data-spectr-save-current]");
    }, "[data-spectr-save-panel]");

    exercise("help", [&] {
        activate(rig,
                 "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    }, "[data-spectr-help-panel]");

    storage.require_unchanged();
}

TEST_CASE("native Flare preserves mixed-sign curves and bands crossing 0 dB",
          "[native-n1][state-parity][flare]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    rig.close();
    for (std::size_t index = 0; index < rig.processor.field().bands.size(); ++index) {
        auto& band = rig.processor.field().bands[index];
        band.gain_db = index % 3 == 0 ? -4.0f : index % 3 == 1 ? 4.0f : 0.0f;
        band.muted = false;
    }
    rig.open();
    require_home(rig);
    activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-edit-mode=\"flare\"]");
    require_app_state(rig, "s.editMode === 'flare'", "Flare mode did not become authoritative");

    rig.bridge().load_script(R"js((() => {
      const selector = '[data-spectr-filter-surface]';
      const fire = (type, x, y, buttons) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
          clientX: x, clientY: y, pointerId: 94, button: 0, buttons
        })) throw new Error('Flare activation failed: ' + type);
      };
      fire('pointerdown', 660, 430, 1);
      fire('pointermove', 660, 350, 1);
      fire('pointerup', 660, 350, 0);
      // Exercise a neighboring positive-sign band as well as the negative
      // band above; a single x coordinate can only paint one band.
      fire('pointerdown', 680, 430, 1);
      fire('pointermove', 680, 350, 1);
      fire('pointerup', 680, 350, 0);
      if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
        globalThis.__pulpRuntimeSettle__(6);
      const gains = globalThis.__spectrTestHooks.renderState().targetGains;
      if (!gains.some((value, index) => index % 3 === 0 && Number.isFinite(value) && value < (-4 / 24)))
        throw new Error('Flare did not push a negative band farther below zero');
      if (!gains.some((value, index) => index % 3 === 1 && Number.isFinite(value) && value > (4 / 24)))
        throw new Error('Flare did not push a positive band farther above zero: ' + JSON.stringify(gains));
      if (gains.some((value, index) => index % 3 === 0 && value > 0))
        throw new Error('Flare flipped a below-zero band positive');
      if (gains.some((value, index) => index % 3 === 1 && value < 0))
        throw new Error('Flare flipped an above-zero band negative');
      if (gains.some((value, index) => index % 3 === 2 && Math.abs(value) > 1e-7))
        throw new Error('Flare moved a zero-crossing band away from 0 dB');
    })();)js", "spectr-native-flare-crossing-zero");
    settle(rig.clock, 8);
    const auto visible = spectr::visible_count(rig.processor.layout());
    REQUIRE(std::any_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + visible,
                        [](const auto& band) { return band.gain_db < -4.01f; }));
    // The two vertical strokes intentionally touch one negative and one
    // positive band; untouched bands retain their starting values.
    REQUIRE(rig.processor.field().bands[15].gain_db < -4.01f);
    REQUIRE(rig.processor.field().bands[16].gain_db > 4.01f);
    REQUIRE(rig.processor.field().bands[17].gain_db
            == Catch::Approx(0.0f).margin(1.0e-6f));
    storage.require_unchanged();
}

TEST_CASE("native frozen state atlas interactions and persistence",
          "[native-n1][state-parity]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    const auto directory = atlas_directory();
    require_home(rig);
    require_app_state(rig, "s.nativeHydrated === true && s.statusMounted === false",
                      "initial native commit was not hydrated and status-free");
    require_app_state(
        rig,
        "JSON.stringify(globalThis.__spectrHeaderOpticalCenteringReceipt__) "
        "=== '[{\"role\":\"mark\",\"x_shift\":0,\"y_shift\":0.5},"
        "{\"role\":\"word\",\"x_shift\":-1,\"y_shift\":1},"
        "{\"role\":\"separator\",\"x_shift\":0,\"y_shift\":1.25},"
        "{\"role\":\"tagline\",\"x_shift\":0,\"y_shift\":1.25}]'",
        "header optical-centering corrections were not applied");
    const auto* brand_word = find_label(*rig.root, "SPECTR");
    const auto* brand_tagline = find_label(*rig.root, "ZOOMABLE FILTER BANK");
    REQUIRE(brand_word != nullptr);
    REQUIRE(brand_tagline != nullptr);
    CHECK(brand_word->font_family().find("JetBrains Mono")
          != std::string::npos);
    CHECK(brand_word->font_size() == Catch::Approx(11.0f));
    CHECK(brand_word->letter_spacing() == Catch::Approx(1.5f));
    REQUIRE(brand_word->cached_line_boxes().size() == 1);
    CHECK(brand_word->cached_line_boxes().front().width
          == Catch::Approx(48.609375f).margin(0.01f));
    CHECK(brand_tagline->font_family().find("JetBrains Mono")
          != std::string::npos);
    CHECK(brand_tagline->font_size() == Catch::Approx(11.0f));
    CHECK(brand_tagline->letter_spacing() == Catch::Approx(0.5f));
    REQUIRE(brand_tagline->cached_line_boxes().size() == 1);
    CHECK(brand_tagline->cached_line_boxes().front().width
          == Catch::Approx(142.0f).margin(0.01f));
    const auto require_optical_shift = [](const View* view,
                                           float expected_x,
                                           float expected_y) {
        REQUIRE(view != nullptr);
        REQUIRE(view->has_transform_matrix());
        float a, b, c, d, e, f;
        view->get_transform_matrix(a, b, c, d, e, f);
        CHECK(a == Catch::Approx(1.0f));
        CHECK(b == Catch::Approx(0.0f));
        CHECK(c == Catch::Approx(0.0f));
        CHECK(d == Catch::Approx(1.0f));
        CHECK(e == Catch::Approx(expected_x));
        CHECK(f == Catch::Approx(expected_y));
    };
    require_optical_shift(brand_word, -1.0f, 1.0f);
    require_optical_shift(brand_tagline, 0.0f, 1.25f);
    require_app_state(rig, "s.userPatterns.length === 0",
                      "native UI consumed browser-local preset poison");
    REQUIRE(std::all_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) {
                            return band.gain_db == 0.0f && !band.muted;
                        }));
    storage.require_unchanged();
    require_app_state(
        rig,
        "JSON.stringify(globalThis.__spectrToolbarTextButtonCenteringReceipt__) "
        "=== '[{\"text\":\"CLEAR\",\"line_top\":6},"
        "{\"text\":\"⋯\",\"line_top\":6}]'",
        "text-only toolbar centering corrections were not applied");
    const auto* clear_label = find_label(*rig.root, "CLEAR");
    REQUIRE(clear_label != nullptr);
    REQUIRE(clear_label->cached_line_boxes().size() == 1);
    CHECK(clear_label->cached_line_boxes().front().left
          == Catch::Approx(10.0f).margin(0.01f));
    CHECK(clear_label->cached_line_boxes().front().top
          == Catch::Approx(6.0f).margin(0.01f));
    const auto* overflow_label = find_label(*rig.root, "⋯");
    REQUIRE(overflow_label != nullptr);
    CAPTURE(overflow_label->font_family(),
            overflow_label->cached_line_boxes().size(),
            overflow_label->bounds().width,
            overflow_label->bounds().height);
    REQUIRE(overflow_label->cached_line_boxes().size() == 1);
    const auto& overflow_line = overflow_label->cached_line_boxes().front();
    CAPTURE(overflow_line.left, overflow_line.top, overflow_line.width,
            overflow_line.height);
    pulp::view::Label::reset_line_break_path_counts();
    pulp::canvas::RecordingCanvas overflow_canvas;
    const_cast<pulp::view::Label*>(overflow_label)->paint(overflow_canvas);
    const auto overflow_counts = pulp::view::Label::line_break_path_counts();
    CAPTURE(overflow_counts.cached, overflow_counts.reflowed,
            overflow_counts.uncached);
    const auto overflow_draw = std::find_if(
        overflow_canvas.commands().begin(), overflow_canvas.commands().end(),
        [](const auto& command) {
            return command.type
                == pulp::canvas::DrawCommand::Type::fill_text;
        });
    REQUIRE(overflow_draw != overflow_canvas.commands().end());
    REQUIRE(overflow_draw->f[0] == Catch::Approx(10.0f).margin(0.01f));
    // The captured 13px line box is painted with the 3px CSS half-leading
    // retained around the 10px face.  This is the non-image canary for the
    // toolbar's optical vertical centering at both 1x and Retina scale.
    // The baseline sits a fraction above the nominal because it comes from the
    // face's real ascent rather than a fixed fraction of the em, so the margin
    // spans a face's worth of ascent variation.  A broken centering moves this
    // by pixels, not by fractions of one.
    REQUIRE(overflow_draw->f[1] == Catch::Approx(16.0f).margin(0.25f));

    const auto require_captured_toolbar_label = [&](std::string_view text,
                                                     float expected_width) {
        const auto* label = find_label(*rig.root, text);
        REQUIRE(label != nullptr);
        CAPTURE(text, label->font_family(), label->font_size(),
                label->letter_spacing(), label->cached_line_boxes().size());
        REQUIRE(label->font_family().find("JetBrains Mono")
                != std::string::npos);
        REQUIRE(label->font_size() == Catch::Approx(10.0f).margin(0.001f));
        REQUIRE(label->letter_spacing()
                == Catch::Approx(1.0f).margin(0.001f));
        REQUIRE(label->cached_line_boxes().size() == 1);
        const auto& line = label->cached_line_boxes().front();
        CAPTURE(line.left, line.top, line.width, line.height);
        REQUIRE(line.width == Catch::Approx(expected_width).margin(0.02f));
        REQUIRE(line.height == Catch::Approx(13.017578f).margin(0.01f));
    };
    require_captured_toolbar_label("SCULPT ▾", 56.034375f);
    require_captured_toolbar_label("PEAK ▾", 42.042578f);
    require_app_state(
        rig,
        "JSON.stringify(globalThis.__spectrToolbarOpticalCenteringReceipt__) === "
        "'[{\"root\":\"edit\",\"svg_top\":6.5,\"label_top\":6.25,\"svg_x_shift\":-1,\"svg_y_shift\":0,\"label_x_shift\":-1,\"label_y_shift\":0},"
        "{\"root\":\"analyzer\",\"svg_top\":3.75,\"label_top\":6.25,\"svg_x_shift\":-1,\"svg_y_shift\":0,\"label_x_shift\":-1,\"label_y_shift\":0},"
        "{\"root\":\"pattern\",\"svg_top\":5.375,\"label_top\":6.25,\"svg_x_shift\":0.25,\"svg_y_shift\":0,\"label_x_shift\":0.25,\"label_y_shift\":0}]'",
        "toolbar optical-centering corrections were not applied");
    const auto require_toolbar_child_geometry = [&](std::string_view text,
                                                      float icon_top,
                                                      float label_top,
                                                      float icon_width,
                                                      float icon_height,
                                                      float icon_left,
                                                      float label_left,
                                                      float icon_x_shift,
                                                      float icon_y_shift,
                                                      float label_x_shift,
                                                      float label_y_shift) {
        CAPTURE(text);
        const auto* label = find_label(*rig.root, text);
        REQUIRE(label != nullptr);
        const auto* button = label->parent();
        REQUIRE(button != nullptr);
        const auto* icon = find_sized_descendant(*button, icon_width, icon_height);
        REQUIRE(icon != nullptr);
        CAPTURE(text, button->bounds().width, button->bounds().height,
                icon->bounds().x, icon->bounds().y,
                label->bounds().x, label->bounds().y);
        REQUIRE(button->bounds().height == Catch::Approx(26.0f).margin(0.01f));
        REQUIRE(icon->bounds().x == Catch::Approx(icon_left).margin(0.01f));
        REQUIRE(label->bounds().x == Catch::Approx(label_left).margin(0.01f));
        REQUIRE(icon->bounds().y == Catch::Approx(icon_top).margin(0.01f));
        REQUIRE(label->bounds().y == Catch::Approx(label_top).margin(0.01f));
        const auto require_shift = [](const View* child, float x_shift,
                                      float y_shift) {
            CHECK(child->has_transform_matrix()
                  == (x_shift != 0.0f || y_shift != 0.0f));
            if (x_shift != 0.0f || y_shift != 0.0f) {
                float a, b, c, d, e, f;
                child->get_transform_matrix(a, b, c, d, e, f);
                CHECK(a == Catch::Approx(1.0f));
                CHECK(b == Catch::Approx(0.0f));
                CHECK(c == Catch::Approx(0.0f));
                CHECK(d == Catch::Approx(1.0f));
                CHECK(e == Catch::Approx(x_shift));
                CHECK(f == Catch::Approx(y_shift));
            }
        };
        require_shift(icon, icon_x_shift, icon_y_shift);
        require_shift(label, label_x_shift, label_y_shift);
    };
    // View bounds include the button's 1px border; the receipt above preserves
    // the raw CSS top coordinates.
    require_toolbar_child_geometry("SCULPT ▾", 7.5f,
                                   7.25f, 22.0f, 16.0f,
                                   11.0f, 39.0f,
                                   -1.0f, 0.0f, -1.0f, 0.0f);
    require_toolbar_child_geometry("PEAK ▾", 4.75f,
                                   7.25f, 22.0f, 16.0f,
                                   11.0f, 39.0f,
                                   -1.0f, 0.0f, -1.0f, 0.0f);
    // The preset label is processor-owned and can change before the first
    // painted frame. Its live geometry is covered by the optical-centering
    // receipt above rather than by a frozen initial-label lookup.
    feed_tone(rig);
    capture(rig, directory, "home");

    activate(rig, "[data-spectr-menu-root=\"bands\"] [data-spectr-menu-trigger]");
    require_state(rig, "bands");
    capture(rig, directory, "bands");
    activate(rig, "[data-spectr-band-count=\"40\"]");
    REQUIRE(rig.processor.layout() == spectr::Layout::Bands40);
    require_app_state(rig, "s.settings.bandCount === 40", "40-band menu selection failed");
    activate(rig, "[data-spectr-menu-root=\"bands\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-band-count=\"32\"]");
    REQUIRE(rig.processor.layout() == spectr::Layout::Bands32);

    const auto require_selected_toolbar_baseline = [&] (std::string_view root,
                                                         std::string_view attribute,
                                                         std::string_view value) {
        activate(rig, "[data-spectr-menu-root=\"" + std::string(root)
                      + "\"] [data-spectr-menu-trigger]");
        activate(rig, "[" + std::string(attribute) + "=\""
                      + std::string(value) + "\"]");
        require_runtime_contract(
            rig,
            "globalThis.__spectrToolbarOpticalCenteringReceipt__.find("
            "entry => entry.root === '" + std::string(root)
            + "')?.label_top === 6.25",
            "selected " + std::string(root) + " label lost its optical baseline");
    };

    activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
    require_state(rig, "edit");
    capture(rig, directory, "edit");
    activate(rig, "[data-spectr-edit-mode=\"level\"]");
    require_app_state(rig, "s.editMode === 'level'", "edit mode menu selection failed");
    for (const auto* mode : {"boost", "flare", "glide", "sculpt"})
        require_selected_toolbar_baseline("edit", "data-spectr-edit-mode", mode);
    // The idle status shell stays in the tree without painting, so transient
    // messages cannot shift materialized state paths.
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"analyzer\"] [data-spectr-menu-trigger]");
    require_state(rig, "analyzer");
    capture(rig, directory, "analyzer");
    activate(rig, "[data-spectr-analyzer-mode=\"avg\"]");
    require_app_state(rig, "s.analyzerMode === 'avg'", "analyzer menu selection failed");
    for (const auto* mode : {"both", "off", "peak"})
        require_selected_toolbar_baseline(
            "analyzer", "data-spectr-analyzer-mode", mode);
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"overflow\"] [data-spectr-menu-trigger]");
    require_state(rig, "overflow");
    capture(rig, directory, "overflow");
    activate(rig, "[data-spectr-overflow-action=\"mute-all\"]");
    REQUIRE(std::all_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) { return band.muted; }));
    activate(rig, "[data-spectr-menu-root=\"overflow\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-overflow-action=\"mute-all\"]");
    REQUIRE(std::none_of(rig.processor.field().bands.begin(),
                         rig.processor.field().bands.begin() + 32,
                         [](const auto& band) { return band.muted; }));

    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    require_state(rig, "pattern");
    require_runtime_contract(rig, R"js((() => {
      const popup = document.querySelector(
        '[data-spectr-menu-root="pattern"] [data-spectr-menu-options]');
      const save = document.querySelector('[data-spectr-save-current]');
      const manage = document.querySelector('[data-spectr-pattern-manage]');
      if (!popup || !save || !manage) return false;
      const p = popup.getBoundingClientRect();
      const s = save.getBoundingClientRect();
      const m = manage.getBoundingClientRect();
      const valid = Math.abs(s.left - m.left) < 0.5
        && Math.abs(s.width - m.width) < 0.5
        && m.top >= s.bottom - 0.5
        && s.left >= p.left - 0.5 && m.right <= p.right + 0.5
        && m.bottom <= p.bottom + 0.5;
      if (!valid) throw new Error(JSON.stringify({ popup: p, save: s, manage: m }));
      return true;
    })())js", "preset actions were not separate full-width rows");
    capture(rig, directory, "pattern");
    activate(rig, "[data-spectr-pattern-menu-id=\"factory:tilt\"]");
    REQUIRE(std::any_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) { return std::abs(band.gain_db) > 0.1f; }));
    REQUIRE(std::any_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) { return band.gain_db < -0.1f; }));
    REQUIRE(std::any_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) { return band.gain_db > 0.1f; }));
    require_runtime_contract(
        rig,
        "document.querySelector('[data-spectr-selected-preset]')?.textContent"
        " === 'DOWNWA\u2026 \u25BE'",
        "selected factory preset name was not safely truncated on the trigger");
    require_runtime_contract(
        rig,
        "globalThis.__spectrToolbarOpticalCenteringReceipt__.find("
        "entry => entry.root === 'pattern')?.label_top === 6.25",
        "selected preset label lost the shared toolbar optical baseline");
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-pattern-menu-id=\"factory:flat\"]");
    require_runtime_contract(
        rig,
        "document.querySelector('[data-spectr-selected-preset]')?.textContent"
        " === 'FLAT \u25BE'",
        "selected factory preset name did not update after applying a new preset");
    require_home(rig);

    activate(rig, "[data-spectr-settings-open]");
    require_state(rig, "settings");
    require_runtime_contract(
        rig,
        "(() => { const s = globalThis.__spectrResponsiveLayoutReceipt__?.settings; "
        "return s && s.width === 520 && s.height === 679"
        " && s.top === 90.5 && s.authored_skin === true; })()",
        "authored settings geometry drifted from the frozen 1320x860 capture");
    const auto* authored_settings_title = find_label(*rig.root, "SETTINGS");
    REQUIRE(authored_settings_title != nullptr);
    const auto* settings_body_label = find_label(*rig.root, "APPEARANCE");
    REQUIRE(settings_body_label != nullptr);
    require_runtime_contract(
        rig,
        "(() => { const e = document.querySelector('[data-spectr-settings-body]'); "
        "return !!e && JSON.stringify(e.style?._props || {}) + ' wants=' "
        "+ (typeof __pulpElementWantsScrollView__ === 'function' "
        "? __pulpElementWantsScrollView__(e) : 'missing'); })()",
        "settings body scroll hint was not visible to the native materializer");
    const View* settings_body = settings_body_label;
    while (settings_body != nullptr
           && dynamic_cast<const pulp::view::ScrollView*>(settings_body) == nullptr)
        settings_body = settings_body->parent();
    REQUIRE(settings_body != nullptr);
    const View* authored_settings_panel = settings_body->parent();
    REQUIRE(authored_settings_panel != nullptr);
    // The modal panel is the fixed chrome/container; only its body owns
    // scrolling. Requiring a second ScrollView here would contradict the
    // fixed-header/tabs architecture and would make the test reject the
    // intended single-scroll-owner topology.
    CHECK(authored_settings_panel->bounds().x
          == Catch::Approx(400.0f).margin(0.01f));
    CHECK(authored_settings_panel->bounds().y
          == Catch::Approx(90.5f).margin(0.01f));
    CHECK(authored_settings_panel->bounds().width
          == Catch::Approx(520.0f).margin(0.01f));
    CHECK(authored_settings_panel->bounds().height
          == Catch::Approx(679.0f).margin(0.01f));
    const auto panel_rect = root_rect(*authored_settings_panel);
    const auto* close_label = find_label(*rig.root, "×");
    REQUIRE(close_label != nullptr);
    const auto* close_target = nearest_click_target(close_label);
    REQUIRE(close_target != nullptr);
    const auto close_rect = root_rect(*close_target);
    const View* settings_header = authored_settings_title;
    while (settings_header != nullptr
           && settings_header->position() != View::Position::sticky)
        settings_header = settings_header->parent();
    REQUIRE(settings_header != nullptr);
    const auto header_rect = root_rect(*settings_header);
    // Sticky modal chrome owns the complete top strip. A title-sized opaque
    // box leaves scrolled fields visible through the panel padding around it.
    CHECK(header_rect.left == Catch::Approx(panel_rect.left + 1.0f).margin(0.01f));
    CHECK(header_rect.top == Catch::Approx(panel_rect.top + 1.0f).margin(0.01f));
    CHECK(header_rect.right == Catch::Approx(panel_rect.right - 1.0f).margin(0.01f));
    CHECK(header_rect.bottom >= close_rect.bottom + 8.0f);
    CHECK(close_rect.top >= panel_rect.top + 20.0f);
    CHECK(close_rect.bottom <= panel_rect.top + 64.0f);
    CHECK(panel_rect.right - close_rect.right >= 20.0f);
    CHECK(panel_rect.right - close_rect.right <= 60.0f);
    require_runtime_contract(
        rig,
        "document.querySelectorAll('[data-spectr-settings-group=\"feedback\"]')?.length === 1",
        "feedback group lost its stable materialized identity");
    require_app_state(rig, "s.settings.statusInfo !== false",
                      "status info did not default on");
    const auto* feedback_label = find_label(*rig.root, "FEEDBACK");
    const auto* status_info_label = find_label(*rig.root, "Status info");
    const auto* rulers_label = find_label(*rig.root, "Rulers");
    REQUIRE(feedback_label != nullptr);
    REQUIRE(status_info_label != nullptr);
    REQUIRE(rulers_label != nullptr);
    // The settings groups are not necessarily direct children of the scroll
    // body: the document wraps them, and Pulp's ScrollView adds no content view
    // of its own, so walking up to "child of settings_body" resolves every
    // label to the same full-height wrapper and makes any ordering comparison
    // between two groups compare a node against itself. Resolve each group as
    // the child of the two labels' lowest common ancestor instead, which is the
    // pair of siblings that actually separates them at whatever nesting depth
    // the document happens to use.
    const auto ancestry_to_body = [&](const View* node) {
        std::vector<const View*> chain;
        while (node != nullptr) {
            chain.push_back(node);
            if (node == settings_body) break;
            node = node->parent();
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    };
    const auto feedback_chain = ancestry_to_body(feedback_label);
    const auto rulers_chain = ancestry_to_body(rulers_label);
    REQUIRE_FALSE(feedback_chain.empty());
    REQUIRE_FALSE(rulers_chain.empty());
    REQUIRE(feedback_chain.front() == settings_body);
    REQUIRE(rulers_chain.front() == settings_body);
    std::size_t branch = 0;
    while (branch < feedback_chain.size() && branch < rulers_chain.size()
           && feedback_chain[branch] == rulers_chain[branch])
        ++branch;
    // A shared prefix that runs out means one label nests inside the other's
    // group, which would make "below" meaningless.
    REQUIRE(branch < feedback_chain.size());
    REQUIRE(branch < rulers_chain.size());
    const auto* feedback_group = feedback_chain[branch];
    const auto* rulers_group = rulers_chain[branch];
    REQUIRE(feedback_group != nullptr);
    REQUIRE(rulers_group != nullptr);
    REQUIRE(feedback_group != rulers_group);
    const auto feedback_rect = root_rect(*feedback_group);
    const auto rulers_rect = root_rect(*rulers_group);
    INFO("settings_body=" << root_rect(*settings_body).left << "," << root_rect(*settings_body).top
         << " " << (root_rect(*settings_body).right - root_rect(*settings_body).left) << "x" << (root_rect(*settings_body).bottom - root_rect(*settings_body).top)
         << " feedback=" << feedback_rect.left << "," << feedback_rect.top
         << " " << (feedback_rect.right - feedback_rect.left) << "x" << (feedback_rect.bottom - feedback_rect.top));
    CHECK(feedback_rect.top > rulers_rect.bottom);
    CHECK(feedback_rect.left >= panel_rect.left + 20.0f);
    CHECK(feedback_rect.right <= panel_rect.right - 20.0f);
    capture(rig, directory, "settings-top");
    auto* settings_scroll = dynamic_cast<pulp::view::ScrollView*>(
        const_cast<View*>(settings_body));
    REQUIRE(settings_scroll != nullptr);
    // The production host performs layout immediately before its first paint.
    // Drive that same boundary explicitly so automatic child-derived extent is
    // observable even when this test is run without screenshot capture.
    rig.root->layout_children();
    CHECK(settings_scroll->content_size().height
          > settings_scroll->bounds().height + 0.5f);
    settings_scroll->set_scroll(0.0f, 10000.0f);
    settle(rig.clock, 4);
    CHECK(settings_scroll->scroll_y() > 0.0f);
    capture(rig, directory, "settings-feedback");
    const pulp::view::Point sticky_close_point{
        (close_rect.left + close_rect.right) * 0.5f,
        (close_rect.top + close_rect.bottom) * 0.5f};
    CHECK(nearest_click_target(rig.root->hit_test(sticky_close_point))
          == close_target);
    require_runtime_contract(
        rig,
        "String(document.getElementById('spectr-status-info-toggle')?.getAttribute('aria-checked')) === 'true'",
        "status info toggle did not expose its enabled state");
    activate(rig, "#spectr-status-info-toggle");
    require_app_state(rig, "s.settings.statusInfo === false",
                      "status info toggle did not disable messages");
    require_runtime_contract(
        rig,
        "String(document.getElementById('spectr-status-info-toggle')?.getAttribute('aria-checked')) === 'false'",
        "status info toggle did not expose its disabled state");
    require_runtime_contract(
        rig,
        "!document.querySelector('[data-spectr-status-banner]')",
        "disabling status info left the banner painted");
    activate(rig, "#spectr-status-info-toggle");
    require_app_state(rig, "s.settings.statusInfo === true",
                      "status info toggle did not restore messages");
    require_runtime_contract(
        rig,
        "String(document.getElementById('spectr-status-info-toggle')?.getAttribute('aria-checked')) === 'true'",
        "status info toggle did not restore its enabled state");
    activate(rig, "[data-spectr-settings-close]", "pointerenter");
    require_runtime_contract(
        rig,
        "document.querySelector('[data-spectr-settings-close]')?.getAttribute('data-spectr-close-state') === 'hover'",
        "settings close did not expose hover feedback");
    activate(rig, "[data-spectr-settings-close]", "pointerdown");
    require_runtime_contract(
        rig,
        "document.querySelector('[data-spectr-settings-close]')?.getAttribute('data-spectr-close-state') === 'pressed'",
        "settings close did not expose pressed feedback");
    capture(rig, directory, "settings");
    activate(rig, "[data-spectr-setting-option=\"warm\"]");
    activate(rig, "[data-spectr-setting-toggle]");
    activate(rig, "[data-spectr-setting-slider]", "pointerdown",
             slider_press_at(0.75));
    // The slider snaps to a 0.01 step, so a tolerance this tight still pins the
    // press to one step and cannot be satisfied by a neighbouring one.
    require_app_state(rig,
        "s.settings.theme === 'warm'"
        " && Math.abs(s.settings.bloom - 0.75) < 1e-6",
        "settings controls did not update their painted state");
    // Later atlas states were captured from the same deterministic defaults as
    // home. Prove the settings are reversible, then restore those defaults
    // before continuing the single editor transaction.
    activate(rig, "[data-spectr-setting-option=\"spectral\"]");
    activate(rig, "[data-spectr-setting-toggle]");
    activate(rig, "[data-spectr-setting-slider]", "pointerdown",
             slider_press_at(1.0));
    require_app_state(rig,
        "s.settings.theme === 'spectral' && s.settings.showMinimap === true"
        " && Math.abs(s.settings.bloom - 1) < 1e-6",
        "settings controls did not restore deterministic atlas defaults");
    rig.root->simulate_click(sticky_close_point);
    settle(rig.clock, 12);
    require_home(rig);
    activate(rig, "[data-spectr-settings-open]");
    require_state(rig, "settings");
    activate(rig, "[data-spectr-settings-close]");
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    require_state(rig, "help");
    capture(rig, directory, "help");
    // Help is a persistent rail panel rather than a semantic popup. Reusing
    // its trigger is its deterministic native close interaction.
    activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    require_home(rig);

    activate(rig, "[data-spectr-filter-surface]", "contextmenu",
             R"js({clientX:660,clientY:430,offsetX:660,offsetY:430,button:2})js");
    require_state(rig, "band-context");
    capture(rig, directory, "band-context");
    activate(rig, "[data-spectr-band-action=\"mute-band\"]");
    REQUIRE(std::count_if(rig.processor.field().bands.begin(),
                          rig.processor.field().bands.begin() + 32,
                          [](const auto& band) { return band.muted; }) == 1);

    // Settings/state-atlas transitions may replace native button instances;
    // resolve the live widgets after returning home rather than retaining a
    // pre-modal pointer whose text cache was retired during reparenting.
    auto* capture_a = rig.bridge().widget("spectr-snapshot-capture-a");
    auto* capture_b = rig.bridge().widget("spectr-snapshot-capture-b");
    auto* recall_a = rig.bridge().widget("spectr-snapshot-recall-a");
    auto* recall_b = rig.bridge().widget("spectr-snapshot-recall-b");
    REQUIRE(capture_a != nullptr);
    REQUIRE(capture_b != nullptr);
    REQUIRE(recall_a != nullptr);
    REQUIRE(recall_b != nullptr);
    rig.root->layout_children();

    // Capture A from a known field, then B from a categorically different one.
    rig.processor.field().bands[3] = {-6.0f, false};
    click_each_point_exactly_once(rig, *capture_a, "A", true);
    require_app_state(rig, "s.snapshotStatus.A === true",
                      "capture A did not hydrate the painted snapshot state");
    rig.processor.field().bands[3] = {12.0f, true};
    click_each_point_exactly_once(rig, *capture_b, "B", true);
    require_app_state(rig, "s.snapshotStatus.A === true && s.snapshotStatus.B === true",
                      "capture B did not hydrate the painted snapshot state");
    require_state(rig, "snapshots-morph");
    capture(rig, directory, "snapshots-morph");

    click_each_point_exactly_once(rig, *recall_a, "▸ A", false);
    REQUIRE(rig.processor.field().bands[3].gain_db == Catch::Approx(-6.0f));
    REQUIRE_FALSE(rig.processor.field().bands[3].muted);
    click_each_point_exactly_once(rig, *recall_b, "▸ B", false);
    REQUIRE(rig.processor.field().bands[3].gain_db == Catch::Approx(12.0f));
    REQUIRE(rig.processor.field().bands[3].muted);

    // The morph control paints its own track and its own thumb, so both the
    // value it publishes and the size it draws are read back from the native
    // view tree rather than from the script shim that authored them.
    auto* morph_track = rig.bridge().widget("spectr-snapshot-morph");
    REQUIRE(morph_track != nullptr);
    rig.root->layout_children();
    REQUIRE(morph_track->bounds().width == Catch::Approx(90.0f).margin(0.5f));
    REQUIRE(morph_track->bounds().height == Catch::Approx(16.0f).margin(0.5f));

    // Snapshot A holds band 3 at -6 dB and B holds it at +12 dB, so a morph
    // value v lands the band at -6 + 18v. Every gain assertion below is that
    // one relation read back through the processor the host owns.
    const auto morph_gain_at = [](double value) {
        return Catch::Approx(-6.0 + 18.0 * value).margin(0.01);
    };
    // The thumb is a PILL -- 22x14 idle, 26x16 hovered -- so the size it is
    // resolved by is a width/height pair, not one number. A circle of either
    // size matches neither, which is what makes this a shape assertion and
    // not just a growth one.
    const auto morph_thumb_size = [&](const char* stage) {
        rig.root->layout_children();
        CAPTURE(stage);
        const View* idle = find_sized_descendant(*morph_track, 22.0f, 14.0f);
        const View* grown = find_sized_descendant(*morph_track, 26.0f, 16.0f);
        REQUIRE_FALSE((idle == nullptr && grown == nullptr));
        REQUIRE_FALSE((idle != nullptr && grown != nullptr));
        return grown != nullptr ? 26.0f : 22.0f;
    };

    // The pill must also stay INSIDE the track it is drawn on. The circle was
    // positioned with a fixed half-width margin, so at either end of the
    // travel it hung 7px outside -- at the minimum straight onto the flanking
    // "A" label. This is read back from the laid-out view tree, so it is the
    // rendered position rather than the style that asked for it.
    const auto morph_thumb_inside_track = [&](const char* stage) {
        rig.root->layout_children();
        CAPTURE(stage);
        // `bounds()` is parent-relative, so the thumb's position has to be
        // accumulated down from the track rather than compared against it
        // directly -- two rects in different spaces would compare as garbage
        // and the assertion would be measuring nothing.
        const auto track = morph_track->local_bounds();
        float bx = 0.0f;
        float by = 0.0f;
        float bw = 0.0f;
        float bh = 0.0f;
        bool found = false;
        const std::function<void(const View&, float, float)> walk =
            [&](const View& view, float ox, float oy) {
                for (std::size_t i = 0; i < view.child_count(); ++i) {
                    const auto* child = view.child_at(i);
                    const auto b = child->bounds();
                    const bool pill =
                        (std::abs(b.width - 22.0f) < 0.1f
                         && std::abs(b.height - 14.0f) < 0.1f)
                        || (std::abs(b.width - 26.0f) < 0.1f
                            && std::abs(b.height - 16.0f) < 0.1f);
                    if (pill && !found) {
                        found = true;
                        bx = ox + b.x;
                        by = oy + b.y;
                        bw = b.width;
                        bh = b.height;
                    }
                    walk(*child, ox + b.x, oy + b.y);
                }
            };
        walk(*morph_track, 0.0f, 0.0f);
        REQUIRE(found);
        CAPTURE(track.width, track.height, bx, by, bw, bh);
        REQUIRE(bx >= -0.5f);
        REQUIRE(bx + bw <= track.width + 0.5f);
        REQUIRE(by >= -0.5f);
        REQUIRE(by + bh <= track.height + 0.5f);
    };

    // Thumb-inside-track is only HALF the containment rule, and on its own it
    // is the half that hid a defect for the whole life of this control: the
    // TRACK was laid out at the flanking "A" label's own x, so a thumb
    // perfectly contained in its track still painted straight over the label
    // at value 0, and the label was invisible. The cause was the labels
    // themselves -- authored as bare inline spans, which carry no layout box
    // on this runtime, so each measured ~0 wide, sat at the row's content
    // origin, and let the next flex item start on top of it.
    //
    // So the track has to be measured against its SIBLINGS, in the wrapper's
    // own space where all three rects are comparable, and it has to be
    // measured at both thumb sizes: a clearance that holds for the 22px idle
    // pill and not the 26px hovered one is not a clearance.
    const auto morph_track_clears_labels = [&](const char* stage) {
        rig.root->layout_children();
        CAPTURE(stage);
        const View* wrapper = morph_track->parent();
        REQUIRE(wrapper != nullptr);
        const auto track = morph_track->bounds();
        REQUIRE(track.width == Catch::Approx(90.0f).margin(0.5f));
        std::size_t flanking = 0;
        for (std::size_t i = 0; i < wrapper->child_count(); ++i) {
            const auto* sibling = wrapper->child_at(i);
            if (sibling == morph_track) continue;
            ++flanking;
            const auto b = sibling->bounds();
            // A label collapsed to nothing would clear the track trivially and
            // report a healthy gap while being invisible on screen, so its
            // width is asserted before its distance is.
            CAPTURE(i, b.x, b.width, track.x, track.width);
            REQUIRE(b.width > 0.0f);
            // Which side a label is on is decided by its CENTRE, not its
            // left edge: in the defect the two left edges were equal, and an
            // edge test then called the "A" label a right-hand neighbour and
            // reported a -90px gap instead of the -5.4px overlap it is.
            const float gap = b.x + b.width * 0.5f < track.x + track.width * 0.5f
                ? track.x - (b.x + b.width)
                : b.x - (track.x + track.width);
            CAPTURE(gap);
            REQUIRE(gap >= 2.0f);
        }
        // Positive control. A wrapper the tree never built, or one whose
        // labels are gone, leaves the loop with nothing compared -- which
        // passes every assertion above by never reaching one.
        REQUIRE(flanking == 2);
    };

    // Idle first: this reading is the positive control for the two that
    // follow. A thumb the view tree never drew would report neither size and
    // trip the require above instead of silently agreeing with every stage.
    REQUIRE(morph_thumb_size("idle") == 22.0f);
    morph_thumb_inside_track("idle");
    morph_track_clears_labels("idle");
    activate(rig, "[data-spectr-morph]", "pointerenter",
             slider_press_at(0.5, "[data-spectr-morph]"));
    REQUIRE(morph_thumb_size("hovered") == 26.0f);
    morph_thumb_inside_track("hovered");
    morph_track_clears_labels("hovered");

    // A move with no preceding press is inert: the control tracks the pointer
    // only while it holds the capture the press gave it.
    const auto gain_before_morph = rig.processor.field().bands[3].gain_db;
    activate(rig, "[data-spectr-morph]", "pointermove",
             slider_press_at(0.9, "[data-spectr-morph]"));
    REQUIRE(rig.processor.field().bands[3].gain_db
            == Catch::Approx(gain_before_morph).margin(0.01));

    const auto revision_before_morph = rig.processor.native_editor_revision();
    // The morph control derives its value from where the pointer landed, so
    // press it the way a person would rather than feeding a value straight to
    // a handler.
    activate(rig, "[data-spectr-morph]", "pointerdown",
             slider_press_at(0.5, "[data-spectr-morph]"));
    REQUIRE(rig.processor.native_editor_revision() == revision_before_morph + 1);
    REQUIRE(rig.processor.field().bands[3].gain_db == morph_gain_at(0.5));
    REQUIRE(rig.processor.field().bands[3].muted);

    // The value follows the pointer in both directions across the drag, so a
    // handler that only re-read the press point would fail here.
    activate(rig, "[data-spectr-morph]", "pointermove",
             slider_press_at(0.25, "[data-spectr-morph]"));
    REQUIRE(rig.processor.field().bands[3].gain_db == morph_gain_at(0.25));
    activate(rig, "[data-spectr-morph]", "pointermove",
             slider_press_at(0.75, "[data-spectr-morph]"));
    REQUIRE(rig.processor.field().bands[3].gain_db == morph_gain_at(0.75));

    // Leaving the control mid-drag must not end the drag or shrink the thumb:
    // the pointer is still captured, so the gesture continues off the track.
    activate(rig, "[data-spectr-morph]", "pointerleave",
             slider_press_at(1.4, "[data-spectr-morph]"));
    REQUIRE(morph_thumb_size("left-while-dragging") == 26.0f);
    activate(rig, "[data-spectr-morph]", "pointermove",
             slider_press_at(0.9, "[data-spectr-morph]"));
    REQUIRE(rig.processor.field().bands[3].gain_db == morph_gain_at(0.9));

    // Losing the capture ends the drag. Moves after it are inert again, and
    // the next leave is finally free to restore the idle thumb.
    activate(rig, "[data-spectr-morph]", "lostpointercapture",
             slider_press_at(0.9, "[data-spectr-morph]"));
    activate(rig, "[data-spectr-morph]", "pointermove",
             slider_press_at(0.1, "[data-spectr-morph]"));
    REQUIRE(rig.processor.field().bands[3].gain_db == morph_gain_at(0.9));
    REQUIRE(morph_thumb_size("still-hovered-after-release") == 26.0f);
    activate(rig, "[data-spectr-morph]", "pointerleave",
             slider_press_at(1.4, "[data-spectr-morph]"));
    REQUIRE(morph_thumb_size("left-after-release") == 22.0f);
    // At the far end of the travel, which is where the circle hung 7px past
    // the track onto the "B" label.
    activate(rig, "[data-spectr-morph]", "pointerdown",
             slider_press_at(1.0, "[data-spectr-morph]"));
    activate(rig, "[data-spectr-morph]", "pointerup",
             slider_press_at(1.0, "[data-spectr-morph]"));
    morph_thumb_inside_track("at-maximum");

    // Return the morph to the midpoint the rest of this case expects, and
    // prove the ordinary release ends the drag the way losing the capture did.
    activate(rig, "[data-spectr-morph]", "pointerdown",
             slider_press_at(0.5, "[data-spectr-morph]"));
    activate(rig, "[data-spectr-morph]", "pointerup",
             slider_press_at(0.5, "[data-spectr-morph]"));
    activate(rig, "[data-spectr-morph]", "pointermove",
             slider_press_at(0.1, "[data-spectr-morph]"));
    REQUIRE(rig.processor.field().bands[3].gain_db == morph_gain_at(0.5));
    REQUIRE(rig.processor.field().bands[3].muted);

    // Native UI save and rename update the processor-owned library before the
    // plugin blob is serialized. No browser-local storage participates.
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-save-current]");
    require_state(rig, "save-dialog");
    capture(rig, directory, "save-dialog");
    require_app_state(rig, "s.saveDialogOpen === true",
                      "save command did not open its native dialog");
    REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
        *rig.root, static_cast<int>(pulp::view::KeyCode::escape),
        pulp::view::kModNone, true));
    settle(rig.clock, 8);
    require_app_state(rig, "s.saveDialogOpen === false",
                      "Escape left the save dialog open");
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-save-current]");
    require_state(rig, "save-dialog");
    activate(rig, "#spectr-save-name", "change",
             R"js({value:'STATE ATLAS MASK',target:{value:'STATE ATLAS MASK'},currentTarget:{value:'STATE ATLAS MASK'}})js");
    activate(rig, "[data-spectr-manager-action=\"save-submit\"]");
    REQUIRE(rig.processor.patterns().user().size() == 1);
    REQUIRE(rig.processor.patterns().user().front().name == "STATE ATLAS MASK");
    require_app_state(rig,
        "s.userPatterns.length === 1"
        " && s.userPatterns[0].name === 'STATE ATLAS MASK'"
        " && s.saveDialogOpen === false",
        "saved preset did not hydrate the native pattern list");
    storage.require_unchanged();
    const auto pattern_id = rig.processor.patterns().user().front().id;

    // The menu item removes its own captured subtree while its click callback
    // opens the manager. Exercise the real down/up dispatcher repeatedly: the
    // semantic atlas driver cannot detect callback-lifetime regressions here.
    for (int cycle = 0; cycle < 8; ++cycle) {
        INFO("native self-removing manager click cycle " << cycle);
        activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
        native_click_label_prefix(rig, "MANAGE…");
        require_app_state(rig, "s.managerOpen === true",
                          "native pointer did not open pattern manager");
        native_click_label(rig, "×");
        require_app_state(rig, "s.managerOpen === false",
                          "native pointer did not close pattern manager");
    }
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    native_click_label_prefix(rig, "MANAGE…");
    require_app_state(rig,
        "s.managerOpen === true && s.userPatterns.length === 1",
        "saved preset was not available in the native pattern manager");
    activate(rig, "[data-spectr-pattern-id=\"factory:tilt\"]");
    settle(rig.clock, 4);
    rig.bridge().load_script(R"js((() => {
      const manager = document.querySelector(
        '[data-spectr-overlay="true"][aria-label="Pattern manager"]');
      if (!manager) throw new Error('pattern manager overlay missing');
      const heading = document.querySelector('[data-spectr-manager-heading]');
      const title = document.querySelector('[data-spectr-manager-title]');
      const source = document.querySelector('[data-spectr-manager-source]');
      const preview = document.querySelector('[data-spectr-manager-preview]');
      const bands = document.querySelector('[data-spectr-manager-meta]');
      const actionRow = document.querySelector('[data-spectr-manager-actions]');
      const actions = ['apply', 'set-default', 'duplicate',
        'export-file', 'export-clip'];
      const labels = ['APPLY', 'SET AS DEFAULT', 'DUPLICATE',
        'EXPORT (FILE)', 'EXPORT (CLIP)'];
      const buttons = actions.map(action => document.querySelector(
        '[data-spectr-manager-action="' + action + '"]'));
      if (!heading || !title || !source || !preview || !bands || !actionRow
          || buttons.some(button => !button))
        throw new Error('selected preset detail subjects missing');
      const managerRect = manager.getBoundingClientRect();
      const headingRect = heading.getBoundingClientRect();
      const titleRect = title.getBoundingClientRect();
      const sourceRect = source.getBoundingClientRect();
      const previewRect = preview.getBoundingClientRect();
      const bandsRect = bands.getBoundingClientRect();
      const actionRect = actionRow.getBoundingClientRect();
      const buttonRects = buttons.map(button => button.getBoundingClientRect());
      if (headingRect.width <= 0 || previewRect.width <= 0 || bandsRect.width <= 0
          || actionRect.width <= 0)
        throw new Error('selected preset detail collapsed');
      if (titleRect.right > sourceRect.left + 0.5)
        throw new Error('selected preset source badge overlaps its title');
      if (headingRect.bottom > previewRect.top + 0.5
          || previewRect.bottom > bandsRect.top + 0.5
          || bandsRect.bottom > actionRect.top + 0.5)
        throw new Error('selected preset detail vertical order collapsed');
      for (const rect of buttonRects) {
        if (rect.width < 50 || rect.height < 25)
          throw new Error('selected preset action collapsed');
        if (rect.left < managerRect.left - 0.5 || rect.right > managerRect.right + 0.5
            || rect.top < managerRect.top - 0.5 || rect.bottom > managerRect.bottom + 0.5)
          throw new Error('selected preset action escaped the manager');
      }
      for (let a = 0; a < buttonRects.length; ++a) {
        for (let b = a + 1; b < buttonRects.length; ++b) {
          const xOverlap = Math.min(buttonRects[a].right, buttonRects[b].right)
            - Math.max(buttonRects[a].left, buttonRects[b].left);
          const yOverlap = Math.min(buttonRects[a].bottom, buttonRects[b].bottom)
            - Math.max(buttonRects[a].top, buttonRects[b].top);
          if (xOverlap > 0.5 && yOverlap > 0.5)
            throw new Error('selected preset actions overlap: '
              + labels[a] + ' / ' + labels[b]);
        }
      }
    })();)js", "spectr-native-pattern-manager-selected-layout");
    settle(rig.clock, 4);
    std::vector<const pulp::view::SvgRectWidget*> preview_rects;
    collect_svg_rects(*rig.root, preview_rects);
    const auto distributed_preview_bars = std::count_if(
        preview_rects.begin(), preview_rects.end(), [](const auto* rect) {
            return rect->rect_x() > 1.0f && rect->rect_width() >= 1.0f
                && rect->rect_height() > 0.0f;
        });
    const auto viewport_sized_preview_bars = std::count_if(
        preview_rects.begin(), preview_rects.end(), [](const auto* rect) {
            return rect->rect_x() > 1.0f && rect->bounds().width >= 55.0f
                && rect->bounds().height >= 21.0f;
        });
    const auto visibly_tall_preview_bars = std::count_if(
        preview_rects.begin(), preview_rects.end(), [](const auto* rect) {
            return rect->rect_x() > 1.0f && rect->rect_height() >= 4.0f
                && rect->bounds().width >= 55.0f;
        });
    CAPTURE(preview_rects.size(), distributed_preview_bars,
            viewport_sized_preview_bars, visibly_tall_preview_bars);
    REQUIRE(distributed_preview_bars >= 16);
    REQUIRE(viewport_sized_preview_bars >= 16);
    REQUIRE(visibly_tall_preview_bars >= 40);
    const auto preview_geometry_signature = [](const std::vector<const pulp::view::SvgRectWidget*>& rects) {
        std::ostringstream signature;
        for (const auto* rect : rects) {
            if (rect->rect_x() <= 1.0f || rect->bounds().width < 55.0f)
                continue;
            signature << rect->rect_x() << ':' << rect->rect_height() << ';';
        }
        return signature.str();
    };
    const auto initial_preview_geometry = preview_geometry_signature(preview_rects);
    REQUIRE_FALSE(initial_preview_geometry.empty());
    rig.bridge().load_script(R"js((() => {
      const title = document.querySelector('[data-spectr-manager-title]');
      const preview = document.querySelector('[data-spectr-manager-preview]');
      if (!title || !preview || title.textContent !== 'DOWNWARD TILT'
          || title.getAttribute('data-spectr-pattern-id') !== 'factory:tilt'
          || preview.getAttribute('data-spectr-pattern-id') !== 'factory:tilt')
        throw new Error('initial selected preset title/SVG identity was incoherent: '
          + JSON.stringify({title:title?.textContent, titleId:title?.getAttribute?.('data-spectr-pattern-id'),
             previewId:preview?.getAttribute?.('data-spectr-pattern-id')}));
    })();)js", "spectr-native-pattern-selection-initial");
    settle(rig.clock, 2);
    activate(rig, "[data-spectr-pattern-id=\"factory:flat\"]");
    settle_until_contract(
        rig,
        "(() => { const title = document.querySelector('[data-spectr-manager-title]');"
        " const preview = document.querySelector('[data-spectr-manager-preview]');"
        " const signature = Array.from(preview?.querySelectorAll('svg rect') || [])"
        ".map(rect => rect.getAttribute('y') + ':' + rect.getAttribute('height')).join('|');"
        " if (!(title?.textContent.endsWith('FLAT')"
        " && title?.getAttribute('data-spectr-pattern-id') === 'factory:flat'"
        " && preview?.getAttribute('data-spectr-pattern-id') === 'factory:flat'))"
        " throw new Error('flat state: ' + JSON.stringify({title:title?.textContent," 
        "titleId:title?.getAttribute?.('data-spectr-pattern-id'),previewId:preview?.getAttribute?.('data-spectr-pattern-id')," 
        "signatureLength:signature.length})); return true; })()",
        "selected preset name and SVG did not update in the same committed identity");
    std::vector<const pulp::view::SvgRectWidget*> flat_preview_rects;
    collect_svg_rects(*rig.root, flat_preview_rects);
    const auto flat_preview_geometry = preview_geometry_signature(flat_preview_rects);
    REQUIRE_FALSE(flat_preview_geometry.empty());
    REQUIRE(flat_preview_geometry != initial_preview_geometry);
    activate(rig, "[data-spectr-pattern-id=\"factory:tilt\"]");
    settle_until_contract(
        rig,
        "document.querySelector('[data-spectr-manager-title]')?."
        "getAttribute('data-spectr-pattern-id') === 'factory:tilt'",
        "selected preset did not restore before the frozen manager capture");
    capture(rig, directory, "pattern-manager");
    activate(rig, "[data-spectr-pattern-id=" + js_string(pattern_id) + "]");
    // Selecting the row is itself a React commit. Clicking rename-start before it
    // lands targets a node that does not exist yet, so the click is swallowed and
    // the rename never starts - and then no amount of waiting for the input can
    // succeed. This raced invisibly under JIT-compiled JSC and reproduces under
    // QuickJS. Wait for the control before driving it.
    settle_until_contract(
        rig,
        "typeof globalThis.__pulpFindMaterializedElement__ === 'function'"
        " && !!globalThis.__pulpFindMaterializedElement__("
        "'[data-spectr-manager-action=\"rename-start\"]')",
        "pattern rename-start control did not mount");
    // Entering rename replaces the selected row with a controlled input in a
    // follow-up React commit. Give that commit its own host-frame service
    // window before targeting the new node; otherwise a heavily loaded host
    // can make the semantic driver race the mount it just requested -- and if
    // the click itself lost the race, re-drive it rather than waiting longer.
    activate_until_contract(
        rig, "[data-spectr-manager-action=\"rename-start\"]",
        "typeof globalThis.__pulpFindMaterializedElement__ === 'function'"
        " && !!globalThis.__pulpFindMaterializedElement__('#spectr-manager-rename')",
        "pattern rename input did not mount");
    activate(rig, "#spectr-manager-rename", "change",
             R"js({value:'FLAT',target:{value:'FLAT'},currentTarget:{value:'FLAT'}})js");
    activate(rig, "#spectr-manager-rename", "blur");
    // blur commits the rename through React and then into the processor; neither
    // hop is synchronous with the event.
    settle_until(rig, [&] {
        const auto& user = rig.processor.patterns().user();
        return !user.empty() && user.front().name == "FLAT";
    });
    REQUIRE(rig.processor.patterns().user().front().name == "FLAT");
    storage.require_unchanged();

    const auto plugin_state = rig.processor.serialize_plugin_state();
    rig.close();

    NativeEditorRig reopened(plugin_state);
    REQUIRE(reopened.processor.patterns().user().size() == 1);
    REQUIRE(reopened.processor.patterns().user().front().name == "FLAT");
    require_app_state(reopened,
        "s.userPatterns.length === 1 && s.userPatterns[0].name === 'FLAT'"
        " && s.snapshotStatus.A === true && s.snapshotStatus.B === true",
        "reopened native UI did not hydrate patterns and snapshots");
    storage.require_unchanged();
    reopened.bridge().load_script(R"js((() => {
      const r = globalThis.__spectrTestHooks?.renderState?.();
      const finiteOrMuted = value => Number.isFinite(value) || value === -Infinity;
      if (!r || !r.gains.every(finiteOrMuted)
          || !r.mutedGainDb.every(Number.isFinite)
          || !r.targetGains.every(finiteOrMuted)
          || !r.unmutePulse.every(Number.isFinite)
          || !Number.isFinite(r.view?.lmin) || !Number.isFinite(r.view?.lmax)
          || !(r.view.lmax > r.view.lmin))
        throw new Error('reopened native render state was non-finite: '
          + JSON.stringify(r));
    })();)js", "spectr-native-reopen-finite");
    capture(reopened, directory, "reopened");

    const auto invalid_pattern = corrupt_first_pattern_gain(plugin_state);
    REQUIRE_FALSE(reopened.processor.deserialize_plugin_state(invalid_pattern));
    REQUIRE(reopened.processor.patterns().user().front().name == "FLAT");
    const auto invalid_field = corrupt_first_gain(plugin_state);
    REQUIRE_FALSE(reopened.processor.deserialize_plugin_state(invalid_field));
    REQUIRE(reopened.processor.patterns().user().front().name == "FLAT");
    reopened.close();
    reopened.open();
    require_app_state(reopened,
        "s.userPatterns.length === 1 && s.userPatterns[0].name === 'FLAT'",
        "nonfinite rejection did not preserve last-good native reopen state");
    storage.require_unchanged();

    activate(reopened, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(reopened, "[data-spectr-pattern-manage]");
    activate(reopened, "[data-spectr-pattern-id=" + js_string(pattern_id) + "]");
    // This used to inject `globalThis.confirm = () => true` before pressing
    // DELETE, which is why it passed while DELETE did nothing for users: the
    // handler's only statement called `confirm`, the Pulp scripted-UI runtime
    // defines no such global, and the shim supplied the one thing the product
    // was missing. The confirmation is now a panel the runtime can paint, so
    // the test presses it the way a user does.
    activate(reopened, "[data-spectr-manager-action=\"delete\"]");
    activate(reopened, "[data-spectr-manager-action=\"delete-confirm\"]");
    REQUIRE(reopened.processor.patterns().user().empty());
    storage.require_unchanged();

    const auto deleted_state = reopened.processor.serialize_plugin_state();
    NativeEditorRig deleted_reopen(deleted_state);
    REQUIRE(deleted_reopen.processor.patterns().user().empty());
    require_app_state(deleted_reopen, "s.userPatterns.length === 0",
                      "deleted preset reappeared after native reopen");
    storage.require_unchanged();
}

// Every button must be tappable across its whole painted area, at every host
// size — issue #39, reported twice from Logic. Two separate invariants, because
// two different layers can break the promise:
//
//  1. Paint must not spill outside the hit box. Pulp hit-tests a view's box; a
//     glyph run wider than its button is visible ink with no hit region behind
//     it. The 2px slack absorbs the 1px border a captured chip paints on its
//     own edge, and nothing larger.
//  2. Every point inside the hit box must resolve to that control. Hit testing
//     returns the deepest painted node (a label or an icon), so the walk to the
//     nearest interactive ancestor has to succeed from anywhere in the box.
//
// Then one end-to-end sweep at the host's preferred size proves a synthesized
// click at those points actually reaches the application, not just the view.
TEST_CASE("native buttons are tappable across their whole painted bounds",
          "[native-n1][tap-targets]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    constexpr float kBorderSlack = 2.0f;

    for (const auto& size : std::array<std::pair<int, int>, 4>{
             std::pair{792, 516}, std::pair{990, 645},
             std::pair{1320, 860}, std::pair{2640, 1720}}) {
        rig.resize(static_cast<float>(size.first),
                   static_cast<float>(size.second));
        // QuickJS needs more host frames than a JIT engine to finish the React
        // commit this resize schedules; assert on the settled tree.
        settle(rig.clock, 96);
        INFO("host size " << size.first << 'x' << size.second);
        require_no_rejected_layout_boxes(rig);

        std::vector<const View*> controls;
        collect_click_targets(*rig.root, controls);
        REQUIRE(controls.size() >= 15);

        for (const auto* control : controls) {
            const auto box = root_rect(*control);
            if (box.right - box.left <= 0.0f || box.bottom - box.top <= 0.0f)
                continue;
            INFO("control " << describe_control(*control));
            const auto painted = painted_extent(*control);
            CAPTURE(painted.left, painted.top, painted.right, painted.bottom,
                    box.left, box.top, box.right, box.bottom);
            CHECK(painted.left >= box.left - kBorderSlack);
            CHECK(painted.top >= box.top - kBorderSlack);
            CHECK(painted.right <= box.right + kBorderSlack);
            CHECK(painted.bottom <= box.bottom + kBorderSlack);

            for (const auto& point : box_probe_points(*control)) {
                CAPTURE(point.x, point.y);
                auto* hit = rig.root->hit_test(point);
                REQUIRE(hit != nullptr);
                INFO("hit " << describe_control(*hit));
                const auto* resolved = nearest_click_target(hit);
                INFO("resolved "
                     << (resolved ? describe_control(*resolved)
                                  : std::string("<none>")));
                CHECK(resolved == control);
            }
        }
    }

    // Logic opens the editor at the preferred size, which is where the reported
    // dead zones were. Prove the whole box is live end to end there.
    rig.resize(990, 645);
    settle(rig.clock, 96);
    install_click_dispatch_counter(rig);
    std::vector<const View*> controls;
    collect_click_targets(*rig.root, controls);
    REQUIRE(controls.size() >= 15);
    std::vector<std::string> control_ids;
    control_ids.reserve(controls.size());
    for (const auto* control : controls) control_ids.push_back(control->id());
    const std::function<const View*(const View&, std::string_view)> find_by_id =
        [&](const View& view, std::string_view id) -> const View* {
          if (view.id() == id) return &view;
          for (std::size_t index = 0; index < view.child_count(); ++index)
              if (const auto* match = find_by_id(*view.child_at(index), id))
                  return match;
          return nullptr;
        };
    for (const auto& control_id : control_ids) {
        const auto* initial = find_by_id(*rig.root, control_id);
        REQUIRE(initial != nullptr);
        if (initial->bounds().width <= 0.0f || initial->bounds().height <= 0.0f)
            continue;
        INFO("control " << describe_control(*initial));
        // A grid over the whole painted box -- corners, edges and interior --
        // not a single row. The earlier sweep clicked only the vertical middle
        // (left edge, centre, right edge), so a band along the top or bottom
        // of a control could go dead without this test noticing, which is
        // exactly the shape of the snapshot A/B report.
        for (const float fy : {0.0f, 0.5f, 1.0f})
        for (const float fx : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            if (rig.root->interaction().active_overlay != nullptr) {
                pulp::view::View::dismiss_active_overlay(*rig.root);
                settle(rig.clock, 12);
            }
            const auto* control = find_by_id(*rig.root, control_id);
            REQUIRE(control != nullptr);
            // Aim at the PAINTED extent, in root space, because that is what
            // the user aims at. Re-resolve after dismissing an overlay because
            // that React commit may replace a live view object. Stay 1.5 px
            // inside every edge so a sample is unambiguously on the ink.
            const auto painted = painted_extent(*control);
            const float x = painted.left + 1.5f
                            + (painted.right - painted.left - 3.0f) * fx;
            const float y = painted.top + 1.5f
                            + (painted.bottom - painted.top - 3.0f) * fy;
            CAPTURE(x, y, painted.left, painted.top, painted.right,
                    painted.bottom);
            const auto before = click_dispatch_count(rig);
            rig.root->simulate_click({x, y});
            settle(rig.clock, 12);
            // EXACTLY one, not "at least one". A single native click must reach
            // the application once: the bridge stamps a __pulpDispatchToken on
            // every pointer payload for de-duplication, but nothing on the JS
            // side reads it, so a double delivery here would double-apply a
            // parameter edit with no backstop.
            CHECK(click_dispatch_count(rig) == before + 1);
        }
    }
}

// The same whole-box promise on every surface the editor can open, not only
// the home screen: each dropdown, the band context menu, Settings (scrolled
// through its whole body), the help rail and the preset manager. A menu row or
// a Settings toggle is a control too, and the home-screen sweep above never
// sees one because none of them is mounted until its surface opens.
//
// This resolves a press over a 5x5 grid of each control's box the way the
// hosts do (an open overlay first, then the tree), and asks whether the view
// it lands on is the control or something the control owns. It deliberately
// does not click: a click would activate the row and close the surface under
// the sweep. The home-screen case above proves the resolution-to-dispatch hop.
TEST_CASE("every control on every editor surface resolves a press anywhere in its painted box",
          "[native-n1][tap-targets]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    rig.resize(990, 645);
    settle(rig.clock, 96);
    constexpr float kBorderSlack = 2.0f;

    const std::function<const View*(const View&, std::string_view)> find_view_id =
        [&](const View& view, std::string_view id) -> const View* {
          if (view.id() == id) return &view;
          for (std::size_t index = 0; index < view.child_count(); ++index)
              if (const auto* match = find_view_id(*view.child_at(index), id))
                  return match;
          return nullptr;
        };
    // The native view behind a selector, read back the way this file reads
    // runtime state: throw it and parse the message.
    const auto native_view_for = [&](std::string_view selector) -> const View* {
        std::string id;
        try {
            rig.bridge().load_script(
                std::string{"(() => { const n = globalThis.__pulpFindMaterializedElement__("}
                    + js_string(selector) + "); throw new Error('NATIVEID:' + "
                    "(n ? (n.__pulpId || n.id || '') : '') + ':END'); })();",
                "spectr-native-view-for-selector");
        } catch (const std::exception& error) {
            const std::string message = error.what();
            const auto begin = message.find("NATIVEID:");
            const auto end = message.find(":END");
            if (begin != std::string::npos && end != std::string::npos)
                id = message.substr(begin + 9, end - begin - 9);
        }
        return id.empty() ? nullptr : find_view_id(*rig.root, id);
    };
    // Controls a surface owns, named by what the document says is a control
    // (a <button>, an input, or an element with a control role) rather than by
    // which native views carry a click handler: the bridge arms a handler on
    // captions and panels too, and those are not things a user aims at. With
    // an overlay open only its subtree can take a press; a modal that is not
    // an overlay (Settings) names its panel explicitly.
    const auto semantic_control_ids = [&] {
        std::vector<std::string> ids;
        try {
            rig.bridge().load_script(R"js((() => {
              const selectors = ['button', 'input', '[role="option"]',
                '[role="slider"]', '[role="switch"]', '[role="menuitem"]',
                '[role="menuitemradio"]', '[role="menuitemcheckbox"]',
                '[role="tab"]', '[role="spinbutton"]', '[data-spectr-setting-slider]',
                '[data-spectr-menu-trigger]'];
              const ids = new Set();
              for (const selector of selectors)
                for (const node of document.querySelectorAll(selector)) {
                  const id = node && (node.__pulpId || node.id);
                  if (id) ids.add(String(id));
                }
              throw new Error('CONTROLIDS:' + Array.from(ids).join('|') + ':END');
            })();)js", "spectr-native-semantic-controls");
        } catch (const std::exception& error) {
            const std::string message = error.what();
            const auto begin = message.find("CONTROLIDS:");
            const auto end = message.find(":END");
            if (begin != std::string::npos && end != std::string::npos) {
                std::stringstream list(message.substr(begin + 11, end - begin - 11));
                for (std::string id; std::getline(list, id, '|');)
                    if (!id.empty()) ids.push_back(id);
            }
        }
        return ids;
    };
    const auto is_within = [](const View& view, const View& ancestor) {
        for (const auto* node = &view; node != nullptr; node = node->parent())
            if (node == &ancestor) return true;
        return false;
    };
    const auto surface_controls = [&](const View* explicit_scope) {
        const View* scope = explicit_scope ? explicit_scope
                                           : rig.root->interaction().active_overlay;
        std::vector<const View*> controls;
        for (const auto& id : semantic_control_ids()) {
            const auto* view = find_view_id(*rig.root, id);
            if (view == nullptr || !chain_interactive(*view)) continue;
            if (scope != nullptr && !is_within(*view, *scope)) continue;
            controls.push_back(view);
        }
        return controls;
    };

    int surfaces = 0;
    std::size_t probed = 0;
    std::vector<std::string> dead;
    // Resolve a press the way every host does (route_press_to_active_overlay,
    // then the tree): an open overlay that paints over the point takes it, hit
    // tested from the overlay itself. A bare root hit_test would instead cull
    // a popover that reaches far from the trigger it hangs off, which no real
    // press ever sees. Resolution only: nothing is dismissed or delivered.
    const auto press_target = [&](pulp::view::Point point) -> View* {
        auto* overlay = rig.root->interaction().active_overlay;
        if (overlay != nullptr && overlay->overlay_contains(point)) {
            if (auto* hit = overlay->hit_test(
                    pulp::view::point_to_local(point, overlay, rig.root.get())))
                return hit;
        }
        return rig.root->hit_test(point);
    };
    // A point another view PAINTS over is not the control's to answer: a
    // sticky menu footer over the last scrolled row, a modal over the editor.
    // That holds only when something between the interceptor and the nearest
    // shared ancestor lays down an opaque-enough fill; a transparent sibling
    // (a caption, a layout wrapper) over a control is the defect this hunts.
    const auto occluded_by_paint = [&](const View& hit, const View& control) {
        for (const auto* node = &hit; node != nullptr; node = node->parent()) {
            if (is_within(control, *node)) return false;  // shared ancestor
            if (node->has_background_color() && node->background_color().a >= 0.5f)
                return true;
        }
        return false;
    };
    int occluded = 0;
    // Every control the sweep probed at least once, so a case can prove a
    // control it names was really reached.
    std::vector<const View*> swept;
    const auto directory = atlas_directory();
    // `clip`: a list that scrolls by moving its rows inside a clipping
    // viewport (the LENGTH menu, the Fraction list) paints only there, so a
    // sample outside it is not a press on that row.
    const auto sweep = [&](std::string_view surface,
                           const View* scope = nullptr,
                           std::optional<RootRect> clip = std::nullopt) {
        settle(rig.clock, 24);
        ++surfaces;
        {
            std::string name{"tap-sweep-"};
            for (const char ch : surface) name.push_back(ch == ' ' ? '-' : ch);
            capture(rig, directory, name);
        }
        const auto controls = surface_controls(scope);
        INFO("surface " << surface);
        CHECK_FALSE(controls.empty());
        for (const auto* control : controls) {
            const auto box = root_rect(*control);
            if (box.right - box.left < 1.0f || box.bottom - box.top < 1.0f)
                continue;
            // Clipped out of its scroll viewport or off the editor: not a
            // painted control at this scroll position.
            if (box.right <= 0.0f || box.bottom <= 0.0f || box.left >= 1320.0f
                || box.top >= 860.0f)
                continue;
            if (clip && (box.bottom <= clip->top || box.top >= clip->bottom
                         || box.right <= clip->left || box.left >= clip->right))
                continue;
            const auto painted = painted_extent(*control);
            INFO("control " << describe_control(*control));
            CAPTURE(painted.left, painted.top, painted.right, painted.bottom);
            CHECK(painted.left >= box.left - kBorderSlack);
            CHECK(painted.top >= box.top - kBorderSlack);
            CHECK(painted.right <= box.right + kBorderSlack);
            CHECK(painted.bottom <= box.bottom + kBorderSlack);
            int misses = 0;
            std::string first_miss;
            for (const float fy : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            for (const float fx : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                // Sample the hit box's interior, 1 px in from every edge.
                const pulp::view::Point point{box.left + 1.0f + (box.right - box.left - 2.0f) * fx,
                                  box.top + 1.0f + (box.bottom - box.top - 2.0f) * fy};
                // Clipped by an owning scroll viewport: not painted there. A
                // ScrollView's root rect moves with its own offset, so add the
                // offset back to get the fixed viewport.
                if (const auto* scroll = owning_scroll_view(*control)) {
                    auto viewport = root_rect(*scroll);
                    viewport.left += scroll->scroll_x();
                    viewport.right += scroll->scroll_x();
                    viewport.top += scroll->scroll_y();
                    viewport.bottom += scroll->scroll_y();
                    if (point.y < viewport.top || point.y > viewport.bottom
                        || point.x < viewport.left || point.x > viewport.right)
                        continue;
                }
                if (clip && (point.y < clip->top || point.y > clip->bottom
                             || point.x < clip->left || point.x > clip->right))
                    continue;
                if (swept.empty() || swept.back() != control) swept.push_back(control);
                ++probed;
                auto* hit = press_target(point);
                const auto* resolved = nearest_click_target(hit);
                // The press is the control's when the hit lands on it or on
                // something it owns (a slider's track, a button's caption).
                if (hit != nullptr && is_within(*hit, *control)) continue;
                if (hit != nullptr && occluded_by_paint(*hit, *control)) {
                    ++occluded;
                    continue;
                }
                if (++misses == 1) {
                    std::ostringstream miss;
                    const auto* caption = [&]() -> const pulp::view::Label* {
                        std::function<const pulp::view::Label*(const View&)> first =
                            [&](const View& v) -> const pulp::view::Label* {
                              if (auto* l = dynamic_cast<const pulp::view::Label*>(&v);
                                  l && !l->text().empty()) return l;
                              for (std::size_t i = 0; i < v.child_count(); ++i)
                                  if (auto* m = first(*v.child_at(i))) return m;
                              return nullptr;
                            };
                        return first(*control);
                    }();
                    miss << std::string(surface) << ": " << describe_control(*control)
                         << " '" << (caption ? caption->text() : std::string()) << "'"
                         << " dead at (" << point.x << ',' << point.y << ") -> hit "
                         << (hit ? describe_control(*hit) : std::string("<none>"))
                         << " resolved "
                         << (resolved ? describe_control(*resolved) : std::string("<none>"));
                    first_miss = miss.str();
                }
            }
            if (misses > 0)
                dead.push_back(first_miss + " (" + std::to_string(misses) + "/25)");
        }
    };
    const auto close_overlay = [&] {
        if (rig.root->interaction().active_overlay != nullptr) {
            pulp::view::View::dismiss_active_overlay(*rig.root);
            settle(rig.clock, 12);
        }
    };

    sweep("home");

    for (const char* menu : {"analyzer", "bands", "edit", "overflow", "pattern"}) {
        activate(rig, std::string("[data-spectr-menu-root=\"") + menu
                          + "\"] [data-spectr-menu-trigger]");
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
        sweep(std::string("menu ") + menu);
        close_overlay();
    }

    activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    sweep("help rail");
    activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    require_home(rig);

    activate(rig, "[data-spectr-filter-surface]", "contextmenu",
             R"js({clientX:660,clientY:430,offsetX:660,offsetY:430,button:2})js");
    require_state(rig, "band-context");
    sweep("band context menu");
    close_overlay();

    activate(rig, "[data-spectr-settings-open]");
    const auto* settings_panel = native_view_for("[data-spectr-settings-panel]");
    REQUIRE(settings_panel != nullptr);
    sweep("settings", settings_panel);
    // The Settings body scrolls; sweep the rows below the fold too.
    for (int step = 0; step < 6; ++step) {
        std::vector<pulp::view::ScrollView*> scrolls;
        const std::function<void(View&)> find = [&](View& view) {
            if (auto* scroll = dynamic_cast<pulp::view::ScrollView*>(&view))
                scrolls.push_back(scroll);
            for (std::size_t index = 0; index < view.child_count(); ++index)
                find(*view.child_at(index));
        };
        find(*rig.root);
        bool moved = false;
        for (auto* scroll : scrolls) {
            const auto before = scroll->scroll_y();
            scroll->set_scroll(0.0f, before + 300.0f);
            moved = moved || scroll->scroll_y() != before;
        }
        if (!moved) break;
        rig.root->layout_children();
        sweep("settings scrolled " + std::to_string(step + 1),
              native_view_for("[data-spectr-settings-panel]"));
    }
    activate(rig, "[data-spectr-settings-close]");
    close_overlay();

    // LENGTH. The trigger is a home-surface control; the home sweep above
    // must have reached it.
    {
        const auto* trigger = native_view_for("[data-spectr-length-trigger]");
        REQUIRE(trigger != nullptr);
        CHECK(std::find(swept.begin(), swept.end(), trigger) != swept.end());
    }
    const auto attribute_of = [&](std::string_view selector, std::string_view name) {
        auto value = runtime_string(
            rig, "document.querySelector(" + js_string(selector) + ").getAttribute("
                     + js_string(name) + ")",
            "spectr-native-sweep-attribute");
        return value.substr(0, value.find('\n'));
    };
    // A list that scrolls inside a clipping viewport: swept at every wheel
    // position from the top to the end, each pass clipped to the viewport,
    // and it must have reached every row it holds.
    const auto sweep_scrolling_list = [&](const std::string& name, const std::string& list,
                                          const std::string& viewport_selector,
                                          const std::string& offset_attribute,
                                          const std::string& row_selector) {
        const auto* viewport = native_view_for(viewport_selector);
        REQUIRE(viewport != nullptr);
        const auto vp = root_rect(*viewport);
        const pulp::view::Point over{(vp.left + vp.right) * 0.5f, (vp.top + vp.bottom) * 0.5f};
        for (int i = 0; i < 40; ++i)
            pulp::view::deliver_mouse_wheel(*rig.root, over, 0.0f, -60.0f, {});
        settle(rig.clock, 8);
        REQUIRE(attribute_of(list, offset_attribute) == "0");
        int pass = 0;
        for (std::string previous;; ++pass) {
            const auto offset = attribute_of(list, offset_attribute);
            if (offset == previous) break;
            previous = offset;
            sweep(name + " at offset " + offset, nullptr, root_rect(*native_view_for(viewport_selector)));
            pulp::view::deliver_mouse_wheel(*rig.root, over, 0.0f, 90.0f, {});
            settle(rig.clock, 8);
            REQUIRE(pass < 30);
        }
        // Every row of the list was reached at one of those positions.
        std::vector<std::string> ids;
        try {
            rig.bridge().load_script(
                std::string{"(() => { const out = Array.from(document.querySelectorAll("}
                    + js_string(list + " " + row_selector)
                    + ")).map((n) => n.__pulpId || n.id || ''); "
                      "throw new Error('ROWIDS:' + out.join('|') + ':END'); })();",
                "spectr-native-sweep-rows");
        } catch (const std::exception& error) {
            const std::string message = error.what();
            const auto begin = message.find("ROWIDS:");
            const auto end = message.find(":END");
            if (begin != std::string::npos && end != std::string::npos) {
                std::stringstream rows(message.substr(begin + 7, end - begin - 7));
                for (std::string id; std::getline(rows, id, '|');) ids.push_back(id);
            }
        }
        INFO(name);
        CHECK_FALSE(ids.empty());
        for (const auto& id : ids) {
            const auto* row = find_view_id(*rig.root, id);
            INFO("row " << id);
            REQUIRE(row != nullptr);
            CHECK(std::find(swept.begin(), swept.end(), row) != swept.end());
        }
        return pass;
    };
    const auto sweep_length_menu = [&](const std::string& name) {
        activate(rig, "[data-spectr-menu-root=\"length\"] [data-spectr-menu-trigger]");
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
        return sweep_scrolling_list(
            name, "[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]",
            "[data-spectr-length-viewport-box]", "data-spectr-length-offset",
            "[data-spectr-length-option]");
    };
    // The editor scales the authored 1320x860 layout, so the 21 rows fit
    // below the trigger at this size as they do there: one pass.
    CHECK(sweep_length_menu("menu length") == 1);
    close_overlay();
    // A compound length past the loop memory adds its checked row and the
    // cap note, and the list scrolls: every row at every scroll position.
    REQUIRE(rig.processor.set_freeze_length_from_editor({64, spectr::LengthFraction::f7_8}));
    (void)rig.processor.apply_surface_params(false);
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-freeze-length]').getAttribute('data-spectr-freeze-length-label') === '64 7/8 bars'",
        "the compound length did not reach the LENGTH control");
    CHECK(sweep_length_menu("menu length, scrolling") > 1);
    activate(rig, "[data-spectr-length-option=\"custom-editor\"]");
    REQUIRE(native_view_for("[data-spectr-length-editor]") != nullptr);
    sweep("length custom editor");
    for (const auto* selector : {"[data-spectr-length-bars]", "[data-spectr-length-bars-step=\"up\"]",
                                 "[data-spectr-length-bars-step=\"down\"]",
                                 "[data-spectr-length-fraction]", "[data-spectr-length-cancel]",
                                 "[data-spectr-length-apply]"}) {
        INFO("custom editor control " << selector);
        const auto* control = native_view_for(selector);
        REQUIRE(control != nullptr);
        CHECK(std::find(swept.begin(), swept.end(), control) != swept.end());
    }
    activate(rig, "[data-spectr-length-fraction]");
    REQUIRE(native_view_for("[data-spectr-length-fraction-options]") != nullptr);
    CHECK(sweep_scrolling_list(
              "length fraction list", "[data-spectr-length-fraction-options]",
              "[data-spectr-length-fraction-viewport]", "data-spectr-length-fraction-offset",
              "[data-spectr-length-fraction-option]") > 1);
    close_overlay();  // the Fraction list
    close_overlay();  // the Custom editor
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-pattern-manage]");
    sweep("preset manager");
    close_overlay();

    CHECK(surfaces >= 14);
    // Control: the sweep actually probed a population, so an empty `dead` list
    // is a reading, not an instrument that measured nothing.
    CHECK(probed > 1000);
    // Occlusion is a narrow excuse; if it starts absorbing a real share of the
    // samples the sweep has stopped measuring what it claims to.
    INFO("occluded samples " << occluded << " of " << probed);
    CHECK(static_cast<std::size_t>(occluded) * 20 < probed);
    // No dead band anywhere. The preset menu used to cover the bottom 8pt of
    // AIR LIFT (4k+) with its SAVE CURRENT / MANAGE footer, pinned at the
    // capture's offset by runtime.js; the footer now follows the rows
    // (tools/patch_materialized_runtime_pattern_menu_footer.py).
    for (const auto& entry : dead) UNSCOPED_INFO(entry);
    CHECK(dead.empty());
}

// ── Editor-owned resize grip (AU v2) ─────────────────────────────────────────
//
// AU v2 has no host->plugin resize contract — `AUCocoaUIBase` declares only
// `interfaceVersion` and `uiViewForAudioUnit:withSize:`, and Logic's plugin
// window exposes no AXGrowArea and refuses a host-side resize. A resizable AU v2
// editor must therefore own its gesture and ask the host for a size. These cases
// cover the three things that are provable without a host: the grip is where it
// claims to be, it is reachable, and a drag resolves to a legal size.
namespace {

const View* find_resize_grip(const View& view) {
    // The grip is the editor's only absolutely-positioned mouse-input child of
    // the root, so identify it structurally rather than by a type the anonymous
    // namespace in the plugin TU does not export.
    for (std::size_t index = 0; index < view.child_count(); ++index) {
        const auto* child = view.child_at(index);
        if (child->position() == View::Position::absolute
            && child->wants_mouse_input()) {
            return child;
        }
    }
    return nullptr;
}

// The grip is opt-IN and the flag is process-global (au_v2_entry.cpp asserts it
// for the AU v2 build and nothing else does), so a case that wants a grip must
// ask for one — and must restore, or every later case in this binary inherits
// whatever the last one set.
struct ScopedEditorOwnsResizeGrip {
    bool previous = spectr::editor_owns_resize_grip();
    explicit ScopedEditorOwnsResizeGrip(bool value) {
        spectr::set_editor_owns_resize_grip(value);
    }
    ~ScopedEditorOwnsResizeGrip() {
        spectr::set_editor_owns_resize_grip(previous);
    }
};

// The design-viewport mapping the editor host applies, reproduced here so grip
// cases can drive the SAME window <-> root transform the host does.
//
// This is not incidental scaffolding. Every earlier grip case ran at an
// implicit scale of 1, where a delta measured in root space and a delta
// measured in window space are numerically identical — so a defect that only
// exists at scale != 1 could not be expressed, let alone caught, and the suite
// passed over a grip that flapped the window ~100pt per pointer event in Logic.
// `top_align` matches au_v2_cocoa_view.mm, which pins it true.
pulp::view::Point window_to_root(pulp::view::Point window_pt,
                                 float host_w, float host_h) {
    return pulp::view::WindowHost::design_viewport_window_to_root(
        window_pt, host_w, host_h,
        static_cast<float>(spectr::kEditorDesignWidth),
        static_cast<float>(spectr::kEditorDesignHeight), /*top_align=*/true);
}

pulp::view::Point root_to_window(pulp::view::Point root_pt,
                                 float host_w, float host_h) {
    float sx = 1.0f, sy = 1.0f, tx = 0.0f, ty = 0.0f;
    if (!pulp::view::WindowHost::compute_design_viewport_transform(
            host_w, host_h,
            static_cast<float>(spectr::kEditorDesignWidth),
            static_cast<float>(spectr::kEditorDesignHeight),
            sx, sy, tx, ty, /*top_align=*/true)) {
        return root_pt;
    }
    return {root_pt.x * sx + tx, root_pt.y * sy + ty};
}

void deliver_native_resize_drag(View& root, View* target,
                                pulp::view::Point root_pt,
                                float movement_x, float movement_y) {
    pulp::view::PointerAttributes pointer;
    pointer.movement_x = movement_x;
    pointer.movement_y = movement_y;
    pointer.has_movement_delta = true;
    pulp::view::deliver_mouse_drag(
        root, target, root_pt, /*modifiers=*/0, /*click_count=*/1,
        pulp::view::MouseButton::left, pointer);
}

}  // namespace

TEST_CASE("editor resize grip sits in the bottom bar at every size",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip grip_enabled{true};
    NativeEditorRig rig;

    for (const auto& size : std::array<std::pair<int, int>, 4>{
             std::pair{792, 516}, std::pair{990, 645},
             std::pair{1320, 860}, std::pair{2640, 1720}}) {
        rig.resize(static_cast<float>(size.first),
                   static_cast<float>(size.second));
        settle(rig.clock, 96);
        INFO("host size " << size.first << 'x' << size.second);

        const auto* grip = find_resize_grip(*rig.root);
        REQUIRE(grip != nullptr);

        const auto box = root_rect(*grip);
        CHECK(box.right - box.left == Catch::Approx(20.0f));
        CHECK(box.bottom - box.top == Catch::Approx(20.0f));
        // INVARIANT under the pin, and this is the proportional contract in one
        // assertion: the grip sits at the bottom-right of the AUTHORED box at
        // every host size, because the root never reflows — the host scales it.
        // Under the old responsive contract this tracked the live host bounds
        // instead, which is exactly the reflow the user ruled out.
        CHECK(box.right
              == Catch::Approx(static_cast<float>(spectr::kEditorDesignWidth)));
        // Conventional AU affordance: flush with the plug-in content corner.
        // Logic-owned chrome may continue below the content view, but the
        // plug-in must not leave an internal gap above it.
        CHECK(box.bottom
              == Catch::Approx(static_cast<float>(spectr::kEditorDesignHeight)));

        // Reachable: hit-testing its centre resolves to the grip itself, not to
        // whatever the scripted realm painted underneath.
        const auto centre = pulp::view::Point{(box.left + box.right) * 0.5f,
                                              (box.top + box.bottom) * 0.5f};
        const auto* hit = rig.root->hit_test(centre);
        INFO("hit " << (hit ? describe_control(*hit) : std::string("<null>")));
        INFO("grip " << describe_control(*grip));
        CHECK(hit == grip);
    }
}

TEST_CASE("editor resize grip outranks the behaviour layer",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip grip_enabled{true};
    NativeEditorRig rig;
    rig.resize(990, 645);
    settle(rig.clock, 96);

    const auto* grip = find_resize_grip(*rig.root);
    REQUIRE(grip != nullptr);

    // The materialized tree paints at z = -20000 and takes interaction at
    // z = +20000 through a full-bleed behaviour node. The grip is only
    // reachable while it outranks that node, and the SDK exports no constant
    // for it — so pin the relationship here rather than trusting a magic
    // number to stay valid. A materializer that raises its z fails this test
    // instead of silently killing the gesture.
    const View* behavior = nullptr;
    for (std::size_t index = 0; index < rig.root->child_count(); ++index) {
        const auto* child = rig.root->child_at(index);
        if (describe_control(*child).find("__pulp_materialized_behavior__")
            != std::string::npos) {
            behavior = child;
        }
    }
    REQUIRE(behavior != nullptr);
    CHECK(grip->z_index() > behavior->z_index());
}

TEST_CASE("editor resize grip overlaps no other control",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip grip_enabled{true};
    NativeEditorRig rig;

    // Every declared size, not just the one Logic opens at. The bottom bar
    // reflows, so the corner is tightest at the authored 1320x860 capture — a
    // grip validated only at the preferred size lands on the help button there,
    // which is exactly what `native buttons are tappable across their whole
    // painted bounds` caught the first time this was wired.
    for (const auto& size : std::array<std::pair<int, int>, 4>{
             std::pair{792, 516}, std::pair{990, 645},
             std::pair{1320, 860}, std::pair{2640, 1720}}) {
        rig.resize(static_cast<float>(size.first),
                   static_cast<float>(size.second));
        settle(rig.clock, 96);
        INFO("host size " << size.first << 'x' << size.second);

        const auto* grip = find_resize_grip(*rig.root);
        REQUIRE(grip != nullptr);
        const auto grip_box = root_rect(*grip);

        std::vector<const View*> controls;
        collect_click_targets(*rig.root, controls);
        REQUIRE(controls.size() >= 15);

        for (const auto* control : controls) {
            if (control == grip) continue;
            INFO("control " << describe_control(*control));
            // Compare against the PAINTED extent, not the hit box: the gear and
            // help buttons are what the grip must visibly clear.
            const auto painted = painted_extent(*control);
            CAPTURE(painted.left, painted.top, painted.right, painted.bottom,
                    grip_box.left, grip_box.top, grip_box.right,
                    grip_box.bottom);
            const bool disjoint = painted.right <= grip_box.left
                                  || painted.left >= grip_box.right
                                  || painted.bottom <= grip_box.top
                                  || painted.top >= grip_box.bottom;
            CHECK(disjoint);
        }
    }
}

TEST_CASE("editor resize grip drag requests an aspect-held, clamped size",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip grip_enabled{true};
    NativeEditorRig rig;
    rig.resize(990, 645);
    settle(rig.clock, 96);

    const auto* grip = find_resize_grip(*rig.root);
    REQUIRE(grip != nullptr);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> requests;
    bool accept = true;
    rig.processor.set_editor_resize_handler(
        [&](std::uint32_t w, std::uint32_t h) {
            requests.push_back({w, h});
            return accept;
        });

    // Deltas are stated as POINTER movement — host/window-space points, which
    // is what Pulp's native plug-in host stamps onto PointerAttributes. At this
    // host size the design viewport scales by 990/1320 = 0.75, so the delivered
    // root point deliberately disagrees numerically with the native movement.
    // That keeps this case honest about which channel owns resize arithmetic.
    const auto drag = [&](float window_dx, float window_dy) {
        const auto box = root_rect(*grip);
        const auto anchor = pulp::view::Point{
            (box.left + box.right) * 0.5f,
            (box.top + box.bottom) * 0.5f};
        auto* target = rig.root->hit_test(anchor);
        REQUIRE(target == grip);
        REQUIRE(pulp::view::deliver_mouse_down(
            *rig.root, target, anchor, /*modifiers=*/0,
            /*click_count=*/1, /*bubble=*/true));
        const auto origin = root_to_window(anchor, 990.0f, 645.0f);
        const auto moved = window_to_root(
            {origin.x + window_dx, origin.y + window_dy}, 990.0f, 645.0f);
        deliver_native_resize_drag(
            *rig.root, target, moved, window_dx, window_dy);
    };

    SECTION("a grow drag asks for the aspect-held size") {
        drag(330.0f, 0.0f);
        REQUIRE(requests.size() == 1);
        // Base is the HOST size the rig was driven to, not the authored box,
        // and the delta is the pointer movement in that same host space.
        const auto expected = spectr::resolve_editor_resize(990, 645, 330.0f, 0.0f);
        CHECK(requests.front().first == expected.width);
        CHECK(requests.front().second == expected.height);
        // Aspect held exactly against the authored 1320x860 capture.
        CHECK(requests.front().first * 860ull
              == requests.front().second * 1320ull);
    }

    SECTION("a drag past the declared maximum clamps instead of overshooting") {
        drag(100000.0f, 100000.0f);
        REQUIRE(requests.size() == 1);
        CHECK(requests.front().first == spectr::kEditorMaximumWidth);
        CHECK(requests.front().second == spectr::kEditorMaximumHeight);
    }

    SECTION("a drag past the declared minimum clamps instead of collapsing") {
        drag(-100000.0f, -100000.0f);
        REQUIRE(requests.size() == 1);
        CHECK(requests.front().first == spectr::kEditorMinimumWidth);
        CHECK(requests.front().second == spectr::kEditorMinimumHeight);
    }

    SECTION("a refused request leaves the editor size untouched") {
        accept = false;
        const auto before = rig.root->bounds();
        drag(330.0f, 0.0f);
        REQUIRE(requests.size() == 1);
        settle(rig.clock, 16);
        // The grip only ASKS. Geometry changes solely via on_view_resized, so a
        // host that refuses cannot leave the editor disagreeing with its window.
        CHECK(rig.root->bounds().width == Catch::Approx(before.width));
        CHECK(rig.root->bounds().height == Catch::Approx(before.height));

        // And one refusal ends the gesture's traffic rather than opening a
        // rejected host transaction per mouse-move.
        deliver_native_resize_drag(
            *rig.root, const_cast<View*>(grip), {360.0f, 0.0f}, 30.0f, 0.0f);
        deliver_native_resize_drag(
            *rig.root, const_cast<View*>(grip), {390.0f, 0.0f}, 30.0f, 0.0f);
        CHECK(requests.size() == 1);
    }
}

// The standalone host installs a real editor-resize handler
// (`install_standalone_editor_resize_handler`) that turns
// `request_editor_resize` into an actual window resize, and the resized window
// comes back into the editor as `on_view_resized`. That full loop — ask, host
// grants, editor adopts — is what dragging the grip in the standalone
// exercises, and it is the path where a resize can leave the UI dead. The
// refusal case above only proves nothing moves when the host says no.
TEST_CASE("editor resize grip round-trips a granted resize",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip au_v2{true};
    NativeEditorRig rig;
    float host_w = 990.0f, host_h = 645.0f;
    rig.resize(host_w, host_h);
    settle(rig.clock, 96);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> requests;
    rig.processor.set_editor_resize_handler(
        [&](std::uint32_t w, std::uint32_t h) {
            requests.push_back({w, h});
            return true;
        });

    // `window_dx` / `window_dy` are POINTER movement, so the second gesture is
    // stated in the same units as the first even though the editor — and
    // therefore the design-viewport scale — has grown between them.
    const auto drag_and_grant = [&](float window_dx, float window_dy) {
        const auto* grip = find_resize_grip(*rig.root);
        REQUIRE(grip != nullptr);
        const auto box = root_rect(*grip);
        const auto anchor = pulp::view::Point{
            (box.left + box.right) * 0.5f,
            (box.top + box.bottom) * 0.5f};
        auto* target = rig.root->hit_test(anchor);
        REQUIRE(target == grip);
        REQUIRE(pulp::view::deliver_mouse_down(
            *rig.root, target, anchor, /*modifiers=*/0,
            /*click_count=*/1, /*bubble=*/true));
        const auto origin = root_to_window(anchor, host_w, host_h);
        const auto moved = window_to_root(
            {origin.x + window_dx, origin.y + window_dy}, host_w, host_h);
        deliver_native_resize_drag(
            *rig.root, target, moved, window_dx, window_dy);
        REQUIRE_FALSE(requests.empty());
        // The host grants it: the window resizes, and the new size arrives back
        // through on_view_resized exactly as a real host delivers it — after
        // the gesture, not re-entrantly inside the mouse handler.
        const auto granted = requests.back();
        host_w = static_cast<float>(granted.first);
        host_h = static_cast<float>(granted.second);
        rig.resize(host_w, host_h);
        settle(rig.clock, 96);
        return granted;
    };

    const auto first = drag_and_grant(330.0f, 0.0f);
    const auto expected_first =
        spectr::resolve_editor_resize(990, 645, 330.0f, 0.0f);
    CHECK(first.first == expected_first.width);
    CHECK(first.second == expected_first.height);

    // Under the pin the ROOT stays at the authored box no matter what the host
    // granted — the granted size changes the surface, not the layout.
    CHECK(rig.root->bounds().width
          == Catch::Approx(static_cast<float>(spectr::kEditorDesignWidth)));
    CHECK(rig.root->bounds().height
          == Catch::Approx(static_cast<float>(spectr::kEditorDesignHeight)));
    require_no_rejected_layout_boxes(rig);

    // The grip followed the new corner rather than staying at the old one.
    {
        const auto* grip = find_resize_grip(*rig.root);
        REQUIRE(grip != nullptr);
        const auto box = root_rect(*grip);
        CHECK(box.right
              == Catch::Approx(static_cast<float>(spectr::kEditorDesignWidth)));
        CHECK(box.bottom
              == Catch::Approx(static_cast<float>(spectr::kEditorDesignHeight)));
        CHECK(box.right - box.left == Catch::Approx(20.0f));
        CHECK(box.bottom - box.top == Catch::Approx(20.0f));
    }

    // And the UI is still live at the size the gesture produced. This is the
    // failure the user would actually hit: a resize that leaves controls dead.
    {
        std::vector<const View*> controls;
        collect_click_targets(*rig.root, controls);
        REQUIRE(controls.size() >= 15);
        for (const auto* control : controls) {
            const auto box = root_rect(*control);
            if (box.right - box.left <= 0.0f || box.bottom - box.top <= 0.0f)
                continue;
            INFO("control " << describe_control(*control));
            const auto centre = pulp::view::Point{(box.left + box.right) * 0.5f,
                                                  (box.top + box.bottom) * 0.5f};
            auto* hit = rig.root->hit_test(centre);
            REQUIRE(hit != nullptr);
            CHECK(nearest_click_target(hit) == control);
        }
    }

    // A second gesture measures from the size the editor is NOW at. If the base
    // were still latched at 990 the same delta would ask for 1320 again, and
    // the grip would feel stuck after one drag.
    const auto second = drag_and_grant(330.0f, 0.0f);
    CHECK(second.first > first.first);
    CHECK(second.first * 860ull == second.second * 1320ull);
}


// ── Host dispatch path ───────────────────────────────────────────────────────
//
// The cases above call the grip's methods directly and assert
// `root->hit_test(centre) == grip`. Both pass while the gesture is completely
// dead in a real host, which is the trap this file walked into once already:
// asserting a layer BELOW where the failure lives. The mac host does not call
// widget methods — it resolves `rootView->hit_test(pt)`, runs the focus
// protocol through `transfer_input_focus`, and then delivers through
// `deliver_mouse_down` / `deliver_mouse_drag` (window_host_mac.mm:398-529).
// This case drives those same entry points, so a target that hit-tests
// correctly but is dropped by focus transfer or by a delivery channel fails
// here instead of shipping green.
//
// This models the AU editor specifically: an AU v2 Cocoa view is embedded in
// the host's window, so nothing upstream competes for the corner. In the
// STANDALONE the same gesture is unreachable — macOS owns the bottom-right of
// a resizable NSWindow and consumes press and click alike before the content
// view is asked (measured: no mouse channel on the grip fires at all there,
// while the window itself resizes). That is not a defect in this wiring, and
// the standalone does not need the grip because the OS resizes it natively —
// but it does mean the standalone can NOT verify this path, which is why it is
// pinned here.
TEST_CASE("editor resize grip resizes through the host dispatch path",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip grip_enabled{true};
    NativeEditorRig rig;
    rig.resize(990, 645);
    settle(rig.clock, 96);

    const auto* grip = find_resize_grip(*rig.root);
    REQUIRE(grip != nullptr);
    const auto box = root_rect(*grip);
    const auto press = pulp::view::Point{(box.left + box.right) * 0.5f,
                                         (box.top + box.bottom) * 0.5f};

    std::vector<std::pair<std::uint32_t, std::uint32_t>> requests;
    rig.processor.set_editor_resize_handler(
        [&](std::uint32_t w, std::uint32_t h) {
            requests.push_back({w, h});
            return true;
        });

    // 1. Target resolution, exactly as the host does it.
    auto* target = rig.root->hit_test(press);
    REQUIRE(target == grip);

    // 2. The focus protocol the host runs before it will deliver anything. It
    //    returning false is how a press gets silently dropped — ResizableCorner
    //    is deliberately not focusable, so this pins that a non-focusable
    //    target still survives.
    REQUIRE(pulp::view::transfer_input_focus(*rig.root, target));

    // 3. Delivery through the portable channels the host actually calls.
    REQUIRE(pulp::view::deliver_mouse_down(*rig.root, target, press,
                                           /*modifiers=*/0,
                                           /*click_count=*/1,
                                           /*bubble=*/true));
    // Move the pointer 330 x 215 WINDOW-space points, expressed as the root
    // point the host would deliver for that movement at this scale.
    const auto press_window = root_to_window(press, 990.0f, 645.0f);
    const auto moved = window_to_root(
        {press_window.x + 330.0f, press_window.y + 215.0f}, 990.0f, 645.0f);
    deliver_native_resize_drag(*rig.root, target, moved, 330.0f, 215.0f);

    // The gesture reached the editor and produced a real host request.
    REQUIRE(requests.size() == 1);
    const auto expected = spectr::resolve_editor_resize(990, 645, 330.0f, 215.0f);
    CHECK(requests.front().first == expected.width);
    CHECK(requests.front().second == expected.height);
    CHECK(requests.front().first * 860ull == requests.front().second * 1320ull);
}

// ── Format gating ────────────────────────────────────────────────────────────
//
// The grip exists ONLY for a format that offers the user no resize affordance,
// which today means AU v2 alone: the format hands a size plugin-ward once at
// view creation and never again, and Logic's plug-in window has no grow area.
// VST3 (checkSizeConstraint/onSize) and CLAP (gui_adjust_size) both resize
// correctly through their own host protocols — verified in REAPER — so a grip
// there would duplicate a working affordance. A standalone window is the
// opposite failure: macOS owns the bottom-right corner of a resizable NSWindow
// and consumes press and click there before the content view is asked, measured
// in a live standalone with the grip present, painted, and correctly placed —
// the window resized and the grip's mouse channels never fired once.
//
// So the flag is opt-IN and only `au_v2_entry.cpp` asserts it. These two cases
// are the guard on that default in both directions.

TEST_CASE("editors default to no grip so only AU v2 opts in",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip default_build{false};

    NativeEditorRig rig;
    rig.resize(990, 645);
    settle(rig.clock, 96);

    // No grip at all — not merely inert, absent.
    CHECK(find_resize_grip(*rig.root) == nullptr);

    // And nothing else of the editor's has crept into the corner a standalone's
    // macOS window owns. A control there would compete for the window resize
    // even without a grip.
    const auto corner = pulp::view::Point{
        static_cast<float>(spectr::kEditorDesignWidth) - 4.0f,
        static_cast<float>(spectr::kEditorDesignHeight) - 4.0f};
    auto* hit = rig.root->hit_test(corner);
    INFO("corner hit " << (hit ? describe_control(*hit) : std::string("<null>")));
    CHECK(nearest_click_target(hit) == nullptr);
}

TEST_CASE("the AU v2 opt-in gets the grip", "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip au_v2{true};

    NativeEditorRig rig;
    rig.resize(990, 645);
    settle(rig.clock, 96);

    // The complement of the case above: gating must not silently disable the
    // grip everywhere, which would make every other grip case vacuous.
    REQUIRE(find_resize_grip(*rig.root) != nullptr);
}

// ── The regression this whole slice exists for ───────────────────────────────
//
// Grabbing the grip in Logic "disrupted the editor". Root cause: a pinned
// design viewport makes ROOT space a SCALED space whose scale is
// host_width / 1320 — a function of the very quantity the grip changes. Mouse
// points arrive inverse-mapped into that space, so applying a resize
// retroactively changes what the latched drag origin MEANT. Hold the pointer
// perfectly still after one 100pt move and the reported delta collapses toward
// zero, the next request shrinks the editor, the scale drops back, the delta
// reappears. Measured by this case against the pre-fix code: the requested size
// swings across 903x588 .. 1959x1277 on successive events while the pointer
// never moves, each swing dragging a full materialized re-layout behind it.
//
// Nothing in the suite could see it, and that is the interesting part: every
// other case runs at an implicit scale of 1, where the broken design-space
// arithmetic and the correct window-space arithmetic are the same numbers. The
// case below is the one that fails without the fix — it drives a STATIONARY
// pointer through the real host dispatch path while a host that actually
// applies the requested size moves the scale underneath it.
TEST_CASE("editor resize grip holds still when the pointer holds still",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip au_v2{true};
    NativeEditorRig rig;

    // Open at the authored box, which is where Logic opens the editor.
    float host_w = static_cast<float>(spectr::kEditorDesignWidth);
    float host_h = static_cast<float>(spectr::kEditorDesignHeight);
    rig.resize(host_w, host_h);
    settle(rig.clock, 96);

    const auto* grip = find_resize_grip(*rig.root);
    REQUIRE(grip != nullptr);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> requests;
    rig.processor.set_editor_resize_handler(
        [&](std::uint32_t w, std::uint32_t h) {
            requests.push_back({w, h});
            // A host that APPLIES the request, which is what Logic's AU v2
            // container resize does. Applying it is what moves the scale, so a
            // handler that only records cannot reproduce the defect.
            host_w = static_cast<float>(w);
            host_h = static_cast<float>(h);
            rig.processor.on_view_resized(*rig.root, w, h);
            return true;
        });

    const auto box = root_rect(*grip);
    const auto press = pulp::view::Point{(box.left + box.right) * 0.5f,
                                         (box.top + box.bottom) * 0.5f};
    auto* target = rig.root->hit_test(press);
    REQUIRE(target == grip);
    REQUIRE(pulp::view::transfer_input_focus(*rig.root, target));
    REQUIRE(pulp::view::deliver_mouse_down(*rig.root, target, press,
                                           /*modifiers=*/0, /*click_count=*/1,
                                           /*bubble=*/true));

    // One real 100pt drag to the right, and then the pointer NEVER MOVES AGAIN.
    // Its window-space position is fixed for the rest of the gesture; only the
    // root-space point the host would deliver for it changes, because the scale
    // does.
    const auto pressed_window = root_to_window(press, host_w, host_h);
    const auto held_window =
        pulp::view::Point{pressed_window.x + 100.0f, pressed_window.y};

    for (int event = 0; event < 12; ++event) {
        const auto delivered = window_to_root(held_window, host_w, host_h);
        // One native 100pt movement, followed by eleven native 0pt movements
        // while the pointer remains physically stationary. The changing root
        // coordinate must not be reinterpreted as new resize travel.
        deliver_native_resize_drag(
            *rig.root, target, delivered,
            event == 0 ? 100.0f : 0.0f, 0.0f);
    }

    // A stationary pointer resolves to ONE request. Not "settles eventually"
    // and not twelve duplicate transactions: one physical movement produces
    // one target even though the design-space coordinate keeps changing.
    REQUIRE(requests.size() == 1);
    const auto first = requests.front();
    // And it is the size the movement actually asked for: +100 window points.
    const auto expected = spectr::resolve_editor_resize(
        spectr::kEditorDesignWidth, spectr::kEditorDesignHeight, 100.0, 0.0);
    CHECK(first.first == expected.width);
    CHECK(first.second == expected.height);
}

// Structural guard against the blindness that let the oscillation ship.
//
// The defect above was not merely uncaught, it was INEXPRESSIBLE: every case in
// this file ran at an implicit design-viewport scale of 1, and at scale 1 a
// delta measured in root space and a delta measured in window space are
// literally the same number. A whole suite can be green and blind to an entire
// class of coordinate-space bug because the fixture never leaves the identity
// case.
//
// So this case exists to keep a non-unit scale in the suite permanently, and it
// asserts the scale is non-unit FIRST — otherwise a well-meaning change to the
// host size below would quietly neutralize it and leave a test that proves
// nothing while still passing.
TEST_CASE("editor resize grip measures the pointer, not design units",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    ScopedEditorOwnsResizeGrip au_v2{true};
    NativeEditorRig rig;

    // 990/1320 = 0.75. Deliberately not the authored box.
    constexpr float kHostW = 990.0f, kHostH = 645.0f;
    const float scale = kHostW / static_cast<float>(spectr::kEditorDesignWidth);
    REQUIRE(scale != Catch::Approx(1.0f));

    rig.resize(kHostW, kHostH);
    settle(rig.clock, 96);

    const auto* grip = find_resize_grip(*rig.root);
    REQUIRE(grip != nullptr);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> requests;
    rig.processor.set_editor_resize_handler(
        [&](std::uint32_t w, std::uint32_t h) {
            requests.push_back({w, h});
            return true;
        });

    const auto box = root_rect(*grip);
    const auto press = pulp::view::Point{(box.left + box.right) * 0.5f,
                                         (box.top + box.bottom) * 0.5f};
    auto* target = rig.root->hit_test(press);
    REQUIRE(target == grip);
    REQUIRE(pulp::view::transfer_input_focus(*rig.root, target));
    REQUIRE(pulp::view::deliver_mouse_down(*rig.root, target, press,
                                           /*modifiers=*/0, /*click_count=*/1,
                                           /*bubble=*/true));

    // Move the pointer exactly 120 WINDOW points to the right.
    constexpr float kPointerTravel = 120.0f;
    const auto pressed_window = root_to_window(press, kHostW, kHostH);
    const auto moved = window_to_root(
        {pressed_window.x + kPointerTravel, pressed_window.y}, kHostW, kHostH);
    deliver_native_resize_drag(
        *rig.root, target, moved, kPointerTravel, 0.0f);

    REQUIRE(requests.size() == 1);
    // The editor grows by what the POINTER travelled, 1:1.
    CHECK(requests.front().first
          == static_cast<std::uint32_t>(kHostW) + static_cast<std::uint32_t>(kPointerTravel));
    // And explicitly NOT by the root-space delta, which at this scale is
    // 120/0.75 = 160. That is the number the pre-fix code produced, and the
    // only thing separating the two readings is a scale != 1.
    const auto conflated = static_cast<std::uint32_t>(
        kHostW + kPointerTravel / scale);
    CHECK(requests.front().first != conflated);
}

// Companion guard on the other half of the same gesture: under a pinned
// viewport on_view_resized always publishes the SAME authored box, so the
// materialized restore + re-place pass must not re-run per pointer event. It is
// hundreds of bridge writes producing a layout identical to the one on screen,
// and it ran inside the resize round trip for the whole drag.
TEST_CASE("a resize to an unchanged design box republishes nothing",
          "[native-n1][resize-grip]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    rig.resize(1320, 860);
    settle(rig.clock, 96);

    // Count the passes by wrapping the entry point the publisher calls.
    rig.bridge().load_script(
        "globalThis.__spectrLayoutPasses__ = 0;"
        "globalThis.__spectrWrappedResize__ = globalThis.__spectrResizeNativeEditor;"
        "globalThis.__spectrResizeNativeEditor = function(w, h) {"
        "  globalThis.__spectrLayoutPasses__ += 1;"
        "  return globalThis.__spectrWrappedResize__(w, h); };",
        "spectr-native-layout-pass-counter");

    // Drive the resize path the way a drag does. Under the pin every one of
    // these publishes the authored box, so after the first there is nothing new
    // to say: neither the native tree nor the JS pass should run again.
    const auto layouts_before = pulp::view::View::layout_pass_count();
    for (const auto& size : std::array<std::pair<int, int>, 4>{
             std::pair{1400, 912}, std::pair{1480, 964},
             std::pair{1560, 1016}, std::pair{1640, 1068}}) {
        rig.processor.on_view_resized(*rig.root,
                                      static_cast<std::uint32_t>(size.first),
                                      static_cast<std::uint32_t>(size.second));
    }
    CHECK(pulp::view::View::layout_pass_count() - layouts_before == 0);
    settle(rig.clock, 32);

    // Same throw-to-read seam the rest of this file uses.
    int passes = -1;
    try {
        rig.bridge().load_script(
            "throw new Error('PASSES:' + globalThis.__spectrLayoutPasses__);",
            "spectr-native-layout-pass-read");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        const auto marker = message.find("PASSES:");
        if (marker != std::string::npos)
            passes = std::atoi(message.c_str() + marker + 7);
    }
    INFO("materialized layout passes during four same-box resizes");
    CHECK(passes == 0);
}

TEST_CASE("settings chips answer a native pointer click and not only the semantic driver",
          "[native-n1][state-parity][settings][native-pointer]") {
    // The existing settings coverage enters through
    // __pulpActivateMaterializedElement__, which invokes the React handler
    // directly. That proves the handler works when called; it says nothing
    // about whether a pointer landing on the chip ever reaches it. A user only
    // ever has the pointer path, so drive it here.
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto comma = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma,
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    require_state(rig, "settings");

    // Positive control: the panel is populated and the pre-click state is the
    // deterministic default, so a later 'mono' reading cannot be a no-op pass.
    const auto* appearance = find_label(*rig.root, "APPEARANCE");
    REQUIRE(appearance != nullptr);
    const auto* mono_chip = find_label(*rig.root, "Mono");
    REQUIRE(mono_chip != nullptr);
    require_app_state(rig, "s.settings.theme === 'spectral'",
                      "settings did not open on the default theme");

    INFO("phase=native-pointer-click-on-theme-chip");
    native_click_label(rig, "Mono");
    require_app_state(rig, "s.settings.theme === 'mono'",
                      "a native pointer click on the Mono chip did not reach its handler");

    // Same question for a second control group, so a pass is not specific to
    // one chip's hit geometry.
    INFO("phase=native-pointer-click-on-metaphor-chip");
    const auto* shards_chip = find_label(*rig.root, "Shards");
    REQUIRE(shards_chip != nullptr);
    native_click_label(rig, "Shards");
    require_app_state(rig, "s.settings.metaphor === 'shards'",
                      "a native pointer click on the Shards chip did not reach its handler");
    storage.require_unchanged();
}

// ---------------------------------------------------------------------------
// Appearance detectors
//
// Every other test in this file asserts that a value reached the runtime. None
// of them assert what a person sees, which is how a panel can satisfy its whole
// contract while rendering text on top of other text, or clipped mid-word. These
// two read the laid-out View tree directly: absolute text boxes for collisions,
// and measured-vs-laid-out width for text that cannot fit the box layout gave
// it.
// ---------------------------------------------------------------------------

TEST_CASE("no two text boxes overlap on the home surface",
          "[native-n1][appearance]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 8);

    const auto boxes = spectr::appearance::text_boxes(*rig.root);
    // Control: a detector that finds nothing because it collected nothing is
    // broken, not passing. The home surface is dense with text.
    INFO("collected text boxes: " << boxes.size());
    REQUIRE(boxes.size() > 10);

    if (const char* out_dir = std::getenv("SPECTR_APPEARANCE_SHOT_DIR")) {
        const auto bounds = rig.root->bounds();
        CHECK(pulp::view::render_to_file(
            *rig.root, static_cast<uint32_t>(bounds.width),
            static_cast<uint32_t>(bounds.height),
            std::string(out_dir) + "/home-appearance.png", 2.0f,
            pulp::view::ScreenshotBackend::skia));
    }

    const auto findings = spectr::appearance::detect_overlapping_text(*rig.root);
    INFO("overlapping text:\n" << spectr::appearance::join_findings(findings));
    CHECK(findings.empty());
}

TEST_CASE("no text is laid out narrower than it measures on the home surface",
          "[native-n1][appearance]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 8);

    const auto boxes = spectr::appearance::text_boxes(*rig.root);
    INFO("collected text boxes: " << boxes.size());
    REQUIRE(boxes.size() > 10);

    const auto findings = spectr::appearance::detect_clipped_text(*rig.root);
    INFO("clipped text:\n" << spectr::appearance::join_findings(findings));
    CHECK(findings.empty());
}

TEST_CASE("the settings panel renders text that fits and does not collide",
          "[native-n1][appearance][settings]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto comma = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma,
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    rig.root->layout_children();
    settle(rig.clock, 8);

    const auto boxes = spectr::appearance::text_boxes(*rig.root);
    // Control, and the row's own defect: an EMPTY settings panel is exactly the
    // failure Daniel can see. A settings surface that collects no more text than
    // the home surface behind it has not rendered.
    INFO("collected text boxes with settings open: " << boxes.size());
    for (const auto& entry : boxes)
        INFO("  box \"" << entry.text << "\" " << entry.box.width << "x"
                        << entry.box.height << " at (" << entry.box.x << ","
                        << entry.box.y << ")");
    CHECK(boxes.size() > 10);

    // A tree-level overlap can be a false positive: a node positioned over
    // another but painted invisibly. Raster the same surface so every finding
    // can be checked against pixels. Skia, not CoreGraphics — it is the
    // fidelity reference for the compositor these panels actually run on.
    if (const char* out_dir = std::getenv("SPECTR_APPEARANCE_SHOT_DIR")) {
        const auto bounds = rig.root->bounds();
        const std::string path = std::string(out_dir) + "/settings-appearance.png";
        const bool wrote = pulp::view::render_to_file(
            *rig.root, static_cast<uint32_t>(bounds.width),
            static_cast<uint32_t>(bounds.height), path, 2.0f,
            pulp::view::ScreenshotBackend::skia);
        INFO("screenshot " << path << " written=" << wrote);
        CHECK(wrote);
    }

    const auto overlaps = spectr::appearance::detect_overlapping_text(*rig.root);
    INFO("overlapping text:\n" << spectr::appearance::join_findings(overlaps));
    CHECK(overlaps.empty());

    const auto clipped = spectr::appearance::detect_clipped_text(*rig.root);
    INFO("clipped text:\n" << spectr::appearance::join_findings(clipped));
    CHECK(clipped.empty());
}

// The ABOUT copy button, on both axes a person actually experiences: where the
// word lands inside the button, and whether pressing the button does anything.
//
// The instrument matters. The visual-layout dump cannot represent text
// alignment at all — it reports a Label's NODE origin, so a word painted flush
// left inside a correctly sized box is indistinguishable from a centred one.
// `Label::painted_text_extents` runs the same shaper `paint()` does and reports
// the ink box with text-align applied to x, which is the difference.
//
// The two halves are one test on purpose: the button's feedback span is painted
// over the middle of the button, so centring the word without letting the press
// through moves the dead zone from the button's left edge to the exact point a
// person aims at.
TEST_CASE("the settings copy button centres its feedback and answers a press",
          "[native-n1][appearance][settings]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto comma = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma,
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    rig.root->layout_children();
    settle(rig.clock, 8);

    // The button is gated on `info &&`, so it does not exist until the
    // build_info_get round trip resolves. Poll instead of assuming one settle
    // is enough — and REQUIRE the find, because an absent label is a broken
    // instrument, never a pass.
    static constexpr std::array<std::string_view, 4> copy_states{
        "COPY UNAVAILABLE", "COPYING", "COPIED", "COPY"};
    const auto find_copy_label = [&]() -> const pulp::view::Label* {
        for (const auto state : copy_states)
            if (const auto* label = find_label(*rig.root, state)) return label;
        return nullptr;
    };
    const pulp::view::Label* copy = nullptr;
    for (int attempt = 0; attempt < 120 && copy == nullptr; ++attempt) {
        copy = find_copy_label();
        if (copy == nullptr) {
            settle(rig.clock, 4);
            rig.root->layout_children();
        }
    }
    REQUIRE(copy != nullptr);

    // The authored button box, found by its authored size rather than by a
    // marker the native tree does not carry.
    const auto find_button = [&](const pulp::view::Label& label) -> View* {
        for (auto* node = const_cast<View*>(label.parent()); node != nullptr;
             node = node->parent()) {
            const auto box = pulp::view::ViewInspector::absolute_bounds(*node);
            if (std::abs(box.width - 136.0f) < 0.5f
                && std::abs(box.height - 26.0f) < 0.5f)
                return node;
        }
        return nullptr;
    };

    // Measure the word against the BUTTON, not against its own label slot: the
    // button is the box a person sees.
    const auto require_centred = [&](const pulp::view::Label& label) -> float {
        auto* button = find_button(label);
        REQUIRE(button != nullptr);
        const auto button_box = pulp::view::ViewInspector::absolute_bounds(*button);
        const auto label_box = spectr::appearance::text_rect(label);
        // `spectr::appearance::ink_rect` cannot be used here: it derives its
        // width from `intrinsic_width()`, which a Label styled `width: 100%`
        // reports as 0 because Yoga wants the parent to drive wrapping — so it
        // silently falls back to handing back the whole box, which is centred
        // by construction and would pass this assertion while measuring
        // nothing.
        const auto extents = label.painted_text_extents(label_box.width);
        // Positive control on the instrument: an unmeasured span is that
        // degenerate case wearing a different name.
        REQUIRE(extents.measured);
        REQUIRE(extents.ink.width > 0.0f);
        // The degenerate span is measured against the BUTTON, not against the
        // label's own slot.
        //
        // `ink_rect` really does hand back the whole box when
        // `intrinsic_width()` reports 0, which is why it is rejected above --
        // but `painted_text_extents` has no such arm. It runs the shaper and
        // reports `ink.width` from the shaped line, so it can never return the
        // box it was handed; the only degenerate outcome it has is
        // `measured == false`, already required.
        //
        // Carrying `ink < label_box.width - 1` over to it asserted something
        // else entirely, and something this layout can never satisfy: this
        // label is shrink-to-fit, so Yoga sizes its box to `ceil(shaped
        // width)` and the box EQUALS the ink by construction (measured: a
        // 26.0f ink in a 26.0f box). The word was never overrunning anything
        // -- the button around it is 136pt wide.
        //
        // So compare against the box a person actually sees. A span that
        // filled the button would still be caught, the real product failure
        // (a feedback word wider than the control it sits in) is caught for
        // the first time, and a correctly hugged label passes.
        REQUIRE(extents.ink.width < button_box.width - 1.0f);
        const float ink_centre
            = label_box.x + extents.ink.x + extents.ink.width * 0.5f;
        const float button_centre = button_box.x + button_box.width * 0.5f;
        INFO("state \"" << label.text() << "\": button ["
                        << button_box.x << "," << button_box.y << " "
                        << button_box.width << "x" << button_box.height
                        << "] label [" << label_box.x << "," << label_box.y
                        << " " << label_box.width << "x" << label_box.height
                        << "] ink x=" << (label_box.x + extents.ink.x)
                        << " w=" << extents.ink.width
                        << " ink_centre=" << ink_centre
                        << " button_centre=" << button_centre
                        << " delta=" << (ink_centre - button_centre));
        CHECK(button_box.width == Catch::Approx(136.0f).margin(0.5f));
        CHECK(ink_centre == Catch::Approx(button_centre).margin(1.5f));
        return extents.ink.width;
    };

    const std::string resting_state{copy->text()};
    const float resting_ink = require_centred(*copy);

    // Second half of the line. Scroll the button into the viewport first: the
    // ABOUT group sits far below the settings fold, and a press outside the
    // visible panel proves nothing about what a person can reach.
    auto* scroll = owning_scroll_view(*copy);
    REQUIRE(scroll != nullptr);
    float content_y = 0.0f;
    REQUIRE(content_offset(*copy, *scroll, content_y));
    scroll->set_scroll(0.0f, std::max(0.0f, content_y - 120.0f));
    settle(rig.clock, 12);
    rig.root->layout_children();
    settle(rig.clock, 4);

    copy = find_copy_label();
    REQUIRE(copy != nullptr);
    auto* button = find_button(*copy);
    REQUIRE(button != nullptr);
    const auto button_bounds = button->bounds();

    // Dead centre of the button — where a person aims, and where the feedback
    // span is painted.
    const auto centre = root_point(*button, button_bounds.width * 0.5f,
                                   button_bounds.height * 0.5f);
    const auto* centre_hit = rig.root->hit_test(centre);
    REQUIRE(centre_hit != nullptr);
    rig.root->simulate_click(centre);
    settle(rig.clock, 16);

    const pulp::view::Label* after = find_copy_label();
    REQUIRE(after != nullptr);
    std::string after_centre{after->text()};

    // Control, run only when the press appears to have done nothing: a press
    // that changes no state is ambiguous between "the button is dead" and "the
    // pointer never reached anything". Pressing the padding strip — inside the
    // button, outside the feedback span — separates the two, and its result is
    // reported rather than asserted so it cannot mask the finding above.
    std::string control_note = "not run (the centred press already worked)";
    if (after_centre == resting_state) {
        const auto strip = root_point(*button, 4.0f, button_bounds.height * 0.5f);
        rig.root->simulate_click(strip);
        settle(rig.clock, 16);
        const auto* control = find_copy_label();
        control_note = std::string("press at the padding strip (")
            + std::to_string(strip.x) + "," + std::to_string(strip.y)
            + ") -> " + (control ? std::string(control->text()) : "<none>");
    }

    INFO("press at the button centre (" << centre.x << "," << centre.y
         << ") hit " << (centre_hit == static_cast<const View*>(copy)
                         ? "the feedback span" : "the button")
         << "; state " << resting_state << " -> " << after_centre
         << "; control: " << control_note);
    CHECK(after_centre != resting_state);

    // Centring has to survive the state change too — the feedback word is a
    // different length from the resting one.
    const auto* settled = find_copy_label();
    REQUIRE(settled != nullptr);
    const float settled_ink = require_centred(*settled);

    // The control for the span measurement itself, and the one the old
    // `ink < label_box.width - 1` was reaching for. The degenerate outcome
    // worth excluding is a width that describes the BOX rather than the word:
    // such a reading is constant across states, because the button does not
    // resize. These two states are different words -- the press above already
    // REQUIREs that -- so their ink has to differ. If it does not, the number
    // both `require_centred` calls just asserted on is not a measurement of
    // any text and neither verdict means anything.
    INFO("resting \"" << resting_state << "\" ink=" << resting_ink
         << "; settled \"" << after_centre << "\" ink=" << settled_ink);
    REQUIRE(resting_ink != settled_ink);
}

// Line 711 of the UX burndown — "Status Info is not truncated and unnecessary
// scrollbars are absent" — stayed open because the two existing detectors are
// blind to it, not because anyone measured it. Both blind spots are real and
// they are different:
//
//   * `text_boxes()` crops each Label against every clipping ancestor and drops
//     a box that lands fully outside. The settings body is a 1246px-tall column
//     inside a 531px clip, so the Status Info row — which sits below the fold on
//     first paint — is dropped outright and never reaches either detector. The
//     crop is not a bug: two runs of text can only collide where both are ON
//     screen, so the overlap detector needs it. That is why this adds a detector
//     rather than weakening one.
//   * `detect_clipped_text()` measures with `Label::intrinsic_width()`, which
//     returns 0 for a wrapped Label by design. Every wrapped label therefore
//     satisfies `0 <= box.width` and passes silently. That is the same mechanism
//     behind the layout dump reporting a painted width of 0.0 for 63 of its 101
//     text-bearing Labels.
//
// Fit is a property of layout, not of scroll position, so the detector below
// measures the slot the parent pinned and asks the shaper where the ink lands.
TEST_CASE("settings text fits the slots the layout gives it",
          "[native-n1][appearance][settings][slot-fit]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto comma = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma,
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    rig.root->layout_children();
    settle(rig.clock, 8);

    const auto fits = spectr::appearance::slot_fits(*rig.root);
    INFO("measured slots with settings open: " << fits.size());
    REQUIRE(fits.size() > 40);

    // The row this line is about must be IN the measurement. Naming it is the
    // control that separates "the text fits" from "the detector never saw it" —
    // the older detector reports an absence here for the second reason.
    // Matched on a stable prefix, not on equality, so lengthening one of these
    // strings to plant a truncation defect does not abort the test before the
    // overflow check it is planted for ever runs.
    const auto slot_for = [&fits](std::string_view prefix)
        -> const spectr::appearance::SlotFit* {
        for (const auto& fit : fits)
            if (fit.text.rfind(prefix, 0) == 0) return &fit;
        return nullptr;
    };
    for (const char* required : {"Status info", "Hover, mute, and drag",
                                 "Build info", "Support and debugging"}) {
        const auto* fit = slot_for(required);
        INFO("required settings label: " << required);
        REQUIRE(fit != nullptr);
        // Unknown extents are not a pass. A run of zeros because nothing could
        // be measured looks exactly like a run of zeros because everything fits.
        REQUIRE(fit->measured);
        INFO("  paints " << fit->painted_width << "px of ink over "
                         << fit->line_count << " line(s) in a " << fit->box.width
                         << "px slot");
    }

    const auto unmeasurable = spectr::appearance::unmeasurable_slots(*rig.root);
    INFO("slots the shaper could not measure:\n"
         << spectr::appearance::join_findings(unmeasurable));
    CHECK(unmeasurable.empty());

    const auto overflowing
        = spectr::appearance::detect_text_overflowing_slot(*rig.root);
    INFO("text wider than its slot:\n"
         << spectr::appearance::join_findings(overflowing));
    CHECK(overflowing.empty());

    // Reported, never asserted — see describe_tall_slots() for why the height
    // axis is noise on a single-line label.
    const auto tall = spectr::appearance::describe_tall_slots(*rig.root);
    INFO("ink taller than its slot (diagnostic, not a gate):\n"
         << spectr::appearance::join_findings(tall));
}

// Line 760 of the UX burndown — "LFO controls have a stable finished layout".
//
// "Finished" is two claims, and they fail differently, so both are asserted:
//
//   * The cluster SETTLES. Its rows mount from an async hydrate
//     (`processing_state_get`), so a layout read too early sees a different tree
//     than the one a person ends up looking at. Capturing twice with a long
//     settle between catches a box that is still moving after the surface is
//     supposed to be done.
//   * The settled geometry is the AUTHORED one. Every row shares the group's
//     left edge and width, every label column is the authored 150px, and every
//     inter-row gap is the authored 10px. That is what "stable finished layout"
//     means as a checkable property: nothing is half-sized, nothing is off its
//     column, nothing is spaced by accident.
//
// Deliberately expressed relative to the group rather than as frozen page
// coordinates. The cluster is the LAST group in a scrolling settings body, so
// absolute y would churn on every unrelated settings row anyone adds above it,
// and a test that breaks for that reason teaches people to re-baseline it.
TEST_CASE("the LFO controls reach a finished layout and hold it",
          "[native-n1][appearance][settings][lfo][layout-stability]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto comma = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma,
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    rig.root->layout_children();
    settle(rig.clock, 8);

    struct ClusterRow {
        std::string label;
        pulp::view::Rect row{};
        float label_column_width = 0.0f;
    };

    const auto first_label_text = [](const View& view) {
        std::string found;
        const auto walk = [&found](const View& node, auto&& self) -> void {
            if (!found.empty()) return;
            if (const auto* label = dynamic_cast<const pulp::view::Label*>(&node))
                if (!label->text().empty()) { found = label->text(); return; }
            for (std::size_t i = 0; i < node.child_count(); ++i)
                self(*node.child_at(i), self);
        };
        walk(view, walk);
        return found;
    };

    // The cluster is reached through its own text, not through an id: the
    // materialized document gives the modulation rows no stable id of their own
    // (every id in it belongs to the preset/snapshot surface), so the label is
    // the only durable handle.
    const auto rows_container = [&](const View& root) -> const View* {
        const auto* anchor_label = find_label(root, "LFO");
        if (anchor_label == nullptr) return nullptr;
        const auto* column = anchor_label->parent();
        const auto* row = column ? column->parent() : nullptr;
        return row ? row->parent() : nullptr;
    };

    const auto capture = [&](const View& root) {
        std::vector<ClusterRow> rows;
        const auto* container = rows_container(root);
        if (container == nullptr) return rows;
        for (std::size_t i = 0; i < container->child_count(); ++i) {
            const auto& row = *container->child_at(i);
            ClusterRow entry;
            entry.label = first_label_text(row);
            entry.row = pulp::view::ViewInspector::absolute_bounds(row);
            if (row.child_count() > 0)
                entry.label_column_width = pulp::view::ViewInspector::absolute_bounds(
                    *row.child_at(0)).width;
            rows.push_back(entry);
        }
        return rows;
    };

    const auto describe = [](const std::vector<ClusterRow>& rows) {
        std::ostringstream out;
        for (const auto& row : rows)
            out << "\n  - \"" << row.label << "\" row (" << row.row.x << ","
                << row.row.y << " " << row.row.width << "x" << row.row.height
                << ") label column " << row.label_column_width << "px";
        return out.str();
    };

    const auto same = [](const std::vector<ClusterRow>& a,
                         const std::vector<ClusterRow>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].label != b[i].label) return false;
            if (a[i].row.x != b[i].row.x || a[i].row.y != b[i].row.y) return false;
            if (a[i].row.width != b[i].row.width) return false;
            if (a[i].row.height != b[i].row.height) return false;
            if (a[i].label_column_width != b[i].label_column_width) return false;
        }
        return true;
    };

    // The authored numbers, from the materialized document: SpectrSettingsGroup
    // lays its rows out in a `flexDirection: column, gap: 10` stack, and
    // SpectrSettingsField pins its label column at `width: 150, flexShrink: 0`.
    constexpr float kAuthoredRowGap = 10.0f;
    constexpr float kAuthoredLabelColumn = 150.0f;

    // Every row in the group is MOUNTED at mount, including the ones whose LFO
    // is off, because the widget bridge has no insert-at-index and no move: a
    // row that mounts late is appended rather than placed, which is what made
    // the group re-order itself as the user flipped toggles. A closed row is
    // hidden with display:none instead, which the runtime maps to
    // setVisible(id, false) -- View::visible() skips render AND layout, so the
    // row collapses to exactly 0x0 and leaves no gap.
    //
    // So the cluster has two populations and each gets its own assertion. The
    // disclosed rows must have a finished, authored layout; the closed rows
    // must be EXACTLY 0x0. A half-laid-out row fails both, which is the case
    // that would otherwise slip through a plain "skip the empty ones".
    const auto check_finished = [&](const std::vector<ClusterRow>& rows,
                                    const char* phase) {
        INFO(phase << " cluster:" << describe(rows));
        REQUIRE(rows.size() >= 2);

        std::vector<ClusterRow> shown;
        for (const auto& row : rows) {
            if (row.row.width > 0.0f || row.row.height > 0.0f) {
                shown.push_back(row);
                continue;
            }
            INFO("closed row \"" << row.label << "\"");
            // Not merely "small": a closed disclosure is no box at all.
            CHECK(row.row.width == 0.0f);
            CHECK(row.row.height == 0.0f);
        }

        // Both toggles are unconditional, so at least those two are always
        // disclosed. A run that found fewer has lost the rows, not hidden them.
        INFO("disclosed rows:" << describe(shown));
        REQUIRE(shown.size() >= 2);
        for (std::size_t i = 0; i < shown.size(); ++i) {
            INFO("disclosed row " << i << " \"" << shown[i].label << "\"");
            // A row that never finished laying out is the defect this catches
            // most directly: a zero or negative box.
            CHECK(shown[i].row.width > 0.0f);
            CHECK(shown[i].row.height > 0.0f);
            CHECK(shown[i].row.x == Catch::Approx(shown[0].row.x).margin(0.01f));
            CHECK(shown[i].row.width == Catch::Approx(shown[0].row.width).margin(0.01f));
            CHECK(shown[i].label_column_width
                  == Catch::Approx(kAuthoredLabelColumn).margin(0.01f));
            if (i > 0) {
                // Measured between DISCLOSED neighbours: a hidden row between
                // them contributes no height and no gap, so the authored 10px
                // must still be the whole distance. That is the assertion that
                // proves hiding a row really does reclaim its space.
                const float gap = shown[i].row.y
                                  - (shown[i - 1].row.y + shown[i - 1].row.height);
                INFO("gap above this row: " << gap);
                CHECK(gap == Catch::Approx(kAuthoredRowGap).margin(0.01f));
            }
        }
    };

    const auto settled = capture(*rig.root);
    INFO("modulation cluster at settle:" << describe(settled));
    REQUIRE(settled.size() >= 2);
    // Control on the handle: the rows really are the LFO cluster and not some
    // other column the parent walk happened to land on.
    REQUIRE(settled.front().label == "LFO");
    check_finished(settled, "settled");

    // Hold the surface open far past the point it claims to be finished. A row
    // that is still moving here — a late hydrate, an animation that never
    // converges, a size derived from something transient — shows up as a
    // different capture.
    settle(rig.clock, 240);
    rig.root->layout_children();
    settle(rig.clock, 8);
    const auto held = capture(*rig.root);
    INFO("after holding:" << describe(held));
    CHECK(same(settled, held));

    // Control on the comparator's RECT path. The reflow control further down
    // compares vectors of different length, so it exits through `same()`'s size
    // guard and never reaches the per-field clauses — delete them and every
    // assertion in this test would still pass, leaving the stability half
    // unprotected. Perturb each compared field in turn at EQUAL row count and
    // require its own clause to catch it.
    const auto differs_when = [&](const char* field, auto&& mutate) {
        auto perturbed = held;
        REQUIRE_FALSE(perturbed.empty());
        mutate(perturbed.front());
        INFO("perturbed field: " << field);
        REQUIRE(perturbed.size() == held.size());
        CHECK_FALSE(same(held, perturbed));
    };
    differs_when("label", [](ClusterRow& r) { r.label += "-CONTROL"; });
    differs_when("row.x", [](ClusterRow& r) { r.row.x += 1.0f; });
    differs_when("row.y", [](ClusterRow& r) { r.row.y += 1.0f; });
    differs_when("row.width", [](ClusterRow& r) { r.row.width += 1.0f; });
    differs_when("row.height", [](ClusterRow& r) { r.row.height += 1.0f; });
    differs_when("label column",
                 [](ClusterRow& r) { r.label_column_width += 1.0f; });

    // Turning the LFO on DISCLOSES rows that were already mounted; it does not
    // mount them. The row count is therefore constant by design -- that
    // constancy is exactly what keeps the group's order from depending on the
    // order the user flips toggles -- so the growth to assert is in the number
    // of rows that have a box, not in the size of the cluster.
    const auto disclosed = [](const std::vector<ClusterRow>& rows) {
        std::size_t count = 0;
        for (const auto& row : rows)
            if (row.row.width > 0.0f && row.row.height > 0.0f) ++count;
        return count;
    };

    activate(rig, "[data-spectr-settings-modulation] [data-spectr-setting-toggle]");
    settle(rig.clock, 16);
    rig.root->layout_children();
    settle(rig.clock, 8);

    const auto enabled = capture(*rig.root);
    INFO("with the LFO enabled:" << describe(enabled));
    // The population is fixed; only its disclosure moves.
    REQUIRE(enabled.size() == settled.size());
    INFO("disclosed before: " << disclosed(settled)
         << ", after: " << disclosed(enabled));
    REQUIRE(disclosed(enabled) > disclosed(settled));
    // Control on the comparator itself. `same()` returning true across a hold is
    // only meaningful if `same()` can return false at all, so the one reflow this
    // surface is KNOWN to perform is asserted to be seen.
    CHECK_FALSE(same(settled, enabled));
    // Control: the reflow really did mount the dependent rows, so a stable
    // comparison below is a comparison of the grown cluster.
    REQUIRE(find_label(*rig.root, "Shape") != nullptr);
    REQUIRE(find_label(*rig.root, "Rate") != nullptr);
    REQUIRE(find_label(*rig.root, "Depth") != nullptr);
    check_finished(enabled, "enabled");

    settle(rig.clock, 240);
    rig.root->layout_children();
    settle(rig.clock, 8);
    const auto enabled_held = capture(*rig.root);
    INFO("enabled, after holding:" << describe(enabled_held));
    CHECK(same(enabled, enabled_held));
}

// The second half of line 711. Pulp's plain `View` paints no scrollbar chrome:
// `overflow: scroll` is forwarded to Yoga for descendant measurement and
// otherwise clips like `hidden`. The settings body IS a plain `View` with
// `overflow: scroll`, so the only way a bar can reach this surface is one of the
// virtualised containers being in the tree. That is the falsifiable claim; the
// control section below proves the probe can see one when it is there.
TEST_CASE("no scrollbar-painting widget reaches the settings surface",
          "[native-n1][appearance][settings][scrollbar]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.root->layout_children();
    settle(rig.clock, 4);

    const auto comma = static_cast<pulp::view::KeyCode>(',');
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = comma,
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    rig.root->layout_children();
    settle(rig.clock, 8);

    // Control: the surface really is built. An empty tree also reports no
    // scrollbar.
    REQUIRE(spectr::appearance::slot_fits(*rig.root).size() > 40);

    const auto bars = spectr::appearance::detect_scrollbar_painters(*rig.root);
    INFO("scrollbar painters:\n" << spectr::appearance::join_findings(bars));
    CHECK(bars.empty());
}

// Negative control for the two appearance detectors. Both report an ABSENCE on
// the real surfaces, and an absence is worthless without proof the instrument
// can see the thing it says is not there — a detector wired to the wrong tree,
// or measuring the wrong property, also reports nothing. So build the two
// defects deliberately and require each detector to name them.
TEST_CASE("the appearance detectors report defects that are deliberately built",
          "[native-n1][appearance][control]") {
    using namespace pulp::view;

    SECTION("overlapping text is found") {
        auto root = std::make_unique<View>();
        root->flex().direction = FlexDirection::row;

        auto first = std::make_unique<Label>();
        first->set_text("CONTROL OVERLAP LEFT");
        first->set_font_size(16.0f);

        auto second = std::make_unique<Label>();
        second->set_text("CONTROL OVERLAP RIGHT");
        second->set_font_size(16.0f);
        // Pull the second run back across the first so their glyphs share pixels.
        second->flex().margin_left = -60.0f;

        root->add_child(std::move(first));
        root->add_child(std::move(second));
        root->set_bounds(pulp::view::Rect{0.0f, 0.0f, 400.0f, 40.0f});
        root->layout_children();

        // Control on the control: a green detector below has to mean "looked and
        // found nothing", never "measured nothing".
        REQUIRE(spectr::appearance::text_boxes(*root).size() == 2);

        const auto overlaps = spectr::appearance::detect_overlapping_text(*root);
        INFO("overlap findings:\n" << spectr::appearance::join_findings(overlaps));
        CHECK(overlaps.size() == 1);
    }

    SECTION("text wider than its box is found") {
        auto root = std::make_unique<View>();
        auto pinned = std::make_unique<Label>();
        pinned->set_text("CONTROL TEXT FAR WIDER THAN ITS BOX");
        pinned->set_font_size(16.0f);
        pinned->flex().max_width = 24.0f;

        const auto* pinned_ptr = pinned.get();
        root->add_child(std::move(pinned));
        root->set_bounds(pulp::view::Rect{0.0f, 0.0f, 400.0f, 40.0f});
        root->layout_children();

        REQUIRE(spectr::appearance::text_boxes(*root).size() == 1);
        REQUIRE(pinned_ptr->intrinsic_width() > 30.0f);

        const auto clipped = spectr::appearance::detect_clipped_text(*root);
        INFO("clipped findings:\n" << spectr::appearance::join_findings(clipped));
        CHECK(clipped.size() == 1);
    }

    SECTION("text wider than a pinned slot is found by the slot-fit detector") {
        auto root = std::make_unique<View>();
        root->flex().direction = FlexDirection::row;

        auto pinned = std::make_unique<Label>();
        pinned->set_text("CONTROL SLOT TEXT FAR WIDER THAN THE COLUMN IT SITS IN");
        pinned->set_font_size(11.0f);
        // The shape the real defect has: a label column the parent pins, so the
        // text cannot push it wider and has to be cut instead.
        pinned->flex().preferred_width = 150.0f;
        pinned->flex().max_width = 150.0f;
        pinned->flex().flex_shrink = 0.0f;

        root->add_child(std::move(pinned));
        root->set_bounds(pulp::view::Rect{0.0f, 0.0f, 400.0f, 40.0f});
        root->layout_children();

        // Control on the control, both halves: the detector measured exactly one
        // label, and it measured it for real rather than reporting unknown.
        const auto fits = spectr::appearance::slot_fits(*root);
        REQUIRE(fits.size() == 1);
        REQUIRE(fits[0].measured);
        REQUIRE(fits[0].box.width == Catch::Approx(150.0f).margin(0.5f));

        const auto findings
            = spectr::appearance::detect_text_overflowing_slot(*root);
        INFO("slot-fit findings:\n" << spectr::appearance::join_findings(findings));
        CHECK(findings.size() == 1);
    }

    SECTION("a label scrolled out of its clip is still measured") {
        // This is the blindness line 711 was open on, built deliberately. The
        // settings body is a tall column inside a short clip; a row below the
        // fold is cropped to nothing by `text_boxes()` and never reaches the
        // older detector, which then reports an absence that means "did not
        // look". The slot-fit path must still see it.
        auto root = std::make_unique<View>();
        auto clip = std::make_unique<View>();
        clip->set_overflow(View::Overflow::scroll);

        auto below_the_fold = std::make_unique<Label>();
        below_the_fold->set_text("CONTROL ROW BELOW THE FOLD WIDER THAN ITS COLUMN");
        below_the_fold->set_font_size(11.0f);
        below_the_fold->flex().preferred_width = 150.0f;
        below_the_fold->flex().max_width = 150.0f;
        below_the_fold->flex().flex_shrink = 0.0f;
        // Push it past the bottom of the clip, the way scroll position does.
        below_the_fold->flex().margin_top = 400.0f;

        clip->add_child(std::move(below_the_fold));
        auto* clip_ptr = clip.get();
        root->add_child(std::move(clip));
        root->set_bounds(pulp::view::Rect{0.0f, 0.0f, 400.0f, 120.0f});
        clip_ptr->set_bounds(pulp::view::Rect{0.0f, 0.0f, 400.0f, 120.0f});
        root->layout_children();

        // The old instrument drops it: this REQUIRE is the proof the blind spot
        // is real, not an assumption about it.
        REQUIRE(spectr::appearance::text_boxes(*root).empty());
        REQUIRE(spectr::appearance::detect_clipped_text(*root).empty());

        const auto fits = spectr::appearance::slot_fits(*root);
        REQUIRE(fits.size() == 1);
        REQUIRE(fits[0].measured);

        const auto findings
            = spectr::appearance::detect_text_overflowing_slot(*root);
        INFO("slot-fit findings:\n" << spectr::appearance::join_findings(findings));
        CHECK(findings.size() == 1);
    }

    SECTION("a scrollbar-painting widget is found when one is present") {
        auto root = std::make_unique<View>();
        // Control: the probe reports nothing on a tree that has no painter, so a
        // green on the real surface is not the probe being inert.
        REQUIRE(spectr::appearance::detect_scrollbar_painters(*root).empty());

        auto list = std::make_unique<VirtualList>();
        list->set_row_count(500);
        list->set_row_height(18.0f);
        root->add_child(std::move(list));
        root->set_bounds(pulp::view::Rect{0.0f, 0.0f, 400.0f, 120.0f});
        root->layout_children();

        const auto bars = spectr::appearance::detect_scrollbar_painters(*root);
        INFO("scrollbar findings:\n" << spectr::appearance::join_findings(bars));
        CHECK(bars.size() == 1);
    }
}

// Arrow traversal must reach EVERY option, in visual order, and Enter must
// commit the option the highlight is actually on. A single-step assertion
// cannot see either failure mode this covers: a second keyboard owner that
// swallows the key leaves the highlight one step behind from the first press
// onward, and an index kept by a shadow state machine commits a different
// option than the one painted.
TEST_CASE("native dropdown arrows reach every option and commit the highlighted one",
          "[native-n1][state-parity][dropdown][arrow-traversal]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    const std::array<std::string_view, 6> menus{
        "bands", "edit", "analyzer", "overflow", "pattern", "length"};
    for (const auto menu : menus) {
        INFO("menu=" << menu);
        const auto root = std::string{"[data-spectr-menu-root=\""}
            + std::string(menu) + "\"]";
        const auto trigger = root + " [data-spectr-menu-trigger]";
        const auto options = root + " [data-spectr-menu-options]";
        const auto items_js = "Array.from(document.querySelectorAll("
            + js_string(options + " button") + "))";

        const auto press = [&](pulp::view::KeyCode key) {
            REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
                *rig.root, static_cast<int>(key), pulp::view::kModNone, true));
            settle(rig.clock, 6);
        };
        const auto open_from_keyboard = [&] {
            rig.bridge().load_script(
                "document.querySelector(" + js_string(trigger) + ").focus()",
                "spectr-arrow-focus-trigger");
            press(pulp::view::KeyCode::down);
        };
        // Exactly one option may claim the authoritative highlight, and it must
        // be the one at `expected` in DOM order.
        const auto require_highlight_at = [&](int expected, const char* what) {
            require_runtime_contract(
                rig,
                "(() => { const items = " + items_js + "; "
                "const marked = Array.from(document.querySelectorAll("
                "'[data-pulp-popup-active=\"true\"]')); "
                "return marked.length === 1 && items.length > " + std::to_string(expected)
                + " && marked[0] === items[" + std::to_string(expected) + "]; })()",
                std::string(what) + " (expected index " + std::to_string(expected) + ")");
        };

        open_from_keyboard();
        const auto count = std::stoi(runtime_string(
            rig, "String(" + items_js + ".length)", "spectr-arrow-option-count"));
        REQUIRE(count >= 3);
        // It opens on the checked option where the menu marks one (LENGTH
        // opens on 1 bar, past its fractions), else on the first.
        const auto seed_raw = runtime_string(
            rig, "String(Math.max(0, " + items_js
                 + ".findIndex((n) => n.getAttribute('aria-selected') === 'true')))",
            "spectr-arrow-option-seed");
        const int seed = std::stoi(seed_raw.substr(0, seed_raw.find_first_not_of("0123456789")));
        require_highlight_at(seed, "opening ArrowDown did not highlight the checked option");

        // Walk the whole list. Every successive press must advance exactly one.
        for (int step = 1; step < count; ++step) {
            press(pulp::view::KeyCode::down);
            require_highlight_at((seed + step) % count, "ArrowDown skipped or stalled");
        }
        // One more comes back round rather than sticking at the end.
        press(pulp::view::KeyCode::down);
        require_highlight_at(seed, "ArrowDown did not wrap round the list");
        // And the reverse direction walks back down the same path.
        for (int step = count - 1; step >= 0; --step) {
            press(pulp::view::KeyCode::up);
            require_highlight_at((seed + step) % count, "ArrowUp skipped or stalled");
        }
        press(pulp::view::KeyCode::escape);
        settle(rig.clock, 8);
        require_runtime_contract(
            rig, "!document.querySelector(" + js_string(options) + ")",
            "Escape left the dropdown open");
    }

    // Enter must commit the option the highlight is on — not an index kept
    // somewhere else. Two different targets, so an off-by-one cannot pass.
    for (const int steps : {1, 3}) {
        INFO("bands commit after steps=" << steps);
        const std::string root = "[data-spectr-menu-root=\"bands\"]";
        const std::string options = root + " [data-spectr-menu-options]";
        rig.bridge().load_script(
            "document.querySelector(" + js_string(root + " [data-spectr-menu-trigger]")
            + ").focus()", "spectr-arrow-focus-bands");
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::down),
            pulp::view::kModNone, true));
        settle(rig.clock, 8);
        for (int i = 0; i < steps; ++i) {
            REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
                *rig.root, static_cast<int>(pulp::view::KeyCode::down),
                pulp::view::kModNone, true));
            settle(rig.clock, 6);
        }
        // Read the highlighted label BEFORE committing, so the expectation is
        // the painted state rather than a number this test assumes.
        const auto highlighted_raw = runtime_string(
            rig,
            "String(((document.querySelector('[data-pulp-popup-active=\"true\"]')"
            "?.textContent || '').match(/\\d+/) || [''])[0])",
            "spectr-arrow-highlighted-label");
        // runtime_string returns the tail of a thrown message, so the value
        // arrives with the engine's stack trace appended. Keep the digits.
        const auto highlighted = highlighted_raw.substr(
            0, highlighted_raw.find_first_not_of("0123456789"));
        REQUIRE_FALSE(highlighted.empty());
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::enter),
            pulp::view::kModNone, true));
        settle(rig.clock, 12);
        require_app_state(rig, "s.settings.bandCount === " + highlighted,
                          "Enter committed an option other than the highlighted one");
        REQUIRE(spectr::visible_count(rig.processor.layout()) == std::stoi(highlighted));
    }
    storage.require_unchanged();
}

// The preset manager's list is a plain scroll region, not a Pulp popup, so its
// keyboard walk is Spectr's own. Two things have to hold at once and only a
// combined assertion sees both: every ArrowDown must advance the selection by
// exactly one row (an owner that swallows a key stalls it), and the
// default-on-open star must stay on its own row while the selection walks away
// from it (a walk that reuses the default slot silently retargets what loads).
TEST_CASE("preset manager arrows walk the selection and leave the default marker",
          "[native-n1][state-parity][preset-manager][arrow-nav]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-pattern-manage]");
    require_app_state(rig, "s.managerOpen === true",
                      "pattern manager did not open");

    const std::string rows_js = "Array.from(document.querySelectorAll("
        "'[data-spectr-pattern-list] [data-spectr-pattern-id]'))";
    // runtime_string returns the tail of a thrown message, so the engine's
    // stack trace arrives appended. Every value read here is a bare token.
    const auto first_token = [](const std::string& value) {
        const auto end = value.find_first_of(" \t\r\n");
        return end == std::string::npos ? value : value.substr(0, end);
    };
    const auto count = std::stoi(first_token(runtime_string(
        rig, "String(" + rows_js + ".length)", "spectr-preset-row-count")));
    REQUIRE(count >= 8);

    // Pin the default onto a row the walk will move off, so "the star did not
    // move" is a claim with something to disprove it.
    activate(rig, "[data-spectr-pattern-id=\"factory:flat\"]");
    settle(rig.clock, 4);
    activate(rig, "[data-spectr-manager-action=\"set-default\"]");
    settle(rig.clock, 6);
    require_runtime_contract(
        rig,
        "(() => { const marked = Array.from(document.querySelectorAll("
        "'[data-spectr-pattern-default=\"true\"]')); return marked.length === 1"
        " && marked[0].getAttribute('data-spectr-pattern-id') === 'factory:flat'; })()",
        "SET AS DEFAULT did not mark exactly one row");

    const auto press = [&](pulp::view::KeyCode key) {
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(key), pulp::view::kModNone, true));
        settle(rig.clock, 6);
    };
    // Exactly one row selected, at `expected`, and the star still alone on the
    // row it was pinned to.
    const auto require_row = [&](int expected, const char* what) {
        require_runtime_contract(
            rig,
            "(() => { const rows = " + rows_js + "; "
            "const chosen = rows.filter(r => r.getAttribute("
            "'data-spectr-pattern-selected') === 'true'); "
            "const starred = rows.filter(r => r.getAttribute("
            "'data-spectr-pattern-default') === 'true'); "
            "return chosen.length === 1 && rows.length > " + std::to_string(expected)
            + " && chosen[0] === rows[" + std::to_string(expected) + "] "
            "&& starred.length === 1 && starred[0].getAttribute("
            "'data-spectr-pattern-id') === 'factory:flat'; })()",
            std::string(what) + " (expected row " + std::to_string(expected) + ")");
    };

    // Clicking the row above pinned the selection to it; from there the walk is
    // a plain step per press over the whole list, wrapping at the end.
    require_row(0, "clicking a preset row did not select exactly that row");
    for (int step = 1; step < count; ++step) {
        press(pulp::view::KeyCode::down);
        require_row(step, "ArrowDown skipped or stalled in the preset list");
    }
    press(pulp::view::KeyCode::down);
    require_row(0, "ArrowDown did not wrap to the first preset");
    for (int step = count - 1; step >= 0; --step) {
        press(pulp::view::KeyCode::up);
        require_row(step, "ArrowUp skipped or stalled in the preset list");
    }
    storage.require_unchanged();
}

// COR-2. Dragging one minimap trim must never move the other. Every existing
// block that says "opposite trim" drives `minimap-drag` (the window body) or a
// wheel pan, and every block that drives `minimap-resize` (the edge handles)
// asserts only that the span changed -- so the literal claim had no test. The
// edge handler seeds BOTH bounds from a pointerdown snapshot and writes only
// one of them, which is what makes the invariant hold; a regression that
// rewrites the snapshot mid-drag, swaps the bounds, or normalises the pair in
// the commit would still change the span and still pass every older check.
//
// Both edges are exercised, and the held bound is read twice: mid-gesture (the
// live viewport, before React reconciles) and after release (the published
// state React caught up to). A defect that only lands on release is exactly
// the kind a mid-gesture-only assertion cannot see.
TEST_CASE("native minimap edge drag cannot move the opposite trim",
          "[native-n1][state-parity][minimap][cor-2]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    rig.bridge().load_script(R"js((() => {
      const selector = '[data-spectr-filter-surface]';
      const surface = document.querySelector(selector);
      if (!surface) throw new Error('filter surface missing');
      const hooks = globalThis.__spectrTestHooks;
      const fire = (type, x, y, pointerId, buttons) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
          clientX: x, clientY: y, pointerId, button: 0, buttons
        })) throw new Error('minimap edge activation failed: ' + type);
      };
      // The bound the dragged edge owns, and the bound it must leave alone.
      const opposite = {left: 'lmax', right: 'lmin'};
      const dragEdge = (edge, delta, pointerId) => {
        const before = hooks.renderState();
        const held = opposite[edge];
        const fullMin = Math.log10(20);
        const fullSpan = Math.log10(20000) - fullMin;
        const innerX = 56, innerWidth = surface.clientWidth - 112;
        const fraction = edge === 'left'
          ? (before.view.lmin - fullMin) / fullSpan
          : (before.view.lmax - fullMin) / fullSpan;
        const x = innerX + fraction * innerWidth;
        const y = Array.from({length: surface.clientHeight}, (_, c) => c)
          .find(c => hooks.minimapHit(x, c) === edge);
        if (!Number.isFinite(y))
          throw new Error('minimap ' + edge + ' handle is not hittable');
        fire('pointerdown', x, y, pointerId, 1);
        // Two moves, not one: a handler that accumulates into the untouched
        // bound instead of re-deriving it from the pointerdown snapshot only
        // drifts on the SECOND move.
        fire('pointermove', x + delta * 0.5, y, pointerId, 1);
        fire('pointermove', x + delta, y, pointerId, 1);
        if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
          globalThis.__pulpRuntimeSettle__(2);
        const during = hooks.renderState();
        // Positive control on the gesture itself. Without it, an edge drag
        // that did nothing at all would satisfy the invariance check below.
        const moved = edge === 'left'
          ? Math.abs(during.view.lmin - before.view.lmin)
          : Math.abs(during.view.lmax - before.view.lmax);
        if (!(moved > 1e-6))
          throw new Error(edge + ' handle drag did not move its own trim');
        if (Math.abs(during.view[held] - before.view[held]) > 1e-9)
          throw new Error(edge + ' drag moved the opposite trim mid-gesture: '
            + held + ' ' + before.view[held] + ' -> ' + during.view[held]);
        fire('pointerup', x + delta, y, pointerId, 0);
        if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
          globalThis.__pulpRuntimeSettle__(4);
        const released = hooks.renderState();
        if (Math.abs(released.view[held] - before.view[held]) > 1e-9)
          throw new Error(edge + ' drag moved the opposite trim on release: '
            + held + ' ' + before.view[held] + ' -> ' + released.view[held]);
        if (Math.abs(released.reactView[held] - before.view[held]) > 1e-9)
          throw new Error(edge + ' release published a moved opposite trim: '
            + held + ' ' + before.view[held] + ' -> ' + released.reactView[held]);
      };
      // Inward on both edges (narrowing), then outward, so a clamp that
      // reflects rather than absorbs is covered in both directions.
      dragEdge('left', 60, 81);
      dragEdge('right', -60, 82);
      dragEdge('left', -30, 83);
      dragEdge('right', 30, 84);
    })();)js", "spectr-native-minimap-edge-trim-invariant");
    settle(rig.clock, 8);
    storage.require_unchanged();
}

// COR-2, indirect path. The invariant above holds for the duration of the
// gesture, but the viewport does not stay in the editor: it is encoded into
// two automation parameters and decoded back on every host read, preset
// restore, and automation pass. `encode_viewport` widens any sub-octave span
// about the window's CENTER, so a window the user narrowed past one octave by
// dragging ONE edge comes back with the OTHER edge moved. The editor clamp and
// the codec floor therefore have to agree, and they did not: the edge handler
// stopped at 0.1 decades while `kViewportMinWidthLog` is log10(2) ~= 0.30103.
//
// The C++ half pins the codec behaviour that makes the disagreement matter
// (both directions, so it stays a statement about the floor and not about one
// magic number). The JS half proves the shipping editor now refuses to hand
// the codec a window it cannot represent.
TEST_CASE("minimap edge drag cannot narrow past the viewport codec floor",
          "[native-n1][state-parity][minimap][cor-2]") {
    SECTION("a sub-octave window round-trips with the untouched trim moved") {
        // What a left-edge drag to the old 0.1-decade clamp produced: the user
        // moved only lmin, but lmax comes back 0.1 decades higher.
        const spectr::Viewport narrowed{
            std::pow(10.0f, 3.0f), std::pow(10.0f, 3.1f)};
        const auto [center, width] = spectr::encode_viewport(narrowed);
        const auto restored = spectr::decode_viewport(center, width);
        REQUIRE(std::log10(restored.max_hz)
                > std::log10(narrowed.max_hz) + 0.05f);
        REQUIRE(std::log10(restored.min_hz)
                < std::log10(narrowed.min_hz) - 0.05f);
    }

    SECTION("a one-octave window round-trips with both trims intact") {
        const spectr::Viewport octave{
            std::pow(10.0f, 3.0f),
            std::pow(10.0f, 3.0f + spectr::kViewportMinWidthLog)};
        const auto [center, width] = spectr::encode_viewport(octave);
        const auto restored = spectr::decode_viewport(center, width);
        REQUIRE(std::log10(restored.min_hz)
                == Catch::Approx(std::log10(octave.min_hz)).margin(1e-4));
        REQUIRE(std::log10(restored.max_hz)
                == Catch::Approx(std::log10(octave.max_hz)).margin(1e-4));
    }

    SECTION("the shipping editor will not drag a trim past the floor") {
        PatternStoragePoison storage;
        NativeEditorRig rig;
        require_home(rig);

        rig.bridge().load_script(R"js((() => {
          const selector = '[data-spectr-filter-surface]';
          const surface = document.querySelector(selector);
          if (!surface) throw new Error('filter surface missing');
          const hooks = globalThis.__spectrTestHooks;
          const fire = (type, x, y, pointerId, buttons) => {
            if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
              clientX: x, clientY: y, pointerId, button: 0, buttons
            })) throw new Error('minimap edge activation failed: ' + type);
          };
          // spectr::kViewportMinWidthLog.
          const floor = Math.log10(2);
          const fullMin = Math.log10(20);
          const fullSpan = Math.log10(20000) - fullMin;
          const innerX = 56, innerWidth = surface.clientWidth - 112;
          const toX = (decades) =>
            innerX + (decades - fullMin) / fullSpan * innerWidth;
          const handleY = (edge, x) => {
            const y = Array.from({length: surface.clientHeight}, (_, c) => c)
              .find(c => hooks.minimapHit(x, c) === edge);
            if (!Number.isFinite(y))
              throw new Error('minimap ' + edge + ' handle is not hittable');
            return y;
          };
          const dragEdgeTo = (edge, decades, pointerId) => {
            const state = hooks.renderState();
            const x = toX(edge === 'left' ? state.view.lmin : state.view.lmax);
            const y = handleY(edge, x);
            fire('pointerdown', x, y, pointerId, 1);
            fire('pointermove', toX(decades), y, pointerId, 1);
            fire('pointerup', toX(decades), y, pointerId, 0);
          };
          // The default window is exactly one octave wide -- already sitting on
          // the floor -- so a squeeze from there would prove nothing. Open it
          // to the full range through the editor's own left handle. This rig
          // republishes the host's viewport on every settle, so the widened
          // window is only live between gestures: do not settle here.
          dragEdgeTo('left', fullMin - 0.5, 87);
          const opened = hooks.renderState();
          if (!(opened.view.lmax - opened.view.lmin > floor + 0.5))
            throw new Error('could not open the fixture window: '
              + opened.view.lmin + ' .. ' + opened.view.lmax);

          const squeeze = (edge, pointerId) => {
            const before = hooks.renderState();
            const startSpan = before.view.lmax - before.view.lmin;
            if (!(startSpan > floor + 0.05))
              throw new Error('fixture window is already at the floor: '
                + before.view.lmin + ' .. ' + before.view.lmax);
            const x = toX(edge === 'left' ? before.view.lmin : before.view.lmax);
            const y = handleY(edge, x);
            // Aim a long way PAST the opposite trim, so the clamp -- not the
            // pointer -- is what decides where the trim stops.
            const target = edge === 'left'
              ? before.view.lmax + 0.2
              : before.view.lmin - 0.2;
            fire('pointerdown', x, y, pointerId, 1);
            fire('pointermove', toX(target), y, pointerId, 1);
            const during = hooks.renderState();
            const span = during.view.lmax - during.view.lmin;
            // Positive control: a drag that did nothing would satisfy the
            // floor check below for the wrong reason.
            if (!(span < startSpan - 1e-6))
              throw new Error(edge + ' squeeze did not narrow the window: '
                + startSpan + ' -> ' + span);
            // The trim the gesture does NOT own must not have moved to make
            // room -- the clamp absorbs the overshoot on one side only.
            const held = edge === 'left' ? 'lmax' : 'lmin';
            if (Math.abs(during.view[held] - before.view[held]) > 1e-9)
              throw new Error(edge + ' squeeze moved the opposite trim');
            if (span < floor - 1e-6)
              throw new Error(edge + ' drag narrowed past the codec floor: '
                + span + ' < ' + floor);
            fire('pointerup', toX(target), y, pointerId, 0);
          };
          squeeze('left', 91);
          // Re-open before the second squeeze: the first one left the window
          // sitting exactly on the floor.
          dragEdgeTo('left', fullMin - 0.5, 93);
          squeeze('right', 92);
        })();)js", "spectr-native-minimap-edge-floor");
        settle(rig.clock, 8);
        storage.require_unchanged();
    }
}

// Switching from one open dropdown to a different one must cost ONE press.
//
// Pulp's overlay-dismissal policy claims every semantically modal control
// (`role="listbox"|"menu"|"dialog"`, `aria-modal`) with consume=true, so a
// press outside an open menu closes it WITHOUT also operating whatever sits
// under the press. The counterpart -- `View::overlay_trigger()`, the mark that
// says "this control OPENS an overlay" -- is what keeps that rule from taxing
// the one press the user actually meant. An unmarked trigger is spent entirely
// on the dismissal, and the menu the user aimed at needs a second press.
//
// The assertions below are written against `route_press_to_active_overlay`,
// the shared policy verb every Pulp host calls, rather than against
// `simulate_click`, which bypasses the policy entirely and therefore cannot
// see this defect at all.
namespace {

int count_views(const View& view) {
    int total = 1;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        total += count_views(*view.child_at(index));
    return total;
}

int count_overlay_triggers(const View& view) {
    int total = view.overlay_trigger() ? 1 : 0;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        total += count_overlay_triggers(*view.child_at(index));
    return total;
}

Point trigger_centre(const View& root, std::string_view label_text) {
    const auto* label = find_label(root, label_text);
    REQUIRE(label != nullptr);
    const auto* owner = nearest_click_target(label);
    REQUIRE(owner != nullptr);
    const auto bounds = owner->bounds();
    REQUIRE(bounds.width > 0.0f);
    REQUIRE(bounds.height > 0.0f);
    return root_point(*owner, bounds.width * 0.5f, bounds.height * 0.5f);
}

// The exact order every Pulp host runs a press in (window_host_mac.mm and the
// plug-in hosts share these verbs): consult the overlay slot first, stop when
// the dismissal consumed the press, otherwise let the ordinary tree receive it.
// Returns whether the press reached the tree -- i.e. whether this press could
// possibly have opened anything.
struct HostPressResult {
    pulp::view::OverlayPressRouting routing =
        pulp::view::OverlayPressRouting::no_overlay;
    bool consumed = false;
    bool reached_tree = false;
};

HostPressResult host_click(NativeEditorRig& rig, Point point) {
    HostPressResult result;
    // The pointer is where it presses: a host has delivered the move that
    // brought it there (-mouseMoved:) before the press. A trigger forgets a
    // dismissal of its own dropdown on pointer entry, so a press that never
    // moved the pointer would read as the press that did the dismissing.
    pulp::view::deliver_hover_move(*rig.root, point);
    const auto press =
        pulp::view::route_press_to_active_overlay(*rig.root, point);
    result.routing = press.routing;
    result.consumed = press.consume_press;
    if (!press.consume_press) {
        rig.root->simulate_click(point);
        result.reached_tree = true;
    }
    settle(rig.clock, 30);
    return result;
}

// Which chrome menu roots currently hold an open popover, as the DOM sees it.
// Asserted through the runtime rather than the view tree so the reading is
// about the app's own state, not about which View happens to hold the slot.
void require_open_menu(NativeEditorRig& rig, std::string_view expected) {
    const auto script = std::string{R"js((() => {
      const roots = Array.from(document.querySelectorAll('[data-spectr-menu-root]'));
      if (!roots.length) throw new Error('no menu roots in the document at all');
      const owner = (node) => {
        for (let n = node; n; n = n.parentElement) {
          const name = n.getAttribute && n.getAttribute('data-spectr-menu-root');
          if (name) return name;
        }
        return '';
      };
      const open = Array.from(
          document.querySelectorAll('[data-spectr-menu-options]'))
        .map(owner)
        .filter(Boolean)
        .sort()
        .join(',');
      const want = )js"} + js_string(expected) + R"js(;
      if (open !== want)
        throw new Error('open menus are [' + open + '], expected [' + want
          + '] over ' + roots.length + ' menu roots');
    })();)js";
    rig.bridge().load_script(script, "spectr-native-open-menu-contract");
}

// Settings is a modal dialog rather than a chrome menu, so it has its own
// liveness marker; `data-spectr-settings-live` is what the runtime itself
// gates its overlay claim on.
void require_settings_open(NativeEditorRig& rig, bool expected) {
    const auto script = std::string{R"js((() => {
      const panel = document.querySelector('[data-spectr-settings-panel]');
      const live = !!panel
        && panel.getAttribute('data-spectr-settings-live') === 'true';
      if (live !== )js"} + (expected ? "true" : "false") + R"js()
        throw new Error('settings live=' + live + ' panel=' + !!panel);
    })();)js";
    rig.bridge().load_script(script, "spectr-native-settings-open-contract");
}

}  // namespace

TEST_CASE("switching native dropdowns costs one press",
          "[native-n1][state-parity][dropdown][overlay-trigger]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Positive control for the instrument. A tree walk that found nothing
    // because it walked the wrong tree reads exactly like an unmarked app, so
    // the marked count is only meaningful next to the visited count.
    const int views = count_views(*rig.root);
    const int marked = count_overlay_triggers(*rig.root);
    CAPTURE(views, marked);
    // Fatal: a walk that visited nothing would report "no triggers" exactly
    // like an unwired app, and that reading must never stand.
    REQUIRE(views > 100);
    // Non-fatal on purpose. These two say WHY the behavioural assertions
    // below fail; letting them abort the case first would hide the failure
    // the user actually reported behind its cause.
    CHECK(marked > 0);
    // Two-sided, and the reason this is an invariant rather than a magic
    // number: every control the document declares as opening a popover has to
    // reach the native tree as a marked trigger, and nothing else may be
    // marked. A control that quietly stops declaring `aria-haspopup`, or a
    // runtime arm that starts marking ordinary content, both fail here.
    CHECK_NOTHROW(require_runtime_contract(
        rig,
        "document.querySelectorAll('[aria-haspopup]').length === "
            + std::to_string(marked),
        "declared aria-haspopup triggers and marked native views disagree"));

    struct Menu {
        const char* root;
        const char* label;
    };
    // One top-rail trigger and three bottom-rail ones, so the pairs below
    // include a switch that crosses rails.
    const std::array<Menu, 4> menus{{
        {"bands", "32 BANDS ▾"},
        {"edit", "SCULPT ▾"},
        {"analyzer", "PEAK ▾"},
        {"overflow", "⋯"},
    }};

    for (const auto& from : menus) {
        for (const auto& to : menus) {
            if (std::string_view(from.root) == to.root) continue;
            INFO("switching from " << from.root << " to " << to.root);

            require_open_menu(rig, "");
            const auto opened = host_click(rig, trigger_centre(*rig.root, from.label));
            REQUIRE(opened.reached_tree);
            require_open_menu(rig, from.root);
            // The open menu really is the consume-everywhere kind, so the
            // one-press result below is the trigger mark doing its job and
            // not an overlay that never consumed anything.
            REQUIRE(rig.root->interaction().active_overlay != nullptr);
            REQUIRE(rig.root->interaction()
                        .active_overlay->overlay_consumes_outside_click());

            const auto switched =
                host_click(rig, trigger_centre(*rig.root, to.label));
            REQUIRE(switched.routing
                    == pulp::view::OverlayPressRouting::dismissed);
            // The whole defect in one assertion: a consumed press is a press
            // the trigger never sees, and the user pays a second one.
            REQUIRE_FALSE(switched.consumed);
            REQUIRE(switched.reached_tree);
            require_open_menu(rig, to.root);

            // Leave the tree closed for the next pair, and the pointer off
            // the trigger it pressed: a dismissal by the code is not a press,
            // and a person reaches the next trigger by moving onto it.
            pulp::view::View::dismiss_active_overlay(*rig.root);
            pulp::view::deliver_hover_move(*rig.root, {660.0f, 430.0f});
            settle(rig.clock, 30);
            require_open_menu(rig, "");
        }
    }

    // Settings is the other kind of overlay opener: a modal dialog rather than
    // a chrome menu. Its gear sits immediately beside the Help button in the
    // bottom rail, and the two behaved differently until Settings declared the
    // same `aria-haspopup` Help already did.
    const pulp::view::Point settings_gear{1247.0f, 832.5f};
    const auto* gear = rig.root->hit_test(settings_gear);
    REQUIRE(gear != nullptr);
    bool gear_is_trigger = false;
    for (const View* node = gear; node != nullptr; node = node->parent())
        gear_is_trigger = gear_is_trigger || node->overlay_trigger();
    REQUIRE(gear_is_trigger);

    require_settings_open(rig, false);
    REQUIRE(host_click(rig, trigger_centre(*rig.root, "SCULPT \u25be"))
                .reached_tree);
    require_open_menu(rig, "edit");
    const auto to_settings = host_click(rig, settings_gear);
    REQUIRE_FALSE(to_settings.consumed);
    REQUIRE(to_settings.reached_tree);
    require_open_menu(rig, "");
    require_settings_open(rig, true);

    storage.require_unchanged();
}

// A press on the trigger of the dropdown that is already open closes it, the
// same as an outside press or Escape -- it never closes and reopens it in one
// press. The other half of the trigger rule above: a press on a DIFFERENT
// trigger still switches menus in one press. Real host order (overlay slot
// first, then the tree), real pixel positions read from the document.
TEST_CASE("pressing an open dropdown's own trigger closes it",
          "[native-n1][state-parity][dropdown][overlay-trigger][trigger-toggle]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    const auto centre_of = [&](const std::string& selector) {
        const auto text = runtime_string(
            rig,
            "(() => { const n = document.querySelector(" + js_string(selector)
                + "); if (!n) throw new Error('no ' + " + js_string(selector)
                + "); const r = n.getBoundingClientRect(); "
                  "return (r.left + r.width / 2) + ',' + (r.top + r.height / 2); })()",
            "spectr-native-trigger-centre");
        const auto comma = text.find(',');
        REQUIRE(comma != std::string::npos);
        return pulp::view::Point{std::stof(text.substr(0, comma)), std::stof(text.substr(comma + 1))};
    };
    const auto trigger_of = [](std::string_view menu) {
        return "[data-spectr-menu-root=\"" + std::string(menu) + "\"] [data-spectr-menu-trigger]";
    };

    // A person's click: the pointer moves onto the control, then presses.
    const auto move_click = [&](pulp::view::Point pt) {
        pulp::view::deliver_hover_move(*rig.root, pt);
        settle(rig.clock, 2);
        return host_click(rig, pt);
    };
    const std::array<std::string_view, 7> menus{
        "bands", "edit", "analyzer", "overflow", "pattern", "length", "help"};
    for (const auto menu : menus) {
        INFO("menu=" << menu);
        require_open_menu(rig, "");
        const auto at = centre_of(trigger_of(menu));
        REQUIRE(move_click(at).reached_tree);
        const auto require_help = [&](bool open) {
            require_runtime_contract(rig,
                std::string(open ? "" : "!") + "document.querySelector('[data-spectr-help-panel]')",
                open ? "the help trigger did not open its popover"
                     : "pressing the open help trigger left its popover open");
        };
        if (menu == "help") require_help(true);
        else require_open_menu(rig, menu);
        REQUIRE(rig.root->interaction().active_overlay != nullptr);

        // The same trigger again: closed, and it stays closed.
        host_click(rig, at);
        settle(rig.clock, 30);
        require_open_menu(rig, "");
        require_help(false);
        CHECK(rig.root->interaction().active_overlay == nullptr);

        // And the next press on it opens it again: the close did not leave a
        // swallowed press behind.
        REQUIRE(host_click(rig, at).reached_tree);
        if (menu == "help") require_help(true);
        else require_open_menu(rig, menu);
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
        // An outside press or Escape followed by a press on the same trigger
        // reopens it: only the press that did the dismissing is ignored.
        const auto outside = move_click({660.0f, 430.0f});
        CHECK(outside.consumed);
        require_open_menu(rig, "");
        require_help(false);
        REQUIRE(move_click(at).reached_tree);
        if (menu == "help") require_help(true);
        else require_open_menu(rig, menu);
        REQUIRE(pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(pulp::view::KeyCode::escape),
            pulp::view::kModNone, true));
        settle(rig.clock, 30);
        require_open_menu(rig, "");
        require_help(false);
        REQUIRE(host_click(rig, at).reached_tree);
        if (menu == "help") require_help(true);
        else require_open_menu(rig, menu);
        REQUIRE(rig.root->interaction().active_overlay != nullptr);
        pulp::view::View::dismiss_active_overlay(*rig.root);
        settle(rig.clock, 30);
        require_open_menu(rig, "");
        require_help(false);
    }

    // Switching: open A, press B's trigger -- B opens in that one press.
    for (const auto from : menus) {
        for (const auto to : menus) {
            if (from == to || from == "help" || to == "help") continue;
            INFO("switching from " << from << " to " << to);
            require_open_menu(rig, "");
            REQUIRE(move_click(centre_of(trigger_of(from))).reached_tree);
            require_open_menu(rig, from);
            const auto switched = move_click(centre_of(trigger_of(to)));
            REQUIRE(switched.reached_tree);
            require_open_menu(rig, to);
            pulp::view::View::dismiss_active_overlay(*rig.root);
            pulp::view::deliver_hover_move(*rig.root, {660.0f, 430.0f});
            settle(rig.clock, 30);
        }
    }
    storage.require_unchanged();
}

// The LENGTH Custom editor closes from the LENGTH trigger (and does not open
// the menu in the same press), and its Fraction list closes from the Fraction
// trigger -- the same rule as the dropdowns above, for the two popovers that
// open from inside the LENGTH control.
TEST_CASE("pressing the LENGTH or Fraction trigger closes its open popover",
          "[native-n1][state-parity][dropdown][overlay-trigger][trigger-toggle][freeze-length]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto centre_of = [&](const std::string& selector) {
        const auto text = runtime_string(
            rig,
            "(() => { const n = document.querySelector(" + js_string(selector)
                + "); if (!n) throw new Error('no ' + " + js_string(selector)
                + "); const r = n.getBoundingClientRect(); "
                  "return (r.left + r.width / 2) + ',' + (r.top + r.height / 2); })()",
            "spectr-native-trigger-centre");
        const auto comma = text.find(',');
        REQUIRE(comma != std::string::npos);
        return pulp::view::Point{std::stof(text.substr(0, comma)), std::stof(text.substr(comma + 1))};
    };
    const auto trigger_of = [](std::string_view menu) {
        return "[data-spectr-menu-root=\"" + std::string(menu) + "\"] [data-spectr-menu-trigger]";
    };
    const auto length_trigger = centre_of(trigger_of("length"));
    const auto mounted = [&](std::string_view selector) {
        auto value = runtime_string(rig, "!!document.querySelector(" + js_string(selector) + ")",
                                    "spectr-native-toggle-mounted");
        return value.substr(0, value.find('\n')) == "true";
    };
    REQUIRE(host_click(rig, length_trigger).reached_tree);
    require_open_menu(rig, "length");
    activate(rig, "[data-spectr-length-option=\"custom-editor\"]");
    settle(rig.clock, 8);
    REQUIRE(mounted("[data-spectr-length-editor]"));
    const auto fraction_trigger = centre_of("[data-spectr-length-fraction]");
    REQUIRE(host_click(rig, fraction_trigger).reached_tree);
    REQUIRE(mounted("[data-spectr-length-fraction-options]"));
    host_click(rig, fraction_trigger);
    CHECK_FALSE(mounted("[data-spectr-length-fraction-options]"));
    CHECK(mounted("[data-spectr-length-editor]"));
    REQUIRE(host_click(rig, fraction_trigger).reached_tree);
    CHECK(mounted("[data-spectr-length-fraction-options]"));
    host_click(rig, fraction_trigger);
    CHECK_FALSE(mounted("[data-spectr-length-fraction-options]"));
    host_click(rig, length_trigger);
    CHECK_FALSE(mounted("[data-spectr-length-editor]"));
    require_open_menu(rig, "");
    REQUIRE(host_click(rig, length_trigger).reached_tree);
    require_open_menu(rig, "length");
    pulp::view::View::dismiss_active_overlay(*rig.root);
    settle(rig.clock, 30);

    storage.require_unchanged();
}

// The negative control for the rule above, in the direction that matters: the
// pass-through is scoped to TRIGGERS. If it ever widened to every dismissing
// press, closing a menu would also operate whatever sits under the click --
// a band drag, a rail button, a mode switch the user never asked for.
TEST_CASE("dismissing a native dropdown over ordinary content still consumes",
          "[native-n1][state-parity][dropdown][overlay-trigger]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    const std::array<pulp::view::Point, 3> ordinary{{
        {660.0f, 430.0f},   // the filter-bank canvas
        {1009.0f, 21.5f},   // the BOTH visualization tab, a plain button
        {48.0f, 832.5f},    // CLEAR, a rail button that is not a trigger
    }};

    for (const auto& point : ordinary) {
        INFO("ordinary press at " << point.x << ',' << point.y);
        const auto* hit = rig.root->hit_test(point);
        REQUIRE(hit != nullptr);
        for (const View* node = hit; node != nullptr; node = node->parent())
            CHECK_FALSE(node->overlay_trigger());

        require_open_menu(rig, "");
        REQUIRE(host_click(rig, trigger_centre(*rig.root, "SCULPT ▾"))
                    .reached_tree);
        require_open_menu(rig, "edit");

        // The pointer moves to where it presses, as a host delivers it.
        pulp::view::deliver_hover_move(*rig.root, point);
        const auto press =
            pulp::view::route_press_to_active_overlay(*rig.root, point);
        CHECK(press.routing == pulp::view::OverlayPressRouting::dismissed);
        CHECK(press.consume_press);
        settle(rig.clock, 30);
        require_open_menu(rig, "");
    }
    storage.require_unchanged();
}

namespace {

// Where a hover lands and which cursor it shows, sampled across an open
// popup's own box in the order a macOS host runs a buttonless move:
// `deliver_hover_move` (the scripted `pointermove`), then `hover_cursor_at`.
struct PopupHoverSweep {
    std::vector<pulp::view::Point> points;
    int crosshair = 0;       // points showing the plot's crosshair
    std::string first_bad;   // first offending point, for the failure message
};

PopupHoverSweep sweep_popup_hover(NativeEditorRig& rig, View& popup) {
    auto& root = *rig.root;
    const auto origin = pulp::view::point_to_local({0.0f, 0.0f}, &popup, &root);
    const float left = -origin.x, top = -origin.y;
    const auto box = popup.bounds();
    PopupHoverSweep sweep;
    for (float y = top + 3.0f; y < top + box.height - 2.0f; y += 11.0f) {
        for (float x = left + 3.0f; x < left + box.width - 2.0f; x += 13.0f) {
            const pulp::view::Point point{x, y};
            sweep.points.push_back(point);
            pulp::view::deliver_hover_move(root, point);
            if (pulp::view::hover_cursor_at(root, point)
                != View::CursorStyle::crosshair)
                continue;
            ++sweep.crosshair;
            if (sweep.first_bad.empty()) {
                // The tree hit names the view whose cursor leaked through.
                const auto* under = root.hit_test(point);
                std::ostringstream text;
                text << point.x << ',' << point.y << " shows the cursor of "
                     << (under ? under->id() : std::string("<nothing>"));
                sweep.first_bad = text.str();
            }
        }
    }
    return sweep;
}

// The popup must actually cover plot pixels for a zero-crosshair sweep to mean
// anything: re-read the same points with the popup closed and count the plot's
// crosshair there.
int crosshair_points_without_popup(NativeEditorRig& rig,
                                   const std::vector<pulp::view::Point>& points) {
    int crosshair = 0;
    for (const auto& point : points) {
        pulp::view::deliver_hover_move(*rig.root, point);
        if (pulp::view::hover_cursor_at(*rig.root, point)
            == View::CursorStyle::crosshair)
            ++crosshair;
    }
    return crosshair;
}

void close_every_popup(NativeEditorRig& rig) {
    while (rig.root->interaction().active_overlay != nullptr) {
        View::dismiss_active_overlay(*rig.root);
        settle(rig.clock, 16);
    }
}

}  // namespace

// Over an open popup the pointer shows the popup's cursor, never the crosshair
// of the band plot it is painted over. The band menu opens inside the plot's
// own subtree; the rail dropdowns open upward from the bottom rail, and the
// tall edit menu reaches far enough over the plot that a tree hit test stops
// reaching its upper rows. A press there was always routed into the menu
// through the overlay slot; the hover has to resolve the same way.
TEST_CASE("an open popup never shows the band plot's crosshair",
          "[native-n1][cursor][overlay][hover]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    auto& root = *rig.root;
    const pulp::view::Point plot{660.0f, 430.0f};

    const auto check_open_popup = [&](const std::string& what) {
        View* popup = root.interaction().active_overlay;
        INFO(what);
        REQUIRE(popup != nullptr);
        const auto sweep = sweep_popup_hover(rig, *popup);
        INFO("first offending point: " << sweep.first_bad);
        REQUIRE(sweep.points.size() > 40);
        CHECK(sweep.crosshair == 0);
        close_every_popup(rig);
        CHECK(crosshair_points_without_popup(rig, sweep.points) > 0);
    };

    // Band context menu, opened on the plot the way a right click opens it.
    pulp::view::deliver_hover_move(root, plot);
    REQUIRE(pulp::view::hover_cursor_at(root, plot) == View::CursorStyle::crosshair);
    REQUIRE(pulp::view::route_context_press(root, plot).handled);
    settle(rig.clock, 16);
    check_open_popup("band context menu");

    // Every chrome dropdown, each opened while the pointer last showed the
    // crosshair so a stale plot cursor has every chance to survive.
    for (const char* label : {"32 BANDS ▾", "SCULPT ▾", "PEAK ▾", "⋯"}) {
        pulp::view::deliver_hover_move(root, plot);
        REQUIRE(host_click(rig, trigger_centre(root, label)).reached_tree);
        View* popup = root.interaction().active_overlay;
        if (popup == nullptr) FAIL("dropdown did not open: " << label);
        // A dropdown mounted clear of the plot has nothing to cover; only the
        // ones painted over it can show the defect, and those must not.
        const auto sweep = sweep_popup_hover(rig, *popup);
        INFO(label << " first offending point: " << sweep.first_bad);
        CHECK(sweep.crosshair == 0);
        close_every_popup(rig);
    }
    storage.require_unchanged();
}

// THE ZOOM READOUT AND THE BANDS CAPTION SIT ON THE HEADER'S LINE.
//
// Two separate causes, one symptom: each painted a point below the segmented
// control's captions.
//
// Its layout box was always centred on the controls beside it; its glyphs were
// not. The readout is a leaf added after the capture, so it carries no text
// binding and resolved the stylesheet's monospace family by name, which reports
// a point more ascent than the bound face every captured header label uses.
// Native text centres a line as (box - ink) / 2 + ascent, so that point landed
// the baseline one point low. A box-only check cannot see this, so the case
// measures three things: the layout boxes, the face metric that caused it, and
// the painted ink itself against the segmented control's captions.
//
// The bands caption's line box is installed by the runtime's optical-centring
// pass, which centred it in the trigger's 22px border box although the box sits
// inside the 1px border; it has to centre in the 20px content box instead.
TEST_CASE("the zoom readout's text sits on the header controls' line",
          "[native-n1][state-parity][header]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Layout boxes: the readout's box is centred on the bands trigger's.
    rig.bridge().load_script(R"js((() => {
      const trigger = document.querySelector(
        '[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      const zoom = Array.from(document.querySelectorAll('span')).find(
        span => span.textContent.trim().endsWith('× ZOOM'));
      if (!trigger || !zoom) throw new Error('header subjects missing');
      const t = trigger.getBoundingClientRect();
      const z = zoom.getBoundingClientRect();
      const tc = t.top + t.height / 2, zc = z.top + z.height / 2;
      if (Math.abs(tc - zc) > 1)
        throw new Error('zoom readout box off the trigger line: trigger centre='
          + tc + ' readout centre=' + zc);
    })();)js", "spectr-native-zoom-readout-box");

    const auto* zoom = find_label(*rig.root, "1.00× ZOOM");
    // The reference caption: LENGTH. BARS / RESPONSE / BOTH, which used to be
    // the reference, now live in Settings as Display. All capitals, so its
    // ink rows are the cap height every header caption shares.
    const auto* caption = find_label(*rig.root, "LENGTH");
    const auto* bands = find_label(*rig.root, "32 BANDS ▾");
    REQUIRE(zoom != nullptr);
    REQUIRE(caption != nullptr);
    REQUIRE(bands != nullptr);

    // The face: the readout's ascent is the bound face's, the one the captured
    // labels beside it are centred with.
    CAPTURE(zoom->effective_font_family(), bands->effective_font_family());
    CHECK(zoom->baseline_y() == Catch::Approx(bands->baseline_y()).margin(0.05f));

    // The pixels: the readout's ink rows match the segmented captions'. Its
    // digits and the captions' capitals share a cap height and neither has a
    // descender, so equal top and bottom rows mean one shared line.
    REQUIRE(pulp::view::raw_rgba_render_available());
    constexpr float kScale = 2.0f;
    std::uint32_t width = 0, height = 0;
    const auto rgba = pulp::view::render_to_rgba(
        *rig.root, 1320, 860, kScale, &width, &height);
    REQUIRE(!rgba.empty());
    struct InkRows { float top = -1.0f; float bottom = -1.0f; };
    const auto ink_rows = [&](const pulp::view::Label& label,
                              const std::vector<std::uint8_t>& frame) {
        const auto origin = root_point(label, 0.0f, 0.0f);
        const auto x0 = static_cast<std::uint32_t>(origin.x * kScale);
        const auto x1 = std::min<std::uint32_t>(
            width, static_cast<std::uint32_t>(
                       (origin.x + label.bounds().width) * kScale));
        const auto y0 = static_cast<std::uint32_t>(
            std::max(0.0f, origin.y - 3.0f) * kScale);
        const auto y1 = std::min<std::uint32_t>(
            height, static_cast<std::uint32_t>(
                        (origin.y + label.bounds().height + 3.0f) * kScale));
        InkRows rows;
        for (std::uint32_t y = y0; y < y1; ++y) {
            bool inked = false;
            for (std::uint32_t x = x0; x < x1 && !inked; ++x) {
                const auto* px =
                    &frame[(static_cast<std::size_t>(y) * width + x) * 4];
                inked = px[0] + px[1] + px[2] > 250;
            }
            if (!inked) continue;
            if (rows.top < 0.0f) rows.top = static_cast<float>(y) / kScale;
            rows.bottom = static_cast<float>(y + 1) / kScale;
        }
        return rows;
    };
    // Each WORD's ink rows, split where the ink breaks for at least 4 pt --
    // a space, never the gap between two glyphs of one word. The whole
    // control's rows are not enough: a lowercase word with no ascender sits
    // on the lower part of the line, and digits beside it would still carry
    // the control's top row up to the capitals'.
    struct WordInk { float left = 0, right = 0, top = -1, bottom = -1; };
    const auto word_ink = [&](const pulp::view::Label& label,
                              const std::vector<std::uint8_t>& frame) {
        const auto origin = root_point(label, 0.0f, 0.0f);
        const auto x0 = static_cast<std::uint32_t>(origin.x * kScale);
        const auto x1 = std::min<std::uint32_t>(
            width, static_cast<std::uint32_t>(
                       (origin.x + label.bounds().width) * kScale));
        const auto y0 = static_cast<std::uint32_t>(
            std::max(0.0f, origin.y - 3.0f) * kScale);
        const auto y1 = std::min<std::uint32_t>(
            height, static_cast<std::uint32_t>(
                        (origin.y + label.bounds().height + 3.0f) * kScale));
        const auto bright = [&](std::uint32_t x, std::uint32_t y) {
            const auto* px = &frame[(static_cast<std::size_t>(y) * width + x) * 4];
            return px[0] + px[1] + px[2] > 250;
        };
        std::vector<WordInk> words;
        std::uint32_t gap = 0;
        constexpr std::uint32_t kWordGap = static_cast<std::uint32_t>(4 * 2);
        for (std::uint32_t x = x0; x < x1; ++x) {
            float top = -1.0f, bottom = -1.0f;
            for (std::uint32_t y = y0; y < y1; ++y) {
                if (!bright(x, y)) continue;
                if (top < 0.0f) top = static_cast<float>(y) / kScale;
                bottom = static_cast<float>(y + 1) / kScale;
            }
            if (top < 0.0f) { ++gap; continue; }
            if (words.empty() || gap >= kWordGap)
                words.push_back({static_cast<float>(x) / kScale, 0.0f, top, bottom});
            auto& word = words.back();
            word.right = static_cast<float>(x + 1) / kScale;
            word.top = std::min(word.top, top);
            word.bottom = std::max(word.bottom, bottom);
            gap = 0;
        }
        return words;
    };
    const auto readout_ink = ink_rows(*zoom, rgba);
    const auto caption_ink = ink_rows(*caption, rgba);
    CAPTURE(readout_ink.top, readout_ink.bottom,
            caption_ink.top, caption_ink.bottom);
    REQUIRE(readout_ink.top >= 0.0f);
    REQUIRE(caption_ink.top >= 0.0f);
    // Half a point: the defect is one point, so a one-point tolerance could
    // not see it.
    CHECK(readout_ink.top == Catch::Approx(caption_ink.top).margin(0.5f));
    CHECK(readout_ink.bottom == Catch::Approx(caption_ink.bottom).margin(0.5f));
    // Word by word: the number and ZOOM each on the captions' rows.
    const auto check_words = [&](const pulp::view::Label& label,
                                 const std::vector<std::uint8_t>& frame,
                                 std::size_t words_to_check) {
        const auto words = word_ink(label, frame);
        INFO(label.text());
        REQUIRE(words.size() >= words_to_check);
        for (std::size_t index = 0; index < words_to_check; ++index) {
            const auto& word = words[index];
            CAPTURE(index, word.left, word.right, word.top, word.bottom,
                    caption_ink.top, caption_ink.bottom);
            CHECK(word.top == Catch::Approx(caption_ink.top).margin(0.5f));
            CHECK(word.bottom == Catch::Approx(caption_ink.bottom).margin(0.5f));
        }
    };
    check_words(*zoom, rgba, 2);

    // The output cluster shares the line too: the OUTPUT caption, the peak
    // chip's PEAK label and its number, and the trim readout. Audio runs first
    // so the chip prints a number rather than its "--" placeholder, whose
    // dashes sit mid-line and would say nothing about the baseline.
    feed_audio_blocks(rig, 16);
    settle(rig.clock, 12);
    const std::function<const View*(const View&, std::string_view)> by_id =
        [&by_id](const View& view, std::string_view id) -> const View* {
            if (view.id() == id) return &view;
            for (std::size_t index = 0; index < view.child_count(); ++index)
                if (const auto* match = by_id(*view.child_at(index), id))
                    return match;
            return nullptr;
        };
    const std::function<const pulp::view::Label*(const View&)> first_label =
        [&first_label](const View& view) -> const pulp::view::Label* {
            if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view))
                return label;
            for (std::size_t index = 0; index < view.child_count(); ++index)
                if (const auto* match = first_label(*view.child_at(index)))
                    return match;
            return nullptr;
        };
    const auto label_at = [&](const char* selector) {
        auto id = runtime_string(
            rig, std::string{"String(document.querySelector('"} + selector
                     + "').__pulpId)",
            "spectr-header-output-id");
        id.erase(std::min(id.find('\n'), id.size()));
        const auto* view = by_id(*rig.root, id);
        INFO(selector << " id=" << id);
        REQUIRE(view != nullptr);
        const auto* label = first_label(*view);
        REQUIRE(label != nullptr);
        return label;
    };
    const auto* output_caption = label_at("[data-spectr-output-trim-label]");
    const auto* peak_label = label_at("[data-spectr-output-peak-label]");
    const auto* trim_readout = label_at("[data-spectr-output-trim-readout]");
    CHECK(output_caption->text() == "OUTPUT");
    CHECK(trim_readout->text() == "0.0");
    INFO("peak chip reads " << peak_label->text());
    REQUIRE(peak_label->text().rfind("PEAK -", 0) == 0);
    REQUIRE(peak_label->text().find_first_of("0123456789") != std::string::npos);
    const auto output_frame = pulp::view::render_to_rgba(
        *rig.root, 1320, 860, kScale, &width, &height);
    REQUIRE(!output_frame.empty());
    check_words(*output_caption, output_frame, 1);
    // "PEAK" and its number, which begins with a minus sign attached to the
    // digits; the digits carry the word's top and bottom rows.
    check_words(*peak_label, output_frame, 2);
    check_words(*trim_readout, output_frame, 1);

    // THE LEVEL KNOBS SHARE THE LINE. MIX and INTENSITY captions and their
    // readouts sit on the captions' rows like OUTPUT's; the AUTO pill's
    // smaller capitals are centred on that line; and every knob's centre is
    // on the line the PEAK chip is centred on.
    const auto* mix_caption = label_at("[data-spectr-mix-label]");
    const auto* mix_readout = label_at("[data-spectr-mix-readout]");
    const auto* intensity_caption = label_at("[data-spectr-intensity-label]");
    const auto* intensity_readout = label_at("[data-spectr-intensity-readout]");
    const auto* auto_label = label_at("[data-spectr-auto-gain] span");
    CHECK(mix_caption->text() == "MIX");
    CHECK(mix_readout->text() == "100%");
    CHECK(intensity_caption->text() == "INTENSITY");
    CHECK(intensity_readout->text() == "100%");
    CHECK(auto_label->text() == "AUTO");
    check_words(*mix_caption, output_frame, 1);
    check_words(*mix_readout, output_frame, 1);
    check_words(*intensity_caption, output_frame, 1);
    check_words(*intensity_readout, output_frame, 1);
    {
        const auto words = word_ink(*auto_label, output_frame);
        REQUIRE_FALSE(words.empty());
        const float auto_centre = 0.5f * (words[0].top + words[0].bottom);
        const float caption_centre = 0.5f * (caption_ink.top + caption_ink.bottom);
        CAPTURE(auto_centre, caption_centre);
        CHECK(auto_centre == Catch::Approx(caption_centre).margin(0.5f));
    }
    const auto box_centre_y = [&](const char* selector) {
        auto id = runtime_string(
            rig, std::string{"String(document.querySelector('"} + selector
                     + "').__pulpId)",
            "spectr-header-box-id");
        id.erase(std::min(id.find('\n'), id.size()));
        const auto* view = by_id(*rig.root, id);
        INFO(selector << " id=" << id);
        REQUIRE(view != nullptr);
        const auto box = pulp::view::ViewInspector::absolute_bounds(*view);
        return box.y + box.height * 0.5f;
    };
    const float line = box_centre_y("[data-spectr-output-peak]");
    for (const char* knob : {"[data-spectr-mix]", "[data-spectr-intensity]",
                             "[data-spectr-output-trim]",
                             "[data-spectr-auto-gain]"}) {
        INFO(knob);
        CHECK(box_centre_y(knob) == Catch::Approx(line).margin(0.5f));
    }

    // The bands trigger's caption shares the same line, both as captured and
    // after its text changes -- a changed caption no longer matches its capture
    // and is laid out natively, which is the state a user who picked another
    // band count sees.
    const auto check_bands_caption = [&](const char* text) {
        const auto* label = find_label(*rig.root, text);
        REQUIRE(label != nullptr);
        const auto frame = pulp::view::render_to_rgba(
            *rig.root, 1320, 860, kScale, &width, &height);
        REQUIRE(!frame.empty());
        const auto rows = ink_rows(*label, frame);
        INFO(text);
        CAPTURE(rows.top, rows.bottom, caption_ink.top, caption_ink.bottom);
        REQUIRE(rows.top >= 0.0f);
        CHECK(rows.top == Catch::Approx(caption_ink.top).margin(0.5f));
        CHECK(rows.bottom == Catch::Approx(caption_ink.bottom).margin(0.5f));
        // The count and the word; the ▾ glyph is not a caption word.
        check_words(*label, frame, 2);
    };
    check_bands_caption("32 BANDS ▾");
    activate(rig, "[data-spectr-menu-root=\"bands\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-band-count=\"64\"]");
    settle(rig.clock, 12);
    require_app_state(rig, "s.settings.bandCount === 64",
                      "the 64-band option was not selected");
    check_bands_caption("64 BANDS ▾");
    storage.require_unchanged();
}


// THE TRACING REMINDER SITS ON THE HEADER'S LINE.
//
// A tracing build shows exactly one "◉ TRACING" reminder, and it is the
// header's: Pulp's root-painted corner pill is hidden while the editor is open,
// and the header's own sits on the line every header control shares. Measured
// on the painted ink -- the TRACING letters against the BOTH caption, whose
// capitals share their cap height -- because that line is what a reader sees.
// A non-tracing build has no reminder to measure; it says so and skips.
TEST_CASE("the tracing reminder sits on the header controls' line",
          "[native-n1][state-parity][header][tracing]") {
    if constexpr (!pulp::runtime::kTracingEnabled) {
        SKIP("not a PULP_TRACING build: there is no tracing reminder to measure");
    } else {
        PatternStoragePoison storage;
        NativeEditorRig rig;
        require_home(rig);
        CHECK_FALSE(pulp::view::tracing_badge_should_paint());
        const auto* badge = find_label(*rig.root, "◉ TRACING");
        const auto* caption = find_label(*rig.root, "BOTH");
        REQUIRE(badge != nullptr);
        REQUIRE(caption != nullptr);

        constexpr float kScale = 2.0f;
        std::uint32_t width = 0, height = 0;
        REQUIRE(pulp::view::raw_rgba_render_available());
        const auto frame = pulp::view::render_to_rgba(
            *rig.root, 1320, 860, kScale, &width, &height);
        REQUIRE(!frame.empty());
        // Ink rows of the bright glyphs inside a label's box, from `from` of
        // its width to its right edge.
        const auto ink_rows = [&](const pulp::view::Label& label, float from) {
            const auto origin = root_point(label, 0.0f, 0.0f);
            const auto x0 = static_cast<std::uint32_t>(
                (origin.x + label.bounds().width * from) * kScale);
            const auto x1 = std::min<std::uint32_t>(
                width, static_cast<std::uint32_t>(
                           (origin.x + label.bounds().width) * kScale));
            const auto y0 = static_cast<std::uint32_t>(
                std::max(0.0f, origin.y - 3.0f) * kScale);
            const auto y1 = std::min<std::uint32_t>(
                height, static_cast<std::uint32_t>(
                            (origin.y + label.bounds().height + 3.0f) * kScale));
            float top = -1.0f, bottom = -1.0f;
            for (std::uint32_t y = y0; y < y1; ++y) {
                bool inked = false;
                for (std::uint32_t x = x0; x < x1 && !inked; ++x) {
                    const auto* px = &frame[(static_cast<std::size_t>(y) * width + x) * 4];
                    inked = px[0] + px[1] + px[2] > 250;
                }
                if (!inked) continue;
                if (top < 0.0f) top = static_cast<float>(y) / kScale;
                bottom = static_cast<float>(y + 1) / kScale;
            }
            return std::pair{top, bottom};
        };
        // Skip the ◉ glyph, which is taller than the capitals.
        const auto [badge_top, badge_bottom] = ink_rows(*badge, 0.3f);
        const auto [caption_top, caption_bottom] = ink_rows(*caption, 0.0f);
        CAPTURE(badge_top, badge_bottom, caption_top, caption_bottom);
        REQUIRE(badge_top >= 0.0f);
        REQUIRE(caption_top >= 0.0f);
        CHECK(badge_top == Catch::Approx(caption_top).margin(0.5f));
        CHECK(badge_bottom == Catch::Approx(caption_bottom).margin(0.5f));
        storage.require_unchanged();
    }
}

// THE LIVE / PRECISION CONTROL IS HIDDEN EVERYWHERE A USER COULD MEET IT.
//
// Motion Mode only ever set how fast the display eased; it never reached the
// audio. The header's segmented control, the Settings MOTION row and the help
// section that explained it are all withheld, so no surface offers a choice
// the editor no longer makes. Each absence is paired with a word that MUST be
// found by the same lookup on the same surface, so "not found" cannot mean
// "looked in the wrong place".
TEST_CASE("the LIVE / PRECISION control is hidden from the header, Settings and help",
          "[native-n1][state-parity][header][motion-mode]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Header. Every "LIVE" label must belong to the freeze toggle, which is
    // the only other control in the header that can print the word.
    REQUIRE(find_label(*rig.root, "BARS") != nullptr);
    CHECK(find_label(*rig.root, "PRECISION") == nullptr);
    std::vector<const pulp::view::Label*> live_labels;
    const std::function<void(const View&)> collect = [&](const View& view) {
        if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
            label != nullptr && label->text() == "LIVE")
            live_labels.push_back(label);
        for (std::size_t index = 0; index < view.child_count(); ++index)
            collect(*view.child_at(index));
    };
    collect(*rig.root);
    const auto freeze_toggles = [&] {
        std::size_t inside = 0;
        for (const auto* label : live_labels) {
            // The freeze toggle sits at the left of the output cluster, well
            // clear of where the segmented control painted (x=687.5).
            if (root_point(*label, 0.0f, 0.0f).x < 400.0f) ++inside;
        }
        return inside;
    }();
    CHECK(freeze_toggles == live_labels.size());

    // Help. The copy the overlay reads is the asset this global holds.
    rig.bridge().load_script(R"js((() => {
      const text = globalThis.SPECTR_HELP_TEXT;
      if (typeof text !== 'string' || !text.includes('## Latency'))
        throw new Error('the help copy is not loaded, so its absences prove nothing');
      if (/Precision/.test(text))
        throw new Error('the help copy still explains LIVE / PRECISION');
    })();)js", "spectr-native-motion-mode-help-absent");

    // Settings.
    rig.root->layout_children();
    settle(rig.clock, 4);
#if defined(__APPLE__)
    constexpr auto primary_modifier = pulp::view::kModCmd;
#else
    constexpr auto primary_modifier = pulp::view::kModCtrl;
#endif
    REQUIRE(rig.root->on_global_key({
        .key = static_cast<pulp::view::KeyCode>(','),
        .modifiers = primary_modifier,
        .is_down = true}));
    settle(rig.clock, 16);
    require_state(rig, "settings");
    REQUIRE(find_label(*rig.root, "Mute style") != nullptr);
    // "Response" is legitimately in Settings now, as a Display choice
    // (tools/patch_materialized_display_setting.py); the MOTION group's own
    // words are what must stay gone.
    CHECK(find_label(*rig.root, "MOTION") == nullptr);
    CHECK(find_label(*rig.root, "Precision") == nullptr);
    storage.require_unchanged();
}

// THE HEADER READS [freeze] OUTPUT --o-- value [PEAK], ALL ON ONE LINE.
//
// The freeze toggle takes the place PEAK held, and PEAK sits right of the
// trim's value at the same gap it used to keep to OUTPUT. Each word is checked
// by its painted ink against the BOTH caption, word by word (a space-sized
// break in the ink starts a new word), because a whole-label reading lets a
// lower word hide behind a taller one. The toggle is then pressed and its
// FROZEN face checked the same way, with its tokens read off the pixels: the
// green dot and neutral box of LIVE, the amber square, amber text and warm box
// of FROZEN, at one fixed width so nothing beside it moves.
TEST_CASE("the header reads freeze, OUTPUT, trim, value, PEAK on the controls' line",
          "[native-n1][state-parity][header][freeze-toggle]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Labels in the top bar only: the plot's rulers print numbers too.
    const auto header_label = [&](std::string_view text) {
        const pulp::view::Label* found = nullptr;
        const std::function<void(const View&)> walk = [&](const View& view) {
            if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
                label != nullptr && label->text() == text
                && root_point(*label, 0.0f, 0.0f).y < 44.0f) {
                REQUIRE(found == nullptr);
                found = label;
            }
            for (std::size_t index = 0; index < view.child_count(); ++index)
                walk(*view.child_at(index));
        };
        walk(*rig.root);
        return found;
    };
    struct Box { float left, top, right, bottom; };
    const auto box_of = [&](const View& view) {
        const auto origin = root_point(view, 0.0f, 0.0f);
        return Box{origin.x, origin.y, origin.x + view.bounds().width,
                   origin.y + view.bounds().height};
    };

    const auto* live = header_label("LIVE");
    const auto* output = header_label("OUTPUT");
    const auto* value = header_label("0.0");
    // The line reference is the LENGTH caption (BARS / RESPONSE / BOTH moved
    // to Settings as Display), and the right-hand bound is the band-count
    // menu the header now runs up to.
    const auto* caption = header_label("LENGTH");
    const auto* bands_label = header_label("32 BANDS ▾");
    const auto* auto_label = header_label("AUTO");
    REQUIRE(bands_label != nullptr);
    REQUIRE(auto_label != nullptr);
    REQUIRE(live != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(value != nullptr);
    REQUIRE(caption != nullptr);
    const pulp::view::Label* peak = nullptr;
    {
        const std::function<void(const View&)> walk = [&](const View& view) {
            if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
                label != nullptr && label->text().rfind("PEAK ", 0) == 0
                && root_point(*label, 0.0f, 0.0f).y < 44.0f)
                peak = label;
            for (std::size_t index = 0; index < view.child_count(); ++index)
                walk(*view.child_at(index));
        };
        walk(*rig.root);
    }
    REQUIRE(peak != nullptr);
    const View* toggle = live->parent();
    const View* peak_button = peak->parent();
    REQUIRE(toggle != nullptr);
    REQUIRE(peak_button != nullptr);

    // ORDER AND SPACING. [toggle] | LENGTH [length] | OUTPUT --o-- value
    // [PEAK]: a divider 5pt either side of it, the caption 6pt from its
    // dropdown, and the cluster's 14pt between OUTPUT, the track, the value
    // and PEAK. All of it clear of the divider before BARS (x=839.5).
    const auto* length_caption = header_label("LENGTH");
    REQUIRE(length_caption != nullptr);
    const auto* length_value = header_label("1 bar");
    REQUIRE(length_value != nullptr);
    const View* length_trigger = length_value->parent();
    REQUIRE(length_trigger != nullptr);
    const auto toggle_box = box_of(*toggle);
    const auto length_caption_box = box_of(*length_caption);
    const auto length_box = box_of(*length_trigger);
    const auto output_box = box_of(*output);
    const auto value_box = box_of(*value);
    const auto peak_box = box_of(*peak_button);
    const auto caption_box = box_of(*caption);
    CAPTURE(toggle_box.left, toggle_box.right, length_caption_box.left,
            length_caption_box.right, length_box.left, length_box.right,
            output_box.left, output_box.right,
            value_box.left, value_box.right, peak_box.left, peak_box.right);
    CHECK(toggle_box.right < length_caption_box.left);
    CHECK(length_caption_box.right < length_box.left);
    CHECK(length_box.right < output_box.left);
    CHECK(output_box.right < value_box.left);
    const auto auto_box = box_of(*auto_label->parent());
    const auto bands_box = box_of(*bands_label);
    CAPTURE(auto_box.left, auto_box.right, bands_box.left);
    CHECK(value_box.right < auto_box.left);
    CHECK(auto_box.right < peak_box.left);
    CHECK(peak_box.right <= bands_box.left - 14.0f);
    // toggle, 5, divider, 5, LENGTH: 11; LENGTH, 6, [length]; [length], 5,
    // divider, 5, MIX: 11 -- the first knob takes LENGTH's spacing.
    const auto* mix_caption = header_label("MIX");
    REQUIRE(mix_caption != nullptr);
    const auto mix_box = box_of(*mix_caption);
    CHECK(length_caption_box.left - toggle_box.right == Catch::Approx(11.0f).margin(1.0f));
    CHECK(length_box.left - length_caption_box.right == Catch::Approx(6.0f).margin(1.0f));
    CHECK(mix_box.left - length_box.right == Catch::Approx(11.0f).margin(1.0f));
    // Inside a knob group: caption, 6, knob, 6, readout (6, AUTO); between
    // groups and before PEAK, the cluster's 14. Read off the laid-out boxes.
    rig.bridge().load_script(R"js((() => {
      const r = (q) => document.querySelector(q)?.getBoundingClientRect();
      const near = (a, b, what) => { if (!(Math.abs(a - b) <= 0.5))
        throw new Error(what + ' gap ' + a + ' expected ' + b); };
      for (const name of ['mix', 'intensity', 'output-trim']) {
        const label = r('[data-spectr-' + name + '-label]');
        const knob = r('[data-spectr-' + name + ']');
        const readout = r('[data-spectr-' + name + '-readout]');
        if (!label || !knob || !readout) throw new Error(name + ' parts missing');
        near(knob.left - label.right, 6, name + ' caption->knob');
        near(readout.left - knob.right, 6, name + ' knob->readout');
      }
      near(r('[data-spectr-intensity-label]').left - r('[data-spectr-mix-readout]').right,
           14, 'MIX->INTENSITY');
      near(r('[data-spectr-output-trim-label]').left - r('[data-spectr-intensity-readout]').right,
           14, 'INTENSITY->OUTPUT');
      near(r('[data-spectr-auto-gain]').left - r('[data-spectr-output-trim-readout]').right,
           6, 'readout->AUTO');
    })();)js", "spectr-native-level-knob-spacing");
    CHECK(length_box.right - length_box.left == Catch::Approx(88.0f).margin(0.5f));
    const float peak_gap = peak_box.left - auto_box.right;
    CAPTURE(peak_gap);
    CHECK(peak_gap == Catch::Approx(14.0f).margin(1.0f));
    CHECK(toggle_box.right - toggle_box.left == Catch::Approx(76.0f).margin(0.5f));

    // THE PIXELS.
    REQUIRE(pulp::view::raw_rgba_render_available());
    constexpr float kScale = 2.0f;
    std::uint32_t width = 0, height = 0;
    const auto render = [&] {
        auto frame = pulp::view::render_to_rgba(
            *rig.root, 1320, 860, kScale, &width, &height);
        REQUIRE(!frame.empty());
        return frame;
    };
    const auto pixel = [&](const std::vector<std::uint8_t>& frame, float x, float y) {
        const auto px = static_cast<std::size_t>(x * kScale);
        const auto py = static_cast<std::size_t>(y * kScale);
        const auto* p = &frame[(py * width + px) * 4];
        return std::array<int, 3>{p[0], p[1], p[2]};
    };
    // Ink of each word inside a label's box, where a word is a run of inked
    // columns broken by less than 4pt of blank. `bright` picks what counts as
    // ink, so dim captions and coloured text are both measurable.
    struct WordInk { float left = 0, right = 0, top = -1, bottom = -1; };
    const auto word_ink = [&](const pulp::view::Label& label,
                              const std::vector<std::uint8_t>& frame) {
        const auto origin = root_point(label, 0.0f, 0.0f);
        const auto x0 = static_cast<std::uint32_t>(origin.x * kScale);
        const auto x1 = std::min<std::uint32_t>(
            width, static_cast<std::uint32_t>(
                       (origin.x + label.bounds().width) * kScale));
        const auto y0 = static_cast<std::uint32_t>(
            std::max(0.0f, origin.y - 3.0f) * kScale);
        const auto y1 = std::min<std::uint32_t>(
            height, static_cast<std::uint32_t>(
                        (origin.y + label.bounds().height + 3.0f) * kScale));
        const auto inked = [&](std::uint32_t x, std::uint32_t y) {
            const auto* px = &frame[(static_cast<std::size_t>(y) * width + x) * 4];
            return px[0] + px[1] + px[2] > 250;
        };
        std::vector<WordInk> words;
        std::uint32_t gap = 0;
        constexpr std::uint32_t kWordGap = static_cast<std::uint32_t>(4 * kScale);
        for (std::uint32_t x = x0; x < x1; ++x) {
            float top = -1.0f, bottom = -1.0f;
            for (std::uint32_t y = y0; y < y1; ++y) {
                if (!inked(x, y)) continue;
                if (top < 0.0f) top = static_cast<float>(y) / kScale;
                bottom = static_cast<float>(y + 1) / kScale;
            }
            if (top < 0.0f) { ++gap; continue; }
            if (words.empty() || gap >= kWordGap)
                words.push_back({static_cast<float>(x) / kScale, 0.0f, top, bottom});
            auto& word = words.back();
            word.right = static_cast<float>(x + 1) / kScale;
            word.top = std::min(word.top, top);
            word.bottom = std::max(word.bottom, bottom);
            gap = 0;
        }
        return words;
    };
    auto frame = render();
    const auto caption_words = word_ink(*caption, frame);
    REQUIRE(caption_words.size() == 1);
    const auto line = caption_words.front();
    CAPTURE(line.top, line.bottom);
    const auto on_line = [&](const pulp::view::Label& label,
                             const std::vector<std::uint8_t>& image,
                             std::size_t words_to_check) {
        const auto words = word_ink(label, image);
        INFO(label.text());
        REQUIRE(words.size() >= words_to_check);
        for (std::size_t index = 0; index < words_to_check; ++index) {
            const auto& word = words[index];
            CAPTURE(index, word.left, word.right, word.top, word.bottom);
            // Half a point: a one-point defect is the size these drift by.
            CHECK(word.top == Catch::Approx(line.top).margin(0.5f));
            CHECK(word.bottom == Catch::Approx(line.bottom).margin(0.5f));
        }
    };
    on_line(*live, frame, 1);
    on_line(*output, frame, 1);
    on_line(*length_caption, frame, 1);
    {
        // The length's value is lower case at its own size, so it is centred
        // on the line rather than sharing its cap top.
        const auto words = word_ink(*length_value, frame);
        REQUIRE(!words.empty());
        const float centre = (words.front().top + words.front().bottom) * 0.5f;
        CAPTURE(centre, line.top, line.bottom);
        CHECK(centre == Catch::Approx((line.top + line.bottom) * 0.5f).margin(1.25f));
    }
    on_line(*value, frame, 1);
    // "PEAK" only: the level beside it is "--" in a silent rig, which has no
    // cap height to compare.
    on_line(*peak, frame, 1);

    // The glyph is centred on the same line as the words.
    const float line_centre = (line.top + line.bottom) * 0.5f;

    // THE NUMBER BELONGS TO THE SLIDER. Measured on the paint: from the
    // track's right end (the last inked column of the track on the line's
    // centre row) to the value's first glyph is the cluster's 14pt gap, the
    // same gap the value's box keeps to PEAK. A right-aligned readout left
    // ~30pt here for "0.0".
    const auto track_end = [&](const std::vector<std::uint8_t>& image,
                               float before) {
        float last = -1.0f;
        for (float x = output_box.right + 2.0f; x < before; x += 0.5f) {
            for (float dy = -1.5f; dy <= 1.5f; dy += 0.5f) {
                const auto c = pixel(image, x, line_centre + dy);
                if (c[0] + c[1] + c[2] > 250) { last = x + 0.5f; break; }
            }
        }
        return last;
    };
    const auto value_ink = [&](const pulp::view::Label& label,
                               const std::vector<std::uint8_t>& image) {
        const auto words = word_ink(label, image);
        REQUIRE(!words.empty());
        return std::pair{words.front().left, words.back().right};
    };
    // ...AND PEAK HOLDS STILL as the number runs its whole range. The host
    // writes the trim, the meter publication carries it to the readout, and
    // PEAK's box and the number's first glyph must not move.
    const auto readout_at = [&]() -> const pulp::view::Label* {
        const pulp::view::Label* found = nullptr;
        const std::function<void(const View&)> walk = [&](const View& view) {
            if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
                label != nullptr && std::abs(box_of(*label).left - value_box.left) < 0.5f
                && box_of(*label).top < 44.0f)
                found = label;
            for (std::size_t index = 0; index < view.child_count(); ++index)
                walk(*view.child_at(index));
        };
        walk(*rig.root);
        return found;
    };
    {
        const auto first_glyph = value_ink(*value, frame).first;
        for (const float trim : {-24.0f, 24.0f, -0.5f, 12.5f, 0.0f}) {
            rig.store.set_value(spectr::kOutputTrim, trim);
            rig.processor.apply_surface_params(false);
            feed_audio_blocks(rig, 8);
            settle(rig.clock, 8);
            char expected[16];
            std::snprintf(expected, sizeof expected, "%s%.1f",
                          trim > 0.0f ? "+" : "", trim);
            const auto* readout = readout_at();
            INFO("trim " << expected);
            REQUIRE(readout != nullptr);
            CHECK(readout->text() == expected);
            const auto image = render();
            const auto [ink_left, ink_right] = value_ink(*readout, image);
            const auto peak_now = box_of(*peak_button);
            CAPTURE(ink_left, ink_right, peak_now.left, first_glyph);
            CHECK(peak_now.left == Catch::Approx(peak_box.left).margin(0.01f));
            CHECK(box_of(*readout).right
                  == Catch::Approx(value_box.right).margin(0.01f));
            // The first glyph stays put (a sign is a glyph too, so it may
            // start a hair left of a digit), and every glyph stays inside the
            // fixed box, clear of PEAK.
            CHECK(ink_left == Catch::Approx(first_glyph).margin(1.0f));
            CHECK(ink_right <= value_box.right + 0.5f);
            if (std::abs(trim) >= 10.0f)
                CHECK(peak_now.left - ink_right >= 13.0f);
        }
    }
    frame = render();
    const auto live_box = box_of(*live);
    const float glyph_x = live_box.left - 6.0f - 3.0f;
    const auto glyph_rows = [&](const std::vector<std::uint8_t>& image, float x) {
        float top = -1.0f, bottom = -1.0f;
        for (float y = toggle_box.top + 2.0f; y < toggle_box.bottom - 2.0f; y += 0.5f) {
            const auto c = pixel(image, x, y);
            if (std::max({c[0], c[1], c[2]}) < 120) continue;
            if (top < 0.0f) top = y;
            bottom = y + 0.5f;
        }
        return std::pair{top, bottom};
    };
    {
        const auto [top, bottom] = glyph_rows(frame, glyph_x);
        CAPTURE(top, bottom, line_centre);
        REQUIRE(top >= 0.0f);
        CHECK((top + bottom) * 0.5f == Catch::Approx(line_centre).margin(1.0f));
    }

    // LIVE TOKENS: a green dot (hsl(150,75%,60%) = rgb(77,230,153)) and a
    // box that adds no hue to the header behind it.
    const float box_probe_x = toggle_box.right - 3.0f;
    const float box_probe_y = (toggle_box.top + toggle_box.bottom) * 0.5f;
    const auto behind = pixel(frame, toggle_box.left - 4.0f, box_probe_y);
    const int behind_warmth = behind[0] - behind[2];
    {
        const auto dot = pixel(frame, glyph_x, line_centre);
        CAPTURE(dot[0], dot[1], dot[2]);
        CHECK(std::abs(dot[0] - 77) <= 20);
        CHECK(std::abs(dot[1] - 230) <= 20);
        CHECK(std::abs(dot[2] - 153) <= 20);
        const auto inside = pixel(frame, box_probe_x, box_probe_y);
        CAPTURE(inside[0], inside[1], inside[2], behind_warmth);
        CHECK(std::abs((inside[0] - inside[2]) - behind_warmth) <= 3);
    }

    // FROZEN. The press flips only this control.
    activate(rig, "[data-spectr-freeze-toggle]");
    require_runtime_contract(
        rig,
        "document.querySelector('[data-spectr-freeze-toggle]')"
        "?.getAttribute('data-spectr-freeze-state') === 'frozen'",
        "the freeze toggle did not take its FROZEN face");
    CHECK(header_label("LIVE") == nullptr);
    const auto* frozen = header_label("FROZEN");
    REQUIRE(frozen != nullptr);
    const auto frozen_toggle_box = box_of(*frozen->parent());
    CHECK(frozen_toggle_box.left == Catch::Approx(toggle_box.left).margin(0.01f));
    CHECK(frozen_toggle_box.right == Catch::Approx(toggle_box.right).margin(0.01f));
    CHECK(box_of(*output).left == Catch::Approx(output_box.left).margin(0.01f));
    // The word fits the fixed box it was sized for.
    CHECK(box_of(*frozen).right <= frozen_toggle_box.right - 10.0f + 0.5f);
    frame = render();
    {
        // The FROZEN text is amber, hsl(35,90%,75%) = rgb(249,201,134), so the
        // ink threshold above still sees it.
        on_line(*frozen, frame, 1);
        const auto words = word_ink(*frozen, frame);
        REQUIRE(!words.empty());
        std::array<int, 3> brightest{0, 0, 0};
        const auto fbox = box_of(*frozen);
        for (float y = words[0].top; y < words[0].bottom; y += 0.5f)
            for (float x = words[0].left; x < words[0].right; x += 0.5f) {
                const auto c = pixel(frame, x, y);
                if (c[0] + c[1] + c[2] > brightest[0] + brightest[1] + brightest[2])
                    brightest = c;
            }
        CAPTURE(brightest[0], brightest[1], brightest[2], fbox.left);
        CHECK(brightest[0] - brightest[2] >= 80);
        CHECK(brightest[0] >= brightest[1]);
        const float frozen_glyph_x = fbox.left - 6.0f - 3.0f;
        const auto [top, bottom] = glyph_rows(frame, frozen_glyph_x);
        CAPTURE(top, bottom);
        REQUIRE(top >= 0.0f);
        CHECK((top + bottom) * 0.5f == Catch::Approx(line_centre).margin(1.0f));
        // The amber square, hsl(35,90%,65%) = rgb(246,179,85). Its corner
        // radius is 1, so its corner is inked where the dot's is not.
        const auto square = pixel(frame, frozen_glyph_x, line_centre);
        CAPTURE(square[0], square[1], square[2]);
        CHECK(std::abs(square[0] - 246) <= 20);
        CHECK(std::abs(square[1] - 179) <= 20);
        CHECK(std::abs(square[2] - 85) <= 20);
        const auto corner = pixel(frame, frozen_glyph_x - 2.5f,
                                  (top + bottom) * 0.5f - 2.5f);
        CAPTURE(corner[0], corner[1], corner[2]);
        CHECK(corner[0] - corner[2] >= 60);
        // The box takes the warm tint rgba(200,140,60,0.18).
        const auto inside = pixel(frame, box_probe_x, box_probe_y);
        CAPTURE(inside[0], inside[1], inside[2], behind_warmth);
        CHECK((inside[0] - inside[2]) - behind_warmth >= 12);
    }
    if (const char* shot = std::getenv("SPECTR_FREEZE_SHOT")) {
        const auto png = pulp::view::render_to_png(
            *rig.root, 1320, 860, kScale, pulp::view::ScreenshotBackend::skia);
        std::ofstream(shot, std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()),
                   static_cast<std::streamsize>(png.size()));
    }

    // And back.
    activate(rig, "[data-spectr-freeze-toggle]");
    CHECK(header_label("LIVE") != nullptr);
    CHECK(header_label("FROZEN") == nullptr);
    storage.require_unchanged();
}

// A SESSION THAT STORED PRECISION STILL LOADS, AND EASES AT THE LIVE RATE.
//
// Param 3100 stays registered and nothing rewrites it: a value the host wrote
// is the host's, and coercing it on load would write to an automation lane.
// The editor keeps the stored value and ignores it. Measured, not inferred:
// the draw loop eases each painted column toward its target by a fixed ratio
// of the remaining distance per frame, exp(-dt * k), so the ratio read on the
// PRECISION session must equal the ratio read on the same editor once the host
// switches it to LIVE. PRECISION was k = 6 against LIVE's 22, which at this
// runtime's 50 ms replay step is a ratio of exp(-0.3) = 0.74 against
// exp(-1.1) = 0.33, so the two are far apart
// whenever the editor honours the stored value. The LIVE reading is also the
// control that shows the instrument reads a rate at all.
TEST_CASE("a session that stored PRECISION loads and eases at the LIVE rate",
          "[native-n1][state-parity][motion-mode]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    rig.close();
    rig.store.set_value(spectr::kParamMotionMode, 1.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    rig.open();
    require_home(rig);

    const auto host_bank = [&](float gain_db) {
        for (std::size_t index = 0; index < spectr::kMaxBands; ++index)
            rig.store.set_value(spectr::band_gain_param_id(index), gain_db);
        REQUIRE(rig.processor.apply_surface_params(false));
        settle(rig.clock, 30);
    };
    // Invert the bank and read the per-frame ratio of the remaining distance,
    // frame by frame, into `globalThis.__spectrEaseRatio[key]`.
    const auto measure = [&](std::string_view key) {
        activate(rig, "[data-spectr-menu-root=\"overflow\"] [data-spectr-menu-trigger]");
        // Pressed WITHOUT a settle, so each frame below is one step of the ease.
        // The runtime's replay clock is re-armed ahead of now, so every frame
        // advances exactly 50 ms and the ratio is a function of k alone, not of
        // how long the host took between ticks.
        rig.bridge().load_script(R"js((() => {
          const clock = globalThis.__pulpCapturedReplayClock__;
          if (!clock) throw new Error('the runtime replay clock is missing');
          clock.target = clock.current + 5000;
          globalThis.__spectrEaseSamples = [];
          if (!globalThis.__pulpActivateMaterializedElement__(
                '[data-spectr-overflow-action="invert"]', 'click', null))
            throw new Error('INVERT could not be pressed');
        })();)js", "spectr-native-motion-ease-press");
        for (int frame = 0; frame < 6; ++frame) {
            settle(rig.clock, 1);
            rig.bridge().load_script(R"js((() => {
              const state = globalThis.__spectrTestHooks?.renderState?.();
              if (!state) throw new Error('native render-state hook missing');
              globalThis.__spectrEaseSamples.push(
                { drawn: state.gains[0], target: state.targetGains[0] });
            })();)js", "spectr-native-motion-ease-sample");
        }
        rig.bridge().load_script(std::string{R"js((() => {
          const samples = globalThis.__spectrEaseSamples;
          const ratios = [];
          for (let i = 1; i < samples.length; ++i) {
            const before = samples[i - 1].target - samples[i - 1].drawn;
            const after = samples[i].target - samples[i].drawn;
            if (Math.abs(before) < 1e-6) continue;
            ratios.push(after / before);
          }
          if (ratios.length < 3 || !ratios.every(r => r > 0 && r < 1))
            throw new Error('the invert produced no measurable ease: '
              + JSON.stringify(samples));
          ratios.sort((a, b) => a - b);
          globalThis.__spectrEaseRatio = globalThis.__spectrEaseRatio || {};
          globalThis.__spectrEaseRatio[)js"} + js_string(key) + R"js(] =
            ratios[Math.floor(ratios.length / 2)];
        })();)js", "spectr-native-motion-ease-ratio");
    };

    // The session: the host projects its stored PRECISION into the editor.
    host_bank(-12.0f);
    require_app_state(rig, "s.settings && s.settings.motionMode === 'precision'",
                      "the editor did not receive the stored PRECISION value");
    measure("precision");
    // Opening the editor and editing never rewrote the stored value.
    CHECK(rig.store.get_value(spectr::kParamMotionMode) == Catch::Approx(1.0f));

    // The control: the same editor, switched to LIVE by the host.
    rig.store.set_value(spectr::kParamMotionMode, 0.0f);
    host_bank(-12.0f);
    require_app_state(rig, "s.settings && s.settings.motionMode === 'live'",
                      "the host could not switch the editor to LIVE");
    measure("live");

    rig.bridge().load_script(R"js((() => {
      const r = globalThis.__spectrEaseRatio;
      if (!r || !(Math.abs(r.live - Math.exp(-22 * 0.05)) < 0.02))
        throw new Error('the LIVE control read no LIVE-rate ease: '
          + JSON.stringify(r));
      if (!(Math.abs(r.precision - r.live) < 0.02))
        throw new Error('a PRECISION session does not ease at the LIVE rate: '
          + JSON.stringify(r));
    })();)js", "spectr-native-motion-ease-verdict");
    storage.require_unchanged();
}

// THE STATUS PILL IS FEEDBACK FOR AN EDIT.
//
// The top-centre pill ("12.3kHz   -1.3 dB   BAND 7/32") may appear or update only
// when a pointer gesture actually changes a band's level or mute state. Not on
// a plain hover, not on a press, not on a drag that leaves every band where it
// was, and never because an LFO moved what is painted. Every write is counted
// at the two doors the bank has into the pill: the direct show
// (spectrStatusBannerShow) and the live writer's keep-alive, which accompanies
// every direct text write. Both are wrapped through a property accessor, so a
// banner re-render that reinstalls either function is still counted.
TEST_CASE("the status pill shows a band reading only when a gesture edits a band",
          "[native-n1][state-parity][status-pill]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    rig.bridge().load_script(R"js((() => {
      const w = typeof window !== 'undefined' ? window : globalThis;
      const log = globalThis.__spectrPillWrites = [];
      for (const name of ['spectrStatusBannerShow', 'spectrStatusBannerKeepAlive']) {
        let impl = w[name];
        Object.defineProperty(w, name, {
          configurable: true,
          get() {
            if (typeof impl !== 'function') return impl;
            return (...args) => {
              log.push({ door: name, text: String(args[0] ?? '') });
              return impl(...args);
            };
          },
          set(value) { impl = value; },
        });
      }
      const selector = '[data-spectr-filter-surface]';
      globalThis.__spectrPillFire = (type, x, y, buttons, extra) => {
        if (!globalThis.__pulpActivateMaterializedElement__(selector, type, {
          clientX: x, clientY: y, pointerId: 91, button: 0, buttons,
          ...(extra || {})
        })) throw new Error('surface activation failed: ' + type);
        if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
          globalThis.__pulpRuntimeSettle__(2);
      };
      globalThis.__spectrPillMark = () => globalThis.__spectrPillWrites.length;
      globalThis.__spectrPillSince = (mark) => globalThis.__spectrPillWrites.slice(mark);
    })();)js", "spectr-native-pill-probe");

    const auto expect_no_writes = [&](std::string_view stimulus,
                                      std::string_view script,
                                      int frames = 12) {
        rig.bridge().load_script("globalThis.__spectrPillStart = __spectrPillMark();",
                                 "spectr-native-pill-mark");
        rig.bridge().load_script(std::string(script), "spectr-native-pill-stimulus");
        settle(rig.clock, frames);
        rig.bridge().load_script(
            std::string{"(() => { const writes = __spectrPillSince(__spectrPillStart); "
                        "if (writes.length) throw new Error("}
                + js_string(stimulus)
                + " + ' wrote the pill ' + writes.length + ' time(s): ' "
                  "+ JSON.stringify(writes)); })();",
            "spectr-native-pill-silent");
    };
    const auto require_js = [&](std::string_view script, std::string_view tag) {
        rig.bridge().load_script(std::string(script), std::string(tag));
    };

    // 1. A plain hover across the bank writes nothing.
    expect_no_writes("a plain hover", R"js((() => {
      for (let x = 300; x <= 900; x += 40) __spectrPillFire('pointermove', x, 430, 0);
    })();)js");

    // 2. A press, and a jitter under the drag threshold, write nothing. The
    // release of that click toggles the band's mute -- an edit -- so it is
    // measured separately below and undone afterwards.
    expect_no_writes("a press without a drag", R"js((() => {
      __spectrPillFire('pointermove', 320, 430, 0);
      __spectrPillFire('pointerdown', 320, 430, 1);
      __spectrPillFire('pointermove', 322, 431, 1);
    })();)js");
    require_js(R"js((() => {
      const mark = __spectrPillMark();
      __spectrPillFire('pointerup', 322, 431, 0);
      const writes = __spectrPillSince(mark);
      if (!writes.some(w => /^BAND \d+ MUTED$/.test(w.text)))
        throw new Error('the click that muted a band did not say so: '
          + JSON.stringify(writes));
      if (writes.some(w => /Hz/.test(w.text)))
        throw new Error('the click release also showed a level reading it did '
          + 'not change: ' + JSON.stringify(writes));
      // Put the band back so it plays no part below.
      __spectrPillFire('pointerdown', 322, 431, 1);
      __spectrPillFire('pointerup', 322, 431, 0);
    })();)js", "spectr-native-pill-click");
    settle(rig.clock, 6);

    // 3. A real stroke shows the reading of the band it edited.
    require_js(R"js((() => {
      const hooks = globalThis.__spectrTestHooks;
      globalThis.__spectrPillBefore = hooks.renderState().targetGains.slice();
      globalThis.__spectrPillStrokeStart = __spectrPillMark();
      __spectrPillFire('pointermove', 560, 430, 0);
      __spectrPillFire('pointerdown', 560, 430, 1);
      __spectrPillFire('pointermove', 600, 365, 1);
      __spectrPillFire('pointermove', 640, 365, 1);
    })();)js", "spectr-native-pill-stroke");
    settle(rig.clock, 6);
    require_js(R"js((() => {
      const hooks = globalThis.__spectrTestHooks;
      const before = globalThis.__spectrPillBefore;
      const after = hooks.renderState().targetGains;
      const changed = after.map((v, i) => v !== before[i] ? i : -1).filter(i => i >= 0);
      if (!changed.length) throw new Error('the stroke changed no band');
      const last = Math.max(...changed);
      const n = hooks.appState().settings.bandCount;
      const writes = __spectrPillSince(globalThis.__spectrPillStrokeStart);
      const expected = 'BAND ' + (last + 1) + '/' + n;
      const db = (after[last] * 24).toFixed(1);
      if (!writes.length)
        throw new Error('a stroke that changed bands ' + JSON.stringify(changed)
          + ' never wrote the pill');
      const shown = document.querySelector('[data-spectr-status-text]')?.textContent || '';
      if (!shown.includes(expected) || !shown.includes(db + ' dB'))
        throw new Error('the pill reads "' + shown + '" but the stroke left band '
          + (last + 1) + ' at ' + db + ' dB (' + expected + ')');
      __spectrPillFire('pointerup', 640, 365, 0);
    })();)js", "spectr-native-pill-stroke-reading");
    settle(rig.clock, 6);

    // 4. The same stroke again changes nothing: press, drag and release are
    // all silent. The control proves it really changed nothing.
    require_js(R"js((() => {
      globalThis.__spectrPillBefore =
        globalThis.__spectrTestHooks.renderState().targetGains.slice();
    })();)js", "spectr-native-pill-noop-before");
    expect_no_writes("a drag that changes no band", R"js((() => {
      __spectrPillFire('pointermove', 600, 365, 0);
      __spectrPillFire('pointerdown', 600, 365, 1);
      __spectrPillFire('pointermove', 620, 365, 1);
      __spectrPillFire('pointermove', 640, 365, 1);
      __spectrPillFire('pointerup', 640, 365, 0);
    })();)js");
    require_js(R"js((() => {
      const before = globalThis.__spectrPillBefore;
      const after = globalThis.__spectrTestHooks.renderState().targetGains;
      if (after.some((v, i) => v !== before[i]))
        throw new Error('the no-op stroke changed a band, so its silence proves '
          + 'nothing');
    })();)js", "spectr-native-pill-noop-control");

    // 5. An LFO moving the painted bank while the pointer is held still on a
    // band writes nothing. The display hold is turned off so the band under
    // the pointer really does move while it is pressed.
    activate(rig, "[data-spectr-settings-open]");
    activate(rig, "[data-spectr-hold-edit] [data-spectr-setting-toggle]");
    require_app_state(rig, "s.settings.holdModulationWhileEditing === false",
                      "the modulation hold did not turn off");
    activate(rig, "[data-spectr-settings-close]");
    settle(rig.clock, 6);
    rig.store.set_value(spectr::kParamLfoEnabled, 1.0f);
    rig.store.set_value(spectr::kParamLfoShape,
                        static_cast<float>(spectr::LfoShape::Sine));
    rig.store.set_value(spectr::kParamLfoRate, 0.25f);
    rig.store.set_value(spectr::kParamLfoDepth, 1.0f);
    rig.store.set_value(spectr::kParamLfoTarget,
                        static_cast<float>(spectr::ModulationTarget::WholeBank));
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);
    require_js(R"js((() => {
      globalThis.__spectrPillStart = __spectrPillMark();
      globalThis.__spectrPillDrawn = [];
      __spectrPillFire('pointermove', 760, 430, 0);
      __spectrPillFire('pointerdown', 760, 430, 1);
    })();)js", "spectr-native-pill-lfo-press");
    for (int sample = 0; sample < 8; ++sample) {
        feed_audio_blocks(rig, 6);
        settle(rig.clock, 3);
        require_js(R"js(globalThis.__spectrPillDrawn.push(
          globalThis.__spectrTestHooks.renderState().gains[20]);)js",
                   "spectr-native-pill-lfo-sample");
    }
    require_js(R"js((() => {
      const drawn = globalThis.__spectrPillDrawn;
      const spread = Math.max(...drawn) - Math.min(...drawn);
      // Stimulus control: the band under the pointer must really have moved.
      if (!(spread > 0.05))
        throw new Error('the LFO did not move the painted band under the pointer '
          + '(spread ' + spread + '), so a silent pill proves nothing');
      const writes = __spectrPillSince(globalThis.__spectrPillStart);
      if (writes.length)
        throw new Error('an LFO under a held pointer wrote the pill '
          + writes.length + ' time(s): ' + JSON.stringify(writes));
      // Releasing a press that never moved is a click, which toggles the
      // band's mute: that edit reports itself, and no level reading rides
      // along with it.
      __spectrPillFire('pointerup', 760, 430, 0);
      const released = __spectrPillSince(globalThis.__spectrPillStart);
      if (!released.length || released.some(w => !/^BAND \d+ (UN)?MUTED$/.test(w.text)))
        throw new Error('the click release after the held press wrote '
          + JSON.stringify(released) + '; only its mute toggle may show');
    })();)js", "spectr-native-pill-lfo-silent");
    rig.store.set_value(spectr::kParamLfoEnabled, 0.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 4);
    storage.require_unchanged();
}

// HOVERING THE SETTINGS CLOSE BUTTON RE-RENDERS THE BUTTON, NOT THE PANEL.
//
// The button's hover and press look is its own state. When it was state of
// the Settings panel, each pointer-enter and pointer-leave re-rendered every
// group, field, chip row and slider in the panel to recolour one 32px square,
// ~18 ms of a ~25 ms hover commit headless. Counted as the panel's rows
// rebuilt during hovers (each field is a React element created per render),
// with a control that the same counter sees the rows when the panel renders.
TEST_CASE("hovering the Settings close button re-renders only the button",
          "[native-n1][state-parity][render-scope]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    activate(rig, "[data-spectr-settings-open]");
    settle(rig.clock, 8);
    // The Settings panel reads window.SPECTR_MODULATION_LOOKS once per render,
    // so a counting getter on it counts panel renders.
    rig.bridge().load_script(R"js((() => {
      const w = typeof window !== 'undefined' ? window : globalThis;
      let looks = w.SPECTR_MODULATION_LOOKS;
      globalThis.__spectrPanelRenders = 0;
      Object.defineProperty(w, 'SPECTR_MODULATION_LOOKS', { configurable: true,
        get() { ++globalThis.__spectrPanelRenders; return looks; },
        set(value) { looks = value; } });
      const sel = '[data-spectr-settings-close]';
      for (let i = 0; i < 10; ++i) {
        globalThis.__pulpActivateMaterializedElement__(sel, i % 2 ? 'pointerleave' : 'pointerenter', null);
        globalThis.__pulpRuntimeSettle__(2);
      }
      const hovered = globalThis.__spectrPanelRenders;
      const state = document.querySelector(sel)?.getAttribute('data-spectr-close-state');
      // Control: a real panel render (a setting changed, then changed back)
      // must register on the same counter.
      globalThis.__spectrPanelRenders = 0;
      for (let i = 0; i < 2; ++i) {
        globalThis.__pulpActivateMaterializedElement__(
          '[data-spectr-status-info-toggle]', 'click', null);
        globalThis.__pulpRuntimeSettle__(4);
      }
      const control = globalThis.__spectrPanelRenders;
      Object.defineProperty(w, 'SPECTR_MODULATION_LOOKS', { configurable: true,
        writable: true, value: looks });
      if (!(control > 0))
        throw new Error('the panel-render counter saw nothing when a setting changed');
      if (state !== 'hover' && state !== 'idle')
        throw new Error('the close button lost its hover state: ' + state);
      if (hovered !== 0)
        throw new Error('10 hovers on the close button re-rendered the Settings panel '
          + hovered + ' times; the button must own its own hover state');
    })();)js", "spectr-native-close-hover-render-scope");
    storage.require_unchanged();
}

// HOST AUTOMATION MOVES THE EDITOR THROUGH ITS CHEAP PATHS.
//
// A host parameter change reaches the editor as one live-state projection per
// frame. It must update what changed through the paths pointer input uses --
// the viewport ref published live (settled once when the burst ends), the
// paint refs, the modulation publication -- and never re-render the editor.
// Perfetto measured spectr_host_automation_project at 22-127 ms per host
// change while each one committed React: the modulation hook merged every
// projection into a new object, re-rendering the (mounted, hidden) Settings
// panel, and each viewport change settled the zoom readout. So every metadata
// pass -- the per-commit hook, scoped or full -- is counted across a burst of
// viewport and LFO-shape automation and must be zero, and the editor must
// still end exactly where the host put it.
TEST_CASE("host automation of the viewport and LFO shape re-renders nothing",
          "[native-n1][state-parity][host-automation-cost]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.bridge().load_script(R"js((() => {
      const log = globalThis.__spectrMetadataPasses = [];
      // One stable wrapper per installed hook: the runtime treats a changed
      // hook identity as a new hook and forces a full pass, so a wrapper
      // minted per read would manufacture the very passes being counted.
      const wrap = (fn) => typeof fn !== 'function' ? fn : (...args) => {
        const scope = args[0];
        log.push(Array.isArray(scope) && scope.length ? 'scoped' : 'full');
        return fn(...args);
      };
      let wrapped = wrap(globalThis.__pulpApplyMaterializedImportMetadata__);
      Object.defineProperty(globalThis, '__pulpApplyMaterializedImportMetadata__', {
        configurable: true,
        get() { return wrapped; },
        set(value) { wrapped = wrap(value); },
      });
    })();)js", "spectr-native-metadata-pass-counter");
    const auto reset = [&] {
        rig.bridge().load_script("globalThis.__spectrMetadataPasses.length = 0;",
                                 "spectr-native-metadata-pass-reset");
    };
    const auto require_no_pass = [&](std::string_view what) {
        rig.bridge().load_script(
            std::string{"(() => { const passes = __spectrMetadataPasses; "
                        "if (passes.length) throw new Error("}
                + js_string(what)
                + " + ' committed React ' + passes.length + ' time(s): ' + JSON.stringify(passes)); })();",
            "spectr-native-metadata-pass-check");
    };
    const auto set_view = [&](float min_hz, float max_hz) {
        const auto [centre, width] = spectr::encode_viewport({min_hz, max_hz});
        rig.store.set_value(spectr::kParamViewportCenter, centre);
        rig.store.set_value(spectr::kParamViewportWidth, width);
        REQUIRE(rig.processor.apply_surface_params(false));
        settle(rig.clock, 2);
    };

    // Positive control on the counter: a real structural change (opening
    // Settings) must register passes, or a zero below proves nothing.
    reset();
    activate(rig, "[data-spectr-settings-open]");
    rig.bridge().load_script(
        "if (!__spectrMetadataPasses.length) throw new Error("
        "'the metadata-pass counter saw nothing when Settings opened');",
        "spectr-native-metadata-pass-control");
    activate(rig, "[data-spectr-settings-close]");
    settle(rig.clock, 8);
    // The first live projection after hydration used to re-set the motion
    // mode into a new settings object (a whole-app re-render) because the
    // live-mode cache starts empty; it must commit nothing either.
    reset();
    set_view(100.0f, 5000.0f);
    settle(rig.clock, 8);
    require_no_pass("the first projection after hydration");
    rig.bridge().load_script(
        "globalThis.__spectrReactViewBefore = JSON.stringify("
        "globalThis.__spectrTestHooks.renderState().reactView);",
        "spectr-native-react-view-before");

    // Viewport automation: eight windows.
    reset();
    float last_min = 0.0f, last_max = 0.0f;
    for (int step = 0; step < 8; ++step) {
        set_view(120.0f + 15.0f * static_cast<float>(step),
                 6000.0f + 400.0f * static_cast<float>(step));
        last_min = rig.processor.viewport().min_hz;
        last_max = rig.processor.viewport().max_hz;
    }
    require_no_pass("viewport automation");
    rig.bridge().load_script(
        std::string{"(() => { const s = globalThis.__spectrTestHooks.renderState(); "
                    "const v = s.view; const lmin = Math.log10("} + std::to_string(last_min)
            + "), lmax = Math.log10(" + std::to_string(last_max) + "); "
              "if (Math.abs(v.lmin - lmin) > 1e-4 || Math.abs(v.lmax - lmax) > 1e-4) "
              "throw new Error('the editor view ' + v.lmin + '..' + v.lmax + "
              "' does not match the host ' + lmin + '..' + lmax); "
              "if (JSON.stringify(s.reactView) !== globalThis.__spectrReactViewBefore) "
              "throw new Error('the burst settled the React copy of the view mid-burst'); })();",
        "spectr-native-host-viewport-matches");

    // LFO shape automation: through every shape twice, ending on sine.
    reset();
    for (int step = 0; step < 8; ++step) {
        rig.store.set_value(spectr::kParamLfoShape, static_cast<float>((step + 1) % 4));
        REQUIRE(rig.processor.apply_surface_params(false));
        settle(rig.clock, 2);
    }
    require_no_pass("LFO shape automation");
    rig.bridge().load_script(
        "(() => { const m = globalThis.__spectrModulationLast; "
        "if (!m) throw new Error('the editor holds no modulation state'); "
        "if (m.shape !== 0) throw new Error("
        "'the editor LFO shape ' + JSON.stringify(m.shape) + ' does not match the host (sine)'); })();",
        "spectr-native-host-lfo-matches");

    // Opening Settings after the burst shows the host's LFO shape: the panel
    // stopped following projections while hidden and catches up when shown.
    activate(rig, "[data-spectr-settings-open]");
    settle(rig.clock, 8);
    require_runtime_contract(rig,
        "globalThis.__spectrModulationLast && globalThis.__spectrModulationLast.shape === 0",
        "the Settings panel did not catch up with the host's LFO shape");
    storage.require_unchanged();
}

// A real host advances a frame by ticking the frame clock AND polling the
// scripted session: the poll services JS timers and commits each canvas's
// recorded commands to its native widget. settle() only ticks the clock, so a
// canvas's committed command stream and every setTimeout stand still under it.
void pump_host_frames(NativeEditorRig& rig, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        rig.clock.tick(1.0f / 60.0f);
        // poll() answers "did anything change"; false is an idle frame. Only a
        // reported error is a failure.
        std::string error;
        (void)rig.session->poll(&error);
        REQUIRE(error.empty());
    }
}

// The canvas that painted `text` last frame, searched through the tree.
const pulp::view::CanvasWidget* canvas_painting(const View& view,
                                                std::string_view text) {
    if (const auto* canvas = dynamic_cast<const pulp::view::CanvasWidget*>(&view))
        for (const auto& command : canvas->commands())
            if (command.type == pulp::view::CanvasDrawCmd::Type::fill_text
                && command.text == text)
                return canvas;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        if (const auto* found = canvas_painting(*view.child_at(index), text))
            return found;
    return nullptr;
}

// The frequency labels the band plot's ruler last committed, with their x.
// The ruler is painted on the plot's static-layer canvas, found as the canvas
// that painted the dBFS heading.
std::vector<std::pair<std::string, float>> committed_frequency_labels(
    NativeEditorRig& rig) {
    const auto* canvas = canvas_painting(*rig.root, "dBFS");
    REQUIRE(canvas != nullptr);
    std::vector<std::pair<std::string, float>> labels;
    for (const auto& command : canvas->commands())
        if (command.type == pulp::view::CanvasDrawCmd::Type::fill_text
            && command.text.size() > 2
            && command.text.compare(command.text.size() - 2, 2, "Hz") == 0)
            labels.emplace_back(command.text, command.x);
    return labels;
}

// HOST AUTOMATION REDRAWS THE PLOT AND SETTLES THE READOUT.
//
// The projection writes the view into refs and publishes it live, so the only
// proof it reached the screen is the plot's committed canvas: the frequency
// ruler must move to the new window. The control is the same pumping with no
// parameter change, which must leave the ruler exactly as it was. The zoom
// readout settles from a timer once the automation pauses, so the test lets
// real time pass and pumps the host, which services timers.
TEST_CASE("a host viewport change redraws the plot and settles the zoom readout",
          "[native-n1][state-parity][host-automation-cost]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    pump_host_frames(rig, 12);
    const auto idle = committed_frequency_labels(rig);
    REQUIRE_FALSE(idle.empty());

    // Control: no parameter change, same pumping, same ruler.
    pump_host_frames(rig, 12);
    CHECK(committed_frequency_labels(rig) == idle);

    const auto [centre, width] = spectr::encode_viewport({300.0f, 3000.0f});
    rig.store.set_value(spectr::kParamViewportCenter, centre);
    rig.store.set_value(spectr::kParamViewportWidth, width);
    REQUIRE(rig.processor.apply_surface_params(false));
    pump_host_frames(rig, 12);
    const auto moved = committed_frequency_labels(rig);
    CAPTURE(idle.size(), moved.size());
    REQUIRE(moved != idle);
    // The 1 kHz label sits where the new window puts 1 kHz on the plot.
    const auto viewport = rig.processor.viewport();
    const float lmin = std::log10(viewport.min_hz);
    const float lmax = std::log10(viewport.max_hz);
    constexpr float kInnerX = 56.0f, kInnerW = 1320.0f - 112.0f;
    const float expected_x = kInnerX + (3.0f - lmin) / (lmax - lmin) * kInnerW;
    const auto one_k = std::find_if(moved.begin(), moved.end(),
        [](const auto& label) { return label.first == "1kHz"; });
    REQUIRE(one_k != moved.end());
    CHECK(one_k->second == Catch::Approx(expected_x).margin(1.0f));

    // The readout settles once the burst pauses: after the settle delay the
    // React copy of the view and the printed zoom follow the host.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pump_host_frames(rig, 6);
    const double zoom = (std::log10(20000.0) - std::log10(20.0)) / (lmax - lmin);
    char expected_zoom[32];
    std::snprintf(expected_zoom, sizeof expected_zoom, "%.2f\u00d7 ZOOM", zoom);
    require_runtime_contract(rig,
        std::string{"(() => { const s = globalThis.__spectrTestHooks.renderState(); "
                    "return Math.abs(s.reactView.lmin - s.view.lmin) < 1e-9 "
                    "&& Math.abs(s.reactView.lmax - s.view.lmax) < 1e-9 "
                    "&& Array.from(document.querySelectorAll('span')).some("
                    "span => span.textContent === '"} + expected_zoom + "'); })()",
        "the zoom readout did not settle to the host's window");

    // LFO shape: the Settings panel shows the host's shape when it opens.
    rig.store.set_value(spectr::kParamLfoEnabled, 1.0f);
    rig.store.set_value(spectr::kParamLfoShape,
                        static_cast<float>(spectr::LfoShape::Saw));
    REQUIRE(rig.processor.apply_surface_params(false));
    pump_host_frames(rig, 6);
    activate(rig, "[data-spectr-settings-open]");
    pump_host_frames(rig, 8);
    require_runtime_contract(rig,
        // A boolean aria-pressed reflects as an empty attribute here, so read
        // the committed prop. The first Saw chip is LFO 1's shape row.
        "(() => { const chip = Array.from(document.querySelectorAll("
        "'[data-spectr-setting-option=\"3\"]')).find(n => n.textContent === 'Saw'); "
        "return !!chip && chip.__pulpAuthoredLayout__?.['aria-pressed'] === true; })()",
        "the Settings panel does not show the host's LFO shape (saw)");
    rig.store.set_value(spectr::kParamLfoEnabled, 0.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    storage.require_unchanged();
}

// The live band canvas, and its recorded command stream for the last frame.
const pulp::view::CanvasWidget& live_band_canvas(NativeEditorRig& rig) {
    const auto* canvas = dynamic_cast<const pulp::view::CanvasWidget*>(
        rig.bridge().widget("__behavior_pr_1"));
    REQUIRE(canvas != nullptr);
    return *canvas;
}

// BLOOM GLOWS COST THEIR DRAW AND NOTHING MORE.
//
// Each lit band paints an elliptical glow under translate + scale. Bracketing
// that with save()/restore() made every glow also drop the canvas shim's
// sent-state caches, so the next draw re-sent composite, alpha and the rest. A
// glow is recognised by its shape in the command stream -- translate, scale,
// begin_path, a full circle at the origin, fill -- and must be neither opened by
// a save nor closed by a restore; it closes with the inverse scale and
// translate instead, which return the transform exactly where it was. The
// saving is measured too: behind a bracket each glow re-sent alpha, blend and
// four shadow settings before its fill.
TEST_CASE("bloom glows undo their transform without a save and restore",
          "[native-n1][state-parity][paint-cost]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    // Audio through the analyzer lights the bands, which is what paints glows.
    for (int round = 0; round < 12; ++round) {
        feed_audio_blocks(rig, 16);
        pump_host_frames(rig, 2);
    }
    using Cmd = pulp::view::CanvasDrawCmd;
    const auto& cmds = live_band_canvas(rig).commands();
    int glows = 0;
    int bracketed = 0;
    int unbalanced = 0;
    int resent = 0;
    for (std::size_t k = 4; k + 3 < cmds.size(); ++k) {
        const auto& arc = cmds[k];
        if (arc.type != Cmd::Type::path_arc || arc.x != 0.0f || arc.y != 0.0f)
            continue;
        if (cmds[k - 1].type != Cmd::Type::begin_path
            || cmds[k - 2].type != Cmd::Type::scale
            || cmds[k - 3].type != Cmd::Type::translate)
            continue;
        // The fill, after any state the shim re-sends ahead of it (a
        // save/restore bracket drops its sent-state caches, so the fill is
        // preceded by re-sent alpha or composite).
        std::size_t fill = k + 1;
        while (fill < cmds.size() && fill < k + 16
               && cmds[fill].type != Cmd::Type::fill_path) ++fill;
        if (fill >= cmds.size() || cmds[fill].type != Cmd::Type::fill_path
            || fill + 2 >= cmds.size())
            continue;
        // Sticky state re-sent ahead of this glow's fill. The first glow may
        // follow a restore from the painter before it; every later glow runs
        // inside the bloom loop and must find the caches intact. The radial
        // gradient itself is this glow's own fill style, not re-sent state.
        if (glows > 0)
            for (std::size_t j = k + 1; j < fill; ++j) {
                const auto type = cmds[j].type;
                if (type == Cmd::Type::set_global_alpha
                    || type == Cmd::Type::set_blend_mode
                    || type == Cmd::Type::set_shadow_color
                    || type == Cmd::Type::set_shadow_blur
                    || type == Cmd::Type::set_shadow_offset_x
                    || type == Cmd::Type::set_shadow_offset_y)
                    ++resent;
            }
        ++glows;
        if (cmds[k - 4].type == Cmd::Type::save
            || cmds[fill + 1].type == Cmd::Type::restore)
            ++bracketed;
        // The close: the inverse scale, then the inverse translate.
        const auto& scale = cmds[k - 2];
        const auto& translate = cmds[k - 3];
        const auto& undo_scale = cmds[fill + 1];
        const auto& undo_translate = cmds[fill + 2];
        if (undo_scale.type != Cmd::Type::scale
            || undo_translate.type != Cmd::Type::translate
            || std::abs(undo_scale.x * scale.x - 1.0f) > 1e-5f
            || undo_scale.y != 1.0f
            || undo_translate.x != -translate.x
            || undo_translate.y != -translate.y)
            ++unbalanced;
    }
    CAPTURE(cmds.size(), glows, bracketed, unbalanced, resent);
    // Stimulus control: with no glow in the frame there is nothing to judge.
    REQUIRE(glows > 0);
    CHECK(bracketed == 0);
    CHECK(unbalanced == 0);
    // With the caches intact, no glow after the first re-sends state.
    CHECK(resent == 0);
    storage.require_unchanged();
}

// THE PLOT'S STATIC LAYER IS PAINTED ONCE, NOT EVERY FRAME.
//
// The background, grid and rulers depend only on the view, the plot geometry,
// the band count, the theme, the rulers setting and the analyzer's dB scale.
// They live on their own canvas behind the band canvas and repaint only when
// one of those changes. So while audio animates the band canvas every frame,
// the band canvas carries no ruler label and the static canvas's recorded
// commands stay exactly as they were; a host viewport change repaints the
// static canvas with the ruler moved to the new window. Pixel identity with
// the single-canvas painter is checked by native-shot Skia renders.
TEST_CASE("the plot's static layer repaints only when its inputs change",
          "[native-n1][state-parity][paint-cost]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.bridge().load_script(
        "globalThis.__spectrStaticId = String(document.querySelector("
        "'[data-spectr-static-canvas]')?.__pulpId || '');",
        "spectr-native-static-canvas-id");
    rig.bridge().load_script(
        "if (!globalThis.__spectrStaticId) throw new Error('no static canvas');",
        "spectr-native-static-canvas-present");
    const auto static_canvas = [&] { return canvas_painting(*rig.root, "dBFS"); };
    const auto* band_canvas = dynamic_cast<const pulp::view::CanvasWidget*>(
        rig.bridge().widget("__behavior_pr_1"));
    REQUIRE(band_canvas != nullptr);
    for (int round = 0; round < 6; ++round) {
        feed_audio_blocks(rig, 16);
        pump_host_frames(rig, 2);
    }
    const auto* layer = static_canvas();
    REQUIRE(layer != nullptr);
    REQUIRE(layer != band_canvas);
    const auto recorded = layer->commands().size();
    // No ruler label on the per-frame band canvas.
    const auto band_ruler_labels = std::count_if(
        band_canvas->commands().begin(), band_canvas->commands().end(),
        [](const auto& command) {
            return command.type == pulp::view::CanvasDrawCmd::Type::fill_text
                && (command.text == "dBFS" || command.text == "1kHz");
        });
    CHECK(band_ruler_labels == 0);
    const auto layer_labels = [&] {
        std::vector<std::pair<std::string, float>> labels;
        for (const auto& command : layer->commands())
            if (command.type == pulp::view::CanvasDrawCmd::Type::fill_text
                && command.text.size() > 2
                && command.text.compare(command.text.size() - 2, 2, "Hz") == 0)
                labels.emplace_back(command.text, command.x);
        return labels;
    };
    const auto idle_labels = layer_labels();
    REQUIRE_FALSE(idle_labels.empty());

    // Audio keeps the band canvas busy; the static layer does not move.
    for (int round = 0; round < 6; ++round) {
        feed_audio_blocks(rig, 16);
        pump_host_frames(rig, 2);
    }
    CHECK(layer->commands().size() == recorded);
    CHECK(layer_labels() == idle_labels);

    // A host viewport change repaints it with the ruler in the new window.
    const auto [centre, width] = spectr::encode_viewport({300.0f, 3000.0f});
    rig.store.set_value(spectr::kParamViewportCenter, centre);
    rig.store.set_value(spectr::kParamViewportWidth, width);
    REQUIRE(rig.processor.apply_surface_params(false));
    pump_host_frames(rig, 12);
    CHECK(layer_labels() != idle_labels);
    storage.require_unchanged();
}

// ── Plain-key shortcut policy ───────────────────────────────────────────────
//
// A DAW owns its plain keys: Logic's Musical Typing plays notes on A S D F G H
// J K L ; ' and W E T Y U O P, and the digits pick octave and velocity. Spectr
// cannot ask a host whether Musical Typing is open, so inside a plug-in its
// single-letter shortcuts are off unless "Keyboard shortcuts in DAW" is on,
// and every such key is handed back UNCONSUMED -- `dispatch_key_for_root`
// returning false is exactly what makes the plug-in view return the key to
// the host. The standalone owns its window and keeps them.

namespace {

struct EditorContextScope {
    explicit EditorContextScope(bool standalone)
        : previous(spectr::editor_is_standalone()) {
        spectr::set_editor_is_standalone(standalone);
    }
    ~EditorContextScope() { spectr::set_editor_is_standalone(previous); }
    bool previous;
};

bool press_key(NativeEditorRig& rig, pulp::view::KeyCode key,
               std::uint16_t modifiers = pulp::view::kModNone) {
    const bool consumed = pulp::view::WidgetBridge::dispatch_key_for_root(
        *rig.root, static_cast<int>(key), modifiers, true);
    settle(rig.clock, 8);
    return consumed;
}

// runtime_string carries the thrown error's stack after the value.
std::string runtime_value(NativeEditorRig& rig, std::string_view expression,
                          std::string_view label) {
    auto value = runtime_string(rig, expression, label);
    const auto newline = value.find('\n');
    if (newline != std::string::npos) value.erase(newline);
    return value;
}

pulp::view::KeyCode key_of(char c) {
    return static_cast<pulp::view::KeyCode>(c);
}

int muted_band_count(NativeEditorRig& rig) {
    int muted = 0;
    for (const auto& band : rig.processor.field().bands) muted += band.muted ? 1 : 0;
    return muted;
}

// Everything a plain key in the inventory could change, in one string.
std::string shortcut_fingerprint(NativeEditorRig& rig) {
    return runtime_value(
               rig,
               "(() => { const a = __spectrTestHooks.appState(); "
               "const r = __spectrTestHooks.renderState(); "
               "return JSON.stringify([a.editMode, a.analyzerMode, "
               "r.selection.length]); })()",
               "spectr-keyboard-fingerprint")
        + "|muted=" + std::to_string(muted_band_count(rig))
        + "|render=" + std::to_string(static_cast<int>(rig.processor.render_mode()))
        + "|freeze=" + std::to_string(rig.store.get_value(spectr::kParamFreeze));
}

std::string app_string(NativeEditorRig& rig, std::string_view field) {
    return runtime_value(
        rig, "String(__spectrTestHooks.appState()." + std::string(field) + ")",
        "spectr-keyboard-app-field");
}

// Select every band through the Cmd chord, which every context keeps, so M and
// Escape are tested with something to act on rather than nothing to do.
void select_all(NativeEditorRig& rig) {
    REQUIRE(press_key(rig, pulp::view::KeyCode::a, pulp::view::kModCmd));
    REQUIRE(runtime_value(rig, "__spectrTestHooks.renderState().selection.length",
                           "spectr-keyboard-selection") != "0");
}

// Keys removed in every context: the digit aliases for the edit modes, which
// no surface showed; and A and 6, which cycled the analyzer (A is a Musical
// Typing note).
void require_removed_keys_do_nothing(NativeEditorRig& rig) {
    select_all(rig);
    for (const char key : {'1', '2', '3', '4', '5', 'a', '6'}) {
        INFO("removed key " << key);
        const auto before = shortcut_fingerprint(rig);
        CHECK_FALSE(press_key(rig, key_of(key)));
        CHECK(shortcut_fingerprint(rig) == before);
    }
    // W and the rest of the Musical Typing rows were never bound; they must
    // stay the host's in every context.
    for (const char key : {'w', 'd', 'h', 'j', 'k', 'e', 'y', 'u', 'o', 'p',
                           'z', 'x', 'c', 'v', '7', '8'}) {
        INFO("unbound key " << key);
        const auto unbound_before = shortcut_fingerprint(rig);
        CHECK_FALSE(press_key(rig, key_of(key)));
        CHECK(shortcut_fingerprint(rig) == unbound_before);
    }
}

// Every documented plain key does its action and is consumed.
void require_documented_keys_act(NativeEditorRig& rig) {
    select_all(rig);
    const std::array<std::pair<char, const char*>, 5> modes{{
        {'l', "level"}, {'b', "boost"}, {'f', "flare"}, {'g', "glide"}, {'s', "sculpt"}}};
    for (const auto& [key, mode] : modes) {
        INFO("edit-mode key " << key);
        CHECK(press_key(rig, key_of(key)));
        CHECK(app_string(rig, "editMode") == mode);
    }
    {
        INFO("mute key m");
        const int before = muted_band_count(rig);
        CHECK(press_key(rig, pulp::view::KeyCode::m));
        CHECK(muted_band_count(rig) != before);
    }
    {
        INFO("latency key t");
        const auto before = rig.processor.render_mode();
        CHECK(press_key(rig, pulp::view::KeyCode::t));
        CHECK(rig.processor.render_mode() != before);
    }
    {
        // Q writes the Freeze parameter, the lane a host records, and the
        // header toggle turns over with it. A second press releases.
        INFO("freeze key q");
        REQUIRE(rig.store.get_value(spectr::kParamFreeze) == 0.0f);
        CHECK(press_key(rig, key_of('q')));
        CHECK(rig.store.get_value(spectr::kParamFreeze) == 1.0f);
        CHECK(runtime_value(rig,
            "document.querySelector('[data-spectr-freeze-toggle]')"
            ".getAttribute('data-spectr-freeze-state')",
            "spectr-keyboard-freeze-face") == "frozen");
        CHECK(press_key(rig, key_of('q')));
        CHECK(rig.store.get_value(spectr::kParamFreeze) == 0.0f);
    }
}

std::string selection_size(NativeEditorRig& rig) {
    return runtime_value(rig, "String(__spectrTestHooks.renderState().selection.length)",
                         "spectr-keyboard-selection-size");
}

// Escape, in every context. An open menu takes it first and the selection
// survives; with the menu gone it clears the selection and is consumed; with
// nothing selected it is not consumed, so the host still gets its Escape.
void require_escape_clears_selection(NativeEditorRig& rig) {
    select_all(rig);
    const auto selected = selection_size(rig);
    REQUIRE(selected != "0");
    activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
    REQUIRE(runtime_value(rig, "String(!!document.querySelector('[data-spectr-menu-options]'))",
                          "spectr-keyboard-escape-menu-open") == "true");
    CHECK(press_key(rig, pulp::view::KeyCode::escape));
    CHECK(runtime_value(rig, "String(!!document.querySelector('[data-spectr-menu-options]'))",
                        "spectr-keyboard-escape-menu-closed") == "false");
    CHECK(selection_size(rig) == selected);
    INFO("Escape with a selection standing");
    CHECK(press_key(rig, pulp::view::KeyCode::escape));
    CHECK(selection_size(rig) == "0");
    INFO("Escape with nothing selected");
    const auto before = shortcut_fingerprint(rig);
    CHECK_FALSE(press_key(rig, pulp::view::KeyCode::escape));
    CHECK(shortcut_fingerprint(rig) == before);
}

constexpr std::uint16_t kFreezeChord =
    pulp::view::kModCtrl | pulp::view::kModAlt | pulp::view::kModCmd;

// Ctrl+Opt+Cmd+F toggles Freeze in every context and is consumed. The chords
// DAWs DO use by default -- and would have to keep -- change nothing and go to
// the host; so does the chord itself while a menu owns the keyboard.
void require_freeze_chord(NativeEditorRig& rig) {
    const auto freeze = [&] { return rig.store.get_value(spectr::kParamFreeze); };
    REQUIRE(freeze() == 0.0f);
    CHECK(press_key(rig, pulp::view::KeyCode::f, kFreezeChord));
    CHECK(freeze() == 1.0f);
    CHECK(runtime_value(rig,
        "document.querySelector('[data-spectr-freeze-toggle]')"
        ".getAttribute('data-spectr-freeze-state')",
        "spectr-keyboard-chord-face") == "frozen");
    CHECK(press_key(rig, pulp::view::KeyCode::f, kFreezeChord));
    CHECK(freeze() == 0.0f);
    using pulp::view::kModAlt;
    using pulp::view::kModCmd;
    using pulp::view::kModCtrl;
    using pulp::view::kModShift;
    for (const std::uint16_t other : {std::uint16_t(kModAlt | kModCmd),
                                      std::uint16_t(kModCtrl | kModAlt),
                                      std::uint16_t(kModCmd | kModShift),
                                      std::uint16_t(kModCtrl | kModShift),
                                      std::uint16_t(kModCmd | kModAlt | kModShift),
                                      std::uint16_t(kFreezeChord | kModShift)}) {
        INFO("a host's chord, modifiers " << other);
        const auto before = shortcut_fingerprint(rig);
        CHECK_FALSE(press_key(rig, pulp::view::KeyCode::f, other));
        CHECK(shortcut_fingerprint(rig) == before);
    }
    activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
    CHECK_FALSE(press_key(rig, pulp::view::KeyCode::f, kFreezeChord));
    CHECK(freeze() == 0.0f);
    CHECK(press_key(rig, pulp::view::KeyCode::escape));
}

// Every documented plain key is handed back to the host and changes nothing.
void require_documented_keys_go_to_host(NativeEditorRig& rig) {
    select_all(rig);
    for (const char key : {'s', 'l', 'b', 'f', 'g', 'm', 't', 'q'}) {
        INFO("gated key " << key);
        const auto before = shortcut_fingerprint(rig);
        CHECK_FALSE(press_key(rig, key_of(key)));
        CHECK(shortcut_fingerprint(rig) == before);
    }
}

}  // namespace

TEST_CASE("in a plug-in, plain-key shortcuts go to the DAW by default",
          "[native-n1][state-parity][keyboard]") {
    PatternStoragePoison storage;
    EditorContextScope hosted(false);
    NativeEditorRig rig;
    require_home(rig);
    REQUIRE_FALSE(rig.processor.keyboard_shortcuts_in_daw());
    // Control: the Cmd chord is still the editor's, so a false below is the
    // policy speaking rather than a dead key path.
    CHECK(press_key(rig, pulp::view::KeyCode::a, pulp::view::kModCmd));
    require_documented_keys_go_to_host(rig);
    require_removed_keys_do_nothing(rig);
    require_escape_clears_selection(rig);
    require_freeze_chord(rig);
    storage.require_unchanged();
}

TEST_CASE("in a plug-in, Keyboard shortcuts in DAW makes the plain keys live",
          "[native-n1][state-parity][keyboard]") {
    PatternStoragePoison storage;
    EditorContextScope hosted(false);
    NativeEditorRig rig;
    require_home(rig);
    // Through the Settings switch itself, so the switch, the cache the key
    // handler reads, and the processor's persisted value are all exercised.
    activate(rig, "[data-spectr-settings-open]");
    require_runtime_contract(
        rig, "document.querySelector('[data-spectr-keyboard-shortcuts-in-daw=\"off\"]')",
        "the Keyboard shortcuts in DAW switch is missing in a plug-in");
    rig.bridge().load_script("spectrSetKeyboardShortcutsInDaw(true);",
                             "spectr-keyboard-switch-on");
    settle(rig.clock, 8);
    CHECK(rig.processor.keyboard_shortcuts_in_daw());
    require_runtime_contract(
        rig, "document.querySelector('[data-spectr-keyboard-shortcuts-in-daw=\"on\"]')",
        "the switch did not show its new state");
    activate(rig, "[data-spectr-settings-close]");
    require_documented_keys_act(rig);
    require_removed_keys_do_nothing(rig);
    require_escape_clears_selection(rig);
    require_freeze_chord(rig);

    // Persisted with the plugin state: a reloaded instance keeps it.
    const auto blob = rig.processor.serialize_plugin_state();
    NativeEditorRig reloaded(blob);
    CHECK(reloaded.processor.keyboard_shortcuts_in_daw());
    storage.require_unchanged();
}

TEST_CASE("in the standalone, plain-key shortcuts stay live",
          "[native-n1][state-parity][keyboard]") {
    PatternStoragePoison storage;
    EditorContextScope standalone(true);
    NativeEditorRig rig;
    require_home(rig);
    REQUIRE_FALSE(rig.processor.keyboard_shortcuts_in_daw());
    // The switch only means something in a DAW, so the standalone hides it.
    activate(rig, "[data-spectr-settings-open]");
    require_runtime_contract(
        rig, "!document.querySelector('[data-spectr-keyboard-shortcuts-in-daw]')",
        "the standalone shows a DAW-only switch");
    activate(rig, "[data-spectr-settings-close]");
    require_documented_keys_act(rig);
    require_removed_keys_do_nothing(rig);
    require_escape_clears_selection(rig);
    require_freeze_chord(rig);
    storage.require_unchanged();
}

TEST_CASE("key hints appear only where their keys are live",
          "[native-n1][state-parity][keyboard][hints]") {
    PatternStoragePoison storage;
    const auto hints = [](bool standalone, bool in_daw) {
        EditorContextScope context(standalone);
        NativeEditorRig rig;
        require_home(rig);
        if (in_daw) {
            rig.bridge().load_script("spectrSetKeyboardShortcutsInDaw(true);",
                                     "spectr-keyboard-switch-on");
            settle(rig.clock, 8);
        }
        std::string out;
        activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
        out += "chips=" + runtime_value(
            rig, "document.querySelectorAll('[data-spectr-shortcut-chip]').length",
            "spectr-keyboard-chips");
        CHECK(press_key(rig, pulp::view::KeyCode::escape));
        activate(rig, "[data-spectr-menu-root=\"analyzer\"] [data-spectr-menu-trigger]");
        // The analyzer names no key in any context: its shortcut is gone.
        out += " analyzer=" + runtime_value(
            rig,
            "Array.from(document.querySelectorAll('[data-spectr-menu-options] div'))"
            ".map(d => d.textContent).filter(t => t.indexOf('ANALYZER') === 0)[0]",
            "spectr-keyboard-analyzer-header");
        CHECK(press_key(rig, pulp::view::KeyCode::escape));
        activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
        out += " note=" + runtime_value(
            rig, "document.querySelectorAll('[data-spectr-shortcuts-daw-note]').length",
            "spectr-keyboard-daw-note");
        out += " help=" + runtime_value(
            rig,
            "String(Array.from(document.querySelectorAll('[data-spectr-help-panel] *'))"
            ".some(n => n.textContent === 'S / L / B'))",
            "spectr-keyboard-help-rows");
        out += " latency=" + runtime_value(
            rig,
            "String(String(document.querySelector('[data-spectr-latency-chip]')"
            ".getAttribute('title') || '').indexOf('press T') >= 0)",
            "spectr-keyboard-latency-title");
        // The freeze toggle's tooltip names the chord everywhere and Q only
        // where Q works; the SHORTCUTS panel lists the chord and Escape in
        // every context, and Q only where it works.
        out += " freeze=" + runtime_value(
            rig,
            "String(document.querySelector('[data-spectr-freeze-toggle]')"
            ".getAttribute('title') || '')",
            "spectr-keyboard-freeze-title");
        const auto row = [&](std::string_view key) {
            return runtime_value(
                rig,
                "String(Array.from(document.querySelectorAll('[data-spectr-help-panel] span'))"
                ".some(n => n.textContent === '" + std::string(key) + "'"
                " && n.nextSibling && n.nextSibling.textContent === 'Freeze / unfreeze'))",
                "spectr-keyboard-freeze-row");
        };
        out += " q=" + row("Q");
        out += " chord=" + row("CTRL+OPT+CMD+F");
        out += " esc=" + runtime_value(
            rig,
            "String(Array.from(document.querySelectorAll('[data-spectr-help-panel] span'))"
            ".some(n => n.textContent === 'ESC' && n.nextSibling"
            " && n.nextSibling.textContent === 'Clear selection'))",
            "spectr-keyboard-escape-row");
        return out;
    };
    const std::string live =
        "chips=5 analyzer=ANALYZER note=0 help=true latency=true"
        " freeze=Freeze the incoming sound (Ctrl+Opt+Cmd+F or Q) q=true chord=true esc=true";
    const std::string off =
        "chips=0 analyzer=ANALYZER note=1 help=false latency=false"
        " freeze=Freeze the incoming sound (Ctrl+Opt+Cmd+F) q=false chord=true esc=true";
    CHECK(hints(/*standalone=*/false, /*in_daw=*/false) == off);
    CHECK(hints(/*standalone=*/false, /*in_daw=*/true) == live);
    CHECK(hints(/*standalone=*/true, /*in_daw=*/false) == live);
    storage.require_unchanged();
}

// HOST AUTOMATION OF FREEZE TURNS THE TOGGLE OVER, AND ONLY THE TOGGLE.
//
// Freeze is host parameter 3. A host write reaches the editor through the same
// live projection the other lanes ride; the toggle must take the host's value,
// and the commit that shows it must be the toggle's own, never a whole-editor
// pass. The control on the pass counter is a real structural change (opening
// Settings), and the control on "only when it moves" is a projection that
// leaves Freeze alone, which must commit nothing.
TEST_CASE("host automation of Freeze turns the toggle over as a leaf",
          "[native-n1][state-parity][host-automation-cost][freeze-toggle]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    rig.bridge().load_script(R"js((() => {
      const log = globalThis.__spectrMetadataPasses = [];
      const wrap = (fn) => typeof fn !== 'function' ? fn : (...args) => {
        const scope = args[0];
        log.push(Array.isArray(scope) && scope.length ? 'scoped' : 'full');
        return fn(...args);
      };
      let wrapped = wrap(globalThis.__pulpApplyMaterializedImportMetadata__);
      Object.defineProperty(globalThis, '__pulpApplyMaterializedImportMetadata__', {
        configurable: true,
        get() { return wrapped; },
        set(value) { wrapped = wrap(value); },
      });
    })();)js", "spectr-native-freeze-pass-counter");
    const auto reset = [&] {
        rig.bridge().load_script("globalThis.__spectrMetadataPasses.length = 0;",
                                 "spectr-native-freeze-pass-reset");
    };
    const auto passes = [&] {
        return runtime_value(rig, "JSON.stringify(globalThis.__spectrMetadataPasses)",
                             "spectr-native-freeze-passes");
    };
    const auto face = [&] {
        return runtime_value(rig,
            "document.querySelector('[data-spectr-freeze-toggle]')"
            ".getAttribute('data-spectr-freeze-state')",
            "spectr-native-freeze-face");
    };
    const auto host_writes = [&](float value) {
        rig.store.set_value(spectr::kParamFreeze, value);
        REQUIRE(rig.processor.apply_surface_params(false));
        settle(rig.clock, 8);
    };

    reset();
    activate(rig, "[data-spectr-settings-open]");
    REQUIRE(passes() != "[]");
    activate(rig, "[data-spectr-settings-close]");
    settle(rig.clock, 8);

    REQUIRE(face() == "live");
    reset();
    host_writes(1.0f);
    CHECK(face() == "frozen");
    const auto engaged = passes();
    INFO("passes for a host freeze: " << engaged);
    CHECK(engaged.find("full") == std::string::npos);

    // A projection that leaves Freeze where it is commits nothing.
    reset();
    rig.store.set_value(spectr::kParamLfoShape, 2.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 8);
    CHECK(passes() == "[]");
    CHECK(face() == "frozen");

    reset();
    host_writes(0.0f);
    CHECK(face() == "live");
    CHECK(passes().find("full") == std::string::npos);

    // And the other direction: a press writes the host parameter.
    activate(rig, "[data-spectr-freeze-toggle]");
    CHECK(rig.store.get_value(spectr::kParamFreeze) == 1.0f);
    CHECK(face() == "frozen");
    activate(rig, "[data-spectr-freeze-toggle]");
    CHECK(rig.store.get_value(spectr::kParamFreeze) == 0.0f);
    storage.require_unchanged();
}



// The Custom editor's Fraction list: one column of the seventeen fractions in
// a viewport eight rows tall that scrolls by wheel and by the arrow keys,
// opens with the chosen fraction in view, and takes a press on every row
// through the host's own press route (overlay first, then mouse down / up).
TEST_CASE("the Fraction list is one scrolling column whose every row takes a press",
          "[native-n1][state-parity][freeze-length][fraction-list]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto value_of = [&](const std::string& expression) {
        auto value = runtime_string(rig, expression, "spectr-native-fraction-value");
        return value.substr(0, value.find('\n'));
    };
    const auto rect_of = [&](const std::string& selector) {
        const auto text = value_of(
            "(() => { const n = document.querySelector(" + js_string(selector)
            + "); if (!n) return 'none'; const r = n.getBoundingClientRect(); "
              "return [r.left, r.top, r.width, r.height].join(','); })()");
        std::array<float, 4> r{};
        if (text == "none") return std::optional<std::array<float, 4>>{};
        std::stringstream in(text);
        std::string part;
        for (auto& v : r) { std::getline(in, part, ','); v = std::stof(part); }
        return std::optional<std::array<float, 4>>{r};
    };
    // The host's press, in its order (window_host_mac.mm): the overlay slot
    // first; a routed press goes to its target without bubbling; the click
    // fires on mouse-up.
    const auto host_press = [&](pulp::view::Point pt) {
        auto& root = *rig.root;
        const auto overlay = pulp::view::route_press_to_active_overlay(root, pt);
        View* target = nullptr;
        bool bubble = true;
        if (overlay.routing == pulp::view::OverlayPressRouting::routed) {
            target = overlay.target;
            bubble = false;
        } else if (overlay.consume_press) {
            settle(rig.clock, 8);
            return std::string{"consumed"};
        } else {
            target = root.hit_test(pt);
        }
        if (target == nullptr) return std::string{"no-target"};
        pulp::view::deliver_mouse_down(root, target, pt, 0, 1, bubble);
        std::string clicked{"<none>"};
        pulp::view::MouseUpHost up;
        up.fire_click = [&clicked](const std::function<void()>& handler,
                                   const std::string& id, std::uint16_t) {
            clicked = id.empty() ? std::string{"<anon>"} : id;
            if (handler) handler();
        };
        pulp::view::deliver_mouse_up(root, target, pt, 0, 1, up);
        settle(rig.clock, 8);
        return clicked;
    };
    const auto centre = [](const std::array<float, 4>& r) {
        return pulp::view::Point{r[0] + r[2] * 0.5f, r[1] + r[3] * 0.5f};
    };
    const auto offset = [&] {
        return std::stoi(value_of(
            "document.querySelector('[data-spectr-length-fraction-options]')"
            ".getAttribute('data-spectr-length-fraction-offset')"));
    };
    const auto open_list = [&] {
        REQUIRE(value_of("!!document.querySelector('[data-spectr-length-editor]')") == "true");
        if (value_of("!!document.querySelector('[data-spectr-length-fraction-options]')") != "true")
            host_press(centre(*rect_of("[data-spectr-length-fraction]")));
        REQUIRE(value_of("!!document.querySelector('[data-spectr-length-fraction-options]')") == "true");
    };
    const auto key = [&](pulp::view::KeyCode code) {
        (void)pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(code), pulp::view::kModNone, true);
        settle(rig.clock, 8);
    };

    activate(rig, "[data-spectr-length-trigger]");
    activate(rig, "[data-spectr-length-option=\"custom-editor\"]");
    settle(rig.clock, 8);
    // While the editor is open the trigger reads "Custom", in the bound mono
    // face every other length label uses.
    CHECK(find_label(*rig.root, "Custom") != nullptr);
    CHECK(find_label(*rig.root, "Custom…") == nullptr);
    // No fraction reads as an em dash on the trigger and in the list, never
    // "0"; the model keeps "0".
    CHECK(value_of("document.querySelector('[data-spectr-length-fraction]')"
                   ".getAttribute('data-spectr-length-fraction')") == "0");
    CHECK(value_of("document.querySelector('[data-spectr-length-fraction] span').textContent")
          == "\u2014");
    open_list();
    CHECK(value_of("document.querySelector('[data-spectr-length-fraction-option=\"0\"] span')"
                   ".textContent") == "\u2014");
    CHECK(value_of("String(Array.from(document.querySelectorAll('[data-spectr-length-fraction-option] span'))"
                   ".some((n) => n.textContent === '0'))") == "false");

    const std::vector<std::string> fractions{
        "0", "1/32", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "1/3",
        "3/8", "1/2", "5/8", "2/3", "3/4", "5/6", "7/8", "15/16"};
    REQUIRE(value_of("JSON.stringify(Array.from(document.querySelectorAll("
                     "'[data-spectr-length-fraction-option]')).map((n) => "
                     "n.getAttribute('data-spectr-length-fraction-option')))")
            == "[\"0\",\"1/32\",\"1/16\",\"1/12\",\"1/8\",\"1/6\",\"3/16\",\"1/4\",\"1/3\","
               "\"3/8\",\"1/2\",\"5/8\",\"2/3\",\"3/4\",\"5/6\",\"7/8\",\"15/16\"]");
    // One column: every row has the same left edge and width; eight rows show.
    const auto viewport = *rect_of("[data-spectr-length-fraction-viewport]");
    const auto first = *rect_of("[data-spectr-length-fraction-option=\"0\"]");
    for (const auto& f : fractions) {
        const auto r = *rect_of("[data-spectr-length-fraction-option=\"" + f + "\"]");
        CHECK(r[0] == Catch::Approx(first[0]));
        CHECK(r[2] == Catch::Approx(first[2]));
        CHECK(r[3] == Catch::Approx(30.0f));
    }
    CHECK(viewport[3] == Catch::Approx(254.0f));
    // Under the Fraction trigger, like a combo box: left edges aligned, the
    // trigger's width, 3pt below it.
    {
        const auto list = *rect_of("[data-spectr-length-fraction-options]");
        const auto trigger = *rect_of("[data-spectr-length-fraction]");
        CHECK(list[0] == Catch::Approx(trigger[0]).margin(1.0));
        CHECK(list[2] == Catch::Approx(trigger[2]).margin(0.5));
        CHECK(list[1] == Catch::Approx(trigger[1] + trigger[3] + 3.0f).margin(0.5));
    }
    CHECK(rect_of("[data-spectr-length-fraction-scrollbar-thumb]").has_value());
    // Opens on "0" at the top.
    CHECK(offset() == 0);

    // The wheel scrolls it, clamped at both ends. Pulp's wheel delta is
    // positive downward (NSEvent's scrollingDeltaY negated).
    const auto over = centre(viewport);
    pulp::view::deliver_mouse_wheel(*rig.root, over, 0.0f, 40.0f, {});
    settle(rig.clock, 8);
    CHECK(offset() == 40);
    for (int i = 0; i < 20; ++i)
        pulp::view::deliver_mouse_wheel(*rig.root, over, 0.0f, 40.0f, {});
    settle(rig.clock, 8);
    CHECK(offset() == 17 * 32 - 2 - 254);
    for (int i = 0; i < 20; ++i)
        pulp::view::deliver_mouse_wheel(*rig.root, over, 0.0f, -40.0f, {});
    settle(rig.clock, 8);
    CHECK(offset() == 0);
    // The wheel moved the list, not the band plot behind it.
    REQUIRE(value_of("!!document.querySelector('[data-spectr-length-fraction-options]')") == "true");

    // Every row takes a press at its painted centre once scrolled into view,
    // and the press picks exactly that fraction.
    const auto pick = [&](const std::string& f) {
        INFO("fraction=" << f);
        open_list();
        const auto sel = "[data-spectr-length-fraction-option=\"" + f + "\"]";
        for (int i = 0; i < 40; ++i) {
            const auto r = *rect_of(sel);
            const auto vp = *rect_of("[data-spectr-length-fraction-viewport]");
            if (r[1] >= vp[1] - 0.5f && r[1] + r[3] <= vp[1] + vp[3] + 0.5f) break;
            pulp::view::deliver_mouse_wheel(*rig.root, centre(vp), 0.0f,
                                            r[1] < vp[1] ? -30.0f : 30.0f, {});
            settle(rig.clock, 4);
        }
        const auto r = *rect_of(sel);
        const auto vp = *rect_of("[data-spectr-length-fraction-viewport]");
        REQUIRE(r[1] >= vp[1] - 0.5f);
        REQUIRE(r[1] + r[3] <= vp[1] + vp[3] + 0.5f);
        // The list's own box is where the press is routed: the row's centre
        // must resolve inside the open overlay.
        auto* overlay = rig.root->interaction().active_overlay;
        REQUIRE(overlay != nullptr);
        CHECK(overlay->overlay_contains(centre(r)));
        host_press(centre(r));
        CHECK(value_of("document.querySelector('[data-spectr-length-fraction]')"
                       ".getAttribute('data-spectr-length-fraction')") == f);
        CHECK(value_of("!!document.querySelector('[data-spectr-length-fraction-options]')") == "false");
    };
    for (const auto& f : fractions) pick(f);

    // Reopened on the last fraction, it is in view (scrolled to the end).
    open_list();
    CHECK(offset() == 17 * 32 - 2 - 254);
    {
        const auto r = *rect_of("[data-spectr-length-fraction-option=\"15/16\"]");
        const auto vp = *rect_of("[data-spectr-length-fraction-viewport]");
        CHECK(r[1] + r[3] <= vp[1] + vp[3] + 0.5f);
    }
    key(pulp::view::KeyCode::escape);
    CHECK(value_of("!!document.querySelector('[data-spectr-length-fraction-options]')") == "false");
    CHECK(value_of("!!document.querySelector('[data-spectr-length-editor]')") == "true");

    // Pick "1/12" (centred on open), then walk the highlight down with the
    // arrows: the highlighted row is always fully in view.
    pick("1/12");
    open_list();
    const int centred = offset();
    CHECK(centred == std::max(0, 3 * 32 - (254 - 30) / 2));
    for (int step = 0; step < 16; ++step) {
        key(pulp::view::KeyCode::down);
        const auto active = value_of(
            "(() => { const n = document.querySelector('[data-spectr-length-fraction-options] "
            "[data-pulp-popup-active=\"true\"]'); return n ? "
            "n.getAttribute('data-spectr-length-fraction-option') : 'none'; })()");
        INFO("step=" << step << " active=" << active);
        REQUIRE(active != "none");
        const auto r = *rect_of("[data-spectr-length-fraction-option=\"" + active + "\"]");
        const auto vp = *rect_of("[data-spectr-length-fraction-viewport]");
        CHECK(r[1] >= vp[1] - 0.5f);
        CHECK(r[1] + r[3] <= vp[1] + vp[3] + 0.5f);
    }
    key(pulp::view::KeyCode::escape);
    storage.require_unchanged();
}

// A LENGTH row, a Fraction row and every Custom editor button take the FIRST
// press, through the plug-in host's own press route, wherever the press lands
// in the row and with the small slip a real click has.
//
// The route is plugin_view_host_mac.mm's: the pointer moves there first, the
// open overlay is consulted, a routed press goes to its target without
// bubbling, and the click fires on mouse-up ONLY when the tree's hit test at
// the release point finds the very view the press went to
// (deliver_mouse_up's same-view rule). Two ways a row missed that, measured
// in this rig before tools/patch_materialized_length_first_press.py (112 of
// these 165 presses missed):
//   * Below ~534pt the hit test only reached the LENGTH menu through the
//     header wrappers' downward reach (hitSlop), which covered the trigger's
//     88pt width and not the 146pt menu. The right part of 15/16 bar, 1, 2, 4
//     and 8 bars and Custom length... took the press and never clicked.
//   * A row's check mark and caption are their own views. A press on the
//     caption released 1-3pt off it (or the reverse) resolved to two
//     different views, so the click was dropped.
// Each variant opens the menu afresh and presses once; a miss leaves the
// menu open, which is what the user saw as "it needed another tap".
TEST_CASE("every LENGTH row, Fraction row and Custom editor button takes the first press",
          "[native-n1][state-parity][freeze-length][first-press]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto value_of = [&](const std::string& expression) {
        auto value = runtime_string(rig, expression, "spectr-native-first-press-value");
        return value.substr(0, value.find('\n'));
    };
    const auto mounted = [&](const std::string& selector) {
        return value_of("!!document.querySelector(" + js_string(selector) + ")") == "true";
    };
    struct Box { float left, top, right, bottom; };
    const auto box_of = [&](const std::string& selector) -> std::optional<Box> {
        const auto text = value_of(
            "(() => { const n = document.querySelector(" + js_string(selector)
            + "); if (!n) return 'none'; const r = n.getBoundingClientRect(); "
              "return [r.left, r.top, r.right, r.bottom].join(','); })()");
        if (text == "none") return std::nullopt;
        std::array<float, 4> v{};
        std::stringstream in(text);
        std::string part;
        for (auto& f : v) { std::getline(in, part, ','); f = std::stof(part); }
        return Box{v[0], v[1], v[2], v[3]};
    };
    // One click, the plug-in host's way. `frames` frames pass between the
    // press and the release (the host presents frames while the button is
    // down); a release away from the press is preceded by a drag there.
    const auto host_click_at = [&](pulp::view::Point down, pulp::view::Point up, int frames) {
        auto& root = *rig.root;
        pulp::view::deliver_hover_move(root, down);
        const auto routed = pulp::view::route_press_to_active_overlay(root, down);
        if (routed.consume_press) { settle(rig.clock, 8); return false; }
        const bool overlay = routed.routing == pulp::view::OverlayPressRouting::routed;
        pulp::view::ViewCapture capture;
        capture.set(overlay ? routed.target : root.hit_test(down));
        if (capture.live_in(root) == nullptr
            || !pulp::view::transfer_input_focus(root, capture.live_in(root)))
            return false;
        if (!pulp::view::deliver_mouse_down(root, capture.live_in(root), down, 0, 1,
                                            /*bubble=*/!overlay))
            return false;
        if (frames > 0) settle(rig.clock, frames);
        if (up.x != down.x || up.y != down.y)
            pulp::view::deliver_mouse_drag(root, capture.live_in(root), up, 0, 1);
        auto* live = capture.live_in(root);
        if (live == nullptr) return false;
        bool clicked = false;
        pulp::view::MouseUpHost host;
        host.fire_click = [&clicked](const std::function<void()>& handler,
                                     const std::string&, std::uint16_t) {
            clicked = static_cast<bool>(handler);
            if (handler) handler();
        };
        pulp::view::deliver_mouse_up(root, live, up, 0, 1, host);
        settle(rig.clock, 8);
        return clicked;
    };
    const auto centre = [](const Box& b) {
        return pulp::view::Point{(b.left + b.right) * 0.5f, (b.top + b.bottom) * 0.5f};
    };
    const std::string menu = "[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]";
    const auto open_menu = [&](int frames_after_open) {
        if (mounted(menu)) return;
        const auto trigger = *box_of("[data-spectr-length-trigger]");
        REQUIRE(host_click_at(centre(trigger), centre(trigger), 0));
        REQUIRE(mounted(menu));
        if (frames_after_open > 0) settle(rig.clock, frames_after_open);
    };
    const auto label = [&] {
        return value_of("document.querySelector('[data-spectr-freeze-length]')"
                        ".getAttribute('data-spectr-freeze-length-label')");
    };
    const auto options = [&] {
        const auto text = value_of(
            "Array.from(document.querySelectorAll(" + js_string(menu + " [data-spectr-length-option]")
            + ")).map((n) => n.getAttribute('data-spectr-length-option')).join('|')");
        std::vector<std::string> out;
        std::stringstream in(text);
        for (std::string part; std::getline(in, part, '|');) out.push_back(part);
        return out;
    };
    // The places on a row a press lands and where the slip takes the release.
    struct Variant { const char* name; int frames_after_open; int frames_held; };
    const auto variants_for = [&](const Box& row, const Box& caption, const Box& check) {
        const float mid = (row.top + row.bottom) * 0.5f;
        std::vector<std::tuple<std::string, pulp::view::Point, pulp::view::Point, int, int>> out;
        out.push_back({"right side, still", {row.right - 10.0f, mid}, {row.right - 10.0f, mid}, 0, 0});
        out.push_back({"right side, slip 2pt", {row.right - 14.0f, mid - 3.0f},
                       {row.right - 12.0f, mid - 2.0f}, 2, 3});
        out.push_back({"caption, released below it", centre(caption),
                       {(caption.left + caption.right) * 0.5f, caption.bottom + 2.0f}, 0, 2});
        out.push_back({"above caption, released on it", {caption.left + 4.0f, caption.top - 1.5f},
                       {caption.left + 4.0f, caption.top + 1.5f}, 1, 0});
        out.push_back({"check, released beside it", centre(check),
                       {check.right + 3.0f, (check.top + check.bottom) * 0.5f}, 0, 1});
        out.push_back({"left padding, slip 1pt", {row.left + 4.0f, row.top + 5.0f},
                       {row.left + 5.0f, row.top + 6.0f}, 30, 0});
        return out;
    };

    int presses = 0;
    std::vector<std::string> missed;
    open_menu(0);
    const auto rows = options();
    REQUIRE(rows.size() == 21);
    for (const auto& option : rows) {
        const std::string row_sel = "[data-spectr-length-option=\"" + option + "\"]";
        open_menu(0);
        const auto row = box_of(row_sel);
        REQUIRE(row.has_value());
        const auto caption = *box_of(row_sel + " span");
        const auto check = *box_of(row_sel + " svg");
        for (const auto& [name, down, up, frames_after_open, frames_held] :
             variants_for(*row, caption, check)) {
            open_menu(frames_after_open);
            // The row must be in view where it was measured: the menu opens
            // at the same offset every time at this size.
            const auto now = *box_of(row_sel);
            REQUIRE(now.top == Catch::Approx(row->top).margin(0.5));
            ++presses;
            const bool clicked = host_click_at(down, up, frames_held);
            const bool took = option == "custom-editor"
                ? mounted("[data-spectr-length-editor]") && !mounted(menu)
                : !mounted(menu) && label() == option;
            if (!clicked || !took) {
                missed.push_back(option + " / " + name + " (press " + std::to_string(down.x) + ","
                                 + std::to_string(down.y) + ")");
            }
            if (mounted("[data-spectr-length-editor]")) {
                pulp::view::View::dismiss_active_overlay(*rig.root);
                settle(rig.clock, 12);
            }
            if (mounted(menu)) {
                pulp::view::View::dismiss_active_overlay(*rig.root);
                settle(rig.clock, 12);
            }
        }
    }

    // The Custom editor: its buttons and every Fraction row, the same way.
    const auto open_editor = [&] {
        if (mounted("[data-spectr-length-editor]")) return;
        open_menu(0);
        const auto custom = *box_of("[data-spectr-length-option=\"custom-editor\"]");
        REQUIRE(host_click_at(centre(custom), centre(custom), 0));
        REQUIRE(mounted("[data-spectr-length-editor]"));
    };
    const auto bars = [&] {
        return value_of("document.querySelector('[data-spectr-length-bars]')"
                        ".getAttribute('data-spectr-length-bars')");
    };
    for (const std::string step : {"up", "down"}) {
        open_editor();
        const auto button = *box_of("[data-spectr-length-bars-step=\"" + step + "\"]");
        const auto glyph = *box_of("[data-spectr-length-bars-step=\"" + step + "\"] svg");
        const auto before = std::stoi(bars());
        ++presses;
        // On the chevron, released just off it.
        const bool clicked = host_click_at(centre(glyph), {glyph.right + 2.0f, centre(glyph).y}, 1);
        const int expected = step == "up" ? before + 1 : std::max(0, before - 1);
        if (!clicked || std::stoi(bars()) != expected) missed.push_back("bars " + step);
        (void)button;
    }
    const auto fraction_list = std::string{"[data-spectr-length-fraction-options]"};
    const auto fraction_of = [&] {
        return value_of("document.querySelector('[data-spectr-length-fraction]')"
                        ".getAttribute('data-spectr-length-fraction')");
    };
    const auto open_fractions = [&] {
        open_editor();
        if (mounted(fraction_list)) return;
        const auto trigger = *box_of("[data-spectr-length-fraction]");
        const auto text = *box_of("[data-spectr-length-fraction] span");
        ++presses;
        // On the trigger's text, released just below it.
        if (!host_click_at(centre(text), {centre(text).x, text.bottom + 2.0f}, 0)
            || !mounted(fraction_list))
            missed.push_back("fraction trigger");
        if (!mounted(fraction_list)) REQUIRE(host_click_at(centre(trigger), centre(trigger), 0));
        REQUIRE(mounted(fraction_list));
    };
    const std::vector<std::string> fractions{
        "0", "1/32", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "1/3",
        "3/8", "1/2", "5/8", "2/3", "3/4", "5/6", "7/8", "15/16"};
    for (const auto& f : fractions) {
        open_fractions();
        const std::string sel = "[data-spectr-length-fraction-option=\"" + f + "\"]";
        // Scroll it into view with the wheel, as a user would.
        for (int i = 0; i < 40; ++i) {
            const auto r = *box_of(sel);
            const auto vp = *box_of("[data-spectr-length-fraction-viewport]");
            if (r.top >= vp.top - 0.5f && r.bottom <= vp.bottom + 0.5f) break;
            pulp::view::deliver_mouse_wheel(*rig.root, centre(vp), 0.0f,
                                            r.top < vp.top ? -30.0f : 30.0f, {});
            settle(rig.clock, 4);
        }
        const auto r = *box_of(sel);
        const auto text = *box_of(sel + " span");
        ++presses;
        // On the caption, released below it; then (if that one took) the
        // right side is covered by the menu rows above.
        const bool clicked = host_click_at(centre(text), {centre(text).x, text.bottom + 2.0f}, 1);
        if (!clicked || fraction_of() != f || mounted(fraction_list))
            missed.push_back("fraction " + f);
        (void)r;
        if (mounted(fraction_list)) {
            pulp::view::View::dismiss_active_overlay(*rig.root);
            settle(rig.clock, 12);
        }
    }
    // CANCEL closes the editor; APPLY applies 2 + 1/8.
    open_editor();
    {
        const auto cancel = *box_of("[data-spectr-length-cancel]");
        ++presses;
        // Pressed on its caption, released 2pt lower.
        const auto c = centre(cancel);
        if (!host_click_at(c, {c.x + 1.0f, c.y + 2.0f}, 1) || mounted("[data-spectr-length-editor]"))
            missed.push_back("cancel");
    }
    open_editor();
    {
        // A draft set through the editor's own Fraction list.
        open_fractions();
        const auto eighth = *box_of("[data-spectr-length-fraction-option=\"1/8\"]");
        REQUIRE(host_click_at(centre(eighth), centre(eighth), 0));
        REQUIRE(fraction_of() == "1/8");
        const auto applied_draft = bars() + " 1/8";
        const auto apply = *box_of("[data-spectr-length-apply]");
        ++presses;
        const auto c = centre(apply);
        const bool clicked = host_click_at(c, {c.x - 2.0f, c.y + 1.0f}, 2);
        if (!clicked || mounted("[data-spectr-length-editor]")) missed.push_back("apply");
        CAPTURE(applied_draft, label());
    }

    CHECK(presses > 150);
    std::string misses;
    for (const auto& m : missed) misses += "\n  missed: " + m;
    INFO(missed.size() << " of " << presses << " presses missed" << misses);
    CHECK(missed.empty());
    if (mounted("[data-spectr-length-editor]") || mounted(menu)) {
        pulp::view::View::dismiss_active_overlay(*rig.root);
        settle(rig.clock, 12);
    }
    storage.require_unchanged();
}

// The preset menu's SAVE CURRENT / MANAGE footer sits below its last factory
// row and inside the menu, at the editor's default size and its authored size.
// It was pinned at the browser capture's offset (runtime.js), 13pt above where
// the 30pt rows now end, and covered the bottom 8pt of AIR LIFT (4k+).
TEST_CASE("the preset menu's footer sits below its last factory row",
          "[native-n1][state-parity][preset-menu][tap-targets]") {
    PatternStoragePoison storage;
    const auto check_footer = [](NativeEditorRig& rig, std::size_t rows_expected) {
        activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
        settle(rig.clock, 24);
        const auto text = runtime_string(rig, R"js((() => {
          const q = (sel) => document.querySelector(sel);
          const menu = '[data-spectr-menu-root="pattern"] ';
          const panel = q(menu + '[data-spectr-menu-options]');
          const rows = Array.from(document.querySelectorAll(menu + '[data-spectr-pattern-menu-id]'));
          const save = q('[data-spectr-save-current]');
          const manage = q('[data-spectr-pattern-manage]');
          const footer = save.parentElement || save._parentElement;
          const trigger = q(menu + '[data-spectr-menu-trigger]');
          const r = (n) => n.getBoundingClientRect();
          const factory = rows.filter((n) => String(n.getAttribute('data-spectr-pattern-menu-id')).startsWith('factory:'));
          const last = r(factory[factory.length - 1]);
          return [rows.length, last.bottom, r(footer).top, r(save).top, r(save).bottom,
                  r(manage).bottom, r(panel).top, r(panel).bottom,
                  factory[factory.length - 1].getAttribute('data-spectr-pattern-menu-id'),
                  r(trigger).top].join(',');
        })())js", "spectr-native-preset-footer");
        std::stringstream in(text.substr(0, text.find('\n')));
        std::vector<std::string> v;
        for (std::string part; std::getline(in, part, ',');) v.push_back(part);
        REQUIRE(v.size() == 10);
        CAPTURE(text);
        CHECK(v[0] == std::to_string(rows_expected));
        // The last factory row is AIR LIFT (4k+).
        CHECK(v[8] == "factory:air");
        const float last_bottom = std::stof(v[1]);
        const float footer_top = std::stof(v[2]);
        const float save_top = std::stof(v[3]);
        const float manage_bottom = std::stof(v[5]);
        const float panel_top = std::stof(v[6]);
        const float panel_bottom = std::stof(v[7]);
        // The footer starts after the last factory row: the 1pt gap and its
        // 4pt margin.
        CHECK(footer_top >= last_bottom);
        CHECK(footer_top == Catch::Approx(last_bottom + 5.0f).margin(0.5));
        CHECK(save_top > last_bottom);
        // Everything it holds is inside the menu, which still ends 2pt above
        // the trigger and grows upward to hold it.
        CHECK(manage_bottom <= panel_bottom);
        CHECK(panel_top < last_bottom);
        CHECK(panel_bottom == Catch::Approx(std::stof(v[9]) - 2.0f).margin(0.5));
        pulp::view::View::dismiss_active_overlay(*rig.root);
        settle(rig.clock, 12);
    };
    for (const auto [width, height] : {std::pair{990, 645}, std::pair{1320, 860}}) {
        INFO("editor " << width << "x" << height);
        NativeEditorRig rig;
        rig.resize(width, height);
        settle(rig.clock, 96);
        // The eight factory rows; the last is AIR LIFT (4k+).
        check_footer(rig, 8);
    }
    // With a user preset the factory rows are still clear of the footer.
    // (The USER heading and rows after them are not: the footer still covers
    // them, as it did before -- see
    // tools/patch_materialized_runtime_pattern_menu_footer.py.)
    NativeEditorRig rig;
    settle(rig.clock, 96);
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-save-current]");
    require_state(rig, "save-dialog");
    activate(rig, "#spectr-save-name", "change",
             R"js({value:'FOOTER CHECK',target:{value:'FOOTER CHECK'},currentTarget:{value:'FOOTER CHECK'}})js");
    activate(rig, "[data-spectr-manager-action=\"save-submit\"]");
    REQUIRE(rig.processor.patterns().user().size() == 1);
    settle(rig.clock, 24);
    check_footer(rig, 9);
    storage.require_unchanged();
}

// THE LENGTH CONTROL, END TO END, THROUGH THE SHIPPING EDITOR.
//
// Header LENGTH [ 1 bar v ]: pick a common length; open Custom length...,
// set 1 + 1/8 and Apply, and the collapsed control reads the resolved
// "1 1/8 bars" while the menu shows it as a checked row under Custom
// length...; Cancel and Escape leave the length alone; a length the model
// refuses cannot be applied; the editor's keys (Up/Down, Tab, Return,
// Escape) work and are stopped there, so Escape does not also clear a band
// selection; and Settings no longer carries a Hold length.
TEST_CASE("the LENGTH control picks, edits and shows a musical length",
          "[native-n1][state-parity][freeze-length]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto attribute = [&](std::string_view name) {
        return runtime_value(rig,
            "document.querySelector('[data-spectr-freeze-length]').getAttribute('"
                + std::string(name) + "')",
            "spectr-native-length-attribute");
    };
    const auto shown = [&] { return attribute("data-spectr-freeze-length-label"); };
    const auto key = [&](pulp::view::KeyCode code, std::uint16_t mods = pulp::view::kModNone) {
        (void)pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(code), mods, true);
        settle(rig.clock, 8);
    };
    const auto mounted = [&](std::string_view selector) {
        return runtime_value(rig, "!!document.querySelector(" + js_string(selector) + ")",
                              "spectr-native-length-mounted") == "true";
    };
    // Bars is typed into: click it, empty it, type each character.
    const auto set_bars = [&](std::string_view text) {
        activate(rig, "[data-spectr-length-bars]");
        key(pulp::view::KeyCode::delete_);
        for (const char c : text) key(static_cast<pulp::view::KeyCode>(c));
    };
    const auto bars_text = [&] {
        return runtime_value(rig,
            "document.querySelector('[data-spectr-length-bars]').getAttribute('data-spectr-length-bars')",
            "spectr-native-length-bars-text");
    };
    const auto preview_valid = [&] {
        return runtime_value(rig,
            "document.querySelector('[data-spectr-length-preview]').getAttribute('data-spectr-length-valid')",
            "spectr-native-length-valid") == "true";
    };
    const auto open_editor = [&] {
        activate(rig, "[data-spectr-length-trigger]");
        REQUIRE(mounted("[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]"));
        activate(rig, "[data-spectr-length-option=\"custom-editor\"]");
        settle(rig.clock, 8);
        REQUIRE(mounted("[data-spectr-length-editor]"));
        REQUIRE_FALSE(mounted("[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]"));
    };

    // SPECTR_LENGTH_SHOTS=<dir> saves the surfaces this walks through
    // (Skia raster, the GPU compositor's reference).
    const auto shot = [&](std::string_view name) {
        const char* dir = std::getenv("SPECTR_LENGTH_SHOTS");
        if (dir == nullptr) return;
        rig.root->layout_children();
        settle(rig.clock, 4);
        const auto path = std::filesystem::path(dir) / (std::string(name) + ".png");
        REQUIRE(pulp::view::render_to_file(*rig.root, 1320, 860, path.string(), 2.0f,
                                           pulp::view::ScreenshotBackend::skia));
    };

    // Default, from the processor.
    REQUIRE(shown() == "1 bar");
    shot("1-collapsed");
    REQUIRE(find_label(*rig.root, "LENGTH") != nullptr);
    REQUIRE(find_label(*rig.root, "1 bar") != nullptr);

    // Settings has no Hold length. Control: a Settings row that must be there.
    activate(rig, "[data-spectr-settings-open]");
    settle(rig.clock, 8);
    REQUIRE(find_label(*rig.root, "Theme") != nullptr);
    CHECK(find_label(*rig.root, "Hold length") == nullptr);
    CHECK_FALSE(mounted("[data-spectr-settings-group=\"freeze\"]"));
    CHECK_FALSE(mounted("[data-spectr-freeze-hold]"));
    activate(rig, "[data-spectr-settings-close]");
    settle(rig.clock, 8);

    // The menu, one flat list: every fraction of a bar, a separator, 1, 2,
    // 4 and 8 bars, a separator, Custom length... -- and no custom row while
    // no custom length is in force.
    activate(rig, "[data-spectr-length-trigger]");
    require_runtime_contract(rig,
        "(() => { const rows = Array.from(document.querySelectorAll("
        "'[data-spectr-menu-root=\"length\"] [data-spectr-menu-options] [data-spectr-length-option]'))"
        ".map((n) => n.getAttribute('data-spectr-length-option'));"
        " return JSON.stringify(rows) === JSON.stringify(['1/32 bar','1/16 bar','1/12 bar','1/8 bar','1/6 bar','3/16 bar','1/4 bar','1/3 bar','3/8 bar','1/2 bar','5/8 bar','2/3 bar','3/4 bar','5/6 bar','7/8 bar','15/16 bar','1 bar','2 bars','4 bars','8 bars','custom-editor'])"
        " && document.querySelectorAll('[data-spectr-length-separator]').length === 2"
        " && !!document.querySelector('[data-spectr-length-separator]')"
        " && document.querySelector('[data-spectr-length-option=\"1 bar\"]').getAttribute('aria-selected') === 'true'; })()",
        "the LENGTH menu does not list exactly the common lengths and Custom length...");
    shot("2-open-dropdown");
    activate(rig, "[data-spectr-length-option=\"2 bars\"]");
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-freeze-length]').getAttribute('data-spectr-freeze-length-label') === '2 bars'",
        "picking 2 bars did not reach the control");
    CHECK_FALSE(mounted("[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]"));
    CHECK(rig.store.get_value(spectr::kParamFreezeLength) == 17.0f);
    CHECK(rig.processor.freeze_length() == spectr::FreezeLength{2, spectr::LengthFraction::zero});

    // Custom length...: 1 + 1/8, Apply. The editor opens on the custom
    // length, 1 bar until one is set, so Bars already reads 1.
    open_editor();
    CHECK(find_label(*rig.root, "Custom") != nullptr);
    CHECK(runtime_value(rig,
              "document.querySelector('[data-spectr-length-bars]').getAttribute('data-spectr-length-bars')",
              "spectr-native-length-bars-initial") == "1");

    activate(rig, "[data-spectr-length-fraction]");
    REQUIRE(mounted("[data-spectr-length-fraction-options]"));
    activate(rig, "[data-spectr-length-fraction-option=\"1/8\"]");
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-length-preview]')?.getAttribute('data-spectr-length-valid') === 'true'",
        "1 + 1/8 never became a valid preview");
    CHECK(find_label(*rig.root, "= 1 1/8 bars") != nullptr);
    shot("3-custom-editor");
    activate(rig, "[data-spectr-length-apply]");
    settle_until_contract(rig, "!document.querySelector('[data-spectr-length-editor]')",
                          "Apply did not close the editor");
    CHECK(shown() == "1 1/8 bars");
    CHECK(find_label(*rig.root, "1 1/8 bars") != nullptr);
    CHECK(find_label(*rig.root, "Custom") == nullptr);
    CHECK(find_label(*rig.root, "Custom…") == nullptr);
    shot("4-after-apply-collapsed");
    CHECK(rig.store.get_value(spectr::kParamFreezeLength)
          == static_cast<float>(spectr::kLengthPresetCustom));
    CHECK(rig.processor.freeze_length() == spectr::FreezeLength{1, spectr::LengthFraction::f1_8});
    // ...and the menu shows it, checked, right above Custom length..., which
    // stays.
    activate(rig, "[data-spectr-length-trigger]");
    require_runtime_contract(rig,
        "(() => { const rows = Array.from(document.querySelectorAll("
        "'[data-spectr-menu-root=\"length\"] [data-spectr-length-option]'));"
        " const ids = rows.map((n) => n.getAttribute('data-spectr-length-option'));"
        " const custom = document.querySelector('[data-spectr-length-option=\"custom\"]');"
        " return JSON.stringify(ids) === JSON.stringify(['1/32 bar','1/16 bar','1/12 bar','1/8 bar','1/6 bar','3/16 bar','1/4 bar','1/3 bar','3/8 bar','1/2 bar','5/8 bar','2/3 bar','3/4 bar','5/6 bar','7/8 bar','15/16 bar','1 bar','2 bars','4 bars','8 bars','custom','custom-editor'])"
        " && custom.getAttribute('aria-selected') === 'true'"
        " && rows.filter((n) => n.getAttribute('aria-selected') === 'true').length === 1; })()",
        "the applied custom length is not the one checked row under Custom length...");
    shot("5-after-apply-menu");
    key(pulp::view::KeyCode::escape);
    CHECK_FALSE(mounted("[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]"));

    // Cancel leaves it alone, and the editor opens on the custom length.
    open_editor();
    CHECK(runtime_value(rig, "document.querySelector('[data-spectr-length-bars]').getAttribute('data-spectr-length-bars')",
                         "spectr-native-length-bars-value") == "1");
    CHECK(runtime_value(rig,
              "document.querySelector('[data-spectr-length-fraction]').getAttribute('data-spectr-length-fraction')",
              "spectr-native-length-fraction-value") == "1/8");
    set_bars("3");
    activate(rig, "[data-spectr-length-cancel]");
    settle(rig.clock, 8);
    CHECK_FALSE(mounted("[data-spectr-length-editor]"));
    CHECK(shown() == "1 1/8 bars");
    CHECK(rig.processor.freeze_length() == spectr::FreezeLength{1, spectr::LengthFraction::f1_8});

    // Lengths the model refuses cannot be applied: past the limit, negative
    // or not a number, empty, and nothing at all.
    open_editor();
    struct Refusal { const char* bars; const char* fraction; const char* message; };
    const Refusal refusals[] = {
        {"129", "0", "Up to 128 bars"},
        {"9999", "1/2", "Up to 128 bars"},
        {"", "0", "Enter a number of bars"},
        {"0", "0", "Choose a length longer than 0"},
    };
    for (const auto& refusal : refusals) {
        INFO("bars '" << refusal.bars << "' + " << refusal.fraction);
        set_bars(refusal.bars);
        activate(rig, "[data-spectr-length-fraction]");
        activate(rig, std::string("[data-spectr-length-fraction-option=\"") + refusal.fraction + "\"]");
        settle_until_contract(rig,
            "document.querySelector('[data-spectr-length-preview]')?.getAttribute('data-spectr-length-valid') === 'false'",
            "a refused length read as valid");
        settle(rig.clock, 8);
        CHECK(find_label(*rig.root, refusal.message) != nullptr);
        CHECK_FALSE(preview_valid());
        activate(rig, "[data-spectr-length-apply]");
        settle(rig.clock, 8);
        CHECK(mounted("[data-spectr-length-editor]"));
        CHECK(rig.processor.freeze_length() == spectr::FreezeLength{1, spectr::LengthFraction::f1_8});
    }
    // Only digits can be typed: a sign, a point, a letter never reach it.
    set_bars("-1.5x");
    CHECK(bars_text() == "15");
    key(pulp::view::KeyCode::backspace);
    CHECK(bars_text() == "1");
    // The positive control for the refusals: the same field, a valid value.
    set_bars("2");
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-length-preview]')?.getAttribute('data-spectr-length-valid') === 'true'",
        "a valid length after the refusals did not read as valid");
    // The loop memory's limit is mentioned only when it bites: 2 bars at
    // the rig's 120 BPM (4 s) says nothing, 40 bars (80 s, past 60 s) does.
    CHECK_FALSE(mounted("[data-spectr-length-cap-note]"));
    set_bars("40");
    settle_until_contract(rig, "!!document.querySelector('[data-spectr-length-cap-note]')",
                          "a length past the loop memory did not say so");
    CHECK(preview_valid());
    shot("7-editor-cap-note");
    activate(rig, "[data-spectr-length-cancel]");
    settle(rig.clock, 8);

    // THE KEYS. A band selection stands, so an Escape that leaked through
    // the editor would clear it.
#if defined(__APPLE__)
    key(static_cast<pulp::view::KeyCode>('a'), pulp::view::kModCmd);
#else
    key(static_cast<pulp::view::KeyCode>('a'), pulp::view::kModCtrl);
#endif
    const auto selected = [&] {
        return runtime_value(rig,
            "globalThis.__spectrTestHooks.renderState().selection.length",
            "spectr-native-length-selection");
    };
    REQUIRE(selected() != "0");
    const auto before = selected();
    // The menu's Escape closes the menu and nothing else.
    activate(rig, "[data-spectr-length-trigger]");
    REQUIRE(mounted("[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]"));
    key(pulp::view::KeyCode::escape);
    CHECK_FALSE(mounted("[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]"));
    CHECK(selected() == before);
    // The editor's Escape cancels it and nothing else.
    open_editor();
    key(pulp::view::KeyCode::escape);
    CHECK_FALSE(mounted("[data-spectr-length-editor]"));
    CHECK(selected() == before);
    CHECK(shown() == "1 1/8 bars");

    // Up/Down step Bars (focused on open); Tab moves to Fraction, where they
    // step the fraction; Return applies.
    open_editor();
    key(pulp::view::KeyCode::up);
    key(pulp::view::KeyCode::up);
    key(pulp::view::KeyCode::down);
    CHECK(runtime_value(rig, "document.querySelector('[data-spectr-length-bars]').getAttribute('data-spectr-length-bars')",
                         "spectr-native-length-bars-stepped") == "2");
    key(pulp::view::KeyCode::tab);
    key(pulp::view::KeyCode::up);
    CHECK(runtime_value(rig,
              "document.querySelector('[data-spectr-length-fraction]').getAttribute('data-spectr-length-fraction')",
              "spectr-native-length-fraction-stepped") == "1/6");
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-length-preview]')?.getAttribute('data-spectr-length-valid') === 'true'",
        "2 + 1/6 never became a valid preview");
    CHECK(find_label(*rig.root, "= 2 1/6 bars") != nullptr);
    shot("6-editor-keyboard");
    key(pulp::view::KeyCode::enter);
    settle_until_contract(rig, "!document.querySelector('[data-spectr-length-editor]')",
                          "Return did not apply and close the editor");
    CHECK(shown() == "2 1/6 bars");
    CHECK(rig.processor.freeze_length() == spectr::FreezeLength{2, spectr::LengthFraction::f1_6});
    CHECK(selected() == before);

    // Tab to CANCEL and Return there cancels; Shift+Tab walks back.
    open_editor();
    key(pulp::view::KeyCode::up);
    key(pulp::view::KeyCode::tab);
    key(pulp::view::KeyCode::tab);
    key(pulp::view::KeyCode::tab);
    key(pulp::view::KeyCode::tab, pulp::view::kModShift); // APPLY -> CANCEL
    key(pulp::view::KeyCode::enter);
    CHECK_FALSE(mounted("[data-spectr-length-editor]"));
    CHECK(rig.processor.freeze_length() == spectr::FreezeLength{2, spectr::LengthFraction::f1_6});

    // The host moves the parameter: the control follows.
    rig.store.set_value(spectr::kParamFreezeLength, 18.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-freeze-length]').getAttribute('data-spectr-freeze-length-label') === '4 bars'",
        "host automation of Freeze Length did not reach the control");
    storage.require_unchanged();
}



// The LENGTH menu is one flat list: the sixteen fractions of a bar, a
// separator, 1, 2, 4 and 8 bars, a separator, the checked compound length
// when one is in force, and Custom length.... A fraction row is exactly
// that fraction of one bar, on the host parameter. The menu is as tall as
// its rows up to the room below the trigger, and scrolls past that: it
// opens with the checked row in view, the wheel and the keys move it, and
// every row takes a press once in view.
TEST_CASE("the LENGTH menu lists every fraction, then the bars, and scrolls when it must",
          "[native-n1][state-parity][freeze-length][length-menu]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto value = [&](const std::string& expression) {
        return runtime_value(rig, expression, "spectr-native-length-menu");
    };
    const auto number = [&](const std::string& expression) {
        return std::stof(value("String(" + expression + ")"));
    };
    const auto key = [&](pulp::view::KeyCode code) {
        (void)pulp::view::WidgetBridge::dispatch_key_for_root(
            *rig.root, static_cast<int>(code), pulp::view::kModNone, true);
        settle(rig.clock, 8);
    };
    const std::string menu = "[data-spectr-menu-root=\"length\"] [data-spectr-menu-options]";
    const auto open = [&] { return value("!!document.querySelector(" + js_string(menu) + ")") == "true"; };
    const auto rect = [&](const std::string& selector) {
        const auto text = value("(() => { const n = document.querySelector(" + js_string(selector)
            + "); if (!n) return 'none'; const r = n.getBoundingClientRect();"
              " return [r.left, r.top, r.width, r.height].join(','); })()");
        REQUIRE(text != "none");
        std::array<float, 4> r{};
        std::stringstream in(text);
        std::string part;
        for (auto& v : r) { std::getline(in, part, ','); v = std::stof(part); }
        return r;
    };
    const auto row = [](const std::string& option) {
        return "[data-spectr-length-option=\"" + option + "\"]";
    };
    // A row is in view when it lies wholly inside the menu's viewport.
    const auto in_view = [&](const std::string& option) {
        const auto r = rect(row(option));
        const auto vp = rect("[data-spectr-length-viewport-box]");
        return r[1] >= vp[1] - 0.5f && r[1] + r[3] <= vp[1] + vp[3] + 0.5f;
    };
    // The host's press at a row's painted centre: the overlay first, the
    // press to its routed target, the click on mouse-up.
    const auto press_row = [&](const std::string& option) {
        const auto r = rect(row(option));
        const pulp::view::Point pt{r[0] + r[2] * 0.5f, r[1] + r[3] * 0.5f};
        auto& root = *rig.root;
        REQUIRE(root.interaction().active_overlay != nullptr);
        CHECK(root.interaction().active_overlay->overlay_contains(pt));
        pulp::view::deliver_hover_move(root, pt);
        const auto routed = pulp::view::route_press_to_active_overlay(root, pt);
        REQUIRE(routed.routing == pulp::view::OverlayPressRouting::routed);
        // The click needs the tree's own hit test to find the pressed row
        // again at mouse-up; a row it cannot reach is pressed but never
        // clicked.
        {
            const View* hit = root.hit_test(pt);
            bool same = false;
            for (const View* n = routed.target; n && !same; n = n->parent()) same = n == hit;
            for (const View* n = hit; n && !same; n = n->parent()) same = n == routed.target;
            CHECK(same);
        }
        pulp::view::deliver_mouse_down(root, routed.target, pt, 0, 1, false);
        pulp::view::MouseUpHost up;
        up.fire_click = [](const std::function<void()>& handler, const std::string&, std::uint16_t) {
            if (handler) handler();
        };
        pulp::view::deliver_mouse_up(root, routed.target, pt, 0, 1, up);
        settle(rig.clock, 8);
    };
    const auto label = [&] {
        return value("document.querySelector('[data-spectr-freeze-length]')"
                     ".getAttribute('data-spectr-freeze-length-label')");
    };

    // The rows, in order, and where the two separators fall.
    activate(rig, "[data-spectr-length-trigger]");
    REQUIRE(open());
    CHECK(value("JSON.stringify(Array.from(document.querySelectorAll(" + js_string(menu + " [data-spectr-length-option]")
                + ")).concat(Array.from(document.querySelectorAll(" + js_string(menu + " [data-spectr-length-separator]")
                + "))).map((n) => [n.getBoundingClientRect().top, n.getAttribute('data-spectr-length-option')"
                  " || ('-' + n.getAttribute('data-spectr-length-separator'))]).sort((a, b) => a[0] - b[0])"
                  ".map((p) => p[1]))")
          == "[\"1/32 bar\",\"1/16 bar\",\"1/12 bar\",\"1/8 bar\",\"1/6 bar\",\"3/16 bar\",\"1/4 bar\","
             "\"1/3 bar\",\"3/8 bar\",\"1/2 bar\",\"5/8 bar\",\"2/3 bar\",\"3/4 bar\",\"5/6 bar\","
             "\"7/8 bar\",\"15/16 bar\",\"-bars\",\"1 bar\",\"2 bars\",\"4 bars\",\"8 bars\","
             "\"-custom\",\"custom-editor\"]");
    // At the authored size every row fits: no scrolling, no scrollbar.
    CHECK(value("document.querySelector(" + js_string(menu) + ").getAttribute('data-spectr-length-offset')") == "0");
    CHECK(value("!!document.querySelector('[data-spectr-length-scrollbar]')") == "false");
    CHECK(in_view("1/32 bar"));
    CHECK(in_view("custom-editor"));

    // Each fraction row is bars 0 + exactly that fraction, a press away.
    const std::vector<std::pair<std::string, spectr::LengthFraction>> fractions{
        {"1/32 bar", spectr::LengthFraction::f1_32}, {"1/16 bar", spectr::LengthFraction::f1_16},
        {"1/12 bar", spectr::LengthFraction::f1_12}, {"1/8 bar", spectr::LengthFraction::f1_8},
        {"1/6 bar", spectr::LengthFraction::f1_6},   {"3/16 bar", spectr::LengthFraction::f3_16},
        {"1/4 bar", spectr::LengthFraction::f1_4},   {"1/3 bar", spectr::LengthFraction::f1_3},
        {"3/8 bar", spectr::LengthFraction::f3_8},   {"1/2 bar", spectr::LengthFraction::f1_2},
        {"5/8 bar", spectr::LengthFraction::f5_8},   {"2/3 bar", spectr::LengthFraction::f2_3},
        {"3/4 bar", spectr::LengthFraction::f3_4},   {"5/6 bar", spectr::LengthFraction::f5_6},
        {"7/8 bar", spectr::LengthFraction::f7_8},   {"15/16 bar", spectr::LengthFraction::f15_16}};
    for (std::size_t i = 0; i < fractions.size(); ++i) {
        INFO("row " << fractions[i].first);
        if (!open()) activate(rig, "[data-spectr-length-trigger]");
        press_row(fractions[i].first);
        CHECK_FALSE(open());
        CHECK(label() == fractions[i].first);
        CHECK(rig.store.get_value(spectr::kParamFreezeLength) == static_cast<float>(i));
        CHECK(rig.processor.freeze_length() == spectr::FreezeLength{0, fractions[i].second});
    }
    for (const auto& [option, param] : std::vector<std::pair<std::string, float>>{
             {"1 bar", 16.0f}, {"2 bars", 17.0f}, {"4 bars", 18.0f}, {"8 bars", 19.0f}}) {
        INFO("row " << option);
        activate(rig, "[data-spectr-length-trigger]");
        press_row(option);
        CHECK(label() == option);
        CHECK(rig.store.get_value(spectr::kParamFreezeLength) == param);
    }

    // The tallest it gets -- a compound length in force, past the loop
    // memory, so its checked row and the note are there too -- is more than
    // the room below the trigger: it stops above the bottom rail and
    // scrolls.
    REQUIRE(rig.processor.set_freeze_length_from_editor({64, spectr::LengthFraction::f7_8}));
    (void)rig.processor.apply_surface_params(false);
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-freeze-length]').getAttribute('data-spectr-freeze-length-label') === '64 7/8 bars'",
        "the custom length did not reach the control");
    activate(rig, "[data-spectr-length-trigger]");
    REQUIRE(open());
    const auto panel = rect(menu);
    const auto rail = rect("[data-spectr-bottom-rail]");
    CAPTURE(panel[1], panel[3], rail[1]);
    CHECK(panel[1] + panel[3] < rail[1]);
    REQUIRE(value("!!document.querySelector('[data-spectr-length-scrollbar]')") == "true");
    REQUIRE(value("!!document.querySelector(" + js_string(menu) + " + ' [data-spectr-length-cap-note]')") == "true");
    // It opened on the checked row, in view, the compound length right
    // above Custom length....
    CHECK(value("document.querySelector('[data-spectr-length-option=\"custom\"]').getAttribute('aria-selected')") == "true");
    CHECK(in_view("custom"));
    CHECK(number("document.querySelector('[data-spectr-length-option=\"custom-editor\"]').getBoundingClientRect().top"
                 " - document.querySelector('[data-spectr-length-option=\"custom\"]').getBoundingClientRect().top")
          == Catch::Approx(32.0f));
    const auto offset = [&] {
        return std::stoi(value("document.querySelector(" + js_string(menu) + ").getAttribute('data-spectr-length-offset')"));
    };
    CHECK(offset() > 0);
    CHECK_FALSE(in_view("1/32 bar"));
    // The wheel scrolls it (Pulp's delta is positive downward), clamped.
    const auto vp = rect("[data-spectr-length-viewport-box]");
    const pulp::view::Point over{vp[0] + vp[2] * 0.5f, vp[1] + vp[3] * 0.5f};
    for (int i = 0; i < 40; ++i) pulp::view::deliver_mouse_wheel(*rig.root, over, 0.0f, -40.0f, {});
    settle(rig.clock, 8);
    CHECK(offset() == 0);
    CHECK(in_view("1/32 bar"));
    // ...and a row scrolled into view by the wheel takes a press.
    press_row("1/32 bar");
    CHECK(label() == "1/32 bar");
    // The host applies parameters every block; the rig applies them here.
    (void)rig.processor.apply_surface_params(false);
    // The keys keep the highlight in view: End, Home, PageDown, PageUp. The
    // host selects Custom again; the custom length was kept.
    rig.store.set_value(spectr::kParamFreezeLength, static_cast<float>(spectr::kLengthPresetCustom));
    (void)rig.processor.apply_surface_params(false);
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-freeze-length]').getAttribute('data-spectr-freeze-length-label') === '64 7/8 bars'",
        "the custom length did not come back");
    rig.bridge().load_script("document.querySelector('[data-spectr-length-trigger]').focus()",
                             "spectr-native-length-focus");
    key(pulp::view::KeyCode::down);
    REQUIRE(open());
    const auto highlighted = [&] {
        return value("(() => { const all = Array.from(document.querySelectorAll("
                     "'[data-pulp-popup-active=\"true\"]')); return all.length === 1 ? "
                     "all[0].getAttribute('data-spectr-length-option') : 'count=' + all.length; })()");
    };
    CHECK(highlighted() == "custom");
    key(pulp::view::KeyCode::home);
    CHECK(highlighted() == "1/32 bar");
    CHECK(in_view("1/32 bar"));
    key(pulp::view::KeyCode::end_);
    CHECK(highlighted() == "custom-editor");
    CHECK(in_view("custom-editor"));
    key(pulp::view::KeyCode::home);
    key(pulp::view::KeyCode::page_down);
    const auto paged = highlighted();
    CHECK(paged != "1/32 bar");
    CHECK(in_view(paged));
    key(pulp::view::KeyCode::page_up);
    CHECK(highlighted() == "1/32 bar");
    // Every row, walked by the arrow, is in view when it is highlighted.
    for (int step = 0; step < 22; ++step) {
        key(pulp::view::KeyCode::down);
        const auto at = highlighted();
        INFO("step " << step << " on " << at);
        REQUIRE(at.rfind("count=", 0) != 0);
        CHECK(in_view(at));
    }
    key(pulp::view::KeyCode::escape);
    CHECK_FALSE(open());
    storage.require_unchanged();
}

// ── The EDIT MODE rows lay out the same with or without their key badge ─────
//
// Each row is an icon, then a header line (title, "· tagline", and the key
// badge at the right while the keys are live), then the description below,
// spanning the text column. Hiding the badge in a plug-in used to reflow the
// whole row: the title and tagline collapsed into a narrow middle column,
// stacked and truncated, with the description beside them instead of below.
// The geometry is measured in every context and must agree across them.

TEST_CASE("EDIT MODE rows lay out the same with or without the key badge",
          "[native-n1][state-parity][keyboard][hints][layout]") {
    PatternStoragePoison storage;
    const auto rows = [](bool standalone) {
        EditorContextScope context(standalone);
        NativeEditorRig rig;
        require_home(rig);
        std::vector<std::string> out;
        for (const char* mode : {"sculpt", "level", "boost", "flare", "glide"}) {
            // One row per evaluation keeps each report on a single line.
            activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
            out.push_back(runtime_value(
                rig,
                std::string(
                    "(() => {"
                    "  const rect = (n) => { const r = n.getBoundingClientRect();"
                    "    return { l: r.left, t: r.top, r: r.left + r.width,"
                    "             b: r.top + r.height, w: r.width, h: r.height }; };"
                    "  const round = (r) => [r.l, r.t, r.w, r.h].map(v => Math.round(v)).join(',');"
                    "  const row = document.querySelector('[data-spectr-edit-mode=\"") + mode +
                    "\"]');"
                    "  if (!row) return 'no row';"
                    "  const text = row.children[1];"
                    "  const header = text && text.children[0];"
                    "  const desc = text && text.children[1];"
                    "  const title = header && header.children[0];"
                    "  const tagline = header && header.children[1];"
                    "  if (!title || !tagline || !desc) return 'missing parts';"
                    "  const t = rect(title), g = rect(tagline), d = rect(desc), h = rect(header);"
                    "  const rowRect = rect(row);"
                    "  const faults = [];"
                    "  if (Math.abs(t.t - g.t) > 2) faults.push('tagline not on the title line');"
                    "  if (g.l < t.r - 0.5) faults.push('tagline overlaps the title');"
                    "  if (t.h > 20 || g.h > 20) faults.push('title/tagline wrapped');"
                    "  if (Math.max(t.b, g.b, h.b) > d.t + 0.5) faults.push('header overlaps the description');"
                    "  if (Math.abs(t.l - d.l) > 1) faults.push('title not left-aligned with the description');"
                    "  if (d.w < 180) faults.push('description does not span the row');"
                    "  if (t.t - rowRect.t > 16) faults.push('title not at the top of the row');"
                    "  return (faults.length ? faults.join('; ') : 'ok')"
                    "    + ' | title=' + round(t) + ' tagline=' + round(g) + ' desc=' + round(d);"
                    "})()",
                "spectr-edit-mode-row-geometry"));
            CHECK(press_key(rig, pulp::view::KeyCode::escape));
        }
        return out;
    };
    const auto hosted = rows(/*standalone=*/false);
    const auto standalone = rows(/*standalone=*/true);
    REQUIRE(hosted.size() == 5);
    REQUIRE(standalone.size() == 5);
    for (std::size_t i = 0; i < hosted.size(); ++i) {
        INFO("row " << i << "\n  plug-in:    " << hosted[i]
                    << "\n  standalone: " << standalone[i]);
        CHECK(hosted[i].rfind("ok |", 0) == 0);
        CHECK(standalone[i].rfind("ok |", 0) == 0);
        // The badge is the only difference between the contexts.
        CHECK(hosted[i] == standalone[i]);
    }
    storage.require_unchanged();
}

// ── A modal dialog makes the plot behind it inert ───────────────────────────
//
// With About or Settings open, a two-finger scroll over the band plot used to
// zoom the viewport underneath the dialog. The wheel is delivered through the
// host's own wheel verb at a point on the plot that the dialog's panel does
// not cover, so the only thing standing between the gesture and the plot is
// the dialog being open.

namespace {

std::string plot_view(NativeEditorRig& rig) {
    return runtime_value(
        rig, "JSON.stringify(__spectrTestHooks.renderState().view)",
        "spectr-modal-plot-view");
}

void wheel_over_plot(NativeEditorRig& rig) {
    // Low on the plot's left edge, outside every dialog panel Spectr
    // centres. The About scrim paints over this point but lies outside its
    // ancestors' bounds here, so the tree hit test reaches the plot through
    // it -- the geometry the reported scroll went through.
    const pulp::view::Point over_plot{140.0f, 620.0f};
    for (int i = 0; i < 4; ++i)
        pulp::view::deliver_mouse_wheel(*rig.root, over_plot, 0.0f, -30.0f, {});
    settle(rig.clock, 12);
}

}  // namespace

TEST_CASE("a scroll over the plot behind About or Settings changes nothing",
          "[native-n1][state-parity][modal][wheel]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

    // Control: with nothing open the same gesture zooms, so an unchanged view
    // below is the dialog's doing, not a wheel that never arrived.
    const auto at_rest = plot_view(rig);
    wheel_over_plot(rig);
    const auto zoomed = plot_view(rig);
    REQUIRE(zoomed != at_rest);

    const auto exercise = [&](std::string_view name, auto&& open,
                              std::string_view open_selector, auto&& close) {
        INFO("dialog=" << name);
        open();
        require_runtime_contract(
            rig, "document.querySelector(" + js_string(open_selector) + ")",
            std::string{name} + " did not open");
        const auto before = plot_view(rig);
        wheel_over_plot(rig);
        CHECK(plot_view(rig) == before);
        CHECK(runtime_value(rig, "document.querySelector(" + js_string(open_selector)
                                     + ") ? 'open' : 'closed'",
                            "spectr-modal-still-open") == "open");
        close();
        const auto closed = plot_view(rig);
        wheel_over_plot(rig);
        CHECK(plot_view(rig) != closed);
    };

    exercise("about", [&] {
        activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
        activate(rig, "[data-spectr-help-learn-more]");
    }, "[data-spectr-help-guide-scrim]", [&] {
        CHECK(press_key(rig, pulp::view::KeyCode::escape));
        require_runtime_contract(
            rig, "!document.querySelector('[data-spectr-help-guide-scrim]')",
            "About did not close");
    });

    exercise("settings", [&] {
        activate(rig, "[data-spectr-settings-open]");
    }, "[data-spectr-settings-panel][data-spectr-settings-live=\"true\"]", [&] {
        activate(rig, "[data-spectr-settings-close]");
    });

    // A press on the About backdrop -- the start of any drag there -- closes
    // About and edits no band: the SDK spends a press outside an open overlay
    // on its dismissal. simulate_click is the host press path (overlay
    // routing first); simulate_drag is not, so it cannot stand in here.
    activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-help-learn-more]");
    require_runtime_contract(rig, "document.querySelector('[data-spectr-help-guide-scrim]')",
                             "About did not open for the drag case");
    const auto bands_before = rig.processor.field().bands;
    const auto view_before = plot_view(rig);
    rig.root->simulate_click({140.0f, 620.0f});
    settle(rig.clock, 12);
    for (std::size_t i = 0; i < bands_before.size(); ++i) {
        CHECK(rig.processor.field().bands[i].gain_db
              == Catch::Approx(bands_before[i].gain_db));
        CHECK(rig.processor.field().bands[i].muted == bands_before[i].muted);
    }
    CHECK(plot_view(rig) == view_before);
    require_runtime_contract(rig, "!document.querySelector('[data-spectr-help-guide-scrim]')",
                             "a press on the About backdrop did not close it");
    // Closed, the plot takes a press again: the same point now edits.
    rig.root->simulate_click({140.0f, 620.0f});
    settle(rig.clock, 12);
    bool edited = false;
    for (std::size_t i = 0; i < bands_before.size(); ++i)
        edited = edited || rig.processor.field().bands[i].muted != bands_before[i].muted
            || rig.processor.field().bands[i].gain_db != bands_before[i].gain_db;
    CHECK(edited);
    storage.require_unchanged();
}

// ── A freeze press records in host automation ────────────────────────────────
//
// A host recording in Touch, Latch or Write keys on the edit gesture: begin,
// value, end. The recorder below is what a format adapter sees -- the store's
// gesture callbacks and its inline value listener, in order, for Freeze only.
// Control: the old route, `param_set`, moves the value with no gesture, and
// the recorder must show exactly that, so a bracket below is the product's.

namespace {

struct FreezeEditRecorder {
    std::vector<std::string> events;
    pulp::state::ListenerToken token;
    explicit FreezeEditRecorder(pulp::state::StateStore& store) {
        store.set_gesture_callbacks(
            [this](pulp::state::ParamID id) {
                if (id == spectr::kParamFreeze) events.emplace_back("begin");
            },
            [this](pulp::state::ParamID id) {
                if (id == spectr::kParamFreeze) events.emplace_back("end");
            });
        token = store.add_audio_listener([this](pulp::state::ParamID id, float value) {
            if (id == spectr::kParamFreeze)
                events.emplace_back(value >= 0.5f ? "set 1" : "set 0");
        });
    }
    std::string take() {
        std::string out;
        for (const auto& e : events) out += (out.empty() ? "" : ", ") + e;
        events.clear();
        return out;
    }
};

}  // namespace

TEST_CASE("a freeze press goes to the host as one edit gesture",
          "[native-n1][state-parity][freeze-toggle][automation]") {
    PatternStoragePoison storage;
    for (const bool standalone : {false, true}) {
        INFO((standalone ? "standalone" : "plug-in"));
        EditorContextScope context(standalone);
        NativeEditorRig rig;
        require_home(rig);
        FreezeEditRecorder recorder(rig.store);

        activate(rig, "[data-spectr-freeze-toggle]");
        CHECK(recorder.take() == "begin, set 1, end");
        activate(rig, "[data-spectr-freeze-toggle]");
        CHECK(recorder.take() == "begin, set 0, end");

        // The chord, live in every context by default.
        REQUIRE(press_key(rig, pulp::view::KeyCode::f, kFreezeChord));
        CHECK(recorder.take() == "begin, set 1, end");
        REQUIRE(press_key(rig, pulp::view::KeyCode::f, kFreezeChord));
        CHECK(recorder.take() == "begin, set 0, end");

        // Q, where plain keys are live (the standalone, or a plug-in whose
        // user turned them on).
        if (!standalone) rig.processor.set_keyboard_shortcuts_in_daw(true);
        rig.bridge().load_script("globalThis.__spectrApplyKeyboardPolicy("
                                 "{ shortcuts_in_daw: true });",
                                 "spectr-freeze-gesture-keys-live");
        settle(rig.clock, 8);
        REQUIRE(press_key(rig, key_of('q')));
        CHECK(recorder.take() == "begin, set 1, end");
        REQUIRE(press_key(rig, key_of('q')));
        CHECK(recorder.take() == "begin, set 0, end");

        // Control: a bare value write is visible to the recorder as exactly
        // that -- a value with no gesture around it.
        rig.bridge().load_script(
            "window.pulp.postMessage('param_set', { id: 3, value: 1 }, 'test');",
            "spectr-freeze-gesture-control");
        settle(rig.clock, 8);
        CHECK(recorder.take() == "set 1");
    }
    storage.require_unchanged();
}

// ── The modulation controls record in host automation, and play back ────────
//
// The whole loop a user performs with the LFOs as an instrument: change them
// in the editor while a host records, then play the lane back and watch the
// editor follow. The recorder is what a format adapter sees -- the store's
// gesture callbacks and its inline value listener, in order -- for the
// modulation lanes only. A host in Touch, Latch or Write records the begin/end
// bracket; a value with no bracket around it is an edit it cannot record.

namespace {

struct ModulationEditRecorder {
    std::vector<std::string> events;
    pulp::state::ListenerToken token;
    static bool watched(pulp::state::ParamID id) {
        return (id >= spectr::kParamLfoEnabled && id <= spectr::kParamLfoTarget)
            || (id >= spectr::kParamLfo2Enabled && id <= spectr::kParamLfo2Depth)
            || id == spectr::kOutputTrim || id == spectr::kParamMorph
            || id == spectr::kMix || id == spectr::kParamIntensity
            || id == spectr::kParamAutoGain;
    }
    explicit ModulationEditRecorder(pulp::state::StateStore& store) {
        store.set_gesture_callbacks(
            [this](pulp::state::ParamID id) {
                if (watched(id)) events.push_back("begin " + std::to_string(id));
            },
            [this](pulp::state::ParamID id) {
                if (watched(id)) events.push_back("end " + std::to_string(id));
            });
        token = store.add_audio_listener([this](pulp::state::ParamID id, float value) {
            if (!watched(id)) return;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "set %u=%g", static_cast<unsigned>(id),
                          static_cast<double>(value));
            events.emplace_back(buf);
        });
    }
    std::string take() {
        std::string out;
        for (const auto& e : events) out += (out.empty() ? "" : ", ") + e;
        events.clear();
        return out;
    }
};

void open_band_menu(NativeEditorRig& rig) {
    activate(rig, "[data-spectr-filter-surface]", "contextmenu",
             R"js({clientX:660,clientY:430,offsetX:660,offsetY:430,button:2})js");
    require_runtime_contract(rig, "document.querySelector('[data-spectr-band-context-menu]')",
                             "the band menu did not open");
    // The LFO rows live in its Modulation submenu.
    activate(rig, "[data-spectr-band-action=\"modulation-toggle\"]");
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-band-action=\"lfo1-enable\"]')",
        "the Modulation submenu did not open");
}

// Press a band-menu slider track at `ratio` of its width and release it.
void press_menu_slider(NativeEditorRig& rig, std::string_view action, double ratio) {
    const auto track = std::string("[data-spectr-menu-slider-track=\"")
        + std::string(action) + "\"]";
    activate(rig, track, "pointerdown", slider_press_at(ratio, track));
    activate(rig, track, "pointerup", slider_press_at(ratio, track));
}

}  // namespace

TEST_CASE("every LFO edit in the band menu records as a host gesture",
          "[native-n1][state-parity][modulation][automation]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    open_band_menu(rig);
    ModulationEditRecorder recorder(rig.store);

    activate(rig, "[data-spectr-band-action=\"lfo1-enable\"]");
    CHECK(recorder.take() == "begin 4000, set 4000=1, end 4000");

    activate(rig, "[data-spectr-band-action=\"lfo1-shape\"] [data-spectr-shape-option=\"2\"]",
             "pointerdown");
    CHECK(recorder.take() == "begin 4001, set 4001=2, end 4001");

    // Rate moves over the musical rates (0.25 .. 16 beats, 7 stops): the far
    // end of the track is 16 beats.
    press_menu_slider(rig, "lfo1-rate", 1.0);
    CHECK(recorder.take() == "begin 4002, set 4002=16, end 4002");

    press_menu_slider(rig, "lfo1-depth", 0.25);
    CHECK(recorder.take() == "begin 4003, set 4003=0.25, end 4003");

    activate(rig, "[data-spectr-band-action=\"modulation-target-morph\"]");
    CHECK(recorder.take() == "begin 4004, set 4004=3, end 4004");

    activate(rig, "[data-spectr-band-action=\"lfo2-enable\"]");
    CHECK(recorder.take() == "begin 4010, set 4010=1, end 4010");
    activate(rig, "[data-spectr-modulation-source-action=\"2\"]");
    activate(rig, "[data-spectr-band-action=\"lfo2-shape\"] [data-spectr-shape-option=\"3\"]",
             "pointerdown");
    CHECK(recorder.take() == "begin 4011, set 4011=3, end 4011");
    press_menu_slider(rig, "lfo2-rate", 0.0);
    CHECK(recorder.take() == "begin 4012, set 4012=0.25, end 4012");
    press_menu_slider(rig, "lfo2-depth", 1.0);
    CHECK(recorder.take() == "begin 4013, set 4013=1, end 4013");

    // Off again: the toggle records its falling edge too.
    activate(rig, "[data-spectr-band-action=\"lfo1-enable\"]");
    CHECK(recorder.take() == "begin 4000, set 4000=0, end 4000");
    CHECK(rig.store.open_gesture_count() == 0);
    storage.require_unchanged();
}

TEST_CASE("every LFO edit in Settings records as a host gesture",
          "[native-n1][state-parity][modulation][automation]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    activate(rig, "[data-spectr-settings-open]");
    ModulationEditRecorder recorder(rig.store);

    activate(rig, "[data-spectr-settings-modulation] [data-spectr-setting-toggle]");
    CHECK(recorder.take() == "begin 4000, set 4000=1, end 4000");
    settle(rig.clock, 8);
    // Settings' LFO 1 rows, in document order: shape chips, then rate and
    // depth sliders.
    activate(rig, "[data-spectr-settings-modulation] [data-spectr-setting-option=\"3\"]");
    CHECK(recorder.take() == "begin 4001, set 4001=3, end 4001");
    const auto rate = std::string("[data-spectr-settings-modulation] [data-spectr-setting-slider]");
    activate(rig, rate, "pointerdown", slider_press_at(1.0, rate));
    activate(rig, rate, "pointerup", slider_press_at(1.0, rate));
    CHECK(recorder.take() == "begin 4002, set 4002=16, end 4002");
    CHECK(rig.store.open_gesture_count() == 0);
    storage.require_unchanged();
}

TEST_CASE("host playback of the LFO lanes moves the band menu, even after an edit",
          "[native-n1][state-parity][modulation][automation]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    open_band_menu(rig);

    // The user sets depth to 37% by hand, which is a value a float cannot hold
    // exactly. The processor reports it back as 0.3700000047683716.
    press_menu_slider(rig, "lfo1-depth", 0.37);
    REQUIRE(rig.store.get_value(spectr::kParamLfoDepth) == Catch::Approx(0.37f));
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 8);
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-band-action=\"lfo1-depth\"]')"
        "?.getAttribute('aria-valuetext') === '37%'",
        "the depth row does not show the edit");

    // Now the host plays its lanes back.
    rig.store.set_value(spectr::kParamLfoEnabled, 1.0f);
    rig.store.set_value(spectr::kParamLfoShape, 3.0f);
    rig.store.set_value(spectr::kParamLfoRate, 0.5f);
    rig.store.set_value(spectr::kParamLfoDepth, 0.8f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 8);
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-band-action=\"lfo1-enable\"] [data-spectr-menu-switch]')"
        "?.getAttribute('data-spectr-menu-switch') === 'on'",
        "LFO 1 on/off did not follow host playback");
    require_runtime_contract(rig,
        "String(document.querySelector('[data-spectr-band-action=\"lfo1-shape\"]')"
        "?.getAttribute('aria-valuenow')) === '3'",
        "LFO 1 shape did not follow host playback");
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-band-action=\"lfo1-rate\"]')"
        "?.getAttribute('aria-valuetext') === '0.5 beats'",
        "LFO 1 rate did not follow host playback");
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-band-action=\"lfo1-depth\"]')"
        "?.getAttribute('aria-valuetext') === '80%'",
        "LFO 1 depth did not follow host playback after the user edited it");

    // And back off: the switch follows the lane in both directions.
    rig.store.set_value(spectr::kParamLfoEnabled, 0.0f);
    REQUIRE(rig.processor.apply_surface_params(false));
    settle(rig.clock, 8);
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-band-action=\"lfo1-enable\"] [data-spectr-menu-switch]')"
        "?.getAttribute('data-spectr-menu-switch') === 'off'",
        "LFO 1 off did not follow host playback");
    storage.require_unchanged();
}

// THE HEADER LEVEL KNOBS: MIX (1), INTENSITY (5000), OUTPUT (2) and AUTO
// (5001). Every way a person moves one is a host gesture a DAW can record --
// one bracket per drag, one per key press, one per wheel burst -- and the
// values are the knob's own arithmetic (160pt of travel for the full range,
// ten times finer with Shift). Host automation moves them back.
TEST_CASE("header level knobs record one host gesture per act and follow the host",
          "[native-n1][state-parity][automation][level]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    ModulationEditRecorder recorder(rig.store);
    const auto pause_past_double_press = [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    };
    const auto value_of = [&](const char* selector) {
        auto text = runtime_string(rig, std::string{"String(document.querySelector('"}
            + selector + "').getAttribute('aria-valuenow'))", "spectr-knob-value");
        text.erase(std::min(text.find('\n'), text.size()));
        return std::stod(text);
    };

    // MIX: a vertical drag, 80pt down = half the range.
    const auto mix = std::string("[data-spectr-mix]");
    activate(rig, mix, "pointerdown", knob_point(mix, 0.0));
    activate(rig, mix, "pointermove", knob_point(mix, 80.0));
    activate(rig, mix, "pointerup", knob_point(mix, 80.0));
    CHECK(recorder.take() == "begin 1, set 1=50, end 1");
    CHECK(rig.store.get_value(spectr::kMix) == Catch::Approx(50.0f));
    CHECK(value_of("[data-spectr-mix]") == Catch::Approx(50.0));
    pause_past_double_press();

    // INTENSITY: Shift is ten times finer -- 40pt down is 2.5 %, not 25 %.
    const auto intensity = std::string("[data-spectr-intensity]");
    activate(rig, intensity, "pointerdown", knob_point(intensity, 0.0, true));
    activate(rig, intensity, "pointermove", knob_point(intensity, 40.0, true));
    activate(rig, intensity, "pointerup", knob_point(intensity, 40.0, true));
    CHECK(recorder.take() == "begin 5000, set 5000=97.5, end 5000");
    pause_past_double_press();

    // Keys: one complete gesture per press, on the knob's 1 % grid.
    activate(rig, intensity, "keydown", R"js({key:"ArrowDown"})js");
    CHECK(recorder.take() == "begin 5000, set 5000=97, end 5000");
    activate(rig, intensity, "keydown", R"js({key:"End"})js");
    CHECK(recorder.take() == "begin 5000, set 5000=100, end 5000");

    // Wheel: a notch down moves a quarter of the range, each event its own
    // complete gesture (no timer has to fire to close one).
    activate(rig, intensity, "wheel", R"js({deltaY:40})js");
    activate(rig, intensity, "wheel", R"js({deltaY:40})js");
    CHECK(recorder.take()
          == "begin 5000, set 5000=75, end 5000, begin 5000, set 5000=50, end 5000");
    CHECK(rig.store.open_gesture_count() == 0);
    pause_past_double_press();

    // Double press resets to the default inside its own bracket.
    activate(rig, intensity, "pointerdown", knob_point(intensity, 0.0));
    activate(rig, intensity, "pointerup", knob_point(intensity, 0.0));
    activate(rig, intensity, "pointerdown", knob_point(intensity, 0.0));
    activate(rig, intensity, "pointerup", knob_point(intensity, 0.0));
    CHECK(recorder.take() == "begin 5000, end 5000, begin 5000, set 5000=100, end 5000");
    pause_past_double_press();

    // AUTO toggles Auto Gain, one complete gesture. New instances start on.
    REQUIRE(rig.store.get_value(spectr::kParamAutoGain) == 1.0f);
    activate(rig, "[data-spectr-auto-gain]");
    CHECK(recorder.take() == "begin 5001, set 5001=0, end 5001");
    require_runtime_contract(rig,
        "document.querySelector('[data-spectr-auto-gain]')"
        "?.getAttribute('data-spectr-auto-gain-state') === 'off'",
        "AUTO did not show its new state");

    // Host automation moves every knob and the pill.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    rig.store.set_value(spectr::kParamIntensity, 30.0f);
    rig.store.set_value(spectr::kMix, 70.0f);
    rig.store.set_value(spectr::kOutputTrim, -6.0f);
    rig.store.set_value(spectr::kParamAutoGain, 1.0f);
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-intensity]')?.getAttribute('aria-valuenow') === '30'"
        " && document.querySelector('[data-spectr-mix]')?.getAttribute('aria-valuenow') === '70'"
        " && document.querySelector('[data-spectr-output-trim]')?.getAttribute('aria-valuenow') === '-6'"
        " && document.querySelector('[data-spectr-auto-gain]')?.getAttribute('data-spectr-auto-gain-state') === 'on'",
        "the level knobs did not follow host automation");
    CHECK(recorder.take().find("begin") == std::string::npos);  // no echo gestures
    storage.require_unchanged();
}

TEST_CASE("an Output trim or Morph edit records as a host gesture",
          "[native-n1][state-parity][modulation][automation]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    ModulationEditRecorder recorder(rig.store);

    // The trim is the OUTPUT knob: a drag opens one bracket on press and
    // closes it on release, whatever happens in between.
    const auto knob = std::string("[data-spectr-output-trim]");
    activate(rig, knob, "pointerdown", knob_point(knob, 0.0));
    activate(rig, knob, "pointermove", knob_point(knob, -20.0));
    activate(rig, knob, "pointermove", knob_point(knob, -40.0));
    activate(rig, knob, "pointerup", knob_point(knob, -40.0));
    CHECK(recorder.take() == "begin 2, set 2=6, set 2=12, end 2");
    CHECK(rig.store.open_gesture_count() == 0);

    // Morph: capture two different snapshots, then drag. The derived Morph
    // lane is written by the processor as the drag runs; the whole drag is ONE
    // bracket on it.
    const auto press_capture = [&](const char* id) {
        rig.root->layout_children();
        auto* button = rig.bridge().widget(id);
        REQUIRE(button != nullptr);
        const auto box = pulp::view::ViewInspector::absolute_bounds(*button);
        rig.root->simulate_click({box.x + box.width * 0.5f, box.y + box.height * 0.5f});
        settle(rig.clock, 12);
    };
    press_capture("spectr-snapshot-capture-a");
    rig.processor.field().bands[3] = {12.0f, false};
    press_capture("spectr-snapshot-capture-b");
    require_app_state(rig, "s.snapshotStatus.A === true && s.snapshotStatus.B === true",
                      "the two snapshots were not captured");
    recorder.take();
    const auto morph = std::string("[data-spectr-morph]");
    settle_until_contract(rig,
        "document.querySelector('[data-spectr-morph]')"
        "?.getAttribute('data-spectr-morph-state') === 'enabled'",
        "morph never enabled after capturing both snapshots");
    activate(rig, morph, "pointerdown", slider_press_at(0.25, morph));
    activate(rig, morph, "pointermove", slider_press_at(0.5, morph));
    activate(rig, morph, "pointermove", slider_press_at(0.75, morph));
    activate(rig, morph, "pointerup", slider_press_at(0.75, morph));
    const auto recorded = recorder.take();
    INFO("morph drag recorded: " << recorded);
    // One begin, one end, and the values in between.
    CHECK(recorded.rfind("begin 3000, ", 0) == 0);
    const std::string tail = ", end 3000";
    REQUIRE(recorded.size() >= tail.size());
    CHECK(recorded.substr(recorded.size() - tail.size()) == tail);
    std::size_t begins = 0, ends = 0;
    for (std::size_t at = recorded.find("begin 3000"); at != std::string::npos;
         at = recorded.find("begin 3000", at + 1)) ++begins;
    for (std::size_t at = recorded.find("end 3000"); at != std::string::npos;
         at = recorded.find("end 3000", at + 1)) ++ends;
    CHECK(begins == 1);
    CHECK(ends == 1);
    CHECK(recorded.find("set 3000=0.75") != std::string::npos);
    CHECK(rig.store.open_gesture_count() == 0);
    storage.require_unchanged();
}


TEST_CASE("a band paint drag is one host gesture per band it touches",
          "[native-n1][state-parity][modulation][automation]") {
    // The plot republishes the whole processing state on every pointer move,
    // and the processor pushes each changed band lane. Without a drag bracket
    // each of those pushes is its own begin/value/end, so a host in Touch sees
    // the band released between every two moves and snaps back to the old
    // lane in each gap. With one, each lane the drag touches opens once and
    // closes on release.
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    std::map<pulp::state::ParamID, int> begins, ends, sets;
    const auto band_lane = [](pulp::state::ParamID id) {
        return id >= spectr::kParamBandGainBase && id < spectr::kParamBandMuteBase + 64;
    };
    rig.store.set_gesture_callbacks(
        [&](pulp::state::ParamID id) { if (band_lane(id)) ++begins[id]; },
        [&](pulp::state::ParamID id) { if (band_lane(id)) ++ends[id]; });
    auto token = rig.store.add_audio_listener([&](pulp::state::ParamID id, float) {
        if (band_lane(id)) ++sets[id];
    });

    const auto fire = [&](const char* type, int y, int buttons) {
        activate(rig, "[data-spectr-filter-surface]", type,
                 "{clientX:660,clientY:" + std::to_string(y)
                 + ",pointerId:73,button:0,buttons:" + std::to_string(buttons) + "}");
        settle(rig.clock, 4);
    };
    fire("pointerdown", 430, 1);
    fire("pointermove", 380, 1);
    fire("pointermove", 330, 1);
    fire("pointermove", 280, 1);
    fire("pointerup", 280, 0);
    settle(rig.clock, 12);

    // Control: the drag edited something, and edited it more than once, so a
    // per-publication bracket would show up as more than one begin.
    REQUIRE_FALSE(sets.empty());
    int most_sets = 0;
    for (const auto& [id, count] : sets) most_sets = std::max(most_sets, count);
    INFO("lanes written: " << sets.size() << ", most writes to one lane: " << most_sets);
    REQUIRE(most_sets >= 2);
    for (const auto& [id, count] : sets) {
        INFO("lane " << id << " written " << count << " times");
        CHECK(begins[id] == 1);
        CHECK(ends[id] == 1);
    }
    CHECK(rig.store.open_gesture_count() == 0);
    storage.require_unchanged();
}

TEST_CASE("host playback of Morph moves the Morph slider",
          "[native-n1][state-parity][modulation][automation]") {
    // The bands Morph derives always followed playback; the slider did not, so
    // a recorded morph sweep played back with the thumb parked wherever the
    // user last left it.
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);
    const auto thumb_value = [&] {
        return runtime_value(rig,
            "String(document.querySelector('[data-spectr-morph]')"
            "?.getAttribute('aria-valuenow'))",
            "spectr-native-morph-value");
    };
    REQUIRE(thumb_value() == "0");
    for (const float t : {0.25f, 0.75f, 0.0f}) {
        INFO("host morph " << t);
        rig.store.set_value(spectr::kParamMorph, t);
        REQUIRE(rig.processor.apply_surface_params(true));
        settle(rig.clock, 8);
        CHECK(std::stod(thumb_value()) == Catch::Approx(t).margin(1e-6));
    }
    storage.require_unchanged();
}
