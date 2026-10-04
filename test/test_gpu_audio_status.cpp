#include <spectr/spectr.hpp>
#include <spectr/editor_bridge.hpp>
#include <spectr/detail/gpu_audio_status_projection.hpp>
#include <pulp/state/store.hpp>
#include <choc/text/choc_JSON.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
void projection_controls() {
    using S=spectr::GpuAudioStatus;
    for(const auto availability:{S::Availability::NotBuilt,S::Availability::NotPrepared,
                                 S::Availability::NonSharedRenderer,S::Availability::Available}) {
        const S s{availability,{}};
        const auto p=spectr::detail::gpu_audio_status_projection(s);
        require(!p["available"].getBool(),"absent snapshot claimed available");
        require(!p.hasObjectMember("gpu_selected"),"unavailable snapshot invented GPU zero");
        require(!p.hasObjectMember("current_epoch"),"unavailable snapshot invented epoch");
        require(spectr::detail::gpu_audio_status_copy_text(s).find("unavailable")!=std::string::npos,
                "copy text hid unavailable status");
    }
    S s{S::Availability::Available,S::Delivery{1,std::numeric_limits<std::uint64_t>::max(),7,3,2,1}};
    const auto p=spectr::detail::gpu_audio_status_projection(s);
    require(p["available"].getBool(),"real snapshot hidden");
    require(p["current_epoch"].get<std::string>()=="18446744073709551615","epoch lost integer precision");
    require(p["gpu_selected"].get<std::string>()=="7","GPU selection mislabeled");
    require(p["cpu_fallback"].get<std::string>()=="3","fallback count mislabeled");
    require(p["cancelled"].get<std::string>()=="2","cancel count lost");
    require(p["lost_terminal_records"].get<std::string>()=="1","loss hidden");
    require(p["sampling"].get<std::string>()=="independent_live_counters","coherent snapshot implied");
    const auto text=spectr::detail::gpu_audio_status_copy_text(s);
    require(text.find("GPU selected: 7")!=std::string::npos&&text.find("Lost terminal records: 1")!=std::string::npos,"copy projection differs");
    // A refused freeze source is reported in every availability, never dropped.
    require(p["freeze_available"].getBool()&&!p.hasObjectMember("freeze_note"),"freeze reported refused when wired");
    require(text.find("Freeze unavailable")==std::string::npos,"copy text claims Freeze unavailable");
    for(const auto availability:{S::Availability::NotBuilt,S::Availability::NonSharedRenderer,S::Availability::Available}) {
        S refused=availability==S::Availability::Available?s:S{availability,{}};
        refused.freeze_available=false;
        const auto q=spectr::detail::gpu_audio_status_projection(refused);
        require(!q["freeze_available"].getBool(),"refused freeze source projected as available");
        require(q.hasObjectMember("freeze_note")&&q["freeze_note"].get<std::string>()=="Freeze unavailable in this mode",
                "refused freeze source has no note");
        require(spectr::detail::gpu_audio_status_copy_text(refused).find("Freeze unavailable in this mode")!=std::string::npos,
                "copy text hid a refused freeze source");
    }
    s.delivery->provider_state=999;
    require(spectr::detail::gpu_audio_status_projection(s)["provider_state"].get<std::string>()=="unknown","unknown provider state claimed ready");
    s.delivery->provider_state=1;s.delivery->gpu_selected=0;
    require(spectr::detail::gpu_audio_status_projection(s)["gpu_selected"].get<std::string>()=="0","readiness fabricated GPU use");
}
struct Rig {
    pulp::state::StateStore store;
    spectr::Spectr processor;
    Rig(){processor.set_state_store(&store);processor.define_parameters(store);}
};
void lifecycle_and_bridge_controls() {
    using A=spectr::GpuAudioStatus::Availability;
    Rig first,second;
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    require(first.processor.gpu_audio_status().availability==A::NotPrepared,"unprepared shared instance measured");
#else
    require(first.processor.gpu_audio_status().availability==A::NotBuilt,"non-shared build measured");
#endif
    pulp::view::EditorBridge bridge;
    std::string copied;
    spectr::register_spectr_editor_handlers(bridge,first.processor,first.processor.patterns(),
        first.processor.editor_authority(),[&](std::string_view s){copied=s;return true;});
    const auto response=choc::json::parse(bridge.dispatch_json(R"({"type":"build_info_get","payload":{}})"));
    require(response["ok"].getBool()&&!response["gpu_audio"]["available"].getBool(),"actual bridge fabricated measurements");
    require(!response["gpu_audio"].hasObjectMember("gpu_selected"),"actual bridge invented GPU zero");
    require(response["gpu_audio"]["freeze_available"].getBool(),"build_info hides the freeze availability");
    require(choc::json::parse(bridge.dispatch_json(R"({"type":"build_info_copy","payload":{}})"))["ok"].getBool(),"copy route failed");
    require(copied==response["copy_text"].get<std::string>(),"structured/copy observation diverged");
    std::atomic<bool> done{false},bad{false};
    std::atomic<std::uint64_t> reads{0};
    std::jthread reader([&]{while(!done.load()) {
        const auto status=first.processor.gpu_audio_status();
        if((status.availability==A::Available)!=status.delivery.has_value())bad=true;
        if(second.processor.gpu_audio_status().delivery.has_value())bad=true;
        ++reads;std::this_thread::yield();
    }});
    try {
        for(int iteration=0;iteration<4;++iteration) {
            first.processor.prepare({48000.0,128,2,2});
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
            require(first.processor.gpu_audio_status().availability==A::NonSharedRenderer,"default mode claimed shared renderer");
            // Mixing with GPU processing off is the CPU linear-phase renderer.
            require(first.processor.set_render_mode(spectr::MaskRenderMode::linear_phase),"linear renderer failed to prepare");
            require(first.processor.gpu_audio_status().availability==A::NonSharedRenderer,"GPU processing off still built the shared renderer");
            require(first.processor.set_gpu_processing(true),"GPU processing failed to prepare");
            const auto status=first.processor.gpu_audio_status();
            require(status.availability==A::Available&&status.delivery.has_value(),"shared renderer unavailable");
            require(status.freeze_available&&first.processor.freeze_source_wired(),"shared renderer refused the freeze source");
            // Existing preparation primes renderer history. These counters are
            // observed as-is, never asserted to represent host-rendered audio.
            require(second.processor.gpu_audio_status().availability==A::NotPrepared,"cross-instance status leak");
            require(first.processor.set_render_mode(spectr::MaskRenderMode::zero_latency),"zero latency replacement failed");
            require(first.processor.set_gpu_processing(false),"GPU processing off failed");
#endif
            first.processor.release();
        }
    } catch(...) {done=true;reader.join();throw;}
    done=true;reader.join();
    require(reads>0&&!bad,"concurrent observation contract failed");
    require(!first.processor.gpu_audio_status().delivery.has_value(),"released renderer remained observable");
    std::cout<<"lifecycle_reads="<<reads.load()<<'\n';
}
}
// The editor's GPU processing message: it switches Mixing's renderer, answers
// with the latency state the panel shows, and build_info reports the figure
// the host is told.
void gpu_processing_bridge_controls() {
    Rig rig;
    pulp::view::EditorBridge bridge;
    spectr::register_spectr_editor_handlers(bridge,rig.processor,rig.processor.patterns(),
        rig.processor.editor_authority(),[](std::string_view){return true;});
    require(!choc::json::parse(bridge.dispatch_json(R"({"type":"gpu_processing_set","payload":{"enabled":"yes"}})"))["ok"].getBool(),
            "a non-bool GPU processing value was accepted");
    rig.processor.prepare({48000.0,512,2,2});
    require(rig.processor.set_render_mode(spectr::MaskRenderMode::linear_phase),"Mixing failed");
    const auto mixing_option=[](const choc::value::ValueView& latency){
        for(std::uint32_t i=0;i<latency["options"].size();++i)
            if(latency["options"][i]["mode"].get<std::string>()=="linear_phase")
                return latency["options"][i]["samples"].getWithDefault<double>(-1);
        return -1.0;
    };
    const auto info=[&]{return choc::json::parse(bridge.dispatch_json(R"({"type":"build_info_get","payload":{}})"));};
    require(info()["latency"]["reported_samples"].getWithDefault<double>(-1)==rig.processor.latency_samples(),
            "build_info latency is not the reported latency");
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
    const int cpu=rig.processor.latency_samples();
    const auto on=choc::json::parse(bridge.dispatch_json(R"({"type":"gpu_processing_set","payload":{"enabled":true}})"));
    const auto& on_payload=on.hasObjectMember("payload")?on["payload"]:on;
    require(on["ok"].getBool(),"GPU processing on failed");
    require(rig.processor.gpu_processing()&&rig.processor.latency_samples()>cpu,"GPU processing did not move the latency");
    require(on_payload["latency"]["gpu_processing"].getBool(),"response hides the GPU choice");
    require(mixing_option(on_payload["latency"])==rig.processor.latency_samples(),"Mixing option is not the reported latency");
    require(info()["latency"]["reported_samples"].getWithDefault<double>(-1)==rig.processor.latency_samples(),
            "build_info latency is stale after the switch");
    require(info()["copy_text"].get<std::string>().find(std::to_string(rig.processor.latency_samples())+" samples")!=std::string::npos,
            "copied build info omits the reported latency");
    require(choc::json::parse(bridge.dispatch_json(R"({"type":"gpu_processing_set","payload":{"enabled":false}})"))["ok"].getBool(),
            "GPU processing off failed");
    require(rig.processor.latency_samples()==cpu,"GPU processing off did not restore the CPU latency");
#endif
    rig.processor.release();
}
int main(){try{projection_controls();lifecycle_and_bridge_controls();gpu_processing_bridge_controls();std::cout<<"GPU audio status controls passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
