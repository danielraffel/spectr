#pragma once

#include <pulp/format/editor_host.hpp>
#include <vellum/view/view.hpp>

namespace pulp::vellum { struct EditorEnv; }

namespace spectr {
class Spectr;

namespace vellum_editor {

class RootView final : public ::vellum::view::View {
public:
    RootView(pulp::vellum::EditorEnv&, pulp::format::Processor&);
    ~RootView() override;

    pulp::format::ViewSize editor_view_size() const;
    void editor_opened();
    void editor_idle();
    // True once the materialized UI has mounted, so the host shows the
    // editor's first frame only when there is content to show.
    bool editor_content_ready() const;
    void editor_closing();

private:
    pulp::vellum::EditorEnv& env_;
    Spectr& processor_;
};

} // namespace vellum_editor
} // namespace spectr
