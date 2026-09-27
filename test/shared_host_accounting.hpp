#pragma once
#include <cstdint>
#include <stdexcept>

namespace spectr::host_probe {
struct EpochAccounting {
    std::uint64_t admitted_quantums;
    std::uint64_t partial_frames;
};
// A reset discards the adapter's incomplete input quantum. It was never
// admitted to the bridge and must not acquire a terminal disposition.
inline EpochAccounting epoch_accounting(std::uint64_t frames, unsigned quantum) {
    if (!quantum) throw std::invalid_argument("zero shared quantum");
    return {frames / quantum, frames % quantum};
}
}
