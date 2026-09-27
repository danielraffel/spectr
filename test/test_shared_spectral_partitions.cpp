#include <spectr/experimental/shared_spectral_partition_adapter.hpp>
#include <pulp/audio/analysis/latency_evidence.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

namespace {
thread_local bool guard_allocations=false;
thread_local unsigned callback_allocations=0;
void* allocate(std::size_t bytes,std::size_t alignment=0){
    if(guard_allocations)++callback_allocations;
    void* p=nullptr;
    if(alignment){if(posix_memalign(&p,alignment,bytes?bytes:1))throw std::bad_alloc();}
    else if(!(p=std::malloc(bytes?bytes:1)))throw std::bad_alloc();
    return p;
}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){return allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return allocate(n,std::size_t(a));}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}

using Adapter=spectr::experimental::SharedSpectralPartitionAdapter;
using Outcome=pulp::gpu_audio::GpuAudioTerminalDisposition;
int run(const std::vector<unsigned>& partitions,bool identity,bool no_service,bool inplace){
    constexpr unsigned frames=4096,quantum=32,additional=160,total_latency=480;
    Adapter::Config config;
    config.renderer={.design_grid_size=256,.analysis_hop=64,.channels=2,.max_block=128,
                     .sample_rate=48000,.initial_mix=1,.mix_ramp_samples=0};
    config.immutable_layout.transition_frames=0;
    config.immutable_layout.active_bands=identity?1:2;
    config.immutable_layout.edge_policy=pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    config.immutable_layout.bands[0].gain_db=identity?0:-6;
    config.immutable_layout.bands[1].gain_db=-12;
    config.internal_quantum=quantum;config.additional_latency_samples=additional;
    config.max_callback_frames=128;
    auto adapter=std::make_unique<Adapter>();
    if(!adapter->prepare(config)||adapter->latency_samples()!=total_latency)return 1;
    auto oracle=spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    if(!oracle->prepare(config.renderer)||!oracle->publish_layout(config.immutable_layout))return 2;
    pulp::audio::Buffer<float> input(2,frames),output(2,frames),reference(2,frames);
    unsigned seed=42;
    for(unsigned ch=0;ch<2;++ch)for(unsigned i=0;i<frames;++i){
        seed=seed*1664525u+1013904223u;
        input.channel(ch)[i]=identity?(i==23?.5f:0.f):(i<2048?float(seed>>8)/16777216.f-.5f:0.f);
    }
    for(unsigned pos=0;pos<frames;pos+=128){
        const float* in[]={input.channel(0).data()+pos,input.channel(1).data()+pos};
        float* out[]={reference.channel(0).data()+pos,reference.channel(1).data()+pos};
        if(!oracle->process(in,out,128))return 3;
    }
    if(inplace)for(unsigned ch=0;ch<2;++ch)std::copy(input.channel(ch).begin(),input.channel(ch).end(),output.channel(ch).begin());
    unsigned pos=0,part=0;
    callback_allocations=0;
    while(pos<frames){
        const unsigned n=std::min(partitions[part++%partitions.size()],frames-pos);
        const float* in[]={input.channel(0).data()+pos,input.channel(1).data()+pos};
        float* out[]={output.channel(0).data()+pos,output.channel(1).data()+pos};
        if(inplace){in[0]=out[0];in[1]=out[1];}
        guard_allocations=true;
        const bool ok=adapter->process(in,out,n);
        guard_allocations=false;
        if(!ok||callback_allocations||adapter->latency_samples()!=total_latency)return 4;
        pos+=n;
        // Driver servicing establishes correctness, not real-time performance.
        // Pause for a fixed sample interval, independent of host partitioning.
        if(!no_service && (pos<1024||pos>=1536) && !adapter->fenced()){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            do{
                adapter->service();
                if(adapter->serviced_quantums()>=adapter->quantum_count() &&
                   adapter->completed_hops()>=adapter->quantum_count()*quantum/64)break;
                if(std::chrono::steady_clock::now()>deadline)return 5;
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }while(true);
        }
    }
    double error=0;
    for(unsigned ch=0;ch<2;++ch)for(unsigned i=0;i<frames;++i){
        const float expected=i<additional?0:reference.channel(ch)[i-additional];
        const double residual=std::abs(double(output.channel(ch)[i])-expected);
        if(!std::isfinite(residual))return 6;
        error=std::max(error,residual);
    }
    if(error>1e-4)return 7;
    if(identity){
        const auto good=pulp::test::audio::measure_marker_offset(input,output,total_latency,{.input_marker_frame=23});
        const auto wrong=pulp::test::audio::measure_marker_offset(input,output,total_latency+1,{.input_marker_frame=23});
        if(good.contract_outcome!=pulp::test::audio::LatencyContractOutcome::satisfied ||
           good.measured_samples!=total_latency ||
           wrong.contract_outcome!=pulp::test::audio::LatencyContractOutcome::violated)return 8;
    }
    if(!adapter->release())return 9;
    unsigned gpu=0,cpu=0,cancelled=0,records=0;
    spectr::experimental::SharedSpectralBridge::Terminal t;
    while(adapter->pop_terminal(t)){
        if(t.block_sequence!=records++)return 10;
        if(t.disposition==Outcome::GpuDelivered)++gpu;
        else if(t.disposition==Outcome::CpuFallback)++cpu;
        else if(t.disposition==Outcome::Cancelled)++cancelled;
        else return 11;
    }
    if(records!=frames/quantum||adapter->lost_trace_records()||!cpu||(!no_service&&!gpu)||cancelled!=4)return 12;
    if(no_service&&gpu)return 13;
    std::cout<<"partition_first="<<partitions[0]<<" irregular="<<(partitions.size()>1)
        <<" identity="<<identity<<" no_service="<<no_service<<" inplace="<<inplace
        <<" latency_samples="<<total_latency<<" max_error="<<error
        <<" callback_allocations="<<callback_allocations<<" bridge_gpu="<<gpu
        <<" bridge_fallback="<<cpu<<" cancelled="<<cancelled<<'\n';
    return 0;
}
int main(){
    for(unsigned n:{1u,31u,32u,63u,64u,127u,128u})
        for(bool identity:{false,true}){const int rc=run({n},identity,false,false);if(rc){std::cerr<<"failure="<<rc<<" partition="<<n<<'\n';return rc;}}
    for(bool inplace:{false,true})for(bool no_service:{false,true}){
        const int rc=run({1,31,128,63,32,127,64,7},false,no_service,inplace);
        if(rc){std::cerr<<"irregular_failure="<<rc<<'\n';return rc;}
    }
    return 0;
}
