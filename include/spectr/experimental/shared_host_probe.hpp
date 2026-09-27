#pragma once
#include <cstdint>

// Diagnostic build only; not a production ABI. Commands are main/control-thread
// operations serialized with instance creation/destruction. Snapshot/Finalize
// require the host to have called CLAP stop_processing. No editor may mutate
// modes/state concurrently. Tokens bind requests to the live processor created
// by this CLAP module's diagnostic factory, never a separately linked author.
struct SpectrSharedHostRequest {
    std::uint32_t size=sizeof(SpectrSharedHostRequest), version=1;
    enum Command : std::uint32_t { Snapshot=0, Configure=1, Finalize=2 };
    std::uint32_t command=Snapshot, force_cpu=0;
    std::uint64_t instance_token=0;
    std::uint32_t phase=0, shared_renderer=0, release_confirmed=0;
    std::int32_t latency_samples=0, tail_samples=0;
    std::uint64_t epoch=0, gpu_selected=0, cpu_selected=0, cancelled=0,
                  lost_records=0, processed_frames=0, rejected_process_calls=0;
};
using SpectrSharedHostQuery=int(*)(SpectrSharedHostRequest*);
// 0 success; 1 malformed; 2 invalid phase; 3 absent/ambiguous instance;
// 4 stale/missing token; 5 checked renderer release failed.
