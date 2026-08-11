#include "spectr/ui/editor_view.hpp"
#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/view/asset_manager.hpp>

#include "spectr_editor_assets_data.hpp"

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace spectr {

namespace {

constexpr const char* kAssetKey = "spectr_editor_html";
constexpr std::size_t kVisibleAnalyzerPoints = 321;
constexpr std::size_t kOverviewAnalyzerPoints = 121;
constexpr float kAnalyzerPublishPeriodSeconds = 1.0f / 30.0f;
constexpr float kAnalyzerCeilingDb = 24.0f;

void register_editor_assets_once() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& assets = pulp::view::AssetManager::instance();
    assets.register_embedded(kAssetKey,
                             spectr_editor::editor_html,
                             spectr_editor::editor_html_size);
}

bool valid_snapshot(const EditorAnalyzerSnapshot& snapshot) noexcept {
    constexpr auto kLargestExactJsonInteger = std::uint64_t{9007199254740991ull};
    if (snapshot.magnitude_db.size() < 2 || snapshot.fft_size < 2
        || snapshot.fft_size % 2 != 0
        || (snapshot.fft_size & (snapshot.fft_size - 1)) != 0
        || snapshot.magnitude_db.size()
               != static_cast<std::size_t>(snapshot.fft_size / 2 + 1)
        || !std::isfinite(snapshot.sample_rate) || snapshot.sample_rate <= 0.0f
        || !std::isfinite(snapshot.floor_db) || snapshot.floor_db >= 0.0f
        || snapshot.source_channels < 1
        || snapshot.epoch > kLargestExactJsonInteger
        || snapshot.sequence_number > kLargestExactJsonInteger
        || snapshot.dropped_frames > kLargestExactJsonInteger)
        return false;
    for (const auto db : snapshot.magnitude_db)
        if (!std::isfinite(db) || db < snapshot.floor_db)
            return false;
    return true;
}

std::vector<float> peak_bucket_log_spectrum(
    const EditorAnalyzerSnapshot& snapshot,
    float min_hz,
    float max_hz,
    std::size_t point_count) {
    std::vector<float> result(point_count, snapshot.floor_db);
    const auto bin_hz = snapshot.sample_rate
                      / static_cast<float>(snapshot.fft_size);
    const auto last_bin = static_cast<int>(snapshot.magnitude_db.size()) - 1;
    const auto log_min = std::log(min_hz);
    const auto log_span = std::log(max_hz) - log_min;

    for (std::size_t point = 0; point < point_count; ++point) {
        const auto center = (static_cast<float>(point) + 0.5f)
                          / static_cast<float>(point_count);
        const auto lower = static_cast<float>(point)
                         / static_cast<float>(point_count);
        const auto upper = static_cast<float>(point + 1)
                         / static_cast<float>(point_count);
        const auto center_hz = std::exp(log_min + center * log_span);
        const auto lower_hz = std::exp(log_min + lower * log_span);
        const auto upper_hz = std::exp(log_min + upper * log_span);
        const auto first = std::clamp(
            static_cast<int>(std::ceil(lower_hz / bin_hz)), 0, last_bin);
        const auto last = std::clamp(
            static_cast<int>(std::floor(upper_hz / bin_hz)), 0, last_bin);

        float peak = snapshot.floor_db;
        if (first <= last) {
            for (int bin = first; bin <= last; ++bin)
                peak = std::max(peak, snapshot.magnitude_db[bin]);
        } else {
            const auto position = std::clamp(center_hz / bin_hz,
                                             0.0f,
                                             static_cast<float>(last_bin));
            const auto left = static_cast<int>(std::floor(position));
            const auto right = std::min(left + 1, last_bin);
            const auto mix = position - static_cast<float>(left);
            peak = snapshot.magnitude_db[left]
                 + (snapshot.magnitude_db[right]
                    - snapshot.magnitude_db[left]) * mix;
        }
        result[point] = std::clamp(
            peak, snapshot.floor_db, kAnalyzerCeilingDb);
    }
    return result;
}

choc::value::Value analyzer_trace(std::string_view class_name,
                                  float min_hz,
                                  float max_hz,
                                  std::span<const float> values) {
    auto magnitudes = choc::value::createEmptyArray();
    for (const auto value : values)
        magnitudes.addArrayElement(static_cast<double>(value));
    auto trace = choc::value::createObject(class_name);
    trace.addMember("min_hz", static_cast<double>(min_hz));
    trace.addMember("max_hz", static_cast<double>(max_hz));
    trace.addMember("magnitude_db", magnitudes);
    return trace;
}

} // namespace

EditorAnalyzerPublicationKey make_editor_analyzer_publication_key(
    const EditorAnalyzerSnapshot& snapshot,
    const Viewport& viewport) noexcept {
    return {snapshot.epoch, snapshot.sequence_number,
            viewport.min_hz, viewport.max_hz};
}

pulp::view::WebViewMessage make_editor_hydration_message(const Spectr& plugin) {
    const auto n = visible_count(plugin.layout());
    auto gains = choc::value::createEmptyArray();
    auto muted = choc::value::createEmptyArray();
    for (std::size_t i = 0; i < n; ++i) {
        gains.addArrayElement(static_cast<double>(plugin.field().bands[i].gain_db));
        muted.addArrayElement(plugin.field().bands[i].muted);
    }

    auto payload = choc::value::createObject("SpectrEditorHydration");
    payload.addMember("n_visible", static_cast<std::int32_t>(n));
    payload.addMember("gain_db", gains);
    payload.addMember("muted", muted);
    payload.addMember("min_hz", static_cast<double>(plugin.viewport().min_hz));
    payload.addMember("max_hz", static_cast<double>(plugin.viewport().max_hz));

    return {
        .type = "processing_state_hydrate",
        .payload_json = choc::json::toString(payload, false),
        .id = "spectr-processing-state-hydrate",
    };
}

bool make_editor_resolution_message(
    const Spectr& plugin, pulp::view::WebViewMessage& out_message) {
    pulp::signal::SpectralBandResolution report;
    if (!plugin.spectral_resolution(report)) return false;

    auto payload = choc::value::createObject("SpectrEditorResolution");
    payload.addMember("represented_bands",
                      static_cast<std::int32_t>(report.represented_bands));
    payload.addMember("active_bands",
                      static_cast<std::int32_t>(report.active_bands));
    payload.addMember("fully_represented", report.fully_represented());
    payload.addMember("fft_size", static_cast<std::int32_t>(report.fft_size));
    payload.addMember("sample_rate", static_cast<double>(report.sample_rate));
    payload.addMember("min_hz", static_cast<double>(plugin.viewport().min_hz));
    payload.addMember("max_hz", static_cast<double>(plugin.viewport().max_hz));

    out_message = {
        .type = "spectral_resolution",
        .payload_json = choc::json::toString(payload, false),
        .id = "spectr-spectral-resolution",
    };
    return true;
}

bool make_editor_analyzer_message(
    const EditorAnalyzerSnapshot& snapshot,
    const Viewport& viewport,
    pulp::view::WebViewMessage& out_message) {
    if (!valid_snapshot(snapshot) || !viewport.valid()) return false;

    const auto nyquist = snapshot.sample_rate * 0.5f;
    if (!(nyquist > 20.0f)) return false;
    const auto visible_min = std::clamp(viewport.min_hz, 1.0f, nyquist);
    const auto visible_max = std::min(viewport.max_hz, nyquist);
    const auto overview_min = 20.0f;
    const auto overview_max = std::min(20000.0f, nyquist);
    if (!(visible_max > visible_min) || !(overview_max > overview_min))
        return false;

    const auto visible = peak_bucket_log_spectrum(
        snapshot, visible_min, visible_max, kVisibleAnalyzerPoints);
    const auto overview = peak_bucket_log_spectrum(
        snapshot, overview_min, overview_max, kOverviewAnalyzerPoints);

    auto payload = choc::value::createObject("SpectrAnalyzerFrame");
    payload.addMember("schema_version", static_cast<std::int32_t>(1));
    payload.addMember("epoch", static_cast<std::int64_t>(snapshot.epoch));
    payload.addMember("sequence_number",
                      static_cast<std::int64_t>(snapshot.sequence_number));
    payload.addMember("dropped_frames",
                      static_cast<std::int64_t>(snapshot.dropped_frames));
    payload.addMember("source_channels",
                      static_cast<std::int32_t>(snapshot.source_channels));
    payload.addMember("fft_size", static_cast<std::int32_t>(snapshot.fft_size));
    payload.addMember("sample_rate", static_cast<double>(snapshot.sample_rate));
    payload.addMember("floor_db", static_cast<double>(snapshot.floor_db));
    payload.addMember("ceiling_db", static_cast<double>(kAnalyzerCeilingDb));
    payload.addMember("visible", analyzer_trace(
        "SpectrAnalyzerVisible", visible_min, visible_max, visible));
    payload.addMember("overview", analyzer_trace(
        "SpectrAnalyzerOverview", overview_min, overview_max, overview));

    pulp::view::WebViewMessage staged{
        .type = "analyzer_frame",
        .payload_json = choc::json::toString(payload, false),
        .id = "spectr-analyzer-frame",
    };
    out_message = std::move(staged);
    return true;
}

EditorView::EditorView(Spectr& plugin) : plugin_(plugin) {
    register_editor_assets_once();
    // Populate the bridge with Spectr's product handlers. Closures capture
    // `plugin_`, `plugin_.patterns()`, and `drag_` by reference — all
    // live as long as `this` does, so the bridge's non-movable
    // guarantee plus EditorView being heap-allocated via create_view()
    // covers lifetime.
    register_spectr_editor_handlers(bridge_, plugin_, plugin_.patterns(), drag_);
    bridge_.add_handler("editor_ready", [this](const choc::value::ValueView&) {
        if (!panel_)
            return pulp::view::EditorBridge::err_response("editor is not attached");
        panel_->post_message(make_editor_hydration_message(plugin_));
        post_resolution_();
        document_ready_ = true;
        start_analyzer_clock_();
        post_analyzer_();
        return pulp::view::EditorBridge::ok_response();
    });
    bridge_.add_handler("spectral_resolution_request",
        [this](const choc::value::ValueView&) {
            if (!panel_)
                return pulp::view::EditorBridge::err_response("editor is not attached");
            if (!post_resolution_())
                return pulp::view::EditorBridge::err_response(
                    "spectral resolution unavailable");
            return pulp::view::EditorBridge::ok_response();
        });
}

bool EditorView::post_analyzer_() {
    if (!panel_ || !document_ready_) return false;

    // This is the sole adapter to Pulp's publication type. EditorView is the
    // one non-RT analyzer owner: polling is explicitly separated from the
    // cheap, snapshot-only observation that follows.
    plugin_.bridge().poll();
    const auto& spectrum = plugin_.read_spectrum();
    if (spectrum.num_bins <= 0) return false;
    const EditorAnalyzerSnapshot snapshot{
        .magnitude_db = std::span<const float>(
            spectrum.magnitude_db.data(),
            static_cast<std::size_t>(spectrum.num_bins)),
        .epoch = spectrum.epoch,
        .sequence_number = spectrum.sequence_number,
        .dropped_frames = spectrum.dropped_frames,
        .source_channels = spectrum.source_channels,
        .fft_size = spectrum.fft_size,
        .sample_rate = spectrum.sample_rate,
        .floor_db = spectrum.floor_db,
    };
    const auto publication_key = make_editor_analyzer_publication_key(
        snapshot, plugin_.viewport());
    if (analyzer_publication_key_
        && *analyzer_publication_key_ == publication_key)
        return false;

    pulp::view::WebViewMessage message;
    if (!make_editor_analyzer_message(snapshot, plugin_.viewport(), message))
        return false;
    panel_->post_message(message);
    analyzer_publication_key_ = publication_key;
    return true;
}

bool EditorView::analyzer_tick_(float dt) {
    if (!panel_ || !document_ready_) return true;
    analyzer_elapsed_ += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    if (analyzer_elapsed_ < kAnalyzerPublishPeriodSeconds) return true;
    analyzer_elapsed_ = std::fmod(analyzer_elapsed_, kAnalyzerPublishPeriodSeconds);
    post_analyzer_();
    return true;
}

void EditorView::start_analyzer_clock_() {
    if (analyzer_subscription_ >= 0) return;
    auto* clock = frame_clock();
    if (!clock) return;
    analyzer_clock_ = clock;
    analyzer_subscription_ = analyzer_clock_->subscribe(
        [this](float dt) { return analyzer_tick_(dt); });
}

void EditorView::stop_analyzer_clock_() {
    if (analyzer_subscription_ >= 0 && analyzer_clock_)
        analyzer_clock_->unsubscribe(analyzer_subscription_);
    analyzer_subscription_ = -1;
    analyzer_clock_ = nullptr;
    analyzer_elapsed_ = 0.0f;
    analyzer_publication_key_.reset();
}

EditorView::~EditorView() { detach_if_needed(); }

bool EditorView::post_resolution_() {
    if (!panel_) return false;
    pulp::view::WebViewMessage message;
    if (!make_editor_resolution_message(plugin_, message)) return false;
    panel_->post_message(message);
    return true;
}

void EditorView::attach_if_needed() {
    if (!panel_) {
        pulp::view::WebViewOptions options;
        options.enable_debug           = true;
        options.accept_first_click     = true;
        options.transparent_background = false;
        // Pre-paint placeholder shown before navigate() completes —
        // matches the prototype's background so the user never sees a
        // white flash or any intermediate chrome. Needs Pulp v0.38.0+
        // (pulp#662 / PR#673).
        options.initial_html = R"(<!doctype html><html><head><meta charset="utf-8">
<style>html,body{margin:0;height:100%;background:#05070a;}</style>
</head><body></body></html>)";
        options.fetch_resource = pulp::view::make_webview_embedded_resource_fetcher(
            kAssetKey, /*assets*/ {});
        options.custom_scheme_uri = "pulp://spectr";

        panel_ = pulp::view::WebViewPanel::create(options);
        if (!panel_ || !panel_->native_handle()) {
            pulp::runtime::log_error("[Spectr] WebViewPanel::create failed");
            panel_.reset();
            return;
        }

        // Route WebView messages through the EditorBridge. attach_webview
        // installs panel_->set_message_handler under the hood.
        bridge_.attach_webview(*panel_);
        bridge_attached_ = true;
        panel_->set_ready_handler([p = panel_.get()] {
            p->navigate("pulp://spectr");
        });

        auto* panel = panel_.get();
        set_native_child(panel->native_handle(),
                         [panel](uint32_t /*width*/, uint32_t /*height*/) {
                             return panel->snapshot_png();
                         });
    }
    update_native_layout();
}

void EditorView::sync_to_host() {
    if (panel_) {
        update_native_layout();
        if (document_ready_) start_analyzer_clock_();
    }
}

void EditorView::detach_if_needed() {
    if (!panel_) return;
    document_ready_ = false;
    stop_analyzer_clock_();
    // Explicit teardown order: clear the bridge's WebView handler
    // BEFORE the native child view comes off the host. This closes
    // the residual race where the panel's last in-flight WebView
    // callback could dispatch through the bridge after panel_
    // destruction started. Symmetric partner of attach_webview(),
    // added in pulp#728 (fixes #726).
    if (bridge_attached_) {
        bridge_.detach_webview(*panel_);
        bridge_attached_ = false;
    }
    clear_native_child();
    // A close/open cycle gets a fresh document so React reinstalls its
    // hydration listener and issues a new editor_ready request.
    panel_.reset();
}

} // namespace spectr
