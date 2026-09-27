#pragma once
#include "shared_host_probe.hpp"
#include <map>
#include <mutex>

namespace spectr::host_probe {
// No callback use. Holding the lock through dispatch protects instance lifetime;
// it does not make independently loaded renderer counters coherent.
template<class Instance> class Registry {
public:
    std::uint64_t add(Instance* p) {
        std::lock_guard lock(mutex_);
        const auto token=++next_;
        live_.emplace(token,p);
        return token;
    }
    void remove(std::uint64_t token) { std::lock_guard lock(mutex_);live_.erase(token); }
    int query(SpectrSharedHostRequest* request) {
        if(!request || request->size!=sizeof(*request) || request->version!=1 ||
           request->command>SpectrSharedHostRequest::Finalize || request->force_cpu>1)return 1;
        std::lock_guard lock(mutex_);
        if(live_.size()!=1)return 3;
        const auto& [token,p]=*live_.begin();
        if((request->instance_token && request->instance_token!=token) ||
           (!request->instance_token && request->command!=SpectrSharedHostRequest::Snapshot))return 4;
        request->instance_token=token;
        return p->query(*request);
    }
private:
    std::mutex mutex_;
    std::map<std::uint64_t,Instance*> live_;
    std::uint64_t next_=0;
};
}
