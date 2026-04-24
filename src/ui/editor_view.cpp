#include "spectr/ui/editor_view.hpp"
#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/view/asset_manager.hpp>
#include <pulp/view/plugin_view_host.hpp>
#include <pulp/view/window_host.hpp>

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

/// Adapter over whichever host is set on the view tree. PluginViewHost is
/// used by plugin editors (via pulp#651), WindowHost by the standalone.
/// Both expose equivalent native-child attach/bounds/detach APIs and, as of
/// pulp#670, both expose a content-size accessor — so we no longer need a
/// fallback-number dance for the standalone path.
struct NativeChildHost {
    pulp::view::PluginViewHost* plugin_host = nullptr;
    pulp::view::WindowHost*     window_host = nullptr;

    explicit operator bool() const noexcept { return plugin_host || window_host; }

    struct Size { float w = 0, h = 0; };

    /// Host-reported content area. PluginViewHost exposes `get_size()`;
    /// WindowHost exposes `get_content_size()` (pulp#670, Pulp v0.40.0+).
    /// We prefer plugin_host when both are present since plugin editors
    /// embed inside a host-owned window and only the plugin-side dimensions
    /// track the embed area.
    Size content_size() const {
        if (plugin_host) {
            const auto s = plugin_host->get_size();
            return {static_cast<float>(s.width), static_cast<float>(s.height)};
        }
        if (window_host) {
            const auto s = window_host->get_content_size();
            return {static_cast<float>(s.width), static_cast<float>(s.height)};
        }
        return {0, 0};
    }

    bool attach(void* child, float w, float h) const {
        if (plugin_host) return plugin_host->attach_native_child_view(child, 0.0f, 0.0f, w, h);
        if (window_host) return window_host->attach_native_child_view(child, 0.0f, 0.0f, w, h);
        return false;
    }

    void set_bounds(void* child, float w, float h) const {
        if (plugin_host) { plugin_host->set_native_child_view_bounds(child, 0.0f, 0.0f, w, h); return; }
        if (window_host) { window_host->set_native_child_view_bounds(child, 0.0f, 0.0f, w, h); return; }
    }

    void detach(void* child) const {
        if (plugin_host) { plugin_host->detach_native_child_view(child); return; }
        if (window_host) { window_host->detach_native_child_view(child); return; }
    }
};

NativeChildHost find_native_child_host(const pulp::view::View* v) {
    NativeChildHost out{};
    const pulp::view::View* cur = v;
    while (cur) {
        if (!out.plugin_host) out.plugin_host = cur->plugin_view_host();
        if (!out.window_host) out.window_host = cur->window_host();
        if (out) break;
        cur = cur->parent();
    }
    return out;
}

} // namespace

EditorView::EditorView(Spectr& plugin) : plugin_(plugin) {
    register_editor_assets_once();
}

EditorView::~EditorView() { detach_if_needed(); }

void EditorView::attach_if_needed() {
    if (attached_) return;

    auto host = find_native_child_host(this);
    if (!host) {
        pulp::runtime::log_error("[Spectr] attach_if_needed — no host on the view tree");
        return;
    }

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

        panel_->set_message_handler([this](const pulp::view::WebViewMessage& m) -> std::string {
            return handle_message_(m);
        });
        panel_->set_ready_handler([p = panel_.get()] {
            p->navigate("pulp://spectr");
        });
    }

    // Both hosts now report a real content size (pulp#651 for plugin
    // editors, pulp#670 for the standalone). No more fallback dance or
    // magic 1320x860 numbers; we trust whichever host we resolved.
    const auto sz = host.content_size();
    if (sz.w <= 0 || sz.h <= 0) {
        pulp::runtime::log_error("[Spectr] attach_if_needed — host reports 0x0 content size");
        return;
    }

    if (host.attach(panel_->native_handle(), sz.w, sz.h)) {
        attached_ = true;
        pulp::runtime::log_info("[Spectr] WebView editor attached {}x{} via {}",
                                sz.w, sz.h,
                                host.plugin_host ? "PluginViewHost" : "WindowHost");
    } else {
        pulp::runtime::log_error("[Spectr] attach_native_child_view failed");
    }
}

void EditorView::sync_to_host() {
    if (!attached_ || !panel_) return;
    auto host = find_native_child_host(this);
    if (!host) return;
    const auto sz = host.content_size();
    if (sz.w <= 0 || sz.h <= 0) return;
    host.set_bounds(panel_->native_handle(), sz.w, sz.h);
}

void EditorView::detach_if_needed() {
    if (!attached_ || !panel_) {
        attached_ = false;
        return;
    }
    auto host = find_native_child_host(this);
    if (host) host.detach(panel_->native_handle());
    attached_ = false;
}

std::string EditorView::handle_message_(const pulp::view::WebViewMessage& msg) {
    pulp::runtime::log_info("[Spectr] webview msg type='{}' payload='{}'",
                            msg.type, msg.payload_json);

    // The pulp WebViewMessage surfaces `type` as its own field and
    // `payload_json` as the payload object. Rebuild the envelope
    // shape the bridge expects so we can reuse one dispatcher for
    // both in-WebView and unit-test entry points. Inner JSON is
    // trusted — any parse error is surfaced through the bridge's
    // normal error response.
    std::string envelope;
    envelope.reserve(msg.type.size() + msg.payload_json.size() + 32);
    envelope += R"({"type":")";
    envelope += msg.type;
    envelope += R"(","payload":)";
    envelope += msg.payload_json.empty() ? std::string("{}") : msg.payload_json;
    envelope += "}";

    return dispatch_editor_message_json(plugin_, &plugin_.patterns(),
                                        bridge_state_, envelope);
}

} // namespace spectr
