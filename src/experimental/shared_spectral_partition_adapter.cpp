#include <spectr/experimental/shared_spectral_partition_adapter.hpp>
#include <algorithm>
#include <limits>

namespace spectr::experimental {
bool SharedSpectralPartitionAdapter::prepare(const Config& c) {
    if(!release())return false;
    // One quantum is assembly/serialization delay. Remaining latency belongs
    // to the bridge's fixed-quantum lead. No host callback size enters either.
    if(!c.internal_quantum || c.internal_quantum>8192 ||
       !c.max_callback_frames || c.max_callback_frames>65536 ||
       c.additional_latency_samples%c.internal_quantum ||
       c.additional_latency_samples/c.internal_quantum<2 ||
       c.additional_latency_samples/c.internal_quantum>9)return false;
    auto renderer=c.renderer;
    renderer.max_block=std::max(renderer.max_block,int(c.internal_quantum));
    SharedSpectralBridge::Config bridge_config{renderer,c.internal_quantum,
        c.additional_latency_samples/c.internal_quantum-1,c.immutable_layout};
    if(!bridge_.prepare(bridge_config))return false;
    config_=c;
    input_.assign(std::size_t(c.internal_quantum)*c.renderer.channels,0.f);
    output_.assign(input_.size(),0.f);
    input_ptrs_.resize(c.renderer.channels);output_ptrs_.resize(c.renderer.channels);
    for(int ch=0;ch<c.renderer.channels;++ch){
        input_ptrs_[ch]=input_.data()+std::size_t(ch)*c.internal_quantum;
        output_ptrs_[ch]=output_.data()+std::size_t(ch)*c.internal_quantum;
    }
    fill_=0;quantums_=0;prepared_=true;return true;
}
bool SharedSpectralPartitionAdapter::reset(){const auto copy=config_;return prepare(copy);}
bool SharedSpectralPartitionAdapter::reset_realtime() noexcept {
    if(!prepared_ || !bridge_.reset_realtime())return false;
    std::fill(input_.begin(),input_.end(),0.f);
    std::fill(output_.begin(),output_.end(),0.f);
    fill_=0;quantums_=0;return true;
}
bool SharedSpectralPartitionAdapter::release(){prepared_=false;return bridge_.release();}
unsigned SharedSpectralPartitionAdapter::latency_samples() const noexcept {
    return prepared_?bridge_.latency_samples()+config_.internal_quantum:0;
}
bool SharedSpectralPartitionAdapter::process(const float* const* input,
                                             float* const* output,unsigned frames) noexcept {
    if(!prepared_ || frames>config_.max_callback_frames)return false;
    if(!frames)return true;
    if(!input||!output)return false;
    for(int ch=0;ch<config_.renderer.channels;++ch)if(!input[ch]||!output[ch])return false;
    unsigned offset=0;
    while(offset<frames){
        const auto count=std::min(frames-offset,config_.internal_quantum-fill_);
        // Copy input first so ordinary in-place processing cannot destroy it.
        for(int ch=0;ch<config_.renderer.channels;++ch){
            auto* assembled=input_.data()+std::size_t(ch)*config_.internal_quantum;
            std::copy_n(input[ch]+offset,count,assembled+fill_);
            std::copy_n(output_ptrs_[ch]+fill_,count,output[ch]+offset);
        }
        fill_+=count;offset+=count;
        if(fill_==config_.internal_quantum){
            if(!bridge_.process(input_ptrs_.data(),output_ptrs_.data()))return false;
            fill_=0;quantums_.fetch_add(1,std::memory_order_release);
        }
    }
    return true;
}
}
