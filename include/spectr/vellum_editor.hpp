#pragma once

// First Spectr package slice for the Vellum editor path. The root owns no
// parameter state: Pulp's Editor wrapper supplies one ParamPortModel per
// editor, and this view resolves the existing Spectr allowlist for bindings.

#include <pulp/vellum/editor_env.hpp>
#include <vellum/view/view.hpp>

#include "spectr/spectr.hpp"

namespace spectr::vellum_editor {

class RootView final : public ::vellum::view::View {
  public:
    explicit RootView(::pulp::vellum::EditorEnv& env) : env_(env) {}

    ::pulp::vellum::EditorEnv& env() noexcept { return env_; }
    const ::pulp::vellum::EditorEnv& env() const noexcept { return env_; }

    static std::optional<pulp::state::ParamID>
    resolve_param_key(std::string_view key) noexcept {
        return Spectr::resolve_editor_param_key(key);
    }

  private:
    ::pulp::vellum::EditorEnv& env_;
};

} // namespace spectr::vellum_editor
