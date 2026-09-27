#include <spectr/experimental/shared_spectral_bridge.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

using spectr::experimental::SharedSpectralBridge;
using Disposition=pulp::gpu_audio::GpuAudioTerminalDisposition;

template<class F> bool wait_until(F&& condition) {
    const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!condition()) {if(std::chrono::steady_clock::now()>stop)return false;std::this_thread::sleep_for(std::chrono::microseconds(50));}
    return true;
}

int run(unsigned lead,bool force_overflow) {
    SharedSpectralBridge::Config config;
    config.renderer={.design_grid_size=1024,.analysis_hop=256,.channels=2,.max_block=64,
                     .sample_rate=48000,.initial_mix=1,.mix_ramp_samples=0};
    config.host_block=64;config.lead_host_blocks=lead;
    config.immutable_layout.active_bands=2;
    config.immutable_layout.transition_frames=0;
    config.immutable_layout.bands[0].gain_db=-6;
    config.immutable_layout.bands[1].gain_db=-12;
    auto fixture=std::make_unique<SharedSpectralBridge>();
    if(!fixture->prepare(config))return 1;
    if(fixture->latency_samples()!=1024+256+lead*64 || !fixture->publish_layout(config.immutable_layout) || fixture->set_mix(.5))return 2;
    const auto old_epoch=fixture->epoch();
    auto oracle=spectr::make_mask_renderer(spectr::MaskRenderMode::linear_phase);
    if(!oracle->prepare(config.renderer)||!oracle->publish_layout(config.immutable_layout))return 3;
    constexpr unsigned blocks=160,b=64,c=2;
    std::vector<float> input(c*b),output(c*b),reference(c*b),history((blocks+8)*c*b);
    const float* in[]={input.data(),input.data()+b};
    float* out[]={output.data(),output.data()+b};
    float* ref[]={reference.data(),reference.data()+b};
    std::atomic<bool> pause{force_overflow},paused{false};
    std::jthread worker([&](std::stop_token stop){
        while(!stop.stop_requested()){
            if(pause.load()){paused=true;std::this_thread::yield();continue;}
            paused=false;fixture->service();std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    });
    unsigned seed=8493;
    double error=0;
    for(unsigned q=0;q<blocks+lead;++q){
        if(!force_overflow && q==24){pause=true;if(!wait_until([&]{return paused.load();}))return 4;}
        if(!force_overflow && q==40)pause=false;
        for(auto& v:input){seed=seed*1664525u+1013904223u;v=q<112?float(seed>>8)/16777216.f-.5f:0.f;}
        if(!oracle->process(in,ref,b))return 5;
        std::copy(reference.begin(),reference.end(),history.begin()+q*c*b);
        if(!fixture->process(in,out,q<blocks))return 6;
        for(unsigned i=0;i<c*b;++i){
            const float expected=q<lead?0:history[(q-lead)*c*b+i];
            if(!std::isfinite(output[i])||!std::isfinite(expected))return 7;
            const double residual=std::abs(double(output[i])-expected);
            if(!std::isfinite(residual))return 8;
            error=std::max(error,residual);
        }
        // Test-driver wait only; never inside the callback. This establishes
        // correctness with a serviced worker, not realtime scheduling evidence.
        if(q<blocks && !pause && !fixture->fenced()) {
            if(!wait_until([&]{return fixture->serviced_blocks()>=q+1;}))return 9;
            if((q+1)%4==0 && !wait_until([&]{return fixture->completed_hops()>=(q+1)/4;}))return 10;
        }
    }
    worker.request_stop();worker.join();
    unsigned terminal=0,delivered=0,fallback=0,admitted=0;
    SharedSpectralBridge::Terminal record;
    while(fixture->pop_terminal(record)){
        if(record.stream_epoch!=old_epoch||record.block_sequence!=terminal)return 11;
        ++terminal;admitted+=record.ingress_admitted;
        if(record.disposition==Disposition::GpuDelivered)++delivered;
        else if(record.disposition==Disposition::CpuFallback)++fallback;
        else return 12;
        std::cout<<"{\"event\":\"gpu_audio_terminal\",\"stream_epoch\":"<<record.stream_epoch
                 <<",\"block_sequence\":"<<record.block_sequence<<",\"ingress_admitted\":"<<record.ingress_admitted
                 <<",\"terminal_disposition\":\""<<(record.disposition==Disposition::GpuDelivered?"gpu_delivered":"cpu_fallback")<<"\"}\n";
    }
    if(terminal!=blocks||fixture->lost_trace_records()||!fallback||(!force_overflow&&!delivered)||error>1e-4)return 13;
    if(force_overflow && (!fixture->fenced()||delivered))return 14;
    const auto report=fixture->diagnostics();
    if(!report.authenticated_shared_metal||report.runtime_write_buffer_calls||report.runtime_copy_buffer_calls||report.runtime_map_async_calls)return 15;
    if(!fixture->reset()||fixture->epoch()==old_epoch||fixture->fenced())return 16;
    std::fill(input.begin(),input.end(),0);
    if(!fixture->process(in,out))return 17;
    for(float v:output)if(v!=0)return 18;
    if(!fixture->release())return 19;
    std::cerr<<"lead_host_blocks="<<lead<<" host_block=64 hop=256 overflow="<<force_overflow
             <<" max_error="<<error<<" gpu="<<delivered<<" fallback="<<fallback<<" admitted="<<admitted<<'\n';
    return 0;
}
int abrupt(bool reset, bool overflow_trace) {
    auto bridge=std::make_unique<SharedSpectralBridge>();
    SharedSpectralBridge::Config config;
    config.renderer={.design_grid_size=1024,.analysis_hop=256,.channels=2,.max_block=64,
                     .sample_rate=48000,.initial_mix=1,.mix_ramp_samples=0};
    config.host_block=64;config.lead_host_blocks=4;config.immutable_layout.transition_frames=0;
    if(!bridge->prepare(config))return 20;
    const auto old_epoch=bridge->epoch();
    std::vector<float> input(128,0),output(128,0);
    const float* in[]={input.data(),input.data()+64};
    float* out[]={output.data(),output.data()+64};
    const unsigned count=overflow_trace?4100:8;
    // No worker: accepted ingress is outstanding when the quiescent owner stops.
    for(unsigned q=0;q<count;++q)if(!bridge->process(in,out))return 21;
    if(reset?!bridge->reset():!bridge->release())return 22;
    if(reset&&bridge->epoch()==old_epoch)return 23;
    unsigned records=0,cancelled=0;
    SharedSpectralBridge::Terminal t;
    while(bridge->pop_terminal(t)){
        if(t.stream_epoch!=old_epoch||t.block_sequence!=records)return 24;
        if(t.disposition==Disposition::Cancelled)++cancelled;
        else if(t.disposition!=Disposition::CpuFallback)return 25;
        if(!overflow_trace&&!t.ingress_admitted)return 26;
        ++records;
    }
    if(overflow_trace){
        if(records!=4096||bridge->lost_trace_records()!=4)return 27;
    }else if(records!=count||cancelled!=4||bridge->lost_trace_records())return 28;
    if(!bridge->release()||bridge->pop_terminal(t))return 29;
    std::cerr<<"abrupt_reset="<<reset<<" trace_overflow="<<overflow_trace
             <<" records="<<records<<" cancelled="<<cancelled
             <<" lost="<<bridge->lost_trace_records()<<'\n';
    return 0;
}
int main(){for(bool reset:{false,true}){int rc=abrupt(reset,false);if(rc)return rc;}
    {int rc=abrupt(false,true);if(rc)return rc;}
for(unsigned lead:{1u,2u,4u,8u}){int rc=run(lead,false);if(rc)return rc;}return run(4,true);}
