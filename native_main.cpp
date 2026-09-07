#include "spectr/spectr.hpp"

#include <pulp/format/standalone.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    pulp::format::StandaloneApp app(spectr::create_spectr);
    pulp::format::StandaloneConfig config;
    config.input_channels = 2;
    config.output_channels = 2;
    config.show_settings_tab = false;
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strncmp(argument, "--screenshot=", 13) == 0) {
            config.screenshot_path = argument + 13;
        } else if (std::strcmp(argument, "--screenshot") == 0
                   && index + 1 < argc) {
            config.screenshot_path = argv[++index];
        } else if (std::strncmp(argument, "--screenshot-frame-delay=", 25) == 0) {
            config.screenshot_frame_delay = std::stoi(argument + 25);
        }
    }
    // A screenshot-only launch creates no audio system, and Spectr rebuilds
    // ModulationSettings from parameters inside process(). Without audio the
    // LFO state therefore never leaves its defaults, and reading it back
    // measures the missing audio thread rather than the feature.
    if (const auto* keep = std::getenv("SPECTR_SCREENSHOT_KEEPS_AUDIO");
        keep != nullptr && std::strcmp(keep, "1") == 0) {
        config.screenshot_keeps_audio = true;
    }
    app.set_config(config);
    return app.run_with_editor(/*use_gpu=*/true) ? 0 : 1;
}
