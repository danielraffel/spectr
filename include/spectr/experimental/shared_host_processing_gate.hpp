#pragma once
#include <atomic>

namespace spectr::host_probe {
// Actual diagnostic CLAP start/stop callbacks own the Processing transition.
// Query and start cannot overlap, even if a host violates control serialization.
// No callback or polling work; a contending start/query fails immediately.
class ProcessingGate {
    static_assert(std::atomic<unsigned>::is_always_lock_free);
public:
    bool start() noexcept {return acquire(Processing);}
    void stopped() noexcept {
        unsigned expected=Processing;
        (void)state_.compare_exchange_strong(expected,Stopped,std::memory_order_acq_rel);
    }
    bool begin_query() noexcept {return acquire(Query);}
    void end_query() noexcept {state_.store(Stopped,std::memory_order_release);}
private:
    enum State:unsigned {Stopped,Query,Processing};
    bool acquire(State next) noexcept {
        unsigned expected=Stopped;
        return state_.compare_exchange_strong(expected,next,std::memory_order_acq_rel);
    }
    std::atomic<unsigned> state_{Stopped};
};
}
