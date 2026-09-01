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
// reference, and it is also the LIVE geometry: the root view always lays out at
// this box and the host applies one uniform scale at paint time. The editor does
// NOT reflow at the host bounds -- see make_editor_view_size() below for why the
// earlier reflowing (`viewport_policy = Responsive`) behaviour was replaced.
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

// The member probes keep this product-side size contract tolerant of additive
// ViewSize fields. SDK admission itself is intentionally stricter in CMake:
// shipping Spectr requires the dedicated native-view target and may pin one
// exact SDK source SHA.
// Resolved target for an editor-owned resize gesture.
struct EditorResizeTarget {
    std::uint32_t width;
    std::uint32_t height;
};

// Resolve a resize-grip drag into an aspect-locked size inside the declared
// host bounds.
//
// AU v2 has no host->plugin resize callback, so the editor owns its resize
// affordance and asks the host for a size (see `Spectr::create_native_editor_`).
// That makes this the single place the two guarantees live: the authored aspect
// ratio is preserved exactly, and the result never leaves
// [kEditorMinimum*, kEditorMaximum*] — so a drag cannot produce a geometry the
// responsive layout was never validated at.
//
// `dx` / `dy` are cumulative pointer deltas from the drag start; `base_*` is the
// editor size when the drag began. The axis the user moved further (compared in
// width-equivalent units) drives the result, which keeps a mostly-vertical drag
// from feeling dead.
inline EditorResizeTarget resolve_editor_resize(std::uint32_t base_width,
                                                std::uint32_t base_height,
                                                double dx,
                                                double dy) {
    const double from_x = static_cast<double>(base_width) + dx;
    const double from_y =
        (static_cast<double>(base_height) + dy) * kEditorAspectRatio;
    double width = (dx * dx >= dy * dy * kEditorAspectRatio * kEditorAspectRatio)
        ? from_x
        : from_y;

    const auto clamp = [](double value, double low, double high) {
        return value < low ? low : (value > high ? high : value);
    };
    width = clamp(width, static_cast<double>(kEditorMinimumWidth),
                  static_cast<double>(kEditorMaximumWidth));
    double height = width / kEditorAspectRatio;
    if (height < static_cast<double>(kEditorMinimumHeight)
        || height > static_cast<double>(kEditorMaximumHeight)) {
        height = clamp(height, static_cast<double>(kEditorMinimumHeight),
                       static_cast<double>(kEditorMaximumHeight));
        width = height * kEditorAspectRatio;
    }
    return EditorResizeTarget{
        static_cast<std::uint32_t>(width + 0.5),
        static_cast<std::uint32_t>(height + 0.5),
    };
}

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
    // Leave viewport_policy at Automatic. With a non-zero aspect and a
    // resizable min/max, `should_pin_design_viewport()` returns true, so the
    // host pins the root at the authored box and paint applies one uniform
    // aspect-correct scale. That is the "proportional only, no cropping"
    // contract: the layout is identical at every size, just larger or smaller.
    //
    // This deliberately replaces an earlier `= Responsive` override. Responsive
    // short-circuits the pin to false, which made the root reflow via Yoga at
    // the live host size — layout MODE changed with the window (the bottom rail
    // switched between one and two rows, the brand subtitle was hidden), and
    // type stayed at an absolute size instead of scaling. Every "bars are
    // misaligned at size X" symptom came from that reflow.
    return size;
}

} // namespace spectr
