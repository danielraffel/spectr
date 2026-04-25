#pragma once

// Spectr native editor — replaces the WebView-embedded editor.html with a
// React app that renders DIRECTLY through Pulp's WidgetBridge → Yoga →
// Skia → Dawn pipeline. No WebView, no Babel-standalone, no DOM.
//
// The editor JS is the IIFE bundle produced by:
//   cd native-react && npm run build
// → native-react/dist/editor.js
//
// CMake's pulp_add_binary_data target (spectr_editor_assets) embeds that
// file as `spectr_editor::editor_js` when the SPECTR_NATIVE_EDITOR option
// is set to ON. With it OFF (the default), this header still compiles,
// but Spectr::create_view() will keep returning the WebView-backed
// EditorView path for safety.
//
// pulp #772 / spectr #25 / spectr #28.

#include <pulp/state/store.hpp>
#include <pulp/view/script_engine.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widget_bridge.hpp>

#include <memory>

namespace spectr {

class Spectr;

class NativeEditorView : public pulp::view::View {
public:
    explicit NativeEditorView(Spectr& plugin);
    ~NativeEditorView() override;

private:
    // Member order matters for destruction. The bridge holds references
    // into engine_ and uses *this as its widget root, so the bridge must
    // be destroyed before engine_ but after this View's children are
    // torn down by the base class. Declare in this order:
    //   plugin_   → reference, no destructor
    //   engine_   → must outlive bridge_ (destroyed last)
    //   bridge_   → owns the JS-created widgets attached to *this;
    //               destroyed before engine_, after plugin_
    Spectr&                                       plugin_;
    pulp::view::ScriptEngine                      engine_;
    std::unique_ptr<pulp::view::WidgetBridge>     bridge_;
};

} // namespace spectr
