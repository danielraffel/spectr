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
            require(first.processor.set_render_mode(spectr::MaskRenderMode::linear_phase),"linear renderer failed to prepare");
            const auto status=first.processor.gpu_audio_status();
            require(status.availability==A::Available&&status.delivery.has_value(),"shared renderer unavailable");
            // Existing preparation primes renderer history. These counters are
            // observed as-is, never asserted to represent host-rendered audio.
            require(second.processor.gpu_audio_status().availability==A::NotPrepared,"cross-instance status leak");
            require(first.processor.set_render_mode(spectr::MaskRenderMode::zero_latency),"zero latency replacement failed");
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
int main(){try{projection_controls();lifecycle_and_bridge_controls();std::cout<<"GPU audio status controls passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
