#include "spectr/spectr.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/canvas_widget.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/input_events.hpp>
#include <pulp/view/js_engine.hpp>
#include <pulp/view/scripted_ui.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace {

pulp::view::View& native_band(pulp::view::ScriptedUiSession& session, int index) {
    auto* band = session.bridge()->widget("spectr-band-" + std::to_string(index));
    REQUIRE(band != nullptr);
    return *band;
}

pulp::view::Point root_point(const pulp::view::View& view, float local_y) {
    pulp::view::Point point{view.bounds().width * 0.5f, local_y};
    for (auto* node = &view; node && node->parent(); node = node->parent()) {
        point.x += node->bounds().x;
        point.y += node->bounds().y;
    }
    return point;
}

void tap_band(pulp::view::View& root,
              pulp::view::ScriptedUiSession& session, int index) {
    auto& band = native_band(session, index);
    root.simulate_click(root_point(band, band.bounds().height * 0.5f));
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

    auto* canvas = dynamic_cast<pulp::view::CanvasWidget*>(
        session->bridge()->widget("spectr-analyzer-canvas"));
    REQUIRE(canvas != nullptr);
    REQUIRE(canvas->command_count() > 10);
    const auto static_analyzer_commands = canvas->command_count();
    for (int band = 0; band < 32; ++band) {
        REQUIRE(session->bridge()->widget(
            "spectr-band-" + std::to_string(band)) != nullptr);
    }

    feed_tone(processor, clock);
    REQUIRE(processor.read_spectrum().sequence_number > 0);
    REQUIRE(canvas->command_count() > static_analyzer_commands + 200);

    // Malformed paint geometry must fail before touching the authoritative
    // field or revision. Exercise the same attached JS-to-C++ endpoint used by
    // @pulp/react, rather than calling the product handler directly.
    const auto invalid_gain = processor.field().bands[4].gain_db;
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
    REQUIRE(processor.native_editor_revision() == 0);

    SECTION("tap and sculpt round-trip through C++ exactly once and reload hydrate") {
        constexpr int edited_band = 4;
        REQUIRE_FALSE(processor.field().bands[edited_band].muted);
        REQUIRE(processor.native_editor_revision() == 0);

        tap_band(*root, *session, edited_band);
        REQUIRE(processor.field().bands[edited_band].muted);
        REQUIRE(processor.native_editor_revision() == 1);

        std::string error;
        REQUIRE(session->reload(&error));
        root->layout_children();
        REQUIRE(processor.native_editor_revision() == 1);
        REQUIRE(processor.field().bands[edited_band].muted);

        // A reload retires the realm that opened this gesture. The new realm's
        // state handshake must clear its C++ snapshot, so paint cannot resume.
        session->bridge()->load_script(R"js(
          globalThis.__spectrEditorDispatch(JSON.stringify({type:'paint_start', payload:{}}));
        )js", "spectr-native-open-paint");
        REQUIRE(session->reload(&error));
        session->bridge()->load_script(R"js(
          const rejected = JSON.parse(globalThis.__spectrEditorDispatch(JSON.stringify({type:'paint', payload:{
            mode:'Sculpt', start_band:4, start_value:0,
            current_band:4, current_value:12, n_visible:32
          }})));
          if (rejected.ok !== false) throw new Error('reload resumed retired paint');
        )js", "spectr-native-retired-paint");
        REQUIRE(processor.native_editor_revision() == 1);

        // The post-reload tap toggles from hydrated true to false. A JS-local
        // default would send true again and this assertion would fail.
        tap_band(*root, *session, edited_band);
        REQUIRE_FALSE(processor.field().bands[edited_band].muted);
        REQUIRE(processor.native_editor_revision() == 2);

        auto& band = native_band(*session, edited_band);
        root->simulate_drag(root_point(band, band.bounds().height * 0.5f),
                            root_point(band, 1.0f), 1);

        REQUIRE(processor.field().bands[edited_band].gain_db > 20.0f);
        REQUIRE(processor.native_editor_revision() == 3);
        const auto sculpted_gain = processor.field().bands[edited_band].gain_db;

        REQUIRE(session->reload(&error));
        root->layout_children();
        REQUIRE(processor.native_editor_revision() == 3);
        REQUIRE(processor.field().bands[edited_band].gain_db
                == Catch::Approx(sculpted_gain));
    }

    const auto revision_before_close = processor.native_editor_revision();
    processor.on_view_closed(*root);
    REQUIRE(processor.active_scripted_ui() == nullptr);
    root.reset();

    auto reopened = processor.create_view();
    REQUIRE(reopened != nullptr);
    REQUIRE(processor.native_editor_revision() == revision_before_close);
    processor.on_view_closed(*reopened);
}
