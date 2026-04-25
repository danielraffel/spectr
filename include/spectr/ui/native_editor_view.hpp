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

    /// Push the current StateStore param values into the editor's
    /// widget tree. Each Knob/Fader in editor.tsx has a stable id
    /// matching its StateStore param name (mix, output, response,
    /// engine, bands, morph) so the bridge can address them by
    /// setValue('mix', 0.42). Call from Spectr::on_view_opened
    /// (one-shot) and from a UI-thread tick once we add automation
    /// reflection.
    void update_params();

    /// Push an FFT spectrum frame into the analyzer band's <Spectrum>
    /// widget. The widget id is "spectrum" (see editor.tsx). Caller
    /// owns the buffer; we copy on the way through to JS.
    void update_spectrum(const float* magnitudes, std::size_t n);

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
