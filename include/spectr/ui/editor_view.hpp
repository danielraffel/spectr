#pragma once

// Spectr plugin editor — embeds the prototype HTML via a WebViewPanel.
//
// The plugin editor is a NativeViewHost that owns a pulp::view::WebViewPanel.
// NativeViewHost supplies the shared plugin/standalone attachment, resize,
// clipping, teardown, and headless-snapshot lifecycle. Spectr's Processor
// drives its explicit lifecycle hooks through on_view_opened /
// on_view_resized / on_view_closed.
//
// Message routing: the EditorView owns a pulp::view::EditorBridge (pulp#711
// framework, Pulp v0.41.0+). Handlers are registered at construction via
// register_spectr_editor_handlers(); attach_webview() on the panel routes
// inbound JSON envelopes through the bridge to those handlers.

#include <pulp/view/editor_bridge.hpp>
#include <pulp/view/native_view_host.hpp>
#include <pulp/view/web_view.hpp>

#include <memory>

#include "spectr/editor_bridge.hpp"

namespace spectr {

class Spectr;

/// Build the native-to-editor snapshot sent after the page reports that its
/// React bridge listener is installed.
pulp::view::WebViewMessage make_editor_hydration_message(const Spectr& plugin);

bool make_editor_resolution_message(
    const Spectr& plugin, pulp::view::WebViewMessage& out_message);

class EditorView : public pulp::view::NativeViewHost {
public:
    explicit EditorView(Spectr& plugin);
    ~EditorView() override;

    /// Reconcile the native child with the active plugin or standalone host.
    void attach_if_needed();

    /// Recompute the native child bounds after a host resize.
    void sync_to_host();

    /// Detach on editor close.
    void detach_if_needed();

private:
    bool post_resolution_();

    // ── Member order matters for destruction ───────────────────────────
    //
    // C++ destroys members in REVERSE declaration order. `panel_` must
    // tear down BEFORE `bridge_` so any in-flight WebView callbacks
    // that route through bridge_ don't reference a dead bridge.
    // Destruction order (last declared → first destroyed):
    //
    //   panel_      → destroyed FIRST — stops WebView inbound messages
    //   attached_   → pod, trivially destroyed
    //   bridge_     → destroyed AFTER panel_ — handler closures safe
    //                 to drain
    //   drag_       → destroyed AFTER bridge_ — closures that captured
    //                 &drag_ have stopped firing by now
    //   plugin_     → reference, no destructor
    //
    // EditorBridge is non-movable + non-copyable by design (pulp#711
    // makes it a compile-error to put it in a moveable container),
    // so we construct it in place as a direct member.
    //
    // Teardown order is now explicit: detach_if_needed() calls
    // bridge_.detach_webview(*panel_) before the native child view
    // comes off the host, so the race window that existed in
    // Pulp v0.41.1 (between set_message_handler clearing and the
    // WebView's last in-flight callback) is closed. Symmetric
    // teardown landed in pulp#728 (fixes #726).

    Spectr&                                   plugin_;
    EditorDragState                           drag_{};
    pulp::view::EditorBridge                  bridge_{};
    bool                                      bridge_attached_ = false;
    std::unique_ptr<pulp::view::WebViewPanel> panel_;
};

} // namespace spectr
