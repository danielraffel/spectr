// Spectr AAX entry point.
// The AAX SDK is supplied by the developer and kept outside this repository.

#include "include/spectr/spectr.hpp"

#include <pulp/format/aax_entry.hpp>

#if defined(PULP_AAX_HEADLESS)
PULP_AAX_PLUGIN(spectr::create_spectr)
#else
PULP_AAX_PLUGIN_WITH_GUI(spectr::create_spectr)
#endif
