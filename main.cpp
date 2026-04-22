#include "spectr.hpp"
#include <pulp/format/standalone.hpp>

int main() {
    pulp::format::StandaloneApp app(spectr::create_spectr);
    pulp::format::StandaloneConfig config;
    config.input_channels = 2;
    config.output_channels = 2;
    app.set_config(config);
    return app.run_with_editor(false) ? 0 : 1;
}
