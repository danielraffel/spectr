#pragma once

// Spectral engine interface.
//
// Spectr supports multiple internal DSP engines (IIR filter-bank, FFT mask,
// Hybrid). They share one state model (BandField + Viewport) and one
// product-level contract; internals may differ. Milestone 2 of the V1 build
// plan implements the first real engines behind this interface.

#include <pulp/audio/buffer.hpp>
#include <cstddef>
#include <memory>

#include "spectr/band_state.hpp"
#include "spectr/viewport.hpp"

namespace spectr {

enum class EngineKind : int {
    Iir    = 0,
    Fft    = 1,
    Hybrid = 2,
};

enum class ResponseMode : int {
    Live      = 0,   ///< lower-latency, may relax precision
    Precision = 1,   ///< reference truth mode
};

/// Preparation parameters passed to SpectralEngine::prepare().
struct EnginePrepare {
    double   sample_rate  = 48000.0;
    int      max_block    = 512;
    Layout   layout       = Layout::Bands32;
    Viewport viewport{};
};

/// Abstract engine contract.
///
/// All engines read from the BandField + Viewport + mode set on each
/// process() call; they do not own state.
class SpectralEngine {
public:
    virtual ~SpectralEngine() = default;

    /// Allocate and configure resources for the given block / SR / layout.
    /// Must be called before process() and on any configuration change.
    virtual void prepare(const EnginePrepare& p) = 0;

    /// Release resources; engine becomes un-prepared.
    virtual void release() = 0;

    /// Latency reported to the host, in samples. May change per prepare().
    virtual int latency_samples() const = 0;

    /// Render a block.
    ///
    /// @param output  destination buffer view, same channels/samples as input
    /// @param input   source buffer view
    /// @param field   canonical band state
    /// @param view    viewport bounds (used to map bands to frequencies)
    /// @param layout  visible layout (N visible bands project from field)
    /// @param mode    response mode (Live vs Precision)
    virtual void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        const BandField& field,
        const Viewport& view,
        Layout layout,
        ResponseMode mode) = 0;
};

/// Factory for the three engine kinds. Returns nullptr for unknown kinds.
/// Milestone 2 will replace the IIR/FFT/Hybrid stubs with real impls.
std::unique_ptr<SpectralEngine> make_engine(EngineKind kind);

} // namespace spectr
