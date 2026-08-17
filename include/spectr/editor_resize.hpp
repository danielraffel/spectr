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

// Claude Design's 1320 x 860 capture remains the pixel-exact authored
// reference. The live editor reflows at the host bounds instead of scaling the
// whole tree, so type and hit targets remain readable at the minimum size.
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

// Keep Spectr source-compatible with the frozen Pulp SDK used for the N0
// baseline while taking advantage of the explicit authored-viewport contract
// in newer SDKs. The member probes are dependent on ViewSize, so an older
// seven-field ViewSize remains a supported compile target without preprocessor
// version guesses.
template <typename ViewSize>
constexpr ViewSize make_editor_view_size() {
    ViewSize size{
        kEditorPreferredWidth,
        kEditorPreferredHeight,
        kEditorMinimumWidth,
        kEditorMinimumHeight,
        kEditorMaximumWidth,
        kEditorMaximumHeight,
        kEditorAspectRatio,
    };
    if constexpr (requires(ViewSize value) {
                      value.design_width;
                      value.design_height;
                  }) {
        size.design_width = kEditorDesignWidth;
        size.design_height = kEditorDesignHeight;
    }
    if constexpr (requires(ViewSize value) { value.viewport_policy; }) {
        size.viewport_policy = decltype(size.viewport_policy)::Responsive;
    }
    return size;
}

} // namespace spectr
