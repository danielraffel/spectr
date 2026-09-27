#pragma once
#include "shared_host_registry.hpp"
#include <atomic>
#include "shared_host_processing_gate.hpp"

namespace spectr::host_probe {
class Processor;
inline Registry<Processor> registry;
class Processor final : public Spectr {
public:
    Processor():token_(registry.add(this)){}
    ~Processor() override {registry.remove(token_);}
    void prepare(const pulp::format::PrepareContext& ctx) override {
        // CLAP must deactivate after a seal before activating again.
        if(phase_.load()!=0)return;
        Spectr::prepare(ctx);frames_=0;rejected_=0;
        phase_.store(1);
    }
    void release() override {Spectr::release();phase_.store(0);}
    void process(pulp::audio::BufferView<float>& output,
                 const pulp::audio::BufferView<const float>& input,
                 pulp::midi::MidiBuffer& mi,pulp::midi::MidiBuffer& mo,
                 const pulp::format::ProcessContext& ctx) override {
        if(phase_.load(std::memory_order_acquire)!=1){output.clear();++rejected_;return;}
        Spectr::process(output,input,mi,mo,ctx);
        frames_.fetch_add(output.num_samples(),std::memory_order_relaxed);
    }
    bool start_processing_probe() noexcept {return processing_gate_.start();}
    void stop_processing_probe() noexcept {processing_gate_.stopped();}
    int query_v2(SpectrSharedHostRequestV2& r) {
        if(!processing_gate_.begin_query())return 2;
        struct Guard {ProcessingGate& gate;~Guard(){gate.end_query();}} guard{processing_gate_};
        if(phase_.load()!=1)return 2;
        const int result=query(r.snapshot);
        if(result||!r.snapshot.shared_renderer)return result?result:2;
        r.renderer_run_id=shared_product_trace_run_id_stopped();
        return r.renderer_run_id?0:2;
    }
    int query(SpectrSharedHostRequest& r) {
        const auto phase=phase_.load();
        if(r.command==SpectrSharedHostRequest::Configure){
            if(phase!=0)return 2;
            if(!set_shared_product_force_cpu(r.force_cpu!=0) ||
               !set_render_mode(MaskRenderMode::linear_phase))return 2;
        }
        const int latency=latency_samples(),tail=descriptor().tail_samples;
        SharedProductSnapshot snapshot;
        bool confirmed=false;
        if(r.command==SpectrSharedHostRequest::Finalize){
            if(phase!=1)return 2;
            confirmed=finalize_shared_product_snapshot(snapshot);
            phase_.store(confirmed?2u:3u,std::memory_order_release);
        }else snapshot=shared_product_snapshot();
        r.phase=phase_.load();r.shared_renderer=snapshot.shared_renderer;
        r.release_confirmed=confirmed;r.latency_samples=latency;r.tail_samples=tail;
        r.epoch=snapshot.epoch;r.gpu_selected=snapshot.gpu_selected;r.cpu_selected=snapshot.cpu_selected;
        r.cancelled=snapshot.cancelled;r.lost_records=snapshot.lost_records;
        r.processed_frames=frames_.load();r.rejected_process_calls=rejected_.load();
        return r.command==SpectrSharedHostRequest::Finalize && !confirmed?5:0;
    }
private:
    ProcessingGate processing_gate_;
    std::uint64_t token_;
    std::atomic<unsigned> phase_{0};
    std::atomic<std::uint64_t> frames_{0},rejected_{0};
};
inline std::unique_ptr<pulp::format::Processor> create(){return std::make_unique<Processor>();}
}
extern "C" __attribute__((visibility("default")))
int spectr_shared_host_probe_v1(SpectrSharedHostRequest* request){
    return spectr::host_probe::registry.query(request);
}

extern "C" __attribute__((visibility("default")))
int spectr_shared_host_probe_v2(SpectrSharedHostRequestV2* request){
    return spectr::host_probe::registry.query_v2(request);
}
