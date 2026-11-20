#pragma once

// First Spectr package slice for the Vellum editor path. The root owns no
// parameter state: Pulp's Editor wrapper supplies one ParamPortModel per
// editor, and this view resolves the existing Spectr allowlist for bindings.

#include <pulp/vellum/editor_env.hpp>
#include <vellum/view/scripted_ui.hpp>
#include <vellum/view/view.hpp>

#include <array>
#include <memory>
#include <string>
#include "spectr/spectr.hpp"

namespace spectr::vellum_editor {

class RootView final : public ::vellum::view::View {
  public:
    explicit RootView(::pulp::vellum::EditorEnv& env) : env_(env) {
        ::vellum::view::ScriptedUiOptions options;
        options.script_name = "spectr-vellum-editor.js";
        options.script_source =
            "const root = createCol('root');\n"
            "setFlex('root', 'gap', 8);\n"
            "[" + control_script_keys() + "] .forEach((key) => {\n"
            "  const id = key.replace(/[^a-zA-Z0-9]/g, '_');\n"
            "  createKnob(id, 'root');\n"
            "  bindWidgetToParam(id, key);\n"
            "});\n";
        scripted_session_ = std::make_unique<::vellum::view::ScriptedUiSession>(
            *this, env_.params, std::move(options));
        std::string error;
        if (!scripted_session_->load(&error))
            scripted_session_.reset();
    }

    /// One editor frame: pumps the scripted session, which pushes bound
    /// parameter values into their widgets. The editor host calls this from
    /// its idle tick.
    void editor_idle() {
        if (scripted_session_ != nullptr)
            (void)scripted_session_->poll();
    }

    ::pulp::vellum::EditorEnv& env() noexcept { return env_; }
    const ::pulp::vellum::EditorEnv& env() const noexcept { return env_; }

    static std::optional<pulp::state::ParamID>
    resolve_param_key(std::string_view key) noexcept {
        return Spectr::resolve_editor_param_key(key);
    }

    static constexpr std::array<std::string_view, 14> model_control_keys() noexcept {
        return {"Intensity", "Auto Gain", "Mix", "Output Trim", "LFO Enabled",
                "LFO Shape", "LFO Rate", "LFO Depth", "LFO Target", "LFO 2 Enabled",
                "LFO 2 Shape", "LFO 2 Rate", "LFO 2 Depth", "Freeze Hold for Length"};
    }

  private:
    static std::string control_script_keys() {
        std::string result;
        bool first = true;
        for (const auto key : model_control_keys()) {
            if (!first) result += ", ";
            result += "'";
            result += key;
            result += "'";
            first = false;
        }
        constexpr const char* route_names[] = {"Bank", "Snapshot A", "Snapshot B", "Morph",
            "Band shift", "Band spread", "Freeze", "Length", "Intensity", "Mix",
            "Output", "Bands", "Preset"};
        for (std::size_t lfo = 1; lfo <= 2; ++lfo)
            for (const auto* route : route_names)
                for (const std::string suffix : {std::string{}, std::string{" Depth"}}) {
                    result += ", 'LFO " + std::to_string(lfo) + " " + route + suffix + "'";
                }
        return result;
    }

    ::pulp::vellum::EditorEnv& env_;
    std::unique_ptr<::vellum::view::ScriptedUiSession> scripted_session_;
};

} // namespace spectr::vellum_editor
