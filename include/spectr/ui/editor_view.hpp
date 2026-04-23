#pragma once

// Spectr plugin editor — embeds the prototype HTML via a WebViewPanel.
//
// The plugin editor is a thin native View that owns a pulp::view::WebViewPanel.
// On attach, we locate whichever host owns the editor window (PluginViewHost
// in plugin editors, WindowHost in the standalone), ask it for its actual
// native content size, and attach our WebView as its native child. Spectr's
// Processor drives attach/sync/detach via on_view_opened / on_view_resized /
// on_view_closed.

#include <pulp/view/view.hpp>
#include <pulp/view/web_view.hpp>

#include <memory>

namespace spectr {

class Spectr;

class EditorView : public pulp::view::View {
public:
    explicit EditorView(Spectr& plugin);
    ~EditorView() override;

    /// Create the WebViewPanel (if needed) and attach its NSView as a
    /// native child of whichever host owns the editor window. Sizes the
    /// child to the host's actual content size to avoid letterbox gaps.
    void attach_if_needed();

    /// Update the native child view bounds to match the current host
    /// content size. Wired to Processor::on_view_resized.
    void sync_to_host();

    /// Detach on editor close.
    void detach_if_needed();

private:
    Spectr& plugin_;
    std::unique_ptr<pulp::view::WebViewPanel> panel_;
    bool attached_ = false;

    void handle_message_(const pulp::view::WebViewMessage& msg);
};

} // namespace spectr
