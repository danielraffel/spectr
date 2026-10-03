#pragma once
#include <spectr/experimental/shared_spectral_bridge.hpp>
#include <pulp/signal/dry_wet_mixer.hpp>

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
        bool allow_cpu_only=false;
        bool force_cpu_only=false;
    };
    // Quiescent control-owner operations, never audio-thread reset/reprepare.
    bool prepare(const Config&);
    bool reset();
    bool reset_realtime() noexcept;
    bool release();
    unsigned latency_samples() const noexcept;
    unsigned maximum_tail_samples() const noexcept { return prepared_?bridge_.maximum_tail_samples()+config_.internal_quantum:0; }
    unsigned long long active_generation() const noexcept { return bridge_.active_generation(); }
    bool provider_prepared() const noexcept { return bridge_.provider_prepared(); }
    // Zero length is a no-op. Other lengths must fit the prepared maximum.
    // In-place per-channel buffers are supported; distinct channels may not alias.
    bool process(const float* const*,float* const*,unsigned frames) noexcept;
    void service() noexcept { bridge_.service(); }
    bool publish_layout(const MaskRenderer::Layout& layout) { return bridge_.publish_layout(layout); }
    bool set_layout_rt(const MaskRenderer::Layout& layout) noexcept { return bridge_.set_layout_rt(layout); }
    // Callback-owner request at the current input-sample position. Its envelope
    // is delayed by additional_latency_samples to match the CPU reference.
    bool set_mix(float mix) noexcept;
    // Time-domain source for the WET path only, as MaskRenderer::set_wet_source:
    // each callback the source rewrites the live input, the bridge (its CPU
    // reference and the GPU journal alike) realises the mask over what the
    // source wrote, and the dry leg of the mix stays the live input. Install
    // while the callback cannot be inside process(); survives prepare/reset.
    void set_wet_source(MaskRenderer::WetSource* source) noexcept { wet_source_=source; }
    MaskRenderer::WetSource* wet_source() const noexcept { return wet_source_; }
    bool pop_terminal(SharedSpectralBridge::Terminal& t) noexcept { return bridge_.pop_terminal(t); }
    std::uint64_t quantum_count() const noexcept { return quantums_.load(std::memory_order_acquire); }
    std::uint64_t serviced_quantums() const noexcept { return bridge_.serviced_blocks(); }
    std::uint64_t completed_hops() const noexcept { return bridge_.completed_hops(); }
    auto callback_fence_reason() const noexcept { return bridge_.callback_fence_reason(); }
    auto worker_fence_reason() const noexcept { return bridge_.worker_fence_reason(); }
    bool fenced() const noexcept { return bridge_.fenced(); }
    std::uint64_t lost_trace_records() const noexcept { return bridge_.lost_trace_records(); }
    auto stopped_accounting() const noexcept { return bridge_.stopped_accounting(); }
    auto diagnostics() const { return bridge_.diagnostics(); }
    std::uint64_t epoch() const noexcept { return bridge_.epoch(); }
    void set_service_observer(SharedSpectralBridge::ServiceObserver fn,void* context) noexcept {
        bridge_.set_service_observer(fn,context);
    }
private:
    bool process_wet(const float* const*,float* const*,unsigned frames) noexcept;
    struct MixEvent { std::uint64_t sample=0; float target=1.f; };
    Config config_;
    pulp::signal::DryWetMixer mixer_;
    std::vector<MixEvent> mix_events_;
    std::vector<const float*> mix_input_;
    std::vector<float*> mix_output_;
    MaskRenderer::WetSource* wet_source_=nullptr;
    std::vector<float> wet_;
    std::vector<float*> wet_write_;
    std::vector<const float*> wet_read_;
    std::size_t mix_read_=0,mix_write_=0,mix_count_=0;
    std::uint64_t sample_cursor_=0;
    float latest_mix_=1.f;
    SharedSpectralBridge bridge_;
    std::vector<float> input_,output_;
    std::vector<const float*> input_ptrs_;
    std::vector<float*> output_ptrs_;
    std::atomic<std::uint64_t> quantums_{0};
    unsigned fill_=0;
    bool prepared_=false;
};
}
