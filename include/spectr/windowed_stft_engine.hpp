#pragma once

// Milestone 11 — Windowed STFT + overlap-add engine.
//
// Replacement for BlockFftEngine that does real windowed STFT instead
// of a one-shot FFT per host block. The product-truth motivation is
// the spec's -80 dB mute-depth target on non-aligned tones: a naive
// one-block FFT only hits that target when the tone frequency aligns
// exactly with an FFT bin. Real-world content never does.
//
// Design:
//   - Fixed internal frame size (1024) regardless of host block. This
//     decouples frequency resolution from DAW buffer size.
//   - 75% overlap (hop = frame/4). A Hann² window pair at 75% overlap
//     satisfies COLA, so a flat-gain pass reconstructs the input
//     sample-for-sample after the initial fill-in period.
//   - Per-channel ring buffers for input (analysis) and output
//     (overlap-add accumulator). Both sized fft_size; positions wrap
//     modulo fft_size.
//   - Latency = fft_size samples — the time it takes the analysis
//     window to fill before the first meaningful frame is emitted.
//
// This engine is exposed via the `make_windowed_stft_engine()`
// factory. A follow-up will switch `EngineKind::Fft` to use it by
// default; for now it lives alongside BlockFftEngine so tests can
// verify both in isolation.

#include "spectr/engine.hpp"

#include <memory>

namespace spectr {

/// Construct the windowed STFT engine. Same SpectralEngine contract as
/// BlockFftEngine — drop-in replacement once the swap lands.
std::unique_ptr<SpectralEngine> make_windowed_stft_engine();

} // namespace spectr
