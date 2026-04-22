#include "spectr/spectr.hpp"
#include <pulp/format/vst3_entry.hpp>

PULP_VST3_PLUGIN(
    Steinberg::FUID(0xE0A36443, 0x43D1A08E, 0xC73C7FDC, 0xC7E5D370),
    "Spectr",
    Steinberg::Vst::PlugType::kFx,
    "Pulp",
    "1.0.0",
    "",
    spectr::create_spectr
)
