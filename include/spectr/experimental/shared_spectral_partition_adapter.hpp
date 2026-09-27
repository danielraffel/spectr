#pragma once
#include <spectr/experimental/shared_spectral_bridge.hpp>

namespace spectr::experimental {
// Adapts arbitrary host partitions to one fixed bridge quantum. The two local
// quantum buffers only assemble input and serialize output; DSP/history and
// all asynchronous slot ownership remain in SharedSpectralBridge.
class SharedSpectralPartitionAdapter {
public:
    struct Config {
        MaskRendererConfig renderer;
        MaskRenderer::Layout immutable_layout;
        unsigned internal_quantum=64;
        unsigned additional_latency_samples=320;
        unsigned max_callback_frames=1024;
    };
    // Quiescent control-owner operations, never audio-thread reset/reprepare.
    bool prepare(const Config&);
    bool reset();
    bool reset_realtime() noexcept;
    bool release();
    unsigned latency_samples() const noexcept;
    // Zero length is a no-op. Other lengths must fit the prepared maximum.
    // In-place per-channel buffers are supported; distinct channels may not alias.
    bool process(const float* const*,float* const*,unsigned frames) noexcept;
    void service() noexcept { bridge_.service(); }
    bool publish_layout(const MaskRenderer::Layout& layout) { return bridge_.publish_layout(layout); }
    bool set_layout_rt(const MaskRenderer::Layout& layout) noexcept { return bridge_.set_layout_rt(layout); }
    bool set_mix(float) noexcept { return false; }
    bool pop_terminal(SharedSpectralBridge::Terminal& t) noexcept { return bridge_.pop_terminal(t); }
    std::uint64_t quantum_count() const noexcept { return quantums_.load(std::memory_order_acquire); }
    std::uint64_t serviced_quantums() const noexcept { return bridge_.serviced_blocks(); }
    std::uint64_t completed_hops() const noexcept { return bridge_.completed_hops(); }
    bool fenced() const noexcept { return bridge_.fenced(); }
    std::uint64_t lost_trace_records() const noexcept { return bridge_.lost_trace_records(); }
    auto diagnostics() const { return bridge_.diagnostics(); }
    std::uint64_t epoch() const noexcept { return bridge_.epoch(); }
    void set_service_observer(SharedSpectralBridge::ServiceObserver fn,void* context) noexcept {
        bridge_.set_service_observer(fn,context);
    }
private:
    Config config_;
    SharedSpectralBridge bridge_;
    std::vector<float> input_,output_;
    std::vector<const float*> input_ptrs_;
    std::vector<float*> output_ptrs_;
    std::atomic<std::uint64_t> quantums_{0};
    unsigned fill_=0;
    bool prepared_=false;
};
}
