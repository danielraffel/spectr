#include <spectr/experimental/shared_spectral_partition_adapter.hpp>
#include <algorithm>
#include <limits>
#include <cmath>

namespace spectr::experimental {
bool SharedSpectralPartitionAdapter::prepare(const Config& c) {
    if(!release())return false;
    // One quantum is assembly/serialization delay. Remaining latency belongs
    // to the bridge's fixed-quantum lead. No host callback size enters either.
    if(!std::isfinite(c.renderer.initial_mix) || c.renderer.initial_mix<0.f ||
       c.renderer.initial_mix>1.f || c.renderer.mix_ramp_samples<0 ||
       !c.internal_quantum || c.internal_quantum>8192 ||
       !c.max_callback_frames || c.max_callback_frames>65536 ||
       c.additional_latency_samples%c.internal_quantum ||
       c.additional_latency_samples/c.internal_quantum<2 ||
       c.additional_latency_samples/c.internal_quantum>9)return false;
    auto renderer=c.renderer;
    renderer.max_block=std::max(renderer.max_block,int(c.internal_quantum));
    renderer.initial_mix=1.f;renderer.mix_ramp_samples=0;
    SharedSpectralBridge::Config bridge_config{renderer,c.internal_quantum,
        c.additional_latency_samples/c.internal_quantum-1,c.immutable_layout,c.allow_cpu_only,c.force_cpu_only};
    if(!bridge_.prepare(bridge_config))return false;
    config_=c;
    input_.assign(std::size_t(c.internal_quantum)*c.renderer.channels,0.f);
    output_.assign(input_.size(),0.f);
    input_ptrs_.resize(c.renderer.channels);output_ptrs_.resize(c.renderer.channels);
    for(int ch=0;ch<c.renderer.channels;++ch){
        input_ptrs_[ch]=input_.data()+std::size_t(ch)*c.internal_quantum;
        output_ptrs_[ch]=output_.data()+std::size_t(ch)*c.internal_quantum;
    }
    mixer_.set_mix(c.renderer.initial_mix);
    mixer_.set_curve(pulp::signal::MixCurve::Linear);
    mixer_.set_ramp_samples(c.renderer.mix_ramp_samples);
    mixer_.set_wet_latency(bridge_.latency_samples()+c.internal_quantum);
    mixer_.prepare(c.renderer.channels,c.max_callback_frames);
    // At most one effective request per input sample, plus one not yet processed
    // at the current position. Same-position setters coalesce to their last value.
    mix_events_.resize(std::size_t(c.additional_latency_samples)+2);
    mix_input_.resize(c.renderer.channels);mix_output_.resize(c.renderer.channels);
    mix_read_=mix_write_=mix_count_=0;sample_cursor_=0;latest_mix_=c.renderer.initial_mix;
    fill_=0;quantums_=0;prepared_=true;return true;
}
bool SharedSpectralPartitionAdapter::reset(){const auto copy=config_;return prepare(copy);}
bool SharedSpectralPartitionAdapter::reset_realtime() noexcept {
    if(!prepared_ || !bridge_.reset_realtime())return false;
    std::fill(input_.begin(),input_.end(),0.f);
    std::fill(output_.begin(),output_.end(),0.f);
    // CPU reset settles the latest source-requested target, including a request
    // whose delayed output event has not reached the mixer yet.
    mixer_.set_mix(latest_mix_);mixer_.reset();
    mix_read_=mix_write_=mix_count_=0;sample_cursor_=0;
    fill_=0;quantums_=0;return true;
}
bool SharedSpectralPartitionAdapter::release(){prepared_=false;return bridge_.release();}
unsigned SharedSpectralPartitionAdapter::latency_samples() const noexcept {
    return prepared_?bridge_.latency_samples()+config_.internal_quantum:0;
}
bool SharedSpectralPartitionAdapter::set_mix(float mix) noexcept {
    if(!prepared_ || !std::isfinite(mix) ||
       sample_cursor_>std::numeric_limits<std::uint64_t>::max()-config_.additional_latency_samples)return false;
    mix=std::clamp(mix,0.f,1.f);
    const auto at=sample_cursor_+config_.additional_latency_samples;
    if(mix_count_){
        auto& last=mix_events_[(mix_write_+mix_events_.size()-1)%mix_events_.size()];
        if(last.sample==at){last.target=mix;latest_mix_=mix;return true;}
    }
    if(mix_count_==mix_events_.size())return false;
    mix_events_[mix_write_]={at,mix};mix_write_=(mix_write_+1)%mix_events_.size();++mix_count_;
    latest_mix_=mix;return true;
}
bool SharedSpectralPartitionAdapter::process(const float* const* input,
                                             float* const* output,unsigned frames) noexcept {
    if(!prepared_ || frames>config_.max_callback_frames)return false;
    if(!frames)return true;
    if(!input||!output)return false;
    for(int ch=0;ch<config_.renderer.channels;++ch)if(!input[ch]||!output[ch])return false;
    if(frames>std::numeric_limits<std::uint64_t>::max()-sample_cursor_)return false;
    unsigned offset=0;
    while(offset<frames){
        if(mix_count_ && mix_events_[mix_read_].sample<=sample_cursor_){
            mixer_.set_mix(mix_events_[mix_read_].target);
            mix_read_=(mix_read_+1)%mix_events_.size();--mix_count_;
        }
        unsigned count=frames-offset;
        if(mix_count_)count=unsigned(std::min<std::uint64_t>(count,mix_events_[mix_read_].sample-sample_cursor_));
        for(int ch=0;ch<config_.renderer.channels;++ch){
            mix_input_[ch]=input[ch]+offset;mix_output_[ch]=output[ch]+offset;
        }
        mixer_.push_dry(mix_input_.data(),config_.renderer.channels,count);
        if(!process_wet(mix_input_.data(),mix_output_.data(),count))return false;
        mixer_.mix_wet(mix_output_.data(),config_.renderer.channels,count);
        sample_cursor_+=count;offset+=count;
    }
    return true;
}
bool SharedSpectralPartitionAdapter::process_wet(const float* const* input,
                                                float* const* output,unsigned frames) noexcept {
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
