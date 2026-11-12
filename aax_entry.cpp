// Spectr AAX entry point.
// The AAX SDK is supplied by the developer and kept outside this repository.

#include "include/spectr/spectr.hpp"

#include <pulp/format/aax_entry.hpp>

#if defined(PULP_AAX_HEADLESS)
PULP_AAX_PLUGIN(spectr::create_spectr)
#else
namespace {

// Pro Tools owns the AAX editor window and supplies resize callbacks.  The
// native grip is therefore enabled here only for the GUI AAX entry point; the
// adapter remains the authority for the host frame dimensions.
const bool g_aax_owns_resize_grip = [] {
    spectr::set_editor_owns_resize_grip(true);
    return true;
}();

}  // namespace

PULP_AAX_PLUGIN_WITH_GUI(spectr::create_spectr)
#endif
