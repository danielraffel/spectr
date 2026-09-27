#define main partition_controls_main
#include "test_shared_spectral_partitions.cpp"
#undef main
#include <spectr/experimental/shared_spectral_renderer.hpp>

int renderer_case(int grid,bool cpu_only,bool burst=false){
    using Renderer=spectr::experimental::SharedSpectralMaskRenderer;
    spectr::MaskRendererConfig config{.design_grid_size=grid,.analysis_hop=grid/4,
        .channels=2,.max_block=512,.sample_rate=48000,.initial_mix=.35f,.mix_ramp_samples=64};
    auto renderer=std::make_unique<Renderer>(cpu_only);
    auto oracle=spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    if(!renderer->prepare(config)||!oracle->prepare(config))return 90;
    spectr::MaskRenderer::Layout layout;layout.active_bands=2;layout.transition_frames=0;
    layout.edge_policy=pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    layout.bands[0].gain_db=-6;layout.bands[1].gain_db=-12;
    if(!renderer->publish_layout(layout)||!oracle->publish_layout(layout))return 91;
    // Spectr::build_renderer_ only prewarms while generation is zero. Initial
    // publication already advanced it, so it performs this immediate reset.
    if(renderer->active_generation()==0)return 103;
    guard_allocations=true;renderer->reset();guard_allocations=false;oracle->reset();
    if(callback_allocations)return 104;
    const auto added=Renderer::additional_latency(config);
    if(renderer->latency_samples()!=oracle->latency_samples()+int(added)||
       renderer->maximum_tail_samples()!=oracle->maximum_tail_samples()+int(added))return 92;
    const unsigned total=grid==256?8192:65536,reset_at=grid==256?4093:40001;
    std::vector<float> input(total*2),actual(total*2),reference(total*2);
    unsigned rng=211;for(auto& x:input){rng=rng*1664525u+1013904223u;x=float(rng>>8)/16777216.f-.5f;}
    unsigned pos=0,part=0;double error=0;callback_allocations=0;
    constexpr unsigned parts[]={31,127,512,128,1};
    while(pos<total){
        if(pos==1111){renderer->set_mix(.8f);oracle->set_mix(.8f);}
        if(pos==1999){layout.transition_frames=4;layout.bands[0].gain_db=-18;
            if(!renderer->set_layout_rt(layout)||!oracle->set_layout_rt(layout))return 93;}
        if(pos==reset_at){guard_allocations=true;renderer->reset();guard_allocations=false;oracle->reset();}
        unsigned n=std::min(grid==256&&!burst?31u:parts[part++%5],total-pos);
        for(auto event:{1111u,1999u,reset_at})if(pos<event)n=std::min(n,event-pos);
        const float* in[]={input.data()+pos,input.data()+total+pos};
        float* out[]={actual.data()+pos,actual.data()+total+pos};
        float* ref[]={reference.data()+pos,reference.data()+total+pos};
        if(!oracle->process(in,ref,n))return 94;
        guard_allocations=true;const bool ok=renderer->process(in,out,n);guard_allocations=false;
        if(!ok||callback_allocations)return 95;
        for(unsigned ch=0;ch<2;++ch)for(unsigned i=pos;i<pos+n;++i){
            const unsigned epoch=i>=reset_at?reset_at:0;
            const double expected=i<epoch+added?0:reference[ch*total+i-added];
            const double residual=std::abs(double(actual[ch*total+i])-expected);
            if(!std::isfinite(residual))return 96;error=std::max(error,residual);
        }
        pos+=n;
        // Allows the independent worker to complete useful GPU work. This is
        // functional pacing, explicitly not a benchmark or host deadline test.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto snapshot=renderer->snapshot();
    std::cout<<"renderer grid="<<grid<<" cpu_only="<<cpu_only<<" extra_latency="<<added
        <<" total_latency="<<renderer->latency_samples()<<" max_error="<<error
        <<" gpu="<<snapshot.gpu_delivered<<" cpu="<<snapshot.cpu_fallback
        <<" state="<<unsigned(snapshot.state)<<" epoch="<<snapshot.epoch<<" callback_fence="<<snapshot.callback_fence_reason<<" worker_fence="<<snapshot.worker_fence_reason<<" burst="<<burst<<" lost="<<snapshot.lost_records
        <<" callback_allocations="<<callback_allocations<<'\n';
    if(snapshot.lost_records || (cpu_only?(!snapshot.cpu_fallback || snapshot.gpu_delivered!=0):(!burst&&snapshot.gpu_delivered==0)))return 97;
    if(!cpu_only&&!burst&&snapshot.state!=Renderer::ProviderState::SharedReady)return 100;
    if(!renderer->release())return 101;
    const auto final=renderer->snapshot();
    const auto expected=reset_at/unsigned(config.analysis_hop/2)+(total-reset_at)/unsigned(config.analysis_hop/2);
    const auto terminals=final.gpu_delivered+final.cpu_fallback+final.cancelled;
    std::cout<<"terminal_total="<<terminals<<" expected="<<expected<<" cancelled="<<final.cancelled<<'\n';
    if(terminals!=expected || final.lost_records)return 102;
    if(cpu_only&&snapshot.state!=Renderer::ProviderState::CpuOnly)return 98;
    return error<1e-4?0:99;
}
int main(){
    for(auto grid:{256,8192})for(bool cpu:{false,true}){
        const auto rc=renderer_case(grid,cpu);
        if(rc){std::cerr<<"renderer_failure="<<rc<<'\n';return rc;}
    }
    return renderer_case(256,false,true);
}
