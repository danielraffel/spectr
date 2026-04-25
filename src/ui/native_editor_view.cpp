#include "spectr/ui/native_editor_view.hpp"
#include "spectr/spectr.hpp"

#include <pulp/runtime/log.hpp>

#include "spectr_editor_assets_data.hpp"

namespace spectr {

NativeEditorView::NativeEditorView(Spectr& plugin)
    : plugin_(plugin) {
    // Root container for the JS-created widget tree. The bridge attaches
    // every JS-created widget under this view, so the editor's outermost
    // <View> from editor.tsx becomes our first child.
    flex().direction = pulp::view::FlexDirection::column;

    // Construct the bridge over the editor's StateStore so the JS side
    // can read/write Spectr's parameters (Mix, Output, Response, Engine,
    // Bands, Morph) directly via setValue / __dispatch__.
    bridge_ = std::make_unique<pulp::view::WidgetBridge>(
        engine_, *this, plugin_.state());

#ifdef SPECTR_NATIVE_EDITOR_JS_EMBEDDED
    // editor.js is built by `cd native-react && npm run build` and
    // embedded by CMake's pulp_add_binary_data step. The pointer +
    // size are exposed in spectr_editor_assets_data.hpp as
    // spectr_editor::editor_js / editor_js_size.
    const std::string js_source(
        reinterpret_cast<const char*>(spectr_editor::editor_js),
        spectr_editor::editor_js_size);
    bridge_->load_script(js_source);
    pulp::runtime::log_info("Spectr native editor: loaded editor.js ({} bytes)",
                            js_source.size());
#else
    // Build was configured with SPECTR_NATIVE_EDITOR=OFF or editor.js
    // wasn't packed. The view exists but renders nothing — Spectr's
    // create_view() should not have returned this in that case.
    pulp::runtime::log_warn("Spectr native editor: editor.js not embedded "
                            "(SPECTR_NATIVE_EDITOR is OFF). Empty editor.");
#endif
}

NativeEditorView::~NativeEditorView() = default;

} // namespace spectr
