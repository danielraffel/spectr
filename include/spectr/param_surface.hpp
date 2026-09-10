#pragma once

// Spectr host parameter surface (spectr#34).
//
// Everything the user manipulates is a host-automatable parameter: 64 band
// gains, 64 band mutes, the A/B snapshot morph, the viewport (center +
// width in the log-frequency display domain), the band count, and the mode
// toggles — plus the legacy Mix/Output pair. The set is STATIC: all 64 band
// slots are registered unconditionally even though the visible layout shows
// 32/40/48/56/64, because hosts (Logic in particular) cache the parameter
// list and a runtime-mutating list invites stale automation lanes and
// rescans. Slots at or above the visible count are inert: they store and
// round-trip values but never enter the compiled mask.
//
// PARAMETER IDs ARE A PERMANENT COMPATIBILITY CONTRACT. Once shipped,
// moving or reusing an ID breaks saved sessions and automation lanes. The
// full scheme, including reserved ranges for future LFO/modulation work
// (spectr#36), lives in docs/parameter-surface.md — change nothing here
// without updating that document.

#include "spectr/band_state.hpp"
#include "spectr/viewport.hpp"

#include <pulp/state/parameter.hpp>

#include <cstddef>
#include <utility>

namespace pulp::state { class StateStore; }

namespace spectr {

// ── Control block ────────────────────────────────────────────────────────
// Legacy global params (Mix = 1, Output = 2) are declared with the Processor
// in spectr.hpp. IDs 3..999 are reserved headroom for future globals.

/// A/B snapshot morph position, 0 = A .. 1 = B.
inline constexpr pulp::state::ParamID kParamMorph = 3000;
/// Visible window center, log10 Hz (display domain, see pattern.cpp).
inline constexpr pulp::state::ParamID kParamViewportCenter = 3001;
/// Visible window width, log10 decades. Center+width (not min/max edges)
/// so independent automation of the two can never invert the window.
inline constexpr pulp::state::ParamID kParamViewportWidth = 3002;
/// Visible band count: 32/40/48/56/64, stepped.
inline constexpr pulp::state::ParamID kParamBandCount = 3003;

// ── Mode toggles (enum params; the editor observes them via the bridge) ──
inline constexpr pulp::state::ParamID kParamMotionMode    = 3100;  // Live/Precision
inline constexpr pulp::state::ParamID kParamAnalyzerMode  = 3101;  // Peak/Avg/Both/Off
inline constexpr pulp::state::ParamID kParamEditMode      = 3102;  // Sculpt..Glide
inline constexpr pulp::state::ParamID kParamVisualization = 3103;  // Bars/Response/Both

// Internal tempo-synced LFO. These are ordinary static host parameters so a
// host or third-party modulator can automate them while the derived LFO output
// remains a non-destructive DSP overlay.
inline constexpr pulp::state::ParamID kParamLfoEnabled = 4000;
inline constexpr pulp::state::ParamID kParamLfoShape   = 4001;
inline constexpr pulp::state::ParamID kParamLfoRate    = 4002; // beats/cycle
inline constexpr pulp::state::ParamID kParamLfoDepth   = 4003;
inline constexpr pulp::state::ParamID kParamLfoTarget  = 4004;
inline constexpr pulp::state::ParamID kParamLfo2Enabled = 4010;
inline constexpr pulp::state::ParamID kParamLfo2Shape   = 4011;
inline constexpr pulp::state::ParamID kParamLfo2Rate    = 4012;
inline constexpr pulp::state::ParamID kParamLfo2Depth   = 4013;

// ── Band blocks ──────────────────────────────────────────────────────────
// Band i gain = kParamBandGainBase + i, mute = kParamBandMuteBase + i.
// 1064..1999 and 2064..2999 are reserved growth tails for those blocks.
inline constexpr pulp::state::ParamID kParamBandGainBase = 1000;
inline constexpr pulp::state::ParamID kParamBandMuteBase = 2000;

constexpr pulp::state::ParamID band_gain_param_id(std::size_t band) noexcept {
    return kParamBandGainBase + static_cast<pulp::state::ParamID>(band);
}
constexpr pulp::state::ParamID band_mute_param_id(std::size_t band) noexcept {
    return kParamBandMuteBase + static_cast<pulp::state::ParamID>(band);
}

/// Total registered parameters: 2 legacy + 64 gain + 64 mute + 4 control
/// (morph, center, width, count) + 4 modes + 9 internal LFO controls.
inline constexpr std::size_t kSurfaceParamCount = 147;

// ── Viewport log-frequency encoding ─────────────────────────────────────
// The display mapping (pattern.cpp) spans log10(20)..log10(20000), so the
// parameters live in that same domain: automating center pans musically
// instead of linearly in Hz.
inline constexpr float kViewportLogMinHz = 1.3010299956639813f;  // log10(20)
inline constexpr float kViewportLogMaxHz = 4.3010299956639813f;  // log10(20000)
/// Minimum window width: one octave. A narrower window serves no musical
/// purpose and keeps the decode clamp well away from a zero-width window.
inline constexpr float kViewportMinWidthLog = 0.3010299956639812f;  // log10(2)
inline constexpr float kViewportMaxWidthLog =
    kViewportLogMaxHz - kViewportLogMinHz;  // 3.0 decades
inline constexpr float kViewportDefaultCenterLog =
    (kViewportLogMinHz + kViewportLogMaxHz) * 0.5f;

/// Viewport -> (center_log, width_log). Always exact for a valid viewport.
std::pair<float, float> encode_viewport(const Viewport& v) noexcept;

/// (center_log, width_log) -> Viewport. Width is clamped into
/// [kViewportMinWidthLog, kViewportMaxWidthLog]; center is then clamped so
/// the window stays inside [20 Hz, 20 kHz]. The result always passes
/// Viewport::valid() — the encoding cannot invert.
Viewport decode_viewport(float center_log, float width_log) noexcept;

/// Snap a raw band-count parameter value to the nearest legal layout.
Layout layout_from_param_value(float value) noexcept;
/// The exact parameter value for a layout (32/40/48/56/64).
float param_value_from_layout(Layout layout) noexcept;

/// Register the complete static surface (parameters + groups) with the
/// store. Called once from Spectr::define_parameters; registration is not
/// thread-safe per the StateStore contract.
void register_surface_params(pulp::state::StateStore& store);

namespace detail {

// Applied-value cache slot layout (Spectr's sync engine; see spectr.hpp).
// 0..63 band gains, 64..127 band mutes, then the control/mode block.
inline constexpr std::size_t kSlotMorph      = 128;
inline constexpr std::size_t kSlotCenter     = 129;
inline constexpr std::size_t kSlotWidth      = 130;
inline constexpr std::size_t kSlotBandCount  = 131;
inline constexpr std::size_t kSlotModeBase   = 132;  // +0..3: motion/analyzer/edit/visualization
inline constexpr std::size_t kSlotLfoBase    = 136;  // +0..4: enabled/shape/rate/depth/target
inline constexpr std::size_t kSlotLfo2Base   = 141;  // +0..3: enabled/shape/rate/depth
inline constexpr std::size_t kSurfaceSlots   = 145;

constexpr pulp::state::ParamID surface_slot_param_id(std::size_t slot) noexcept {
    if (slot < 64) return band_gain_param_id(slot);
    if (slot < 128) return band_mute_param_id(slot - 64);
    switch (slot) {
        case kSlotMorph:     return kParamMorph;
        case kSlotCenter:    return kParamViewportCenter;
        case kSlotWidth:     return kParamViewportWidth;
        case kSlotBandCount: return kParamBandCount;
        default:
            if (slot < kSlotLfoBase)
                return kParamMotionMode
                    + static_cast<pulp::state::ParamID>(slot - kSlotModeBase);
            if (slot < kSlotLfo2Base)
                return kParamLfoEnabled
                    + static_cast<pulp::state::ParamID>(slot - kSlotLfoBase);
            return kParamLfo2Enabled
                + static_cast<pulp::state::ParamID>(slot - kSlotLfo2Base);
    }
}

} // namespace detail

} // namespace spectr
