#pragma once

// First Spectr package slice for the Vellum editor path. The root owns no
// parameter state: Pulp's Editor wrapper supplies one ParamPortModel per
// editor, and this view resolves the existing Spectr allowlist for bindings.

#include <pulp/vellum/editor_env.hpp>
#include <pulp/vellum_audio/audio_widgets.hpp>
#include <vellum/view/scripted_ui.hpp>
#include <vellum/view/view.hpp>

#include <array>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include "spectr/spectr.hpp"

namespace spectr::vellum_editor {

class RootView final : public ::vellum::view::View {
  public:
    explicit RootView(::pulp::vellum::EditorEnv& env) : env_(env) {
        visual_provider_ = dynamic_cast<::pulp::vellum_audio::AudioDisplayProvider*>(env_.visuals);
        if (visual_provider_ == nullptr && env_.visuals != nullptr) {
            owned_visual_provider_ = std::make_unique<::pulp::vellum_audio::AudioDisplayProvider>(
                std::vector<::pulp::vellum_audio::AudioDisplayProvider::ChannelSpec>{
                    {"output", "linear", ::vellum::value::ChannelShape::meter, 0.0f},
                    {"analyzer", "dBFS", ::vellum::value::ChannelShape::vector, -120.0f}});
            visual_provider_ = owned_visual_provider_.get();
        }
        // A column: the scripted control groups grow into the space above a
        // fixed-height audio row, so the displays never cover a control.
        flex().direction = ::vellum::view::FlexDirection::column;
        ::vellum::view::ScriptedUiOptions options;
        options.script_name = "spectr-vellum-editor.js";
        options.script_source = layout_script();
        scripted_session_ = std::make_unique<::vellum::view::ScriptedUiSession>(
            *this, env_.params, std::move(options));
        std::string error;
        if (!scripted_session_->load(&error))
            scripted_session_.reset();
        if (visual_provider_ != nullptr) {
            auto row = std::make_unique<::vellum::view::View>();
            row->flex().direction = ::vellum::view::FlexDirection::row;
            row->flex().gap = 6;
            row->flex().padding = 6;
            row->flex().preferred_height = kAudioRowHeight;
            row->flex().flex_shrink = 0;
            auto scope = std::make_unique<::pulp::vellum_audio::AnalyzerScope>();
            scope_ = scope.get();
            scope_->set_feed(visual_provider_, 1);
            scope_->configure_mapping({48000.0, ::spectr::kAnalyzerDisplayFftSize, 240, 20.0, 20000.0,
                                        -120.0f, 0.0f});
            scope_->flex().flex_grow = 1;
            row->add_child(std::move(scope));
            auto meter = std::make_unique<::pulp::vellum_audio::LevelMeter>();
            meter_ = meter.get();
            meter_->set_feed(visual_provider_, 0);
            meter_->flex().preferred_width = 24;
            meter_->flex().flex_shrink = 0;
            row->add_child(std::move(meter));
            audio_row_ = row.get();
            add_child(std::move(row));
        }
    }

    /// The audio display views, or null when the editor has no feed.
    ::vellum::view::View* audio_row() noexcept { return audio_row_; }
    ::vellum::view::View* audio_scope() noexcept { return scope_; }
    ::vellum::view::View* audio_meter() noexcept { return meter_; }

    /// Height of the audio display row under the control groups.
    static constexpr float kAudioRowHeight = 64.0f;

    /// One editor frame: pumps the scripted session, which pushes bound
    /// parameter values into their widgets. The editor host calls this from
    /// its idle tick.
    void editor_idle() {
        if (scripted_session_ != nullptr)
            (void)scripted_session_->poll();
        if (owned_visual_provider_ != nullptr && env_.visuals != nullptr) {
            std::array<float, 32> rms{}, peak{};
            const auto count = env_.visuals->read_meter(0, rms, peak);
            const auto bounded_count = std::min<std::size_t>(count, rms.size());
            if (bounded_count != 0)
                owned_visual_provider_->publish_meter(
                    0, std::span<const float>(rms.data(), bounded_count),
                    std::span<const float>(peak.data(), bounded_count));
            std::array<float, ::pulp::vellum_audio::AudioDisplayProvider::kMaxScopeSamples> bins{};
            const auto samples = env_.visuals->read_vector(1, bins);
            if (samples != 0)
                owned_visual_provider_->publish_scope(1, std::span<const float>(bins.data(), samples));
        }
        if (meter_ != nullptr) meter_->sample();
        if (scope_ != nullptr) scope_->sample();
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
    // Three titled groups in one full-size column: the main controls, then
    // one group per LFO holding its route amounts. Each group is split into
    // fixed-count rows whose knobs grow to fill the width, and the rows grow
    // to fill the height, so a larger editor enlarges every control. The
    // default 400x300 fits all 66 controls above a 24 px minimum edge. Creation order is unchanged, main controls first, which
    // bindings and tests rely on.
    static std::string layout_script() {
        return
            "const root = createCol('root');\n"
            "setFlex('root', 'gap', 4);\n"
            "setFlex('root', 'padding', 6);\n"
            "setFlex('root', 'width', '100%');\n"
            "setFlex('root', 'flex_grow', 1);\n"
            "setFlex('root', 'min_height', 0);\n"
            "const group = (gid, title, perRow, grow, keys) => {\n"
            "  createLabel(gid + '_title', title, 'root');\n"
            "  setFlex(gid + '_title', 'height', 14);\n"
            "  for (let r = 0; r * perRow < keys.length; r++) {\n"
            "    const rid = gid + '_row' + r;\n"
            "    createRow(rid, 'root');\n"
            "    setFlex(rid, 'gap', 3);\n"
            "    setFlex(rid, 'flex_grow', grow);\n"
            "    setFlex(rid, 'min_height', 24);\n"
            "    keys.slice(r * perRow, (r + 1) * perRow).forEach((key) => {\n"
            "      const id = key.replace(/[^a-zA-Z0-9]/g, '_');\n"
            "      createKnob(id, rid);\n"
            "      setFlex(id, 'flex_grow', 1);\n"
            "      setFlex(id, 'flex_basis', 0);\n"
            "      setFlex(id, 'min_width', 24);\n"
            "      bindWidgetToParam(id, key);\n"
            "    });\n"
            "  }\n"
            "};\n"
            "group('main', 'Spectr', 7, 1.2, [" + keys_script(main_keys()) + "]);\n"
            "group('lfo1', 'LFO 1 routes', 13, 1, [" + keys_script(lfo_keys(1)) + "]);\n"
            "group('lfo2', 'LFO 2 routes', 13, 1, [" + keys_script(lfo_keys(2)) + "]);\n";
    }

    static std::vector<std::string> main_keys() {
        std::vector<std::string> result;
        for (const auto key : model_control_keys()) result.emplace_back(key);
        return result;
    }

    static std::vector<std::string> lfo_keys(std::size_t lfo) {
        constexpr const char* route_names[] = {"Bank", "Snapshot A", "Snapshot B", "Morph",
            "Band shift", "Band spread", "Freeze", "Length", "Intensity", "Mix",
            "Output", "Bands", "Preset"};
        std::vector<std::string> result;
        for (const auto* route : route_names)
            for (const std::string suffix : {std::string{}, std::string{" Depth"}})
                result.push_back("LFO " + std::to_string(lfo) + " " + route + suffix);
        return result;
    }

    static std::string keys_script(const std::vector<std::string>& keys) {
        std::string result;
        for (const auto& key : keys) {
            if (!result.empty()) result += ", ";
            result += "'" + key + "'";
        }
        return result;
    }

    ::pulp::vellum::EditorEnv& env_;
    ::pulp::vellum_audio::AudioDisplayProvider* visual_provider_ = nullptr;
    std::unique_ptr<::pulp::vellum_audio::AudioDisplayProvider> owned_visual_provider_;
    ::pulp::vellum_audio::LevelMeter* meter_ = nullptr;
    ::pulp::vellum_audio::AnalyzerScope* scope_ = nullptr;
    ::vellum::view::View* audio_row_ = nullptr;
    std::unique_ptr<::vellum::view::ScriptedUiSession> scripted_session_;
};

} // namespace spectr::vellum_editor
