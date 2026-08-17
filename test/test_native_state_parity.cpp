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

    void resize(float width, float height) {
        REQUIRE(root != nullptr);
        processor.on_view_resized(*root, width, height);
        settle(clock, 16);
        CHECK(root->bounds().width == Catch::Approx(width));
        CHECK(root->bounds().height == Catch::Approx(height));
    }
};

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
    const auto script = std::string{"(() => { if (!("}
        + std::string(expression) + ")) throw new Error("
        + js_string(message) + " + ': ' + JSON.stringify("
        + "globalThis.__spectrResponsiveLayoutReceipt__)); })();";
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
    CHECK(size.preferred_width == 990);
    CHECK(size.preferred_height == 645);
    CHECK(size.min_width == 792);
    CHECK(size.min_height == 516);
    CHECK(size.max_width == 2640);
    CHECK(size.max_height == 1720);
    CHECK(size.aspect_ratio == Catch::Approx(990.0 / 645.0));
    CHECK(size.design_width == 1320);
    CHECK(size.design_height == 860);
    CHECK(size.viewport_policy == pulp::format::ViewportPolicy::Responsive);
    CHECK_FALSE(pulp::format::should_pin_design_viewport(size));
    CHECK(pulp::format::should_lock_view_aspect(size));

    NativeEditorRig rig;
    const auto directory = atlas_directory();
    struct ResizeCase {
        int width;
        int height;
        std::string_view mode;
        int bottom_height;
        int graph_height;
        std::string_view image;
    };
    for (const auto& sample : std::array<ResizeCase, 4>{
             ResizeCase{792, 516, "compact-two-row", 96, 376, "minimum-home"},
             ResizeCase{990, 645, "compact-two-row", 96, 505, "preferred-home"},
             ResizeCase{1320, 860, "authored", 56, 760, "authored-home"},
             ResizeCase{2640, 1720, "expanded", 56, 1620, "enlarged-home"},
         }) {
        rig.resize(sample.width, sample.height);
        require_runtime_contract(
            rig,
            "(() => { const r = globalThis.__spectrResponsiveLayoutReceipt__; "
            "return r && r.schema === 'spectr-responsive-layout-v1'"
            " && r.width === " + std::to_string(sample.width)
            + " && r.height === " + std::to_string(sample.height)
            + " && r.mode === " + js_string(sample.mode)
            + " && r.design_transform === 'none' && r.top_height === 44"
            + " && r.bottom_height === " + std::to_string(sample.bottom_height)
            + " && r.graph_height === " + std::to_string(sample.graph_height)
            + " && r.focus_order.length > 0"
            + " && r.typography_scale === 1"
            + "; })()",
            "responsive layout receipt mismatch");
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
        "(() => { const s = globalThis.__spectrResponsiveLayoutReceipt__?.settings; "
        "return s && s.width === 520 && Math.abs(s.height - 464.4) < 0.01"
        " && s.content_height === 684 && s.scroll_reachable === true"
        " && s.native_scroll_view === true"
        " && s.authored_skin === true; })()",
        "compact settings were not constrained to a reachable scroll viewport");
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
    CHECK(scroll_view->bounds().height == Catch::Approx(464.4f).margin(0.1f));
    scroll_view->set_scroll(0.0f, 684.0f);
    settle(rig.clock, 4);
    CHECK(scroll_view->scroll_y() > 200.0f);
    const auto* response_label = find_label(*rig.root, "Response");
    REQUIRE(response_label != nullptr);
    const auto response_point = root_point(
        *response_label, response_label->bounds().width * 0.5f,
        response_label->bounds().height * 0.5f);
    CHECK(response_point.y >= 0.0f);
    CHECK(response_point.y <= 516.0f);
    capture(rig, directory, "minimum-settings-bottom", 792, 516);
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
    activate(rig, "[data-spectr-manager-action=\"rename-start\"]");
    // Entering rename replaces the selected row with a controlled input in a
    // follow-up React commit. Give that commit its own host-frame service
    // window before targeting the new node; otherwise a heavily loaded host
    // can make the semantic driver race the mount it just requested.
    settle_until_contract(
        rig,
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
