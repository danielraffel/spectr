// The built CLAP must report the same parameter values whether a host sets
// them through clap_plugin_params::flush() or through process().
//
// This is clap-validator's `state-reproducibility-flush` check, reproduced
// over the CLAP ABI against the exact built bundle: one instance receives a
// batch of random values for every writable parameter as process() input
// events, a second receives the identical batch through params.flush(), and
// every get_value() must agree. The legacy LFO Depth (4003/4013) and Target
// (4004) lanes are commands that fan out onto per-target routing lanes; a
// batch that sets those lanes explicitly must keep the explicit values on
// both paths, whenever the parameter-sync worker happens to run.

#include <catch2/catch_test_macros.hpp>

#if defined(SPECTR_HAVE_TEST_CLAP)

#include <clap/clap.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "spectr/param_surface.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

struct EventList {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t in{};
    clap_output_events_t out{};

    EventList() {
        in.ctx = this;
        in.size = [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<uint32_t>(
                static_cast<const EventList*>(list->ctx)->events.size());
        };
        in.get = [](const clap_input_events_t* list,
                    uint32_t index) -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return index < self->events.size() ? &self->events[index].header
                                               : nullptr;
        };
        out.ctx = this;
        out.try_push = [](const clap_output_events_t*,
                          const clap_event_header_t*) { return true; };
    }
};

clap_host_t make_host() {
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "spectr-flush-parity";
    host.vendor = "spectr-tests";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host_t*, const char*) -> const void* {
        return nullptr;
    };
    host.request_restart = [](const clap_host_t*) {};
    host.request_process = [](const clap_host_t*) {};
    host.request_callback = [](const clap_host_t*) {};
    return host;
}

struct Library {
#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    std::string plugin_id;

    explicit Library(const std::filesystem::path& bundle) {
#if defined(_WIN32)
        const auto binary = bundle;
        handle = LoadLibraryW(binary.c_str());
        if (!handle) return;
        entry = reinterpret_cast<const clap_plugin_entry_t*>(
            GetProcAddress(handle, "clap_entry"));
#else
        const auto binary = bundle / "Contents" / "MacOS" / bundle.stem();
        handle = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
        if (!handle) return;
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
#endif
        const auto bundle_string = bundle.string();
        if (!entry || !entry->init(bundle_string.c_str())) {
            entry = nullptr;
            return;
        }
        factory = static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (factory && factory->get_plugin_count(factory) > 0)
            plugin_id = factory->get_plugin_descriptor(factory, 0)->id;
    }
    ~Library() {
        if (entry) entry->deinit();
        // Left loaded: the plugin may own threads that outlive deinit by a
        // scheduling quantum, and unmapping their code under them is not
        // what this test measures.
    }
};

using Values = std::map<clap_id, double>;

struct Outcome {
    std::map<clap_id, double> values;
    std::vector<std::uint8_t> state;
};

struct Instance {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;

    Instance(const Library& lib, const clap_host_t* host) {
        plugin = lib.factory->create_plugin(lib.factory, host, lib.plugin_id.c_str());
        if (!plugin || !plugin->init(plugin)) {
            plugin = nullptr;
            return;
        }
        params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    }
    ~Instance() {
        if (plugin) plugin->destroy(plugin);
    }

    std::vector<std::uint8_t> save_state() const {
        const auto* state = static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE));
        std::vector<std::uint8_t> bytes;
        if (!state) return bytes;
        clap_ostream_t stream{};
        stream.ctx = &bytes;
        stream.write = [](const clap_ostream_t* s, const void* data,
                          uint64_t size) -> int64_t {
            auto* out = static_cast<std::vector<std::uint8_t>*>(s->ctx);
            const auto* begin = static_cast<const std::uint8_t*>(data);
            out->insert(out->end(), begin, begin + size);
            return static_cast<int64_t>(size);
        };
        if (!state->save(plugin, &stream)) bytes.clear();
        return bytes;
    }

    Values read() const {
        Values values;
        const uint32_t count = params->count(plugin);
        for (uint32_t i = 0; i < count; ++i) {
            clap_param_info_t info{};
            params->get_info(plugin, i, &info);
            double value = 0.0;
            if (params->get_value(plugin, info.id, &value)) values[info.id] = value;
        }
        return values;
    }
};

// Random values for every writable parameter, drawn the way clap-validator
// draws them: an integer in range for a stepped parameter, a uniform real
// otherwise (bypass excepted, below).
std::vector<clap_event_param_value_t> random_batch(const Instance& instance,
                                                   std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<clap_event_param_value_t> batch;
    const uint32_t count = instance.params->count(instance.plugin);
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info_t info{};
        instance.params->get_info(instance.plugin, i, &info);
        if (info.flags & CLAP_PARAM_IS_READONLY) continue;
        double value;
        if (info.flags & CLAP_PARAM_IS_BYPASS) {
            // Held off: a bypassed instance skips the block that hands the
            // batch to the sync worker, and the comparison would then read
            // values nothing reconciled.
            value = info.min_value;
        } else if (info.flags & CLAP_PARAM_IS_STEPPED) {
            std::uniform_int_distribution<long> pick(
                static_cast<long>(std::lround(info.min_value)),
                static_cast<long>(std::lround(info.max_value)));
            value = static_cast<double>(pick(rng));
        } else {
            std::uniform_real_distribution<double> pick(info.min_value, info.max_value);
            value = pick(rng);
        }
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = 0;
        event.param_id = info.id;
        event.cookie = info.cookie;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        batch.push_back(event);
    }
    return batch;
}

constexpr uint32_t kFrames = 512;

// One block of silence through an active, processing instance, carrying
// @p batch as its input events.
void process_block(const Instance& instance,
                   const std::vector<clap_event_param_value_t>& batch) {
    std::array<std::vector<float>, 2> in_ch{std::vector<float>(kFrames, 0.0f),
                                            std::vector<float>(kFrames, 0.0f)};
    std::array<std::vector<float>, 2> out_ch{std::vector<float>(kFrames, 0.0f),
                                             std::vector<float>(kFrames, 0.0f)};
    float* in_ptrs[2] = {in_ch[0].data(), in_ch[1].data()};
    float* out_ptrs[2] = {out_ch[0].data(), out_ch[1].data()};
    clap_audio_buffer_t in_buf{};
    in_buf.data32 = in_ptrs;
    in_buf.channel_count = 2;
    clap_audio_buffer_t out_buf{};
    out_buf.data32 = out_ptrs;
    out_buf.channel_count = 2;

    EventList events;
    events.events = batch;
    clap_process_t process{};
    process.steady_time = 0;
    process.frames_count = kFrames;
    process.transport = nullptr;
    process.audio_inputs = &in_buf;
    process.audio_inputs_count = 1;
    process.audio_outputs = &out_buf;
    process.audio_outputs_count = 1;
    process.in_events = &events.in;
    process.out_events = &events.out;
    REQUIRE(instance.plugin->process(instance.plugin, &process) != CLAP_PROCESS_ERROR);
}

void flush(const Instance& instance,
           const std::vector<clap_event_param_value_t>& batch) {
    EventList events;
    events.events = batch;
    instance.params->flush(instance.plugin, &events.in, &events.out);
}

// Let any parameter-sync worker a batch woke run to completion, so the
// comparison sees settled values rather than whichever write came first.
void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(300)); }

enum class Path { process, flush_inactive, flush_then_process };

Outcome apply_batch(const Library& lib, const clap_host_t* host, Path path,
                   const std::vector<clap_event_param_value_t>& batch) {
    Instance instance(lib, host);
    REQUIRE(instance.plugin != nullptr);
    REQUIRE(instance.params != nullptr);
    if (path == Path::flush_inactive) {
        flush(instance, batch);
        settle();
        return {instance.read(), instance.save_state()};
    }
    REQUIRE(instance.plugin->activate(instance.plugin, 48000.0, 1, kFrames));
    REQUIRE(instance.plugin->start_processing(instance.plugin));
    if (path == Path::process) {
        process_block(instance, batch);
    } else {
        // An active plugin's flush, then the next block, which is where the
        // drift sweep hands the batch to the sync worker.
        flush(instance, batch);
        process_block(instance, {});
    }
    settle();
    process_block(instance, {});
    instance.plugin->stop_processing(instance.plugin);
    instance.plugin->deactivate(instance.plugin);
    settle();
    return {instance.read(), instance.save_state()};
}

} // namespace

TEST_CASE("Built Spectr CLAP reports the same parameter values after "
          "params.flush() as after process()") {
    const std::filesystem::path bundle{SPECTR_TEST_CLAP_PATH};
    INFO("CLAP artifact " << bundle.string());
    REQUIRE(std::filesystem::exists(bundle));
    Library lib(bundle);
    REQUIRE(lib.handle != nullptr);
    REQUIRE(lib.entry != nullptr);
    REQUIRE(lib.factory != nullptr);
    REQUIRE(!lib.plugin_id.empty());
    const clap_host_t host = make_host();

    // Several seeds: a single draw can leave a target disabled, and the fan
    // out only touches enabled targets.
    for (std::uint32_t seed : {1u, 2u, 3u, 0x6588d61u, 0xC1A9u}) {
        INFO("seed " << seed);
        std::vector<clap_event_param_value_t> batch;
        {
            Instance probe(lib, &host);
            REQUIRE(probe.plugin != nullptr);
            batch = random_batch(probe, seed);
        }
        REQUIRE(batch.size() > spectr::kRouteParamCount);
        const Outcome by_process = apply_batch(lib, &host, Path::process, batch);
        const Outcome by_flush = apply_batch(lib, &host, Path::flush_inactive, batch);
        const Outcome by_flush_active =
            apply_batch(lib, &host, Path::flush_then_process, batch);
        const Values& processed = by_process.values;
        const Values& flushed = by_flush.values;
        const Values& flushed_active = by_flush_active.values;

        // clap-validator's second half: the saved state is byte-identical.
        REQUIRE(!by_process.state.empty());
        CHECK(by_flush.state == by_process.state);
        CHECK(by_flush_active.state == by_process.state);

        std::size_t mismatches = 0;
        std::string mismatch_report;
        for (const Values* other : {&flushed, &flushed_active}) {
            REQUIRE(processed.size() == other->size());
            for (const auto& [id, value] : processed) {
                const auto it = other->find(id);
                REQUIRE(it != other->end());
                if (it->second != value) {
                    ++mismatches;
                    mismatch_report += "param " + std::to_string(id)
                        + ": process " + std::to_string(value) + " vs flush "
                        + std::to_string(it->second) + "\n";
                }
            }
        }
        INFO(mismatch_report);
        CHECK(mismatches == 0);

        // The per-target routing lanes hold exactly what the batch wrote on
        // every path: the legacy command lanes in the same batch never
        // overwrite them.
        std::size_t clobbered = 0;
        std::string clobber_report;
        for (const auto& event : batch) {
            if (!spectr::is_lfo_route_param(event.param_id)) continue;
            for (const Values* side : {&processed, &flushed, &flushed_active}) {
                const auto it = side->find(event.param_id);
                REQUIRE(it != side->end());
                if (std::abs(it->second - event.value) > 1e-6) {
                    ++clobbered;
                    clobber_report += "route param " + std::to_string(event.param_id)
                        + " wrote " + std::to_string(event.value) + " reads "
                        + std::to_string(it->second) + "\n";
                }
            }
        }
        INFO(clobber_report);
        CHECK(clobbered == 0);
    }
}

TEST_CASE("Built Spectr CLAP still fans a lone legacy Depth move out to its "
          "enabled targets") {
    // Old automation moves only the LFO-level Depth lane. That must keep
    // working as the command it always was: every enabled target of that LFO
    // takes the value, disabled ones keep their own.
    const std::filesystem::path bundle{SPECTR_TEST_CLAP_PATH};
    REQUIRE(std::filesystem::exists(bundle));
    Library lib(bundle);
    REQUIRE(lib.factory != nullptr);
    const clap_host_t host = make_host();

    auto event_for = [](clap_id id, double value) {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        return event;
    };

    Instance instance(lib, &host);
    REQUIRE(instance.params != nullptr);
    REQUIRE(instance.plugin->activate(instance.plugin, 48000.0, 1, kFrames));
    REQUIRE(instance.plugin->start_processing(instance.plugin));
    const auto enabled_on = spectr::lfo_route_enabled_param_id(0, 4);   // Band shift
    const auto enabled_off = spectr::lfo_route_enabled_param_id(0, 9);  // Mix
    const auto depth_on = spectr::lfo_route_amount_param_id(0, 4);
    const auto depth_off = spectr::lfo_route_amount_param_id(0, 9);
    process_block(instance, {event_for(enabled_on, 1.0), event_for(enabled_off, 0.0),
                             event_for(depth_on, 0.25), event_for(depth_off, 0.4)});
    settle();
    process_block(instance, {});
    // The legacy lane alone, as old automation plays it.
    process_block(instance, {event_for(spectr::kParamLfoDepth, 0.8)});
    settle();
    process_block(instance, {});
    instance.plugin->stop_processing(instance.plugin);
    instance.plugin->deactivate(instance.plugin);
    const Values values = instance.read();
    INFO("Depth lane reads " << values.at(spectr::kParamLfoDepth)
         << ", enabled target " << values.at(enabled_on) << "/" << values.at(depth_on)
         << ", disabled target " << values.at(enabled_off) << "/" << values.at(depth_off));
    CHECK(std::abs(values.at(depth_on) - 0.8) < 1e-6);
    CHECK(std::abs(values.at(depth_off) - 0.4) < 1e-6);
}

#endif  // SPECTR_HAVE_TEST_CLAP
