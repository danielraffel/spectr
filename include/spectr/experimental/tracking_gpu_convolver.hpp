#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace spectr::experimental {

/// Product-private fixed-IR GPU runner for Tracking's 64-frame path.
///
/// The CPU renderer remains the product's authority.  A prepared generation is
/// only an optional wet source, and a failed or fenced generation never removes
/// the CPU result.  `process()` is the audio-thread entry point.  It performs
/// no allocation, locking, waiting, GPU calls, or generation destruction.
class TrackingGpuConvolver final {
  public:
    static constexpr std::uint32_t kQuantumFrames = 64;
    static constexpr std::uint32_t kGpuLatencyQuanta = 2;
    // The product's minimum lead.  Each immutable IR generation adds its own
    // continuous history requirement (ceil(IR/Q) + this lead).
    static constexpr std::uint32_t kGpuLeadQuanta = kGpuLatencyQuanta;

    struct Config {
        std::uint32_t channels = 2;
        std::uint32_t sample_rate = 48'000;
        std::uint32_t max_ir_taps = 8'192;
    };

    enum class ProviderState : std::uint8_t {
        Unprepared = 0,
        CpuOnly = 1,
        SharedReady = 2,
        Degraded = 3,
        Unavailable = 4,
    };

    enum class Health : std::uint8_t {
        None = 0,
        ProviderRefused = 1,
        ProviderLost = 2,
        ProviderFailure = 3,
    };

    struct Snapshot {
        ProviderState state = ProviderState::Unprepared;
        Health health = Health::None;
        std::uint8_t provider = 0; // pulp::gpu_audio::GpuAudioProvider
        std::uint64_t requested_generation = 0;
        std::uint64_t ready_generation = 0;
        std::uint64_t active_generation = 0;
        std::uint64_t failed_generation = 0;
        std::uint64_t epoch = 0;
        std::uint64_t gpu_selected = 0;
        std::uint64_t cpu_fallback = 0;
        std::uint64_t gpu_misses = 0;
        std::uint64_t reset_count = 0;
    };

    explicit TrackingGpuConvolver(Config config);
    ~TrackingGpuConvolver();

    TrackingGpuConvolver(const TrackingGpuConvolver&) = delete;
    TrackingGpuConvolver& operator=(const TrackingGpuConvolver&) = delete;

    /// Control thread, with audio stopped.  The initial IR is generation zero.
    /// Returns false when the exact shared provider, lead or host shape is
    /// unavailable. The caller then keeps its ordinary CPU renderer.
    bool prepare(std::span<const float> initial_ir);

    /// Single design/control producer, never the callback.  The bounded queue
    /// keeps the newest request and coalesces an older pending request.  The
    /// producer owns the span for the duration of this call only.
    bool enqueue_ir(std::uint64_t generation, std::span<const float> ir);

    /// Audio thread.  `input` is the wet-source planar input and `cpu_current` is the
    /// already-computed Tracking wet result for this exact quantum.  The output
    /// is delayed by exactly two 64-frame quanta so it aligns with Pulp's
    /// GpuAudioTransport.  `stable` is false while the product has an in-flight
    /// CPU design/fade.  No callback-side operation can stop or destroy a node.
    bool process(const float* const* input, const float* const* cpu_current, float* const* output,
                 std::uint64_t product_generation, bool stable) noexcept;

    /// Audio thread.  Clears the two-quantum CPU delay and invalidates GPU
    /// adoption history.  Provider teardown and generation destruction stay on
    /// the worker/control lane.
    void reset() noexcept;

    /// Control thread, after the callback has stopped.  Joins the worker and
    /// releases every transport before destroying its borrowed convolver.
    bool release() noexcept;

    Snapshot snapshot() const noexcept;

  private:
    struct Impl;
    Config config_;
    std::unique_ptr<Impl> impl_;
};

} // namespace spectr::experimental
