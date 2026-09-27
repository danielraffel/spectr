#pragma once
#include <spectr/mask_renderer.hpp>
#include <pulp/gpu_audio/gpu_spectral_mask.hpp>
#include <pulp/gpu_audio/gpu_audio_program.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace spectr::experimental {
// Experimental consumer, never selected by the shipping renderer factory.
// One callback producer; one serialized non-RT service owner; one trace reader.
class SharedSpectralBridge {
public:
    struct Config {
        MaskRendererConfig renderer;
        unsigned host_block=64, lead_host_blocks=4;
        MaskRenderer::Layout immutable_layout;
    };
    struct Terminal {
        std::uint64_t stream_epoch=0, block_sequence=0;
        bool ingress_admitted=false;
        pulp::gpu_audio::GpuAudioTerminalDisposition disposition;
    };
    // All preparation/reset/release calls require stopped and joined callers.
    bool prepare(const Config&);
    bool reset();
    bool release();
    unsigned latency_samples() const noexcept;
    // Planar, fixed prepared block. false marks end-of-input draining; input
    // still feeds the CPU renderer, but no new GPU block is admitted.
    bool process(const float* const*, float* const*, bool admit=true) noexcept;
    void service() noexcept;
    bool pop_terminal(Terminal&) noexcept;
    bool publish_layout(const MaskRenderer::Layout&) noexcept { return false; }
    bool set_mix(float) noexcept { return false; }
    bool fenced() const noexcept { return fenced_.load(std::memory_order_acquire); }
    std::uint64_t epoch() const noexcept { return epoch_; }
    std::uint64_t serviced_blocks() const noexcept { return serviced_.load(std::memory_order_acquire); }
    std::uint64_t completed_hops() const noexcept { return completed_.load(std::memory_order_acquire); }
    std::uint64_t lost_trace_records() const noexcept { return trace_lost_.load(); }
    // Read only on the service owner, or after callback and worker have joined.
    pulp::gpu_audio::GpuSpectralMaskSession::Diagnostics diagnostics() const;
private:
    static constexpr unsigned slots=64, trace_slots=4096;
    enum : unsigned { empty, ready, busy };
    struct Slot {
        std::atomic<unsigned> state{empty};
        std::uint64_t sequence=0;
        std::vector<float> samples;
    };
    void collect_completed() noexcept;
    void terminal(std::uint64_t sequence, pulp::gpu_audio::GpuAudioTerminalDisposition) noexcept;
    Config config_;
    std::unique_ptr<MaskRenderer> cpu_;
    std::unique_ptr<pulp::gpu_audio::GpuSpectralMaskSession> gpu_;
    std::array<Slot, slots> inputs_, outputs_;
    std::vector<float> cpu_block_, fallback_, hop_input_, hop_output_;
    std::vector<float*> cpu_ptrs_;
    std::array<bool, slots> admitted_{};
    std::array<Terminal, trace_slots> trace_{};
    std::atomic<std::uint64_t> trace_read_{0}, trace_write_{0}, trace_lost_{0};
    std::atomic<std::uint64_t> callback_count_{0}, serviced_{0}, completed_{0};
    std::atomic<bool> fenced_{false};
    std::uint64_t epoch_=0, callback_sequence_=0, worker_sequence_=0, hop_sequence_=0, input_count_=0, next_terminal_=0;
    unsigned accumulated_=0;
    bool prepared_=false, finishing_=false, hop_pending_=false;
};
}
