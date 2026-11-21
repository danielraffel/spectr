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
    void editor_closing();

private:
    pulp::vellum::EditorEnv& env_;
    Spectr& processor_;
};

} // namespace vellum_editor
} // namespace spectr
