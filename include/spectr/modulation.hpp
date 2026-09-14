#pragma once

#include "spectr/band_state.hpp"
#include "spectr/snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spectr {

enum class LfoShape : std::uint8_t { Sine, Triangle, Square, Saw };
enum class ModulationTarget : std::uint8_t { WholeBank, SnapshotA, SnapshotB, Morph };

/// Sentinel meaning "no explicit destination selection has been made", so the
/// destination follows the single-target `ModulationSettings::target` enum
/// (the host-automatable lane). It is deliberately distinct from `0x00`,
/// which is an explicit *empty* selection — the user asked for no
/// destinations and modulation is silent.
inline constexpr std::uint8_t kModulationTargetMaskUnset = 0xFF;

/// Every destination selected.
inline constexpr std::uint8_t kModulationTargetMaskAll = 0x0F;

struct ModulationSettings {
    bool enabled = false;
    LfoShape shape = LfoShape::Sine;
    float beats_per_cycle = 4.0f;
    float depth = 0.5f;
    ModulationTarget target = ModulationTarget::WholeBank;
    bool lfo2_enabled = false;
    LfoShape lfo2_shape = LfoShape::Sine;
    float lfo2_beats_per_cycle = 4.0f;
    float lfo2_depth = 0.0f;
    // Destination mask for ALL/NONE composition; bits 0..3 map
    // Bank/A/B/Morph. `kModulationTargetMaskUnset` defers to `target`.
    std::uint8_t target_mask = kModulationTargetMaskUnset;
};

/// The single mask bit that stands for @p target.
inline constexpr std::uint8_t modulation_target_bit(ModulationTarget target) noexcept {
    return static_cast<std::uint8_t>(std::uint8_t{1} << static_cast<std::uint8_t>(target));
}

/// The destinations @p settings actually modulates: the explicit mask when one
/// has been chosen, otherwise the single bit for the enum target. The result is
/// always a concrete 4-bit selection, so callers never have to special-case the
/// sentinel.
inline constexpr std::uint8_t resolve_modulation_target_mask(
    const ModulationSettings& settings) noexcept {
    if (settings.target_mask == kModulationTargetMaskUnset)
        return modulation_target_bit(settings.target);
    return static_cast<std::uint8_t>(settings.target_mask & kModulationTargetMaskAll);
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

/// Apply one LFO sample to a single destination.
///
/// The authored mute topology of @p canonical always survives — see
/// `preserve_authored_mutes`. Only levels are modulated.
inline BandField apply_modulation_to_target(const BandField& canonical,
                                            const SnapshotBank& snapshots,
                                            float host_morph,
                                            const ModulationSettings& settings,
                                            ModulationTarget target,
                                            float bipolar_lfo) noexcept {
    BandField out = canonical;
    const float wave = std::clamp(bipolar_lfo, -1.0f, 1.0f);
    const float depth = std::clamp(settings.depth, 0.0f, 1.0f);
    if (target == ModulationTarget::WholeBank) {
        constexpr float kMaximumExcursionDb = 12.0f;
        const float delta = wave * depth * kMaximumExcursionDb;
        for (auto& band : out.bands)
            band.gain_db = std::clamp(band.gain_db + delta,
                                      kBandGainMinDb, kBandGainMaxDb);
        preserve_authored_mutes(out, canonical);
        return out;
    }

    if (target == ModulationTarget::Morph) {
        if (snapshots.has(SnapshotBank::Slot::A)
            && snapshots.has(SnapshotBank::Slot::B)) {
            const float t = std::clamp(host_morph + wave * depth * 0.5f,
                                       0.0f, 1.0f);
            morph_fields(out, snapshots.a.field, snapshots.b.field, t);
        }
        preserve_authored_mutes(out, canonical);
        return out;
    }

    const auto slot = target == ModulationTarget::SnapshotA
        ? SnapshotBank::Slot::A : SnapshotBank::Slot::B;
    if (snapshots.has(slot)) {
        // Snapshot destinations are unipolar: the LFO moves from the current
        // host-controlled field toward the selected captured shape and back.
        const float amount = (wave + 1.0f) * 0.5f * depth;
        morph_fields(out, canonical, snapshots.get(slot).field, amount);
    }
    preserve_authored_mutes(out, canonical);
    return out;
}

/// Build the audible field from canonical state plus one LFO sample.
/// This function never mutates canonical state or snapshots: internal
/// modulation therefore composes with host automation/modulation instead of
/// feeding derived values back into host parameter lanes.
inline BandField apply_internal_modulation(const BandField& canonical,
                                           const SnapshotBank& snapshots,
                                           float host_morph,
                                           const ModulationSettings& settings,
                                           float bipolar_lfo) noexcept {
    BandField out = canonical;
    if (!settings.enabled || settings.depth <= 0.0f) return out;

    const std::uint8_t mask = resolve_modulation_target_mask(settings);
    constexpr ModulationTarget targets[] = {
        ModulationTarget::WholeBank, ModulationTarget::SnapshotA,
        ModulationTarget::SnapshotB, ModulationTarget::Morph};
    for (std::size_t bit = 0; bit < 4; ++bit) {
        if ((mask & (std::uint8_t{1} << bit)) == 0) continue;
        out = apply_modulation_to_target(out, snapshots, host_morph, settings,
                                         targets[bit], bipolar_lfo);
    }
    return out;
}

} // namespace spectr
