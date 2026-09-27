#define main partition_controls_main
#include "test_shared_spectral_partitions.cpp"
#undef main
#include <spectr/experimental/shared_spectral_renderer.hpp>
#include <stdexcept>
#include <limits>
#define REQUIRE_TRACE(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
namespace spectr::experimental {
struct SharedSpectralBridgeTestAccess {
    static void near_overflow(SharedSpectralBridge& b) {
        b.lifetime_input_quanta_=std::numeric_limits<std::uint64_t>::max();
        b.trace_write_=b.trace_read_=b.trace_lost_=std::numeric_limits<std::uint64_t>::max();
    }
};
}
using namespace spectr::experimental;
using TraceOutcome=pulp::gpu_audio::GpuAudioTerminalDisposition;
struct CapturedTrace {
    std::array<SharedSpectralTraceFinal,8> finals{};
    std::array<std::pair<std::uint64_t,SharedSpectralBridge::Terminal>,1024> records{};
    unsigned final_count=0,record_count=0;bool overflow=false;
    SharedSpectralMaskRenderer::TraceObserver observer(){return {this,
        [](void* p,std::uint64_t run,const SharedSpectralBridge::Terminal& t) noexcept {
            auto& c=*static_cast<CapturedTrace*>(p);
            if(c.record_count==c.records.size()){c.overflow=true;return;}
            c.records[c.record_count++]={run,t};
        },
        [](void* p,const SharedSpectralTraceFinal& f) noexcept {
            auto& c=*static_cast<CapturedTrace*>(p);
            if(c.final_count==c.finals.size()){c.overflow=true;return;}
            c.finals[c.final_count++]=f;
        }};}
};
spectr::MaskRendererConfig trace_config(){return {.design_grid_size=256,.analysis_hop=64,
    .channels=2,.max_block=128,.sample_rate=48000,.initial_mix=1,.mix_ramp_samples=0};}
void bridge_controls(){
    for(unsigned blocks:{8u,4100u}){
        auto b=std::make_unique<SharedSpectralBridge>();
        SharedSpectralBridge::Config c{trace_config(),32,4,{},true,true};
        REQUIRE_TRACE(b->prepare(c));
        std::array<float,64> in{},out{};
        const float* src[]={in.data(),in.data()+32};float* dst[]={out.data(),out.data()+32};
        for(unsigned i=0;i<blocks;++i){guard_allocations=true;auto ok=b->process(src,dst);guard_allocations=false;REQUIRE_TRACE(ok);}
        REQUIRE_TRACE(callback_allocations==0);
        REQUIRE_TRACE(b->release());
        auto a=b->stopped_accounting();
        REQUIRE_TRACE(a.input_quanta==blocks);REQUIRE_TRACE(a.ingress_admitted_quanta==0);
        REQUIRE_TRACE(a.terminal_attempts==blocks);REQUIRE_TRACE(!a.counter_overflow);
        REQUIRE_TRACE(a.terminal_enqueued==std::min(blocks,4096u));
        REQUIRE_TRACE(a.lost_records==(blocks>4096?blocks-4096:0));
        SharedSpectralBridge::Terminal t;unsigned popped=0,cancelled=0;
        while(b->pop_terminal(t)){REQUIRE_TRACE(t.block_sequence==popped++);cancelled+=t.disposition==TraceOutcome::Cancelled;}
        a=b->stopped_accounting();REQUIRE_TRACE(a.terminal_popped==popped);
        if(blocks==8)REQUIRE_TRACE(cancelled==4);
        REQUIRE_TRACE(b->release());REQUIRE_TRACE(b->stopped_accounting().terminal_attempts==blocks);
    }
    auto b=std::make_unique<SharedSpectralBridge>();
    REQUIRE_TRACE(b->prepare({trace_config(),32,4,{},true,true}));
    SharedSpectralBridgeTestAccess::near_overflow(*b);
    std::array<float,64> input{},output{};
    const float* src[]={input.data(),input.data()+32};float* dst[]={output.data(),output.data()+32};
    REQUIRE_TRACE(b->process(src,dst));REQUIRE_TRACE(b->release());
    REQUIRE_TRACE(b->stopped_accounting().counter_overflow);
    REQUIRE_TRACE(b->stopped_accounting().input_quanta==std::numeric_limits<std::uint64_t>::max());
}
void renderer_controls(){
    CapturedTrace capture;
    std::uint64_t first_run=0;
    {
        SharedSpectralMaskRenderer renderer(true);renderer.set_trace_observer(capture.observer());
        for(unsigned run=0;run<2;++run){
            REQUIRE_TRACE(renderer.prepare(trace_config()));
            std::array<float,256> in{},out{};
            const float* src[]={in.data(),in.data()+128};float* dst[]={out.data(),out.data()+128};
            // Partial input does not become an admitted fixed quantum.
            REQUIRE_TRACE(renderer.process(src,dst,31));renderer.reset();renderer.reset();
            for(unsigned i=0;i<8;++i){guard_allocations=true;auto ok=renderer.process(src,dst,32);guard_allocations=false;REQUIRE_TRACE(ok);}
            renderer.reset();REQUIRE_TRACE(renderer.process(src,dst,32));
            REQUIRE_TRACE(renderer.process(src,dst,3));
            REQUIRE_TRACE(renderer.release());REQUIRE_TRACE(renderer.release());
            REQUIRE_TRACE(capture.final_count==run+1);
            const auto& f=capture.finals[run];
            REQUIRE_TRACE(f.accounting.input_quanta==9);REQUIRE_TRACE(f.accounting.terminal_attempts==9);
            REQUIRE_TRACE(f.accounting.terminal_enqueued==9);REQUIRE_TRACE(f.accounting.terminal_popped==9);
            std::uint64_t observed_admissions=0;
            for(unsigned i=0;i<capture.record_count;++i)
                if(capture.records[i].first==f.renderer_run_id)
                    observed_admissions+=capture.records[i].second.ingress_admitted;
            REQUIRE_TRACE(f.accounting.ingress_admitted_quanta==observed_admissions);
            REQUIRE_TRACE(f.accounting.lost_records==0);
            REQUIRE_TRACE(!f.accounting.counter_overflow);REQUIRE_TRACE(f.gpu_delivered==0);
            REQUIRE_TRACE(f.cpu_fallback+f.cancelled==9);REQUIRE_TRACE(f.physical_release_confirmed);
            REQUIRE_TRACE(f.quantum_frames==32&&f.lead_quanta==4);
            REQUIRE_TRACE(f.last_epoch==f.first_epoch+3);
            REQUIRE_TRACE(f.renderer_run_id==f.first_epoch);
            if(!run)first_run=f.renderer_run_id;else REQUIRE_TRACE(first_run!=f.renderer_run_id);
        }
    }
    REQUIRE_TRACE(capture.final_count==2);REQUIRE_TRACE(capture.record_count==18);REQUIRE_TRACE(!capture.overflow);
    REQUIRE_TRACE(callback_allocations==0);
    // A second renderer receives a separately reserved run identity.
    {
        SharedSpectralMaskRenderer second(true);second.set_trace_observer(capture.observer());
        REQUIRE_TRACE(second.prepare(trace_config()));
    }
    REQUIRE_TRACE(capture.final_count==3);REQUIRE_TRACE(capture.finals[2].renderer_run_id!=first_run);
    REQUIRE_TRACE(capture.finals[2].accounting.input_quanta==0);
    for(unsigned i=0;i<18;++i){
        const auto expected=capture.finals[i/9].renderer_run_id;
        REQUIRE_TRACE(capture.records[i].first==expected);
    }
}
void final_state_controls(){
    SharedSpectralTraceRun run;SharedSpectralStoppedAccounting counts{1,1,1,1,1,0,false};
    run.begin({},1,32,4);
    REQUIRE_TRACE(!run.finish(false,counts,1,1,0,0));
    REQUIRE_TRACE(!run.finish(false,counts,1,1,0,0));
    auto final=run.finish(true,counts,1,1,0,0);REQUIRE_TRACE(final&&final->accounting.input_quanta==1);
    REQUIRE_TRACE(!run.finish(true,counts,1,1,0,0));
    run.begin(counts,2,32,4);
    counts.input_quanta=0;
    REQUIRE_TRACE(run.finish(true,counts,2,0,0,0)->accounting.counter_overflow);
    run.begin({},3,32,4);counts.counter_overflow=true;
    REQUIRE_TRACE(run.finish(true,counts,3,0,0,0)->accounting.counter_overflow);
}
int main(){try{bridge_controls();renderer_controls();final_state_controls();
    std::cout<<"Spectr stopped trace accounting controls passed; CPU-only; no physical GPU release-failure claim\n";
    return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
