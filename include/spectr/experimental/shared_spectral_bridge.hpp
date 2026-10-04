#pragma once
#include <spectr/mask_renderer.hpp>
#include <spectr/experimental/shared_spectral_trace.hpp>
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
        bool allow_cpu_only=false;
        bool force_cpu_only=false;
    };
    enum class FenceReason : unsigned {
        None, CpuProcess, InputJournal, ControlJournal, MissingControl,
        ControlSequence, ProviderPrepare, ProviderRelease, ProviderResult,
        ProviderSubmit, InputSequence, ForcedCpu
    };
    struct Terminal {
        std::uint64_t stream_epoch=0, block_sequence=0;
        bool ingress_admitted=false;
        FenceReason callback_reason=FenceReason::None,worker_reason=FenceReason::None;
        pulp::gpu_audio::GpuAudioTerminalDisposition disposition;
    };
    // Preparation, quiescent reset and release require stopped and joined callers.
    bool prepare(const Config&);
    bool reset();
    // Single callback owner, between process calls. No GPU access or waiting.
    bool reset_realtime() noexcept;
    bool release();
    unsigned latency_samples() const noexcept;
    unsigned maximum_tail_samples() const noexcept { return prepared_?unsigned(cpu_->maximum_tail_samples())+config_.host_block*config_.lead_host_blocks:0; }
    unsigned long long active_generation() const noexcept { return prepared_?cpu_->active_generation():0; }
    // Service owner or quiescent only.
    bool provider_prepared() const noexcept { return gpu_ && gpu_->prepared(); }
    // Planar, fixed prepared block. false marks end-of-input draining; input
    // still feeds the CPU renderer, but no new GPU block is admitted.
    bool process(const float* const*, float* const*, bool admit=true) noexcept;
    void service() noexcept;
    // Callback owner. While true the callback waits, bounded, for the GPU
    // output of the quantum it is about to deliver instead of substituting
    // the CPU stand-in: an offline bounce is not paced, so without the wait
    // it outruns the worker and falls back for most of the render. Never set
    // for a realtime block.
    void set_offline(bool offline) noexcept { offline_=offline; }
    static constexpr std::uint64_t offline_wait_budget_ns=250'000'000;
    bool pop_terminal(Terminal&) noexcept;
    // Existing CPU renderer remains the only layout/transition authority.
    bool publish_layout(const MaskRenderer::Layout& layout) { return prepared_ && cpu_->publish_layout(layout); }
    bool set_layout_rt(const MaskRenderer::Layout& layout) noexcept { return prepared_ && cpu_->set_layout_rt(layout); }
    bool set_mix(float) noexcept { return false; }
    bool fenced() const noexcept {
        const auto e=requested_epoch_.load(std::memory_order_acquire);
        return callback_failed_epoch_.load(std::memory_order_acquire)==e ||
               worker_failed_epoch_.load(std::memory_order_acquire)==e;
    }
    FenceReason callback_fence_reason() const noexcept {
        return callback_failed_epoch_.load(std::memory_order_acquire)==epoch()?callback_reason_.load():FenceReason::None;
    }
    FenceReason worker_fence_reason() const noexcept {
        return worker_failed_epoch_.load(std::memory_order_acquire)==epoch()?worker_reason_.load():FenceReason::None;
    }
    std::uint64_t epoch() const noexcept { return requested_epoch_.load(std::memory_order_acquire); }
    std::uint64_t serviced_blocks() const noexcept {
        return progress_epoch_.load(std::memory_order_acquire)==epoch()?serviced_.load(std::memory_order_acquire):0;
    }
    std::uint64_t completed_hops() const noexcept {
        return progress_epoch_.load(std::memory_order_acquire)==epoch()?completed_.load(std::memory_order_acquire):0;
    }
    std::uint64_t lost_trace_records() const noexcept { return trace_lost_.load(); }
    // Callback, worker and trace reader must all be stopped. No live-reader contract.
    SharedSpectralStoppedAccounting stopped_accounting() const noexcept;
    // Read only on the service owner, or after callback and worker have joined.
    pulp::gpu_audio::GpuSpectralMaskSession::Diagnostics diagnostics() const;
    enum class ServicePoint { BeforeInputClaim, InputClaimed, BeforeSubmit, BeforeOutputPublish, BeforeRelease, BeforePrepare };
    using ServiceObserver=void(*)(void*,ServicePoint) noexcept;
    // Optional non-RT instrumentation; install only while callers are stopped.
    void set_service_observer(ServiceObserver fn,void* context) noexcept { observer_=fn;observer_context_=context; }
private:
    friend struct SharedSpectralBridgeTestAccess;
    static constexpr unsigned slots=64, trace_slots=4096;
    enum : unsigned { empty, ready, busy };
    struct Slot {
        std::atomic<unsigned> state{empty};
        std::uint64_t epoch=0, sequence=0;
        std::vector<float> samples;
    };
    void fail_callback(FenceReason why) noexcept { callback_reason_=why;callback_failed_epoch_.store(epoch_,std::memory_order_release); }
    void fail_worker(FenceReason why,std::uint64_t epoch) noexcept {worker_reason_=why;worker_failed_epoch_.store(epoch,std::memory_order_release);}
    void collect_completed() noexcept;
    bool gpu_ready_for_wait_() const noexcept {
        return !config_.force_cpu_only && !fenced();
    }
    void capture_frame(const MaskRenderer::Table&,std::uint64_t ordinal) noexcept;
    bool load_hop_gains() noexcept;
    bool claim_empty_or_obsolete(Slot&,std::uint64_t epoch) noexcept;
    void reset_callback_state() noexcept;
    bool prepare_worker(std::uint64_t epoch) noexcept;
    void observe(ServicePoint point) noexcept { if(observer_)observer_(observer_context_,point); }
    void terminal(std::uint64_t sequence, pulp::gpu_audio::GpuAudioTerminalDisposition) noexcept;
    Config config_;
    std::unique_ptr<MaskRenderer> cpu_;
    std::unique_ptr<pulp::gpu_audio::GpuSpectralMaskSession> gpu_;
    std::array<Slot, slots> inputs_, outputs_, controls_;
    std::vector<float> cpu_block_, fallback_, hop_input_, hop_output_, hop_gains_;
    std::vector<float*> cpu_ptrs_;
    std::array<bool, slots> admitted_{};
    std::array<Terminal, trace_slots> trace_{};
    std::atomic<std::uint64_t> trace_read_{0}, trace_write_{0}, trace_lost_{0};
    std::atomic<std::uint64_t> callback_count_{0}, serviced_{0}, completed_{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> requested_epoch_{0},ready_epoch_{0},progress_epoch_{0};
    // One writer per fence: a late old-worker report cannot clear a new callback fence.
    std::atomic<std::uint64_t> callback_failed_epoch_{0},worker_failed_epoch_{0};
    std::atomic<FenceReason> callback_reason_{FenceReason::None},worker_reason_{FenceReason::None};
    std::uint64_t lifetime_input_quanta_=0,lifetime_ingress_admitted_quanta_=0;
    bool accounting_overflow_=false;
    void count_quanta(std::uint64_t& counter) noexcept;
    std::uint64_t epoch_limit_=0,worker_epoch_=0,physical_epoch_=0;
    ServiceObserver observer_=nullptr;
    void* observer_context_=nullptr;
    std::uint64_t epoch_=0, callback_sequence_=0, worker_sequence_=0, hop_sequence_=0, input_count_=0, next_terminal_=0;
    // Silent hops the GPU session analyses before the stream's first real hop,
    // matching the CPU reference's stream-start priming
    // (MaskRendererConfig::prime_stream_start): its first frame then ends one
    // hop into the stream, as the CPU's does, and both give the stream's first
    // samples the full window overlap. Their results are discarded.
    std::uint64_t prime_hops_=0, primed_hops_=0;
    unsigned accumulated_=0;
    bool prepared_=false, finishing_=false, hop_pending_=false, hop_gains_loaded_=false, offline_=false;
};
}
