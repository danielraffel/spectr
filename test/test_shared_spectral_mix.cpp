#define main partition_controls_main
#include "test_shared_spectral_partitions.cpp"
#undef main
#include <map>
#include <limits>

int mix_case(const std::vector<unsigned>& partitions,bool do_reset,bool overflow,int ramp,bool dense,bool inplace,bool fully_dry){
    Adapter::Config c;
    c.renderer={.design_grid_size=256,.analysis_hop=64,.channels=2,.max_block=128,
                .sample_rate=48000,.initial_mix=fully_dry?0.f:.25f,.mix_ramp_samples=ramp};
    c.immutable_layout.transition_frames=0;c.immutable_layout.active_bands=2;
    c.immutable_layout.edge_policy=pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    c.immutable_layout.bands[0].gain_db=-6;c.immutable_layout.bands[1].gain_db=-12;
    c.internal_quantum=32;c.additional_latency_samples=160;c.max_callback_frames=128;
    auto adapter=std::make_unique<Adapter>();
    auto oracle=spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    if(!adapter->prepare(c)||!oracle->prepare(c.renderer)||!oracle->publish_layout(c.immutable_layout))return 70;
    if(adapter->set_mix(std::numeric_limits<float>::quiet_NaN()))return 71;
    constexpr unsigned frames=4096,reset_at=2100;
    std::vector<float> input(2*frames),actual(2*frames),reference(2*frames);
    unsigned rng=567;
    for(auto& v:input){rng=rng*1664525u+1013904223u;v=float(rng>>8)/16777216.f-.5f;}
    for(unsigned ch=0;ch<2;++ch)std::fill(input.begin()+ch*frames+3200,input.begin()+(ch+1)*frames,0.f);
    const unsigned events[]={320,384,448,765,1537,2201,2240,2432};
    const unsigned mix_positions[]={0,321,337,401,767,1999,2099,2203,2497};
    unsigned pos=0,part=0,event=0,mix_event=0;double error=0;callback_allocations=0;
    while(mix_event<9 && mix_positions[mix_event]==0){
        if(!fully_dry){adapter->set_mix(.9f);oracle->set_mix(.9f);}++mix_event;
    }
    while(pos<frames){
        if(do_reset && pos==reset_at){
            guard_allocations=true;const bool ok=adapter->reset_realtime();guard_allocations=false;
            if(!ok||callback_allocations)return 72;oracle->reset();
        }
        if(event<8 && pos==events[event]){
            auto next=c.immutable_layout;
            next.transition_frames=event%2?2:8;
            next.bands[0].gain_db=event%2?-18.f:3.f;
            next.bands[1].gain_db=event%2?0.f:-24.f;
            if(event%2){
                guard_allocations=true;const bool ok=adapter->set_layout_rt(next);guard_allocations=false;
                if(!ok||!oracle->set_layout_rt(next))return 73;
            }else if(!adapter->publish_layout(next)||!oracle->publish_layout(next))return 74;
            ++event;
        }
        if(!fully_dry && (dense || (mix_event<9 && pos==mix_positions[mix_event]))){
            const float target=float((pos*17)%101)/100.f;
            guard_allocations=true;
            bool ok=true;
            for(unsigned repeat=0;repeat<(pos==321?1000u:2u);++repeat)
                ok=adapter->set_mix(.1f)&&adapter->process(nullptr,nullptr,0)&&ok;
            ok=adapter->set_mix(target)&&ok;
            guard_allocations=false;
            if(!ok||callback_allocations)return 84;
            oracle->set_mix(.1f);oracle->set_mix(target);
            if(!dense)++mix_event;
        }
        unsigned n=std::min(partitions[part++%partitions.size()],frames-pos);
        if(dense)n=1;
        if(!fully_dry && !dense && mix_event<9)n=std::min(n,mix_positions[mix_event]-pos);
        if(event<8)n=std::min(n,events[event]-pos);
        if(do_reset && pos<reset_at)n=std::min(n,reset_at-pos);
        const float* in[]={input.data()+pos,input.data()+frames+pos};
        float* out[]={actual.data()+pos,actual.data()+frames+pos};
        float* ref[]={reference.data()+pos,reference.data()+frames+pos};
        if(!oracle->process(in,ref,n))return 75;
        if(inplace)for(unsigned ch=0;ch<2;++ch)std::copy_n(in[ch],n,out[ch]);
        const float* inplace_in[]={out[0],out[1]};
        guard_allocations=true;const bool ok=adapter->process(inplace?inplace_in:in,out,n);guard_allocations=false;
        if(!ok||callback_allocations)return 76;
        for(unsigned ch=0;ch<2;++ch)for(unsigned i=pos;i<pos+n;++i){
            const unsigned epoch_start=do_reset&&i>=reset_at?reset_at:0;
            const double expected=i<epoch_start+160?0:reference[ch*frames+i-160];
            if(fully_dry){
                const double delayed=i<epoch_start+adapter->latency_samples()?0:input[ch*frames+i-adapter->latency_samples()];
                if(actual[ch*frames+i]!=delayed)return 85;
            }
            const auto residual=std::abs(double(actual[ch*frames+i])-expected);
            if(!std::isfinite(residual))return 77;error=std::max(error,residual);
        }
        pos+=n;
        const bool stalled=overflow?(pos>=256&&pos<2800):(pos>=600&&pos<1000);
        if(!stalled){
            const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            do{
                adapter->service();
                if(adapter->fenced() || (adapter->serviced_quantums()>=adapter->quantum_count()&&
                   adapter->completed_hops()>=adapter->quantum_count()/2))break;
                if(std::chrono::steady_clock::now()>stop)return 78;
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }while(true);
        }
    }
    const auto diagnostic=adapter->diagnostics();
    if(diagnostic.runtime_write_buffer_calls||diagnostic.runtime_copy_buffer_calls||diagnostic.runtime_map_async_calls)return 79;
    if(adapter->fenced()!=overflow || !adapter->release())return 80;
    std::map<std::uint64_t,std::uint64_t> next_sequence;
    unsigned gpu=0,cpu=0,cancelled=0; spectr::experimental::SharedSpectralBridge::Terminal terminal;
    while(adapter->pop_terminal(terminal)){
        if(terminal.block_sequence!=next_sequence[terminal.stream_epoch]++)return 81;
        gpu+=terminal.disposition==Outcome::GpuDelivered;
        cpu+=terminal.disposition==Outcome::CpuFallback;
        cancelled+=terminal.disposition==Outcome::Cancelled;
    }
    if(!gpu||!cpu||!cancelled||adapter->lost_trace_records()||next_sequence.size()!=(do_reset?2u:1u))return 82;
    std::cout<<"mix partition="<<partitions[0]<<" irregular="<<(partitions.size()>1)
             <<" ramp="<<ramp<<" dense="<<dense<<" inplace="<<inplace<<" dry="<<fully_dry<<" reset="<<do_reset<<" overflow="<<overflow<<" max_error="<<error
             <<" gpu="<<gpu<<" fallback="<<cpu<<" cancelled="<<cancelled
             <<" callback_allocations="<<callback_allocations<<" runtime_webgpu_calls=0\n";
    return error<1e-4?0:83;
}
int main(){
    for(int ramp:{0,93})for(unsigned n:{1u,31u,32u,63u,64u,127u,128u}){
        const auto rc=mix_case({n},true,false,ramp,false,n%2,false);if(rc){std::cerr<<"mix_failure="<<rc<<'\n';return rc;}
    }
    for(int ramp:{0,93})for(bool dense:{false,true}){
        const auto rc=mix_case({1,31,127,64,7,128},true,false,ramp,dense,true,false);if(rc)return rc;
    }
    for(bool overflow:{false,true}){
        const auto rc=mix_case({1,31,127,64,7,128},!overflow,overflow,93,true,true,false);if(rc)return rc;
        const auto dry=mix_case({1,31,127,64,7,128},!overflow,overflow,93,false,true,true);if(dry)return dry;
    }
    return 0;
}
