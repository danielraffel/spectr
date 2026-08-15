#include "spectr/spectr.hpp"

#include "spectr/editor_bridge.hpp"

#include <pulp/runtime/log.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/view/view.hpp>

#include <choc/text/choc_JSON.h>

#include "spectr_native_assets_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace spectr {
namespace {

constexpr float kPublishPeriodSeconds = 1.0f / 30.0f;
constexpr std::size_t kVisibleAnalyzerPointCount = 321;
constexpr std::size_t kOverviewAnalyzerPointCount = 121;
constexpr float kAnalyzerCeilingDb = 24.0f;
struct EmbeddedFile {
    const char* relative_path;
    const unsigned char* data;
    std::size_t size;
};

const std::array kEmbeddedFiles{
    EmbeddedFile{"runtime.js", spectr_native::runtime_js, spectr_native::runtime_js_size},
    EmbeddedFile{"design.js", spectr_native::design_js, spectr_native::design_js_size},
    EmbeddedFile{"editor.ir.json", spectr_native::editor_ir_json, spectr_native::editor_ir_json_size},
    EmbeddedFile{"assets/25ee97e9130cc8e719e4514a227dca176877a5ec0aaaf5cb2f5125b369386249.png", spectr_native::_25ee97e9130cc8e719e4514a227dca176877a5ec0aaaf5cb2f5125b369386249_png, spectr_native::_25ee97e9130cc8e719e4514a227dca176877a5ec0aaaf5cb2f5125b369386249_png_size},
    EmbeddedFile{"assets/406f550c49fc82813b945e66628b54f6a72b2785ad90218809c9a25a1cdfd446.png", spectr_native::_406f550c49fc82813b945e66628b54f6a72b2785ad90218809c9a25a1cdfd446_png, spectr_native::_406f550c49fc82813b945e66628b54f6a72b2785ad90218809c9a25a1cdfd446_png_size},
    EmbeddedFile{"assets/79560b1989a72f89fa7110fa518b679b9f5745dc0e54d17191cb3e44f7a807ae.png", spectr_native::_79560b1989a72f89fa7110fa518b679b9f5745dc0e54d17191cb3e44f7a807ae_png, spectr_native::_79560b1989a72f89fa7110fa518b679b9f5745dc0e54d17191cb3e44f7a807ae_png_size},
    EmbeddedFile{"assets/b7f238f6baabff2ba8356456ad6526a6688af54d4f2ee942969165bb29c19a03.png", spectr_native::b7f238f6baabff2ba8356456ad6526a6688af54d4f2ee942969165bb29c19a03_png, spectr_native::b7f238f6baabff2ba8356456ad6526a6688af54d4f2ee942969165bb29c19a03_png_size},
};

std::filesystem::path package_path_for(const void* instance) {
    std::error_code ec;
    auto directory = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    std::ostringstream name;
#if defined(_WIN32)
    const auto process_id = _getpid();
#else
    const auto process_id = getpid();
#endif
    name << "spectr-native-materialized-" << process_id << '-' << instance;
    return directory / name.str();
}

bool write_embedded_package(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path / "assets", ec);
    if (ec) return false;
    for (const auto& file : kEmbeddedFiles) {
        std::ofstream stream(path / file.relative_path,
                             std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(reinterpret_cast<const char*>(file.data),
                     static_cast<std::streamsize>(file.size));
        if (!stream.good()) return false;
    }
    return true;
}

bool finite_spectrum(const pulp::view::SpectrumData& spectrum) noexcept {
    if (spectrum.num_bins < 2 || spectrum.fft_size < 2
        || !std::isfinite(spectrum.sample_rate) || spectrum.sample_rate <= 0.0f
        || !std::isfinite(spectrum.floor_db) || spectrum.floor_db >= 0.0f)
        return false;
    for (int bin = 0; bin < spectrum.num_bins; ++bin)
        if (!std::isfinite(spectrum.magnitude_db[static_cast<std::size_t>(bin)]))
            return false;
    return true;
}

std::vector<float> analyzer_trace(const pulp::view::SpectrumData& spectrum,
                                  float min_hz,
                                  float max_hz,
                                  std::size_t point_count) {
    std::vector<float> result(point_count, spectrum.floor_db);
    const auto bin_hz = spectrum.sample_rate / static_cast<float>(spectrum.fft_size);
    const auto last_bin = spectrum.num_bins - 1;
    const auto log_min = std::log(min_hz);
    const auto log_span = std::log(max_hz) - log_min;
    for (std::size_t point = 0; point < point_count; ++point) {
        const auto lower_hz = std::exp(log_min
            + static_cast<float>(point) / static_cast<float>(point_count) * log_span);
        const auto upper_hz = std::exp(log_min
            + static_cast<float>(point + 1) / static_cast<float>(point_count) * log_span);
        const auto first = std::clamp(static_cast<int>(std::ceil(lower_hz / bin_hz)),
                                      0, last_bin);
        const auto last = std::clamp(static_cast<int>(std::floor(upper_hz / bin_hz)),
                                     0, last_bin);
        float peak = spectrum.floor_db;
        if (first <= last) {
            for (int bin = first; bin <= last; ++bin)
                peak = std::max(peak,
                    spectrum.magnitude_db[static_cast<std::size_t>(bin)]);
        } else {
            const auto center_hz = std::sqrt(lower_hz * upper_hz);
            const auto position = std::clamp(center_hz / bin_hz,
                0.0f, static_cast<float>(last_bin));
            const auto left = static_cast<int>(std::floor(position));
            const auto right = std::min(left + 1, last_bin);
            const auto mix = position - static_cast<float>(left);
            peak = spectrum.magnitude_db[static_cast<std::size_t>(left)]
                + (spectrum.magnitude_db[static_cast<std::size_t>(right)]
                   - spectrum.magnitude_db[static_cast<std::size_t>(left)]) * mix;
        }
        result[point] = std::clamp(peak, spectrum.floor_db, kAnalyzerCeilingDb);
    }
    return result;
}

void append_trace(std::ostringstream& js,
                  std::string_view name,
                  float min_hz,
                  float max_hz,
                  std::span<const float> values) {
    js << name << ":{min_hz:" << min_hz << ",max_hz:" << max_hz
       << ",magnitude_db:[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) js << ',';
        js << values[index];
    }
    js << "]}";
}

} // namespace

std::unique_ptr<pulp::view::View> Spectr::create_native_editor_() {
    auto root = std::make_unique<pulp::view::View>();
    root->set_theme(pulp::view::Theme::dark());
    root->flex().direction = pulp::view::FlexDirection::column;
    root->set_requires_gpu_host(true);

    native_package_path_ = package_path_for(this);
    if (native_package_path_.empty()
        || !write_embedded_package(native_package_path_)) {
        pulp::runtime::log_error(
            "[Spectr native] materialized editor package could not be written; editor is fail-closed");
        native_editor_root_ = root.get();
        return root;
    }

    pulp::view::ScriptedUiOptions options;
    options.script_path = native_package_path_ / "runtime.js";
    options.enable_hot_reload = false;
    options.enable_theme_reload = false;
    options.enable_runtime_import = true;
    native_scripted_ui_ = std::make_unique<pulp::view::ScriptedUiSession>(
        *root, state(), std::move(options));

    if (!native_editor_handlers_registered_) {
        register_spectr_editor_handlers(
            native_editor_bridge_, *this, patterns(), editor_authority());
        native_editor_bridge_.add_handler(
            "spectral_resolution_request",
            [this](const choc::value::ValueView&) {
                pulp::signal::SpectralBandResolution report;
                if (!spectral_resolution(report))
                    return pulp::view::EditorBridge::err_response(
                        "spectral resolution unavailable");
                auto payload = choc::value::createObject("SpectrEditorResolution");
                payload.addMember("represented_bands",
                    static_cast<std::int32_t>(report.represented_bands));
                payload.addMember("active_bands",
                    static_cast<std::int32_t>(report.active_bands));
                payload.addMember("fully_represented", report.fully_represented());
                payload.addMember("fft_size", static_cast<std::int32_t>(report.fft_size));
                payload.addMember("sample_rate", static_cast<double>(report.sample_rate));
                payload.addMember("min_hz", static_cast<double>(viewport().min_hz));
                payload.addMember("max_hz", static_cast<double>(viewport().max_hz));
                return pulp::view::EditorBridge::ok_response(payload);
            });
        native_editor_handlers_registered_ = true;
    }
    native_editor_bridge_.attach_native_runtime(
        *native_scripted_ui_, "__spectrEditorDispatch");

    std::string error;
    if (!native_scripted_ui_->load(&error)) {
        pulp::runtime::log_error(
            "[Spectr native] materialized QuickJS load failed: {}; editor is fail-closed",
            error);
        native_editor_bridge_.detach_native_runtime(
            *native_scripted_ui_, "__spectrEditorDispatch");
        native_scripted_ui_.reset();
        std::error_code ec;
        std::filesystem::remove_all(native_package_path_, ec);
        native_package_path_.clear();
    } else if (auto* bridge = native_scripted_ui_->bridge()) {
        std::ifstream stream(native_package_path_ / "design.js", std::ios::binary);
        std::string design((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
        try {
            bridge->set_script_base_dir(native_package_path_);
            bridge->load_script(design, "spectr-materialized-design");
            bridge->load_script(
                "if (typeof globalThis.__pulpApplyMaterializedVisualAuthority__ === 'function') "
                "globalThis.__pulpApplyMaterializedVisualAuthority__(); "
                "if (typeof globalThis.__pulpBindMaterializedCanvases__ === 'function') "
                "globalThis.__pulpBindMaterializedCanvases__();",
                "spectr-materialized-bind");
        } catch (const std::exception& error) {
            pulp::runtime::log_error(
                "[Spectr native] DesignIR materialization failed: {}; editor is fail-closed",
                error.what());
            native_editor_bridge_.detach_native_runtime(
                *native_scripted_ui_, "__spectrEditorDispatch");
            native_scripted_ui_.reset();
        }
    }

    native_editor_root_ = root.get();
    return root;
}

void Spectr::open_native_editor_(pulp::view::View& view) {
    if (&view != native_editor_root_ || !native_scripted_ui_) return;
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
    if (!finite_spectrum(spectrum)
        || spectrum.sequence_number == native_analyzer_sequence_)
        return true;
    native_analyzer_sequence_ = spectrum.sequence_number;

    const auto nyquist = spectrum.sample_rate * 0.5f;
    const auto visible_min = std::clamp(viewport().min_hz, 1.0f, nyquist);
    const auto visible_max = std::min(viewport().max_hz, nyquist);
    const auto overview_min = 20.0f;
    const auto overview_max = std::min(20000.0f, nyquist);
    if (!(visible_max > visible_min) || !(overview_max > overview_min)) return true;
    const auto visible = analyzer_trace(spectrum, visible_min, visible_max,
                                        kVisibleAnalyzerPointCount);
    const auto overview = analyzer_trace(spectrum, overview_min, overview_max,
                                         kOverviewAnalyzerPointCount);
    std::ostringstream js;
    js << "if (typeof globalThis.__spectrPublishNativeMessage === 'function') "
          "globalThis.__spectrPublishNativeMessage('analyzer_frame',{"
          "schema_version:1,epoch:" << spectrum.epoch
       << ",sequence_number:" << spectrum.sequence_number
       << ",dropped_frames:" << spectrum.dropped_frames
       << ",source_channels:" << spectrum.source_channels
       << ",fft_size:" << spectrum.fft_size
       << ",sample_rate:" << spectrum.sample_rate
       << ",floor_db:" << spectrum.floor_db
       << ",ceiling_db:" << kAnalyzerCeilingDb << ',';
    append_trace(js, "visible", visible_min, visible_max, visible);
    js << ',';
    append_trace(js, "overview", overview_min, overview_max, overview);
    js << "},'spectr-analyzer-frame');";
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
    if (!native_package_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(native_package_path_, ec);
        native_package_path_.clear();
    }
}

} // namespace spectr
