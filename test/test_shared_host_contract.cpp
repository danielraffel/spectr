#include <spectr/experimental/shared_host_registry.hpp>
#include <iostream>
#include "shared_host_accounting.hpp"
struct Fake {
    unsigned phase=0;
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
