#pragma once
#include <cstdint>
#include <optional>

namespace spectr {
// Read-only UI/control observation. Counts are independent live observations
// since renderer preparation, before product mix/trim; they are not GPU timing
// or a coherent per-epoch accounting receipt.
struct GpuAudioStatus {
    enum class Availability { NotBuilt, NotPrepared, NonSharedRenderer, Available };
    struct Delivery {
        unsigned provider_state = 0;
        std::uint64_t current_epoch = 0;
        std::uint64_t gpu_selected = 0, cpu_fallback = 0, cancelled = 0,
                      lost_terminal_records = 0;
    };
    Availability availability = Availability::NotBuilt;
    std::optional<Delivery> delivery;
};
}
