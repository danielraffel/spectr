#include <spectr/experimental/tracking_gpu_convolver.hpp>

#include <pulp/gpu_audio/gpu_audio_transport.hpp>
#include <pulp/gpu_audio/gpu_convolver.hpp>
#include <pulp/runtime/triple_buffer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace spectr::experimental {
namespace {
using namespace pulp::gpu_audio;
constexpr unsigned kMaxChannels = 8;
constexpr auto kEditSettle = std::chrono::milliseconds(15);

struct IrRequest {
    std::vector<float> coefficients;
    std::uint64_t generation = 0;
};

// Member destruction releases and joins the transport before destroying the
// node it borrows. Generation ownership is transferred as one pointer.
struct Generation {
    Generation(unsigned channels, unsigned rate, const IrRequest& request, std::uint64_t epoch,
               std::uint64_t serial)
        : ir_generation(request.generation), reset_epoch(epoch), serial(serial),
          history_quanta((request.coefficients.size() + TrackingGpuConvolver::kQuantumFrames - 1) /
                             TrackingGpuConvolver::kQuantumFrames +
                         TrackingGpuConvolver::kGpuLeadQuanta),
          node(channels, TrackingGpuConvolver::kQuantumFrames, rate, request.coefficients,
               TrackingGpuConvolver::kGpuLeadQuanta) {}

    const std::uint64_t ir_generation, reset_epoch, serial;
    const std::size_t history_quanta;
    GpuConvolver node;
    GpuAudioTransport transport;
};
static_assert(std::atomic<Generation*>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace

struct TrackingGpuConvolver::Impl {
    Impl(Config shape, std::span<const float> initial)
        : config(shape), requests(IrRequest{std::vector<float>(initial.begin(), initial.end()), 0}),
          cpu_delay(kGpuLeadQuanta * config.channels * kQuantumFrames),
          gpu_audio(config.channels * kQuantumFrames) {
        for (unsigned ch = 0; ch < config.channels; ++ch)
            gpu_channels[ch] = gpu_audio.data() + ch * kQuantumFrames;
    }

    std::unique_ptr<Generation> build(const IrRequest& ir, std::uint64_t epoch) {
        auto candidate = std::make_unique<Generation>(config.channels, config.sample_rate, ir,
                                                      epoch, ++next_serial);
        auto& node = candidate->node;
        if (!node.set_provider_policy(GpuConvolver::ProviderPolicy::SharedRequired) ||
            !node.prepare())
            return {};
        auto& transport = candidate->transport;
        if (!transport.prepare(
                &node, {.ring_blocks = 16, .run_worker_thread = true, .wake_on_write = true}))
            return {};
        const auto cap = transport.capability_report();
        if (cap.path != GpuAudioExecutionPath::SharedMemory ||
            cap.provider != GpuAudioProvider::Dawn || !cap.fallback_available ||
            cap.prepared_lead_blocks != kGpuLeadQuanta ||
            transport.latency_samples() != kGpuLeadQuanta * kQuantumFrames ||
            node.backend() != "Metal")
            return {};
        return candidate;
    }

    void provider_health(const Generation& generation) noexcept {
        const auto status = generation.transport.status_snapshot();
        ProviderState projected = ProviderState::SharedReady;
        Health cause = Health::None;
        if (status.provider_state == GpuAudioProviderState::Unavailable) {
            projected = ProviderState::Unavailable;
            cause = status.recovery_reason == GpuAudioRecoveryReason::ProviderLost
                        ? Health::ProviderLost
                        : Health::ProviderFailure;
        } else if (status.provider_state != GpuAudioProviderState::Ready ||
                   !status.delivery_epoch_active) {
            projected = ProviderState::Degraded;
            cause = Health::ProviderFailure;
        }
        health.store(cause, std::memory_order_relaxed);
        provider.store(static_cast<std::uint8_t>(status.provider), std::memory_order_relaxed);
        state.store(projected, std::memory_order_release);
    }

    void refused(std::uint64_t generation) noexcept {
        failed_generation.store(generation, std::memory_order_relaxed);
        provider.store(static_cast<std::uint8_t>(GpuAudioProvider::Unknown),
                       std::memory_order_relaxed);
        health.store(Health::ProviderRefused, std::memory_order_relaxed);
        state.store(ProviderState::Unavailable, std::memory_order_release);
    }

    void run() noexcept {
        // The triple buffer's front stays owned by this thread through the
        // next read(), including when design publishes during GPU compilation.
        std::uint64_t seen_generation = 0, seen_epoch = 0;
        bool needs_build = false, latest_failed = false;
        auto quiet_since = std::chrono::steady_clock::now();
        while (!stop.load(std::memory_order_acquire)) {
            // An acquire of a retired pointer observes the preceding callback
            // publication of observed_active. Only this thread can destroy it.
            delete retired.exchange(nullptr, std::memory_order_acq_rel);
            const auto& ir = requests.read();
            const auto epoch = requested_epoch.load(std::memory_order_acquire);
            const auto now = std::chrono::steady_clock::now();
            if (ir.generation != seen_generation || epoch != seen_epoch) {
                seen_generation = ir.generation;
                seen_epoch = epoch;
                quiet_since = now;
                needs_build = true;
                latest_failed = false;
            }
            if (needs_build && now - quiet_since >= kEditSettle) {
                needs_build = false;
                std::unique_ptr<Generation> result;
                try {
                    result = build(ir, epoch);
                } catch (...) {
                }
                if (requests.read().generation == seen_generation &&
                    requested_epoch.load(std::memory_order_acquire) == epoch) {
                    if (result) {
                        ready_generation.store(seen_generation, std::memory_order_relaxed);
                        // Ownership of the displaced mailbox entry goes only to
                        // the exchanger. Audio cannot still be using it.
                        delete ready.exchange(result.release(), std::memory_order_acq_rel);
                    } else {
                        latest_failed = true;
                        refused(seen_generation);
                    }
                }
            }
            if (auto* current = observed_active.load(std::memory_order_acquire);
                current && !latest_failed)
                provider_health(*current);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool render(const float* const* input, const float* const* cpu, float* const* output,
                std::uint64_t generation, bool stable) noexcept {
        // Both exchanges transfer exclusive ownership. At most one retiring
        // transport can be queued. Design can keep replacing ready while the
        // old transport drains. No callback accesses another owner's slot.
        if (retired.load(std::memory_order_acquire) == nullptr) {
            if (auto* incoming = ready.exchange(nullptr, std::memory_order_acq_rel)) {
                auto* old = active;
                active = incoming;
                history = 0;
                observed_active.store(active, std::memory_order_release);
                retired.store(old, std::memory_order_release);
                active_generation.store(active->ir_generation, std::memory_order_relaxed);
                active_serial.store(active->serial, std::memory_order_relaxed);
            }
        }
        const auto q = kQuantumFrames;
        bool valid = input && cpu && output;
        for (unsigned ch = 0; ch < config.channels && valid; ++ch)
            valid = input[ch] && cpu[ch] && output[ch];
        if (!valid) {
            reset_audio();
            return false;
        }
        if (generation != cpu_generation || !stable)
            stable_quanta = 0;
        cpu_generation = generation;
        const bool stable_due = stable && stable_quanta >= kGpuLeadQuanta;
        if (stable && stable_quanta < kGpuLeadQuanta)
            ++stable_quanta;

        bool selected = false;
        if (active->ir_generation == generation &&
            active->reset_epoch == requested_epoch.load(std::memory_order_relaxed)) {
            pulp::audio::BufferView<const float> in(input, config.channels, q);
            pulp::audio::BufferView<float> out(gpu_channels.data(), config.channels, q);
            const auto before = active->transport.delivery_snapshot().gpu_blocks;
            active->transport.process(in, out, q);
            const bool gpu_due = active->transport.delivery_snapshot().gpu_blocks != before;
            // Pulp invalidates its own epoch when input or completions have a
            // gap. Only the current callback can increment gpu_blocks. Apply
            // the independent product generation/fade/reset gate as well.
            if (history >= active->history_quanta && stable_due) {
                selected = gpu_due;
                if (!gpu_due)
                    misses.fetch_add(1, std::memory_order_relaxed);
            }
            if (history < active->history_quanta)
                ++history;
        } else {
            history = 0;
        }
        for (unsigned ch = 0; ch < config.channels; ++ch) {
            float* delay = cpu_delay.data() + (delay_pos * config.channels + ch) * q;
            for (unsigned i = 0; i < q; ++i) {
                const float due = delay[i];
                delay[i] = cpu[ch][i]; // capture current before overwriting its aliased output
                output[ch][i] = selected ? gpu_channels[ch][i] : due;
            }
        }
        delay_pos = (delay_pos + 1) % kGpuLeadQuanta;
        (selected ? gpu_selected : cpu_fallback).fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void reset_audio() noexcept {
        std::fill(cpu_delay.begin(), cpu_delay.end(), 0.f);
        delay_pos = 0;
        history = stable_quanta = 0;
        requested_epoch.fetch_add(1, std::memory_order_release);
    }

    ~Impl() {
        stop.store(true, std::memory_order_release);
        if (worker.joinable())
            worker.join();
        delete ready.exchange(nullptr);
        delete retired.exchange(nullptr);
        delete active;
    }

    Config config;
    pulp::runtime::TripleBuffer<IrRequest> requests;
    std::uint64_t next_serial = 0; // control at startup, worker thereafter
    Generation* active = nullptr;  // callback-owned until audio has stopped
    std::atomic<Generation*> ready{nullptr}, retired{nullptr}, observed_active{nullptr};
    std::thread worker;
    std::atomic<bool> stop{false};
    std::vector<float> cpu_delay, gpu_audio;
    std::array<float*, kMaxChannels> gpu_channels{};
    unsigned delay_pos = 0, stable_quanta = 0;
    std::size_t history = 0;
    std::uint64_t cpu_generation = 0;
    std::atomic<std::uint64_t> requested_epoch{0}, requested_generation{0};
    std::atomic<std::uint64_t> ready_generation{0}, active_generation{0}, failed_generation{0};
    std::atomic<std::uint64_t> active_serial{0}, gpu_selected{0}, cpu_fallback{0}, misses{0};
    std::atomic<ProviderState> state{ProviderState::Unprepared};
    std::atomic<Health> health{Health::None};
    std::atomic<std::uint8_t> provider{0};
};

TrackingGpuConvolver::TrackingGpuConvolver(Config config) : config_(config) {}
TrackingGpuConvolver::~TrackingGpuConvolver() = default;

bool TrackingGpuConvolver::prepare(std::span<const float> initial_ir) {
    release();
    if (!config_.channels || config_.channels > kMaxChannels || !config_.sample_rate ||
        initial_ir.empty() || initial_ir.size() > config_.max_ir_taps)
        return false;
    try {
        impl_ = std::make_unique<Impl>(config_, initial_ir);
        auto& s = *impl_;
        auto initial = s.build(s.requests.read(), 0);
        if (!initial) {
            s.refused(0);
            return false;
        }
        s.provider_health(*initial);
        if (s.state.load() != ProviderState::SharedReady) {
            s.refused(0);
            return false;
        }
        s.worker = std::thread([worker_state = &s] { worker_state->run(); });
        s.active = initial.release();
        s.active_serial.store(s.active->serial);
        s.observed_active.store(s.active, std::memory_order_release);
        return true;
    } catch (...) {
        if (impl_)
            impl_->refused(0);
        return false;
    }
}

bool TrackingGpuConvolver::enqueue_ir(std::uint64_t generation, std::span<const float> ir) {
    if (!impl_ || !impl_->worker.joinable() || ir.empty() || ir.size() > config_.max_ir_taps ||
        generation <= impl_->requested_generation.load(std::memory_order_relaxed))
        return false;
    try {
        impl_->requests.write_with([&](IrRequest& back) {
            back.coefficients.assign(ir.begin(), ir.end());
            back.generation = generation;
        });
    } catch (...) {
        return false;
    }
    impl_->requested_generation.store(generation, std::memory_order_release);
    return true;
}

bool TrackingGpuConvolver::process(const float* const* input, const float* const* cpu,
                                   float* const* output, std::uint64_t generation,
                                   bool stable) noexcept {
    return impl_ && impl_->active && impl_->render(input, cpu, output, generation, stable);
}
void TrackingGpuConvolver::reset() noexcept {
    if (impl_)
        impl_->reset_audio();
}
bool TrackingGpuConvolver::release() noexcept {
    impl_.reset();
    return true;
}
TrackingGpuConvolver::Snapshot TrackingGpuConvolver::snapshot() const noexcept {
    if (!impl_)
        return {};
    const auto& s = *impl_;
    Snapshot result;
    result.state = s.state.load(std::memory_order_acquire);
    result.health = s.health.load();
    result.provider = s.provider.load();
    result.requested_generation = s.requested_generation.load();
    result.ready_generation = s.ready_generation.load();
    result.active_generation = s.active_generation.load();
    result.failed_generation = s.failed_generation.load();
    result.epoch = s.active_serial.load();
    result.gpu_selected = s.gpu_selected.load();
    result.cpu_fallback = s.cpu_fallback.load();
    result.gpu_misses = s.misses.load();
    result.reset_count = s.requested_epoch.load();
    return result;
}
} // namespace spectr::experimental
