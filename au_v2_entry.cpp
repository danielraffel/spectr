#include "spectr/spectr.hpp"
#include <pulp/format/au_v2_entry.hpp>

namespace {

// AU v2 is the ONE format Spectr ships where the user has no way to resize the
// editor: the format hands a size plugin-ward once at view creation and never
// again, and Logic's plug-in window offers no grow area. So this build — and
// only this build — draws its own corner grip. See
// `spectr::set_editor_owns_resize_grip` for why the other formats must not.
//
// Asserted from a static initializer because the decision belongs to the linked
// entry point and there is no wrapper-type query on `Processor` to ask instead.
// Safe against static-init order: the flag it writes is a namespace-scope
// `std::atomic<bool>` with a constexpr constructor, so it is constant-
// initialized before any dynamic initializer in the image can run.
const bool g_au_v2_owns_resize_grip = [] {
    spectr::set_editor_owns_resize_grip(true);
    return true;
}();

}  // namespace

#if defined(SPECTR_WEBVIEW_REFERENCE)
PULP_AU_PLUGIN(SpectrWebViewReferenceAU, spectr::create_spectr)
#elif defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
PULP_AU_PLUGIN(SpectrNativePreviewAU, spectr::create_spectr)
#else
PULP_AU_PLUGIN(SpectrAU, spectr::create_spectr)
#endif
