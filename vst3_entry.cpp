#include "spectr/spectr.hpp"
#include <pulp/format/vst3_entry.hpp>

namespace {

// VST3 republishes a tail change as restartComponent(kReloadComponent), and
// JUCE-based hosts answer that with a full deactivate/reactivate -- an audible
// gap on every Freeze press. This build therefore reports a constant infinite
// tail and never flags a tail edge; see `spectr::set_constant_infinite_tail`.
// Constant-initialized flag, so static-init order is safe (as in the AU v2
// entry's resize-grip initializer).
const bool g_vst3_constant_infinite_tail = [] {
    spectr::set_constant_infinite_tail(true);
    return true;
}();

}  // namespace

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
#elif defined(SPECTR_DEV_IDENTITY)
// Class id derived from the dev bundle id in cmake/SpectrIdentity.cmake.
PULP_VST3_PLUGIN(
    Steinberg::FUID(SPECTR_DEV_VST3_UID0, SPECTR_DEV_VST3_UID1,
                    SPECTR_DEV_VST3_UID2, SPECTR_DEV_VST3_UID3),
    SPECTR_DEV_PLUGIN_NAME,
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
