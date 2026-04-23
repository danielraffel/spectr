#include "spectr/snapshot.hpp"

#include <algorithm>

namespace spectr {

void morph_fields(BandField& out,
                  const BandField& a,
                  const BandField& b,
                  float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    const bool b_dominant = (t >= 0.5f);

    for (std::size_t i = 0; i < kMaxBands; ++i) {
        const auto& ai = a.bands[i];
        const auto& bi = b.bands[i];
        out.bands[i].gain_db = ai.gain_db + (bi.gain_db - ai.gain_db) * t;
        out.bands[i].muted   = b_dominant ? bi.muted : ai.muted;
    }
}

} // namespace spectr
