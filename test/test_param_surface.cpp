// spectr#34 — host parameter surface.
//
// Phase 1 (registration contract) deliberately queries the store by literal
// ID so the file compiles against the pre-#34 two-parameter world and fails
// at runtime, proving the surface is absent before the implementation lands:
// a green run here before the change would mean the tests assert nothing.
//
// Coverage:
//   - The full static surface is registered: 64 band gains, 64 band mutes,
//     A/B morph, viewport center/width, band count, four mode toggles, plus
//     the legacy Mix/Output — 138 parameters, no more, no less.
//   - IDs follow the documented scheme (docs/parameter-surface.md) and the
//     reserved ranges stay empty.
//   - Names are zero-padded so host menus sort lexicographically.
//   - Groups exist so hosts with group support can cluster the surface.
//   - Ranges/defaults/kinds match the scheme (gain ±24 dB, mute toggle,
//     morph 0..1, log-frequency viewport, stepped band count, enum modes).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/state/store.hpp>
#include <pulp/format/plugin_state_io.hpp>

#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using Catch::Approx;

namespace {

struct Wired {
    pulp::state::StateStore       store;
    std::unique_ptr<spectr::Spectr> proc;

    Wired() : proc(std::make_unique<spectr::Spectr>()) {
        proc->set_state_store(&store);
        proc->define_parameters(store);
    }
};

// The documented ID scheme (docs/parameter-surface.md). Tests use the
// literals, not the production constants, so a careless constant edit in
// the implementation cannot silently move the test's expectations.
constexpr pulp::state::ParamID kGainBase = 1000;
constexpr pulp::state::ParamID kMuteBase = 2000;
constexpr pulp::state::ParamID kMorphId = 3000;
constexpr pulp::state::ParamID kViewportCenterId = 3001;
constexpr pulp::state::ParamID kViewportWidthId = 3002;
constexpr pulp::state::ParamID kBandCountId = 3003;
constexpr pulp::state::ParamID kMotionModeId = 3100;
constexpr pulp::state::ParamID kAnalyzerModeId = 3101;
constexpr pulp::state::ParamID kEditModeId = 3102;
constexpr pulp::state::ParamID kVisualizationId = 3103;

constexpr std::size_t kExpectedParamCount = 138;

const pulp::state::ParamInfo* find(const pulp::state::StateStore& store,
                                   pulp::state::ParamID id) {
    return store.info(id);
}

} // namespace

TEST_CASE("#34: the full static parameter surface is registered") {
    Wired w;
    CHECK(w.store.param_count() == kExpectedParamCount);

    // Legacy parameters keep their shipped IDs — the compatibility contract
    // starts with what already shipped.
    REQUIRE(find(w.store, 1) != nullptr);
    REQUIRE(find(w.store, 2) != nullptr);

    // All 64 band gain and band mute slots, unconditionally — the set is
    // static even though the visible band count varies at runtime.
    for (std::size_t i = 0; i < 64; ++i) {
        INFO("missing band gain slot " << i);
        CHECK(find(w.store, kGainBase + static_cast<pulp::state::ParamID>(i)) != nullptr);
        INFO("missing band mute slot " << i);
        CHECK(find(w.store, kMuteBase + static_cast<pulp::state::ParamID>(i)) != nullptr);
    }

    CHECK(find(w.store, kMorphId) != nullptr);
    CHECK(find(w.store, kViewportCenterId) != nullptr);
    CHECK(find(w.store, kViewportWidthId) != nullptr);
    CHECK(find(w.store, kBandCountId) != nullptr);
    CHECK(find(w.store, kMotionModeId) != nullptr);
    CHECK(find(w.store, kAnalyzerModeId) != nullptr);
    CHECK(find(w.store, kEditModeId) != nullptr);
    CHECK(find(w.store, kVisualizationId) != nullptr);
}

TEST_CASE("#34: reserved ID ranges stay empty") {
    Wired w;
    // Negative control: the probe must be able to distinguish registered
    // from unregistered — id 1 (Mix) is always there.
    REQUIRE(find(w.store, 1) != nullptr);

    // Legacy global growth headroom between Output and the band block.
    CHECK(find(w.store, 3) == nullptr);
    CHECK(find(w.store, 99) == nullptr);
    // Reserved tails between the band blocks and the control block.
    CHECK(find(w.store, 1064) == nullptr);
    CHECK(find(w.store, 1999) == nullptr);
    CHECK(find(w.store, 2064) == nullptr);
    CHECK(find(w.store, 2999) == nullptr);
    CHECK(find(w.store, 3004) == nullptr);
    CHECK(find(w.store, 3099) == nullptr);
    CHECK(find(w.store, 3104) == nullptr);
    // LFO ranges are reserved for #36 and must NOT be registered yet.
    CHECK(find(w.store, 4000) == nullptr);
    CHECK(find(w.store, 4031) == nullptr);
    CHECK(find(w.store, 4100) == nullptr);
    CHECK(find(w.store, 4199) == nullptr);
    // Beyond the documented scheme entirely.
    CHECK(find(w.store, 9999) == nullptr);
}

TEST_CASE("#34: band parameter names are zero-padded and grouped") {
    Wired w;

    const auto* g1 = find(w.store, kGainBase);
    const auto* g2 = find(w.store, kGainBase + 1);
    const auto* g64 = find(w.store, kGainBase + 63);
    REQUIRE(g1 != nullptr);
    REQUIRE(g2 != nullptr);
    REQUIRE(g64 != nullptr);
    CHECK(g1->name == "Band 01 Gain");
    CHECK(g2->name == "Band 02 Gain");
    CHECK(g64->name == "Band 64 Gain");

    const auto* m1 = find(w.store, kMuteBase);
    const auto* m64 = find(w.store, kMuteBase + 63);
    REQUIRE(m1 != nullptr);
    REQUIRE(m64 != nullptr);
    CHECK(m1->name == "Band 01 Mute");
    CHECK(m64->name == "Band 64 Mute");

    // Lexicographic ordering of the names must match band ordering — the
    // reason the names are zero-padded in the first place.
    std::vector<std::string> gain_names;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto* p = find(w.store, kGainBase + static_cast<pulp::state::ParamID>(i));
        REQUIRE(p != nullptr);
        gain_names.push_back(p->name);
    }
    CHECK(std::is_sorted(gain_names.begin(), gain_names.end()));

    // Every band parameter carries a non-root group id, and the store knows
    // the groups. Hosts without group support fall back to the names.
    CHECK(g1->group_id != 0);
    CHECK(m1->group_id != 0);
    CHECK(g1->group_id != m1->group_id);
    CHECK(w.store.all_groups().size() == 6);
    REQUIRE(find(w.store, 1) != nullptr);
    REQUIRE(find(w.store, 2) != nullptr);
    CHECK(find(w.store, 1)->group_id == 1);
    CHECK(find(w.store, 2)->group_id == 1);
}

TEST_CASE("#34: ranges, defaults, and kinds match the scheme") {
    Wired w;

    const auto* mix = find(w.store, 1);
    REQUIRE(mix != nullptr);
    CHECK(mix->range.min == Approx(0.0f));
    CHECK(mix->range.max == Approx(100.0f));
    CHECK(mix->range.default_value == Approx(100.0f));

    const auto* out = find(w.store, 2);
    REQUIRE(out != nullptr);
    CHECK(out->range.min == Approx(-24.0f));
    CHECK(out->range.max == Approx(24.0f));
    CHECK(out->range.default_value == Approx(0.0f));

    const auto* gain = find(w.store, kGainBase);
    REQUIRE(gain != nullptr);
    CHECK(gain->range.min == Approx(-24.0f));
    CHECK(gain->range.max == Approx(24.0f));
    CHECK(gain->range.default_value == Approx(0.0f));
    CHECK(gain->unit == "dB");
    CHECK(gain->kind == pulp::state::ParamKind::Continuous);

    const auto* mute = find(w.store, kMuteBase);
    REQUIRE(mute != nullptr);
    CHECK(mute->kind == pulp::state::ParamKind::Toggle);
    CHECK(mute->range.min == Approx(0.0f));
    CHECK(mute->range.max == Approx(1.0f));
    CHECK(mute->range.default_value == Approx(0.0f));

    const auto* morph = find(w.store, kMorphId);
    REQUIRE(morph != nullptr);
    CHECK(morph->range.min == Approx(0.0f));
    CHECK(morph->range.max == Approx(1.0f));
    CHECK(morph->range.default_value == Approx(0.0f));
    CHECK(morph->kind == pulp::state::ParamKind::Continuous);

    // Viewport: log10-frequency domain matching the display mapping
    // (pattern.cpp maps log10(20)..log10(20000)).
    const auto* center = find(w.store, kViewportCenterId);
    REQUIRE(center != nullptr);
    CHECK(center->range.min == Approx(1.30103f).margin(0.0001f));
    CHECK(center->range.max == Approx(4.30103f).margin(0.0001f));
    // Default center is the log midpoint of 20..20000.
    CHECK(center->range.default_value == Approx(2.80103f).margin(0.0001f));

    const auto* width = find(w.store, kViewportWidthId);
    REQUIRE(width != nullptr);
    CHECK(width->range.default_value == Approx(3.0f).margin(0.0001f));
    CHECK(width->range.max == Approx(3.0f).margin(0.0001f));
    CHECK(width->range.min > 0.0f);

    // Band count is a stepped control over the five legal layouts.
    const auto* count = find(w.store, kBandCountId);
    REQUIRE(count != nullptr);
    CHECK(count->range.min == Approx(32.0f));
    CHECK(count->range.max == Approx(64.0f));
    CHECK(count->range.step == Approx(8.0f));
    CHECK(count->range.default_value == Approx(32.0f));
    CHECK(count->kind == pulp::state::ParamKind::Integer);

    // Mode toggles are enums with display labels.
    const auto* motion = find(w.store, kMotionModeId);
    REQUIRE(motion != nullptr);
    CHECK(motion->name == "Motion Mode");
    CHECK(motion->kind == pulp::state::ParamKind::Enum);
    REQUIRE(motion->value_labels.size() == 2);
    CHECK(motion->value_labels[0] == "Live");
    CHECK(motion->value_labels[1] == "Precision");

    const auto* analyzer = find(w.store, kAnalyzerModeId);
    REQUIRE(analyzer != nullptr);
    CHECK(analyzer->kind == pulp::state::ParamKind::Enum);
    REQUIRE(analyzer->value_labels.size() == 4);
    CHECK(analyzer->value_labels[0] == "Peak");
    CHECK(analyzer->value_labels[1] == "Avg");
    CHECK(analyzer->value_labels[2] == "Both");
    CHECK(analyzer->value_labels[3] == "Off");

    const auto* edit = find(w.store, kEditModeId);
    REQUIRE(edit != nullptr);
    CHECK(edit->kind == pulp::state::ParamKind::Enum);
    REQUIRE(edit->value_labels.size() == 5);
    CHECK(edit->value_labels[0] == "Sculpt");
    CHECK(edit->value_labels[4] == "Glide");

    const auto* vis = find(w.store, kVisualizationId);
    REQUIRE(vis != nullptr);
    CHECK(vis->kind == pulp::state::ParamKind::Enum);
    REQUIRE(vis->value_labels.size() == 3);
    CHECK(vis->value_labels[0] == "Bars");
    CHECK(vis->value_labels[1] == "Response");
    CHECK(vis->value_labels[2] == "Both");
    CHECK(vis->range.default_value == Approx(2.0f));
}

TEST_CASE("#34: viewport parameters round-trip without inverted edges") {
    const spectr::Viewport authored{120.0f, 7200.0f};
    const auto [center, width] = spectr::encode_viewport(authored);
    const auto decoded = spectr::decode_viewport(center, width);

    CHECK(decoded.min_hz == Approx(authored.min_hz).epsilon(0.0001f));
    CHECK(decoded.max_hz == Approx(authored.max_hz).epsilon(0.0001f));

    const auto clamped = spectr::decode_viewport(-100.0f, -1.0f);
    CHECK(clamped.valid());
    CHECK(clamped.min_hz >= 20.0f);
    CHECK(clamped.max_hz <= 20000.0f);
}

TEST_CASE("#34: host parameter writes reach canonical processing state") {
    Wired w;
    w.store.set_value(kGainBase + 7, -9.5f);
    w.store.set_value(kMuteBase + 7, 1.0f);
    w.store.set_value(kViewportCenterId, std::log10(1000.0f));
    w.store.set_value(kViewportWidthId, std::log10(4.0f));
    w.store.set_value(kBandCountId, 56.0f);

    w.proc->apply_surface_params(false);

    CHECK(w.proc->field().bands[7].gain_db == Approx(-9.5f));
    CHECK(w.proc->field().bands[7].muted);
    CHECK(w.proc->viewport().min_hz == Approx(500.0f).epsilon(0.0001f));
    CHECK(w.proc->viewport().max_hz == Approx(2000.0f).epsilon(0.0001f));
    CHECK(w.proc->layout() == spectr::Layout::Bands56);
}

TEST_CASE("#34: canonical edits push only changed host parameters") {
    Wired w;
    std::vector<pulp::state::ParamID> changed;
    std::vector<pulp::state::ParamID> gesture_begins;
    std::vector<pulp::state::ParamID> gesture_ends;
    w.store.set_gesture_callbacks(
        [&gesture_begins](pulp::state::ParamID id) { gesture_begins.push_back(id); },
        [&gesture_ends](pulp::state::ParamID id) { gesture_ends.push_back(id); });
    auto listener = w.store.add_listener(
        [&changed](pulp::state::ParamID id, float) { changed.push_back(id); },
        pulp::state::ListenerThread::Main);

    auto field = w.proc->field();
    field.bands[2].gain_db = 4.0f;
    field.bands[9].muted = true;
    REQUIRE(w.proc->replace_processing_state(
        field, {100.0f, 6400.0f}, spectr::Layout::Bands48));

    CHECK(w.store.get_value(kGainBase + 2) == Approx(4.0f));
    CHECK(w.store.get_value(kMuteBase + 9) == Approx(1.0f));
    CHECK(w.store.get_value(kBandCountId) == Approx(48.0f));
    CHECK(std::count(changed.begin(), changed.end(), kGainBase + 2) == 1);
    CHECK(std::count(changed.begin(), changed.end(), kMuteBase + 9) == 1);
    CHECK(std::count(changed.begin(), changed.end(), kGainBase + 3) == 0);
    CHECK(changed.size() == 5); // gain, mute, center, width, and band count
    CHECK(gesture_begins == changed);
    CHECK(gesture_ends == changed);
    CHECK(w.store.open_gesture_count() == 0);

    REQUIRE(w.proc->replace_processing_state(
        field, {100.0f, 6400.0f}, spectr::Layout::Bands48));
    CHECK(changed.size() == 5); // an identical publication emits nothing
    CHECK(gesture_begins.size() == 5);
    CHECK(gesture_ends.size() == 5);
}

TEST_CASE("#34: process hands host automation to the control worker") {
    Wired w;
    pulp::format::PrepareContext prepare;
    prepare.sample_rate = 48000.0;
    prepare.max_buffer_size = 32;
    prepare.input_channels = 2;
    prepare.output_channels = 2;
    w.proc->prepare(prepare);

    w.store.set_value_rt(kGainBase + 5, -7.0f);

    std::array<float, 32> in_left{};
    std::array<float, 32> in_right{};
    std::array<float, 32> out_left{};
    std::array<float, 32> out_right{};
    const float* inputs[] = {in_left.data(), in_right.data()};
    float* outputs[] = {out_left.data(), out_right.data()};
    pulp::audio::BufferView<const float> input(inputs, 2, 32);
    pulp::audio::BufferView<float> output(outputs, 2, 32);
    pulp::midi::MidiBuffer midi_in;
    pulp::midi::MidiBuffer midi_out;
    pulp::format::ProcessContext context;
    context.sample_rate = 48000.0;
    context.num_samples = 32;
    w.proc->process(output, input, midi_in, midi_out, context);

    bool applied = false;
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (w.proc->processing_state_snapshot().field.bands[5].gain_db == -7.0f) {
            applied = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(applied);
    w.proc->release();
}

TEST_CASE("#34: paint gesture epochs close on end cancel and reset") {
    Wired w;

    auto push_one_band = [&] {
        auto field = w.proc->field();
        field.bands[3].gain_db += 1.0f;
        REQUIRE(w.proc->replace_processing_state(
            field, w.proc->viewport(), w.proc->layout()));
    };

    REQUIRE(w.proc->editor_authority().begin_band_edit().accepted);
    push_one_band();
    CHECK(w.store.open_gesture_count() == 1);
    CHECK(w.proc->editor_authority().end_band_edit().accepted);
    CHECK(w.store.open_gesture_count() == 0);

    REQUIRE(w.proc->editor_authority().begin_band_edit().accepted);
    push_one_band();
    CHECK(w.store.open_gesture_count() == 1);
    CHECK(w.proc->editor_authority().cancel_band_edit().accepted);
    CHECK(w.store.open_gesture_count() == 0);

    REQUIRE(w.proc->editor_authority().begin_band_edit().accepted);
    push_one_band();
    CHECK(w.store.open_gesture_count() == 1);
    w.proc->editor_authority().reset_transient_state();
    CHECK(w.store.open_gesture_count() == 0);
}

TEST_CASE("#34: v3 state restores morph derivation and sparse overrides") {
    Wired a;

    spectr::BandField field_a;
    for (auto& band : field_a.bands) band.gain_db = -10.0f;
    a.proc->replace_field(field_a);
    a.proc->capture_snapshot(spectr::SnapshotBank::Slot::A);

    spectr::BandField field_b;
    for (auto& band : field_b.bands) band.gain_db = 10.0f;
    a.proc->replace_field(field_b);
    a.proc->capture_snapshot(spectr::SnapshotBank::Slot::B);

    a.proc->apply_morph_to_live(0.5f);
    auto post_morph = a.proc->processing_state_snapshot();
    post_morph.field.bands[3].gain_db = -4.0f;
    REQUIRE(a.proc->replace_processing_state(
        post_morph.field, post_morph.viewport, post_morph.layout));

    const auto blob = pulp::format::plugin_state_io::serialize(a.store, *a.proc);
    REQUIRE_FALSE(blob.empty());

    Wired b;
    REQUIRE(pulp::format::plugin_state_io::deserialize(blob, b.store, *b.proc));
    const auto restored = b.proc->processing_state_snapshot();
    CHECK(restored.field.bands[0].gain_db == Approx(0.0f));
    CHECK(restored.field.bands[3].gain_db == Approx(-4.0f));
    CHECK(restored.field.bands[63].gain_db == Approx(0.0f));
    CHECK(b.store.get_value(kMorphId) == Approx(0.5f));
    CHECK(b.store.get_value(kGainBase + 3) == Approx(-4.0f));
}

TEST_CASE("#34: simultaneous morph and band automation preserves the band lane") {
    Wired w;

    spectr::BandField field_a;
    for (auto& band : field_a.bands) band.gain_db = -10.0f;
    w.proc->replace_field(field_a);
    w.proc->capture_snapshot(spectr::SnapshotBank::Slot::A);

    spectr::BandField field_b;
    for (auto& band : field_b.bands) band.gain_db = 10.0f;
    w.proc->replace_field(field_b);
    w.proc->capture_snapshot(spectr::SnapshotBank::Slot::B);

    // Both writes land before one reconciliation, as they can in a host block.
    w.store.set_value(kMorphId, 0.5f);
    w.store.set_value(kGainBase + 7, -6.0f);
    w.proc->apply_surface_params(true);

    const auto state = w.proc->processing_state_snapshot();
    CHECK(state.field.bands[0].gain_db == Approx(0.0f));
    CHECK(state.field.bands[7].gain_db == Approx(-6.0f));
}
