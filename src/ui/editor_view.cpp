#include "spectr/ui/editor_view.hpp"
#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/view/asset_manager.hpp>

#include "spectr_editor_assets_data.hpp"

#include <algorithm>
#include <string>

namespace spectr {

namespace {

constexpr const char* kAssetKey = "spectr_editor_html";

void register_editor_assets_once() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& assets = pulp::view::AssetManager::instance();
    assets.register_embedded(kAssetKey,
                             spectr_editor::editor_html,
                             spectr_editor::editor_html_size);
}

} // namespace

EditorView::EditorView(Spectr& plugin) : plugin_(plugin) {
    register_editor_assets_once();
    // Populate the bridge with Spectr's 10 handlers. Closures capture
    // `plugin_`, `plugin_.patterns()`, and `drag_` by reference — all
    // live as long as `this` does, so the bridge's non-movable
    // guarantee plus EditorView being heap-allocated via create_view()
    // covers lifetime.
    register_spectr_editor_handlers(bridge_, plugin_, plugin_.patterns(), drag_);
}

EditorView::~EditorView() { detach_if_needed(); }

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
    if (panel_) update_native_layout();
}

void EditorView::detach_if_needed() {
    if (!panel_) return;
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
}

} // namespace spectr
