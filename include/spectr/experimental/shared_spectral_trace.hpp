#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace spectr::experimental {
// Read only with callback, service and terminal reader stopped. These lifetime
// counters count source operations independently of the diagnostic reader.
struct SharedSpectralStoppedAccounting {
    std::uint64_t input_quanta=0,ingress_admitted_quanta=0;
    std::uint64_t terminal_attempts=0,terminal_enqueued=0,terminal_popped=0,lost_records=0;
    bool counter_overflow=false;
};
struct SharedSpectralTraceFinal {
    std::uint64_t renderer_run_id=0,first_epoch=0,last_epoch=0;
    SharedSpectralStoppedAccounting accounting;
    std::uint64_t gpu_delivered=0,cpu_fallback=0,cancelled=0;
    unsigned quantum_frames=0,lead_quanta=0;
    bool physical_release_confirmed=false;
};
// Sole stopped owner. Failed physical release cannot finalize a run; a later
// successful retry consumes the pending final exactly once.
class SharedSpectralTraceRun {
public:
    void begin(SharedSpectralStoppedAccounting baseline,std::uint64_t epoch,unsigned quantum,unsigned lead) noexcept {
        baseline_=baseline;first_epoch_=epoch;quantum_=quantum;lead_=lead;pending_=true;
    }
    std::uint64_t id() const noexcept {return first_epoch_;}
    std::optional<SharedSpectralTraceFinal> finish(bool released,
        SharedSpectralStoppedAccounting current,std::uint64_t last_epoch,
        std::uint64_t gpu,std::uint64_t cpu,std::uint64_t cancelled) noexcept {
        if(!pending_||!released)return {};
        pending_=false;
        auto delta=current;
        delta.counter_overflow=current.counter_overflow||baseline_.counter_overflow;
        auto subtract=[&](std::uint64_t value,std::uint64_t base){
            if(value<base){delta.counter_overflow=true;return std::uint64_t{0};}
            return value-base;
        };
        delta.input_quanta=subtract(current.input_quanta,baseline_.input_quanta);
        delta.ingress_admitted_quanta=subtract(current.ingress_admitted_quanta,baseline_.ingress_admitted_quanta);
        delta.terminal_attempts=subtract(current.terminal_attempts,baseline_.terminal_attempts);
        delta.terminal_enqueued=subtract(current.terminal_enqueued,baseline_.terminal_enqueued);
        delta.terminal_popped=subtract(current.terminal_popped,baseline_.terminal_popped);
        delta.lost_records=subtract(current.lost_records,baseline_.lost_records);
        return SharedSpectralTraceFinal{first_epoch_,first_epoch_,last_epoch,delta,gpu,cpu,cancelled,quantum_,lead_,true};
    }
private:
    SharedSpectralStoppedAccounting baseline_;
    std::uint64_t first_epoch_=0;
    unsigned quantum_=0,lead_=0;
    bool pending_=false;
};
}
