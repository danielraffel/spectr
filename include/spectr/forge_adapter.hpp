#pragma once

// Optional in-process consumer seam for Forge and other Pulp graph hosts.
// Spectr remains the authority for its renderer, parameters, provider policy,
// and fallback accounting. This adapter only exposes the existing Processor
// factory; it does not copy or re-register any DSP/control definitions.

#include <spectr/spectr.hpp>

namespace spectr::forge_adapter {

inline std::unique_ptr<pulp::format::Processor> create_processor() {
    return create_spectr();
}

}  // namespace spectr::forge_adapter
