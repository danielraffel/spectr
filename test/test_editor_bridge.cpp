// Milestone 9.5 (slice 1) — JS ↔ C++ editor bridge.
//
// Unit tests for the message router. Every message type is exercised
// through the JSON envelope (i.e. the same path the WebView will
// drive). The `Spectr` plugin is wired up to observe side effects.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/editor_bridge.hpp"
#include "spectr/pattern.hpp"
#include "spectr/preset_format.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/spectr.hpp"
#include "spectr/ui/editor_view.hpp"

#include "spectr_editor_assets_data.hpp"

#include <pulp/state/store.hpp>
#include <pulp/runtime/build_info.hpp>
#include <pulp/view/script_engine.hpp>

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <memory>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Approx;
using spectr::BandField;
using spectr::SnapshotBank;
using spectr::Spectr;
using spectr::register_spectr_editor_handlers;

namespace {

struct Rig {
    pulp::state::StateStore       store;
    std::unique_ptr<Spectr>       proc;
    pulp::view::EditorBridge      bridge;

    explicit Rig(spectr::ClipboardWriter clipboard_writer = {})
        : proc(std::make_unique<Spectr>()) {
        proc->set_state_store(&store);
        proc->define_parameters(store);
        // Production registers the processor-owned library so pattern edits
        // participate in the plugin state blob. Keep the bridge oracle wired
        // to that same authority instead of a detached test-only library.
        register_spectr_editor_handlers(
            bridge, *proc, proc->patterns(), proc->editor_authority(),
            std::move(clipboard_writer));
    }

    std::string dispatch(std::string_view envelope_json) {
        return bridge.dispatch_json(envelope_json);
    }
};

// choc::json::toString emits `"ok": true/false` with a space after
// the colon — match both with/without space so these helpers stay
// robust if the bridge switches to a no-space emitter later.
bool response_ok(const std::string& r) {
    return r.find("\"ok\": true")  != std::string::npos
        || r.find("\"ok\":true")   != std::string::npos;
}

bool response_has_error(const std::string& r, std::string_view substr) {
    const bool not_ok = r.find("\"ok\": false") != std::string::npos
                     || r.find("\"ok\":false")  != std::string::npos;
    return not_ok && r.find(substr) != std::string::npos;
}

std::string field_envelope(std::uint32_t n,
                           std::uint32_t special_band = 0,
                           float special_gain = 0.0f,
                           bool special_muted = false,
                           bool all_muted = false) {
    auto gains = choc::value::createEmptyArray();
    auto mutes = choc::value::createEmptyArray();
    for (std::uint32_t i = 0; i < n; ++i) {
        gains.addArrayElement(static_cast<double>(i == special_band ? special_gain : 0.0f));
        mutes.addArrayElement(all_muted || (i == special_band && special_muted));
    }
    auto payload = choc::value::createObject("BandFieldPayload");
    payload.addMember("n_visible", static_cast<std::int32_t>(n));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", mutes);
    auto envelope = choc::value::createObject("Envelope");
    envelope.addMember("type", "band_field_set");
    envelope.addMember("payload", payload);
    return choc::json::toString(envelope, false);
}

std::string processing_state_envelope(std::uint32_t n,
                                      float min_hz,
                                      float max_hz,
                                      std::uint32_t special_band = 0,
                                      float special_gain = 0.0f,
                                      bool special_muted = false,
                                      bool mute_edges = false) {
    auto gains = choc::value::createEmptyArray();
    auto mutes = choc::value::createEmptyArray();
    for (std::uint32_t i = 0; i < n; ++i) {
        gains.addArrayElement(
            static_cast<double>(i == special_band ? special_gain : 0.0f));
        mutes.addArrayElement((i == special_band && special_muted)
                            || (mute_edges && (i == 0 || i + 1 == n)));
    }
    auto payload = choc::value::createObject("ProcessingStatePayload");
    payload.addMember("n_visible", static_cast<std::int32_t>(n));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", mutes);
    payload.addMember("min_hz", static_cast<double>(min_hz));
    payload.addMember("max_hz", static_cast<double>(max_hz));
    auto envelope = choc::value::createObject("Envelope");
    envelope.addMember("type", "processing_state_set");
    envelope.addMember("payload", payload);
    return choc::json::toString(envelope, false);
}

} // namespace

TEST_CASE("native editor bridge exposes truthful build information and copy feedback") {
    bool copied = false;
    std::string copied_text;
    Rig r([&](std::string_view text) {
        copied = true;
        copied_text = text;
        return true;
    });

    const auto response = choc::json::parse(
        r.dispatch(R"({"type":"build_info_get","payload":{}})"));
    REQUIRE(response.isObject());
    REQUIRE(response["ok"].getBool());
    CHECK(response["product_version"].get<std::string>()
          == r.proc->descriptor().version);
    CHECK(response["sdk_version"].get<std::string>()
          == pulp::runtime::kSdkVersion);
    CHECK(response["sdk_dirty"].getBool() == pulp::runtime::kGitDirty);
    constexpr std::string_view product_sha{SPECTR_PRODUCT_GIT_SHA};
    if (product_sha.empty()) {
        CHECK_FALSE(response.hasObjectMember("product_sha"));
        CHECK_FALSE(response["product_provenance_known"].getBool());
    } else {
        REQUIRE(response.hasObjectMember("product_sha"));
        CHECK(response["product_sha"].get<std::string>() == product_sha);
        CHECK(response["product_provenance_known"].getBool());
    }
    CHECK(response["product_dirty"].getBool()
          == static_cast<bool>(SPECTR_PRODUCT_GIT_DIRTY));
    REQUIRE(response.hasObjectMember("copy_text"));
    CHECK(response["copy_text"].get<std::string>().find("Spectr: ") == 0);
    CHECK(response["copy_text"].get<std::string>().find("Pulp SDK: ")
          != std::string::npos);
    CHECK(response["copy_text"].get<std::string>().find(
              std::string{"Spectr SHA: "}
              + (product_sha.empty() ? "unknown" : std::string{product_sha}))
          != std::string::npos);
    CHECK(response["copy_text"].get<std::string>().find(
              std::string{"Spectr source: "}
              + (product_sha.empty() ? "unknown"
                 : SPECTR_PRODUCT_GIT_DIRTY ? "dirty" : "clean"))
          != std::string::npos);
    CHECK(response["copy_text"].get<std::string>().find(
              std::string{"Pulp SDK SHA: "} + SPECTR_PULP_SDK_SOURCE_GIT_SHA)
          != std::string::npos);
    CHECK(response["copy_text"].get<std::string>().find("Pulp SDK source: ")
          != std::string::npos);

    const auto copy_response = r.dispatch(
        R"({"type":"build_info_copy","payload":{}})");
    REQUIRE(response_ok(copy_response));
    CHECK(copied);
    CHECK(copied_text == response["copy_text"].get<std::string>());
}

TEST_CASE("native editor bridge reports clipboard refusal") {
    Rig r([](std::string_view) { return false; });
    const auto response = r.dispatch(
        R"({"type":"build_info_copy","payload":{}})");
    CHECK(response_has_error(response, "clipboard unavailable"));
}

TEST_CASE("native editor bridge: complete field preserves exact mute and layout") {
    Rig r;
    r.proc->field().bands[63].gain_db = 7.0f;

    const auto response = r.dispatch(field_envelope(40, 7, -13.5f, true));
    REQUIRE(response_ok(response));
    CHECK(r.proc->layout() == spectr::Layout::Bands40);
    CHECK(r.proc->field().bands[7].gain_db == Approx(-13.5f));
    CHECK(r.proc->field().bands[7].muted);
    CHECK(r.proc->field().linear_gain(7) == 0.0f);
    CHECK(r.proc->field().bands[63].gain_db == Approx(7.0f));
}

TEST_CASE("editor authority owns one monotonic revision across state snapshots and morph") {
    Rig r;
    auto& authority = r.proc->editor_authority();
    REQUIRE(authority.revision() == 0);

    auto field = r.proc->field();
    field.bands[3].gain_db = -9.0f;
    const auto replace = authority.replace_processing_state(
        field, {80.0f, 8000.0f}, spectr::Layout::Bands32, 0);
    REQUIRE(replace.accepted);
    REQUIRE(replace.revision == 1);

    const auto capture_a = authority.capture_snapshot(SnapshotBank::Slot::A, 1);
    REQUIRE(capture_a.accepted);
    REQUIRE(capture_a.revision == 2);

    field.bands[3].gain_db = 15.0f;
    const auto replace_b = authority.replace_processing_state(
        field, {80.0f, 8000.0f}, spectr::Layout::Bands32, 2);
    REQUIRE(replace_b.accepted);
    REQUIRE(replace_b.revision == 3);

    const auto capture_b = authority.capture_snapshot(SnapshotBank::Slot::B, 3);
    REQUIRE(capture_b.accepted);
    REQUIRE(capture_b.revision == 4);

    const auto morph = authority.apply_morph(0.5f, 4);
    REQUIRE(morph.accepted);
    REQUIRE(morph.revision == 5);
    REQUIRE(r.proc->field().bands[3].gain_db == Approx(3.0f));
}

TEST_CASE("editor authority rejects stale or invalid commands failure atomically") {
    Rig r;
    auto& authority = r.proc->editor_authority();
    auto field = r.proc->field();
    field.bands[9].gain_db = 6.0f;
    REQUIRE(authority.replace_processing_state(
        field, r.proc->viewport(), r.proc->layout(), 0).accepted);

    const auto before = r.proc->field();
    const auto before_viewport = r.proc->viewport();
    const auto before_layout = r.proc->layout();
    const auto revision = authority.revision();

    auto stale = before;
    stale.bands[9].gain_db = -17.0f;
    const auto stale_receipt = authority.replace_processing_state(
        stale, {300.0f, 600.0f}, spectr::Layout::Bands64, 0);
    REQUIRE_FALSE(stale_receipt.accepted);
    CHECK(stale_receipt.error == "stale editor revision");

    auto invalid = before;
    invalid.bands[9].gain_db = std::numeric_limits<float>::infinity();
    const auto invalid_receipt = authority.replace_processing_state(
        invalid, {300.0f, 600.0f}, spectr::Layout::Bands64, revision);
    REQUIRE_FALSE(invalid_receipt.accepted);
    CHECK(authority.revision() == revision);
    CHECK(r.proc->field().bands[9].gain_db == Approx(before.bands[9].gain_db));
    CHECK(r.proc->viewport().min_hz == Approx(before_viewport.min_hz));
    CHECK(r.proc->viewport().max_hz == Approx(before_viewport.max_hz));
    CHECK(r.proc->layout() == before_layout);
}

TEST_CASE("editor authority invalidates a captured gesture on concurrent mutation") {
    Rig r;
    auto& authority = r.proc->editor_authority();
    REQUIRE(authority.begin_band_edit(0).accepted);

    auto concurrent = r.proc->field();
    concurrent.bands[4].gain_db = -7.0f;
    REQUIRE(authority.replace_processing_state(
        concurrent, r.proc->viewport(), r.proc->layout(), 0).accepted);
    REQUIRE(authority.revision() == 1);

    spectr::DragGesture gesture;
    gesture.start_band = 4;
    gesture.current_band = 4;
    gesture.start_value = -7.0f;
    gesture.current_value = 12.0f;
    gesture.n_visible = 32;
    const auto stale_update = authority.update_band_edit(
        spectr::EditMode::Sculpt, gesture, 0);
    REQUIRE_FALSE(stale_update.accepted);
    CHECK(r.proc->field().bands[4].gain_db == Approx(-7.0f));
    CHECK(authority.revision() == 1);

    const auto retired_update = authority.update_band_edit(
        spectr::EditMode::Sculpt, gesture, 1);
    REQUIRE_FALSE(retired_update.accepted);
    CHECK(retired_update.error == "paint without paint_start");
}

TEST_CASE("native editor bridge publishes zoomed processing state atomically") {
    Rig r;
    const auto response = r.dispatch(
        processing_state_envelope(48, 280.0f, 340.0f, 17, -9.5f, true));

    REQUIRE(response_ok(response));
    CHECK(r.proc->layout() == spectr::Layout::Bands48);
    CHECK(r.proc->viewport().min_hz == Approx(280.0f));
    CHECK(r.proc->viewport().max_hz == Approx(340.0f));
    CHECK(r.proc->field().bands[17].gain_db == Approx(-9.5f));
    CHECK(r.proc->field().bands[17].muted);
    CHECK(r.proc->field().linear_gain(17) == 0.0f);
}

TEST_CASE("native editor hydration reports the restored field viewport and layout") {
    Rig r;
    r.proc->set_layout(spectr::Layout::Bands48);
    r.proc->viewport() = {280.0f, 340.0f};
    r.proc->field().bands[17] = {-9.5f, true};
    r.proc->field().bands[47] = {6.25f, false};
    r.proc->capture_snapshot(SnapshotBank::Slot::A);
    r.proc->field().bands[17] = {3.0f, false};
    r.proc->capture_snapshot(SnapshotBank::Slot::B);
    r.proc->field().bands[17] = {-9.5f, true};

    const auto message = spectr::make_editor_hydration_message(*r.proc);
    CHECK(message.type == "processing_state_hydrate");
    CHECK(message.id == "spectr-processing-state-hydrate");

    const auto payload = choc::json::parse(message.payload_json);
    REQUIRE(payload.isObject());
    CHECK(payload["n_visible"].get<int64_t>() == 48);
    CHECK(payload["gain_db"].size() == 48);
    CHECK(payload["muted"].size() == 48);
    CHECK(payload["gain_db"][17].get<double>() == Approx(-9.5));
    CHECK(payload["muted"][17].getBool());
    CHECK(payload["gain_db"][47].get<double>() == Approx(6.25));
    CHECK(payload["min_hz"].get<double>() == Approx(280.0));
    CHECK(payload["max_hz"].get<double>() == Approx(340.0));
    CHECK(payload["revision"].get<int64_t>() == 0);
    REQUIRE(payload["snapshots"].isObject());
    CHECK(payload["snapshots"]["A"]["populated"].getBool());
    CHECK(payload["snapshots"]["B"]["populated"].getBool());
    CHECK(payload["snapshots"]["A"]["gain_db"][17].get<double>() == Approx(-9.5));
    CHECK(payload["snapshots"]["A"]["muted"][17].getBool());
    CHECK(payload["snapshots"]["B"]["gain_db"][17].get<double>() == Approx(3.0));
}

TEST_CASE("native editor resolution disclosure uses current product geometry") {
    Rig r;
    pulp::format::PrepareContext prepare;
    prepare.sample_rate = 48000.0;
    prepare.max_buffer_size = 512;
    prepare.input_channels = 1;
    prepare.output_channels = 1;
    r.proc->prepare(prepare);
    r.proc->set_layout(spectr::Layout::Bands64);
    r.proc->viewport() = {280.0f, 340.0f};

    pulp::view::WebViewMessage message;
    REQUIRE(spectr::make_editor_resolution_message(*r.proc, message));
    CHECK(message.type == "spectral_resolution");
    CHECK(message.id == "spectr-spectral-resolution");

    const auto payload = choc::json::parse(message.payload_json);
    REQUIRE(payload.isObject());
    CHECK(payload["active_bands"].get<int64_t>() == 64);
    CHECK(payload["represented_bands"].get<int64_t>() >= 0);
    CHECK(payload["represented_bands"].get<int64_t>() <= 64);
    CHECK(payload["fft_size"].get<int64_t>() == spectr::kSpectralFftSize);
    CHECK(payload["sample_rate"].get<double>() == Approx(48000.0));
    CHECK(payload["fully_represented"].getBool()
          == (payload["represented_bands"].get<int64_t>() == 64));
}

TEST_CASE("native analyzer message is finite bounded and peak-preserving") {
    constexpr int fft_size = 8192;
    constexpr float sample_rate = 48000.0f;
    std::vector<float> bins(fft_size / 2 + 1, -120.0f);
    const auto peak_bin = static_cast<std::size_t>(
        std::lround(1000.0f * fft_size / sample_rate));
    bins[peak_bin] = -6.0f;

    const spectr::EditorAnalyzerSnapshot snapshot{
        .magnitude_db = bins,
        .epoch = 7,
        .sequence_number = 19,
        .dropped_frames = 2,
        .source_channels = 2,
        .fft_size = fft_size,
        .sample_rate = sample_rate,
        .floor_db = -120.0f,
    };
    pulp::view::WebViewMessage message;
    REQUIRE(spectr::make_editor_analyzer_message(
        snapshot, spectr::Viewport{}, message));
    CHECK(message.type == "analyzer_frame");
    CHECK(message.id == "spectr-analyzer-frame");

    const auto payload = choc::json::parse(message.payload_json);
    REQUIRE(payload.isObject());
    CHECK(payload["schema_version"].get<int64_t>() == 1);
    CHECK(payload["epoch"].get<int64_t>() == 7);
    CHECK(payload["sequence_number"].get<int64_t>() == 19);
    CHECK(payload["dropped_frames"].get<int64_t>() == 2);
    CHECK(payload["source_channels"].get<int64_t>() == 2);
    CHECK(payload["fft_size"].get<int64_t>() == fft_size);
    CHECK(payload["sample_rate"].get<double>() == Approx(sample_rate));
    CHECK(payload["floor_db"].get<double>() == Approx(-120.0));
    CHECK(payload["ceiling_db"].get<double>() == Approx(24.0));
    CHECK(payload["visible"]["magnitude_db"].size() == 321);
    CHECK(payload["overview"]["magnitude_db"].size() == 121);

    double visible_peak = -120.0;
    for (std::uint32_t i = 0;
         i < payload["visible"]["magnitude_db"].size(); ++i) {
        const auto value = payload["visible"]["magnitude_db"][i].get<double>();
        CHECK(std::isfinite(value));
        visible_peak = std::max(visible_peak, value);
    }
    CHECK(visible_peak == Approx(-6.0));

    const auto full_key = spectr::make_editor_analyzer_publication_key(
        snapshot, spectr::Viewport{});
    const spectr::Viewport zoomed{900.0f, 1100.0f};
    const auto zoomed_key = spectr::make_editor_analyzer_publication_key(
        snapshot, zoomed);
    CHECK_FALSE(full_key == zoomed_key);
    pulp::view::WebViewMessage zoomed_message;
    REQUIRE(spectr::make_editor_analyzer_message(
        snapshot, zoomed, zoomed_message));
    const auto zoomed_payload = choc::json::parse(zoomed_message.payload_json);
    CHECK(zoomed_payload["epoch"].get<int64_t>() == 7);
    CHECK(zoomed_payload["sequence_number"].get<int64_t>() == 19);
    CHECK(zoomed_payload["visible"]["min_hz"].get<double>() == Approx(900.0));
    CHECK(zoomed_payload["visible"]["max_hz"].get<double>() == Approx(1100.0));
    CHECK(zoomed_message.payload_json != message.payload_json);
}

TEST_CASE("native analyzer message rejects malformed input atomically") {
    std::vector<float> bins(33, -120.0f);
    spectr::EditorAnalyzerSnapshot snapshot{
        .magnitude_db = bins,
        .epoch = 1,
        .sequence_number = 1,
        .source_channels = 2,
        .fft_size = 64,
        .sample_rate = 48000.0f,
        .floor_db = -120.0f,
    };
    pulp::view::WebViewMessage message{
        .type = "sentinel", .payload_json = "sentinel", .id = "sentinel"};
    bins[4] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(spectr::make_editor_analyzer_message(
        snapshot, spectr::Viewport{}, message));
    CHECK(message.type == "sentinel");
    CHECK(message.payload_json == "sentinel");
    CHECK(message.id == "sentinel");

    bins[4] = -120.0f;
    snapshot.fft_size = 128; // 65 bins required, but only 33 were supplied.
    CHECK_FALSE(spectr::make_editor_analyzer_message(
        snapshot, spectr::Viewport{}, message));
    CHECK(message.type == "sentinel");
    CHECK(message.payload_json == "sentinel");
    CHECK(message.id == "sentinel");
}

TEST_CASE("native analyzer silence remains the finite floor") {
    std::vector<float> bins(513, -96.0f);
    const spectr::EditorAnalyzerSnapshot snapshot{
        .magnitude_db = bins,
        .epoch = 2,
        .sequence_number = 3,
        .source_channels = 2,
        .fft_size = 1024,
        .sample_rate = 48000.0f,
        .floor_db = -96.0f,
    };
    pulp::view::WebViewMessage message;
    REQUIRE(spectr::make_editor_analyzer_message(
        snapshot, spectr::Viewport{}, message));
    const auto payload = choc::json::parse(message.payload_json);
    for (const auto trace_name : {"visible", "overview"}) {
        const auto values = payload[trace_name]["magnitude_db"];
        for (std::uint32_t i = 0; i < values.size(); ++i)
            CHECK(values[i].get<double>() == Approx(-96.0));
    }
}

TEST_CASE("native editor resolution message is failure atomic") {
    Rig r;
    pulp::format::PrepareContext prepare;
    prepare.sample_rate = 48000.0;
    prepare.max_buffer_size = 512;
    prepare.input_channels = 1;
    prepare.output_channels = 1;
    r.proc->prepare(prepare);
    r.proc->viewport() = {500.0f, 400.0f};
    pulp::view::WebViewMessage message{
        .type = "sentinel",
        .payload_json = R"({"keep":true})",
        .id = "sentinel-id",
    };

    CHECK_FALSE(spectr::make_editor_resolution_message(*r.proc, message));
    CHECK(message.type == "sentinel");
    CHECK(message.payload_json == R"({"keep":true})");
    CHECK(message.id == "sentinel-id");
}

TEST_CASE("native editor resolution is unavailable before prepare") {
    Rig r;
    pulp::view::WebViewMessage message{
        .type = "sentinel",
        .payload_json = "null",
        .id = "sentinel-id",
    };
    CHECK_FALSE(spectr::make_editor_resolution_message(*r.proc, message));
    CHECK(message.type == "sentinel");
    CHECK(message.id == "sentinel-id");
}

TEST_CASE("embedded editor gates default publication until native hydration") {
    const std::string html(
        reinterpret_cast<const char*>(spectr_editor::editor_html),
        spectr_editor::editor_html_size);

    CHECK(html.find("window.pulp.on('processing_state_hydrate'")
          != std::string::npos);
    CHECK(html.find("window.pulp.postMessage('editor_ready'")
          != std::string::npos);
    CHECK(html.find("if (!nativeHydrated) return;")
          != std::string::npos);
    CHECK(html.find("if (nativeBridgeAvailable) return;")
          != std::string::npos);
    CHECK(html.find("mutedGainDbRef.current[i] ?? 0")
          != std::string::npos);
    CHECK(html.find("hydrateProcessingState(nativeHydration)")
          != std::string::npos);
    CHECK(html.find("spectral_resolution_request") != std::string::npos);
    CHECK(html.find("RES {resolution ?") == std::string::npos);
    CHECK(html.find("data-spectr-resolution") == std::string::npos);
}

TEST_CASE("native editor bridge rejects invalid zoom without partial mutation") {
    Rig r;
    const auto before_field = r.proc->field();
    const auto before_viewport = r.proc->viewport();
    const auto before_layout = r.proc->layout();

    const auto response = r.dispatch(
        processing_state_envelope(64, 340.0f, 280.0f, 9, -12.0f, true));
    REQUIRE(response_has_error(response, "invalid viewport"));
    CHECK(r.proc->layout() == before_layout);
    CHECK(r.proc->viewport().min_hz == Approx(before_viewport.min_hz));
    CHECK(r.proc->viewport().max_hz == Approx(before_viewport.max_hz));
    CHECK(r.proc->field().bands[9].gain_db
          == Approx(before_field.bands[9].gain_db));
    CHECK(r.proc->field().bands[9].muted == before_field.bands[9].muted);
}

TEST_CASE("native editor bridge: field contract rejects malformed values atomically") {
    Rig r;
    const auto before = r.proc->field();

    CHECK(response_has_error(r.dispatch(
        R"({"type":"band_field_set","payload":{"n_visible":31,"gain_db":[],"muted":[]}})"),
        "n_visible"));
    CHECK(response_has_error(r.dispatch(
        R"({"type":"band_field_set","payload":{"n_visible":32,"gain_db":[0],"muted":[false]}})"),
        "lengths"));

    auto out_of_range = field_envelope(32, 3, 24.01f, false);
    CHECK(response_has_error(r.dispatch(out_of_range), "within -24 and +24"));
    CHECK(r.proc->field().bands[3].gain_db == Approx(before.bands[3].gain_db));

    CHECK(response_has_error(r.dispatch(
        R"({"type":"band_field_set","payload":{"n_visible":32,"gain_db":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"muted":[false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,0]}})"),
        "boolean"));
}

TEST_CASE("native ScriptEngine calls the same Spectr field contract") {
    Rig r;
    pulp::view::ScriptEngine engine;
    r.bridge.attach_native_runtime(engine, "__spectrDispatch");

    const auto envelope = field_envelope(32, 11, 6.25f, true);
    const auto result = engine.evaluate(
        "__spectrDispatch(JSON.stringify(" + envelope + "))");

    REQUIRE(result.isString());
    CHECK(response_ok(std::string(result.getString())));
    CHECK(r.proc->field().bands[11].gain_db == Approx(6.25f));
    CHECK(r.proc->field().bands[11].muted);
}

TEST_CASE("CLI proof: JS field dispatch reaches C++ DSP and produces digital silence") {
    Rig r;
    pulp::format::PrepareContext prepare;
    prepare.sample_rate = 48000.0;
    prepare.max_buffer_size = 512;
    prepare.input_channels = 1;
    prepare.output_channels = 1;
    r.proc->prepare(prepare);

    pulp::view::ScriptEngine engine;
    r.bridge.attach_native_runtime(engine, "__spectrDispatch");
    const auto envelope = field_envelope(32, 0, 0.0f, false, true);
    const auto response = engine.evaluate(
        "__spectrDispatch(JSON.stringify(" + envelope + "))");
    REQUIRE(response.isString());
    REQUIRE(response_ok(std::string(response.getString())));

    constexpr std::size_t block_size = 512;
    constexpr std::size_t required = static_cast<std::size_t>(
        spectr::kSpectralLatency + spectr::kSpectralFftSize + 1024);
    constexpr std::size_t total =
        ((required + block_size - 1) / block_size) * block_size;
    std::vector<float> input(total), output(total, 1.0f);
    for (std::size_t i = 0; i < total; ++i) {
        input[i] = 0.5f * std::sin(2.0 * 3.14159265358979323846 * 997.0
                                  * static_cast<double>(i) / 48000.0);
    }
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::format::ProcessContext context;
    context.sample_rate = 48000.0;
    context.num_samples = static_cast<std::uint32_t>(block_size);

    for (std::size_t offset = 0; offset < total; offset += block_size) {
        const float* input_channels[] = {input.data() + offset};
        float* output_channels[] = {output.data() + offset};
        pulp::audio::BufferView<const float> input_view(input_channels, 1, block_size);
        pulp::audio::BufferView<float> output_view(output_channels, 1, block_size);
        r.proc->process(output_view, input_view, midi_in, midi_out, context);
    }

    double output_energy = 0.0;
    // Ignore startup latency so this cannot pass merely because WOLA has not
    // emitted a frame yet.
    for (std::size_t i = static_cast<std::size_t>(spectr::kSpectralLatency);
         i < output.size(); ++i)
        output_energy += output[i] * output[i];
    CHECK(output_energy == Approx(0.0).margin(1.0e-12));
}

TEST_CASE("CLI proof: zoomed viewport passes its island and mutes outside") {
    const auto render_peak = [](float frequency_hz) {
        Rig r;
        pulp::format::PrepareContext prepare;
        prepare.sample_rate = 48000.0;
        prepare.max_buffer_size = 512;
        prepare.input_channels = 1;
        prepare.output_channels = 1;
        r.proc->prepare(prepare);
        REQUIRE(response_ok(r.dispatch(
            processing_state_envelope(32, 1000.0f, 2000.0f,
                                      0, 0.0f, false, true))));

        constexpr std::size_t block_size = 512;
        constexpr std::size_t blocks = static_cast<std::size_t>(
            (spectr::kSpectralLatency + spectr::kSpectralFftSize
             + 4 * static_cast<int>(block_size)
             + static_cast<int>(block_size) - 1)
            / static_cast<int>(block_size));
        std::vector<float> input(block_size), output(block_size);
        pulp::midi::MidiBuffer midi_in, midi_out;
        pulp::format::ProcessContext context;
        context.sample_rate = 48000.0;
        context.num_samples = static_cast<std::uint32_t>(block_size);

        float settled_peak = 0.0f;
        for (std::size_t block = 0; block < blocks; ++block) {
            for (std::size_t sample = 0; sample < block_size; ++sample) {
                const auto absolute = block * block_size + sample;
                input[sample] = 0.5f * std::sin(
                    2.0 * 3.14159265358979323846 * frequency_hz
                    * static_cast<double>(absolute) / 48000.0);
            }
            const float* in_channels[] = {input.data()};
            float* out_channels[] = {output.data()};
            pulp::audio::BufferView<const float> in_view(
                in_channels, 1, block_size);
            pulp::audio::BufferView<float> out_view(
                out_channels, 1, block_size);
            r.proc->process(out_view, in_view, midi_in, midi_out, context);
            if (block * block_size >= static_cast<std::size_t>(
                    spectr::kSpectralLatency + spectr::kSpectralFftSize)) {
                for (const auto value : output)
                    settled_peak = std::max(settled_peak, std::abs(value));
            }
        }
        return settled_peak;
    };

    // All tones are FFT-bin-centred. With the HPF/LPF edge bands muted, the
    // first and last prove rejection below and above the isolated viewport,
    // while the middle tone proves the selected island still passes.
    CHECK(render_peak(187.5f) < 1.0e-6f);
    CHECK(render_peak(1500.0f) > 0.4f);
    CHECK(render_peak(6000.0f) < 1.0e-6f);
}

TEST_CASE("M9.5 bridge: malformed JSON returns error") {
    Rig r;
    const auto resp = r.dispatch(
                                                   "not json");
    CHECK(response_has_error(resp, "malformed JSON"));
}

TEST_CASE("M9.5 bridge: missing 'type' returns error") {
    Rig r;
    const auto resp = r.dispatch(
                                                   R"({"payload":{}})");
    CHECK(response_has_error(resp, "'type'"));
}

TEST_CASE("M9.5 bridge: unknown type returns error") {
    Rig r;
    const auto resp = r.dispatch(
                                                   R"({"type":"not_a_message"})");
    CHECK(response_has_error(resp, "unknown message type"));
}

TEST_CASE("M9.5 bridge paint: paint without paint_start is rejected") {
    Rig r;
    const auto resp = r.dispatch(
        R"({"type":"paint","payload":{"mode":"Sculpt","start_band":0,"start_value":0,
            "current_band":3,"current_value":-6,"n_visible":32}})");
    CHECK(response_has_error(resp, "paint without paint_start"));
}

TEST_CASE("M9.5 bridge paint: start → paint → end mutates the field") {
    Rig r;
    // Start with a neutral field.
    r.proc->field() = BandField{};

    REQUIRE(response_ok(r.dispatch(
            R"({"type":"paint_start"})")));

    // Sculpt drag from band 2 (0 dB) to band 5 (-6 dB).
    REQUIRE(response_ok(r.dispatch(
            R"({"type":"paint","payload":{"mode":"Sculpt","start_band":2,"start_value":0,
                "current_band":5,"current_value":-6,"n_visible":32}})")));

    // Bands 2..5 should now sit at -6 dB.
    for (std::size_t i = 2; i <= 5; ++i) {
        CHECK(r.proc->field().bands[i].gain_db == Approx(-6.0f));
    }
    // Bands outside the drag untouched.
    CHECK(r.proc->field().bands[0].gain_db == Approx(0.0f));

    REQUIRE(response_ok(r.dispatch(
            R"({"type":"paint_end"})")));

    // After end, a paint without a new start fails.
    const auto after = r.dispatch(
        R"({"type":"paint","payload":{"mode":"Sculpt","start_band":0,"start_value":0,
            "current_band":0,"current_value":-3,"n_visible":32}})");
    CHECK(response_has_error(after, "paint without paint_start"));
}

TEST_CASE("M9.5 bridge paint: unknown mode returns error without mutating") {
    Rig r;
    const auto before = r.proc->field().bands[0].gain_db;
    REQUIRE(response_ok(r.dispatch(
            R"({"type":"paint_start"})")));
    const auto resp = r.dispatch(
        R"({"type":"paint","payload":{"mode":"Blaster","start_band":0,"start_value":0,
            "current_band":3,"current_value":-6,"n_visible":32}})");
    CHECK(response_has_error(resp, "unknown edit mode"));
    CHECK(r.proc->field().bands[0].gain_db == Approx(before));
}

TEST_CASE("M9.5 bridge morph: clamps t and applies to live field") {
    Rig r;
    // Populate A with -10 dB flat, B with +10 dB flat.
    for (auto& b : r.proc->field().bands) b.gain_db = -10.0f;
    r.proc->field().bands[3].muted = false;
    r.proc->capture_snapshot(SnapshotBank::Slot::A);
    for (auto& b : r.proc->field().bands) b.gain_db = +10.0f;
    r.proc->field().bands[3].muted = true;
    r.proc->capture_snapshot(SnapshotBank::Slot::B);

    const auto midpoint = r.dispatch(
            R"({"type":"morph","payload":{"t":0.5,"revision":41}})");
    REQUIRE(response_ok(midpoint));
    CHECK(r.proc->field().bands[0].gain_db == Approx(0.0f));
    CHECK(r.proc->field().bands[3].muted); // B owns the exact midpoint.
    const auto midpoint_payload = choc::json::parse(midpoint);
    // The legacy `revision` field is not authoritative. Only
    // `expected_revision` participates in optimistic concurrency, and the
    // editor authority owns the monotonic response revision.
    CHECK(midpoint_payload["revision"].get<int64_t>() == 1);
    CHECK(midpoint_payload["muted"][3].getBool());
    CHECK(midpoint_payload["snapshots"]["A"]["populated"].getBool());
    CHECK(midpoint_payload["snapshots"]["B"]["populated"].getBool());

    // Out-of-range t clamps rather than erroring.
    REQUIRE(response_ok(r.dispatch(
            R"({"type":"morph","payload":{"t":5.0}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(+10.0f));

    REQUIRE(response_ok(r.dispatch(
            R"({"type":"morph","payload":{"t":-2.0}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(-10.0f));
}

TEST_CASE("M9.5 bridge capture_snapshot: slot string is required") {
    Rig r;
    r.proc->field().bands[10].gain_db = -4.0f;

    const auto bad = r.dispatch(
        R"({"type":"capture_snapshot"})");
    CHECK(response_has_error(bad, "'A' or 'B'"));

    const auto captured = r.dispatch(
            R"({"type":"capture_snapshot","payload":{"slot":"A","revision":7}})");
    REQUIRE(response_ok(captured));
    CHECK(r.proc->snapshots().has(SnapshotBank::Slot::A));
    CHECK(r.proc->snapshots().a.field.bands[10].gain_db == Approx(-4.0f));
    const auto captured_payload = choc::json::parse(captured);
    CHECK(captured_payload["revision"].get<int64_t>() == 1);
    CHECK(captured_payload["snapshots"]["A"]["populated"].getBool());

    REQUIRE(response_ok(r.dispatch(
            R"({"type":"capture_snapshot","payload":{"slot":"B"}})")));
    CHECK(r.proc->snapshots().has(SnapshotBank::Slot::B));
}

TEST_CASE("M9.5 bridge recall_snapshot restores field viewport and layout") {
    Rig r;
    r.proc->set_layout(spectr::Layout::Bands40);
    r.proc->viewport() = {280.0f, 340.0f};
    r.proc->field().bands[9] = {-13.0f, true};
    r.proc->capture_snapshot(SnapshotBank::Slot::A);

    r.proc->set_layout(spectr::Layout::Bands64);
    r.proc->viewport() = {20.0f, 20000.0f};
    r.proc->field().bands[9] = {8.0f, false};

    CHECK(response_has_error(r.dispatch(
        R"({"type":"recall_snapshot","payload":{"slot":"B"}})"), "empty"));
    const auto recalled = r.dispatch(
        R"({"type":"recall_snapshot","payload":{"slot":"A","revision":19}})");
    REQUIRE(response_ok(recalled));
    CHECK(r.proc->layout() == spectr::Layout::Bands40);
    CHECK(r.proc->viewport().min_hz == Approx(280.0f));
    CHECK(r.proc->viewport().max_hz == Approx(340.0f));
    CHECK(r.proc->field().bands[9].gain_db == Approx(-13.0f));
    CHECK(r.proc->field().bands[9].muted);
    const auto recalled_payload = choc::json::parse(recalled);
    CHECK(recalled_payload["revision"].get<int64_t>() == 1);
    CHECK(recalled_payload["n_visible"].get<int64_t>() == 40);
}

TEST_CASE("M9.5 bridge ab_toggle: flips active slot") {
    Rig r;
    CHECK(r.proc->snapshots().active == SnapshotBank::Slot::A);
    REQUIRE(response_ok(r.dispatch(
            R"({"type":"ab_toggle"})")));
    CHECK(r.proc->snapshots().active == SnapshotBank::Slot::B);
    REQUIRE(response_ok(r.dispatch(
            R"({"type":"ab_toggle"})")));
    CHECK(r.proc->snapshots().active == SnapshotBank::Slot::A);
}

TEST_CASE("M9.5 bridge load_pattern: applies by id, errors on unknown") {
    Rig r;
    // Flat factory pattern should land every band at 0 dB.
    for (auto& b : r.proc->field().bands) b.gain_db = -12.0f;

    REQUIRE(response_ok(r.dispatch(
            R"({"type":"load_pattern","payload":{"id":"factory:flat"}})")));
    CHECK(r.proc->field().bands[0].gain_db == Approx(0.0f));
    CHECK(r.proc->field().bands[63].gain_db == Approx(0.0f));

    const auto bad = r.dispatch(
        R"({"type":"load_pattern","payload":{"id":"factory:bogus"}})");
    CHECK(response_has_error(bad, "unknown pattern id"));

    const auto empty = r.dispatch(
        R"({"type":"load_pattern","payload":{}})");
    CHECK(response_has_error(empty, "pattern id missing"));
}

TEST_CASE("native pattern CRUD persists through the plugin state blob") {
    Rig r;
    r.proc->field().bands[4] = {-7.5f, true};

    auto response = r.dispatch(R"({"type":"save_current_pattern","payload":{}})");
    REQUIRE(response_ok(response));
    CHECK(response.find("PATTERN 01") != response.npos);
    REQUIRE(r.proc->patterns().user().size() == 1);
    const auto id = r.proc->patterns().user().front().id;

    response = r.dispatch(std::string{"{\"type\":\"rename_pattern\",\"payload\":{\"id\":\""}
        + id + R"(","name":"MY MASK"}})");
    REQUIRE(response_ok(response));
    CHECK(r.proc->patterns().user().front().name == "MY MASK");

    const auto bytes = r.proc->serialize_plugin_state();
    Rig reopened;
    REQUIRE(reopened.proc->deserialize_plugin_state(bytes));
    REQUIRE(reopened.proc->patterns().user().size() == 1);
    CHECK(reopened.proc->patterns().user().front().name == "MY MASK");
    CHECK(reopened.proc->patterns().user().front().gain_db[4] == Approx(-7.5f));
    CHECK(reopened.proc->patterns().user().front().muted[4]);

    response = reopened.dispatch(std::string{"{\"type\":\"delete_pattern\",\"payload\":{\"id\":\""}
        + id + R"("}})");
    REQUIRE(response_ok(response));
    CHECK(reopened.proc->patterns().user().empty());
}

// Obsolete under pulp#711: the EditorBridge framework takes the
// library by reference at handler registration, so there's no
// "nullptr library" code path to exercise. Unknown-pattern-id is
// the remaining error surface and it's covered by the test above.

// ── M9.5 slice 2 — save_preset / load_preset / param_set ─────────────

TEST_CASE("M9.5 bridge save_preset: returns the preset JSON in the response") {
    Rig r;
    r.store.set_value(spectr::kMix, 42.0f);
    r.proc->field().bands[3].gain_db = -9.0f;

    const auto resp = r.dispatch(
        R"({"type":"save_preset","payload":{"name":"Bridge Save","author":"Daniel"}})");
    REQUIRE(response_ok(resp));
    // Response embeds the preset JSON under "preset_json".
    CHECK(resp.find("preset_json") != std::string::npos);
    CHECK(resp.find("spectr.preset") != std::string::npos);  // format tag
    CHECK(resp.find("Bridge Save") != std::string::npos);    // metadata round-trips
}

TEST_CASE("M9.5 bridge load_preset: applies and echoes metadata") {
    // Build a preset from one rig, load it into another.
    Rig a;
    a.store.set_value(spectr::kMix, 18.0f);
    // Presets serialize the StateStore-authoritative processing state. Seed
    // the band through the same editor bridge used by the shipping UI rather
    // than mutating the processor's derived field cache behind that authority.
    REQUIRE(response_ok(a.dispatch(
        processing_state_envelope(32, 20.0f, 20000.0f, 10, -3.0f))));
    // A stale direct cache mutation must not override the authoritative value.
    a.proc->field().bands[10].gain_db = -11.0f;
    spectr::PresetMetadata meta;
    meta.name = "Bridge Load";
    meta.author = "Test";
    const auto preset = spectr::save_preset_to_string(*a.proc, meta);

    Rig b;
    // Escape the preset JSON inline via choc so the test doesn't have to
    // hand-escape quotes in a raw string.
    auto payload = choc::value::createObject("LoadPayload");
    payload.addMember("preset_json", preset);
    auto envelope = choc::value::createObject("Envelope");
    envelope.addMember("type", "load_preset");
    envelope.addMember("payload", payload);
    const auto envelope_json = choc::json::toString(envelope, /*useLineBreaks=*/false);

    const auto resp = b.dispatch(
                                                   envelope_json);
    REQUIRE(response_ok(resp));
    CHECK(resp.find("Bridge Load") != std::string::npos);
    CHECK(resp.find("\"author\": \"Test\"") != std::string::npos);
    CHECK(b.store.get_value(spectr::kMix) == Approx(18.0f));
    CHECK(b.proc->field().bands[10].gain_db == Approx(-3.0f));
}

TEST_CASE("M9.5 bridge load_preset: missing preset_json errors") {
    Rig r;
    const auto resp = r.dispatch(
        R"({"type":"load_preset","payload":{}})");
    CHECK(response_has_error(resp, "preset_json missing"));
}

TEST_CASE("M9.5 bridge load_preset: malformed preset surfaces the load error") {
    Rig r;
    const auto resp = r.dispatch(
        R"({"type":"load_preset","payload":{"preset_json":"not valid"}})");
    CHECK(response_has_error(resp, "JSON"));
}

TEST_CASE("M9.5 bridge param_set: writes to the StateStore") {
    Rig r;
    CHECK(r.store.get_value(spectr::kMix) == Approx(100.0f));   // default
    const auto resp = r.dispatch(
        R"({"type":"param_set","payload":{"id":1,"value":73.5}})");
    REQUIRE(response_ok(resp));
    CHECK(r.store.get_value(spectr::kMix) == Approx(73.5f));
}

TEST_CASE("M9.5 bridge param_set: missing id or value errors") {
    Rig r;
    const auto no_id = r.dispatch(
        R"({"type":"param_set","payload":{"value":0}})");
    CHECK(response_has_error(no_id, "param id missing"));

    const auto no_val = r.dispatch(
        R"({"type":"param_set","payload":{"id":1}})");
    CHECK(response_has_error(no_val, "param value missing"));
}

// ── M9.5 slice 2 — PatternLibrary persistence through plugin_state ──

TEST_CASE("M9.5 plugin_state: user patterns round-trip through serialize") {
    Rig a;
    // Save a user pattern with distinctive state.
    a.proc->field().bands[5].gain_db = -7.0f;
    a.proc->field().bands[6].gain_db = +2.0f;
    const auto p = a.proc->patterns().save_current(a.proc->field(), "BridgeRoundTrip");
    REQUIRE_FALSE(p.id.empty());

    const auto blob = a.proc->serialize_plugin_state();

    Rig b;
    // Fresh rig starts with only factory patterns.
    REQUIRE(b.proc->patterns().user().empty());
    REQUIRE(b.proc->deserialize_plugin_state(blob));
    // After load, the user pattern is back and factory presets are
    // still there (rebuilt at PatternLibrary construction).
    CHECK(b.proc->patterns().factory().size() == a.proc->patterns().factory().size());
    CHECK(b.proc->patterns().user().size() == 1);
    CHECK(b.proc->patterns().user().front().name == "BridgeRoundTrip");
    CHECK(b.proc->patterns().user().front().gain_db[5] == Approx(-7.0f));
    CHECK(b.proc->patterns().user().front().gain_db[6] == Approx(+2.0f));
}

TEST_CASE("M9.5 plugin_state: empty-span reset clears user patterns") {
    Rig r;
    r.proc->patterns().save_current(r.proc->field(), "Temp");
    REQUIRE_FALSE(r.proc->patterns().user().empty());
    // pulp#625 contract: empty span means "reset to defaults".
    REQUIRE(r.proc->deserialize_plugin_state({}));
    CHECK(r.proc->patterns().user().empty());
    // Factories still present (reconstructed by PatternLibrary()).
    CHECK_FALSE(r.proc->patterns().factory().empty());
}

TEST_CASE("M9.5 plugin_state: malformed pattern envelope is failure-atomic") {
    Rig r;
    const auto saved = r.proc->patterns().save_current(r.proc->field(), "KEEP");
    REQUIRE(r.proc->patterns().set_default(saved.id));

    for (const std::string json : {
        R"({"version":2,"patterns_json":17})",
        R"({"version":2,"patterns_json":"{\"format\":\"spectr.patterns\",\"version\":1,\"patterns\":[{\"id\":\"user:bad\",\"name\":\"BAD\",\"source\":\"user\",\"created_at\":\"now\",\"updated_at\":\"now\",\"gain_db\":[1e999],\"muted\":[false]}],\"default_id\":\"factory:flat\"}"})",
    }) {
        const std::vector<std::uint8_t> bytes(json.begin(), json.end());
        CHECK_FALSE(r.proc->deserialize_plugin_state(bytes));
        REQUIRE(r.proc->patterns().user().size() == 1);
        CHECK(r.proc->patterns().user().front().id == saved.id);
        CHECK(r.proc->patterns().user().front().name == "KEEP");
        CHECK(r.proc->patterns().default_id() == saved.id);
    }
}

TEST_CASE("plugin state migrates finite legacy gains without float overflow") {
    Rig r;

    const std::string live_overflow =
        R"({"version":1,"band_gain":[-36,1e100]})";
    const std::vector<std::uint8_t> live_bytes(
        live_overflow.begin(), live_overflow.end());
    REQUIRE(r.proc->deserialize_plugin_state(live_bytes));
    CHECK(r.proc->field().bands[0].gain_db == Approx(spectr::kBandGainMinDb));
    CHECK(r.proc->field().bands[1].gain_db == Approx(spectr::kBandGainMaxDb));
    CHECK(std::isfinite(r.proc->field().bands[1].gain_db));

    const std::string snapshot_overflow =
        R"({"version":2,"band_gain":[0],"snapshots":{"a":{"populated":true,"band_gain":[1e100]}}})";
    const std::vector<std::uint8_t> snapshot_bytes(
        snapshot_overflow.begin(), snapshot_overflow.end());
    REQUIRE(r.proc->deserialize_plugin_state(snapshot_bytes));
    CHECK(r.proc->snapshots().a.field.bands[0].gain_db
          == Approx(spectr::kBandGainMaxDb));
    CHECK(std::isfinite(r.proc->snapshots().a.field.bands[0].gain_db));
}

TEST_CASE("plugin state rejects non-finite gain encodings failure-atomically") {
    for (const std::string json : {
        R"({"version":2,"band_gain":[1e999]})",
        R"({"version":2,"band_gain":["Infinity"]})",
        R"({"version":2,"band_gain":[0],"snapshots":{"a":{"populated":true,"band_gain":[1e999]}}})",
    }) {
        Rig r;
        r.proc->field().bands[0].gain_db = 7.0f;
        r.proc->capture_snapshot(SnapshotBank::Slot::B);
        const auto before_b = r.proc->snapshots().b.field.bands[0].gain_db;
        const std::vector<std::uint8_t> bytes(json.begin(), json.end());
        CHECK_FALSE(r.proc->deserialize_plugin_state(bytes));
        CHECK(r.proc->field().bands[0].gain_db == Approx(7.0f));
        CHECK(r.proc->snapshots().b.field.bands[0].gain_db == Approx(before_b));
    }
}
