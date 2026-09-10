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

/// Apply one LFO sample to a single destination.
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
        return out;
    }

    if (target == ModulationTarget::Morph) {
        if (snapshots.has(SnapshotBank::Slot::A)
            && snapshots.has(SnapshotBank::Slot::B)) {
            const float t = std::clamp(host_morph + wave * depth * 0.5f,
                                       0.0f, 1.0f);
            morph_fields(out, snapshots.a.field, snapshots.b.field, t);
        }
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
