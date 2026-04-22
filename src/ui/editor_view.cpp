#include "spectr/ui/editor_view.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/view/asset_manager.hpp>
#include <pulp/view/plugin_view_host.hpp>
#include <pulp/view/window_host.hpp>

#include "spectr_editor_assets_data.hpp"

#include <string>
#include <string_view>

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
    // Panel creation is deferred to on_attached() — CHOC's WebView requires
    // a host NSView hierarchy to exist before it can initialize. Creating
    // it in the constructor crashes with an NSForwarding-selector abort
    // because there's no window yet.
    register_editor_assets_once();
}

EditorView::~EditorView() {
    // Detach is handled by PluginViewHost teardown; nothing to do here.
}

namespace {

// Walk up the view tree collecting whichever host is set. Pulp uses two
// distinct host types depending on the deployment:
//   - PluginViewHost: plugin editor (VST3/AU/CLAP), via pulp#651
//   - WindowHost:     standalone app window
// Both expose the same attach_native_child_view signature, so we take
// whichever is non-null on this view or an ancestor and route through it
// via a small adapter.
struct NativeChildHost {
    pulp::view::PluginViewHost* plugin_host = nullptr;
    pulp::view::WindowHost*     window_host = nullptr;

    bool attach(void* child, float x, float y, float w, float h) const {
        if (plugin_host) return plugin_host->attach_native_child_view(child, x, y, w, h);
        if (window_host) return window_host->attach_native_child_view(child, x, y, w, h);
        return false;
    }
    explicit operator bool() const noexcept { return plugin_host || window_host; }
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

void EditorView::attach_now() {
    if (attached_ || panel_) return;

    auto host = find_native_child_host(this);
    if (!host) {
        pulp::runtime::log_error("[Spectr] EditorView::attach_now — no PluginViewHost or WindowHost on the view tree");
        return;
    }

    pulp::view::WebViewOptions options;
    options.enable_debug           = true;
    options.accept_first_click     = true;
    options.transparent_background = false;
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

    const auto r = bounds();
    const auto w = static_cast<uint32_t>(r.width  > 0 ? r.width  : 1320);
    const auto h = static_cast<uint32_t>(r.height > 0 ? r.height : 860);
    if (host.attach(panel_->native_handle(),
                    0.0f, 0.0f,
                    static_cast<float>(w),
                    static_cast<float>(h))) {
        attached_ = true;
        pulp::runtime::log_info("[Spectr] WebView editor attached {}x{} via {}",
                                w, h,
                                host.plugin_host ? "PluginViewHost" : "WindowHost");
    } else {
        pulp::runtime::log_error("[Spectr] attach_native_child_view failed");
    }
}

void EditorView::handle_message_(const pulp::view::WebViewMessage& msg) {
    pulp::runtime::log_info("[Spectr] webview msg type='{}' payload='{}'",
                            msg.type, msg.payload_json);
    (void)plugin_;
}

} // namespace spectr
