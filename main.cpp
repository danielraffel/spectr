#include "spectr/spectr.hpp"
#include <pulp/format/standalone.hpp>

int main() {
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
    app.set_config(config);
    // Second arg is `use_gpu`, not an editor toggle.
    return app.run_with_editor(/*use_gpu=*/true) ? 0 : 1;
}
