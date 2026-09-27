#pragma once
#include <spectr/gpu_audio_status.hpp>
#include <choc/containers/choc_Value.h>
#include <string>

namespace spectr::detail {
inline const char* gpu_audio_availability(GpuAudioStatus::Availability value) {
    switch(value) {
    case GpuAudioStatus::Availability::NotBuilt: return "not_built";
    case GpuAudioStatus::Availability::NotPrepared: return "not_prepared";
    case GpuAudioStatus::Availability::NonSharedRenderer: return "non_shared_renderer";
    case GpuAudioStatus::Availability::Available: return "available";
    }
    return "unknown";
}
inline const char* gpu_audio_provider_state(unsigned value) {
    switch(value) {
    case 0: return "unprepared";
    case 1: return "shared_ready";
    case 2: return "cpu_only";
    case 3: return "fenced";
    case 4: return "release_unconfirmed";
    default: return "unknown";
    }
}
inline bool gpu_audio_status_available(const GpuAudioStatus& s) {
    return s.availability==GpuAudioStatus::Availability::Available && s.delivery.has_value();
}
inline choc::value::Value gpu_audio_status_projection(const GpuAudioStatus& s) {
    auto value=choc::value::createObject("SpectrGpuAudioStatus");
    value.addMember("schema_version",1);
    const bool available=gpu_audio_status_available(s);
    value.addMember("available",available);
    value.addMember("reason",std::string(available?"available":
        s.availability==GpuAudioStatus::Availability::Available?"snapshot_unavailable":gpu_audio_availability(s.availability)));
    if(!available)return value;
    const auto& d=*s.delivery;
    value.addMember("sampling","independent_live_counters");
    value.addMember("count_scope","renderer_preparation_before_mix_trim");
    value.addMember("provider_state",std::string(gpu_audio_provider_state(d.provider_state)));
    // Decimal strings preserve all 64 bits across JavaScript editor bridges.
    value.addMember("current_epoch",std::to_string(d.current_epoch));
    value.addMember("gpu_selected",std::to_string(d.gpu_selected));
    value.addMember("cpu_fallback",std::to_string(d.cpu_fallback));
    value.addMember("cancelled",std::to_string(d.cancelled));
    value.addMember("lost_terminal_records",std::to_string(d.lost_terminal_records));
    return value;
}
inline std::string gpu_audio_status_copy_text(const GpuAudioStatus& s) {
    if(!gpu_audio_status_available(s))return std::string("GPU audio: unavailable (")+
        (s.availability==GpuAudioStatus::Availability::Available?"snapshot_unavailable":gpu_audio_availability(s.availability))+")";
    const auto& d=*s.delivery;
    return std::string("GPU audio: ")+gpu_audio_provider_state(d.provider_state)+
        "\nGPU selected: "+std::to_string(d.gpu_selected)+
        "\nCPU fallback: "+std::to_string(d.cpu_fallback)+
        "\nCancelled: "+std::to_string(d.cancelled)+
        "\nLost terminal records: "+std::to_string(d.lost_terminal_records)+
        "\nCurrent epoch: "+std::to_string(d.current_epoch)+
        "\nGPU audio counts: independent live observations since renderer preparation, before mix/trim";
}
}
