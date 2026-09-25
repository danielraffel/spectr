#pragma once

/// @file render_mode.hpp
/// The one place Spectr decides what a render mode is called, which one a new
/// instance starts in, and how it is exposed.
///
/// Every user-visible string and every default for the mask render mode lives
/// here and nowhere else. Code that needs to know "what is this mode called"
/// or "what does a fresh instance use" asks this header; nothing re-states an
/// answer locally. That is deliberate: the naming and the default are a
/// product ruling, and a ruling that is written down in six places is a ruling
/// that will eventually disagree with itself.
///
/// What is NOT a ruling, and cannot be changed here: which mode a **saved
/// project** recalls. A project that was authored before this mode existed
/// always reopens as `linear_phase`, because that is what it was authored
/// through — see `Spectr::deserialize_plugin_state`. `kDefaultRenderMode`
/// applies to a NEW instance only and never retro-fits an old one.

#include "spectr/mask_renderer.hpp"

#include <cstddef>
#include <string_view>

namespace spectr {

/// The mode a NEW instance starts in.
///
/// Applies to a fresh instance only. It is never consulted when restoring a
/// project: a pre-parameter project pins `linear_phase`, and a project saved
/// since carries its authored mode explicitly. Changing this value therefore
/// cannot change how any existing session sounds or what latency it reports.
/// Tracking: it plays in time with the user, and the latency Mixing reports
/// (about 213 ms at 48 kHz) makes the editor and monitoring feel sluggish for
/// a first session. Mixing remains one click or `T` away for the deepest cuts.
inline constexpr MaskRenderMode kDefaultRenderMode = MaskRenderMode::zero_latency;

/// Whether the mode is exposed to the host as an automatable parameter.
///
/// Pulp's `ParamInfo` carries no "not automatable" flag: a parameter
/// registered with the `StateStore` is visible to host automation, full stop.
/// So this is not a presentation choice, it is a choice about whether the mode
/// is a `StateStore` parameter at all. It is `false` because switching mode
/// rebuilds the renderer — an allocating, control-thread operation — and a
/// host automation lane can request that per block. The mode is saved state
/// and an editor control; it is not a knob a sequencer can sweep.
inline constexpr bool kRenderModeIsHostAutomatable = false;

/// Milliseconds of delay a mode costs at `sample_rate`.
///
/// Derived, never typed. Every user-facing figure -- the Settings sub-line,
/// the About guide, the status strip -- comes through here, so a change to a
/// mode's geometry cannot leave a stale number in the product.
[[nodiscard]] inline double render_mode_latency_ms(MaskRenderMode mode,
                                                   const MaskRendererConfig& config,
                                                   double sample_rate) noexcept {
    if (!(sample_rate > 0.0)) return 0.0;
    return 1000.0 * static_cast<double>(
        mask_render_latency_samples(mode, config)) / sample_rate;
}

/// Stable, machine-readable token for a mode. This is what a saved project
/// stores, so these strings are a compatibility surface: renaming one orphans
/// every project that used it. The user-facing label is separate and free to
/// change.
[[nodiscard]] constexpr std::string_view render_mode_token(MaskRenderMode mode) noexcept {
    switch (mode) {
    case MaskRenderMode::linear_phase: return "linear_phase";
    case MaskRenderMode::zero_latency: return "zero_latency";
    }
    return {};
}

/// Parse a stored token. Returns false for anything unrecognised rather than
/// falling back to a default: a project written by a newer build names a mode
/// this one cannot realise, and silently substituting another mode would
/// change how it sounds without saying so.
[[nodiscard]] constexpr bool render_mode_from_token(std::string_view token,
                                                    MaskRenderMode& out) noexcept {
    if (token == "linear_phase") { out = MaskRenderMode::linear_phase; return true; }
    if (token == "zero_latency") { out = MaskRenderMode::zero_latency; return true; }
    return false;
}

/// The name of the control these modes are options of.
inline constexpr std::string_view kRenderModeControlLabel = "Latency";

/// The user-facing name of a mode.
///
/// The names answer the question a musician actually has -- "can I play
/// through this?" -- rather than describing the mechanism. Neither is the
/// better one and neither name may imply it is: the modes differ on two
/// separate axes, so each name says what the mode is *for* and leaves the
/// choice where it belongs.
///
/// What these are deliberately NOT:
///  - "Zero latency", because it is false. The mode costs 64 samples.
///  - "Linear phase" / "Minimum phase", because those are mechanism. Fine in
///    developer docs, not on a control.
///  - "Live", because Motion Mode already uses it for something else, and two
///    controls reading "Live" in one Settings panel is a support ticket.
[[nodiscard]] constexpr std::string_view render_mode_label(MaskRenderMode mode) noexcept {
    switch (mode) {
    case MaskRenderMode::linear_phase: return "Mixing";
    case MaskRenderMode::zero_latency: return "Tracking";
    }
    return {};
}

/// One line of honest guidance per mode: what it costs and what it buys.
///
/// The millisecond figures are deliberately absent from these strings. They
/// are a function of the mode and the live sample rate, so a caller derives
/// them from `mask_render_latency_samples` rather than reading a number that
/// was typed once and is wrong at 96 kHz.
[[nodiscard]] constexpr std::string_view render_mode_description(MaskRenderMode mode) noexcept {
    switch (mode) {
    case MaskRenderMode::linear_phase:
        return "Deepest cuts. Adds latency your DAW lines up automatically. "
               "Not for playing through live.";
    case MaskRenderMode::zero_latency:
        return "Plays in time with you. Very narrow bands cut less deeply.";
    }
    return {};
}

/// Every mode, in display order.
inline constexpr MaskRenderMode kRenderModes[] = {
    MaskRenderMode::linear_phase,
    MaskRenderMode::zero_latency,
};
inline constexpr std::size_t kRenderModeCount =
    sizeof(kRenderModes) / sizeof(kRenderModes[0]);

} // namespace spectr
