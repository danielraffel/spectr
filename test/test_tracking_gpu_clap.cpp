// Bounded host proof for the Tracking opt-in at the real CLAP binary boundary.
//
// The Tracking choice is session state rather than a CLAP parameter. This host
// therefore saves a normal CLAP state stream, changes only the documented
// plugin-state boolean inside its Pulp envelope, recomputes the envelope CRC,
// and restores it through clap_plugin_state_t::load. Audio and latency then
// cross only the standard CLAP ports.

#include <clap/clap.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

const void* host_extension(const clap_host_t*, const char*) { return nullptr; }
void host_noop(const clap_host_t*) {}

clap_host_t make_host() {
    return {CLAP_VERSION, nullptr, "Spectr Tracking GPU host proof", "Pulp",
            "", "1", host_extension, host_noop, host_noop, host_noop};
}

struct Events {
    std::vector<clap_event_param_value_t> values;
    clap_input_events_t input{};

    Events() {
        input.ctx = this;
        input.size = [](const clap_input_events_t* list) {
            return static_cast<std::uint32_t>(
                static_cast<const Events*>(list->ctx)->values.size());
        };
        input.get = [](const clap_input_events_t* list, std::uint32_t index)
            -> const clap_event_header_t* {
            const auto& values = static_cast<const Events*>(list->ctx)->values;
            return index < values.size() ? &values[index].header : nullptr;
        };
    }

    void add(clap_id id, double value, std::uint32_t time = 0) {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        values.push_back(event);
    }
};

const clap_output_events_t discard_events{
    nullptr, [](const clap_output_events_t*, const clap_event_header_t*) {
        return true;
    }};

struct StreamOut {
    std::vector<std::uint8_t> bytes;
};

std::int64_t stream_write(const clap_ostream_t* stream, const void* data,
                          std::uint64_t size) {
    auto& output = *static_cast<StreamOut*>(stream->ctx);
    const auto* begin = static_cast<const std::uint8_t*>(data);
    output.bytes.insert(output.bytes.end(), begin, begin + size);
    return static_cast<std::int64_t>(size);
}

struct StreamIn {
    const std::vector<std::uint8_t>* bytes = nullptr;
    std::size_t offset = 0;
};

std::int64_t stream_read(const clap_istream_t* stream, void* data,
                         std::uint64_t size) {
    auto& input = *static_cast<StreamIn*>(stream->ctx);
    const auto remaining = input.bytes->size() - input.offset;
    const auto count = std::min<std::size_t>(remaining, size);
    std::memcpy(data, input.bytes->data() + input.offset, count);
    input.offset += count;
    return static_cast<std::int64_t>(count);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
    require(offset + 4 <= bytes.size(), "truncated CLAP state envelope");
    return std::uint32_t(bytes[offset])
        | (std::uint32_t(bytes[offset + 1]) << 8)
        | (std::uint32_t(bytes[offset + 2]) << 16)
        | (std::uint32_t(bytes[offset + 3]) << 24);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    require(offset + 4 <= bytes.size(), "truncated CLAP state envelope");
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> tracking_state(
    const std::vector<std::uint8_t>& source, bool enabled) {
    auto bytes = source;
    require(bytes.size() >= 20 && std::memcmp(bytes.data(), "PLST", 4) == 0,
            "CLAP state is not the Pulp versioned envelope");
    const auto version = read_u32(bytes, 4);
    const auto store_size = read_u32(bytes, 8);
    const auto plugin_size = read_u32(bytes, 12);
    const std::size_t plugin_offset = 16 + store_size;
    require(version == 1 && plugin_offset + plugin_size + 4 == bytes.size(),
            "unsupported or malformed Pulp CLAP state envelope");

    std::string plugin(reinterpret_cast<const char*>(bytes.data() + plugin_offset),
                       plugin_size);
    const auto key = plugin.find("\"tracking_gpu_processing\"");
    require(key != std::string::npos,
            "CLAP state stream did not expose tracking_gpu_processing");
    const auto colon = plugin.find(':', key);
    require(colon != std::string::npos, "malformed Tracking state member");
    auto value = colon + 1;
    while (value < plugin.size()
           && std::isspace(static_cast<unsigned char>(plugin[value])))
        ++value;
    const auto current = plugin.compare(value, 4, "true") == 0;
    require(plugin.compare(value, current ? 4 : 5, current ? "true" : "false") == 0,
            "malformed Tracking state boolean");
    if (!(enabled && current)) {
        // Keep the envelope payload length stable when changing the default
        // false value to true. JSON permits the trailing space.
        const auto wanted = enabled ? "true " : "false";
        plugin.replace(value, current ? 4 : 5, wanted);
    }
    require(plugin.size() == plugin_size, "Tracking state patch changed payload size");
    std::copy(plugin.begin(), plugin.end(), bytes.begin() + plugin_offset);
    write_u32(bytes, bytes.size() - 4,
              ::crc32(0L, reinterpret_cast<const Bytef*>(bytes.data()),
                      static_cast<uInt>(bytes.size() - 4)));
    return bytes;
}

bool contains_tracking_enabled(const std::vector<std::uint8_t>& bytes) {
    constexpr std::string_view needle = "\"tracking_gpu_processing\": true";
    const auto version = read_u32(bytes, 4);
    const auto store_size = read_u32(bytes, 8);
    const auto plugin_size = read_u32(bytes, 12);
    const std::size_t plugin_offset = 16 + store_size;
    require(version == 1 && plugin_offset + plugin_size + 4 == bytes.size(),
            "unsupported or malformed Pulp CLAP state envelope");
    const auto begin = reinterpret_cast<const char*>(bytes.data() + plugin_offset);
    const std::string_view plugin(begin, plugin_size);
    return std::search(plugin.begin(), plugin.end(), needle.begin(), needle.end())
        != plugin.end();
}

struct Module {
    void* library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_latency_t* latency = nullptr;
    bool initialized = false;
    bool active = false;
    bool processing = false;

    Module() = default;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&& other) noexcept { *this = std::move(other); }
    Module& operator=(Module&& other) noexcept {
        if (this == &other) return *this;
        library = std::exchange(other.library, nullptr);
        entry = std::exchange(other.entry, nullptr);
        factory = std::exchange(other.factory, nullptr);
        plugin = std::exchange(other.plugin, nullptr);
        params = std::exchange(other.params, nullptr);
        state = std::exchange(other.state, nullptr);
        latency = std::exchange(other.latency, nullptr);
        initialized = std::exchange(other.initialized, false);
        active = std::exchange(other.active, false);
        processing = std::exchange(other.processing, false);
        return *this;
    }

    ~Module() { close(); }

    void close() {
        if (plugin) {
            if (processing) {
                plugin->stop_processing(plugin);
            }
            if (active) plugin->deactivate(plugin);
            plugin->destroy(plugin);
        }
        if (initialized) entry->deinit();
        if (library) dlclose(library);
        plugin = nullptr;
        library = nullptr;
        initialized = active = processing = false;
    }
};

Module load_module(const std::filesystem::path& bundle,
                   const clap_host_t& host) {
    Module module;
    const auto binary = bundle / "Contents" / "MacOS" / bundle.stem();
    module.library = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    require(module.library != nullptr, "CLAP candidate binary could not be loaded");
    module.entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(module.library, "clap_entry"));
    require(module.entry != nullptr, "CLAP candidate has no clap_entry");
    require(module.entry->init(bundle.c_str()), "CLAP entry init failed");
    module.initialized = true;
    module.factory = static_cast<const clap_plugin_factory_t*>(
        module.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(module.factory && module.factory->get_plugin_count(module.factory) == 1,
            "CLAP candidate factory identity mismatch");
    const auto* descriptor = module.factory->get_plugin_descriptor(module.factory, 0);
    require(descriptor != nullptr, "CLAP candidate descriptor missing");
    module.plugin = module.factory->create_plugin(module.factory, &host, descriptor->id);
    require(module.plugin && module.plugin->init(module.plugin),
            "CLAP candidate plugin init failed");
    module.params = static_cast<const clap_plugin_params_t*>(
        module.plugin->get_extension(module.plugin, CLAP_EXT_PARAMS));
    module.state = static_cast<const clap_plugin_state_t*>(
        module.plugin->get_extension(module.plugin, CLAP_EXT_STATE));
    module.latency = static_cast<const clap_plugin_latency_t*>(
        module.plugin->get_extension(module.plugin, CLAP_EXT_LATENCY));
    require(module.params && module.state && module.latency,
            "CLAP candidate lacks params/state/latency extensions");
    return module;
}

std::vector<std::uint8_t> save_state(const Module& module) {
    StreamOut output;
    const clap_ostream_t stream{&output, stream_write};
    require(module.state->save(module.plugin, &stream), "CLAP state save failed");
    require(!output.bytes.empty(), "CLAP state save returned an empty stream");
    return output.bytes;
}

void load_state(const Module& module, const std::vector<std::uint8_t>& bytes) {
    StreamIn input{&bytes, 0};
    const clap_istream_t stream{&input, stream_read};
    require(module.state->load(module.plugin, &stream), "CLAP state load failed");
}

clap_id parameter(const Module& module, std::string_view name) {
    for (std::uint32_t i = 0; i < module.params->count(module.plugin); ++i) {
        clap_param_info_t info{};
        require(module.params->get_info(module.plugin, i, &info),
                "CLAP parameter info failed");
        if (name == info.name) return info.id;
    }
    return CLAP_INVALID_ID;
}

void flush_surface(const Module& module, clap_id mix, clap_id output,
                   std::span<const clap_id> gains) {
    Events events;
    events.add(mix, 100.0);
    events.add(output, 0.0);
    for (const auto id : gains) events.add(id, -24.0);
    module.params->flush(module.plugin, &events.input, &discard_events);
}

struct Capture {
    std::vector<float> left;
    std::vector<float> right;
};

Capture render(Module& module, unsigned frames, unsigned block,
               clap_id mix, clap_id output, std::span<const clap_id> gains,
               unsigned expected_latency) {
    flush_surface(module, mix, output, gains);
    require(module.plugin->activate(module.plugin, 48000.0, block, block),
            "CLAP activate failed");
    module.active = true;
    require(module.plugin->start_processing(module.plugin),
            "CLAP start_processing failed");
    module.processing = true;
    const auto reported = module.latency->get(module.plugin);
    require(reported == expected_latency,
            "CLAP latency extension did not report the active mode latency");
    Capture capture{std::vector<float>(frames), std::vector<float>(frames)};
    std::array<float, 128> input_left{}, input_right{}, output_left{}, output_right{};
    float* input_ptrs[]{input_left.data(), input_right.data()};
    float* output_ptrs[]{output_left.data(), output_right.data()};
    clap_audio_buffer_t input{input_ptrs, nullptr, 2, 0, 0};
    clap_audio_buffer_t output_buffer{output_ptrs, nullptr, 2, 0, 0};
    const auto start = std::chrono::steady_clock::now();
    Events empty;
    constexpr double pi = 3.14159265358979323846;
    for (unsigned offset = 0; offset < frames; offset += block) {
        const auto count = std::min(block, frames - offset);
        for (unsigned i = 0; i < count; ++i) {
            const auto sample = offset + i;
            const auto t = static_cast<double>(sample) / 48000.0;
            input_left[i] = static_cast<float>(0.22 * std::sin(2.0 * pi * 223.0 * t)
                + 0.17 * std::sin(2.0 * pi * 977.0 * t)
                + 0.11 * std::sin(2.0 * pi * 3211.0 * t));
            input_right[i] = 0.73f * input_left[i];
        }
        for (unsigned i = count; i < block; ++i)
            input_left[i] = input_right[i] = 0.0f;
        std::fill(output_left.begin(), output_left.end(), 0.0f);
        std::fill(output_right.begin(), output_right.end(), 0.0f);
        clap_process_t process{};
        process.steady_time = offset;
        process.frames_count = count;
        process.audio_inputs = &input;
        process.audio_inputs_count = 1;
        process.audio_outputs = &output_buffer;
        process.audio_outputs_count = 1;
        process.in_events = &empty.input;
        process.out_events = &discard_events;
        require(module.plugin->process(module.plugin, &process) != CLAP_PROCESS_ERROR,
                "CLAP process failed");
        for (unsigned i = 0; i < count; ++i)
            require(std::isfinite(output_left[i]) && std::isfinite(output_right[i]),
                    "CLAP process returned a non-finite output sample");
        std::copy_n(output_left.data(), count, capture.left.data() + offset);
        std::copy_n(output_right.data(), count, capture.right.data() + offset);
        // Keep the GPU worker on the same bounded wall-clock budget as a host.
        std::this_thread::sleep_until(start + std::chrono::nanoseconds(
            std::uint64_t(offset + count) * 1000000000ull / 48000ull));
    }
    require(module.latency->get(module.plugin) == expected_latency,
            "CLAP latency changed while processing");
    module.plugin->stop_processing(module.plugin);
    module.processing = false;
    module.plugin->deactivate(module.plugin);
    module.active = false;
    return capture;
}

double maximum_difference(const Capture& earlier, const Capture& later,
                          unsigned shift) {
    double maximum = 0.0;
    for (std::size_t i = 0; i + shift < earlier.left.size(); ++i) {
        maximum = std::max(maximum, std::abs(double(earlier.left[i])
                                            - later.left[i + shift]));
        maximum = std::max(maximum, std::abs(double(earlier.right[i])
                                            - later.right[i + shift]));
    }
    return maximum;
}

double peak(const Capture& capture) {
    double value = 0.0;
    for (const auto sample : capture.left) value = std::max(value, std::abs(double(sample)));
    for (const auto sample : capture.right) value = std::max(value, std::abs(double(sample)));
    return value;
}

Capture dry_capture(unsigned frames, unsigned delay) {
    Capture dry{std::vector<float>(frames), std::vector<float>(frames)};
    constexpr double pi = 3.14159265358979323846;
    for (unsigned sample = 0; sample + delay < frames; ++sample) {
        const auto t = static_cast<double>(sample) / 48000.0;
        const auto value = static_cast<float>(
            0.22 * std::sin(2.0 * pi * 223.0 * t)
            + 0.17 * std::sin(2.0 * pi * 977.0 * t)
            + 0.11 * std::sin(2.0 * pi * 3211.0 * t));
        dry.left[sample + delay] = value;
        dry.right[sample + delay] = 0.73f * value;
    }
    return dry;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: Spectr-tracking-gpu-clap-host /absolute/path/Candidate.clap\n";
        return 64;
    }
    try {
        const std::filesystem::path bundle = argv[1];
        require(std::filesystem::is_directory(bundle), "candidate .clap bundle is missing");
        const auto host = make_host();
        Module cpu = load_module(bundle, host);
        const auto mix = parameter(cpu, "Mix");
        const auto output = parameter(cpu, "Output");
        require(mix != CLAP_INVALID_ID && output != CLAP_INVALID_ID,
                "candidate control surface lacks Mix or Output");
        std::vector<clap_id> gains;
        for (std::uint32_t i = 0; i < cpu.params->count(cpu.plugin); ++i) {
            clap_param_info_t info{};
            require(cpu.params->get_info(cpu.plugin, i, &info), "CLAP parameter info failed");
            if (std::strncmp(info.name, "Band ", 5) == 0
                && std::strstr(info.name, " Gain") != nullptr)
                gains.push_back(info.id);
        }
        require(!gains.empty(), "candidate control surface lacks band gains");

        const auto saved_cpu = save_state(cpu);
        const auto saved_gpu = tracking_state(saved_cpu, true);
        Module gpu = load_module(bundle, host);
        load_state(gpu, saved_gpu);
        const auto recalled_gpu = save_state(gpu);
        require(contains_tracking_enabled(recalled_gpu),
                "CLAP state roundtrip did not retain Tracking GPU selection");

        constexpr unsigned block = 128;
        constexpr unsigned frames = 32768;
        const auto cpu_capture = render(cpu, frames, block, mix, output, gains, 64);
        const auto gpu_capture = render(gpu, frames, block, mix, output, gains, 192);
        require(peak(cpu_capture) > 1.0e-4 && peak(gpu_capture) > 1.0e-4,
                "Tracking CLAP broadband capture was silent");
        const auto aligned_max_error = maximum_difference(cpu_capture, gpu_capture, 128);
        require(aligned_max_error < 8.0e-4,
                "Tracking GPU wet output did not align with CPU output");
        const auto wet_vs_dry_max_delta =
            maximum_difference(dry_capture(frames, 64), cpu_capture, 0);
        require(wet_vs_dry_max_delta > 1.0e-4,
                "Tracking wet capture was unity or empty");

        Module restored_cpu = load_module(bundle, host);
        load_state(restored_cpu, saved_cpu);
        require(restored_cpu.latency->get(restored_cpu.plugin) == 64,
                "restored CPU state did not return to 64 samples");
        std::cout << "tracking_gpu_clap_host=passed cpu_latency=64 gpu_latency=192"
                  << " gpu_recalled=true peak=" << peak(gpu_capture)
                  << " aligned_max_error=" << aligned_max_error
                  << " wet_vs_dry_max_delta=" << wet_vs_dry_max_delta << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tracking_gpu_clap_host=failed: " << error.what() << '\n';
        return 1;
    }
}
