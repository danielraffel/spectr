#pragma once

// Spectr plugin editor — embeds the prototype HTML via a WebViewPanel.
//
// The plugin editor is a thin native View that owns a pulp::view::WebViewPanel.
// On attach, we grab the parent window host and attach the WebViewPanel's
// native subview inside it, then navigate to the embedded prototype HTML.
// JS↔C++ state sync flows through set_message_handler / post_message.

#include <pulp/view/view.hpp>
#include <pulp/view/web_view.hpp>

#include <memory>

namespace spectr {

class Spectr;

class EditorView : public pulp::view::View {
public:
    explicit EditorView(Spectr& plugin);
    ~EditorView() override;

    /// Attach the webview to the plugin's editor window. Called by
    /// Spectr::on_view_opened() because that fires AFTER the framework
    /// wires window_host() — our own on_attached() runs too early.
    void attach_now();

private:
    Spectr& plugin_;
    std::unique_ptr<pulp::view::WebViewPanel> panel_;
    bool attached_ = false;

    void handle_message_(const pulp::view::WebViewMessage& msg);
};

} // namespace spectr
