#include <spectr/experimental/shared_host_registry.hpp>
#include <iostream>
#include <spectr/experimental/shared_host_processing_gate.hpp>
#include "shared_host_accounting.hpp"
struct Fake {
    unsigned phase=0;
    spectr::host_probe::ProcessingGate gate;
    std::uint64_t run_id=0;
    int query_v2(SpectrSharedHostRequestV2& r){
        if(!gate.begin_query())return 2;
        const bool ready=phase==1&&run_id!=0;
        if(ready){r.snapshot.phase=phase;r.snapshot.shared_renderer=1;r.renderer_run_id=run_id;}
        gate.end_query();return ready?0:2;
    }
    int query(SpectrSharedHostRequest& r){
        if(r.command==SpectrSharedHostRequest::Finalize){if(phase!=1)return 2;phase=2;}
        if(r.command==SpectrSharedHostRequest::Configure && phase!=0)return 2;
        r.phase=phase;return 0;
    }
};
int main(){
    spectr::host_probe::Registry<Fake> registry;
    unsigned checks=0;auto check=[&](bool x){++checks;if(!x)std::cerr<<"failed "<<checks<<'\n';return x;};
    SpectrSharedHostRequest r;
    if(!check(registry.query(nullptr)==1)||!check(registry.query(&r)==3))return 1;
    Fake a,b;const auto ta=registry.add(&a);
    if(!check(registry.query(&r)==0)||!check(r.instance_token==ta))return 1;
    auto malformed=r;malformed.size--;if(!check(registry.query(&malformed)==1))return 1;
    malformed=r;malformed.version++;if(!check(registry.query(&malformed)==1))return 1;
    malformed=r;malformed.command=99;if(!check(registry.query(&malformed)==1))return 1;
    malformed=r;malformed.force_cpu=2;if(!check(registry.query(&malformed)==1))return 1;
    malformed=r;malformed.instance_token=ta+100;if(!check(registry.query(&malformed)==4))return 1;
    malformed=r;malformed.instance_token=0;malformed.command=1;if(!check(registry.query(&malformed)==4))return 1;
    const auto tb=registry.add(&b);if(!check(registry.query(&r)==3))return 1;
    registry.remove(tb);r.command=SpectrSharedHostRequest::Configure;if(!check(registry.query(&r)==0))return 1;
    r.command=SpectrSharedHostRequest::Finalize;if(!check(registry.query(&r)==2))return 1;
    a.phase=1;if(!check(registry.query(&r)==0)||!check(r.phase==2)||!check(registry.query(&r)==2))return 1;
    r.command=SpectrSharedHostRequest::Configure;if(!check(registry.query(&r)==2))return 1;
    registry.remove(ta);const auto tc=registry.add(&b);
    r.command=SpectrSharedHostRequest::Snapshot;if(!check(registry.query(&r)==4))return 1;
    r.instance_token=0;if(!check(registry.query(&r)==0)||!check(r.instance_token==tc))return 1;
    SpectrSharedHostRequestV2 v2;
    if(!check(registry.query_v2(nullptr)==1)||!check(registry.query_v2(&v2)==4))return 1;
    v2.snapshot.instance_token=tc;
    if(!check(registry.query_v2(&v2)==2))return 1;
    b.phase=1;b.run_id=91;
    if(!check(registry.query_v2(&v2)==0)||!check(v2.renderer_run_id==91))return 1;
    for(unsigned field=0;field<5;++field){
        auto bad=v2;bad.renderer_run_id=999;
        if(field==0)--bad.size;if(field==1)++bad.version;
        if(field==2)--bad.snapshot.size;if(field==3)++bad.snapshot.version;
        if(field==4)bad.snapshot.command=SpectrSharedHostRequest::Finalize;
        if(!check(registry.query_v2(&bad)==1)||!check(bad.renderer_run_id==(field==0?999:0)))return 1;
    }
    auto bad=v2;bad.snapshot.force_cpu=2;if(!check(registry.query_v2(&bad)==1))return 1;
    bad=v2;bad.snapshot.instance_token=ta;if(!check(registry.query_v2(&bad)==4)||!check(bad.renderer_run_id==0))return 1;
    const auto extra=registry.add(&a);if(!check(registry.query_v2(&v2)==3)||!check(v2.renderer_run_id==0))return 1;registry.remove(extra);
    if(!check(b.gate.start())||!check(!b.gate.start())||!check(registry.query_v2(&v2)==2)||!check(v2.renderer_run_id==0))return 1;
    b.gate.stopped();b.gate.stopped();if(!check(registry.query_v2(&v2)==0))return 1;
    if(!check(b.gate.begin_query())||!check(!b.gate.start()))return 1;
    b.gate.stopped();if(!check(!b.gate.start()))return 1; // stray stop cannot release a query
    b.gate.end_query();if(!check(b.gate.start()))return 1;
    b.gate.stopped(); // failed delegated start rollback
    if(!check(b.gate.begin_query()))return 1;b.gate.end_query();
    b.phase=0;if(!check(registry.query_v2(&v2)==2)||!check(v2.renderer_run_id==0))return 1;
    // Valid 1024-frame quantum, reset deliberately inside its fourth input.
    // The old whole-render floor gives 8, but only 3 + 4 were admitted.
    const auto left=spectr::host_probe::epoch_accounting(4093,1024);
    const auto right=spectr::host_probe::epoch_accounting(8192-4093,1024);
    if(!check(left.admitted_quantums==3)||!check(left.partial_frames==1021)||
       !check(right.admitted_quantums==4)||!check(right.partial_frames==3)||
       !check(left.admitted_quantums+right.admitted_quantums==7)||
       !check(left.partial_frames+right.partial_frames==1024))return 1;
    const auto aligned=spectr::host_probe::epoch_accounting(128*512,1024);
    if(!check(aligned.admitted_quantums==64)||!check(aligned.partial_frames==0))return 1;
    bool refused=false;try{(void)spectr::host_probe::epoch_accounting(1,0);}
    catch(const std::invalid_argument&){refused=true;}
    if(!check(refused))return 1;
    std::cout<<checks<<" registry/accounting checks passed; fake phase checks are not renderer lifecycle proof\n";
}
