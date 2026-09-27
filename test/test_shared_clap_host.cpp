#include <spectr/experimental/shared_host_probe.hpp>
#include "shared_host_accounting.hpp"
#include <clap/clap.h>
#include <pulp/audio/analysis/audio_assertions.hpp>
#include <pulp/audio/analysis/latency_evidence.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <filesystem>
#include <set>
#include <unistd.h>

namespace {
thread_local bool audio_thread=false;
bool is_main(const clap_host_t*){return !audio_thread;}
bool is_audio(const clap_host_t*){return audio_thread;}
const clap_host_thread_check_t thread_check{is_main,is_audio};
const void* extension(const clap_host_t*,const char* id){return std::strcmp(id,CLAP_EXT_THREAD_CHECK)==0?&thread_check:nullptr;}
void noop(const clap_host_t*){}
void require(bool c,const char* why){if(!c)throw std::runtime_error(why);}
struct Events {
    std::array<clap_event_param_value_t,8> values{};unsigned count=0;
    clap_input_events_t input{this,[](const clap_input_events_t* e){return static_cast<const Events*>(e->ctx)->count;},
        [](const clap_input_events_t* e,std::uint32_t i)->const clap_event_header_t*{
            const auto& s=*static_cast<const Events*>(e->ctx);return i<s.count?&s.values[i].header:nullptr;}};
    void add(clap_id id,double v,unsigned offset=0){
        auto& e=values[count++];e.header={sizeof(e),offset,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
        e.param_id=id;e.value=v;e.note_id=-1;e.port_index=-1;e.channel=-1;e.key=-1;
    }
};
const clap_output_events_t discard{nullptr,[](const clap_output_events_t*,const clap_event_header_t*){return true;}};
struct Module {
    void* library=nullptr;const clap_plugin_entry_t* entry=nullptr;const clap_plugin_t* plugin=nullptr;
    bool initialized=false,active=false,processing=false;
    ~Module(){close();}
    void close(){if(plugin){if(processing){audio_thread=true;plugin->stop_processing(plugin);audio_thread=false;}if(active)plugin->deactivate(plugin);plugin->destroy(plugin);}if(initialized)entry->deinit();if(library)dlclose(library);plugin=nullptr;library=nullptr;initialized=false;active=false;processing=false;}
};
constexpr unsigned block=512, blocks=256, reset_block=128;
constexpr int quantum=SPECTR_HOST_HOP/2;
constexpr int expected_latency=SPECTR_HOST_FFT+SPECTR_HOST_HOP+5*quantum;
constexpr int expected_tail=2*SPECTR_HOST_FFT+SPECTR_HOST_HOP+5*quantum;
void dump(const std::string& path,const pulp::audio::Buffer<float>& data){
    if(path.empty())return;
    std::ofstream f(path);require(bool(f),"capture open failed");f<<std::setprecision(9)<<"sample,left,right\n";
    for(unsigned i=0;i<data.num_samples();++i)f<<i<<','<<data.channel(0)[i]<<','<<data.channel(1)[i]<<'\n';
    f.close();require(bool(f),"capture write failed");
}
}
int main(int argc,char** argv){
    if(argc<2||argc>4)return 64;
    try{
        std::ofstream identity_file;
        if(argc==4){require(!std::filesystem::exists(argv[3]),"identity output must be new");identity_file.open(argv[3]);require(bool(identity_file),"identity output open failed");}
        auto identity_record=[&](const std::string& line){std::cout<<line<<std::endl;if(identity_file.is_open()){identity_file<<line<<std::endl;require(bool(identity_file),"identity receipt write failed");}};
        clap_host_t host{CLAP_VERSION,nullptr,"Spectr shared acceptance","Pulp","","1",extension,noop,noop,noop};
        Module m; // The host must outlive plugin teardown, including exception paths.
        m.library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);require(m.library,"CLAP binary load failed");
        m.entry=static_cast<const clap_plugin_entry_t*>(dlsym(m.library,"clap_entry"));
        const auto query=reinterpret_cast<SpectrSharedHostQuery>(dlsym(m.library,"spectr_shared_host_probe_v1"));
        const auto query_v2=reinterpret_cast<SpectrSharedHostQueryV2>(dlsym(m.library,"spectr_shared_host_probe_v2"));
        require(m.entry&&query&&query_v2,"diagnostic export absent");require(m.entry->init(argv[1]),"entry init failed");m.initialized=true;
        const auto* factory=static_cast<const clap_plugin_factory_t*>(m.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        require(factory&&factory->get_plugin_count(factory)==1,"expected one descriptor");
        const auto* descriptor=factory->get_plugin_descriptor(factory,0);
        require(descriptor&&std::strcmp(descriptor->name,SPECTR_HOST_EXPECTED_NAME)==0&&
                std::strcmp(descriptor->id,SPECTR_HOST_EXPECTED_ID)==0,"actual descriptor identity mismatch");
        std::cout<<"descriptor_name="<<descriptor->name<<" descriptor_id="<<descriptor->id<<'\n';
        m.plugin=factory->create_plugin(factory,&host,descriptor->id);
        require(m.plugin&&m.plugin->init(m.plugin),"plugin init failed");
        const auto* params=static_cast<const clap_plugin_params_t*>(m.plugin->get_extension(m.plugin,CLAP_EXT_PARAMS));
        const auto* latency=static_cast<const clap_plugin_latency_t*>(m.plugin->get_extension(m.plugin,CLAP_EXT_LATENCY));
        const auto* tail=static_cast<const clap_plugin_tail_t*>(m.plugin->get_extension(m.plugin,CLAP_EXT_TAIL));
        require(params&&latency&&tail,"required extensions absent");
        // Resolve IDs from the actual loaded descriptor surface, not a source-linked processor.
        clap_id mix=CLAP_INVALID_ID,trim=CLAP_INVALID_ID,band=CLAP_INVALID_ID;
        for(unsigned i=0;i<params->count(m.plugin);++i){clap_param_info_t info{};require(params->get_info(m.plugin,i,&info),"parameter info failed");
            if(std::strcmp(info.name,"Mix")==0)mix=info.id;if(std::strcmp(info.name,"Output")==0)trim=info.id;if(std::strcmp(info.name,"Band 01 Gain")==0)band=info.id;}
        require(mix!=CLAP_INVALID_ID&&trim!=CLAP_INVALID_ID&&band!=CLAP_INVALID_ID,"control surface mismatch");
        SpectrSharedHostRequest request;require(query(&request)==0&&request.phase==0,"initial query failed");
        const auto token=request.instance_token;
        require(query(nullptr)==1,"null query accepted");auto bad=request;bad.size--;require(query(&bad)==1,"bad size accepted");
        bad=request;bad.version++;require(query(&bad)==1,"bad version accepted");
        bad=request;bad.instance_token=token+100;require(query(&bad)==4,"stale token accepted");
        const auto* second=factory->create_plugin(factory,&host,descriptor->id);require(second,"second instance missing");
        const bool second_initialized=second->init(second);
        const int ambiguous=second_initialized?query(&request):-1;
        second->destroy(second);
        require(second_initialized,"second init failed");require(ambiguous==3,"multiple instances not rejected");
        std::set<std::uint64_t> expected_runs;
        unsigned prepare_ordinal=0;
        auto stopped_id=[&](){SpectrSharedHostRequestV2 r;r.snapshot.instance_token=token;require(query_v2(&r)==0&&r.renderer_run_id,"stopped identity query failed");return r.renderer_run_id;};
        auto record_prepare=[&](bool require_gpu){
            const auto id=stopped_id();require(expected_runs.insert(id).second,"prepare reused trace identity");++prepare_ordinal;
            identity_record("{\"schema\":\"spectr.native-host-trace-inventory.v1\",\"kind\":\"prepared\",\"pid\":"+std::to_string(getpid())+",\"instance_token\":"+std::to_string(token)+",\"prepare_ordinal\":"+std::to_string(prepare_ordinal)+",\"renderer_run_id\":"+std::to_string(id)+",\"gpu_required\":"+(require_gpu?"true":"false")+"}");
            return id;
        };
        auto require_processing_rejected=[&](){SpectrSharedHostRequestV2 r;r.snapshot.instance_token=token;r.renderer_run_id=999;require(query_v2(&r)==2&&r.renderer_run_id==0,"processing identity query accepted");};
        std::array<pulp::audio::Buffer<float>,2> captures{
            pulp::audio::Buffer<float>(2,blocks*block),pulp::audio::Buffer<float>(2,blocks*block)};
        for(unsigned mode=0;mode<2;++mode){
            request={};request.instance_token=token;request.command=SpectrSharedHostRequest::Configure;request.force_cpu=mode;
            require(query(&request)==0,"preparation configuration failed");
            Events initial;initial.add(mix,100);initial.add(trim,0);initial.add(band,0);params->flush(m.plugin,&initial.input,&discard);
            require(m.plugin->activate(m.plugin,48000,block,block),"activate failed");m.active=true;
            const auto trace_run_id=record_prepare(mode==0);
            require(latency->get(m.plugin)==expected_latency&&tail->get(m.plugin)==expected_tail,"PDC/tail report mismatch");
            request.command=SpectrSharedHostRequest::Configure;require(query(&request)==2,"prepared mutation accepted");
            audio_thread=true;require(m.plugin->start_processing(m.plugin),"start failed");m.processing=true;
            require_processing_rejected();
            require(!m.plugin->start_processing(m.plugin),"double start accepted");
            std::array<std::array<float,block>,2> input{},output{};float* ip[]{input[0].data(),input[1].data()},*op[]{output[0].data(),output[1].data()};
            clap_audio_buffer_t in{ip,nullptr,2,0,0},out{op,nullptr,2,0,0};
            const auto start=std::chrono::steady_clock::now();
            for(unsigned b=0;b<blocks;++b){
                std::this_thread::sleep_until(start+std::chrono::nanoseconds(std::uint64_t(b)*block*1000000000/48000));
                if(b==reset_block){m.plugin->stop_processing(m.plugin);m.processing=false;require(stopped_id()==trace_run_id,"identity changed before reset");m.plugin->reset(m.plugin);require(stopped_id()==trace_run_id,"reset changed trace identity");require(m.plugin->start_processing(m.plugin),"reset restart failed");m.processing=true;}
                for(unsigned i=0;i<block;++i){input[0][i]=.2f*std::sin(6.283185307179586*997*(b*block+i)/48000);input[1][i]=.15f*std::sin(6.283185307179586*431*(b*block+i)/48000);}
                Events events;events.add(mix,b>96?40:100);events.add(trim,b>160?-6:0);events.add(band,b>64?-3:0);
                if(b==64)events.add(band,-3,31);if(b==96)events.add(mix,40,17);if(b==160)events.add(trim,-6,29);
                clap_process_t p{};p.steady_time=b*block;p.frames_count=block;p.audio_inputs=&in;p.audio_outputs=&out;p.audio_inputs_count=1;p.audio_outputs_count=1;p.in_events=&events.input;p.out_events=&discard;
                require(m.plugin->process(m.plugin,&p)!=CLAP_PROCESS_ERROR,"process failed");
                for(unsigned ch=0;ch<2;++ch)std::copy_n(output[ch].data(),block,captures[mode].channel(ch).data()+b*block);
            }
            m.plugin->stop_processing(m.plugin);m.processing=false;audio_thread=false;
            require(stopped_id()==trace_run_id,"trace identity changed after processing/reset");
            require(latency->get(m.plugin)==expected_latency,"PDC changed during processing");
            request.command=SpectrSharedHostRequest::Finalize;require(query(&request)==0&&request.release_confirmed,"checked renderer release failed");
            std::cout<<"mode="<<(mode?"forced_cpu":"normal")<<" gpu="<<request.gpu_selected<<" cpu="<<request.cpu_selected<<" cancelled="<<request.cancelled<<" lost="<<request.lost_records<<" processed_frames="<<request.processed_frames<<" release_confirmed="<<request.release_confirmed<<'\n';
            require(request.processed_frames==blocks*block&&request.rejected_process_calls==0,"callback accounting wrong");
            const auto before_reset=spectr::host_probe::epoch_accounting(reset_block*block,quantum);
            const auto after_reset=spectr::host_probe::epoch_accounting((blocks-reset_block)*block,quantum);
            std::cout<<" admitted_quantums="<<before_reset.admitted_quantums+after_reset.admitted_quantums
                     <<" reset_partial_frames="<<before_reset.partial_frames
                     <<" final_partial_frames="<<after_reset.partial_frames<<'\n';
            require(request.gpu_selected+request.cpu_selected+request.cancelled==
                    before_reset.admitted_quantums+after_reset.admitted_quantums &&
                    request.lost_records==0,"terminal accounting incomplete");
            require(mode?request.gpu_selected==0&&request.cpu_selected>0:request.gpu_selected>0,"GPU positive/forcedCPU control failed");
            require(query(&request)==2,"duplicate finalize accepted");request.command=SpectrSharedHostRequest::Configure;require(query(&request)==2,"finalized reconfigure accepted");
            // Deliberately try a callback after finalization: the diagnostic
            // processor must refuse DSP, clear output and count the violation.
            audio_thread=true;require(m.plugin->start_processing(m.plugin),"negative start failed");m.processing=true;
            Events empty;clap_process_t rejected{};rejected.frames_count=block;rejected.audio_inputs=&in;rejected.audio_outputs=&out;rejected.audio_inputs_count=1;rejected.audio_outputs_count=1;rejected.in_events=&empty.input;rejected.out_events=&discard;
            for(auto& channel:output)channel.fill(7);m.plugin->process(m.plugin,&rejected);m.plugin->stop_processing(m.plugin);m.processing=false;audio_thread=false;
            for(const auto& channel:output)for(float v:channel)require(v==0,"finalized process emitted audio");
            request.command=SpectrSharedHostRequest::Snapshot;require(query(&request)==0&&request.rejected_process_calls==1,"post-finalize process not rejected");
            m.plugin->deactivate(m.plugin);m.active=false;
            if(argc>=3)dump(std::string(argv[2])+(mode?"-cpu.csv":"-gpu.csv"),captures[mode]);
        }
        const auto comparison=pulp::test::audio::assert_null_near(captures[0],captures[1],-90);
        require(comparison.passed,comparison.message.c_str());
        require(pulp::test::audio::assert_not_silent(pulp::test::audio::analyze(captures[0],48000)).passed,"silent positive capture");
        // Independent PDC measurement on the actual binary in fully dry mode.
        for(unsigned mode=0;mode<2;++mode){
            request={};request.instance_token=token;request.command=SpectrSharedHostRequest::Configure;request.force_cpu=mode;require(query(&request)==0,"PDC configuration failed");
            Events initial;initial.add(mix,0);initial.add(trim,0);initial.add(band,0);params->flush(m.plugin,&initial.input,&discard);
            require(m.plugin->activate(m.plugin,48000,block,block),"PDC activate failed");m.active=true;
            (void)record_prepare(false);
            const auto reported=latency->get(m.plugin);const unsigned frames=((expected_latency+1024+block-1)/block)*block;
            pulp::audio::Buffer<float> stimulus(2,frames),rendered(2,frames),expected(2,frames);stimulus.clear();rendered.clear();expected.clear();
            constexpr unsigned marker[]{13,29};constexpr float level[]{.5f,-.3f};for(unsigned ch=0;ch<2;++ch){stimulus.channel(ch)[marker[ch]]=level[ch];expected.channel(ch)[marker[ch]+expected_latency]=level[ch];}
            audio_thread=true;require(m.plugin->start_processing(m.plugin),"PDC start failed");m.processing=true;
            Events empty;for(unsigned offset=0;offset<frames;offset+=block){
                float* ip[]{stimulus.channel(0).data()+offset,stimulus.channel(1).data()+offset};float* op[]{rendered.channel(0).data()+offset,rendered.channel(1).data()+offset};
                clap_audio_buffer_t in{ip,nullptr,2,0,0},out{op,nullptr,2,0,0};clap_process_t p{};p.steady_time=offset;p.frames_count=block;p.audio_inputs=&in;p.audio_outputs=&out;p.audio_inputs_count=1;p.audio_outputs_count=1;p.in_events=&empty.input;p.out_events=&discard;require(m.plugin->process(m.plugin,&p)!=CLAP_PROCESS_ERROR,"PDC process failed");
            }
            m.plugin->stop_processing(m.plugin);m.processing=false;audio_thread=false;
            require(latency->get(m.plugin)==reported,"PDC report drift");
            const auto null=pulp::test::audio::assert_null_near(rendered,expected,-100);require(null.passed,null.message.c_str());
            for(unsigned ch=0;ch<2;++ch){pulp::audio::Buffer<float> mi(1,frames),mo(1,frames);std::copy_n(stimulus.channel(ch).data(),frames,mi.channel(0).data());std::copy_n(rendered.channel(ch).data(),frames,mo.channel(0).data());
                auto evidence=pulp::test::audio::measure_marker_offset(mi,mo,int(reported),{.input_marker_frame=marker[ch],.onset_threshold=.1});pulp::test::audio::apply_expected_samples(evidence,expected_latency);
                std::cout<<"pdc mode="<<mode<<" channel="<<ch<<' '<<pulp::test::audio::latency_evidence_to_json(evidence)<<'\n';
                require(evidence.contract_outcome==pulp::test::audio::LatencyContractOutcome::satisfied,"measured PDC mismatch");
                require(pulp::test::audio::measure_marker_offset(mi,mo,int(reported)+1,{.input_marker_frame=marker[ch],.onset_threshold=.1}).contract_outcome==pulp::test::audio::LatencyContractOutcome::violated,"wrong PDC control passed");}
            request.command=SpectrSharedHostRequest::Finalize;require(query(&request)==0&&request.release_confirmed,"PDC release unconfirmed");m.plugin->deactivate(m.plugin);m.active=false;
        }
        m.plugin->destroy(m.plugin);m.plugin=nullptr;
        m.plugin=factory->create_plugin(factory,&host,descriptor->id);require(m.plugin&&m.plugin->init(m.plugin),"replacement init failed");
        request={};request.instance_token=token;require(query(&request)==4,"old instance token accepted by replacement");
        SpectrSharedHostRequestV2 stale;stale.snapshot.instance_token=token;
        require(query_v2(&stale)==4&&stale.renderer_run_id==0,"v2 stale token accepted");
        require(prepare_ordinal==4,"unexpected successful prepare inventory");
        m.close();
        identity_record("{\"schema\":\"spectr.native-host-trace-inventory.v1\",\"kind\":\"complete\",\"pid\":"+std::to_string(getpid())+",\"prepared_count\":"+std::to_string(prepare_ordinal)+",\"module_closed\":true}");
        std::cout<<"shared_clap_acceptance=passed scheduling=ordinary_thread per_sequence_uniqueness=not_tested physical_device=not_used\n";
        return 0;
    }catch(const std::exception& e){audio_thread=false;std::cerr<<e.what()<<'\n';return 1;}
}
