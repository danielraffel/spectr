#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/audio/buffer.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/format/quirk_apply.hpp>
#include <pulp/host/plugin_slot.hpp>
#include <pulp/midi/buffer.hpp>

#include "spectr/spectr.hpp"
#include "spectr/param_surface.hpp"

#if defined(SPECTR_HAVE_TEST_AU)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::pair<std::uint32_t, std::string>> expected_host_parameters() {
    std::vector<std::pair<std::uint32_t, std::string>> expected{
        {pulp::format::kSynthesizedBypassParamId, "Bypass"},
        {spectr::kMix, "Mix"},
        {spectr::kOutputTrim, "Output"},
    };
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band) {
        char name[32];
        std::snprintf(name, sizeof(name), "Band %02zu Gain", band + 1);
        expected.emplace_back(spectr::band_gain_param_id(band), name);
    }
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band) {
        char name[32];
        std::snprintf(name, sizeof(name), "Band %02zu Mute", band + 1);
        expected.emplace_back(spectr::band_mute_param_id(band), name);
    }
    expected.insert(expected.end(), {
        {spectr::kParamMorph, "A/B Morph"},
        {spectr::kParamViewportCenter, "Viewport Center"},
        {spectr::kParamViewportWidth, "Viewport Width"},
        {spectr::kParamBandCount, "Band Count"},
        {spectr::kParamMotionMode, "Motion Mode"},
        {spectr::kParamAnalyzerMode, "Analyzer Mode"},
        {spectr::kParamEditMode, "Edit Mode"},
        {spectr::kParamVisualization, "Visualization"},
        {spectr::kParamLfoEnabled, "LFO Enabled"},
        {spectr::kParamLfoShape, "LFO Shape"},
        {spectr::kParamLfoRate, "LFO Rate"},
        {spectr::kParamLfoDepth, "LFO Depth"},
        {spectr::kParamLfoTarget, "LFO Target"},
        {spectr::kParamLfo2Enabled, "LFO 2 Enabled"},
        {spectr::kParamLfo2Shape, "LFO 2 Shape"},
        {spectr::kParamLfo2Rate, "LFO 2 Rate"},
        {spectr::kParamLfo2Depth, "LFO 2 Depth"},
    });
    return expected;
}

// `loader_reports_bypass_flag` says whether the loading format can tell a host
// which parameter is the bypass. CLAP and VST3 carry that in the parameter
// descriptor. AU v2 does not: it carries bypass as
// `kAudioUnitProperty_BypassEffect`, a property, so the parameter Pulp
// synthesizes reaches an AU host as an ordinary parameter and Pulp's AU loader
// has nothing to derive `ParamFlags::is_bypass` from. The AU case therefore
// still requires the bypass parameter to be present under its exact id and
// name, and asserts the flag is absent rather than quietly skipping it.
void check_host_parameter_contract(
    const std::vector<pulp::host::HostParamInfo>& parameters,
    bool loader_reports_bypass_flag) {
    const auto expected = expected_host_parameters();
    REQUIRE(expected.size() == spectr::kSurfaceParamCount + 1);
    REQUIRE(parameters.size() == expected.size());

    std::vector<std::uint32_t> actual_ids;
    actual_ids.reserve(parameters.size());
    for (const auto& parameter : parameters)
        actual_ids.push_back(parameter.id);
    std::ranges::sort(actual_ids);
    CHECK(std::ranges::adjacent_find(actual_ids) == actual_ids.end());

    for (const auto& [id, name] : expected) {
        const auto it = std::ranges::find(parameters, id,
            &pulp::host::HostParamInfo::id);
        INFO("expected host parameter " << name << " id=" << id);
        REQUIRE(it != parameters.end());
        CHECK(it->name == name);
        CHECK(it->flags.is_bypass
              == (loader_reports_bypass_flag
                  && id == pulp::format::kSynthesizedBypassParamId));
        if (id != pulp::format::kSynthesizedBypassParamId) {
            CHECK(it->flags.automatable);
            CHECK_FALSE(it->flags.read_only);
            CHECK_FALSE(it->flags.hidden);
        }
    }

    // The static surface stays complete when the current layout shows only
    // 32 bands. Hidden slots are host-visible state lanes whose DSP effect is
    // deliberately inert until their band enters the visible layout.
    CHECK(std::ranges::find(parameters, spectr::band_gain_param_id(63),
          &pulp::host::HostParamInfo::id) != parameters.end());
    CHECK(std::ranges::find(parameters, spectr::band_mute_param_id(63),
          &pulp::host::HostParamInfo::id) != parameters.end());
}

std::vector<std::uint8_t> make_all_muted_state() {
    pulp::format::HeadlessHost author(spectr::create_spectr);
    author.prepare(48000.0, 512);
    auto* processor = dynamic_cast<spectr::Spectr*>(author.processor());
    REQUIRE(processor != nullptr);
    spectr::BandField muted;
    for (auto& band : muted.bands) band.muted = true;
    processor->replace_field(muted);
    auto state = author.save_state();
    REQUIRE_FALSE(state.empty());
    return state;
}

std::vector<std::uint8_t> make_three_island_state() {
    pulp::format::HeadlessHost author(spectr::create_spectr);
    author.prepare(48000.0, 512);
    auto* processor = dynamic_cast<spectr::Spectr*>(author.processor());
    REQUIRE(processor != nullptr);
    spectr::BandField islands;
    for (auto& band : islands.bands) band.muted = true;
    for (const float hz : {304.6875f, 1201.171875f, 3498.046875f})
        islands.bands[processor->viewport().band_for_hz(hz, 32)].muted = false;
    REQUIRE(processor->replace_processing_state(
        islands, spectr::Viewport{}, spectr::Layout::Bands32));
    auto state = author.save_state();
    REQUIRE_FALSE(state.empty());
    return state;
}

void check_three_islands(pulp::host::PluginSlot& slot) {
    if constexpr (SPECTR_FFT_SIZE < 8192) return;

    constexpr double pi = 3.14159265358979323846;
    constexpr int sample_rate = 48000;
    constexpr int block_size = 512;
    constexpr int analysis_size = SPECTR_FFT_SIZE;
    constexpr int bin_scale = analysis_size / 8192;
    constexpr double amplitude = 0.04;
    const std::array<int, 3> kept_bins{
        52 * bin_scale, 205 * bin_scale, 597 * bin_scale};
    const std::array<int, 3> rejected_bins{
        102 * bin_scale, 376 * bin_scale, 1195 * bin_scale};
    const auto hz_for_bin = [](int bin) {
        return static_cast<double>(bin) * sample_rate / analysis_size;
    };
    REQUIRE(slot.restore_state(make_three_island_state()));

    const auto total_samples = static_cast<std::size_t>(
        SPECTR_EXPECTED_LATENCY + SPECTR_FFT_SIZE * 4);
    const auto padded_samples =
        ((total_samples + block_size - 1) / block_size) * block_size;
    std::vector<float> source_left(padded_samples, 0.0f);
    std::vector<float> rendered_left(padded_samples, 0.0f);
    std::vector<float> rendered_right(padded_samples, 0.0f);
    for (std::size_t sample = 0; sample < padded_samples; ++sample) {
        double value = 0.0;
        for (const auto bin : kept_bins)
            value += amplitude * std::sin(
                2.0 * pi * hz_for_bin(bin) * sample / sample_rate);
        for (const auto bin : rejected_bins)
            value += amplitude * std::sin(
                2.0 * pi * hz_for_bin(bin) * sample / sample_rate);
        source_left[sample] = static_cast<float>(value);
    }

    std::vector<float> left(block_size), right(block_size);
    std::vector<float> out_left(block_size), out_right(block_size);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {out_left.data(), out_right.data()};
    auto input = pulp::audio::BufferView<const float>(inputs, 2, block_size);
    auto output = pulp::audio::BufferView<float>(outputs, 2, block_size);
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::host::ParameterEventQueue parameter_events;
    for (std::size_t offset = 0; offset < padded_samples; offset += block_size) {
        std::copy_n(source_left.data() + offset, block_size, left.data());
        for (int sample = 0; sample < block_size; ++sample)
            right[sample] = -0.6f * left[sample];
        slot.process(output, input, midi_in, midi_out, parameter_events, block_size);
        std::copy_n(out_left.data(), block_size, rendered_left.data() + offset);
        std::copy_n(out_right.data(), block_size, rendered_right.data() + offset);
    }

    const auto output_start = padded_samples - analysis_size;
    const auto source_start = output_start
                            - static_cast<std::size_t>(SPECTR_EXPECTED_LATENCY);
    const auto projection = [=](const std::vector<float>& signal,
                                std::size_t start,
                                int bin) {
        std::complex<double> sum{};
        for (int sample = 0; sample < analysis_size; ++sample) {
            const auto phase = -2.0 * pi * bin * sample / analysis_size;
            sum += static_cast<double>(
                       signal[start + static_cast<std::size_t>(sample)])
                 * std::complex<double>(std::cos(phase), std::sin(phase));
        }
        return sum * (2.0 / analysis_size);
    };
    for (const auto bin : kept_bins) {
        const auto input_bin = projection(source_left, source_start, bin);
        const auto left_bin = projection(rendered_left, output_start, bin);
        const auto right_bin = projection(rendered_right, output_start, bin);
        INFO("built artifact retained FFT bin " << bin);
        CHECK(std::abs(left_bin) / std::abs(input_bin)
              == Catch::Approx(1.0).margin(0.02));
        const auto stereo_ratio = right_bin / left_bin;
        CHECK(stereo_ratio.real() == Catch::Approx(-0.6).margin(0.002));
        CHECK(stereo_ratio.imag() == Catch::Approx(0.0).margin(0.002));
    }
    for (const auto bin : rejected_bins) {
        const auto input_bin = projection(source_left, source_start, bin);
        INFO("built artifact rejected FFT bin " << bin);
        CHECK(std::abs(projection(rendered_left, output_start, bin))
                  / std::abs(input_bin) < 1.0e-5);
        CHECK(std::abs(projection(rendered_right, output_start, bin))
                  / std::abs(input_bin) < 1.0e-5);
    }
}

// Settling budget shared by every format. Clearing both the fixed WOLA latency
// and one complete FFT frame is what makes a reading describe steady state
// rather than the transition that precedes it. Valid for Live, Balanced, and
// Maximum builds because both terms come from the configured profile.
constexpr int kBlockSize = 512;
constexpr int kSettleSamples = SPECTR_EXPECTED_LATENCY + SPECTR_FFT_SIZE;
constexpr int kSettleBlocks = (kSettleSamples + kBlockSize - 1) / kBlockSize;
static_assert(kSettleBlocks > 0,
              "built-artifact settling must process at least one block");

// Drive `blocks` blocks of a 997 Hz tone through the slot and return the peak
// magnitude of the FINAL block across both channels. Reporting only the last
// block is deliberate: an earlier block still carries the transition history a
// state or parameter change produces, and mistaking that for leakage from a
// zero-valued spectral mask is exactly the reading this avoids.
//
// `first_block_index` keeps the tone phase continuous across successive passes
// so a later pass never starts from a discontinuity the plugin has to smooth.
float render_tone_peak(pulp::host::PluginSlot& slot,
                       int first_block_index,
                       int blocks) {
    std::vector<float> left(kBlockSize), right(kBlockSize);
    std::vector<float> out_left(kBlockSize), out_right(kBlockSize);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {out_left.data(), out_right.data()};
    auto input = pulp::audio::BufferView<const float>(inputs, 2, kBlockSize);
    auto output = pulp::audio::BufferView<float>(outputs, 2, kBlockSize);
    pulp::midi::MidiBuffer midi_in, midi_out;
    pulp::host::ParameterEventQueue parameter_events;

    float final_peak = 0.0f;
    for (int block = 0; block < blocks; ++block) {
        for (int sample = 0; sample < kBlockSize; ++sample) {
            const auto absolute =
                (first_block_index + block) * kBlockSize + sample;
            const auto value = static_cast<float>(
                0.4 * std::sin(2.0 * 3.14159265358979323846 * 997.0
                               * static_cast<double>(absolute) / 48000.0));
            left[static_cast<std::size_t>(sample)] = value;
            right[static_cast<std::size_t>(sample)] = -value;
        }
        slot.process(output, input, midi_in, midi_out, parameter_events,
                     kBlockSize);
        final_peak = 0.0f;
        for (const auto sample : out_left)
            final_peak = std::max(final_peak, std::abs(sample));
        for (const auto sample : out_right)
            final_peak = std::max(final_peak, std::abs(sample));
    }
    return final_peak;
}

// Identity, parameter surface, and one settled audio pass across the binary
// boundary. Every format that can carry Spectr owes this much, so it is the
// part the CLAP, VST3, and AU cases share.
void check_loaded_artifact_surface(pulp::host::PluginSlot& slot,
                                   bool loader_reports_bypass_flag) {
    CHECK(slot.info().name == "Spectr");
    const auto parameters = slot.parameters();
    check_host_parameter_contract(parameters, loader_reports_bypass_flag);
    for (const auto& parameter : parameters) {
        INFO("parameter " << parameter.name << " id=" << parameter.id
             << " default=" << parameter.default_value
             << " current=" << slot.get_parameter(parameter.id));
        CHECK(slot.get_parameter(parameter.id)
              == Catch::Approx(parameter.default_value));
    }
    CHECK(slot.has_editor());

    INFO("artifact profile: fft=" << SPECTR_FFT_SIZE
         << ", latency=" << SPECTR_EXPECTED_LATENCY
         << ", settle_blocks=" << kSettleBlocks);
    CHECK(render_tone_peak(slot, 0, kSettleBlocks) > 0.1f);
}

void check_built_artifact(const std::filesystem::path& bundle,
                          pulp::host::PluginFormat format) {
    namespace fs = std::filesystem;
    REQUIRE(fs::exists(bundle));

    pulp::host::PluginInfo info;
    info.name = "Spectr";
    info.path = bundle.string();
    info.format = format;
    auto slot = pulp::host::PluginSlot::load(info);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->is_loaded());
    REQUIRE(slot->prepare(48000.0, 512));
    CHECK(slot->latency_samples() == SPECTR_EXPECTED_LATENCY);

    check_loaded_artifact_surface(*slot, /*loader_reports_bypass_flag=*/true);

    const auto state = slot->save_state();
    REQUIRE_FALSE(state.empty());
    CHECK(slot->restore_state(state));

    check_three_islands(*slot);

    // Cross the real format boundary with authored structured Spectr state,
    // then prove categorical -infinity is exact zero in the built artifact.
    // The same profile-derived budget clears frame-boundary adoption, fixed
    // latency, and one complete WOLA frame after the state change.
    REQUIRE(slot->restore_state(make_all_muted_state()));
    CHECK(render_tone_peak(*slot, kSettleBlocks, kSettleBlocks) == 0.0f);
    slot->release();
}

#if defined(SPECTR_HAVE_TEST_AU)

// ── AU v2: reach the BUILT bundle without installing it ─────────────────
//
// `PluginSlot` reaches an AU through `AudioComponentFindNext`, i.e. through the
// component registry, keyed by a 'TYPE:SUBT:MANU' triplet. There is no
// load-by-path for AU the way there is for CLAP and VST3 — `PluginInfo::path`
// is not even read on that route — so a built `.component` that has never been
// registered is invisible to the host layer.
//
// Installing it is not an option. The contract is Build -> Validate -> Install,
// and a test must not write into the user's real
// ~/Library/Audio/Plug-Ins/Components or disturb the AudioComponentRegistrar
// cache, whose stale entries outlive a hand-removed bundle. So this registers
// the bundle's OWN factory function into the CURRENT PROCESS with
// `AudioComponentRegister`: nothing reaches disk, nothing survives the test
// process, and the registrar is never consulted.
//
// The registration deliberately uses a TEST-ONLY subtype instead of the
// shipping one. A developer machine and the acceptance runner both have a real
// Spectr.component installed, and a lookup for the shipping triplet would be
// free to hand back THAT copy — the test would silently stop measuring the
// build tree while still reporting green. A subtype nothing else claims makes
// the in-process registration the only possible match, and the lookup asserted
// empty before registering is the control that proves it.
constexpr const char* kAuTestSubtype = "SpTs";

struct AuTestComponent {
    std::string unique_id;  // the 'TYPE:SUBT:MANU' handed to PluginSlot
    std::string declared_type;
    std::string declared_subtype;
    std::string declared_manufacturer;
    std::string factory_function;
};

std::string cf_string_to_std(CFStringRef value) {
    if (!value) return {};
    char buffer[512] = {0};
    if (!CFStringGetCString(value, buffer, sizeof(buffer),
                            kCFStringEncodingUTF8)) {
        return {};
    }
    return buffer;
}

OSType four_char_code(const std::string& text) {
    REQUIRE(text.size() == 4);
    return (static_cast<OSType>(static_cast<unsigned char>(text[0])) << 24)
         | (static_cast<OSType>(static_cast<unsigned char>(text[1])) << 16)
         | (static_cast<OSType>(static_cast<unsigned char>(text[2])) << 8)
         | static_cast<OSType>(static_cast<unsigned char>(text[3]));
}

AuTestComponent register_built_au_in_process(
    const std::filesystem::path& bundle) {
    AuTestComponent component;

    CFStringRef path = CFStringCreateWithCString(
        nullptr, bundle.string().c_str(), kCFStringEncodingUTF8);
    REQUIRE(path != nullptr);
    CFURLRef url = CFURLCreateWithFileSystemPath(
        nullptr, path, kCFURLPOSIXPathStyle, true);
    CFRelease(path);
    REQUIRE(url != nullptr);
    // Held for the life of the process on purpose: the registration below
    // stores a function pointer into this bundle's loaded executable, so
    // releasing the bundle would invalidate the component we just registered.
    CFBundleRef cf_bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    INFO("AU bundle " << bundle.string());
    REQUIRE(cf_bundle != nullptr);

    CFDictionaryRef info = CFBundleGetInfoDictionary(cf_bundle);
    REQUIRE(info != nullptr);
    auto components = static_cast<CFArrayRef>(
        CFDictionaryGetValue(info, CFSTR("AudioComponents")));
    REQUIRE(components != nullptr);
    REQUIRE(CFArrayGetCount(components) == 1);
    auto declared = static_cast<CFDictionaryRef>(
        CFArrayGetValueAtIndex(components, 0));
    REQUIRE(declared != nullptr);

    const auto read = [declared](CFStringRef key) {
        return cf_string_to_std(
            static_cast<CFStringRef>(CFDictionaryGetValue(declared, key)));
    };
    component.declared_type = read(CFSTR("type"));
    component.declared_subtype = read(CFSTR("subtype"));
    component.declared_manufacturer = read(CFSTR("manufacturer"));
    component.factory_function = read(CFSTR("factoryFunction"));

    // The codes come from the bundle the build just produced, and are checked
    // against the build's own source of truth. Drift in either direction — a
    // renamed plugin code, a generator that stops emitting the triplet — fails
    // here instead of turning up as an `auval -v aufx Spec Pulp` that suddenly
    // cannot find its component.
    CHECK(component.declared_type == SPECTR_TEST_AU_TYPE);
    CHECK(component.declared_subtype == SPECTR_TEST_AU_SUBTYPE);
    CHECK(component.declared_manufacturer == SPECTR_TEST_AU_MANUFACTURER);
    REQUIRE_FALSE(component.factory_function.empty());

    UInt32 version = 0;
    if (auto number = static_cast<CFNumberRef>(
            CFDictionaryGetValue(declared, CFSTR("version")))) {
        SInt64 raw = 0;
        if (CFNumberGetValue(number, kCFNumberSInt64Type, &raw))
            version = static_cast<UInt32>(raw);
    }
    REQUIRE(version != 0);

    AudioComponentDescription desc{};
    desc.componentType = four_char_code(component.declared_type);
    desc.componentSubType = four_char_code(kAuTestSubtype);
    desc.componentManufacturer = four_char_code(component.declared_manufacturer);

    INFO("in-process AU triplet " << component.declared_type << ':'
         << kAuTestSubtype << ':' << component.declared_manufacturer);
    REQUIRE(AudioComponentFindNext(nullptr, &desc) == nullptr);

    REQUIRE(CFBundleLoadExecutable(cf_bundle));
    CFStringRef factory_name = CFStringCreateWithCString(
        nullptr, component.factory_function.c_str(), kCFStringEncodingUTF8);
    REQUIRE(factory_name != nullptr);
    void* factory = CFBundleGetFunctionPointerForName(cf_bundle, factory_name);
    CFRelease(factory_name);
    INFO("AU factory function " << component.factory_function);
    REQUIRE(factory != nullptr);

    AudioComponent registered = AudioComponentRegister(
        &desc, CFSTR("Pulp: Spectr (in-process artifact test)"), version,
        reinterpret_cast<AudioComponentFactoryFunction>(factory));
    REQUIRE(registered != nullptr);
    // The find that PluginSlot will perform must resolve to the component this
    // test registered, and to nothing else.
    REQUIRE(AudioComponentFindNext(nullptr, &desc) == registered);

    component.unique_id = component.declared_type + ":" + kAuTestSubtype + ":"
                        + component.declared_manufacturer;
    return component;
}

#endif  // SPECTR_HAVE_TEST_AU

} // namespace

#if defined(SPECTR_HAVE_TEST_CLAP)
TEST_CASE("Pulp host loads and processes the built Spectr CLAP artifact") {
    check_built_artifact(SPECTR_TEST_CLAP_PATH,
                         pulp::host::PluginFormat::CLAP);
}
#endif

#if defined(SPECTR_HAVE_TEST_VST3)
TEST_CASE("Pulp host loads and processes the built Spectr VST3 artifact") {
    check_built_artifact(SPECTR_TEST_VST3_PATH,
                         pulp::host::PluginFormat::VST3);
}
#endif

#if defined(SPECTR_HAVE_TEST_AU)
TEST_CASE("Pulp host loads and processes the built Spectr AU artifact") {
    namespace fs = std::filesystem;
    const fs::path bundle{SPECTR_TEST_AU_PATH};
    INFO("AU artifact " << bundle.string());
    REQUIRE(fs::exists(bundle));

    const auto component = register_built_au_in_process(bundle);

    pulp::host::PluginInfo info;
    info.name = "Spectr";
    info.path = bundle.string();
    info.format = pulp::host::PluginFormat::AudioUnit;
    info.unique_id = component.unique_id;
    auto slot = pulp::host::PluginSlot::load(info);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->is_loaded());
    REQUIRE(slot->prepare(48000.0, 512));

    // AU reports latency in SECONDS, so the sample count makes a float round
    // trip out of the plugin and back. The one sample of slack is that round
    // trip, not room for a wrong answer.
    CHECK(std::abs(slot->latency_samples() - SPECTR_EXPECTED_LATENCY) <= 1);

    check_loaded_artifact_surface(*slot, /*loader_reports_bypass_flag=*/false);

    // AU state is a ClassInfo property list, not a Pulp state blob, so the
    // authored fixtures the CLAP and VST3 cases restore cannot cross this
    // boundary — handing one to `restore_state` would fail property-list
    // parsing rather than test anything. Round-trip the format's own state.
    const auto state = slot->save_state();
    REQUIRE_FALSE(state.empty());
    CHECK(slot->restore_state(state));

    // Then prove DSP effect through the AU PARAMETER path, which is the
    // AU-specific surface neither sibling case can exercise: muting every band
    // has to take the output to exact zero.
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band)
        slot->set_parameter(spectr::band_mute_param_id(band), 1.0f);
    CHECK(render_tone_peak(*slot, kSettleBlocks, 2 * kSettleBlocks) == 0.0f);

    // Positive control for that zero. Silence has to be something the plugin
    // DID, not something a dead parameter path always produces, so unmuting
    // must bring the signal back.
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band)
        slot->set_parameter(spectr::band_mute_param_id(band), 0.0f);
    CHECK(render_tone_peak(*slot, 3 * kSettleBlocks, 2 * kSettleBlocks) > 0.1f);

    slot->release();
}
#endif

#if defined(SPECTR_HAVE_WEBVIEW_REFERENCE_TEST_CLAP)
TEST_CASE("Pulp host loads and processes the frozen Spectr WebView CLAP artifact") {
    check_built_artifact(SPECTR_WEBVIEW_REFERENCE_TEST_CLAP_PATH,
                         pulp::host::PluginFormat::CLAP);
}
#endif

#if defined(SPECTR_HAVE_WEBVIEW_REFERENCE_TEST_VST3)
TEST_CASE("Pulp host loads and processes the frozen Spectr WebView VST3 artifact") {
    check_built_artifact(SPECTR_WEBVIEW_REFERENCE_TEST_VST3_PATH,
                         pulp::host::PluginFormat::VST3);
}
#endif
