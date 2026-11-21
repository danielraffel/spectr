#pragma once

#if defined(SPECTR_VELLUM_EDITOR_PACKAGE)
#include <vellum/view/command_registry.hpp>
#include <vellum/view/frame_clock.hpp>
#include <vellum/view/scripted_ui.hpp>

namespace pulp::vellum { struct EditorEnv; }
namespace spectr::vellum_editor { class RootView; }
namespace spectr {
namespace editor_view = ::vellum::view;
namespace editor_canvas = ::vellum::canvas;
}
#else
#include <pulp/view/command_registry.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/scripted_ui.hpp>

namespace spectr {
namespace editor_view = pulp::view;
namespace editor_canvas = pulp::canvas;
}
#endif
