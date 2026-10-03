// The level controls through the REAL Audio Unit, offline.
//
// Hosts the built .component in-process (no install, no audio device, no
// GUI), then checks what a DAW sees and plays back:
//
//   1. the parameter list carries Intensity (5000) and Auto Gain (5001) under
//      their names, with Intensity 0..100 default 100;
//   2. Intensity automated through AudioUnitScheduleParameters -- a 100 -> 0
//      -> 100 % ramp over two seconds, one event per block, the way a host
//      plays a lane back -- moves a +12 dB shape's band smoothly: no block's
//      envelope step exceeds the Bank-LFO yardstick (2.3 dB per 512 frames)
//      and the ends land on +12 and 0 dB;
//   3. an Intensity JUMP set with AudioUnitSetParameter (a UI write or an
//      automation point at a block top) still ramps (<= 2.3 dB/block);
//   4. Auto Gain on brings that +12 dB shape back near the input level.
//
// The shape is written as host parameters (Band NN Gain), so the whole path
// is the adapter's. Exit 0 pass, 1 a gate failed, 2 the probe could not run.
//
// Pacing: a host renders in real time, and the mask a parameter change stages
// is designed on a worker that keeps up with that. This probe renders blocks
// back to back, so after each one it waits until the bundle's
// spectr_mask_design_backlog_v1() reads zero -- every staged layout designed
// and waiting for the next block -- instead of measuring how far a loaded
// machine let the render outrun that worker.
//
// Usage: Spectr-au-level-probe --bundle path/to/Spectr.component [--mode tracking|mixing]

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr UInt32 kBlock = 512;
constexpr AudioUnitParameterID kIntensity = 5000, kAutoGain = 5001;
constexpr AudioUnitParameterID kBandGainBase = 1000;

using BacklogFn = std::uint64_t (*)();
BacklogFn design_backlog = nullptr;

// Block until the mask-design worker has caught up with every layout the last
// render staged. Event-driven on the counter, not a fixed delay: it returns as
// soon as the worker is done, and a worker that never finishes is a failure to
// run rather than a silently stale measurement.
void await_design_worker() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (design_backlog() != 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "mask-design worker never drained its backlog (%llu)\n",
                         static_cast<unsigned long long>(design_backlog()));
            std::exit(2);
        }
        std::this_thread::yield();
    }
}

OSType four_cc(CFDictionaryRef dict, const char* key) {
    auto name = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
    auto value = static_cast<CFStringRef>(CFDictionaryGetValue(dict, name));
    CFRelease(name);
    char text[8] = {};
    if (!value || !CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8)) return 0;
    return OSType(std::uint8_t(text[0])) << 24 | OSType(std::uint8_t(text[1])) << 16
         | OSType(std::uint8_t(text[2])) << 8 | OSType(std::uint8_t(text[3]));
}

AudioComponent register_bundle(const std::string& path) {
    auto url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), true);
    CFBundleRef bundle = url ? CFBundleCreate(nullptr, url) : nullptr;
    if (url) CFRelease(url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) return nullptr;
    auto list = static_cast<CFArrayRef>(CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents")));
    if (!list || CFArrayGetCount(list) < 1) return nullptr;
    auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, 0));
    AudioComponentDescription desc{};
    desc.componentType = four_cc(entry, "type");
    desc.componentSubType = four_cc(entry, "subtype");
    desc.componentManufacturer = four_cc(entry, "manufacturer");
    auto factory_name = static_cast<CFStringRef>(CFDictionaryGetValue(entry, CFSTR("factoryFunction")));
    auto factory = factory_name
        ? reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle, factory_name))
        : nullptr;
    if (!factory) return nullptr;
    design_backlog = reinterpret_cast<BacklogFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("spectr_mask_design_backlog_v1")));
    if (!design_backlog) {
        std::fprintf(stderr, "%s does not export spectr_mask_design_backlog_v1\n", path.c_str());
        return nullptr;
    }
    return AudioComponentRegister(&desc, CFSTR("Pulp: level probe (in-process)"), 1, factory);
}

struct Unit {
    AudioUnit au = nullptr;
    const std::vector<float>* in = nullptr;
    std::size_t pos = 0;

    static OSStatus input(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                          UInt32, UInt32 frames, AudioBufferList* io) {
        auto* u = static_cast<Unit*>(ctx);
        for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
            auto* d = static_cast<float*>(io->mBuffers[b].mData);
            for (UInt32 i = 0; i < frames; ++i) {
                const std::size_t at = u->pos + i;
                d[i] = at < u->in->size() ? (*u->in)[at] : 0.0f;
            }
        }
        return noErr;
    }

    bool open(AudioComponent comp) {
        if (AudioComponentInstanceNew(comp, &au) != noErr || !au) return false;
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = kSr;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        fmt.mFramesPerPacket = 1; fmt.mChannelsPerFrame = 2; fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4; fmt.mBytesPerPacket = 4;
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));
        UInt32 maxf = 4096;
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof(maxf));
        AURenderCallbackStruct cb{&Unit::input, this};
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
        return AudioUnitInitialize(au) == noErr;
    }
    void set(AudioUnitParameterID id, float v) {
        AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, v, 0);
    }
    void close() {
        if (!au) return;
        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
        au = nullptr;
    }
    // Render the whole input; `before(block)` runs ahead of each block.
    template <typename F>
    std::vector<float> render(const std::vector<float>& signal, F&& before) {
        in = &signal;
        std::vector<float> out(signal.size()), l(kBlock), r(kBlock);
        for (pos = 0; pos < signal.size(); pos += kBlock) {
            before(pos / kBlock);
            const UInt32 frames = UInt32(std::min<std::size_t>(kBlock, signal.size() - pos));
            AudioBufferList* abl = static_cast<AudioBufferList*>(
                std::calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer)));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0] = {1, frames * 4, l.data()};
            abl->mBuffers[1] = {1, frames * 4, r.data()};
            AudioTimeStamp ts{};
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            ts.mSampleTime = Float64(pos);
            AudioUnitRenderActionFlags flags = 0;
            if (AudioUnitRender(au, &flags, &ts, 0, frames, abl) != noErr) {
                std::fprintf(stderr, "AudioUnitRender failed\n");
                std::exit(2);
            }
            std::copy_n(static_cast<const float*>(abl->mBuffers[0].mData), frames, out.begin() + long(pos));
            std::free(abl);
            await_design_worker();
        }
        return out;
    }
};

double block_db(const std::vector<float>& x, std::size_t b) {
    double e = 0;
    for (std::size_t i = b * kBlock; i < (b + 1) * kBlock && i < x.size(); ++i) e += double(x[i]) * x[i];
    return 10.0 * std::log10(std::max(e / kBlock, 1e-30));
}

int failures = 0;
void gate(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::string bundle, mode = "tracking";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--bundle" && i + 1 < argc) bundle = argv[++i];
        else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
    }
    if (bundle.empty()) { std::fprintf(stderr, "usage: %s --bundle X.component\n", argv[0]); return 2; }
    AudioComponent comp = register_bundle(bundle);
    if (!comp) { std::fprintf(stderr, "cannot host %s\n", bundle.c_str()); return 2; }

    // 1. The parameter list.
    {
        Unit u;
        if (!u.open(comp)) return 2;
        for (const auto [id, name] : {std::pair{kIntensity, "Intensity"}, std::pair{kAutoGain, "Auto Gain"}}) {
            AudioUnitParameterInfo info{};
            UInt32 size = sizeof(info);
            const bool got = AudioUnitGetProperty(u.au, kAudioUnitProperty_ParameterInfo,
                kAudioUnitScope_Global, id, &info, &size) == noErr;
            char text[64] = {};
            if (got && info.cfNameString)
                CFStringGetCString(info.cfNameString, text, sizeof(text), kCFStringEncodingUTF8);
            std::printf("param %u: %s [%g..%g] default %g\n", unsigned(id), text,
                        info.minValue, info.maxValue, info.defaultValue);
            gate(got && std::string(text) == name, name);
            if (id == kIntensity)
                gate(info.minValue == 0.0f && info.maxValue == 100.0f && info.defaultValue == 100.0f,
                     "Intensity spans 0..100 % with 100 % default");
        }
        u.close();
    }

    // A tone at the centre of band 16 of 32 (20 Hz .. 20 kHz).
    const double hz = 20.0 * std::pow(1000.0, 16.5 / 32.0);
    std::vector<float> tone(std::size_t(kSr * 5.0));
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = float(0.05 * std::sin(2.0 * M_PI * hz * double(i) / kSr));
    const auto setup = [&](Unit& u) {
        if (mode == "mixing") {
            // Tracking is the default; Mixing is not a parameter, so this
            // probe measures Tracking unless the host build defaults otherwise.
        }
        u.set(kAutoGain, 0.0f);
        for (AudioUnitParameterID b = 0; b < 32; ++b) u.set(kBandGainBase + b, 12.0f);
    };
    const double dry_db = 20.0 * std::log10(0.05 / std::sqrt(2.0));
    constexpr double kGate = 2.3;

    // 2. Scheduled automation ramp.
    {
        Unit u;
        if (!u.open(comp)) return 2;
        setup(u);
        const std::size_t start = 100, ramp = std::size_t(1.0 * kSr / kBlock);
        const auto out = u.render(tone, [&](std::size_t block) {
            float v = 100.0f;
            if (block >= start && block < start + ramp) v = 100.0f * (1.0f - float(block - start) / float(ramp));
            else if (block >= start + ramp && block < start + 2 * ramp)
                v = 100.0f * float(block - start - ramp) / float(ramp);
            AudioUnitParameterEvent ev{};
            ev.scope = kAudioUnitScope_Global;
            ev.parameter = kIntensity;
            ev.eventType = kParameterEvent_Immediate;
            ev.eventValues.immediate.bufferOffset = 0;
            ev.eventValues.immediate.value = v;
            AudioUnitScheduleParameters(u.au, &ev, 1);
        });
        double worst = 0;
        for (std::size_t b = start - 4; b < start + 2 * ramp + 20; ++b)
            worst = std::max(worst, std::abs(block_db(out, b + 1) - block_db(out, b)));
        const double full = block_db(out, start - 2) - dry_db;
        const double flat = block_db(out, start + ramp) - dry_db;
        std::printf("scheduled ramp: start %+.2f dB, at 0%% %+.2f dB, max step %.2f dB/block\n",
                    full, flat, worst);
        gate(std::abs(full - 12.0) < 0.3, "100 % plays the +12 dB shape");
        gate(std::abs(flat) < 0.5, "0 % plays flat");
        gate(worst <= kGate, "scheduled Intensity automation plays back smoothly");
        u.close();
    }

    // 3. A set-parameter jump still ramps.
    {
        Unit u;
        if (!u.open(comp)) return 2;
        setup(u);
        const auto out = u.render(tone, [&](std::size_t block) {
            if (block == 100) u.set(kIntensity, 0.0f);
            if (block == 200) u.set(kIntensity, 100.0f);
        });
        double worst = 0;
        for (std::size_t b = 96; b < 230; ++b)
            worst = std::max(worst, std::abs(block_db(out, b + 1) - block_db(out, b)));
        std::printf("set-parameter jump: max step %.2f dB/block\n", worst);
        gate(worst <= kGate, "an Intensity jump ramps");
        u.close();
    }

    // 4. Auto Gain.
    {
        Unit u;
        if (!u.open(comp)) return 2;
        setup(u);
        u.set(kAutoGain, 1.0f);
        const auto out = u.render(tone, [](std::size_t) {});
        const double level = block_db(out, 300) - dry_db;
        std::printf("Auto Gain on, +12 dB everywhere: %+.2f dB against the input\n", level);
        gate(std::abs(level) < 1.0, "Auto Gain brings an all +12 dB shape back to the input level");
        u.close();
    }
    std::printf("%s\n", failures ? "FAIL" : "PASS: level controls through the AU");
    return failures ? 1 : 0;
}
