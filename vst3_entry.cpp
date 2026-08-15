#include "spectr/spectr.hpp"
#include <pulp/format/vst3_entry.hpp>

#if defined(SPECTR_WEBVIEW_REFERENCE)
PULP_VST3_PLUGIN(
    Steinberg::FUID(0xB7D7C75B, 0xBC1C4CF9, 0xA71444BA, 0x53504E31),
    "Spectr WebView Reference",
    Steinberg::Vst::PlugType::kFx,
    "Pulp",
    "1.0.0",
    "",
    spectr::create_spectr
)
#elif defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
PULP_VST3_PLUGIN(
    Steinberg::FUID(0x2A1E66F4, 0x40A94790, 0xA1774EA7, 0x53504E50),
    "Spectr Native Preview",
    Steinberg::Vst::PlugType::kFx,
    "Pulp",
    "1.0.0",
    "",
    spectr::create_spectr
)
#else
PULP_VST3_PLUGIN(
    Steinberg::FUID(0xE0A36443, 0x43D1A08E, 0xC73C7FDC, 0xC7E5D370),
    "Spectr",
    Steinberg::Vst::PlugType::kFx,
    "Pulp",
    "1.0.0",
    "",
    spectr::create_spectr
)
#endif
