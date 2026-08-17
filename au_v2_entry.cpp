#include "spectr/spectr.hpp"
#include <pulp/format/au_v2_entry.hpp>

#if defined(SPECTR_WEBVIEW_REFERENCE)
PULP_AU_PLUGIN(SpectrWebViewReferenceAU, spectr::create_spectr)
#elif defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
PULP_AU_PLUGIN(SpectrNativePreviewAU, spectr::create_spectr)
#else
PULP_AU_PLUGIN(SpectrAU, spectr::create_spectr)
#endif
