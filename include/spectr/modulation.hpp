#pragma once

#include "spectr/band_state.hpp"
#include "spectr/snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>

namespace spectr {

enum class LfoShape : std::uint8_t { Sine, Triangle, Square, Saw };

/// Where an LFO can be routed. The first four are the original destinations
/// and keep their values: the legacy single-target host lane (`4004`) indexes
/// them directly. The two viewport targets were appended after them.
enum class ModulationTarget : std::uint8_t {
    WholeBank, SnapshotA, SnapshotB, Morph,
    ViewportPosition,  ///< "Band shift" in the UI and the host
    ViewportZoom,      ///< "Band spread"
    Freeze,            ///< gates LIVE / FROZEN; Depth is the frozen duty
    Length,            ///< steps the next freeze's loop length
    Intensity,         ///< scales the Intensity amount toward flat (unipolar)
    Mix,               ///< pulls Mix toward dry (unipolar): the freeze blend
    Output,            ///< moves Output trim in dB, after Auto Gain (bipolar)
    Bands,             ///< steps the visible band count around the user's
    Preset,            ///< morphs toward neighbouring presets' band fields
};

/// Destinations the legacy single-target lane can name (Bank..Morph).
inline constexpr std::size_t kLegacyModulationTargetCount = 4;
/// Every routable destination.
inline constexpr std::size_t kModulationTargetCount = 13;
/// Internal LFOs.
inline constexpr std::size_t kLfoCount = 2;

/// Sentinel meaning "no explicit destination selection has been made", so the
/// destination follows the single-target `ModulationSettings::target` enum
/// (the host-automatable lane). It is deliberately distinct from `0x00`,
/// which is an explicit *empty* selection — the user asked for no
/// destinations and modulation is silent.
inline constexpr std::uint8_t kModulationTargetMaskUnset = 0xFF;

/// Every LEGACY destination selected (Bank, A, B, Morph).
inline constexpr std::uint8_t kModulationTargetMaskAll = 0x0F;
/// Every routable destination selected. Routing masks are 16-bit: there are
/// more destinations than the 8 bits the legacy `target_mask` uses.
inline constexpr std::uint16_t kModulationRouteMaskAll = 0x1FFF;

/// One LFO -> destination route. `amount` IS this destination's depth
/// (effective modulation = wave x amount); there is no LFO-level depth, so one
/// LFO can sweep the viewport gently while it pumps the bank hard. (The legacy
/// LFO Depth lanes are commands that set the amount of every enabled route.)
struct ModulationRoute {
    bool  enabled = false;
    float amount  = 0.5f;  ///< the destination's Depth, 0..1
};
using LfoRoutes = std::array<ModulationRoute, kModulationTargetCount>;

/// A fresh LFO drives the whole bank at 50 % Depth: the destination and depth a
/// 1.0.x instance opened with, so a new instance sounds as it did.
inline constexpr LfoRoutes default_lfo_routes() noexcept {
    LfoRoutes routes{};
    routes[static_cast<std::size_t>(ModulationTarget::WholeBank)].enabled = true;
    return routes;
}

struct ModulationSettings {
    bool enabled = false;
    LfoShape shape = LfoShape::Sine;
    float beats_per_cycle = 4.0f;
    float depth = 0.5f;
    /// The legacy single-target lane (`4004`). Read-mostly: see
    /// docs/modulation.md. Routing is `routes`.
    ModulationTarget target = ModulationTarget::WholeBank;
    bool lfo2_enabled = false;
    LfoShape lfo2_shape = LfoShape::Sine;
    float lfo2_beats_per_cycle = 4.0f;
    float lfo2_depth = 0.0f;
    // Legacy destination mask for ALL/NONE composition; bits 0..3 map
    // Bank/A/B/Morph. `kModulationTargetMaskUnset` defers to `target`. Only
    // the single-LFO compatibility path (`apply_internal_modulation`) and
    // migration of a pre-routing session read it.
    std::uint8_t target_mask = kModulationTargetMaskUnset;
    /// Per-LFO routing: index 0 is LFO 1, 1 is LFO 2. Each LFO drives every
    /// enabled destination at once. On the audio owner `amount` carries the
    /// SLEWED route level (see `slew_lfo_level`) rather than the raw lane.
    std::array<LfoRoutes, kLfoCount> routes{default_lfo_routes(),
                                            default_lfo_routes()};
};

/// The destinations @p routes enables, as a bit mask in enum order.
inline constexpr std::uint16_t route_mask(const LfoRoutes& routes) noexcept {
    std::uint16_t mask = 0;
    for (std::size_t t = 0; t < kModulationTargetCount; ++t)
        if (routes[t].enabled) mask = static_cast<std::uint16_t>(mask | (1u << t));
    return mask;
}

/// Replace the enabled flags of @p routes with @p mask (amounts untouched).
inline constexpr void set_route_mask(LfoRoutes& routes, std::uint16_t mask) noexcept {
    for (std::size_t t = 0; t < kModulationTargetCount; ++t)
        routes[t].enabled = (mask >> t) & 1u;
}

/// The single mask bit that stands for @p target.
inline constexpr std::uint8_t modulation_target_bit(ModulationTarget target) noexcept {
    return static_cast<std::uint8_t>(std::uint8_t{1} << static_cast<std::uint8_t>(target));
}

inline float lfo_value(LfoShape shape, double phase) noexcept {
    phase -= std::floor(phase);
    switch (shape) {
        case LfoShape::Triangle:
            return phase < 0.5 ? static_cast<float>(4.0 * phase - 1.0)
                               : static_cast<float>(3.0 - 4.0 * phase);
        case LfoShape::Square: return phase < 0.5 ? 1.0f : -1.0f;
        case LfoShape::Saw: return static_cast<float>(2.0 * phase - 1.0);
        case LfoShape::Sine:
        default: return std::sin(static_cast<float>(phase * 6.2831853071795864769));
    }
}

/// A change of LFO shape crossfades from the old waveform to the new one
/// instead of switching at once. Both waveforms are evaluated at the same
/// phase, so the phase never moves; without the fade a switch lands wherever
/// the two shapes differ at that phase (a square at +1 switched to a saw at
/// -0.8 is a 1.8 step), which is heard as a click in the gains and seen as a
/// jump in the drawn bands.
inline constexpr double kLfoShapeFadeSeconds = 0.15;

struct LfoShapeFade {
    LfoShape from = LfoShape::Sine;
    LfoShape to   = LfoShape::Sine;
    float    mix  = 1.0f;  ///< 0 = `from`, 1 = `to` (fade complete)
};

/// Start at @p shape with no fade in progress.
inline constexpr LfoShapeFade settled_lfo_shape(LfoShape shape) noexcept {
    return LfoShapeFade{shape, shape, 1.0f};
}

/// Point the fade at @p shape. A change made while a fade is still running
/// starts the new fade from whichever shape currently dominates.
inline constexpr LfoShapeFade retarget_lfo_shape(LfoShapeFade fade,
                                                 LfoShape shape) noexcept {
    if (shape == fade.to) return fade;
    return LfoShapeFade{fade.mix < 0.5f ? fade.from : fade.to, shape, 0.0f};
}

/// The fade @p seconds later. Pure, so the editor can evaluate the fade at
/// its own frame time from what the audio owner published.
inline LfoShapeFade advance_lfo_shape(LfoShapeFade fade,
                                      double seconds) noexcept {
    if (fade.mix >= 1.0f || seconds <= 0.0) return fade;
    fade.mix = static_cast<float>(std::min(
        1.0, static_cast<double>(fade.mix) + seconds / kLfoShapeFadeSeconds));
    return fade;
}

inline float lfo_value(const LfoShapeFade& fade, double phase) noexcept {
    if (fade.mix >= 1.0f || fade.from == fade.to)
        return lfo_value(fade.to, phase);
    const float mix = std::clamp(fade.mix, 0.0f, 1.0f);
    const float a = lfo_value(fade.from, phase);
    const float b = lfo_value(fade.to, phase);
    return a + (b - a) * mix;
}

/// An LFO's audible LEVEL -- its depth while enabled, zero while off -- slews
/// toward its target instead of switching. Switching the LFO on at a crest, or
/// jumping its depth, would otherwise move every modulated band by up to the
/// full 12 dB excursion in one block, which is heard as a step. The phase and
/// shape are untouched, so a slewed enable fades the running waveform in where
/// it already is rather than restarting it.
///
/// Short on purpose: an LFO toggle is a performance gesture, so the ramp is
/// sized to remove the step and no longer. Full scale (0 -> 1) takes this
/// long; a smaller move takes proportionally less.
inline constexpr double kLfoLevelSlewSeconds = 0.06;

/// Route levels for the two VIEWPORT destinations slew more slowly. Fading a
/// viewport route in or out moves the whole filter bank across the spectrum
/// (up to a decade at full depth), and over 60 ms that is several times faster
/// than the fastest free-running sweep -- measured through the AU host as a
/// 1.7-2.2 dB/ms envelope step on a tone the bank passes over, against
/// 0.3-0.4 for the running LFO. A quarter second keeps the fade inside the
/// sweep's own speed and still reads as immediate.
inline constexpr double kViewportRouteSlewSeconds = 0.25;

/// The Intensity destination's route level slews like the Intensity knob
/// (level_controls.hpp, kIntensitySlewSeconds): a full-depth Intensity route
/// switched on at a crest moves every band at once, and over 60 ms that
/// restages faster than the AU-measured yardstick allows.
inline constexpr double kIntensityRouteSlewSeconds = 0.2;

/// Seconds for a full-scale route-level move of @p target.
inline constexpr double route_slew_seconds(std::size_t target) noexcept {
    if (target == static_cast<std::size_t>(ModulationTarget::Intensity))
        return kIntensityRouteSlewSeconds;
    return (target == static_cast<std::size_t>(ModulationTarget::ViewportPosition)
            || target == static_cast<std::size_t>(ModulationTarget::ViewportZoom))
        ? kViewportRouteSlewSeconds : kLfoLevelSlewSeconds;
}

/// Destinations that shape the band field or the window, and so feed the
/// modulated-field publication. Freeze and Length act on the freeze source.
inline constexpr bool modulation_target_moves_field(std::size_t target) noexcept {
    return target < static_cast<std::size_t>(ModulationTarget::Freeze)
        || target == static_cast<std::size_t>(ModulationTarget::Preset);
}

/// The level @p seconds later, moving toward @p target at full scale per
/// kLfoLevelSlewSeconds. Pure, so a test can reason about the ramp exactly.
inline float slew_lfo_level(float current, float target,
                            double seconds) noexcept {
    if (seconds <= 0.0 || current == target) return current;
    const float step = static_cast<float>(seconds / kLfoLevelSlewSeconds);
    if (current < target) return std::min(target, current + step);
    return std::max(target, current - step);
}

/// Re-impose the authored mute topology on a modulated field.
///
/// An LFO modulates LEVELS. It must never toggle a mute, in either direction.
/// Mute is a discrete act the user performed on the live field — `Band::muted`
/// is an explicit flag, "never `gain == -inf`" — and every destination here
/// reaches its field through `morph_fields`, which does not interpolate mute
/// but PICKS it wholesale from whichever slot dominates at the current t. Two
/// distinct defects follow from letting that pick survive into a modulated
/// frame:
///
///   - The Morph destination overwrites `out` entirely from the two
///     snapshots, so a band the user muted AFTER capturing them comes back
///     un-muted. It is then audible — `linear_gain()` gates on this flag —
///     and its painted height sweeps with the LFO while the editor keeps
///     drawing the mute badge, which is read from the authored field. That is
///     the muted-band jiggle, and its audible half is the serious one: a band
///     the user silenced is heard.
///   - The dominance rule flips at t = 0.5. Under a user-dragged morph the
///     user controls that crossing; under an LFO it is crossed twice per
///     cycle, so any band whose mute differs between the two endpoints
///     strobes at LFO rate. A discrete pop is exactly the glitch this guard
///     exists to prevent, so the rule is applied in both directions.
///
/// A muted band is excluded from modulation ENTIRELY rather than merely
/// re-flagged: it keeps its authored gain as well as its mute. While muted the
/// two are indistinguishable (0 linear gain, painted at the mute sentinel),
/// but they differ the instant the user unmutes — restoring the authored level
/// is deterministic, whereas keeping the modulated one would reveal whatever
/// phase the LFO happened to be at.
///
/// This is deliberately scoped to internal modulation. A user-dragged morph
/// still moves mute by the dominance rule; that is a direct manipulation the
/// user is driving and watching, not a modulator running behind their back.
inline void preserve_authored_mutes(BandField& out,
                                    const BandField& canonical) noexcept {
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        if (canonical.bands[i].muted) out.bands[i] = canonical.bands[i];
        else                          out.bands[i].muted = false;
    }
}

// ── Combining routes ─────────────────────────────────────────────────────
//
// Each LFO can drive several destinations, and both LFOs can drive the same
// one. The combination is defined in two steps so it is deterministic and
// level-safe whatever is stacked:
//
//  1. SUM per destination. Every (LFO, destination) route contributes
//     `wave x Depth` to that destination's coordinate, where Depth is the
//     route's own (`amount`) and the LFO's on/off ramp gates it (unipolar
//     `(wave + 1) / 2 x Depth` for the snapshot destinations, which
//     pull toward a captured shape and back). Contributions from the two
//     LFOs add, so the result does not depend on which LFO is visited first.
//  2. APPLY each destination once, in a fixed order, clamping once:
//       Morph -> Snapshot A -> Snapshot B -> Bank        (band levels)
//       Band spread, Band shift                 (band frequencies)
//     Morph and the snapshots RESHAPE the field (each is a blend between
//     fields, so every band stays inside the range its inputs span); Bank then
//     OFFSETS that shape, so a Bank wobble rides on top of a morphing shape
//     rather than being overwritten by it. The final offset is clamped into the
//     band range once.
//
// Every destination is the identity at coordinate 0, so a route fading in or
// out (its level is slewed like the LFO's own, see `slew_lfo_level`) moves the
// sound continuously from the unmodulated field.

/// Full-depth excursions, per destination.
inline constexpr float kModulationBankExcursionDb = 12.0f;
inline constexpr float kModulationMorphExcursion = 0.5f;
/// Band shift: the window centre moves by up to one decade each way
/// (about 3.3 octaves), keeping its width.
inline constexpr float kModulationViewportPositionDecades = 1.0f;
/// Band spread: the window width is scaled by up to 2x wider / 2x narrower
/// (in log-frequency), about its centre.
inline constexpr float kModulationViewportZoomOctaves = 1.0f;

/// The summed modulation coordinate of every destination.
struct ModulationCoordinates {
    std::array<float, kModulationTargetCount> value{};

    float operator[](ModulationTarget t) const noexcept {
        return value[static_cast<std::size_t>(t)];
    }
    bool moves_viewport() const noexcept {
        return (*this)[ModulationTarget::ViewportPosition] != 0.0f
            || (*this)[ModulationTarget::ViewportZoom] != 0.0f;
    }
};

inline constexpr bool modulation_target_is_unipolar(ModulationTarget t) noexcept {
    return t == ModulationTarget::SnapshotA || t == ModulationTarget::SnapshotB
        ;
}

// ── Level destinations: Intensity, Mix, Output ──────────────────────────
//
// These move a level control around the user's setting and never write it:
// the knob keeps its value, the host lane keeps its automation.
//
//   Intensity, Mix, and Output use the normalized range of the underlying
//              parameter. At 100% depth, -1 reaches the real minimum and +1
//              reaches the real maximum, regardless of the authored value.
//
// Auto Gain is computed from the UNMODULATED Intensity and Mix as well, so
// no LFO on a level target is cancelled by it.

/// Full-depth Output excursion, dB each way (12 dB peak to peak).
inline constexpr float kModulationOutputExcursionDb = 6.0f;
/// The Output trim range (spectr.cpp registers the lane with it).
inline constexpr float kOutputTrimMinDb = -24.0f;
inline constexpr float kOutputTrimMaxDb = 24.0f;

inline float normalized_modulated_value(float base, float minimum, float maximum,
                                        float coordinate) noexcept {
    if (!std::isfinite(base) || !std::isfinite(coordinate)
        || !(maximum > minimum)) return std::clamp(base, minimum, maximum);
    const float b = std::clamp(base, minimum, maximum);
    const float c = std::clamp(coordinate, -1.0f, 1.0f);
    return c < 0.0f ? b + c * (b - minimum)
                    : b + c * (maximum - b);
}

/// The 0..1 pull a unipolar level destination's coordinate asks for.
inline float level_pull(const ModulationCoordinates& coords,
                        ModulationTarget target) noexcept {
    const float c = coords[target];
    return std::isfinite(c) ? std::clamp(c, 0.0f, 1.0f) : 0.0f;
}

/// Intensity (0..1 factor) after the Intensity destination.
inline float modulated_intensity(float base, const ModulationCoordinates& coords) noexcept {
    return normalized_modulated_value(base, 0.0f, 1.0f,
                                      coords[ModulationTarget::Intensity]);
}

/// Mix (0..1, 1 = wet) after the Mix destination.
inline float modulated_mix(float base, const ModulationCoordinates& coords) noexcept {
    return normalized_modulated_value(base, 0.0f, 1.0f,
                                      coords[ModulationTarget::Mix]);
}

/// The Output destination's offset in dB, before the trim-range clamp.
inline float output_modulation_db(const ModulationCoordinates& coords) noexcept {
    const float c = coords[ModulationTarget::Output];
    return std::isfinite(c) ? c * kModulationOutputExcursionDb : 0.0f;
}

/// Output trim in dB after the Output destination, clamped to the lane range.
inline float modulated_output_trim_db(float base_db,
                                      const ModulationCoordinates& coords) noexcept {
    return normalized_modulated_value(base_db, kOutputTrimMinDb,
                                      kOutputTrimMaxDb,
                                      coords[ModulationTarget::Output]);
}

inline float output_modulation_db(float base_db,
                                  const ModulationCoordinates& coords) noexcept {
    return modulated_output_trim_db(base_db, coords) - base_db;
}

/// Add one LFO's contribution. @p level is the LFO's (slewed) on/off level --
/// 1 while on, 0 while off, ramping between; the depth of each destination is
/// its route's `amount`. @p wave is the bipolar sample.
inline void accumulate_modulation(ModulationCoordinates& coords,
                                  const LfoRoutes& routes, float level,
                                  float wave) noexcept {
    if (!(level > 0.0f)) return;
    const float w = std::clamp(wave, -1.0f, 1.0f);
    const float depth = std::clamp(level, 0.0f, 1.0f);
    for (std::size_t t = 0; t < kModulationTargetCount; ++t) {
        const auto& route = routes[t];
        if (!route.enabled) continue;
        const float amount = std::clamp(route.amount, 0.0f, 1.0f);
        const float shaped = modulation_target_is_unipolar(
                                 static_cast<ModulationTarget>(t))
            ? (w + 1.0f) * 0.5f : w;
        coords.value[t] += shaped * depth * amount;
    }
}

/// Apply the level destinations (Morph, Snapshot A, Snapshot B, Bank) to
/// @p canonical. Never mutates its inputs; the authored mute topology always
/// survives (`preserve_authored_mutes`).
inline BandField apply_field_modulation(const BandField& canonical,
                                        const SnapshotBank& snapshots,
                                        float host_morph,
                                        const ModulationCoordinates& coords) noexcept {
    BandField out = canonical;
    // Morph moves the morph position, and the field follows by the DIFFERENCE
    // that move makes: identity at coordinate 0, and composable with what is
    // already in the field (macros, overrides) instead of replacing it.
    const float morph = coords[ModulationTarget::Morph];
    if (morph != 0.0f && snapshots.has(SnapshotBank::Slot::A)
        && snapshots.has(SnapshotBank::Slot::B)) {
        const float base_t = std::clamp(host_morph, 0.0f, 1.0f);
        const float moved_t = std::clamp(
            base_t + morph * kModulationMorphExcursion, 0.0f, 1.0f);
        if (moved_t != base_t) {
            BandField at_base{}, at_moved{};
            morph_fields(at_base, snapshots.a.field, snapshots.b.field, base_t);
            morph_fields(at_moved, snapshots.a.field, snapshots.b.field, moved_t);
            for (std::size_t i = 0; i < kMaxBands; ++i)
                out.bands[i].gain_db = std::clamp(
                    out.bands[i].gain_db
                        + (at_moved.bands[i].gain_db - at_base.bands[i].gain_db),
                    kBandGainMinDb, kBandGainMaxDb);
        }
    }
    // Snapshot destinations pull from the current field toward the captured
    // shape. A blend: every band stays between its two inputs.
    const auto pull = [&](SnapshotBank::Slot slot, ModulationTarget target) {
        const float amount = std::clamp(coords[target], 0.0f, 1.0f);
        if (amount <= 0.0f || !snapshots.has(slot)) return;
        const BandField from = out;
        morph_fields(out, from, snapshots.get(slot).field, amount);
    };
    pull(SnapshotBank::Slot::A, ModulationTarget::SnapshotA);
    pull(SnapshotBank::Slot::B, ModulationTarget::SnapshotB);
    // Bank offsets whatever shape the reshaping stages produced, clamped once.
    const float bank = coords[ModulationTarget::WholeBank];
    if (bank != 0.0f) {
        const float delta = bank * kModulationBankExcursionDb;
        for (auto& band : out.bands)
            band.gain_db = std::clamp(band.gain_db + delta,
                                      kBandGainMinDb, kBandGainMaxDb);
    }
    preserve_authored_mutes(out, canonical);
    return out;
}

/// The audible viewport: @p base (the user's window, after any morph
/// derivation) moved by the viewport destinations. Zoom scales the width about
/// the centre; position slides the centre keeping the (zoomed) width. Clamped
/// into 20 Hz..20 kHz with a one-octave minimum width, the same envelope
/// `decode_viewport` enforces, and an edge reached by position holds the width
/// rather than squeezing it. Exactly @p base when neither destination moves.
inline Viewport apply_viewport_modulation(const Viewport& base,
                                          const ModulationCoordinates& coords) noexcept {
    if (!coords.moves_viewport() || !base.valid()) return base;
    constexpr float kLogMin = 1.3010299956639813f;   // log10(20)
    constexpr float kLogMax = 4.3010299956639813f;   // log10(20000)
    constexpr float kMinWidth = 0.3010299956639812f; // log10(2): one octave
    const float lmin = std::log10(base.min_hz);
    const float lmax = std::log10(base.max_hz);
    float center = 0.5f * (lmin + lmax);
    float width = lmax - lmin;
    const float zoom = coords[ModulationTarget::ViewportZoom];
    if (zoom != 0.0f)
        width = std::clamp(width * std::exp2(zoom * kModulationViewportZoomOctaves),
                           kMinWidth, kLogMax - kLogMin);
    center += coords[ModulationTarget::ViewportPosition]
        * kModulationViewportPositionDecades;
    const float half = 0.5f * width;
    center = std::clamp(center, kLogMin + half, kLogMax - half);
    Viewport out;
    out.min_hz = std::pow(10.0f, center - half);
    out.max_hz = std::pow(10.0f, center + half);
    return out.valid() ? out : base;
}

/// Whether @p routes drive anything at all: a destination enabled with a
/// non-zero amount.
inline bool lfo_routes_audible(const LfoRoutes& routes) noexcept {
    for (std::size_t t = 0; t < kModulationTargetCount; ++t)
        if (modulation_target_moves_field(t) && routes[t].enabled
            && routes[t].amount > 0.0f)
            return true;
    return false;
}

// ── Freeze and Length ────────────────────────────────────────────────────

/// The Freeze gate: whether an LFO at @p phase holds the freeze ON, with
/// @p duty (the target's Depth) the fraction of each cycle that is frozen --
/// 0 never, 0.5 half the cycle, 1 always. "Frozen while the LFO is above a
/// threshold", with the threshold placed so the duty is exact for the shape:
/// sine `cos(pi duty)`, triangle and saw (uniform over a cycle) `1 - 2 duty`.
/// A square only takes two values, so its gate is the window of `duty` of a
/// cycle centred on the middle of its high half (exactly the high half at
/// 50 %).
inline bool lfo_freeze_gate(LfoShape shape, double phase, float duty) noexcept {
    const float d = std::clamp(duty, 0.0f, 1.0f);
    if (d <= 0.0f) return false;
    if (d >= 1.0f) return true;
    phase -= std::floor(phase);
    switch (shape) {
        case LfoShape::Square: {
            double from_centre = std::fabs(phase - 0.25);
            if (from_centre > 0.5) from_centre = 1.0 - from_centre;
            return from_centre < 0.5 * d;
        }
        case LfoShape::Triangle:
        case LfoShape::Saw:
            return lfo_value(shape, phase) > 1.0f - 2.0f * d;
        case LfoShape::Sine:
        default:
            return lfo_value(shape, phase)
                > static_cast<float>(std::cos(3.14159265358979323846 * d));
    }
}

/// How far either way, in steps of the LENGTH list, a full-depth Length
/// route moves the next freeze's loop length.
inline constexpr int kLengthModulationSteps = 8;

/// The LENGTH-list index a freeze engaging now takes: @p base_index (the
/// user's LENGTH) moved by `round(coordinate x 8)` steps, clamped to the
/// list. @p coordinate is the Length destination's summed `wave x Depth`.
inline constexpr int modulated_length_index(int base_index, float coordinate,
                                            int list_size) noexcept {
    const float steps = coordinate * static_cast<float>(kLengthModulationSteps);
    const int offset = static_cast<int>(steps < 0.0f ? steps - 0.5f : steps + 0.5f);
    const int index = base_index + offset;
    return index < 0 ? 0 : (index >= list_size ? list_size - 1 : index);
}

// ── Bands and Preset ─────────────────────────────────────────────────────

/// The band-count options, ascending (Layout's values).
inline constexpr std::array<int, 5> kBandCountOptions{32, 40, 48, 56, 64};
/// How far either way, in steps of the band-count list, a full-depth Bands
/// route moves the visible band count: the whole list from the middle.
inline constexpr int kBandsModulationSteps = 4;

/// Index into kBandCountOptions of @p count, or -1.
inline constexpr int band_count_option_index(int count) noexcept {
    for (std::size_t i = 0; i < kBandCountOptions.size(); ++i)
        if (kBandCountOptions[i] == count) return static_cast<int>(i);
    return -1;
}

/// The band count the Bands destination plays: @p base_count (the user's
/// BANDS) moved by `round(coordinate x 4)` steps of the list, clamped to it.
/// The same rounding as the Length destination.
inline constexpr int modulated_band_count(int base_count, float coordinate) noexcept {
    const int base = band_count_option_index(base_count);
    if (base < 0) return base_count;
    const float steps = coordinate * static_cast<float>(kBandsModulationSteps);
    const int offset = static_cast<int>(steps < 0.0f ? steps - 0.5f : steps + 0.5f);
    const int last = static_cast<int>(kBandCountOptions.size()) - 1;
    const int index = base + offset;
    return kBandCountOptions[static_cast<std::size_t>(
        index < 0 ? 0 : (index > last ? last : index))];
}

/// A Bands count change fades the shape to flat over this long, switches the
/// count, and fades it back over this long again.
inline constexpr double kBandsFadeSeconds = 0.08;

/// Escape hatch: true leaves the Bands destination's lanes in place but plays
/// the user's band count, should the structural change ever prove unsafe on a
/// host. Off: measured click-free and inside the cost gate.
inline constexpr bool kBandsTargetDisabled = false;

/// How far either way, in presets, a full-depth Preset route reaches.
inline constexpr int kPresetModulationSteps = 4;
/// Neighbouring presets the audio owner holds: the current one +- the reach.
inline constexpr std::size_t kPresetNeighbourCount =
    2 * static_cast<std::size_t>(kPresetModulationSteps) + 1;

/// Where in the preset list the Preset destination sits, as a continuous
/// offset from the current preset (0 = the current field, as drawn), clamped
/// to the @p below / @p above neighbours that exist.
inline float preset_modulation_offset(float coordinate, int below, int above) noexcept {
    if (!std::isfinite(coordinate)) return 0.0f;
    const float offset = coordinate * static_cast<float>(kPresetModulationSteps);
    return std::clamp(offset, -static_cast<float>(below), static_cast<float>(above));
}

/// The preset the Preset destination is nearest, as a whole step from the
/// current one: what the preset dropdown names while it moves.
inline int preset_modulation_step(float offset) noexcept {
    return static_cast<int>(std::lround(offset));
}

/// Whether @p settings move anything: an LFO that is on, has depth, and has a
/// live route. An LFO with every destination off is as silent as one at
/// depth 0, and the editor's overlay must release for it the same way.
inline bool modulation_audible(const ModulationSettings& settings) noexcept {
    return (settings.enabled && settings.depth > 0.0f
            && lfo_routes_audible(settings.routes[0]))
        || (settings.lfo2_enabled && settings.lfo2_depth > 0.0f
            && lfo_routes_audible(settings.routes[1]));
}

/// Both LFOs routed through @p settings.routes, evaluated at @p wave1 /
/// @p wave2. `settings.depth` / `lfo2_depth` carry each LFO's on/off LEVEL
/// here (the audio owner substitutes its slewed 0..1 level), which gates the
/// per-route depths. This is the ONE composition the audio owner
/// renders and the editor draws.
struct ComposedModulation {
    BandField field{};
    ModulationCoordinates coords{};
};

inline ModulationCoordinates modulation_coordinates(const ModulationSettings& settings,
                                                    float wave1, float wave2) noexcept {
    ModulationCoordinates coords;
    if (settings.enabled)
        accumulate_modulation(coords, settings.routes[0], settings.depth, wave1);
    if (settings.lfo2_enabled)
        accumulate_modulation(coords, settings.routes[1], settings.lfo2_depth, wave2);
    return coords;
}

/// Whether an LFO in @p settings drives @p target right now: the LFO on (its
/// level above zero, on the audio owner the slewed level) and the route on
/// with a non-zero Depth. What decides whether a header control is drawn at
/// its modulated value.
inline bool modulation_drives(const ModulationSettings& settings,
                              ModulationTarget target) noexcept {
    const auto t = static_cast<std::size_t>(target);
    const auto& r1 = settings.routes[0][t];
    const auto& r2 = settings.routes[1][t];
    return (settings.enabled && settings.depth > 0.0f && r1.enabled && r1.amount > 0.0f)
        || (settings.lfo2_enabled && settings.lfo2_depth > 0.0f
            && r2.enabled && r2.amount > 0.0f);
}

inline ComposedModulation compose_internal_modulation(const BandField& canonical,
                                                      const SnapshotBank& snapshots,
                                                      float host_morph,
                                                      const ModulationSettings& settings,
                                                      float wave1,
                                                      float wave2) noexcept {
    ComposedModulation out;
    out.coords = modulation_coordinates(settings, wave1, wave2);
    out.field = apply_field_modulation(canonical, snapshots, host_morph, out.coords);
    return out;
}

/// The destinations @p settings actually modulates in the LEGACY single-target
/// model: the explicit mask when one has been chosen, otherwise the single bit
/// for the enum target. Used to migrate a pre-routing session and by the
/// single-LFO compatibility path below.
inline constexpr std::uint8_t resolve_modulation_target_mask(
    const ModulationSettings& settings) noexcept {
    if (settings.target_mask == kModulationTargetMaskUnset)
        return modulation_target_bit(settings.target);
    return static_cast<std::uint8_t>(settings.target_mask & kModulationTargetMaskAll);
}

/// Single-LFO compatibility path, in 1.0.6's terms: LFO 1's enabled/depth with
/// the LEGACY destination selection (`target` / `target_mask`), every route at
/// full depth scaled by that one LFO depth, through the same combination as
/// `compose_internal_modulation`.
/// Never mutates canonical state or snapshots.
inline BandField apply_internal_modulation(const BandField& canonical,
                                           const SnapshotBank& snapshots,
                                           float host_morph,
                                           const ModulationSettings& settings,
                                           float bipolar_lfo) noexcept {
    if (!settings.enabled || settings.depth <= 0.0f) return canonical;
    LfoRoutes routes{};
    for (auto& route : routes) route.amount = 1.0f;
    set_route_mask(routes, resolve_modulation_target_mask(settings));
    ModulationCoordinates coords;
    accumulate_modulation(coords, routes, settings.depth, bipolar_lfo);
    return apply_field_modulation(canonical, snapshots, host_morph, coords);
}

} // namespace spectr
