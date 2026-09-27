#include <spectr/experimental/shared_spectral_bridge.hpp>
#include <algorithm>
#include <cmath>

namespace spectr::experimental {
bool SharedSpectralBridge::prepare(const Config& c) {
    if (!release()) return false;
    if (!c.host_block || c.host_block>8192 || c.lead_host_blocks<1 || c.lead_host_blocks>8 ||
        c.renderer.analysis_hop<int(c.host_block) || c.renderer.analysis_hop%c.host_block ||
        c.renderer.max_block<int(c.host_block) || c.renderer.channels<1 || c.renderer.channels>8 ||
        c.renderer.initial_mix!=1.f || c.renderer.mix_ramp_samples!=0 ||
        c.immutable_layout.transition_frames!=0) return false;
    MaskRenderer::Table table;
    if (!pulp::signal::build_spectral_mask(c.immutable_layout,c.renderer.design_grid_size,
                                          float(c.renderer.sample_rate),table)) return false;
    auto cpu=make_mask_renderer(MaskRenderMode::linear_phase);
    if (!cpu || !cpu->prepare(c.renderer) || !cpu->publish_layout(c.immutable_layout)) return false;
    auto gpu=pulp::gpu_audio::GpuSpectralMaskSession::create({
        unsigned(c.renderer.design_grid_size),unsigned(c.renderer.analysis_hop),
        unsigned(c.renderer.channels),unsigned(c.renderer.sample_rate),4,
        std::span<const float>(table.gain_linear.data(),table.num_bins)});
    if (!gpu) { gpu_=std::move(gpu.session); return false; }
    config_=c; cpu_=std::move(cpu); gpu_=std::move(gpu.session); epoch_=gpu_->epoch();
    const auto samples=c.host_block*c.renderer.channels;
    cpu_block_.assign(samples,0); fallback_.assign(slots*samples,0);
    hop_input_.assign(c.renderer.analysis_hop*c.renderer.channels,0);
    hop_output_.assign(hop_input_.size(),0); cpu_ptrs_.resize(c.renderer.channels);
    for (int ch=0;ch<c.renderer.channels;++ch) cpu_ptrs_[ch]=cpu_block_.data()+ch*c.host_block;
    for (unsigned i=0;i<slots;++i) {
        inputs_[i].samples.assign(samples,0); outputs_[i].samples.assign(samples,0);
        inputs_[i].state.store(empty); outputs_[i].state.store(empty); admitted_[i]=false;
    }
    callback_sequence_=worker_sequence_=hop_sequence_=input_count_=next_terminal_=0; accumulated_=0;
    callback_count_=0; serviced_=0; completed_=0;
    fenced_=false; finishing_=false; hop_pending_=false; prepared_=true;
    return true;
}
bool SharedSpectralBridge::reset() { const auto copy=config_; return prepare(copy); }
bool SharedSpectralBridge::release() {
    prepared_=false;
    while(next_terminal_<input_count_)
        terminal(next_terminal_,pulp::gpu_audio::GpuAudioTerminalDisposition::Cancelled);
    if (gpu_ && !gpu_->release()) return false;
    gpu_.reset(); cpu_.reset(); return true;
}
unsigned SharedSpectralBridge::latency_samples() const noexcept {
    return prepared_ ? unsigned(cpu_->latency_samples())+config_.host_block*config_.lead_host_blocks : 0;
}
bool SharedSpectralBridge::process(const float* const* input,float* const* output,bool admit) noexcept {
    if (!prepared_ || (finishing_ && admit)) return false;
    if (!admit) finishing_=true;
    const unsigned b=config_.host_block, channels=config_.renderer.channels, samples=b*channels;
    const auto q=callback_sequence_++;
    if (!cpu_->process(input,cpu_ptrs_.data(),b)) {fenced_.store(true);return false;}
    std::copy(cpu_block_.begin(),cpu_block_.end(),fallback_.begin()+(q%slots)*samples);
    admitted_[q%slots]=false;
    if (admit) {
        ++input_count_;
        auto& slot=inputs_[q%slots];
        unsigned expected=empty;
        if (!fenced() && slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire)) {
            slot.sequence=q;
            for(unsigned ch=0;ch<channels;++ch)
                std::copy_n(input[ch],b,slot.samples.data()+ch*b);
            slot.state.store(ready,std::memory_order_release); admitted_[q%slots]=true;
        } else fenced_.store(true,std::memory_order_release);
    }
    if (q<config_.lead_host_blocks) {
        for(unsigned ch=0;ch<channels;++ch)std::fill_n(output[ch],b,0.f);
    } else {
        const auto target=q-config_.lead_host_blocks;
        bool delivered=false;
        auto& slot=outputs_[target%slots];
        unsigned expected=ready;
        if(slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire)) {
            if(slot.sequence==target && !fenced()) {
                for(unsigned ch=0;ch<channels;++ch)std::copy_n(slot.samples.data()+ch*b,b,output[ch]);
                delivered=true;
            }
            slot.state.store(empty,std::memory_order_release);
        }
        if(!delivered)for(unsigned ch=0;ch<channels;++ch)
            std::copy_n(fallback_.data()+(target%slots)*samples+ch*b,b,output[ch]);
        if(target<input_count_) terminal(target,
            delivered?pulp::gpu_audio::GpuAudioTerminalDisposition::GpuDelivered:
                      pulp::gpu_audio::GpuAudioTerminalDisposition::CpuFallback);
    }
    callback_count_.store(q+1,std::memory_order_release);
    return true;
}
void SharedSpectralBridge::terminal(std::uint64_t sequence,
                                    pulp::gpu_audio::GpuAudioTerminalDisposition disposition) noexcept {
    const auto write=trace_write_.load(std::memory_order_relaxed);
    if(write-trace_read_.load(std::memory_order_acquire)<trace_slots) {
        trace_[write%trace_slots]={epoch_,sequence,admitted_[sequence%slots],disposition};
        trace_write_.store(write+1,std::memory_order_release);
    } else trace_lost_.fetch_add(1);
    next_terminal_=sequence+1;
}
void SharedSpectralBridge::collect_completed() noexcept {
    while(auto result=gpu_->receive(hop_output_)) {
        if(result->epoch!=epoch_ || !result->delivered || result->late) {fenced_=true;continue;}
        completed_.fetch_add(1,std::memory_order_release);
        const unsigned b=config_.host_block,h=config_.renderer.analysis_hop,c=config_.renderer.channels;
        for(unsigned part=0;part<h/b;++part) {
            const auto sequence=result->sequence*(h/b)+part;
            if(fenced() || callback_count_.load(std::memory_order_acquire)>sequence+config_.lead_host_blocks)continue;
            auto& slot=outputs_[sequence%slots];
            unsigned expected=empty;
            if(!slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire))continue;
            slot.sequence=sequence;
            for(unsigned ch=0;ch<c;++ch)
                std::copy_n(hop_output_.data()+ch*h+part*b,b,slot.samples.data()+ch*b);
            slot.state.store(ready,std::memory_order_release);
        }
    }
}
void SharedSpectralBridge::service() noexcept {
    if(!prepared_)return;
    gpu_->service(0); collect_completed();
    // Only the service owner reclaims late output; callback claims a ready slot
    // before reading metadata, so reclamation cannot race a sample read.
    const auto callback=callback_count_.load(std::memory_order_acquire);
    for(auto& slot:outputs_)if(slot.state.load(std::memory_order_acquire)==ready &&
        callback>slot.sequence+config_.lead_host_blocks) {
        unsigned expected=ready;slot.state.compare_exchange_strong(expected,empty,std::memory_order_acq_rel);
    }
    if(fenced())return;
    const unsigned b=config_.host_block,h=config_.renderer.analysis_hop,c=config_.renderer.channels;
    for(unsigned budget=0;budget<slots;++budget) {
        if(hop_pending_) {
            if(!gpu_->submit_hop(hop_input_,hop_sequence_)) {
                if(!gpu_->prepared())fenced_=true;
                return; // Retry intact hop, never drop history.
            }
            ++hop_sequence_; accumulated_=0;hop_pending_=false;
        }
        auto& slot=inputs_[worker_sequence_%slots];unsigned expected=ready;
        if(!slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire))return;
        if(slot.sequence!=worker_sequence_){slot.state.store(empty);fenced_=true;return;}
        for(unsigned ch=0;ch<c;++ch)
            std::copy_n(slot.samples.data()+ch*b,b,hop_input_.data()+ch*h+accumulated_);
        slot.state.store(empty,std::memory_order_release);++worker_sequence_;accumulated_+=b;
        serviced_.store(worker_sequence_,std::memory_order_release);
        if(accumulated_==h)hop_pending_=true;
    }
}
bool SharedSpectralBridge::pop_terminal(Terminal& result) noexcept {
    const auto read=trace_read_.load(std::memory_order_relaxed);
    if(read==trace_write_.load(std::memory_order_acquire))return false;
    result=trace_[read%trace_slots];trace_read_.store(read+1,std::memory_order_release);return true;
}
pulp::gpu_audio::GpuSpectralMaskSession::Diagnostics SharedSpectralBridge::diagnostics() const {
    return gpu_?gpu_->diagnostics():pulp::gpu_audio::GpuSpectralMaskSession::Diagnostics{};
}
}
