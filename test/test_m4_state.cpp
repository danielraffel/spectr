// Milestone 4 — State registration tests (pulp#625 / PR#628).
//
// Verifies the supplemental plugin-state contract:
// - Round-trip through pulp::format::plugin_state_io::serialize/deserialize.
// - Legacy StateStore-only blobs call deserialize_plugin_state with empty
//   span and reset supplemental state to defaults.
// - Version mismatch in the JSON payload is rejected.
// - Malformed JSON is rejected.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/plugin_state_io.hpp>

#include "spectr/spectr.hpp"

#include <cstring>
#include <span>
#include <string>
#include <string_view>
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

} // namespace

TEST_CASE("M4: every V1 parameter is registered") {
    Wired w;
    auto params = w.store.all_params();
    // At least the 5 V1 parameters should be present.
    CHECK(params.size() >= 5);

    const std::vector<pulp::state::ParamID> expected = {
        spectr::kMix, spectr::kOutputTrim, spectr::kResponseMode,
        spectr::kEngineMode, spectr::kBandCount,
    };
    for (auto id : expected) {
        bool found = false;
        for (const auto& p : params) if (p.id == id) { found = true; break; }
        INFO("Missing parameter id " << id);
        CHECK(found);
    }
}

TEST_CASE("M4: serialize_plugin_state produces non-empty JSON") {
    Wired w;
    auto blob = w.proc->serialize_plugin_state();
    CHECK(!blob.empty());
    std::string_view text(reinterpret_cast<const char*>(blob.data()), blob.size());
    CHECK(text.find("version") != std::string_view::npos);
    CHECK(text.find("band_gain") != std::string_view::npos);
    CHECK(text.find("band_mute") != std::string_view::npos);
    CHECK(text.find("view_min_hz") != std::string_view::npos);
    CHECK(text.find("view_max_hz") != std::string_view::npos);
}

TEST_CASE("M4: plugin_state round-trips band gains, mutes, and viewport") {
    Wired a;
    // Set non-default state.
    a.proc->field().bands[5].gain_db  = -12.0f;
    a.proc->field().bands[10].muted   = true;
    a.proc->field().bands[63].gain_db =  +6.0f;
    a.proc->viewport().min_hz = 200.0f;
    a.proc->viewport().max_hz = 8000.0f;
    a.proc->set_layout(spectr::Layout::Bands48);

    // Serialize through the plugin_state_io wrapper (the same adapters
    // will use in host save/load paths).
    auto blob = pulp::format::plugin_state_io::serialize(a.store, *a.proc);
    REQUIRE(!blob.empty());

    // Restore into a fresh processor.
    Wired b;
    const bool ok = pulp::format::plugin_state_io::deserialize(blob, b.store, *b.proc);
    REQUIRE(ok);

    CHECK(b.proc->field().bands[5].gain_db  == Approx(-12.0f));
    CHECK(b.proc->field().bands[10].muted   == true);
    CHECK(b.proc->field().bands[63].gain_db == Approx(+6.0f));
    CHECK(b.proc->viewport().min_hz         == Approx(200.0f));
    CHECK(b.proc->viewport().max_hz         == Approx(8000.0f));
    CHECK(b.proc->layout()                  == spectr::Layout::Bands48);
}

TEST_CASE("M4: legacy StateStore-only blob restores params and resets supplemental state") {
    // Build a legacy-style payload: StateStore alone, no supplemental JSON.
    Wired a;
    a.proc->field().bands[7].gain_db = -18.0f;
    a.proc->field().bands[8].muted   = true;
    a.proc->viewport().max_hz = 5000.0f;

    // Raw StateStore blob (PULP magic) — exactly what PR#628's wrapper
    // passes through unchanged when plugin_state is empty.
    const auto legacy_blob = a.store.serialize();

    // Restore into a fresh processor whose state we've already dirtied —
    // supplemental fields should reset to defaults because there's no
    // supplemental payload.
    Wired b;
    b.proc->field().bands[20].gain_db = -24.0f;
    b.proc->field().bands[21].muted   = true;
    b.proc->viewport().min_hz = 100.0f;

    const bool ok = pulp::format::plugin_state_io::deserialize(
        std::span<const uint8_t>(legacy_blob), b.store, *b.proc);
    REQUIRE(ok);

    // b's supplemental state should be defaults (legacy blob carries none).
    for (const auto& band : b.proc->field().bands) {
        CHECK(band.gain_db == Approx(0.0f));
        CHECK_FALSE(band.muted);
    }
    CHECK(b.proc->viewport().min_hz == Approx(20.0f));
    CHECK(b.proc->viewport().max_hz == Approx(20000.0f));
    CHECK(b.proc->layout() == spectr::Layout::Bands32);
}

TEST_CASE("M4: deserialize_plugin_state rejects version mismatch") {
    Wired w;
    std::string bad = R"({"version": 99, "band_gain": [], "band_mute": []})";
    std::vector<uint8_t> bytes(bad.begin(), bad.end());
    CHECK_FALSE(w.proc->deserialize_plugin_state(bytes));
}

TEST_CASE("M4: deserialize_plugin_state rejects malformed JSON") {
    Wired w;
    const std::string garbage = "not json at all";
    std::vector<uint8_t> bytes(garbage.begin(), garbage.end());
    CHECK_FALSE(w.proc->deserialize_plugin_state(bytes));
}

TEST_CASE("M4: deserialize_plugin_state on empty span resets to defaults") {
    Wired w;
    // Dirty the state.
    w.proc->field().bands[0].gain_db = -10.0f;
    w.proc->field().bands[1].muted   = true;
    w.proc->viewport().min_hz = 40.0f;

    CHECK(w.proc->deserialize_plugin_state(std::span<const uint8_t>{}));

    // Defaults restored.
    CHECK(w.proc->field().bands[0].gain_db == Approx(0.0f));
    CHECK_FALSE(w.proc->field().bands[1].muted);
    CHECK(w.proc->viewport().min_hz == Approx(20.0f));
}

TEST_CASE("M4: host param automation reaches the engine on process()") {
    // Wire a fresh Spectr and drive it by setting store params, then
    // verifying that process() picks up the new layout / mode.
    Wired w;
    pulp::format::PrepareContext pc;
    pc.sample_rate     = 48000.0;
    pc.max_buffer_size = 256;
    pc.input_channels  = 2;
    pc.output_channels = 2;
    w.proc->prepare(pc);

    // Default layout is 32-band (Layout::Bands32). Switch param to 64-band
    // and confirm process() picks it up.
    w.store.set_value(spectr::kBandCount, 4.0f);  // index 4 = Bands64

    // Minimal buffers.
    std::vector<float> in0(256), in1(256), out0(256), out1(256);
    const float* in_ptrs[2]  = {in0.data(), in1.data()};
    float*       out_ptrs[2] = {out0.data(), out1.data()};
    pulp::audio::BufferView<const float> iv(in_ptrs, 2, 256);
    pulp::audio::BufferView<float>       ov(out_ptrs, 2, 256);
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::format::ProcessContext ctx;
    ctx.sample_rate = 48000.0;
    ctx.num_samples = 256;

    w.proc->process(ov, iv, midi_in, midi_out, ctx);
    CHECK(w.proc->layout() == spectr::Layout::Bands64);
}
