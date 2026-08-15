#include "spectr/spectr.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/canvas/font_resolver.hpp>
#include <pulp/canvas/text_shaper.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/canvas_widget.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/input_events.hpp>
#include <pulp/view/js_engine.hpp>
#include <pulp/view/pointer_dispatch.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/widgets.hpp>
#include <pulp/view/widgets/svg_rect.hpp>

#include <cmath>
#include <array>
#include <string>
#include <sstream>
#include <vector>

namespace {

pulp::view::Point root_point(const pulp::view::View& view,
                             float local_x, float local_y) {
    auto* target = const_cast<pulp::view::View*>(&view);
    auto* root = target;
    while (root->parent()) root = root->parent();

    // point_to_local() is the production inverse mapping used by native input.
    // Sample its affine basis and solve it here instead of reimplementing the
    // ancestor transform/scroll chain with an offset-only test helper.
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

void feed_tone(spectr::Spectr& processor, pulp::view::FrameClock& clock) {
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
        processor.process(output, input, midi_in, midi_out, context);
        clock.tick(1.0f / 30.0f);
    }
}

void dump_hit_tree(const pulp::view::View& view, int depth,
                   std::ostringstream& out) {
    if (!view.id().empty()) {
        out << '\n' << std::string(static_cast<size_t>(depth), ' ')
            << view.id() << " pe=" << static_cast<int>(view.pointer_events())
            << " b=" << view.bounds().x << ',' << view.bounds().y << ','
            << view.bounds().width << ',' << view.bounds().height
            << " op=" << view.opacity();
    }
    for (size_t i = 0; i < view.child_count(); ++i)
        dump_hit_tree(*view.child_at(i), depth + 1, out);
}

const pulp::view::Label* find_label(const pulp::view::View& view,
                                    std::string_view text) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
        label != nullptr && label->text() == text)
        return label;
    for (size_t index = 0; index < view.child_count(); ++index)
        if (const auto* match = find_label(*view.child_at(index), text))
            return match;
    return nullptr;
}

} // namespace

TEST_CASE("native N1 mounts live QuickJS widgets without an editor fallback",
          "[native-n1]") {
    spectr::Spectr processor;
    pulp::state::StateStore store;
    processor.set_state_store(&store);
    processor.define_parameters(store);
    pulp::format::PrepareContext prepare;
    prepare.sample_rate = 48000.0;
    prepare.max_buffer_size = 256;
    prepare.input_channels = 2;
    prepare.output_channels = 2;
    processor.prepare(prepare);

    const auto size = processor.view_size();
    REQUIRE(size.preferred_width == 1320);
    REQUIRE(size.preferred_height == 860);
    REQUIRE(size.min_width == 1320);
    REQUIRE(size.max_width == 1320);

    const auto engines_before = pulp::view::js_engine_creation_stats();
    auto root = processor.create_view();
    REQUIRE(root != nullptr);
    const auto engines_after = pulp::view::js_engine_creation_stats();
    REQUIRE(engines_after.quickjs > engines_before.quickjs);
    REQUIRE(engines_after.jsc == engines_before.jsc);
    REQUIRE(engines_after.v8 == engines_before.v8);
    REQUIRE(root->requires_gpu_host());
    root->set_bounds({0, 0, 1320, 860});
    pulp::view::FrameClock clock;
    root->set_frame_clock(&clock);
    root->layout_children();
    processor.on_view_opened(*root);

    auto* session = processor.active_scripted_ui();
    REQUIRE(session != nullptr);
    REQUIRE(session->bridge() != nullptr);

    // Native visual authority keeps the live materialized canvas as the sole
    // painter and input owner. The captured Browser_canvas sibling is hidden
    // evidence and must never be used to synthesize an interaction.
    auto* canvas = dynamic_cast<pulp::view::CanvasWidget*>(
        session->bridge()->widget("__behavior_pr_1"));
    REQUIRE(canvas != nullptr);
    REQUIRE(canvas->command_count() > 10);
    const auto static_analyzer_commands = canvas->command_count();
    for (int frame = 0; frame < 4; ++frame)
        clock.tick(1.0f / 60.0f);
    session->bridge()->load_script(R"js(
      globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};
      globalThis.__spectrEditorRequests = [];
      const spectrEditorDispatch = globalThis.__spectrEditorDispatch;
      globalThis.__spectrEditorDispatch = request => {
        globalThis.__spectrEditorRequests.push(JSON.parse(request));
        return spectrEditorDispatch(request);
      };
      globalThis.__spectrBridgeTypes = {
        sameWindow: globalThis.window === globalThis,
        samePulp: globalThis.pulp === globalThis.window?.pulp,
        pulp: typeof globalThis.pulp,
        windowPulp: typeof globalThis.window?.pulp,
        state: typeof globalThis.SpectrNativeState,
        windowState: typeof globalThis.window?.SpectrNativeState,
        hooks: Object.keys(globalThis.__spectrTestHooks || {}),
        windowKeys: Object.keys(globalThis.window || {}).filter(key =>
          key.startsWith('Spectr') || key === 'pulp'),
      };
    )js", "spectr-native-install-test-hooks");
    for (int frame = 0; frame < 8; ++frame)
        clock.tick(1.0f / 60.0f);
    auto* behavior_owner = session->bridge()->widget("__behavior_pr_3");
    REQUIRE(behavior_owner != nullptr);
    REQUIRE(static_cast<bool>(behavior_owner->on_dom_pointer_event));
    REQUIRE(static_cast<bool>(behavior_owner->on_dom_pointer_move_event));
    session->bridge()->load_script(R"js(
      const homeDiagnostics = globalThis.__pulpMaterializedMetadataDiagnostics__;
      if (!homeDiagnostics || homeDiagnostics.state_id !== '' ||
          homeDiagnostics.paint_expected !== 17 ||
          homeDiagnostics.paint_applied !== 17 ||
          homeDiagnostics.paint_node_miss !== 0 ||
          homeDiagnostics.paint_unsupported !== 0)
        throw new Error(`materialized home paint was not applied exactly: ${JSON.stringify(homeDiagnostics)}`);
      if (typeof globalThis.__pulpNativeBridgeFunctions__?.setSvgRect !== 'function')
        throw new Error(`native setSvgRect snapshot unavailable: ${typeof globalThis.__pulpNativeBridgeFunctions__?.setSvgRect}`);
    )js", "spectr-native-materialized-home-paint-contract");
    const auto logo_rect_ids = {"__behavior_pr_4", "__behavior_pr_5",
                                "__behavior_pr_6", "__behavior_pr_7",
                                "__behavior_pr_8"};
    for (const auto* id : logo_rect_ids) {
        const auto* rect = dynamic_cast<const pulp::view::SvgRectWidget*>(
            session->bridge()->widget(id));
        REQUIRE(rect != nullptr);
        CAPTURE(id, rect->bounds().x, rect->bounds().y,
                rect->bounds().width, rect->bounds().height,
                rect->rect_x(), rect->rect_y(),
                rect->rect_width(), rect->rect_height(),
                rect->has_fill(), rect->fill_color().r,
                rect->fill_color().g, rect->fill_color().b,
                rect->fill_color().a);
        REQUIRE(rect->bounds().width > 0.0f);
        REQUIRE(rect->bounds().height > 0.0f);
        REQUIRE(rect->rect_width() > 0.0f);
        REQUIRE(rect->rect_height() > 0.0f);
        REQUIRE(rect->has_fill());
        REQUIRE(rect->fill_color().a > 0.0f);
    }

    // Drive the imported React pointer handler through the actual native
    // CanvasWidget. The processor-owned EditorBridge remains the only state
    // authority: each complete tap advances exactly once and a second tap
    // restores the authored gain without losing it behind categorical mute.
    const auto band = 16;
    const auto authored_gain = processor.field().bands[band].gain_db;
    const auto canvas_bounds = canvas->bounds();
    constexpr float graph_pad_left = 56.0f;
    constexpr float graph_pad_right = 56.0f;
    constexpr float band_gap = 2.0f;
    constexpr float visible_bands = 32.0f;
    const float inner_width = canvas_bounds.width - graph_pad_left - graph_pad_right;
    const float band_width =
        (inner_width - band_gap * (visible_bands - 1.0f)) / visible_bands;
    const float band_center = graph_pad_left +
        static_cast<float>(band) * (band_width + band_gap) + band_width * 0.5f;
    const auto tap = root_point(*canvas,
        band_center,
        canvas_bounds.height * 0.5f);
    auto* tap_target = root->hit_test(tap);
    REQUIRE(tap_target != nullptr);
    std::ostringstream hit_tree;
    dump_hit_tree(*root, 0, hit_tree);
    CAPTURE(tap.x, tap.y, tap_target->id(), tap_target->anchor_id(),
            tap_target->bounds().x, tap_target->bounds().y,
            tap_target->bounds().width, tap_target->bounds().height,
            tap_target->opacity(), behavior_owner->bounds().x,
            behavior_owner->bounds().y, behavior_owner->bounds().width,
            behavior_owner->bounds().height, behavior_owner->opacity(),
            hit_tree.str());
    root->simulate_click(tap);
    for (int frame = 0; frame < 8; ++frame)
        clock.tick(1.0f / 60.0f);
    if (processor.native_editor_revision() != 1) {
        session->bridge()->load_script(R"js(
          const callbacks = globalThis.__pulpReactEventCallbacks__;
          const keys = callbacks instanceof Map ? [...callbacks.keys()] : [];
          throw new Error(`tap did not publish; callbacks=${keys.filter(key =>
            key.includes('__behavior_pr_3')).join(',')}; state=${JSON.stringify(
              globalThis.__spectrTestHooks?.renderState?.() ?? null)}; requests=${JSON.stringify(
              globalThis.__spectrEditorRequests)}; bridgeTypes=${JSON.stringify(
              globalThis.__spectrBridgeTypes)}`);
        )js", "spectr-native-band-tap-debug");
    }
    REQUIRE(processor.native_editor_revision() == 1);
    std::vector<int> muted_after_first_tap;
    for (int i = 0; i < static_cast<int>(processor.field().bands.size()); ++i) {
        if (processor.field().bands[i].muted)
            muted_after_first_tap.push_back(i);
    }
    CAPTURE(muted_after_first_tap);
    REQUIRE(muted_after_first_tap == std::vector<int>{band});
    REQUIRE(processor.field().bands[band].muted);
    REQUIRE(processor.field().bands[band].gain_db == authored_gain);
    root->simulate_click(tap);
    for (int frame = 0; frame < 8; ++frame)
        clock.tick(1.0f / 60.0f);
    REQUIRE(processor.native_editor_revision() == 2);
    REQUIRE_FALSE(processor.field().bands[band].muted);
    REQUIRE(processor.field().bands[band].gain_db == authored_gain);

    session->bridge()->load_script(R"js(
      if (typeof globalThis.__pulpActivateMaterializedElement__ !== 'function' ||
          !globalThis.__pulpActivateMaterializedElement__(
              '[data-spectr-settings-open]', 'click', null))
        throw new Error('materialized Settings trigger was not reconstructed');
    )js", "spectr-native-open-settings");
    for (int frame = 0; frame < 4; ++frame)
        clock.tick(1.0f / 60.0f);
    session->bridge()->load_script(R"js(
      const settingsDiagnostics = globalThis.__pulpMaterializedMetadataDiagnostics__;
      if (!settingsDiagnostics || settingsDiagnostics.state_id !== 'settings' ||
          settingsDiagnostics.layout_expected !== 175 ||
          settingsDiagnostics.layout_applied !== 175 ||
          settingsDiagnostics.layout_node_miss !== 0 ||
          settingsDiagnostics.text_expected !== 67 ||
          settingsDiagnostics.text_applied !== 67 ||
          settingsDiagnostics.text_node_miss !== 0 ||
          settingsDiagnostics.text_content_mismatch !== 0 ||
          settingsDiagnostics.text_target_miss !== 0)
        throw new Error(`materialized settings metadata was not applied exactly: ${JSON.stringify(settingsDiagnostics)}`);
    )js", "spectr-native-materialized-text-contract");
    const auto* spectral_label = find_label(*root, "Spectral");
    REQUIRE(spectral_label != nullptr);
    CAPTURE(spectral_label->font_family(), spectral_label->font_size(),
            spectral_label->font_weight(), spectral_label->bounds().x,
            spectral_label->bounds().y, spectral_label->bounds().width,
            spectral_label->bounds().height);
    REQUIRE(spectral_label->font_family().find("pulp-materialized-asset-")
            != std::string::npos);
    REQUIRE(pulp::canvas::resolved_face_identity(
                spectral_label->font_family(), spectral_label->font_weight())
            == "JetBrainsMono-Regular");
    const auto spectral_shaped = pulp::canvas::global_text_shaper().prepare(
        "Spectral", spectral_label->font_family(), 10.0f, 400,
        /*font_style=*/0, /*letter_spacing=*/0.8f);
    CAPTURE(spectral_shaped.total_width(), spectral_shaped.ascent(),
            spectral_shaped.descent(), spectral_shaped.leading(),
            spectral_shaped.metrics_are_real());
    REQUIRE(spectral_shaped.metrics_are_real());
    // Chromium's captured line box for this exact string is 54.40625 px.
    // This gate exercises the real Skia typeface and shaping path; a merely
    // declared family name that falls back to another face cannot pass it.
    REQUIRE(spectral_shaped.total_width()
            == Catch::Approx(54.40625f).margin(0.02f));
    REQUIRE(spectral_label->font_size()
            == Catch::Approx(10.0f).margin(0.001f));
    REQUIRE(spectral_label->font_weight() == 400);
    REQUIRE(spectral_label->letter_spacing()
            == Catch::Approx(0.8f).margin(0.001f));
    // Chromium captured the line relative to the semantic button's 76.40625 x
    // 25 border box.  The native host generates this paint-only Label one
    // border pixel inward on each side, so the importer must translate the
    // captured line into the Label's 74.40625 x 23 local coordinate space.
    REQUIRE(spectral_label->bounds().width
            == Catch::Approx(74.40625f).margin(0.01f));
    REQUIRE(spectral_label->bounds().height
            == Catch::Approx(23.0f).margin(0.01f));
    REQUIRE(spectral_label->cached_line_boxes().size() == 1);
    const auto& spectral_line = spectral_label->cached_line_boxes().front();
    CAPTURE(spectral_line.left, spectral_line.top, spectral_line.width,
            spectral_line.height);
    REQUIRE(spectral_line.left == Catch::Approx(10.0f).margin(0.01f));
    REQUIRE(spectral_line.top == Catch::Approx(5.0f).margin(0.01f));
    REQUIRE(spectral_line.width
            == Catch::Approx(54.40625f).margin(0.01f));
    REQUIRE(spectral_line.height == Catch::Approx(13.0f).margin(0.01f));
    if (const auto* capture_path =
            std::getenv("SPECTR_NATIVE_TEST_CAPTURE_SETTINGS");
        capture_path != nullptr && *capture_path != '\0') {
        REQUIRE(pulp::view::render_to_file(
            *root, 1320, 860, capture_path, 2.0f,
            pulp::view::ScreenshotBackend::gpu));
    }
    session->bridge()->load_script(R"js(
      if (!globalThis.__pulpActivateMaterializedElement__(
              '[data-spectr-settings-close]', 'click', null))
        throw new Error('could not close settings after its native contract check');
    )js", "spectr-native-close-settings");
    for (int frame = 0; frame < 4; ++frame)
        clock.tick(1.0f / 60.0f);
    session->bridge()->load_script(R"js(
      const restoredHomeDiagnostics = globalThis.__pulpMaterializedMetadataDiagnostics__;
      if (!restoredHomeDiagnostics || restoredHomeDiagnostics.state_id !== '' ||
          restoredHomeDiagnostics.layout_expected !== 81 ||
          restoredHomeDiagnostics.layout_applied !== 81 ||
          restoredHomeDiagnostics.layout_node_miss !== 0 ||
          restoredHomeDiagnostics.text_expected !== 23 ||
          restoredHomeDiagnostics.text_applied !== 23 ||
          restoredHomeDiagnostics.text_node_miss !== 0 ||
          restoredHomeDiagnostics.text_content_mismatch !== 0 ||
          restoredHomeDiagnostics.text_target_miss !== 0)
        throw new Error(`materialized home metadata was not restored exactly after settings close: ${JSON.stringify(restoredHomeDiagnostics)}`);
    )js", "spectr-native-home-restore-contract");

    session->bridge()->load_script(R"js(
      const analyzer = globalThis.SpectrAnalyzer;
      if (!analyzer || typeof analyzer.sample !== 'function')
        throw new Error('native analyzer service was not installed');
      const sample = analyzer.sample.bind(analyzer);
      globalThis.__spectrAnalyzerAppSampleCalls = 0;
      analyzer.sample = (...args) => {
        ++globalThis.__spectrAnalyzerAppSampleCalls;
        return sample(...args);
      };
    )js", "spectr-native-analyzer-observer");

    feed_tone(processor, clock);
    REQUIRE(processor.read_spectrum().sequence_number > 0);
    REQUIRE(canvas->command_count() >= static_analyzer_commands);
    if (const auto* capture_path = std::getenv("SPECTR_NATIVE_TEST_CAPTURE");
        capture_path != nullptr && *capture_path != '\0') {
        REQUIRE(pulp::view::render_to_file(
            *root, 1320, 860, capture_path, 2.0f,
            pulp::view::ScreenshotBackend::gpu));
    }
    session->bridge()->load_script(R"js(
      const frame = globalThis.SpectrAnalyzer?.debugSnapshot?.();
      if (!frame || frame.sequence_number <= 0)
        throw new Error('native analyzer frame was not accepted');
      const at1k = globalThis.SpectrAnalyzer.sample(Math.log10(1000), 0, 'visible');
      const at100 = globalThis.SpectrAnalyzer.sample(Math.log10(100), 0, 'visible');
      const at10k = globalThis.SpectrAnalyzer.sample(Math.log10(10000), 0, 'visible');
      if (!(Number.isFinite(at1k) && at1k > at100 + 0.1 && at1k > at10k + 0.1))
        throw new Error(`native analyzer did not preserve the 1k tone: ${at100}/${at1k}/${at10k}`);
      if (!(globalThis.__spectrAnalyzerAppSampleCalls > 3))
        throw new Error('materialized application did not sample the native analyzer');
    )js", "spectr-native-analyzer-contract");

    // The native retained command stream must preserve the calibrated dBFS
    // ruler, not merely the browser-side source math.  Use the dBFS heading's
    // x-coordinate to disambiguate the right-side `0` from the EQ gain-axis
    // `0`, then prove every label uses the same linear [-120,+24] projection.
    using CanvasCommand = pulp::view::CanvasDrawCmd;
    const auto& analyzer_commands = canvas->commands();
    const auto heading = std::find_if(
        analyzer_commands.begin(), analyzer_commands.end(), [](const auto& cmd) {
            return cmd.type == CanvasCommand::Type::fill_text
                && cmd.text == "dBFS";
        });
    REQUIRE(heading != analyzer_commands.end());
    const auto ruler_tick_y = [&](std::string_view label) {
        const auto match = std::find_if(
            analyzer_commands.begin(), analyzer_commands.end(),
            [&](const auto& cmd) {
                return cmd.type == CanvasCommand::Type::fill_text
                    && cmd.text == label
                    && std::abs(cmd.x - heading->x) < 0.05f;
            });
        REQUIRE(match != analyzer_commands.end());
        return match->y;
    };
    struct DbfsTick { std::string_view label; float db; };
    constexpr std::array<DbfsTick, 6> dbfs_ticks{{
        {"-120", -120.0f}, {"-90", -90.0f}, {"-60", -60.0f},
        {"-30", -30.0f}, {"0", 0.0f}, {"+24", 24.0f},
    }};
    const float floor_y = ruler_tick_y("-120");
    const float ceiling_y = ruler_tick_y("+24");
    REQUIRE(ceiling_y < floor_y);
    for (const auto& tick : dbfs_ticks) {
        const float amount = (tick.db + 120.0f) / 144.0f;
        const float expected_y = floor_y + amount * (ceiling_y - floor_y);
        CHECK(ruler_tick_y(tick.label)
              == Catch::Approx(expected_y).margin(0.05f));
    }

    // Malformed paint geometry must fail before touching the authoritative
    // field or revision. Exercise the same attached JS-to-C++ endpoint used by
    // @pulp/react, rather than calling the product handler directly.
    const auto invalid_gain = processor.field().bands[4].gain_db;
    const auto revision_before_invalid_paint = processor.native_editor_revision();
    session->bridge()->load_script(R"js(
      globalThis.__spectrEditorDispatch(JSON.stringify({type:'paint_start', payload:{}}));
      const rejected = JSON.parse(globalThis.__spectrEditorDispatch(JSON.stringify({type:'paint', payload:{
        mode:'Sculpt', start_band:4, start_value:0,
        current_band:999, current_value:12, n_visible:32
      }})));
      if (rejected.ok !== false) throw new Error('invalid paint was not rejected');
      globalThis.__spectrEditorDispatch(JSON.stringify({type:'paint_end', payload:{}}));
    )js", "spectr-native-invalid-paint");
    REQUIRE(processor.field().bands[4].gain_db == invalid_gain);
    REQUIRE(processor.native_editor_revision() == revision_before_invalid_paint);

    const auto revision_before_close = processor.native_editor_revision();
    processor.on_view_closed(*root);
    REQUIRE(processor.active_scripted_ui() == nullptr);
    root.reset();

    auto reopened = processor.create_view();
    REQUIRE(reopened != nullptr);
    REQUIRE(processor.native_editor_revision() == revision_before_close);
    processor.on_view_closed(*reopened);
}
