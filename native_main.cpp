#include "spectr/spectr.hpp"

#include <pulp/format/standalone.hpp>

#include <cstring>
#include <string>

int main(int argc, char** argv) {
    // The standalone window is natively resizable by macOS, which owns the
    // bottom-right corner; the editor must not draw a competing grip there.
    spectr::set_host_draws_native_resize(true);
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
    app.set_config(config);
    return app.run_with_editor(/*use_gpu=*/true) ? 0 : 1;
}
