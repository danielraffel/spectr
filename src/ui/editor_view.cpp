#include "spectr/ui/editor_view.hpp"
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
/// used by plugin editors (via pulp#651), WindowHost by the standalone. Both
/// expose the same native-child attach/bounds/detach API; we route through
/// whichever one we find first on this view or an ancestor.
struct NativeChildHost {
    pulp::view::PluginViewHost* plugin_host = nullptr;
    pulp::view::WindowHost*     window_host = nullptr;

    explicit operator bool() const noexcept { return plugin_host || window_host; }

    struct Size { float w = 0, h = 0; };
    Size content_size_plugin() const {
        if (plugin_host) {
            const auto s = plugin_host->get_size();
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
            handle_message_(m);
            return R"({"ok":true})";
        });
        panel_->set_ready_handler([p = panel_.get()] {
            p->navigate("pulp://spectr");
        });
    }

    // PluginViewHost exposes an explicit content size; WindowHost doesn't
    // yet (see danielraffel/pulp#661). In the standalone path
    // on_view_opened fires BEFORE the first layout, so bounds() is 0x0
    // here and we fall back to the view_size() preferred dimensions.
    //
    // There's a cosmetic artifact: the standalone's TabPanel eats ~32pt
    // at the top of the window content, which leaves a thin strip below
    // our WebView. The right fix is pulp#661 + pulp#663; we deliberately
    // avoid the "just over-size the attach" band-aid because getting the
    // number wrong clips the bottom rail off the bottom of the window.
    // Accepting the strip until the upstream fixes land.
    auto sz = host.content_size_plugin();
    const auto b = bounds();
    if (sz.w <= 0 || sz.h <= 0) {
        sz.w = b.width  > 0 ? b.width  : 1320.0f;
        sz.h = b.height > 0 ? b.height : 860.0f;
    }
    const auto w = sz.w;
    const auto h = sz.h;

    if (host.attach(panel_->native_handle(), w, h)) {
        attached_ = true;
        pulp::runtime::log_info("[Spectr] WebView editor attached {}x{} via {}",
                                w, h,
                                host.plugin_host ? "PluginViewHost" : "WindowHost");
    } else {
        pulp::runtime::log_error("[Spectr] attach_native_child_view failed");
    }
}

void EditorView::sync_to_host() {
    if (!attached_ || !panel_) return;
    auto host = find_native_child_host(this);
    if (!host) return;
    auto sz = host.content_size_plugin();
    if (sz.w <= 0 || sz.h <= 0) {
        const auto b = bounds();
        sz.w = b.width;
        sz.h = b.height;
    }
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

void EditorView::handle_message_(const pulp::view::WebViewMessage& msg) {
    pulp::runtime::log_info("[Spectr] webview msg type='{}' payload='{}'",
                            msg.type, msg.payload_json);
    (void)plugin_;
}

} // namespace spectr
