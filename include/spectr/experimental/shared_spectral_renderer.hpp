#pragma once
#include <spectr/experimental/shared_spectral_partition_adapter.hpp>
#include <atomic>
#include <thread>

namespace spectr::experimental {
// Build-time experiment only. The ordinary MaskRenderer factory remains CPU-only.
// Construction/prepare/destruction require the product's stopped ownership lane.
class SharedSpectralMaskRenderer final : public MaskRenderer {
public:
    enum class ProviderState : unsigned { Unprepared, SharedReady, CpuOnly, Fenced, ReleaseUnconfirmed };
    struct Snapshot {
        ProviderState state=ProviderState::Unprepared;
        std::uint64_t epoch=0;
        unsigned callback_fence_reason=0,worker_fence_reason=0;
        std::uint64_t gpu_delivered=0,cpu_fallback=0,cancelled=0,lost_records=0;
    };
    // force_cpu_only is an explicit negative/control fixture, not host state.
    explicit SharedSpectralMaskRenderer(bool force_cpu_only=false):force_cpu_only_(force_cpu_only){}
    ~SharedSpectralMaskRenderer() override;
    bool prepare(const MaskRendererConfig&) override;
    bool release() noexcept;
    bool prepared() const noexcept override { return prepared_; }
    int latency_samples() const noexcept override;
    int maximum_tail_samples() const noexcept override;
    int design_grid_size() const noexcept override { return config_.design_grid_size; }
    bool publish_layout(const Layout&) override;
    bool set_layout_rt(const Layout&) noexcept override;
    void set_mix(float) noexcept override;
    bool process(const float* const*,float* const*,int) noexcept override;
    void reset() noexcept override;
    unsigned long long active_generation() const noexcept override;
    Snapshot snapshot() const noexcept;
    // Optional observer of the same non-RT emitted records. Install only before
    // prepare; its context must outlive the renderer and its final stop.
    struct TraceObserver {
        void* context=nullptr;
        void (*delivery)(void*,std::uint64_t,const SharedSpectralBridge::Terminal&) noexcept=nullptr;
        void (*final)(void*,const SharedSpectralTraceFinal&) noexcept=nullptr;
    };
    void set_trace_observer(TraceObserver observer) noexcept { trace_observer_=observer; }
    static unsigned additional_latency(const MaskRendererConfig& c) noexcept {
        return c.analysis_hop>0?unsigned(c.analysis_hop/2)*5:0;
    }
private:
    bool stop() noexcept;
    void service() noexcept;
    void drain_terminals() noexcept;
    SharedSpectralTraceRun trace_run_;
    TraceObserver trace_observer_;
    MaskRendererConfig config_{};
    std::unique_ptr<SharedSpectralPartitionAdapter> adapter_=std::make_unique<SharedSpectralPartitionAdapter>();
    std::jthread worker_;
    std::atomic<ProviderState> state_{ProviderState::Unprepared};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<unsigned> callback_reason_{0},worker_reason_{0};
    std::atomic<std::uint64_t> gpu_delivered_{0},cpu_fallback_{0},cancelled_{0},lost_records_{0};
    bool prepared_=false,force_cpu_only_=false;
};
}
