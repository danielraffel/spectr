#include "spectr/spectr.hpp"
#include <pulp/format/standalone.hpp>

#include <cstring>
#include <string>

int main(int argc, char** argv) {
    pulp::format::StandaloneApp app(spectr::create_spectr);
    pulp::format::StandaloneConfig config;
    config.input_channels  = 2;
    config.output_channels = 2;
    // Spectr's editor IS the whole app — no need for Pulp's built-in
    // Editor/Settings tab bar wrapping the plugin view. This also
    // eliminates the 32pt bottom strip the TabPanel used to leave and
    // the tab-header flash that used to show through the WebView during
    // first paint. Requires Pulp v0.38.0 (pulp#663 / PR#665).
    config.show_settings_tab = false;

    // Generic argv parsing — the SDK exposes
    // `StandaloneConfig::screenshot_path` (#106), so any pulp standalone
    // gets headless capture by parsing `--screenshot=PATH` here. Same
    // shape can later be moved into StandaloneApp::parse_args() for
    // every consumer to share, but the per-app version keeps main's
    // responsibilities visible.
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strncmp(arg, "--screenshot=", 13) == 0) {
            config.screenshot_path = arg + 13;
        } else if (std::strcmp(arg, "--screenshot") == 0 && i + 1 < argc) {
            config.screenshot_path = argv[++i];
        } else if (std::strncmp(arg, "--screenshot-frame-delay=", 25) == 0) {
            config.screenshot_frame_delay = std::stoi(arg + 25);
        }
    }

    app.set_config(config);
    // Second arg is `use_gpu`, not an editor toggle.
    return app.run_with_editor(/*use_gpu=*/true) ? 0 : 1;
}
