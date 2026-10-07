#pragma once

namespace spectr {

/// Request one subtle macOS alignment tick. Platforms without an AppKit
/// haptic performer provide a no-op implementation.
void mac_haptic_alignment_tick() noexcept;

} // namespace spectr
