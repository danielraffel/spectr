#include "spectr/spectr.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <pulp/canvas/recording_canvas.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/input_events.hpp>
#include <pulp/view/pointer_dispatch.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/ui_components.hpp>
#include <pulp/view/window_host.hpp>
#include <pulp/view/widgets.hpp>
#include <pulp/view/widgets/svg_rect.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <sstream>
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
    if (view.on_click && chain_interactive(view)) out.push_back(&view);
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

void native_click_label(NativeEditorRig& rig, std::string_view text) {
    const auto* label = find_label(*rig.root, text);
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
    REQUIRE(rig.root->hit_test(point) == click_target);
    rig.root->simulate_click(point);
    settle(rig.clock, 12);
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
        REQUIRE_FALSE(label->cached_line_boxes().empty());
        const auto& line = label->cached_line_boxes().front();
        points.push_back(root_point(*label,
            line.left + line.width * 0.5f, line.top + line.height * 0.5f));
    } else {
        const auto* label = find_label(button, text);
        REQUIRE(label != nullptr);
        REQUIRE_FALSE(label->cached_line_boxes().empty());
        const auto& line = label->cached_line_boxes().front();
        // The exact captured glyph run is "▸ A/B". Sample the visible icon
        // and final slot glyph separately, rather than blank right padding.
        points.push_back(root_point(*label,
            line.left + 3.0f, line.top + line.height * 0.5f));
        points.push_back(root_point(*label,
            line.left + line.width - 3.0f, line.top + line.height * 0.5f));
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
    const auto points = snapshot_hit_points(button, glyph_text, capture_button);
    REQUIRE(points.size() == 7);
    for (std::size_t index = 0; index < points.size(); ++index) {
        INFO("semantic point index " << index << " on " << button.id());
        auto* target = rig.root->hit_test(points[index]);
        REQUIRE(target != nullptr);
        REQUIRE(target->id() == button.id());
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
    CHECK(size.preferred_width == 1320);
    CHECK(size.preferred_height == 860);
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
    for (const auto text : {"CLEAR", "SCULPT ▾", "PEAK ▾", "PRESETS ▾"}) {
        const auto* label = find_label(*rig.root, text);
        REQUIRE(label != nullptr);
        CHECK(label->font_size() >= 10.0f);
        CHECK_FALSE(label->cached_line_boxes().empty());
        auto* target = label->parent();
        while (target != nullptr && !target->on_click) target = target->parent();
        REQUIRE(target != nullptr);
        CHECK(target->bounds().height >= 24.0f);
    }

    activate(rig, "[data-spectr-settings-open]");
    require_runtime_contract(
        rig,
        // Under the pin the panel keeps its AUTHORED height (679) at every host
        // size and paint scales it, so `scroll_reachable` comes from the design
        // -- 684 of content in a 679 viewport -- not from squeezing the panel to
        // fit the window. The previous expectation of 464.4 was the compact
        // branch constraining it to a 792x516 host; that branch no longer runs,
        // and a height that tracked the host would mean the reflow is back.
        "(() => { const s = globalThis.__spectrResponsiveLayoutReceipt__?.settings; "
        "return s && s.width === 520 && s.height === 679"
        " && s.content_height === 684 && s.scroll_reachable === true"
        " && s.native_scroll_view === true"
        " && s.authored_skin === true; })()",
        "settings panel did not keep its authored geometry under the pin");
    capture(rig, directory, "minimum-settings", 792, 516);
    const auto* settings_title = find_label(*rig.root, "SETTINGS");
    REQUIRE(settings_title != nullptr);
    auto* settings_scroll = const_cast<View*>(
        static_cast<const View*>(settings_title));
    while (settings_scroll != nullptr
           && dynamic_cast<pulp::view::ScrollView*>(settings_scroll) == nullptr)
        settings_scroll = settings_scroll->parent();
    auto* scroll_view = dynamic_cast<pulp::view::ScrollView*>(settings_scroll);
    REQUIRE(scroll_view != nullptr);
    REQUIRE(scroll_view->has_background_color());
    CHECK(scroll_view->background_color().r8() == 14);
    CHECK(scroll_view->background_color().g8() == 18);
    CHECK(scroll_view->background_color().b8() == 25);
    CHECK(scroll_view->background_color().a8() == 250);
    REQUIRE(scroll_view->has_border());
    CHECK(scroll_view->border_color().r8() == 255);
    CHECK(scroll_view->border_color().g8() == 255);
    CHECK(scroll_view->border_color().b8() == 255);
    CHECK(scroll_view->border_color().a8() == 26);
    CHECK(scroll_view->border_width() == Catch::Approx(1.0f));
    CHECK(scroll_view->corner_radius() == Catch::Approx(8.0f));
    CHECK(scroll_view->content_size().height == Catch::Approx(684.0f));
    // Authored height at every host size -- the pin scales it at paint, so the
    // panel is never squeezed to the window. 464.4 was the compact branch
    // fitting it into a 792x516 host; a height that tracks the host now would
    // mean the reflow layer is running alongside the pin.
    CHECK(scroll_view->bounds().height == Catch::Approx(679.0f).margin(0.1f));
    scroll_view->set_scroll(0.0f, 684.0f);
    settle(rig.clock, 4);
    // Scrollable by exactly the design's overflow: 684 of content in a 679
    // viewport. The old > 200 expectation only held while the viewport was
    // squeezed to 464.4 and the overflow was correspondingly large.
    CHECK(scroll_view->scroll_y() == Catch::Approx(5.0f).margin(0.5f));
    const auto* response_label = find_label(*rig.root, "Response");
    REQUIRE(response_label != nullptr);
    const auto response_point = root_point(
        *response_label, response_label->bounds().width * 0.5f,
        response_label->bounds().height * 0.5f);
    // Root points are in AUTHORED space under a pinned viewport, so they are
    // bounded by the design box (860), not by the host window (516). The host
    // maps them at paint: 684.5 authored * (516/860) = 410.7 on screen, which
    // is on-screen exactly as the old assertion intended -- it just tested the
    // wrong coordinate space once the root stopped tracking the window.
    CHECK(response_point.y >= 0.0f);
    CHECK(response_point.y <= spectr::kEditorDesignHeight);
    capture(rig, directory, "minimum-settings-bottom", 792, 516);
}

TEST_CASE("native settings command and minimap cursors reach the shipping runtime",
          "[native-n1][state-parity][commands][cursor]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    require_home(rig);

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
    const auto dispatch_minimap = [&](std::string_view event,
                                      std::string_view hit) {
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
                button: 0, buttons: 1
              })) throw new Error('minimap cursor activation failed');
          if (typeof globalThis.__pulpRuntimeSettle__ === 'function')
            globalThis.__pulpRuntimeSettle__(4);
        })();)js";
        rig.bridge().load_script(script, "spectr-native-minimap-cursor");
        settle(rig.clock, 4);
    };

    dispatch_minimap("pointermove", "left");
    CHECK(surface->cursor() == View::CursorStyle::grab);
    dispatch_minimap("pointermove", "right");
    CHECK(surface->cursor() == View::CursorStyle::grab);
    dispatch_minimap("pointerdown", "window");
    CHECK(surface->cursor() == View::CursorStyle::grabbing);
    dispatch_minimap("pointermove", "window");
    CHECK(surface->cursor() == View::CursorStyle::grabbing);
    dispatch_minimap("pointerup", "window");
    CHECK(surface->cursor() == View::CursorStyle::grab);
    activate(rig, "[data-spectr-filter-surface]", "pointermove",
             R"js({clientX:660,clientY:430,pointerId:72,button:0,buttons:0})js");
    CHECK(surface->cursor() == View::CursorStyle::crosshair);
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
    CAPTURE(overflow_draw->f[0], overflow_draw->f[1]);
    REQUIRE(overflow_draw->f[0] == Catch::Approx(10.0f).margin(0.01f));
    // The captured 13px line box is painted with the 3px CSS half-leading
    // retained around the 10px face.  This is the non-image canary for the
    // toolbar's optical vertical centering at both 1x and Retina scale.
    REQUIRE(overflow_draw->f[1] == Catch::Approx(16.0f).margin(0.01f));

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
        "{\"root\":\"pattern\",\"svg_top\":5.375,\"label_top\":6.375,\"svg_x_shift\":0.25,\"svg_y_shift\":0,\"label_x_shift\":0.25,\"label_y_shift\":0}]'",
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
    require_toolbar_child_geometry("PRESETS ▾", 6.375f,
                                   7.375f, 18.0f, 13.0f,
                                   11.0f, 35.0f,
                                   0.25f, 0.0f, 0.25f, 0.0f);
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

    activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
    require_state(rig, "edit");
    capture(rig, directory, "edit");
    activate(rig, "[data-spectr-edit-mode=\"level\"]");
    require_app_state(rig, "s.editMode === 'level'", "edit mode menu selection failed");
    activate(rig, "[data-spectr-menu-root=\"edit\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-edit-mode=\"sculpt\"]");
    // The idle status shell stays in the tree without painting, so transient
    // messages cannot shift materialized state paths.
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"analyzer\"] [data-spectr-menu-trigger]");
    require_state(rig, "analyzer");
    capture(rig, directory, "analyzer");
    activate(rig, "[data-spectr-analyzer-mode=\"avg\"]");
    require_app_state(rig, "s.analyzerMode === 'avg'", "analyzer menu selection failed");
    activate(rig, "[data-spectr-menu-root=\"analyzer\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-analyzer-mode=\"peak\"]");
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
    capture(rig, directory, "pattern");
    activate(rig, "[data-spectr-pattern-menu-id=\"factory:tilt\"]");
    REQUIRE(std::any_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) { return std::abs(band.gain_db) > 0.1f; }));
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-pattern-menu-id=\"factory:flat\"]");
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
    const View* authored_settings_panel = authored_settings_title;
    while (authored_settings_panel != nullptr
           && dynamic_cast<const pulp::view::ScrollView*>(
                  authored_settings_panel) == nullptr)
        authored_settings_panel = authored_settings_panel->parent();
    REQUIRE(authored_settings_panel != nullptr);
    CHECK(authored_settings_panel->bounds().x
          == Catch::Approx(400.0f).margin(0.01f));
    CHECK(authored_settings_panel->bounds().y
          == Catch::Approx(90.5f).margin(0.01f));
    CHECK(authored_settings_panel->bounds().width
          == Catch::Approx(520.0f).margin(0.01f));
    CHECK(authored_settings_panel->bounds().height
          == Catch::Approx(679.0f).margin(0.01f));
    capture(rig, directory, "settings");
    activate(rig, "[data-spectr-setting-option=\"warm\"]");
    activate(rig, "[data-spectr-setting-toggle]");
    activate(rig, "[data-spectr-setting-slider]", "input",
             R"js({value:'0.75',target:{value:'0.75'},currentTarget:{value:'0.75'}})js");
    require_app_state(rig,
        "s.settings.theme === 'warm' && s.settings.bloom === 0.75",
        "settings controls did not update their painted state");
    // Later atlas states were captured from the same deterministic defaults as
    // home. Prove the settings are reversible, then restore those defaults
    // before continuing the single editor transaction.
    activate(rig, "[data-spectr-setting-option=\"spectral\"]");
    activate(rig, "[data-spectr-setting-toggle]");
    activate(rig, "[data-spectr-setting-slider]", "input",
             R"js({value:'1',target:{value:'1'},currentTarget:{value:'1'}})js");
    require_app_state(rig,
        "s.settings.theme === 'spectral' && s.settings.showMinimap === true"
        " && s.settings.bloom === 1",
        "settings controls did not restore deterministic atlas defaults");
    activate(rig, "[data-spectr-settings-close]");
    require_home(rig);

    activate(rig, "[data-spectr-menu-root=\"help\"] [data-spectr-menu-trigger]");
    require_state(rig, "help");
    capture(rig, directory, "help");
    // Native document/window keyboard listeners are not yet fed by Pulp's
    // focused-view key path. The same trigger is the product's deterministic
    // native close interaction; Chromium separately proves Escape.
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

    auto* capture_a = rig.bridge().widget("spectr-snapshot-capture-a");
    auto* capture_b = rig.bridge().widget("spectr-snapshot-capture-b");
    auto* recall_a = rig.bridge().widget("spectr-snapshot-recall-a");
    auto* recall_b = rig.bridge().widget("spectr-snapshot-recall-b");
    REQUIRE(capture_a != nullptr);
    REQUIRE(capture_b != nullptr);
    REQUIRE(recall_a != nullptr);
    REQUIRE(recall_b != nullptr);

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

    const auto revision_before_morph = rig.processor.native_editor_revision();
    activate(rig, "[data-spectr-morph]", "input",
             R"js({value:'0.5',target:{value:'0.5'},currentTarget:{value:'0.5'}})js");
    REQUIRE(rig.processor.native_editor_revision() == revision_before_morph + 1);
    REQUIRE(rig.processor.field().bands[3].gain_db == Catch::Approx(3.0f));
    REQUIRE(rig.processor.field().bands[3].muted);

    // Native UI save and rename update the processor-owned library before the
    // plugin blob is serialized. No browser-local storage participates.
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-save-current]");
    require_state(rig, "save-dialog");
    capture(rig, directory, "save-dialog");
    require_app_state(rig, "s.saveDialogOpen === true",
                      "save command did not open its native dialog");
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
        native_click_label(rig, "MANAGE…");
        require_app_state(rig, "s.managerOpen === true",
                          "native pointer did not open pattern manager");
        native_click_label(rig, "×");
        require_app_state(rig, "s.managerOpen === false",
                          "native pointer did not close pattern manager");
    }
    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    native_click_label(rig, "MANAGE…");
    require_app_state(rig,
        "s.managerOpen === true && s.userPatterns.length === 1",
        "saved preset was not available in the native pattern manager");
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
    reopened.bridge().load_script("globalThis.confirm = () => true;",
                                  "spectr-native-confirm-delete");
    activate(reopened, "[data-spectr-manager-action=\"delete\"]");
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
    for (const auto* control : controls) {
        if (control->bounds().width <= 0.0f || control->bounds().height <= 0.0f)
            continue;
        INFO("control " << describe_control(*control));
        // Aim at the PAINTED extent, in root space, because that is what the
        // user aims at. Probing the hit box instead would pass even when ink
        // spills outside it, which is the whole complaint in #39.
        const auto painted = painted_extent(*control);
        const float y = (painted.top + painted.bottom) * 0.5f;
        for (const float x : {painted.left + 1.5f,
                              (painted.left + painted.right) * 0.5f,
                              painted.right - 1.5f}) {
            CAPTURE(x, y, painted.left, painted.right);
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
        CHECK(box.right - box.left == Catch::Approx(14.0f));
        CHECK(box.bottom - box.top == Catch::Approx(14.0f));
        // INVARIANT under the pin, and this is the proportional contract in one
        // assertion: the grip sits at the bottom-right of the AUTHORED box at
        // every host size, because the root never reflows — the host scales it.
        // Under the old responsive contract this tracked the live host bounds
        // instead, which is exactly the reflow the user ruled out.
        CHECK(box.right
              == Catch::Approx(static_cast<float>(spectr::kEditorDesignWidth) - 3.0f));
        // Laid INTO the bottom bar rather than floated in the corner behind it
        // (the Efx FRAGMENTS arrangement): vertically centred on the bar's 26pt
        // control row, whose centre is bar_top(804) + 15.5 + 13 = 832.5. A grip
        // that drifts off that row stops reading as part of the bar, and a bar
        // height change that nobody propagated fails here.
        constexpr float kBarControlRowCentre = 804.0f + 15.5f + 13.0f;
        CHECK((box.top + box.bottom) * 0.5f
              == Catch::Approx(kBarControlRowCentre));
        CHECK(box.bottom < static_cast<float>(spectr::kEditorDesignHeight));

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
    auto& mutable_grip = *const_cast<View*>(grip);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> requests;
    bool accept = true;
    rig.processor.set_editor_resize_handler(
        [&](std::uint32_t w, std::uint32_t h) {
            requests.push_back({w, h});
            return accept;
        });

    // Deltas are stated as POINTER movement — window-space points, which is
    // what a user actually moves and what the editor must grow by. At this host
    // size the design viewport scales by 990/1320 = 0.75, so a root-space delta
    // is NOT the same number; deriving the root points to deliver from the
    // intended window movement keeps the case honest at any host size.
    const auto drag = [&](float window_dx, float window_dy) {
        const auto anchor = pulp::view::Point{0.0f, 0.0f};
        const auto origin = root_to_window(anchor, 990.0f, 645.0f);
        const auto moved = window_to_root(
            {origin.x + window_dx, origin.y + window_dy}, 990.0f, 645.0f);
        mutable_grip.on_mouse_down(anchor);
        mutable_grip.on_mouse_drag(moved);
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
        mutable_grip.on_mouse_drag({360.0f, 0.0f});
        mutable_grip.on_mouse_drag({390.0f, 0.0f});
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
        auto& mutable_grip = *const_cast<View*>(grip);
        const auto anchor = pulp::view::Point{0.0f, 0.0f};
        const auto origin = root_to_window(anchor, host_w, host_h);
        const auto moved = window_to_root(
            {origin.x + window_dx, origin.y + window_dy}, host_w, host_h);
        mutable_grip.on_mouse_down(anchor);
        mutable_grip.on_mouse_drag(moved);
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
              == Catch::Approx(static_cast<float>(spectr::kEditorDesignWidth) - 3.0f));
        // Laid INTO the bottom bar rather than floated in the corner behind it
        // (the Efx FRAGMENTS arrangement): vertically centred on the bar's 26pt
        // control row, whose centre is bar_top(804) + 15.5 + 13 = 832.5. A grip
        // that drifts off that row stops reading as part of the bar, and a bar
        // height change that nobody propagated fails here.
        constexpr float kBarControlRowCentre = 804.0f + 15.5f + 13.0f;
        CHECK((box.top + box.bottom) * 0.5f
              == Catch::Approx(kBarControlRowCentre));
        CHECK(box.bottom < static_cast<float>(spectr::kEditorDesignHeight));
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
    pulp::view::deliver_mouse_drag(*rig.root, target, moved, /*modifiers=*/0);

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
        pulp::view::deliver_mouse_drag(*rig.root, target, delivered,
                                       /*modifiers=*/0);
    }

    REQUIRE(!requests.empty());
    // A stationary pointer resolves to ONE size. Not "settles eventually" —
    // every request the gesture makes must agree, because each disagreement is
    // a visible jump of the plug-in window in the host.
    const auto first = requests.front();
    for (const auto& request : requests) {
        INFO("requested " << request.first << 'x' << request.second
             << " vs first " << first.first << 'x' << first.second);
        CHECK(request == first);
    }
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
    pulp::view::deliver_mouse_drag(*rig.root, target, moved, /*modifiers=*/0);

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
    // to say and the JS pass must not run again.
    for (const auto& size : std::array<std::pair<int, int>, 4>{
             std::pair{1400, 912}, std::pair{1480, 964},
             std::pair{1560, 1016}, std::pair{1640, 1068}}) {
        rig.processor.on_view_resized(*rig.root,
                                      static_cast<std::uint32_t>(size.first),
                                      static_cast<std::uint32_t>(size.second));
    }
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
