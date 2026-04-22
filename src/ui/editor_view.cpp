#include "spectr/ui/editor_view.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/view/asset_manager.hpp>
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
    if (panel_ && window_host() && attached_) {
        window_host()->detach_native_child_view(panel_->native_handle());
    }
}

void EditorView::attach_now() {
    if (attached_ || panel_) return;

    // Walk up to find a window host; log the chain so we can see what
    // the standalone wraps us in.
    auto* host = window_host();
    const pulp::view::View* cur = this;
    int depth = 0;
    while (!host && cur->parent()) {
        cur = cur->parent();
        ++depth;
        host = cur->window_host();
    }
    pulp::runtime::log_info("[Spectr] attach_now: depth={} host={}",
                            depth, (void*)host);
    if (!host) {
        pulp::runtime::log_error("[Spectr] EditorView::attach_now — no window_host (walked {} levels)", depth);
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
    if (host->attach_native_child_view(panel_->native_handle(), 0, 0, w, h)) {
        attached_ = true;
        pulp::runtime::log_info("[Spectr] WebView editor attached {}x{}", w, h);
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
