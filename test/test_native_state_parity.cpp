#include "spectr/spectr.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/input_events.hpp>
#include <pulp/view/pointer_dispatch.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/widgets.hpp>

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
};

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
             std::string_view name) {
    if (!directory) return;
    const auto path = *directory / (std::string(name) + ".png");
    REQUIRE(pulp::view::render_to_file(
        *rig.root, 1320, 860, path.string(), 2.0f,
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

TEST_CASE("native frozen state atlas interactions and persistence",
          "[native-n1][state-parity]") {
    PatternStoragePoison storage;
    NativeEditorRig rig;
    const auto directory = atlas_directory();
    require_home(rig);
    require_app_state(rig, "s.nativeHydrated === true && s.statusMounted === false",
                      "initial native commit was not hydrated and status-free");
    require_app_state(rig, "s.userPatterns.length === 0",
                      "native UI consumed browser-local preset poison");
    REQUIRE(std::all_of(rig.processor.field().bands.begin(),
                        rig.processor.field().bands.begin() + 32,
                        [](const auto& band) {
                            return band.gain_db == 0.0f && !band.muted;
                        }));
    storage.require_unchanged();
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

    activate(rig, "[data-spectr-menu-root=\"pattern\"] [data-spectr-menu-trigger]");
    activate(rig, "[data-spectr-pattern-manage]");
    require_app_state(rig,
        "s.managerOpen === true && s.userPatterns.length === 1",
        "saved preset was not available in the native pattern manager");
    capture(rig, directory, "pattern-manager");
    activate(rig, "[data-spectr-pattern-id=" + js_string(pattern_id) + "]");
    activate(rig, "[data-spectr-manager-action=\"rename-start\"]");
    activate(rig, "#spectr-manager-rename", "change",
             R"js({value:'FLAT',target:{value:'FLAT'},currentTarget:{value:'FLAT'}})js");
    activate(rig, "#spectr-manager-rename", "blur");
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
