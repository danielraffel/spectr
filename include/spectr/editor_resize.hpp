#pragma once

#include <cstdint>

namespace spectr {

#if !defined(SPECTR_AUTHORED_DESIGN_W) \
    || !defined(SPECTR_AUTHORED_DESIGN_H) \
    || !defined(SPECTR_HOST_PREFERRED_W) \
    || !defined(SPECTR_HOST_PREFERRED_H) \
    || !defined(SPECTR_HOST_MIN_W) \
    || !defined(SPECTR_HOST_MIN_H) \
    || !defined(SPECTR_HOST_MAX_W) \
    || !defined(SPECTR_HOST_MAX_H)
#error "Spectr host dimensions must come from the product build contract"
#endif

// Claude Design's authored coordinate system stays fixed at 1320 x 860.
// The host opens smaller by default and scales this design uniformly.
inline constexpr std::uint32_t kEditorDesignWidth = SPECTR_AUTHORED_DESIGN_W;
inline constexpr std::uint32_t kEditorDesignHeight = SPECTR_AUTHORED_DESIGN_H;
inline constexpr std::uint32_t kEditorPreferredWidth = SPECTR_HOST_PREFERRED_W;
inline constexpr std::uint32_t kEditorPreferredHeight = SPECTR_HOST_PREFERRED_H;
inline constexpr std::uint32_t kEditorMinimumWidth = SPECTR_HOST_MIN_W;
inline constexpr std::uint32_t kEditorMinimumHeight = SPECTR_HOST_MIN_H;
inline constexpr std::uint32_t kEditorMaximumWidth = SPECTR_HOST_MAX_W;
inline constexpr std::uint32_t kEditorMaximumHeight = SPECTR_HOST_MAX_H;
inline constexpr double kEditorAspectRatio =
    static_cast<double>(kEditorDesignWidth) / kEditorDesignHeight;
static_assert(kEditorPreferredWidth * kEditorDesignHeight
              == kEditorPreferredHeight * kEditorDesignWidth);
static_assert(kEditorMinimumWidth * kEditorDesignHeight
              == kEditorMinimumHeight * kEditorDesignWidth);
static_assert(kEditorMaximumWidth * kEditorDesignHeight
              == kEditorMaximumHeight * kEditorDesignWidth);

} // namespace spectr
