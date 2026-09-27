#include <spectr/experimental/shared_host_registry.hpp>
#include <iostream>
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
    std::cout<<checks<<" registry checks passed; fake phase checks are not renderer lifecycle proof\n";
}
