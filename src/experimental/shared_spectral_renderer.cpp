#include <spectr/experimental/shared_spectral_renderer.hpp>
#include <chrono>
#include <pulp/runtime/trace.hpp>

namespace spectr::experimental {
SharedSpectralMaskRenderer::~SharedSpectralMaskRenderer(){
    // A failed physical release is not permission to destroy backing storage.
    // Keep the entire stopped ownership graph alive until process exit.
    if(!stop())(void)adapter_.release();
}
bool SharedSpectralMaskRenderer::release() noexcept {return stop();}
bool SharedSpectralMaskRenderer::stop() noexcept {
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
    prepared_=false;
    const bool released=adapter_->release();drain_terminals();
    state_.store(released?ProviderState::Unprepared:ProviderState::ReleaseUnconfirmed,std::memory_order_release);
    return released;
}
bool SharedSpectralMaskRenderer::prepare(const MaskRendererConfig& c){
    if(!stop())return false;
    if(c.analysis_hop<2 || c.analysis_hop%2 || c.max_block<1)return false;
    SharedSpectralPartitionAdapter::Config a;
    a.renderer=c;a.internal_quantum=unsigned(c.analysis_hop/2);
    a.additional_latency_samples=additional_latency(c);
    a.max_callback_frames=unsigned(c.max_block);
    a.allow_cpu_only=true;a.force_cpu_only=force_cpu_only_;
    if(!adapter_->prepare(a))return false;
    config_=c;prepared_=true;
    gpu_delivered_=0;cpu_fallback_=0;cancelled_=0;lost_records_=0;
    state_.store(adapter_->provider_prepared()?ProviderState::SharedReady:ProviderState::CpuOnly,std::memory_order_release);
    try{
        worker_=std::jthread([this](std::stop_token stop){
            while(!stop.stop_requested()){
                service();
                // Experimental service cadence, not a realtime scheduling claim.
                // Callback never signals a condition variable or enters Dawn.
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
            service();
        });
    }catch(...){stop();return false;}
    return true;
}
void SharedSpectralMaskRenderer::service() noexcept {
    adapter_->service();
    drain_terminals();
    epoch_=adapter_->epoch();callback_reason_=unsigned(adapter_->callback_fence_reason());worker_reason_=unsigned(adapter_->worker_fence_reason());
    const auto next=!adapter_->provider_prepared()?ProviderState::CpuOnly:
                    adapter_->fenced()?ProviderState::Fenced:ProviderState::SharedReady;
    state_.store(next,std::memory_order_release);
}
void SharedSpectralMaskRenderer::drain_terminals() noexcept {
    SharedSpectralBridge::Terminal terminal;
    while(adapter_->pop_terminal(terminal)){
        PULP_TRACE_INSTANT_ARGS("gpu","spectr.shared_audio.delivery",
            "stream_epoch",terminal.stream_epoch,"block_sequence",terminal.block_sequence,
            "ingress_admitted",terminal.ingress_admitted,"disposition",unsigned(terminal.disposition),
            "callback_fence_reason",unsigned(terminal.callback_reason),"worker_fence_reason",unsigned(terminal.worker_reason));
        switch(terminal.disposition){
        case pulp::gpu_audio::GpuAudioTerminalDisposition::GpuDelivered:++gpu_delivered_;break;
        case pulp::gpu_audio::GpuAudioTerminalDisposition::CpuFallback:++cpu_fallback_;break;
        case pulp::gpu_audio::GpuAudioTerminalDisposition::Cancelled:++cancelled_;break;
        default:break;
        }
    }
    lost_records_.store(adapter_->lost_trace_records(),std::memory_order_release);
}
int SharedSpectralMaskRenderer::latency_samples() const noexcept {return int(adapter_->latency_samples());}
int SharedSpectralMaskRenderer::maximum_tail_samples() const noexcept {return int(adapter_->maximum_tail_samples());}
bool SharedSpectralMaskRenderer::publish_layout(const Layout& layout){return prepared_&&adapter_->publish_layout(layout);}
bool SharedSpectralMaskRenderer::set_layout_rt(const Layout& layout) noexcept {return prepared_&&adapter_->set_layout_rt(layout);}
void SharedSpectralMaskRenderer::set_mix(float mix) noexcept {if(prepared_)(void)adapter_->set_mix(mix);}
bool SharedSpectralMaskRenderer::process(const float* const* input,float* const* output,int frames) noexcept {
    return prepared_&&frames>=0&&adapter_->process(input,output,unsigned(frames));
}
void SharedSpectralMaskRenderer::reset() noexcept {if(prepared_)(void)adapter_->reset_realtime();}
unsigned long long SharedSpectralMaskRenderer::active_generation() const noexcept {return adapter_->active_generation();}
SharedSpectralMaskRenderer::Snapshot SharedSpectralMaskRenderer::snapshot() const noexcept {
    return {state_.load(std::memory_order_acquire),epoch_.load(),callback_reason_.load(),worker_reason_.load(),gpu_delivered_.load(),cpu_fallback_.load(),cancelled_.load(),lost_records_.load()};
}
}
