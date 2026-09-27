// Reuse the tested allocation instrumentation and partition controls in the same
// executable, then add reset-specific causal-history and race-barrier checks.
#define main partition_controls_main
#include "test_shared_spectral_partitions.cpp"
#undef main
#include <map>
#include <set>

using Bridge=spectr::experimental::SharedSpectralBridge;
struct Barrier {
    Bridge::ServicePoint point=Bridge::ServicePoint::InputClaimed;
    std::atomic<bool> armed{false},entered{false},resume{false};
    std::atomic<unsigned> callback_service_calls{0};
    static void observe(void* raw,Bridge::ServicePoint p) noexcept {
        auto& self=*static_cast<Barrier*>(raw);
        if(guard_allocations)++self.callback_service_calls;
        if(p==self.point && self.armed.exchange(false)){
            self.entered=true;
            while(!self.resume.load())std::this_thread::yield();
        }
    }
};
bool wait_for(const std::atomic<bool>& ready){
    const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!ready.load()){if(std::chrono::steady_clock::now()>stop)return false;std::this_thread::yield();}
    return true;
}
Adapter::Config reset_config(){
    Adapter::Config c;
    c.renderer={.design_grid_size=256,.analysis_hop=64,.channels=2,.max_block=128,
                .sample_rate=48000,.initial_mix=1,.mix_ramp_samples=0};
    c.immutable_layout.transition_frames=0;c.immutable_layout.active_bands=2;
    c.immutable_layout.edge_policy=pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    c.immutable_layout.bands[0].gain_db=-6;c.immutable_layout.bands[1].gain_db=-12;
    c.internal_quantum=32;c.additional_latency_samples=160;c.max_callback_frames=128;
    return c;
}
bool guarded_reset(Adapter& a){
    guard_allocations=true;const bool ok=a.reset_realtime();guard_allocations=false;
    return ok && !callback_allocations;
}
int reset_history(bool overflow,bool storms){
    auto a=std::make_unique<Adapter>();const auto c=reset_config();if(!a->prepare(c))return 30;
    Barrier observer;a->set_service_observer(Barrier::observe,&observer);
    std::vector<float> inbuf(2*128,.3f),outbuf(2*128);
    const float* in[]={inbuf.data(),inbuf.data()+128};float* out[]={outbuf.data(),outbuf.data()+128};
    // Nonzero pre-reset history, including a partly assembled quantum.
    if(!a->process(in,out,127))return 31;
    a->service();const auto old=a->epoch();callback_allocations=0;
    if(!guarded_reset(*a)||a->epoch()==old||a->latency_samples()!=480||
       a->serviced_quantums()!=0||a->completed_hops()!=0)return 32;
    if(storms){for(unsigned i=0;i<7;++i){if(!a->process(in,out,31)||!guarded_reset(*a))return 33;}}
    const auto current=a->epoch();
    auto oracle=spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    if(!oracle->prepare(c.renderer)||!oracle->publish_layout(c.immutable_layout))return 34;
    constexpr unsigned frames=4096;
    std::vector<float> input(2*frames),actual(2*frames),ref(2*frames);
    unsigned seed=792;
    for(auto& v:input){seed=seed*1664525u+1013904223u;v=float(seed>>8)/16777216.f-.5f;}
    for(unsigned ch=0;ch<2;++ch)std::fill(input.begin()+ch*frames+2048,input.begin()+(ch+1)*frames,0.f);
    for(unsigned pos=0;pos<frames;pos+=128){
        const float* src[]={input.data()+pos,input.data()+frames+pos};
        float* dst[]={ref.data()+pos,ref.data()+frames+pos};
        if(!oracle->process(src,dst,128))return 35;
    }
    unsigned pos=0,part=0;const unsigned sizes[]={1,31,127,64,63,128};
    while(pos<frames){
        const unsigned n=std::min(sizes[part++%6],frames-pos);
        const float* src[]={input.data()+pos,input.data()+frames+pos};
        float* dst[]={actual.data()+pos,actual.data()+frames+pos};
        guard_allocations=true;const bool ok=a->process(src,dst,n);guard_allocations=false;
        if(!ok||callback_allocations)return 36;
        pos+=n;
        // Capture a causal prefix before the worker rebuilds. Overflow variant
        // intentionally exceeds all64 ingress slots and must never rejoin GPU.
        if(pos>=(overflow?2304u:512u)){
            const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            do{
                a->service();
                if(a->fenced() || (a->serviced_quantums()>=a->quantum_count() &&
                   a->completed_hops()>=a->quantum_count()/2))break;
                if(std::chrono::steady_clock::now()>stop)return 37;
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }while(true);
        }
    }
    double error=0;
    for(unsigned ch=0;ch<2;++ch)for(unsigned i=0;i<frames;++i){
        const double expected=i<160?0:ref[ch*frames+i-160];
        const double residual=std::abs(double(actual[ch*frames+i])-expected);
        if(!std::isfinite(residual))return 38;error=std::max(error,residual);
    }
    if(error>1e-4||a->fenced()!=overflow||observer.callback_service_calls){
        std::cerr<<"reset_parity_error="<<error<<" fenced="<<a->fenced()<<'\n';return 39;
    }
    if(!a->release())return 40;
    unsigned current_gpu=0,current_fallback=0,old_cancelled=0,current_records=0;
    std::set<std::pair<std::uint64_t,std::uint64_t>> unique;
    Bridge::Terminal t;
    while(a->pop_terminal(t)){
        if(!unique.insert({t.stream_epoch,t.block_sequence}).second)return 41;
        if(t.stream_epoch==old&&t.disposition==Outcome::Cancelled)++old_cancelled;
        if(t.stream_epoch==current){
            if(t.block_sequence!=current_records++)return 42;
            current_gpu+=t.disposition==Outcome::GpuDelivered;
            current_fallback+=t.disposition==Outcome::CpuFallback;
        }
    }
    if(old_cancelled!=3||current_records!=128||a->lost_trace_records()||!current_fallback||
       (overflow?current_gpu!=0:current_gpu==0))return 43;
    std::cout<<"reset_history overflow="<<overflow<<" storms="<<storms<<" max_error="<<error
        <<" gpu="<<current_gpu<<" fallback="<<current_fallback<<" callback_allocations="<<callback_allocations<<'\n';
    return 0;
}
int reset_barrier(Bridge::ServicePoint point){
    auto a=std::make_unique<Adapter>();if(!a->prepare(reset_config()))return 50;
    Barrier b;b.point=point;a->set_service_observer(Barrier::observe,&b);
    std::vector<float> input(256,.4f),output(256);
    const float* in[]={input.data(),input.data()+128};float* out[]={output.data(),output.data()+128};
    const bool output_race=point==Bridge::ServicePoint::BeforeOutputPublish;
    const unsigned old_blocks=output_race?4:1;
    for(unsigned i=0;i<old_blocks;++i)if(!a->process(in,out,128))return 51;
    if(point==Bridge::ServicePoint::BeforeRelease||point==Bridge::ServicePoint::BeforePrepare)
        if(!a->reset_realtime())return 52;
    b.armed=true;
    std::jthread worker([&]{
        const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!b.entered&&std::chrono::steady_clock::now()<stop){a->service();std::this_thread::yield();}
    });
    const bool reached=wait_for(b.entered);
    if(!reached){b.resume=true;worker.join();return 53;}
    callback_allocations=0;
    const auto old=a->epoch();
    const bool reset_ok=guarded_reset(*a);
    if(output_race)std::fill(input.begin(),input.end(),0.f);
    // InputClaimed deliberately holds slot0 Busy. New-epoch publication must
    // fence rather than overwrite it or silently skip new history.
    const bool process_ok=a->process(in,out,32);
    b.resume=true;worker.join();
    if(!reset_ok||!process_ok||a->epoch()==old||b.callback_service_calls)return 54;
    const bool expect_fence=point==Bridge::ServicePoint::InputClaimed;
    if(a->fenced()!=expect_fence)return 55;
    // Old-output publication may have raced reset. All current startup output
    // must still be zero; the old .4-valued epoch cannot be accepted.
    for(unsigned i=0;i<32;++i)if(out[0][i]!=0||out[1][i]!=0)return 56;
    if(output_race){
        // Keep the worker stopped after it publishes the obsolete nonzero
        // result. Advance far enough to reuse its exact sequence/slot index.
        // Without both logical-epoch guards this delivers old sound into a
        // freshly reset stream containing only silence.
        for(unsigned q=0;q<17;++q){
            if(!a->process(in,out,32))return 60;
            for(unsigned i=0;i<32;++i)if(out[0][i]!=0||out[1][i]!=0)return 61;
        }
    }
    for(unsigned i=0;i<8;++i)a->service();
    if(point==Bridge::ServicePoint::BeforeInputClaim && a->serviced_quantums()!=1)return 63;
    const bool recover_race=point==Bridge::ServicePoint::BeforeInputClaim;
    if(recover_race){
        auto oracle=spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
        const auto config=reset_config();
        if(!oracle->prepare(config.renderer)||!oracle->publish_layout(config.immutable_layout))return 64;
        std::vector<float> ref(256),history(2*64*32);
        float* refp[]={ref.data(),ref.data()+128};
        if(!oracle->process(in,refp,32))return 65; // already processed current quantum0
        for(unsigned ch=0;ch<2;++ch)std::copy_n(refp[ch],32,history.data()+ch*64*32);
        for(unsigned q=1;q<64;++q){
            if(!oracle->process(in,refp,32)||!a->process(in,out,32))return 66;
            for(unsigned ch=0;ch<2;++ch){
                std::copy_n(refp[ch],32,history.data()+ch*64*32+q*32);
                for(unsigned i=0;i<32;++i){
                    const unsigned sample=q*32+i;
                    const float expected=sample<160?0:history[ch*64*32+sample-160];
                    if(!std::isfinite(out[ch][i])||std::abs(out[ch][i]-expected)>1e-4)return 67;
                }
            }
            const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            do{
                a->service();
                if(a->serviced_quantums()>=q+1&&a->completed_hops()>=(q+1)/2)break;
                if(a->fenced()||std::chrono::steady_clock::now()>stop)return 68;
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }while(true);
        }
    }
    const auto current_epoch=a->epoch();
    if(!a->release())return 57;
    std::set<std::pair<std::uint64_t,std::uint64_t>> ids;Bridge::Terminal t;unsigned recovered_gpu=0;
    while(a->pop_terminal(t)){
        if(!ids.insert({t.stream_epoch,t.block_sequence}).second)return 58;
        if(t.stream_epoch==current_epoch && t.disposition==Outcome::GpuDelivered){
            if(output_race)return 62;
            ++recovered_gpu;
        }
    }
    if(ids.size()!=(output_race?34u:recover_race?68u:5u)||a->lost_trace_records()||
       (recover_race&&!recovered_gpu))return 59;
    std::cout<<"reset_barrier="<<int(point)<<" fenced="<<a->fenced()<<" terminal_count="<<ids.size()<<'\n';
    return 0;
}
int main(){
    for(bool overflow:{false,true})for(bool storms:{false,true}){
        int rc=reset_history(overflow,storms);if(rc){std::cerr<<"reset_history_failure="<<rc<<'\n';return rc;}
    }
    for(auto point:{Bridge::ServicePoint::BeforeInputClaim,Bridge::ServicePoint::InputClaimed,Bridge::ServicePoint::BeforeSubmit,
                    Bridge::ServicePoint::BeforeOutputPublish,Bridge::ServicePoint::BeforeRelease,
                    Bridge::ServicePoint::BeforePrepare}){
        int rc=reset_barrier(point);if(rc){std::cerr<<"reset_barrier_failure="<<rc<<" point="<<int(point)<<'\n';return rc;}
    }
    return partition_controls_main();
}
