#include "spectr/spectr.hpp"
#include <pulp/format/clap_entry.hpp>
#if defined(SPECTR_SHARED_NATIVE_HOST_PROBE)
#include "spectr/experimental/shared_host_probe_entry.hpp"
PULP_CLAP_PLUGIN(spectr::host_probe::create)
#else
PULP_CLAP_PLUGIN(spectr::create_spectr)
#endif
