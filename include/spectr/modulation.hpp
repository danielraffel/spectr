#pragma once

#include "spectr/band_state.hpp"
#include "spectr/snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spectr {

enum class LfoShape : std::uint8_t { Sine, Triangle, Square, Saw };
enum class ModulationTarget : std::uint8_t { WholeBank, SnapshotA, SnapshotB, Morph };

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
    // Optional destination mask for ALL/NONE composition. Zero preserves the
    // legacy single-target enum semantics; bits 0..3 map Bank/A/B/Morph.
    std::uint8_t target_mask = 0;
};

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

/// Build the audible field from canonical state plus one LFO sample.
/// This function never mutates canonical state or snapshots: internal
/// modulation therefore composes with host automation/modulation instead of
/// feeding derived values back into host parameter lanes.
inline BandField apply_internal_modulation(const BandField& canonical,
                                           const SnapshotBank& snapshots,
                                           float host_morph,
                                           const ModulationSettings& settings,
                                           float bipolar_lfo) noexcept {
    if (settings.target_mask != 0) {
        auto one = settings;
        one.target_mask = 0;
        BandField result = canonical;
        constexpr ModulationTarget targets[] = {
            ModulationTarget::WholeBank, ModulationTarget::SnapshotA,
            ModulationTarget::SnapshotB, ModulationTarget::Morph};
        for (std::size_t bit = 0; bit < 4; ++bit) {
            if ((settings.target_mask & (std::uint8_t{1} << bit)) == 0) continue;
            one.target = targets[bit];
            result = apply_internal_modulation(result, snapshots, host_morph,
                                               one, bipolar_lfo);
        }
        return result;
    }
    BandField out = canonical;
    if (!settings.enabled || settings.depth <= 0.0f) return out;

    const float wave = std::clamp(bipolar_lfo, -1.0f, 1.0f);
    const float depth = std::clamp(settings.depth, 0.0f, 1.0f);
    if (settings.target == ModulationTarget::WholeBank) {
        constexpr float kMaximumExcursionDb = 12.0f;
        const float delta = wave * depth * kMaximumExcursionDb;
        for (auto& band : out.bands)
            band.gain_db = std::clamp(band.gain_db + delta,
                                      kBandGainMinDb, kBandGainMaxDb);
        return out;
    }

    if (settings.target == ModulationTarget::Morph) {
        if (snapshots.has(SnapshotBank::Slot::A)
            && snapshots.has(SnapshotBank::Slot::B)) {
            const float t = std::clamp(host_morph + wave * depth * 0.5f,
                                       0.0f, 1.0f);
            morph_fields(out, snapshots.a.field, snapshots.b.field, t);
        }
        return out;
    }

    const auto slot = settings.target == ModulationTarget::SnapshotA
        ? SnapshotBank::Slot::A : SnapshotBank::Slot::B;
    if (snapshots.has(slot)) {
        // Snapshot destinations are unipolar: the LFO moves from the current
        // host-controlled field toward the selected captured shape and back.
        const float amount = (wave + 1.0f) * 0.5f * depth;
        morph_fields(out, canonical, snapshots.get(slot).field, amount);
    }
    return out;
}

} // namespace spectr
