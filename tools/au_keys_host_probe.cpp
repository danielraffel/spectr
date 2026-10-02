// Freeze Keys through a real Audio Unit, hosted the way Logic Pro hosts an
// AU MIDI-controlled effect (aumf), offline.
//
// The unit is found the way a host finds it -- AudioComponentFindNext on the
// registered type/subtype/manufacturer (the INSTALLED component unless
// --bundle registers a built one) -- and given what Logic gives it: host
// beat-and-tempo and transport callbacks, a render callback feeding the
// input, and MIDI delivered through MusicDeviceMIDIEvent (MIDI 1.0, the AU v2
// call Logic makes) and/or MusicDeviceMIDIEventList (UMP, macOS 12+).
//
// A 220 Hz sine is played in, Freeze is switched on through the parameter
// (the default Length, 1 bar at the host's 120 BPM: a 2 s loop of the tone),
// and then a key is played. The probe measures the pitch of what comes out
// while the key is held and checks it is the hold transposed by
// 2^((note - 60) / 12). Nothing is played to a speaker.
//
//   au_keys_host_probe [--bundle path.component] [--type aumf] [--subtype SpKz]
//       [--manu Pulp] [--midi event|list] [--note 67] [--sr 48000]
//       [--block 512] [--transport playing|stopped] [--out-of-process]
//
// --out-of-process instantiates the unit in an AUHostingService process, as
// Logic Pro does; that needs an installed (system-registered) component.
//
// Exit: 0 the note played at the expected pitch; 1 it did not; 2 setup failed.

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/MusicDevice.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr AudioUnitParameterID kParamFreeze = 3;

struct Options {
    std::string bundle;
    std::string type = "aumf";
    std::string subtype = "SpKz";
    std::string manu = "Pulp";
    std::string midi = "event";
    std::string transport = "playing";
    int note = 67;
    double sr = 48000.0;
    UInt32 block = 512;
    double tone_hz = 220.0;
    bool out_of_process = false;
};

OSType fourcc(const std::string& s) {
    if (s.size() != 4) return 0;
    return OSType(std::uint8_t(s[0])) << 24 | OSType(std::uint8_t(s[1])) << 16
         | OSType(std::uint8_t(s[2])) << 8 | OSType(std::uint8_t(s[3]));
}

AudioComponent register_bundle(const std::string& path) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), true);
    if (!url) return nullptr;
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) return nullptr;
    CFDictionaryRef info = CFBundleGetInfoDictionary(bundle);
    auto comps = static_cast<CFArrayRef>(CFDictionaryGetValue(info, CFSTR("AudioComponents")));
    if (!comps || CFArrayGetCount(comps) < 1) return nullptr;
    auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(comps, 0));
    const auto str = [&](CFStringRef key) {
        auto v = static_cast<CFStringRef>(CFDictionaryGetValue(entry, key));
        char buf[256] = {};
        if (v) CFStringGetCString(v, buf, sizeof(buf), kCFStringEncodingUTF8);
        return std::string(buf);
    };
    AudioComponentDescription desc{};
    desc.componentType = fourcc(str(CFSTR("type")));
    desc.componentSubType = fourcc(str(CFSTR("subtype")));
    desc.componentManufacturer = fourcc(str(CFSTR("manufacturer")));
    auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
        CFBundleGetFunctionPointerForName(bundle, static_cast<CFStringRef>(
            CFDictionaryGetValue(entry, CFSTR("factoryFunction")))));
    if (!factory) return nullptr;
    return AudioComponentRegister(&desc, CFSTR("Probe: Freeze Keys"), 0x10000, factory);
}

struct Host {
    const Options& opt;
    AudioUnit au = nullptr;
    const std::vector<float>* in = nullptr;
    std::size_t in_pos = 0;
    double sample_time = 0.0;
    bool playing = true;

    explicit Host(const Options& o) : opt(o) {}

    static OSStatus input(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                          UInt32 frames, AudioBufferList* io) {
        auto* h = static_cast<Host*>(ref);
        for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
            auto* dst = static_cast<float*>(io->mBuffers[b].mData);
            for (UInt32 n = 0; n < frames; ++n) {
                const std::size_t at = h->in_pos + n;
                dst[n] = at < h->in->size() ? (*h->in)[at] : 0.0f;
            }
        }
        return noErr;
    }
    // Logic answers these on every render.
    static OSStatus beat_and_tempo(void* ref, Float64* beat, Float64* tempo) {
        auto* h = static_cast<Host*>(ref);
        if (tempo) *tempo = 120.0;
        if (beat) *beat = h->sample_time / h->opt.sr * 2.0;
        return noErr;
    }
    static OSStatus musical_time(void* ref, UInt32* next_beat_offset, Float32* numerator,
                                 UInt32* denominator, Float64* downbeat) {
        auto* h = static_cast<Host*>(ref);
        if (next_beat_offset) *next_beat_offset = 0;
        if (numerator) *numerator = 4.0f;
        if (denominator) *denominator = 4;
        if (downbeat) *downbeat = std::floor(h->sample_time / h->opt.sr * 2.0 / 4.0) * 4.0;
        return noErr;
    }
    static OSStatus transport(void* ref, Boolean* is_playing, Boolean* is_recording, Boolean* changed,
                              Float64* sample, Boolean* cycling, Float64* cycle_start,
                              Float64* cycle_end) {
        auto* h = static_cast<Host*>(ref);
        if (is_playing) *is_playing = h->playing;
        if (is_recording) *is_recording = false;
        if (changed) *changed = false;
        if (sample) *sample = h->playing ? h->sample_time : 0.0;
        if (cycling) *cycling = false;
        if (cycle_start) *cycle_start = 0;
        if (cycle_end) *cycle_end = 0;
        return noErr;
    }

    bool open() {
        AudioComponent comp = opt.bundle.empty() ? nullptr : register_bundle(opt.bundle);
        if (!opt.bundle.empty() && !comp) {
            std::fprintf(stderr, "could not register %s\n", opt.bundle.c_str());
            return false;
        }
        AudioComponentDescription desc{};
        desc.componentType = fourcc(opt.type);
        desc.componentSubType = fourcc(opt.subtype);
        desc.componentManufacturer = fourcc(opt.manu);
        if (!comp) comp = AudioComponentFindNext(nullptr, &desc);
        if (!comp) {
            std::fprintf(stderr, "AU %s %s %s not found\n", opt.type.c_str(), opt.subtype.c_str(),
                         opt.manu.c_str());
            return false;
        }
        AudioComponentDescription found{};
        AudioComponentGetDescription(comp, &found);
        if (found.componentType != fourcc(opt.type)) {
            std::fprintf(stderr, "component type is not %s\n", opt.type.c_str());
            return false;
        }
        if (opt.out_of_process) {
            // As Logic does: the unit runs in an AUHostingService process and
            // every call below crosses XPC.
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            __block AudioComponentInstance made = nullptr;
            __block OSStatus made_status = noErr;
            AudioComponentInstantiate(comp, kAudioComponentInstantiation_LoadOutOfProcess,
                                      ^(AudioComponentInstance instance, OSStatus status) {
                made = instance;
                made_status = status;
                dispatch_semaphore_signal(done);
            });
            if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) != 0
                || made_status != noErr || !made) {
                std::fprintf(stderr, "out-of-process instantiation failed (%d)\n", int(made_status));
                return false;
            }
            au = made;
        } else if (AudioComponentInstanceNew(comp, &au) != noErr || !au) {
            return false;
        }
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = opt.sr;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
                         | kAudioFormatFlagIsNonInterleaved;
        fmt.mFramesPerPacket = 1; fmt.mChannelsPerFrame = 2; fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4; fmt.mBytesPerPacket = 4;
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));
        UInt32 maxf = 1024;
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                             &maxf, sizeof(maxf));
        AURenderCallbackStruct cb{&Host::input, this};
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
        HostCallbackInfo info{};
        info.hostUserData = this;
        info.beatAndTempoProc = &Host::beat_and_tempo;
        info.musicalTimeLocationProc = &Host::musical_time;
        info.transportStateProc2 = &Host::transport;
        AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &info, sizeof(info));
        if (AudioUnitInitialize(au) != noErr) {
            std::fprintf(stderr, "Initialize failed\n");
            return false;
        }
        return true;
    }

    void close() {
        if (!au) return;
        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
        au = nullptr;
    }
};

// Send a channel-voice message the way the option says.
OSStatus send_midi(const Options& o, AudioUnit au, UInt8 status, UInt8 d1, UInt8 d2, UInt32 offset) {
    if (o.midi == "list") {
        if (__builtin_available(macOS 12.0, *)) {
            alignas(8) std::uint8_t storage[256] = {};
            auto* list = reinterpret_cast<MIDIEventList*>(storage);
            MIDIEventPacket* packet = MIDIEventListInit(list, kMIDIProtocol_1_0);
            // UMP MIDI 1.0 channel voice: type 2, group 0.
            const UInt32 word = 0x20000000u | UInt32(status) << 16 | UInt32(d1) << 8 | UInt32(d2);
            packet = MIDIEventListAdd(list, sizeof(storage), packet, 0, 1, &word);
            if (!packet) return -1;
            return MusicDeviceMIDIEventList(au, offset, list);
        }
        return kAudio_UnimplementedError;
    }
    return MusicDeviceMIDIEvent(au, status, d1, d2, offset);
}

// Frequency of a steady tone: Hann-windowed DFT peak, refined by golden
// section on the continuous DFT magnitude around the coarse bin.
double tone_frequency(const std::vector<float>& x, std::size_t from, std::size_t to, double sr) {
    to = std::min(to, x.size());
    if (to <= from + 1024) return 0.0;
    const std::size_t n = to - from;
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1)));
    const auto mag = [&](double hz) {
        double re = 0, im = 0;
        const double step = 2.0 * kPi * hz / sr;
        for (std::size_t i = 0; i < n; ++i) { re += w[i] * std::cos(step * double(i)); im -= w[i] * std::sin(step * double(i)); }
        return re * re + im * im;
    };
    double best = 0, best_hz = 0;
    const double res = sr / double(n);
    for (double hz = 20.0; hz < 4000.0; hz += res * 0.5) {
        const double m = mag(hz);
        if (m > best) { best = m; best_hz = hz; }
    }
    double a = best_hz - res, b = best_hz + res;
    for (int it = 0; it < 40; ++it) {
        const double c = b - (b - a) * 0.618033988749895, d = a + (b - a) * 0.618033988749895;
        if (mag(c) > mag(d)) b = d; else a = c;
    }
    return 0.5 * (a + b);
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    to = std::min(to, x.size());
    double s = 0;
    for (std::size_t i = from; i < to; ++i) s += double(x[i]) * x[i];
    return to > from ? std::sqrt(s / double(to - from)) : 0.0;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--bundle") o.bundle = next();
        else if (a == "--type") o.type = next();
        else if (a == "--subtype") o.subtype = next();
        else if (a == "--manu") o.manu = next();
        else if (a == "--midi") o.midi = next();
        else if (a == "--note") o.note = std::atoi(next().c_str());
        else if (a == "--sr") o.sr = std::atof(next().c_str());
        else if (a == "--block") o.block = UInt32(std::atoi(next().c_str()));
        else if (a == "--transport") o.transport = next();
        else if (a == "--out-of-process") o.out_of_process = true;
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }

    Host host(o);
    host.playing = o.transport != "stopped";
    const double seconds = 5.0;
    const auto total = std::size_t(seconds * o.sr);
    std::vector<float> input(total);
    for (std::size_t i = 0; i < total; ++i)
        input[i] = float(0.3 * std::sin(2.0 * kPi * o.tone_hz * double(i) / o.sr));
    host.in = &input;
    if (!host.open()) return 2;

    // The parameter the probe presses must be Freeze.
    {
        AudioUnitParameterInfo pi{};
        UInt32 size = sizeof(pi);
        if (AudioUnitGetProperty(host.au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                                 kParamFreeze, &pi, &size) != noErr) {
            std::fprintf(stderr, "no parameter %u\n", kParamFreeze);
            return 2;
        }
        char name[128] = {};
        if (pi.cfNameString) CFStringGetCString(pi.cfNameString, name, sizeof(name), kCFStringEncodingUTF8);
        if (std::strstr(name, "Freeze") == nullptr || std::strstr(name, "Length") != nullptr) {
            std::fprintf(stderr, "parameter %u is \"%s\", not Freeze\n", kParamFreeze, name);
            return 2;
        }
    }
    if (__builtin_available(macOS 12.0, *)) {
        MIDIProtocolID protocol = MIDIProtocolID(0);
        UInt32 size = sizeof(protocol);
        const OSStatus ps = AudioUnitGetProperty(host.au, kAudioUnitProperty_AudioUnitMIDIProtocol,
                                                 kAudioUnitScope_Global, 0, &protocol, &size);
        std::printf("kAudioUnitProperty_AudioUnitMIDIProtocol: status %d value %d\n", int(ps), int(protocol));
    }

    const std::size_t freeze_at = std::size_t(1.0 * o.sr);
    const std::size_t note_on = std::size_t(2.0 * o.sr) + 37;
    const std::size_t note_off = std::size_t(4.0 * o.sr) + 11;
    bool frozen = false, on_sent = false, off_sent = false;
    OSStatus on_status = noErr, off_status = noErr;
    std::vector<float> out_l(total), out_r(total);
    std::vector<float> l(o.block), r(o.block);
    std::size_t pos = 0;
    while (pos < total) {
        const UInt32 frames = UInt32(std::min<std::size_t>(o.block, total - pos));
        if (!frozen && freeze_at < pos + frames) {
            AudioUnitSetParameter(host.au, kParamFreeze, kAudioUnitScope_Global, 0, 1.0f, 0);
            frozen = true;
        }
        if (!on_sent && note_on < pos + frames) {
            on_status = send_midi(o, host.au, 0x90, UInt8(o.note), 100, UInt32(note_on - pos));
            on_sent = true;
        }
        if (!off_sent && note_off < pos + frames) {
            off_status = send_midi(o, host.au, 0x80, UInt8(o.note), 0, UInt32(note_off - pos));
            off_sent = true;
        }
        AudioBufferList* abl = static_cast<AudioBufferList*>(
            std::calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer)));
        abl->mNumberBuffers = 2;
        abl->mBuffers[0] = {1, frames * 4, l.data()};
        abl->mBuffers[1] = {1, frames * 4, r.data()};
        AudioTimeStamp ts{};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        ts.mSampleTime = Float64(pos);
        host.sample_time = Float64(pos);
        host.in_pos = pos;
        AudioUnitRenderActionFlags flags = 0;
        const OSStatus st = AudioUnitRender(host.au, &flags, &ts, 0, frames, abl);
        if (st != noErr) { std::fprintf(stderr, "AudioUnitRender error %d\n", int(st)); return 2; }
        std::copy_n(static_cast<const float*>(abl->mBuffers[0].mData), frames, out_l.begin() + long(pos));
        std::copy_n(static_cast<const float*>(abl->mBuffers[1].mData), frames, out_r.begin() + long(pos));
        std::free(abl);
        pos += frames;
    }
    host.close();

    const double held_hz = tone_frequency(out_l, std::size_t(1.5 * o.sr), std::size_t(1.95 * o.sr), o.sr);
    const double key_hz = tone_frequency(out_l, std::size_t(2.4 * o.sr), std::size_t(3.9 * o.sr), o.sr);
    const double expected = o.tone_hz * std::exp2(double(o.note - 60) / 12.0);
    const double cents = key_hz > 0 ? 1200.0 * std::log2(key_hz / expected) : 1e9;
    std::printf("midi=%s note=%d: note-on status %d, note-off status %d\n", o.midi.c_str(), o.note,
                int(on_status), int(off_status));
    std::printf("held %.3f Hz (rms %.3f); while the key is held %.3f Hz (rms %.3f), expected %.3f Hz "
                "(%+.2f cents)\n", held_hz, rms(out_l, std::size_t(1.5 * o.sr), std::size_t(1.95 * o.sr)),
                key_hz, rms(out_l, std::size_t(2.4 * o.sr), std::size_t(3.9 * o.sr)), expected, cents);
    const bool ok = on_status == noErr && std::fabs(cents) < 5.0;
    std::printf("%s\n", ok ? "OK: the key played the hold chromatically" : "FAIL: the key did not transpose the hold");
    return ok ? 0 : 1;
}
