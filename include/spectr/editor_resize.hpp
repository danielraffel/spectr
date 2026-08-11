#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

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

struct EditorResizeSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend constexpr bool operator==(const EditorResizeSize&,
                                     const EditorResizeSize&) = default;
};

/// Validate an untrusted editor resize request, clamp it to Spectr's supported
/// host bounds, and derive height from the authored aspect ratio. Width is the
/// canonical dimension. Every published bound is an exact multiple of the
/// authored 66:43 aspect, so repeated drag callbacks cannot accumulate drift.
inline std::optional<EditorResizeSize> normalize_editor_resize(
    double requested_width, double requested_height) noexcept {
    if (!std::isfinite(requested_width) || !std::isfinite(requested_height)
        || requested_width <= 0.0 || requested_height <= 0.0)
        return std::nullopt;

    const double bounded_width = std::clamp(
        requested_width,
        static_cast<double>(kEditorMinimumWidth),
        static_cast<double>(kEditorMaximumWidth));
    const auto width = static_cast<std::uint32_t>(std::llround(bounded_width));
    const auto height = static_cast<std::uint32_t>(std::llround(
        static_cast<double>(width) / kEditorAspectRatio));
    return EditorResizeSize{
        width,
        std::clamp(height, kEditorMinimumHeight, kEditorMaximumHeight),
    };
}

} // namespace spectr
