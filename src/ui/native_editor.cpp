#include "spectr/spectr.hpp"

#include "spectr/editor_bridge.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/view/view.hpp>

#include <choc/text/choc_JSON.h>

#include "spectr_native_assets_data.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace spectr {
namespace {

constexpr float kPublishPeriodSeconds = 1.0f / 30.0f;
constexpr std::size_t kAnalyzerPointCount = 256;

std::filesystem::path script_path_for(const void* instance) {
    std::error_code ec;
    auto directory = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    std::ostringstream name;
#if defined(_WIN32)
    const auto process_id = _getpid();
#else
    const auto process_id = getpid();
#endif
    name << "spectr-native-n1-" << process_id << '-' << instance << ".js";
    return directory / name.str();
}

bool write_embedded_script(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(
        reinterpret_cast<const char*>(spectr_native::editor_js),
        static_cast<std::streamsize>(spectr_native::editor_js_size));
    return stream.good();
}

} // namespace

std::unique_ptr<pulp::view::View> Spectr::create_native_editor_() {
    auto root = std::make_unique<pulp::view::View>();
    root->set_theme(pulp::view::Theme::dark());
    root->flex().direction = pulp::view::FlexDirection::column;
    root->set_requires_gpu_host(true);

    native_script_path_ = script_path_for(this);
    if (native_script_path_.empty() || !write_embedded_script(native_script_path_)) {
        pulp::runtime::log_error(
            "[Spectr native N1] embedded @pulp/react bundle could not be materialized; editor is fail-closed");
        native_editor_root_ = root.get();
        return root;
    }

    pulp::view::ScriptedUiOptions options;
    options.script_path = native_script_path_;
    options.enable_hot_reload = false;
    options.enable_theme_reload = false;
    native_scripted_ui_ = std::make_unique<pulp::view::ScriptedUiSession>(
        *root, state(), std::move(options));

    if (!native_editor_handlers_registered_) {
        register_spectr_editor_handlers(
            native_editor_bridge_, *this, patterns(), editor_authority());
        native_editor_handlers_registered_ = true;
    }
    native_editor_bridge_.attach_native_runtime(
        *native_scripted_ui_, "__spectrEditorDispatch");

    std::string error;
    if (!native_scripted_ui_->load(&error)) {
        pulp::runtime::log_error(
            "[Spectr native N1] QuickJS/@pulp/react load failed: {}; editor is fail-closed",
            error);
        native_editor_bridge_.detach_native_runtime(
            *native_scripted_ui_, "__spectrEditorDispatch");
        native_scripted_ui_.reset();
        std::error_code ec;
        std::filesystem::remove(native_script_path_, ec);
        native_script_path_.clear();
    }

    native_editor_root_ = root.get();
    if (native_scripted_ui_) hydrate_native_editor_();
    return root;
}

void Spectr::hydrate_native_editor_() {
    if (!native_scripted_ui_ || !native_scripted_ui_->bridge()) return;

    std::ostringstream js;
    js << "if (typeof globalThis.__spectrHydrate !== 'function') "
          "throw new Error('native hydration boundary missing');"
          "globalThis.__spectrHydrate("
       << choc::json::toString(make_editor_state_payload(
              *this, editor_authority().revision()))
       << ");";

    try {
        native_scripted_ui_->bridge()->load_script(js.str(), "spectr-native-hydration");
    } catch (const std::exception& error) {
        pulp::runtime::log_error(
            "[Spectr native N1] hydration failed closed: {}", error.what());
    }
}

void Spectr::open_native_editor_(pulp::view::View& view) {
    if (&view != native_editor_root_ || !native_scripted_ui_) return;
    hydrate_native_editor_();
    if (native_frame_subscription_ >= 0) return;
    native_frame_clock_ = view.frame_clock();
    if (!native_frame_clock_) return;
    native_frame_subscription_ = native_frame_clock_->subscribe(
        [this](float dt) { return tick_native_analyzer_(dt); });
}

bool Spectr::tick_native_analyzer_(float dt) {
    if (!native_scripted_ui_ || !native_scripted_ui_->bridge()) return false;
    native_analyzer_elapsed_ += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    if (native_analyzer_elapsed_ < kPublishPeriodSeconds) return true;
    native_analyzer_elapsed_ = std::fmod(native_analyzer_elapsed_, kPublishPeriodSeconds);

    bridge_.poll();
    const auto& spectrum = read_spectrum();
    if (spectrum.num_bins < 2 || spectrum.sequence_number == native_analyzer_sequence_)
        return true;
    native_analyzer_sequence_ = spectrum.sequence_number;

    std::ostringstream js;
    js << "if (typeof globalThis.__spectrAnalyzer === 'function') "
          "globalThis.__spectrAnalyzer([";
    const auto bins = static_cast<std::size_t>(spectrum.num_bins);
    for (std::size_t point = 0; point < kAnalyzerPointCount; ++point) {
        if (point != 0) js << ',';
        const auto first = point * bins / kAnalyzerPointCount;
        const auto last = std::max(first + 1, (point + 1) * bins / kAnalyzerPointCount);
        float peak = spectrum.floor_db;
        for (auto bin = first; bin < std::min(last, bins); ++bin)
            peak = std::max(peak, spectrum.magnitude_db[bin]);
        const auto normalized = std::clamp(
            (peak - spectrum.floor_db) / (24.0f - spectrum.floor_db), 0.0f, 1.0f);
        js << normalized;
    }
    js << "]);";
    try {
        native_scripted_ui_->bridge()->load_script(js.str(), "spectr-native-analyzer");
    } catch (const std::exception& error) {
        pulp::runtime::log_error(
            "[Spectr native N1] analyzer publication rejected: {}", error.what());
    }
    return true;
}

void Spectr::close_native_editor_() {
    if (native_frame_subscription_ >= 0 && native_frame_clock_)
        native_frame_clock_->unsubscribe(native_frame_subscription_);
    native_frame_subscription_ = -1;
    native_frame_clock_ = nullptr;
    native_analyzer_elapsed_ = 0.0f;
    native_analyzer_sequence_ = 0;
    editor_authority().reset_transient_state();
    native_editor_root_ = nullptr;
    if (native_scripted_ui_) {
        native_editor_bridge_.detach_native_runtime(
            *native_scripted_ui_, "__spectrEditorDispatch");
    }
    native_scripted_ui_.reset();
    if (!native_script_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(native_script_path_, ec);
        native_script_path_.clear();
    }
}

} // namespace spectr
