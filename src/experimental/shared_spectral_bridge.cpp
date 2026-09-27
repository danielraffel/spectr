#include <spectr/experimental/shared_spectral_bridge.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace spectr::experimental {
namespace {
// Reserve logical identities only during quiescent prepare. Callback reset is
// one local increment, not a contended allocation/CAS loop. Physical provider
// epochs remain independent and are checked separately by the service owner.
std::uint64_t reserve_epochs() noexcept {
    static std::atomic<std::uint64_t> next{1};
    constexpr std::uint64_t count=std::uint64_t{1}<<32;
    auto value=next.load(std::memory_order_relaxed);
    while(value<=std::numeric_limits<std::uint64_t>::max()-count)
        if(next.compare_exchange_weak(value,value+count,std::memory_order_relaxed))return value;
    return 0;
}
}
bool SharedSpectralBridge::prepare(const Config& c) {
    if(!release())return false;
    if(!c.host_block || c.host_block>8192 || c.lead_host_blocks<1 || c.lead_host_blocks>8 ||
       c.renderer.analysis_hop<int(c.host_block) || c.renderer.analysis_hop%c.host_block ||
       c.renderer.max_block<int(c.host_block) || c.renderer.channels<1 || c.renderer.channels>8 ||
       c.renderer.initial_mix!=1.f || c.renderer.mix_ramp_samples!=0)return false;
    auto cpu=make_mask_renderer(MaskRenderMode::linear_phase);
    if(!cpu || !cpu->prepare(c.renderer) || !cpu->publish_layout(c.immutable_layout))return false;
    const auto base=reserve_epochs();if(!base)return false;
    config_=c;cpu_=std::move(cpu);epoch_=base;epoch_limit_=base+(std::uint64_t{1}<<32)-1;
    const auto samples=c.host_block*c.renderer.channels;
    cpu_block_.assign(samples,0);fallback_.assign(slots*samples,0);
    hop_input_.assign(c.renderer.analysis_hop*c.renderer.channels,0);
    hop_output_.assign(hop_input_.size(),0);cpu_ptrs_.resize(c.renderer.channels);
    hop_gains_.assign(c.renderer.design_grid_size/2+1,1.f);
    for(int ch=0;ch<c.renderer.channels;++ch)cpu_ptrs_[ch]=cpu_block_.data()+ch*c.host_block;
    for(unsigned i=0;i<slots;++i){
        inputs_[i].samples.assign(samples,0);outputs_[i].samples.assign(samples,0);
        controls_[i].samples.assign(hop_gains_.size(),0.f);
        inputs_[i].state.store(empty);outputs_[i].state.store(empty);controls_[i].state.store(empty);
    }
    if(!cpu_->set_effective_frame_observer(this,
        [](void* context,const MaskRenderer::Table& table,std::uint64_t ordinal) noexcept {
            static_cast<SharedSpectralBridge*>(context)->capture_frame(table,ordinal);
        }))return false;
    reset_callback_state();requested_epoch_.store(epoch_,std::memory_order_release);
    if(!prepare_worker(epoch_))return false;
    prepared_=true;return true;
}
void SharedSpectralBridge::reset_callback_state() noexcept {
    cpu_->reset();
    std::fill(cpu_block_.begin(),cpu_block_.end(),0.f);
    std::fill(fallback_.begin(),fallback_.end(),0.f);
    admitted_.fill(false);
    callback_sequence_=input_count_=next_terminal_=0;callback_count_=0;finishing_=false;
}
bool SharedSpectralBridge::reset() {const auto copy=config_;return prepare(copy);}
bool SharedSpectralBridge::reset_realtime() noexcept {
    if(!prepared_ || epoch_==epoch_limit_)return false;
    while(next_terminal_<input_count_)
        terminal(next_terminal_,pulp::gpu_audio::GpuAudioTerminalDisposition::Cancelled);
    ++epoch_;reset_callback_state();
    requested_epoch_.store(epoch_,std::memory_order_release);
    return true;
}
bool SharedSpectralBridge::release() {
    prepared_=false;
    while(next_terminal_<input_count_)
        terminal(next_terminal_,pulp::gpu_audio::GpuAudioTerminalDisposition::Cancelled);
    if(gpu_ && !gpu_->release())return false;
    gpu_.reset();cpu_.reset();return true;
}
unsigned SharedSpectralBridge::latency_samples() const noexcept {
    return prepared_?unsigned(cpu_->latency_samples())+config_.host_block*config_.lead_host_blocks:0;
}
bool SharedSpectralBridge::claim_empty_or_obsolete(Slot& slot,std::uint64_t epoch) noexcept {
    unsigned expected=empty;
    if(slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire))return true;
    expected=ready;
    if(!slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire))return false;
    if(slot.epoch!=epoch)return true;
    slot.state.store(ready,std::memory_order_release);return false;
}
bool SharedSpectralBridge::process(const float* const* input,float* const* output,bool admit) noexcept {
    if(!prepared_ || (finishing_ && admit))return false;
    if(!admit)finishing_=true;
    const unsigned b=config_.host_block,channels=config_.renderer.channels,samples=b*channels;
    const auto q=callback_sequence_++;
    if(!cpu_->process(input,cpu_ptrs_.data(),b)){callback_failed_epoch_=epoch_;return false;}
    std::copy(cpu_block_.begin(),cpu_block_.end(),fallback_.begin()+(q%slots)*samples);
    admitted_[q%slots]=false;
    if(admit){
        ++input_count_;auto& slot=inputs_[q%slots];
        if(!fenced() && claim_empty_or_obsolete(slot,epoch_)){
            slot.epoch=epoch_;slot.sequence=q;
            for(unsigned ch=0;ch<channels;++ch)std::copy_n(input[ch],b,slot.samples.data()+ch*b);
            slot.state.store(ready,std::memory_order_release);admitted_[q%slots]=true;
        }else callback_failed_epoch_.store(epoch_,std::memory_order_release);
    }
    if(q<config_.lead_host_blocks){
        for(unsigned ch=0;ch<channels;++ch)std::fill_n(output[ch],b,0.f);
    }else{
        const auto target=q-config_.lead_host_blocks;bool delivered=false;
        auto& slot=outputs_[target%slots];unsigned expected=ready;
        if(slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire)){
            if(slot.epoch==epoch_ && slot.sequence==target && !fenced() &&
               ready_epoch_.load(std::memory_order_acquire)==epoch_){
                for(unsigned ch=0;ch<channels;++ch)std::copy_n(slot.samples.data()+ch*b,b,output[ch]);
                delivered=true;
            }
            slot.state.store(empty,std::memory_order_release);
        }
        if(!delivered)for(unsigned ch=0;ch<channels;++ch)
            std::copy_n(fallback_.data()+(target%slots)*samples+ch*b,b,output[ch]);
        if(target<input_count_)terminal(target,
            delivered?pulp::gpu_audio::GpuAudioTerminalDisposition::GpuDelivered:
                      pulp::gpu_audio::GpuAudioTerminalDisposition::CpuFallback);
    }
    callback_count_.store(q+1,std::memory_order_release);return true;
}
void SharedSpectralBridge::terminal(std::uint64_t sequence,
                                    pulp::gpu_audio::GpuAudioTerminalDisposition disposition) noexcept {
    const auto write=trace_write_.load(std::memory_order_relaxed);
    if(write-trace_read_.load(std::memory_order_acquire)<trace_slots){
        trace_[write%trace_slots]={epoch_,sequence,admitted_[sequence%slots],disposition};
        trace_write_.store(write+1,std::memory_order_release);
    }else trace_lost_.fetch_add(1);
    next_terminal_=sequence+1;
}
bool SharedSpectralBridge::prepare_worker(std::uint64_t requested) noexcept {
    observe(ServicePoint::BeforeRelease);
    if(gpu_ && !gpu_->release()){worker_failed_epoch_=requested;return false;}
    gpu_.reset();worker_epoch_=requested;
    worker_sequence_=hop_sequence_=0;accumulated_=0;hop_pending_=false;hop_gains_loaded_=false;serviced_=0;completed_=0;
    progress_epoch_.store(requested,std::memory_order_release);
    if(requested_epoch_.load(std::memory_order_acquire)!=requested)return false;
    MaskRenderer::Table table;
    if(!pulp::signal::build_spectral_mask(config_.immutable_layout,config_.renderer.design_grid_size,
                                        float(config_.renderer.sample_rate),table)){
        worker_failed_epoch_=requested;return false;
    }
    std::copy_n(table.gain_linear.data(),table.num_bins,hop_gains_.data());
    observe(ServicePoint::BeforePrepare);
    auto made=pulp::gpu_audio::GpuSpectralMaskSession::create_with_per_hop_gains({
        unsigned(config_.renderer.design_grid_size),unsigned(config_.renderer.analysis_hop),
        unsigned(config_.renderer.channels),unsigned(config_.renderer.sample_rate),4,
        std::span<const float>(table.gain_linear.data(),table.num_bins)});
    const bool made_ok=bool(made);
    gpu_=std::move(made.session);
    if(!made_ok){worker_failed_epoch_=requested;return false;}
    physical_epoch_=gpu_->epoch();return true;
}
void SharedSpectralBridge::capture_frame(const MaskRenderer::Table& table,std::uint64_t ordinal) noexcept {
    if(fenced())return;
    auto& slot=controls_[ordinal%slots];
    if(table.num_bins!=int(slot.samples.size()) || !claim_empty_or_obsolete(slot,epoch_)){
        callback_failed_epoch_.store(epoch_,std::memory_order_release);return;
    }
    slot.epoch=epoch_;slot.sequence=ordinal;
    std::copy_n(table.gain_linear.data(),table.num_bins,slot.samples.data());
    slot.state.store(ready,std::memory_order_release);
}
bool SharedSpectralBridge::load_hop_gains() noexcept {
    const auto first_frame=std::uint64_t(config_.renderer.design_grid_size/config_.renderer.analysis_hop-1);
    if(hop_sequence_<first_frame){hop_gains_loaded_=true;return true;}
    const auto frame=hop_sequence_-first_frame;
    auto& slot=controls_[frame%slots];unsigned expected=ready;
    if(!slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire)){
        worker_failed_epoch_=worker_epoch_;return false;
    }
    const auto latest=requested_epoch_.load(std::memory_order_acquire);
    if(latest!=worker_epoch_ || slot.epoch!=worker_epoch_){
        slot.state.store(slot.epoch==latest?ready:empty,std::memory_order_release);return false;
    }
    if(slot.sequence!=frame){
        slot.state.store(empty,std::memory_order_release);worker_failed_epoch_=worker_epoch_;return false;
    }
    std::copy(slot.samples.begin(),slot.samples.end(),hop_gains_.begin());
    slot.state.store(empty,std::memory_order_release);
    hop_gains_loaded_=true;return true;
}
void SharedSpectralBridge::collect_completed() noexcept {
    while(auto result=gpu_->receive(hop_output_)){
        if(result->epoch!=physical_epoch_ || !result->delivered || result->late){worker_failed_epoch_=worker_epoch_;continue;}
        const unsigned b=config_.host_block,h=config_.renderer.analysis_hop,c=config_.renderer.channels;
        for(unsigned part=0;part<h/b;++part){
            const auto sequence=result->sequence*(h/b)+part;
            if(requested_epoch_.load(std::memory_order_acquire)!=worker_epoch_ || fenced() ||
               callback_count_.load(std::memory_order_acquire)>sequence+config_.lead_host_blocks)continue;
            auto& slot=outputs_[sequence%slots];
            if(!claim_empty_or_obsolete(slot,worker_epoch_))continue;
            slot.epoch=worker_epoch_;slot.sequence=sequence;
            for(unsigned ch=0;ch<c;++ch)std::copy_n(hop_output_.data()+ch*h+part*b,b,slot.samples.data()+ch*b);
            observe(ServicePoint::BeforeOutputPublish);
            slot.state.store(ready,std::memory_order_release);
        }
        if(requested_epoch_.load(std::memory_order_acquire)==worker_epoch_)
            ready_epoch_.store(worker_epoch_,std::memory_order_release);
        completed_.fetch_add(1,std::memory_order_release);
    }
}
void SharedSpectralBridge::service() noexcept {
    if(!prepared_)return;
    const auto requested=requested_epoch_.load(std::memory_order_acquire);
    if(requested!=worker_epoch_ && !prepare_worker(requested))return;
    if(requested_epoch_.load(std::memory_order_acquire)!=worker_epoch_ || !gpu_)return;
    gpu_->service(0);collect_completed();
    const auto callback=callback_count_.load(std::memory_order_acquire);
    for(auto& slot:outputs_){
        unsigned expected=ready;
        if(!slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire))continue;
        const bool obsolete=slot.epoch!=worker_epoch_ || callback>slot.sequence+config_.lead_host_blocks;
        slot.state.store(obsolete?empty:ready,std::memory_order_release);
    }
    if(fenced())return;
    const unsigned b=config_.host_block,h=config_.renderer.analysis_hop,c=config_.renderer.channels;
    for(unsigned budget=0;budget<slots;++budget){
        if(requested_epoch_.load(std::memory_order_acquire)!=worker_epoch_)return;
        if(hop_pending_){
            if(!hop_gains_loaded_ && !load_hop_gains())return;
            observe(ServicePoint::BeforeSubmit);
            if(requested_epoch_.load(std::memory_order_acquire)!=worker_epoch_)return;
            if(!gpu_->submit_hop_with_gains(hop_input_,hop_sequence_,hop_gains_)){
                if(!gpu_->prepared())worker_failed_epoch_=worker_epoch_;
                return;
            }
            ++hop_sequence_;accumulated_=0;hop_pending_=false;hop_gains_loaded_=false;
        }
        auto& slot=inputs_[worker_sequence_%slots];unsigned expected=ready;
        observe(ServicePoint::BeforeInputClaim);
        if(!slot.state.compare_exchange_strong(expected,busy,std::memory_order_acquire))return;
        observe(ServicePoint::InputClaimed);
        const auto latest=requested_epoch_.load(std::memory_order_acquire);
        if(latest!=worker_epoch_ || slot.epoch!=worker_epoch_){
            // Reset may have published new input after this old worker's loop
            // check. Preserve the current epoch's journal; only obsolete input
            // is ours to discard. Never turn this race into a history gap.
            slot.state.store(slot.epoch==latest?ready:empty,std::memory_order_release);
            return;
        }
        if(slot.sequence!=worker_sequence_){slot.state.store(empty,std::memory_order_release);worker_failed_epoch_=worker_epoch_;return;}
        for(unsigned ch=0;ch<c;++ch)std::copy_n(slot.samples.data()+ch*b,b,hop_input_.data()+ch*h+accumulated_);
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
