#include "spectr/edit_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace spectr {

namespace {

constexpr float kDbMin = -60.0f;
constexpr float kDbMax = +12.0f;
constexpr float kDbRange = kDbMax - kDbMin;

float clamp_db(float v) noexcept {
    return std::clamp(v, kDbMin, kDbMax);
}

/// Inclusive [lo, hi] band range swept by the drag, clamped to the visible
/// layout.
std::pair<std::size_t, std::size_t> swept_range(const DragGesture& d) noexcept {
    const auto n = d.n_visible == 0 ? std::size_t{1} : d.n_visible;
    const auto last = n - 1;
    auto lo = std::min(d.start_band, d.current_band);
    auto hi = std::max(d.start_band, d.current_band);
    if (lo > last) lo = last;
    if (hi > last) hi = last;
    return {lo, hi};
}

/// Normalised drag distance: how far the drag has moved in dB from its
/// start, expressed in fractions of the visible dB range. Positive = up,
/// negative = down.
float drag_dy_norm(const DragGesture& d) noexcept {
    return (d.current_value - d.start_value) / kDbRange;
}

} // namespace

void apply_sculpt(BandField& field, const DragGesture& drag) noexcept {
    const auto [lo, hi] = swept_range(drag);
    for (auto i = lo; i <= hi; ++i) {
        field.bands[i].gain_db = clamp_db(drag.current_value);
        field.bands[i].muted   = false;
    }
}

void apply_level(BandField& field, const DragGesture& drag) noexcept {
    const auto [lo, hi] = swept_range(drag);
    const float target = clamp_db(drag.current_value);
    for (auto i = lo; i <= hi; ++i) {
        field.bands[i].gain_db = target;
        field.bands[i].muted   = false;
    }
}

void apply_boost(BandField& field,
                 const DragGesture& drag,
                 const BandSnapshot& snap) noexcept {
    // Multiply snapshot gains by (1 + dy_norm * scale). The scale of 4.0
    // means a full-range drag (dy_norm = ±1) scales gains 5× or -3×,
    // enough to feel like a real "boost" gesture without runaway behavior.
    const float dy = drag_dy_norm(drag);
    const float mult = 1.0f + dy * 4.0f;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        field.bands[i].gain_db = clamp_db(snap.gain_db[i] * mult);
        field.bands[i].muted   = snap.muted[i];
    }
}

void apply_flare(BandField& field,
                 const DragGesture& drag,
                 const BandSnapshot& snap) noexcept {
    // Exaggerate distance-from-zero in the snapshot, scaled by drag
    // distance. Drag up → expansion; drag down → compression.
    const float dy = drag_dy_norm(drag);
    const float expand = 1.0f + dy * 3.0f;  // [-2 .. +4] over a full-range drag
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        const float s = snap.gain_db[i];
        // Preserve sign; extend magnitude proportionally.
        float v = s * expand;
        field.bands[i].gain_db = clamp_db(v);
        field.bands[i].muted   = snap.muted[i];
    }
}

void apply_glide(BandField& field,
                 const DragGesture& drag,
                 const BandSnapshot& snap) noexcept {
    // Interpolate each band from its snapshot toward current_value by t,
    // where t = |dy_norm| clamped to [0, 1]. Matches the prototype's
    // "Glide" mode: drag distance from start drives how far each snapshot
    // band slides toward the drag's current value.
    const float t = std::clamp(std::fabs(drag_dy_norm(drag)), 0.0f, 1.0f);
    const float target = clamp_db(drag.current_value);
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        const float v = snap.gain_db[i] + (target - snap.gain_db[i]) * t;
        field.bands[i].gain_db = clamp_db(v);
        field.bands[i].muted   = snap.muted[i];
    }
}

void dispatch_edit(EditMode            mode,
                   BandField&          field,
                   const DragGesture&  drag,
                   const BandSnapshot& snap) noexcept {
    switch (mode) {
        case EditMode::Sculpt: apply_sculpt(field, drag);       return;
        case EditMode::Level:  apply_level(field, drag);        return;
        case EditMode::Boost:  apply_boost(field, drag, snap);  return;
        case EditMode::Flare:  apply_flare(field, drag, snap);  return;
        case EditMode::Glide:  apply_glide(field, drag, snap);  return;
    }
}

} // namespace spectr
