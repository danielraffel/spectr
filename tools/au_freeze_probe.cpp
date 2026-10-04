// Freeze taps through the REAL Audio Unit, offline.
//
// The headless click tests drive the Processor through HeadlessHost. A host
// drives the built .component: the AU v2 adapter, its parameter store sync,
// its property notifications, and whatever the host does when it hears one.
// This probe loads the installed component by {aufx, subtype, manufacturer},
// renders loud programme material into memory (no audio device is opened, so
// nothing reaches a speaker), toggles Freeze at random moments exactly the
// way a host does, and scores every engage and release against an unpressed
// control render of the same material:
//
//   * spike    -- the whitened-residual discontinuity detector of
//                 test/test_freeze_click.cpp (dB above the local residual);
//   * true peak -- 4x oversampled peak in the edge window, pressed vs control;
//   * dropout  -- the quietest 1 ms of the edge window, pressed vs control;
//   * notify   -- every kAudioUnitProperty_TailTime / _Latency notification,
//                 with the render call it fired from and the value a host
//                 reading it back at that moment sees;
//   * cost     -- wall time of every AudioUnitRender call, so the block a tap
//                 lands in can be compared with the block's real-time budget.
//
// Usage:
//   Spectr-au-freeze-probe [--sr 48000] [--block 512] [--varied]
//       [--mode tracking|mixing] [--delivery setparam|schedule]
//       [--taps 10] [--seed 1] [--material dense|drums] [--subtype SpFz]
//       [--wav /tmp/x.wav] [--control-wav /tmp/c.wav] [--quiet]
//       [--bundle path/to/X.component]  (host a built bundle in-process
//                           instead of the installed component)
//       [--repeat N]       (render N times; a block's cost is its cheapest)
//       [--max-cost-ratio R]  (fail an edge whose costliest render call is
//                           over R x the costliest call without Freeze)
//       [--forbid-render-notifications]  (fail on a TailTime notification
//                           raised from inside AudioUnitRender)
//       [--deadline F]     (model a real-time device: a render call over
//                           F x its buffer's duration missed the deadline and
//                           its buffer is replaced by silence before scoring)
//       [--notify-reset]   (act like a host that Resets the unit when the
//                           tail time it reads changes)
//       [--length L]       (Freeze Length, as bars and/or a bar fraction:
//                           "1", "8", "1/16", "1+1/8"; written into the
//                           unit's saved state as the custom length and
//                           selected through the Freeze Length parameter.
//                           The probe gives no transport, so it runs at the
//                           unit's 120 BPM 4/4: 1 bar is 2 s, 1/16 bar
//                           125 ms -- a spectral hold. Default: the unit's
//                           own, 1 bar)
// Exit: 0 clean, 1 a problem (click, fade overshoot, dropout, slow edge,
// render-thread tail notification), 2 setup error.

#include <spectr/freeze_length.hpp>

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr AudioUnitParameterID kParamFreeze = 3;
constexpr AudioUnitParameterID kParamFreezeLength = 4;
// The Freeze Length parameter is an index into spectr::kLengthPresets, and
// Custom is the index one past the list. Read it from the product header: a
// hard-coded 4.0 stayed behind when the list grew from 4 presets to 20, and
// every --length run then selected preset 4 (1/6 bar) instead of Custom.
// SPECTR_PROBE_PLANT_STALE_CUSTOM=1 restores the stale 4.0: the hold-check's
// negative control, which must fail.
AudioUnitParameterValue freeze_length_custom() {
    static const AudioUnitParameterValue value =
        std::getenv("SPECTR_PROBE_PLANT_STALE_CUSTOM") != nullptr
            ? 4.0f : static_cast<AudioUnitParameterValue>(spectr::kLengthPresetCustom);
    return value;
}

struct Options {
    double sr = 48000.0;
    UInt32 block = 512;
    bool varied = false;
    std::string mode = "tracking";
    std::string delivery = "setparam";
    int taps = 10;
    unsigned seed = 1;
    std::string material = "dense";
    std::string subtype = "SpFz";
    std::string bundle;
    std::string wav;
    std::string control_wav;
    bool quiet = false;
    bool notify_reset = false;
    std::string length; // empty: leave the unit's Freeze Length alone
    float mix = -1.0f;  // Mix parameter in percent; negative leaves it alone
    bool paced = false; // render at real-time pace (lets a GPU worker deliver)
    bool gpu = false;   // GPU processing for Mixing, set through the saved state
};

struct Stereo {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
    void resize(std::size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
};

// ── Material: loud, dense, deterministic ────────────────────────────────────

struct Bandpass {
    double b0 = 1, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    Bandpass(double hz, double q, double sr) {
        const double w = 2.0 * kPi * hz / sr, alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0; b2 = -alpha / a0;
        a1 = -2.0 * std::cos(w) / a0; a2 = (1.0 - alpha) / a0;
    }
    double operator()(double x) {
        const double y = b0 * x + z1;
        z1 = -a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

Stereo drums(double seconds, double sr) {
    Stereo s; s.resize(static_cast<std::size_t>(seconds * sr));
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const auto at = [&](double t) { return static_cast<std::size_t>(t * sr); };
    const auto hit = [&](double when, double length, auto&& voice) {
        const auto start = at(when);
        for (std::size_t i = 0; i < at(length) && start + i < s.size(); ++i) {
            const auto [l, r] = voice(double(i) / sr);
            s.l[start + i] += float(l); s.r[start + i] += float(r);
        }
    };
    const double beat = 0.5;
    for (double bar = 0.0; bar < seconds; bar += 4 * beat) {
        for (int b = 0; b < 4; ++b) {
            const double when = bar + b * beat;
            if (b % 2 == 0) {
                double phase = 0.0;
                hit(when, 0.9, [&](double t) {
                    phase += 2.0 * kPi * (48.0 + 110.0 * std::exp(-t / 0.035)) / sr;
                    const double v = 0.9 * std::sin(phase) * std::exp(-t / 0.28)
                                   + (t < 0.003 ? 0.3 * u(rng) * (1.0 - t / 0.003) : 0.0);
                    return std::pair{v, v};
                });
            } else {
                Bandpass body(1800.0, 0.7, sr);
                hit(when, 0.6, [&](double t) {
                    const double v = 0.55 * body(u(rng)) * std::exp(-t / 0.13)
                                   + 0.3 * std::sin(2.0 * kPi * 185.0 * t) * std::exp(-t / 0.07);
                    return std::pair{v * 0.95, v};
                });
            }
        }
        for (int e = 0; e < 8; ++e) {
            const double decay = e == 7 ? 0.18 : 0.025;
            double prev = 0.0;
            hit(bar + e * beat / 2.0 + (e % 2 ? 0.012 : 0.0), decay * 6, [&](double t) {
                const double x = u(rng);
                const double v = 0.22 * (x - prev) * std::exp(-t / decay);
                prev = x;
                return std::pair{v * 0.7, v};
            });
        }
        static constexpr double notes[] = {55.0, 55.0, 65.41, 49.0};
        for (int b = 0; b < 4; ++b)
            hit(bar + b * beat, beat, [&](double t) {
                const double v = 0.35 * std::min(1.0, t / 0.01) * std::exp(-t / 0.4)
                               * std::sin(2.0 * kPi * notes[b] * t);
                return std::pair{v, v};
            });
    }
    return s;
}

Stereo chord(double seconds, double sr) {
    Stereo s; s.resize(static_cast<std::size_t>(seconds * sr));
    static constexpr double f0[] = {220.0, 261.63, 329.63, 440.0};
    std::mt19937 rng(21);
    std::normal_distribution<double> g(0.0, 1.0);
    std::vector<double> phase(8 * 64, 0.0);
    double lp_l = 0.0, lp_r = 0.0;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / sr;
        double l = 0.0, r = 0.0;
        for (int v = 0; v < 4; ++v)
            for (int side = 0; side < 2; ++side) {
                const double f = f0[v] * (1.0 + 0.003 * std::sin(2.0 * kPi * (5.1 + v * 0.3) * t))
                               * (side ? 1.0035 : 0.9965);
                double saw = 0.0;
                for (int h = 1; h * f < std::min(16000.0, 0.45 * sr) && h <= 64; ++h)
                    saw += std::sin(h * (2.0 * kPi * f * t) + v + side) / h;
                (side ? r : l) += 0.12 * saw;
            }
        lp_l += 0.15 * (g(rng) - lp_l);
        lp_r += 0.15 * (g(rng) - lp_r);
        const double swell = 0.65 + 0.35 * std::sin(2.0 * kPi * 0.37 * t);
        s.l[n] = float(swell * (l + 0.35 * lp_l));
        s.r[n] = float(swell * (r + 0.35 * lp_r));
    }
    return s;
}

void normalise(Stereo& s, double peak) {
    double m = 1e-9;
    for (std::size_t n = 0; n < s.size(); ++n)
        m = std::max({m, std::fabs(double(s.l[n])), std::fabs(double(s.r[n]))});
    const auto g = float(peak / m);
    for (std::size_t n = 0; n < s.size(); ++n) { s.l[n] *= g; s.r[n] *= g; }
}

Stereo material(const Options& o, double seconds) {
    Stereo d = drums(seconds, o.sr);
    if (o.material == "dense") {
        // A mastered-loud mix: drums over a chord bed, soft-limited so the
        // crest factor is small and the peaks sit right under 0 dBFS.
        const Stereo c = chord(seconds, o.sr);
        normalise(d, 1.0);
        Stereo m; m.resize(d.size());
        for (std::size_t n = 0; n < d.size(); ++n) {
            m.l[n] = float(std::tanh(1.8 * (d.l[n] + 0.55 * c.l[n])));
            m.r[n] = float(std::tanh(1.8 * (d.r[n] + 0.55 * c.r[n])));
        }
        normalise(m, 0.977); // -0.2 dBFS
        return m;
    }
    normalise(d, 0.977);
    return d;
}

// ── Scoring ─────────────────────────────────────────────────────────────────

constexpr int kOrder = 32;
// 20 log10(sqrt(2)): the most an equal-power fade's sum can exceed the larger
// of the two signals it mixes.
constexpr double kEqualPowerSumDb = 3.0103;

std::vector<double> fit_whitener(const std::vector<float>& x, std::size_t from, std::size_t to) {
    const std::size_t n = to - from;
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1)));
    std::vector<double> r(kOrder + 1, 0.0);
    for (int k = 0; k <= kOrder; ++k)
        for (std::size_t i = std::size_t(k); i < n; ++i) r[std::size_t(k)] += w[i] * w[i - std::size_t(k)];
    r[0] = r[0] * (1.0 + 1e-6) + 1e-12;
    std::vector<double> a(kOrder + 1, 0.0), tmp;
    a[0] = 1.0;
    double err = r[0];
    for (int i = 1; i <= kOrder; ++i) {
        double acc = r[std::size_t(i)];
        for (int j = 1; j < i; ++j) acc += a[std::size_t(j)] * r[std::size_t(i - j)];
        const double k = -acc / err;
        tmp = a;
        for (int j = 1; j < i; ++j) a[std::size_t(j)] = tmp[std::size_t(j)] + k * tmp[std::size_t(i - j)];
        a[std::size_t(i)] = k;
        err *= (1.0 - k * k);
        if (err <= 0.0) break;
    }
    return a;
}

std::vector<double> residual(const std::vector<float>& x, const std::vector<double>& a,
                             std::size_t from, std::size_t to) {
    std::vector<double> e(to - from, 0.0);
    for (std::size_t n = from; n < to; ++n) {
        double acc = 0.0;
        for (int k = 0; k <= kOrder && n >= std::size_t(k); ++k) acc += a[std::size_t(k)] * x[n - std::size_t(k)];
        e[n - from] = acc;
    }
    return e;
}

double spike_db(const std::vector<double>& e, std::size_t lead, double sr, std::size_t* where) {
    const auto outer = std::size_t(0.004 * sr), inner = std::size_t(0.0005 * sr);
    std::vector<double> prefix(e.size() + 1, 0.0);
    for (std::size_t i = 0; i < e.size(); ++i) prefix[i + 1] = prefix[i] + e[i] * e[i];
    const auto energy = [&](std::size_t a, std::size_t b) { return prefix[b] - prefix[a]; };
    double worst = -200.0;
    for (std::size_t n = std::max(lead, outer); n + outer < e.size() && n + lead < e.size(); ++n) {
        const double around = energy(n - outer, n - inner) + energy(n + inner + 1, n + outer + 1);
        const double rms = std::sqrt(around / double(2 * (outer - inner)) + 1e-24);
        const double db = 20.0 * std::log10(std::fabs(e[n]) / rms + 1e-12);
        if (db > worst) { worst = db; if (where) *where = n; }
    }
    return worst;
}

// 4x oversampled peak (windowed-sinc interpolation, 32 taps per side).
double true_peak(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double peak = 0.0;
    constexpr int kTaps = 32;
    for (std::size_t n = std::max<std::size_t>(from, kTaps); n < to && n + kTaps < x.size(); ++n) {
        peak = std::max(peak, std::fabs(double(x[n])));
        for (int phase = 1; phase < 4; ++phase) {
            const double frac = phase / 4.0;
            double acc = 0.0;
            for (int k = -kTaps + 1; k <= kTaps; ++k) {
                const double d = k - frac;
                const double sinc = std::sin(kPi * d) / (kPi * d);
                const double win = 0.5 + 0.5 * std::cos(kPi * d / (kTaps + 1));
                acc += x[std::size_t(std::ptrdiff_t(n) + k)] * sinc * win;
            }
            peak = std::max(peak, std::fabs(acc));
        }
    }
    return peak;
}

double sample_peak(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double peak = 0.0;
    for (std::size_t n = from; n < to && n < x.size(); ++n) peak = std::max(peak, std::fabs(double(x[n])));
    return peak;
}

// Quietest 1 ms RMS in [from, to).
double min_ms_rms(const std::vector<float>& x, std::size_t from, std::size_t to, double sr) {
    const auto w = std::size_t(0.001 * sr);
    double lowest = 1e9;
    for (std::size_t s = from; s + w <= to; s += w / 2) {
        double acc = 0.0;
        for (std::size_t i = s; i < s + w; ++i) acc += double(x[i]) * x[i];
        lowest = std::min(lowest, std::sqrt(acc / double(w)));
    }
    return lowest;
}

double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

// ── WAV ─────────────────────────────────────────────────────────────────────

void write_wav(const std::string& path, const Stereo& s, double sr) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    const std::uint32_t frames = std::uint32_t(s.size()), bytes = frames * 8;
    const auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(3); u16(2); u32(std::uint32_t(sr)); u32(std::uint32_t(sr) * 8); u16(8); u16(32);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (std::size_t n = 0; n < s.size(); ++n) { std::fwrite(&s.l[n], 4, 1, f); std::fwrite(&s.r[n], 4, 1, f); }
    std::fclose(f);
}

// ── The host ────────────────────────────────────────────────────────────────

OSType four_cc(CFDictionaryRef dict, const char* key) {
    auto name = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
    auto value = static_cast<CFStringRef>(CFDictionaryGetValue(dict, name));
    CFRelease(name);
    char text[8] = {};
    if (!value || !CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8)) return 0;
    return OSType(std::uint8_t(text[0])) << 24 | OSType(std::uint8_t(text[1])) << 16
         | OSType(std::uint8_t(text[2])) << 8 | OSType(std::uint8_t(text[3]));
}

// Host a built .component in-process, without installing it: load the
// bundle, find the factory its Info.plist names, and register it for this
// process only. It is the same binary a host loads from the Components folder.
AudioComponent register_bundle(const std::string& path) {
    static AudioComponent registered = nullptr;
    if (registered) return registered;
    auto url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), true);
    CFBundleRef bundle = url ? CFBundleCreate(nullptr, url) : nullptr;
    if (url) CFRelease(url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) {
        std::fprintf(stderr, "cannot load bundle %s\n", path.c_str());
        return nullptr;
    }
    auto list = static_cast<CFArrayRef>(CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents")));
    if (!list || CFArrayGetCount(list) < 1) { std::fprintf(stderr, "no AudioComponents in %s\n", path.c_str()); return nullptr; }
    auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, 0));
    AudioComponentDescription desc{};
    desc.componentType = four_cc(entry, "type");
    desc.componentSubType = four_cc(entry, "subtype");
    desc.componentManufacturer = four_cc(entry, "manufacturer");
    auto factory_name = static_cast<CFStringRef>(CFDictionaryGetValue(entry, CFSTR("factoryFunction")));
    auto factory = factory_name
        ? reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle, factory_name))
        : nullptr;
    if (!factory) { std::fprintf(stderr, "no factory function in %s\n", path.c_str()); return nullptr; }
    registered = AudioComponentRegister(&desc, CFSTR("Pulp: freeze probe (in-process)"), 1, factory);
    return registered;
}

struct Host;

struct Notification {
    AudioUnitPropertyID id;
    long long render_call;   // -1: outside a render call
    long long sample;        // host sample time of that render call
    double value;            // the value a host reading it back now sees
};

struct Host {
    const Options& opt;
    AudioUnit au = nullptr;
    const Stereo* in = nullptr;
    std::size_t in_pos = 0;
    long long render_call = -1;
    long long render_sample = 0;
    bool in_render = false;
    std::vector<Notification> notes;
    double last_tail = -1.0;
    int resets_requested = 0;
    bool reset_pending = false;

    explicit Host(const Options& o) : opt(o) {}

    static OSStatus input(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                          UInt32, UInt32 frames, AudioBufferList* io) {
        auto* h = static_cast<Host*>(ctx);
        for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
            auto* d = static_cast<float*>(io->mBuffers[b].mData);
            const auto& src = b == 0 ? h->in->l : h->in->r;
            for (UInt32 i = 0; i < frames; ++i) {
                const std::size_t at = h->in_pos + i;
                d[i] = at < src.size() ? src[at] : 0.0f;
            }
        }
        return noErr;
    }

    static void listener(void* ctx, AudioUnit unit, AudioUnitPropertyID id,
                         AudioUnitScope, AudioUnitElement) {
        auto* h = static_cast<Host*>(ctx);
        Float64 value = -1.0;
        UInt32 size = sizeof(value);
        AudioUnitGetProperty(unit, id, kAudioUnitScope_Global, 0, &value, &size);
        h->notes.push_back({id, h->in_render ? h->render_call : -1, h->render_sample, value});
        if (id == kAudioUnitProperty_TailTime) {
            if (h->opt.notify_reset && h->last_tail >= 0.0 && value != h->last_tail) {
                ++h->resets_requested;
                h->reset_pending = true;
            }
            h->last_tail = value;
        }
    }

    bool open() {
        AudioComponent comp = opt.bundle.empty() ? nullptr : register_bundle(opt.bundle);
        if (!opt.bundle.empty() && !comp) return false;
        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Effect;
        const auto& st = opt.subtype;
        desc.componentSubType = OSType(st[0]) << 24 | OSType(st[1]) << 16 | OSType(st[2]) << 8 | OSType(st[3]);
        desc.componentManufacturer = 'Pulp';
        if (!comp) comp = AudioComponentFindNext(nullptr, &desc);
        if (!comp) { std::fprintf(stderr, "AU aufx/%s/Pulp not found\n", st.c_str()); return false; }
        if (AudioComponentInstanceNew(comp, &au) != noErr || !au) return false;
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = opt.sr;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        fmt.mFramesPerPacket = 1; fmt.mChannelsPerFrame = 2; fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4; fmt.mBytesPerPacket = 4;
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));
        UInt32 maxf = 4096;
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof(maxf));
        AURenderCallbackStruct cb{&Host::input, this};
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
        AudioUnitAddPropertyListener(au, kAudioUnitProperty_TailTime, &Host::listener, this);
        AudioUnitAddPropertyListener(au, kAudioUnitProperty_Latency, &Host::listener, this);
        if (AudioUnitInitialize(au) != noErr) { std::fprintf(stderr, "Initialize failed\n"); return false; }
        if (opt.gpu && !set_gpu_processing()) return false;
        if (opt.mode == "mixing" && !set_mode("linear_phase")) return false;
        // spectr::kMix is parameter id 1 (spectr.hpp).
        if (opt.mix >= 0.0f
            && AudioUnitSetParameter(au, 1, kAudioUnitScope_Global, 0, opt.mix, 0) != noErr) {
            std::fprintf(stderr, "could not set Mix %g\n", double(opt.mix));
            return false;
        }
        if (!opt.length.empty() && !set_length(opt.length)) {
            std::fprintf(stderr, "could not set Freeze Length %s\n", opt.length.c_str());
            return false;
        }
        Float64 tail = -1; UInt32 size = sizeof(tail);
        AudioUnitGetProperty(au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &size);
        last_tail = tail;
        return true;
    }

    // Edit the plugin's own JSON inside the unit's saved state and load it
    // back -- exactly what a host does when it restores a project. The state
    // is Pulp's "PLST" envelope: magic, version, the parameter-store and
    // plugin-state sizes, the two payloads, and a CRC-32 of everything
    // before it.
    template <typename Edit>
    bool edit_state(Edit&& edit) {
        CFPropertyListRef plist = nullptr;
        UInt32 size = sizeof(plist);
        if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size) != noErr || !plist)
            return false;
        auto* dict = CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(plist));
        CFRelease(plist);
        bool changed = false;
        if (auto* data = static_cast<CFDataRef>(CFDictionaryGetValue(dict, CFSTR("pulp-state")));
            data && CFGetTypeID(data) == CFDataGetTypeID()) {
            std::string blob(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), std::size_t(CFDataGetLength(data)));
            const auto u32 = [&](std::size_t at) {
                std::uint32_t v = 0;
                for (int b = 0; b < 4; ++b) v |= std::uint32_t(std::uint8_t(blob[at + std::size_t(b)])) << (8 * b);
                return v;
            };
            const auto put = [](std::string& out, std::uint32_t v) {
                for (int b = 0; b < 4; ++b) out.push_back(char((v >> (8 * b)) & 0xFF));
            };
            if (blob.size() > 20 && blob.compare(0, 4, "PLST") == 0) {
                const std::size_t store = u32(8), plugin = u32(12);
                std::string json = blob.substr(16 + store, plugin);
                if (edit(json)) {
                    std::string out = blob.substr(0, 12);
                    put(out, std::uint32_t(json.size()));
                    out += blob.substr(16, store);
                    out += json;
                    std::uint32_t crc = 0xFFFFFFFFu;
                    for (const char c : out) {
                        crc ^= static_cast<std::uint8_t>(c);
                        for (int bit = 0; bit < 8; ++bit)
                            crc = (crc >> 1) ^ (0xEDB88320u & ((crc & 1u) ? 0xFFFFFFFFu : 0u));
                    }
                    put(out, ~crc);
                    auto* next = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(out.data()), CFIndex(out.size()));
                    CFDictionarySetValue(dict, CFSTR("pulp-state"), next);
                    CFRelease(next);
                    changed = true;
                }
            }
        }
        CFPropertyListRef out = dict;
        const OSStatus st = changed
            ? AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &out, sizeof(out))
            : OSStatus(-1);
        CFRelease(dict);
        return changed && st == noErr;
    }

    // The plugin JSON's current text (for reading a value back).
    std::string state_json() {
        std::string json;
        (void)edit_state([&](std::string& j) { json = j; return false; });
        return json;
    }

    bool set_mode(const char* token) {
        const bool ok = edit_state([&](std::string& json) {
            for (const char* from : {"\"zero_latency\"", "\"linear_phase\""}) {
                const auto at = json.find(from);
                if (at == std::string::npos) continue;
                json.replace(at, std::strlen(from), std::string("\"") + token + "\"");
                return true;
            }
            return false;
        });
        if (!ok) std::fprintf(stderr, "could not set render mode %s\n", token);
        return ok;
    }

    // Mixing's GPU processing choice is session state, set the way a project
    // reload sets it.
    bool set_gpu_processing() {
        const bool ok = edit_state([&](std::string& json) {
            const auto at = json.find("\"gpu_processing\"");
            if (at == std::string::npos) return false;
            const auto value = json.find("false", at);
            const auto end = json.find_first_of(",}", at);
            if (value == std::string::npos || value > end) return false;
            json.replace(value, 5, "true");
            return true;
        });
        if (!ok) std::fprintf(stderr, "could not turn GPU processing on\n");
        return ok;
    }

    // Freeze Length "B", "N/D" or "B+N/D": the custom length in the plugin
    // JSON (as a project reload restores it), then the parameter on Custom.
    bool set_length(const std::string& text) {
        int bars = 0;
        std::string fraction = "0";
        const auto plus = text.find('+');
        if (plus != std::string::npos) {
            bars = std::atoi(text.substr(0, plus).c_str());
            fraction = text.substr(plus + 1);
        } else if (text.find('/') != std::string::npos) {
            fraction = text;
        } else {
            bars = std::atoi(text.c_str());
        }
        char member[96];
        std::snprintf(member, sizeof(member), "\"freeze_length\":{\"bars\":%d,\"fraction\":\"%s\"}",
                      bars, fraction.c_str());
        const bool ok = edit_state([&](std::string& json) {
            const auto at = json.find("\"freeze_length\":");
            if (at == std::string::npos) return false;
            const auto end = json.find('}', at);
            if (end == std::string::npos) return false;
            json.replace(at, end + 1 - at, member);
            return true;
        });
        return ok && AudioUnitSetParameter(au, kParamFreezeLength, kAudioUnitScope_Global, 0,
                                           freeze_length_custom(), 0) == noErr;
    }

    // The custom length and the parameter, as the unit reports them.
    std::string read_length() {
        const std::string json = state_json();
        const auto at = json.find("\"freeze_length\":");
        AudioUnitParameterValue preset = -1.0f;
        AudioUnitGetParameter(au, kParamFreezeLength, kAudioUnitScope_Global, 0, &preset);
        if (at == std::string::npos) return "?";
        const auto end = json.find('}', at);
        const auto bars_at = json.find("\"bars\":", at);
        const auto fraction_at = json.find("\"fraction\":", at);
        if (end == std::string::npos || bars_at > end || fraction_at > end) return "?";
        const int bars = std::atoi(json.c_str() + bars_at + 7);
        const auto f0 = json.find('"', fraction_at + 11) + 1;
        const std::string fraction = json.substr(f0, json.find('"', f0) - f0);
        char out[64];
        std::snprintf(out, sizeof(out), "%s%d+%s", preset == freeze_length_custom() ? "" : "preset:",
                      bars, fraction.c_str());
        return out;
    }

    void close() {
        if (!au) return;
        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
        au = nullptr;
    }

    Float64 latency_seconds() {
        Float64 v = 0; UInt32 size = sizeof(v);
        AudioUnitGetProperty(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &v, &size);
        return v;
    }
};

struct Tap { std::size_t press, release; };

struct RenderResult {
    Stereo out;
    std::vector<double> block_us;
    std::vector<std::size_t> block_start;
    std::vector<UInt32> block_frames;
    std::vector<Notification> notes;
    int latency = 0;
    int resets = 0;
    std::string length_read_back;
};

RenderResult render(const Options& o, const Stereo& input, const std::vector<Tap>* taps) {
    RenderResult result;
    Host host(o);
    host.in = &input;
    if (!host.open()) std::exit(2);
    result.latency = int(std::lround(host.latency_seconds() * o.sr));
    result.length_read_back = host.read_length();
    const std::size_t total = input.size();
    result.out.resize(total);
    std::vector<float> l(4096), r(4096);
    std::mt19937 rng(o.seed * 7919u + 3u);
    std::uniform_int_distribution<UInt32> varied(32, 1024);
    std::vector<std::pair<std::size_t, float>> events;
    if (taps)
        for (const auto& t : *taps) { events.push_back({t.press, 1.0f}); events.push_back({t.release, 0.0f}); }
    std::sort(events.begin(), events.end());
    std::size_t next_event = 0;
    std::size_t pos = 0;
    const auto paced_start = std::chrono::steady_clock::now();
    while (pos < total) {
        if (o.paced)
            std::this_thread::sleep_until(paced_start + std::chrono::nanoseconds(
                static_cast<long long>(double(pos) * 1e9 / o.sr)));
        UInt32 frames = o.varied ? varied(rng) : o.block;
        frames = UInt32(std::min<std::size_t>(frames, total - pos));
        if (host.reset_pending) {
            AudioUnitReset(host.au, kAudioUnitScope_Global, 0);
            host.reset_pending = false;
        }
        // Deliver every edge that falls inside this block.
        while (next_event < events.size() && events[next_event].first < pos + frames) {
            const auto [at, value] = events[next_event++];
            if (o.delivery == "schedule") {
                AudioUnitParameterEvent ev{};
                ev.scope = kAudioUnitScope_Global;
                ev.element = 0;
                ev.parameter = kParamFreeze;
                ev.eventType = kParameterEvent_Immediate;
                ev.eventValues.immediate.bufferOffset = UInt32(at > pos ? at - pos : 0);
                ev.eventValues.immediate.value = value;
                AudioUnitScheduleParameters(host.au, &ev, 1);
            } else {
                // A UI tap or an automation point a host applies at the top
                // of the block it falls in.
                AudioUnitSetParameter(host.au, kParamFreeze, kAudioUnitScope_Global, 0, value, 0);
            }
        }
        AudioBufferList* abl = static_cast<AudioBufferList*>(std::calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer)));
        abl->mNumberBuffers = 2;
        abl->mBuffers[0] = {1, frames * 4, l.data()};
        abl->mBuffers[1] = {1, frames * 4, r.data()};
        AudioTimeStamp ts{};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        ts.mSampleTime = Float64(pos);
        AudioUnitRenderActionFlags flags = 0;
        host.in_pos = pos;
        host.render_call = (long long)result.block_start.size();
        host.render_sample = (long long)pos;
        host.in_render = true;
        const auto t0 = std::chrono::steady_clock::now();
        const OSStatus st = AudioUnitRender(host.au, &flags, &ts, 0, frames, abl);
        const auto t1 = std::chrono::steady_clock::now();
        host.in_render = false;
        if (st != noErr) { std::fprintf(stderr, "AudioUnitRender error %d at %zu\n", int(st), pos); std::exit(2); }
        const auto* ol = static_cast<const float*>(abl->mBuffers[0].mData);
        const auto* orr = static_cast<const float*>(abl->mBuffers[1].mData);
        std::copy(ol, ol + frames, result.out.l.begin() + long(pos));
        std::copy(orr, orr + frames, result.out.r.begin() + long(pos));
        std::free(abl);
        result.block_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        result.block_start.push_back(pos);
        result.block_frames.push_back(frames);
        pos += frames;
    }
    result.notes = host.notes;
    result.resets = host.resets_requested;
    host.close();
    return result;
}

} // namespace

// ── Hold length, end to end ─────────────────────────────────────────────────
//
// Chord A for three seconds, then chord B (no partial in common). The Hold
// length is written into the unit's saved state and restored, as a project
// reload does, and read back. Freeze is pressed `offset` after the change and
// the held sound is measured for each chord's partials. A short hold holds
// only what is sounding now -- though each analysed frame spans kFftSize
// samples (170 ms at 48 kHz), so even the shortest reaches that far back
// before the press. A long one averages the last seconds' frames.

double goertzel_power(const std::vector<float>& x, std::size_t from, std::size_t n, double hz, double sr) {
    const double w = 2.0 * kPi * hz / sr, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1));
        const double s0 = x[from + i] * win + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / (double(n) * double(n));
}

int hold_check(Options o) {
    static constexpr double chord_a[] = {261.63, 329.63, 392.00, 523.25};
    static constexpr double chord_b[] = {369.99, 466.16, 554.37, 739.99};
    const double change = 3.0, seconds = 6.0;
    Stereo input; input.resize(std::size_t(seconds * o.sr));
    for (std::size_t n = 0; n < input.size(); ++n) {
        const double t = double(n) / o.sr;
        const auto& chord = t < change ? chord_a : chord_b;
        double v = 0.0;
        for (const double f : chord) v += 0.12 * std::sin(2.0 * kPi * f * t);
        input.l[n] = input.r[n] = float(v);
    }
    int bad = 0;
    std::printf("hold check sr=%.0f block=%u mode=%s: chord A for %.1f s, then chord B\n",
                o.sr, o.block, o.mode.c_str(), change);
    for (const double offset : {0.05, 0.40}) {
        double a_share[2] = {0, 0};
        int column = 0;
        // 1/16 bar (125 ms at the probe's 120 BPM, a spectral hold) and
        // 1 bar (2 s, a loop).
        for (const char* hold : {"1/16", "1"}) {
            o.length = hold;
            const std::vector<Tap> taps{{std::size_t((change + offset) * o.sr), std::size_t((change + offset + 2.0) * o.sr)}};
            const auto r = render(o, input, &taps);
            const std::size_t from = std::size_t((change + offset + 0.4) * o.sr) + std::size_t(r.latency);
            const std::size_t n = std::size_t(1.0 * o.sr);
            double ea = 0.0, eb = 0.0;
            for (const double f : chord_a) ea += goertzel_power(r.out.l, from, n, f, o.sr);
            for (const double f : chord_b) eb += goertzel_power(r.out.l, from, n, f, o.sr);
            a_share[column++] = ea / (ea + eb + 1e-30);
            if (std::getenv("SPECTR_HOLD_TRACE")) {
                // What the output carries over time, from 0.5 s before the
                // press to 3 s after it, in 250 ms windows: chord A, chord B.
                const auto press_out = std::size_t((change + offset) * o.sr) + std::size_t(r.latency);
                std::printf("    trace Length %s, press +%.2f s (t=0 at the press, latency-aligned):\n", hold, offset);
                for (double t = -0.5; t < 3.0; t += 0.25) {
                    const auto at = std::size_t(double(press_out) + t * o.sr);
                    const auto w = std::size_t(0.25 * o.sr);
                    if (at + w > r.out.l.size()) break;
                    double a = 0, b = 0;
                    for (const double f : chord_a) a += goertzel_power(r.out.l, at, w, f, o.sr);
                    for (const double f : chord_b) b += goertzel_power(r.out.l, at, w, f, o.sr);
                    std::printf("      t=%+5.2f s  A %6.1f dB  B %6.1f dB\n", t,
                                10.0 * std::log10(a + 1e-30), 10.0 * std::log10(b + 1e-30));
                }
            }
            const std::string expected = std::string(std::strchr(hold, '/') ? "0+" : "") + hold
                + (std::strchr(hold, '/') ? "" : "+0");
            std::printf("  press %+.2f s after the change, Length %s bar (unit reads back %s): "
                        "chord A %5.1f%% of the held chord energy (A %6.1f dB, B %6.1f dB)\n",
                        offset, hold, r.length_read_back.c_str(), 100.0 * ea / (ea + eb + 1e-30),
                        10.0 * std::log10(ea + 1e-30), 10.0 * std::log10(eb + 1e-30));
            if (r.length_read_back != expected) ++bad;
        }
        // Pressed well after the change, the short hold must carry almost
        // none of chord A, and the 2 s one -- a loop of the 2 s before the
        // press, most of it chord A -- must carry it: the measured second
        // plays the loop's start.
        if (offset > 0.3 && !(a_share[0] < 0.05)) ++bad;
        if (offset > 0.3 && !(a_share[1] > 0.5)) ++bad;
    }
    std::printf("%s: hold check (the unit read back every Length it was given)\n",
                bad ? "FAIL" : "OK");
    return bad ? 1 : 0;
}

// ── The hold reaches the output ────────────────────────────────────────────
//
// The edge probes score clicks against an unpressed control, so a freeze that
// never reaches the output passes them: nothing changes, nothing clicks. This
// asks what the held sound IS. Chord A plays for three seconds, then chord B
// (no partial in common); Freeze is pressed 50 ms after the change, so the
// hold's analysis -- each frame spans kFftSize samples -- is mostly chord A.
// One second later, well inside the hold, the output must still be chord A.
// The same render without the press is the instrument's control: it must
// carry essentially no chord A there, or the measurement cannot tell a held
// chord from a live one.
int hold_presence(Options o) {
    static constexpr double chord_a[] = {261.63, 329.63, 392.00, 523.25};
    static constexpr double chord_b[] = {369.99, 466.16, 554.37, 739.99};
    const double change = 3.0, seconds = 6.0, offset = 0.05;
    Stereo input; input.resize(std::size_t(seconds * o.sr));
    for (std::size_t n = 0; n < input.size(); ++n) {
        const double t = double(n) / o.sr;
        const auto& chord = t < change ? chord_a : chord_b;
        double v = 0.0;
        for (const double f : chord) v += 0.12 * std::sin(2.0 * kPi * f * t);
        input.l[n] = input.r[n] = float(v);
    }
    if (o.length.empty()) o.length = "1/16";
    const std::vector<Tap> taps{{std::size_t((change + offset) * o.sr), std::size_t((change + offset + 2.5) * o.sr)}};
    double share[2] = {0, 0}, a_db[2] = {0, 0};
    for (int pressed = 0; pressed < 2; ++pressed) {
        const auto r = render(o, input, pressed ? &taps : nullptr);
        const std::size_t from = std::size_t((change + offset + 0.6) * o.sr) + std::size_t(r.latency);
        const std::size_t n = std::size_t(1.0 * o.sr);
        double ea = 0.0, eb = 0.0;
        for (const double f : chord_a) ea += goertzel_power(r.out.l, from, n, f, o.sr);
        for (const double f : chord_b) eb += goertzel_power(r.out.l, from, n, f, o.sr);
        share[pressed] = ea / (ea + eb + 1e-30);
        a_db[pressed] = 10.0 * std::log10(ea + 1e-30);
        std::printf("hold presence mode=%s block=%u Length %s %s: latency %d, chord A %5.1f%% "
                    "of the chord energy (A %6.1f dB, B %6.1f dB)\n",
                    o.mode.c_str(), o.block, o.length.c_str(), pressed ? "pressed  " : "unpressed",
                    r.latency, 100.0 * share[pressed], a_db[pressed], 10.0 * std::log10(eb + 1e-30));
    }
    const bool control_ok = share[0] < 0.05;
    const bool held = share[1] > 0.5 && a_db[1] > -60.0;
    if (!control_ok) std::printf("NO VERDICT: the unpressed control carries chord A; the instrument cannot tell held from live\n");
    std::printf("%s: the held chord %s the output\n", control_ok && held ? "OK" : "FAIL",
                held ? "reaches" : "does NOT reach");
    return control_ok && held ? 0 : 1;
}

// ── Reported latency against measured delay ────────────────────────────────
//
// An impulse per channel through the unit, dry (Mix 0) and wet (Mix 100), and
// the first sample at a tenth of its height located in the output. The delay
// measured that way must equal kAudioUnitProperty_Latency exactly: that
// property is what a host's delay compensation applies. --latency-plant N adds
// N samples to the reported figure before the comparison (a negative control
// that must fail). --paced renders at real-time pace, so a GPU-rendered Mixing
// path is measured as delivered rather than as its CPU fallback.
int latency_check(Options o, int plant) {
    const std::size_t total = std::size_t(1.5 * o.sr);
    constexpr std::size_t marker[2] = {13, 29};
    constexpr float level[2] = {0.5f, -0.3f};
    // Dry (Mix 0): an impulse per channel, located by its onset. Wet (Mix
    // 100): a noise burst, located by the input/output cross-correlation
    // peak -- the spectral path spreads an isolated impulse over its frame.
    Stereo impulse; impulse.resize(total);
    impulse.l[marker[0]] = level[0];
    impulse.r[marker[1]] = level[1];
    constexpr std::size_t burst_at = 1000, burst = 4096;
    Stereo noise; noise.resize(total);
    std::uint32_t rng = 12345u;
    for (std::size_t i = 0; i < burst; ++i) {
        rng = rng * 1664525u + 1013904223u; noise.l[burst_at + i] = 0.25f * (float(rng >> 8) / 16777216.0f - 0.5f);
        rng = rng * 1664525u + 1013904223u; noise.r[burst_at + i] = 0.25f * (float(rng >> 8) / 16777216.0f - 0.5f);
    }
    int bad = 0;
    for (const float mix : {0.0f, 100.0f}) {
        o.mix = mix;
        const bool wet = mix > 0.0f;
        const auto r = render(o, wet ? noise : impulse, nullptr);
        const long long reported = r.latency + plant;
        long long measured[2] = {-1, -1};
        for (int ch = 0; ch < 2; ++ch) {
            const auto& x = ch ? r.out.r : r.out.l;
            if (!wet) {
                const float threshold = 0.1f * std::abs(level[ch]);
                for (std::size_t i = 0; i < x.size(); ++i)
                    if (std::abs(x[i]) >= threshold) { measured[ch] = (long long)i - (long long)marker[ch]; break; }
                continue;
            }
            const auto& in = ch ? noise.r : noise.l;
            double best = -1.0;
            const long long lo = std::max(0LL, (long long)r.latency - 2048), hi = (long long)r.latency + 2048;
            for (long long lag = lo; lag <= hi; ++lag) {
                double acc = 0.0;
                for (std::size_t i = burst_at; i < burst_at + burst && i + std::size_t(lag) < x.size(); ++i)
                    acc += double(in[i]) * double(x[i + std::size_t(lag)]);
                if (acc > best) { best = acc; measured[ch] = lag; }
            }
        }
        const bool ok = measured[0] == reported && measured[1] == reported;
        if (!ok) ++bad;
        std::printf("latency sr=%.0f block=%u mode=%s gpu=%d mix=%.0f paced=%d: reported %lld samples (%.2f ms), "
                    "measured L %lld R %lld  %s\n",
                    o.sr, o.block, o.mode.c_str(), int(o.gpu), double(mix), int(o.paced), reported,
                    1000.0 * double(reported) / o.sr, measured[0], measured[1], ok ? "OK" : "MISMATCH");
    }
    std::printf("%s: reported latency %s the measured delay\n", bad ? "FAIL" : "OK",
                bad ? "does NOT equal" : "equals");
    return bad ? 1 : 0;
}

// ── The first samples of a stream ──────────────────────────────────────────
//
// A fresh unit at Mix 100 with a flat shape must pass the first samples it is
// given at full level: an impulse at a few positions near the start, and a
// kick-drum-like hit at sample 0, each found at the reported latency. A fade
// at the head of the stream is what a drum hit at playback start loses.
int stream_start_check(Options o) {
    o.mix = 100.0f;
    int bad = 0;
    for (const std::size_t at : {std::size_t(0), std::size_t(13), std::size_t(512),
                                 std::size_t(1024), std::size_t(2048), std::size_t(6000)}) {
        Stereo input; input.resize(std::size_t(1.0 * o.sr));
        input.l[at] = input.r[at] = 0.5f;
        const auto r = render(o, input, nullptr);
        const float got = r.out.l[at + std::size_t(r.latency)];
        const bool ok = std::abs(got - 0.5f) < 1e-3f;
        if (!ok) ++bad;
        std::printf("stream start mode=%s: impulse at %5zu -> %.6f (%+.1f dB)  %s\n", o.mode.c_str(), at,
                    double(got), 20.0 * std::log10(std::max(double(std::abs(got)) / 0.5, 1e-15)),
                    ok ? "OK" : "LOST");
    }
    Stereo kick; kick.resize(std::size_t(1.0 * o.sr));
    for (std::size_t i = 0; i < std::size_t(0.2 * o.sr); ++i) {
        const double t = double(i) / o.sr, f = 50.0 + 100.0 * std::exp(-t * 40.0);
        kick.l[i] = kick.r[i] = float(0.8 * std::exp(-t * 18.0) * std::sin(2.0 * kPi * f * t));
    }
    const auto r = render(o, kick, nullptr);
    double energy = 0.0, residual = 0.0, peak_in = 0.0, peak_out = 0.0;
    for (std::size_t i = 0; i < std::size_t(0.2 * o.sr); ++i) {
        const double y = r.out.l[i + std::size_t(r.latency)];
        energy += double(kick.l[i]) * kick.l[i];
        residual += (y - kick.l[i]) * (y - kick.l[i]);
        if (i < std::size_t(0.03 * o.sr)) {
            peak_in = std::max(peak_in, double(std::abs(kick.l[i])));
            peak_out = std::max(peak_out, std::abs(y));
        }
    }
    const double null_db = 10.0 * std::log10(std::max(residual, 1e-30) / energy);
    const bool kick_ok = null_db < -80.0;
    if (!kick_ok) ++bad;
    std::printf("stream start mode=%s: kick at sample 0 -> first 30 ms peak %+.1f dB vs input, "
                "null residual %.1f dB  %s\n", o.mode.c_str(),
                20.0 * std::log10(std::max(peak_out, 1e-15) / peak_in), null_db, kick_ok ? "OK" : "LOST");
    std::printf("%s: the first samples of a stream %s the output at full level\n",
                bad ? "FAIL" : "OK", bad ? "do NOT reach" : "reach");
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
    Options o;
    int repeat = 1;
    double max_cost_ratio = 0.0;
    bool forbid_notifications = false;
    double deadline = 0.0;
    bool check_hold = false, check_presence = false, check_latency = false,
         check_stream_start = false;
    int latency_plant = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--sr") o.sr = std::atof(next().c_str());
        else if (a == "--block") o.block = UInt32(std::atoi(next().c_str()));
        else if (a == "--varied") o.varied = true;
        else if (a == "--mode") o.mode = next();
        else if (a == "--delivery") o.delivery = next();
        else if (a == "--taps") o.taps = std::atoi(next().c_str());
        else if (a == "--seed") o.seed = unsigned(std::atoi(next().c_str()));
        else if (a == "--material") o.material = next();
        else if (a == "--subtype") o.subtype = next();
        else if (a == "--bundle") o.bundle = next();
        else if (a == "--wav") o.wav = next();
        else if (a == "--control-wav") o.control_wav = next();
        else if (a == "--quiet") o.quiet = true;
        else if (a == "--notify-reset") o.notify_reset = true;
        else if (a == "--repeat") repeat = std::max(1, std::atoi(next().c_str()));
        else if (a == "--max-cost-ratio") max_cost_ratio = std::atof(next().c_str());
        else if (a == "--forbid-render-notifications") forbid_notifications = true;
        else if (a == "--deadline") deadline = std::atof(next().c_str());
        else if (a == "--hold-check") check_hold = true;
        else if (a == "--hold-presence") check_presence = true;
        else if (a == "--latency-check") check_latency = true;
        else if (a == "--latency-plant") latency_plant = std::atoi(next().c_str());
        else if (a == "--paced") o.paced = true;
        else if (a == "--gpu") o.gpu = true;
        else if (a == "--stream-start-check") check_stream_start = true;
        else if (a == "--length") o.length = next();
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }

    if (check_hold) return hold_check(o);
    if (check_presence) return hold_presence(o);
    if (check_latency) return latency_check(o, latency_plant);
    if (check_stream_start) return stream_start_check(o);

    // Taps: press, hold 0.7-1.6 s, release, rest 0.8-1.5 s. The first press
    // waits for the capture window to fill.
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> hold_len(0.7, 1.6), rest(0.8, 1.5), jitter(0.0, 0.5);
    std::vector<Tap> taps;
    double t = 1.5;
    for (int k = 0; k < o.taps; ++k) {
        t += jitter(rng);
        const double h = hold_len(rng);
        taps.push_back({std::size_t(t * o.sr), std::size_t((t + h) * o.sr)});
        t += h + rest(rng);
    }
    const double seconds = t + 1.0;
    const Stereo input = material(o, seconds);

    // Renders are deterministic, so repeats split into the same blocks; the
    // cheapest time each block took is its cost with scheduling noise removed.
    auto control = render(o, input, nullptr);
    auto pressed = render(o, input, &taps);
    for (int r = 1; r < repeat; ++r) {
        const auto c = render(o, input, nullptr);
        const auto p = render(o, input, &taps);
        for (std::size_t b = 0; b < control.block_us.size(); ++b)
            control.block_us[b] = std::min(control.block_us[b], c.block_us[b]);
        for (std::size_t b = 0; b < pressed.block_us.size(); ++b)
            pressed.block_us[b] = std::min(pressed.block_us[b], p.block_us[b]);
    }

    // What a real-time device would have played: a render call that took
    // longer than `deadline` x its own duration missed the device's deadline,
    // and that buffer was never delivered.
    int overruns[2] = {0, 0};
    if (deadline > 0.0) {
        int which = 0;
        for (auto* r : {&control, &pressed}) {
            for (std::size_t b = 0; b < r->block_start.size(); ++b) {
                const double budget = 1e6 * r->block_frames[b] / o.sr;
                if (r->block_us[b] <= deadline * budget) continue;
                ++overruns[which];
                std::fill_n(r->out.l.begin() + long(r->block_start[b]), r->block_frames[b], 0.0f);
                std::fill_n(r->out.r.begin() + long(r->block_start[b]), r->block_frames[b], 0.0f);
            }
            ++which;
        }
    }
    if (!o.wav.empty()) write_wav(o.wav, pressed.out, o.sr);
    if (!o.control_wav.empty()) write_wav(o.control_wav, control.out, o.sr);

    // Where an edge reaches the output: the block it was delivered in starts
    // at or before it; the plugin's reported latency moves it later.
    const auto first_block_at_or_after = [&](std::size_t sample) -> std::size_t {
        if (o.delivery == "schedule") return sample;
        for (std::size_t b = 0; b < pressed.block_start.size(); ++b)
            if (pressed.block_start[b] + pressed.block_frames[b] > sample) return pressed.block_start[b];
        return sample;
    };

    std::vector<double> sorted = control.block_us;
    std::sort(sorted.begin(), sorted.end());
    const double control_median = sorted[sorted.size() / 2];
    const double control_p99 = sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
    const double control_max = sorted.back();

    std::printf("probe sr=%.0f block=%s mode=%s delivery=%s material=%s latency=%d taps=%d repeat=%d\n",
                o.sr, o.varied ? "varied" : std::to_string(o.block).c_str(), o.mode.c_str(),
                o.delivery.c_str(), o.material.c_str(), pressed.latency, o.taps, repeat);
    const double budget_us = 1e6 * (o.varied ? 512 : o.block) / o.sr;
    std::printf("render cost per call, no freeze: median %.0f us, p99 %.0f us, max %.0f us "
                "(real-time budget %.0f us)\n", control_median, control_p99, control_max, budget_us);
    if (deadline > 0.0)
        std::printf("deadline %.2fx: %d missed buffer(s) without freeze, %d with the taps\n",
                    deadline, overruns[0], overruns[1]);

    int bad = 0;
    double worst_spike[2] = {-200, -200}, worst_ctrl_spike[2] = {-200, -200};
    double worst_tp[2] = {0, 0}, worst_ctrl_tp[2] = {0, 0}, worst_drop[2] = {-200, -200};
    double worst_cost[2] = {0, 0}, worst_fade_over[2] = {-200, -200};
    for (std::size_t k = 0; k < taps.size(); ++k) {
        for (int edge_kind = 0; edge_kind < 2; ++edge_kind) {
            const std::size_t delivered = first_block_at_or_after(edge_kind ? taps[k].release : taps[k].press);
            const std::size_t edge = delivered + std::size_t(pressed.latency);
            const std::size_t lead = std::size_t(0.035 * o.sr);
            const std::size_t from = edge - std::size_t(0.01 * o.sr) - lead;
            const std::size_t to = std::min(pressed.out.size(), edge + std::size_t(0.2 * o.sr) + lead);
            double spike = -200, ctrl_spike = -200, tp = 0, ctrl_tp = 0, drop = -200;
            std::size_t where = 0;
            for (int side = 0; side < 2; ++side) {
                const auto& x = side ? pressed.out.r : pressed.out.l;
                const auto& c = side ? control.out.r : control.out.l;
                const auto a = fit_whitener(c, edge - std::size_t(0.3 * o.sr), edge);
                std::size_t w = 0;
                const double sp = spike_db(residual(x, a, from, to), lead, o.sr, &w);
                if (sp > spike) { spike = sp; where = from + w; }
                ctrl_spike = std::max(ctrl_spike, spike_db(residual(c, a, from, to), lead, o.sr, nullptr));
                tp = std::max(tp, true_peak(x, from + lead, to - lead));
                ctrl_tp = std::max(ctrl_tp, true_peak(c, from + lead, to - lead));
                // Dropout: how far the quietest 1 ms falls below the control's.
                drop = std::max(drop, db(min_ms_rms(c, from + lead, to - lead, o.sr))
                                    - db(min_ms_rms(x, from + lead, to - lead, o.sr)));
            }
            // THE FADE'S OWN PEAK. The hold is a different sound from the
            // live input and may well peak higher; what the fade must not do
            // is peak above both of the sounds it moves between by more than
            // its law allows. An equal-power fade of two uncorrelated sounds
            // keeps their power, but its sum can reach (cos + sin) = sqrt(2)
            // times the larger of them, +3 dB, where both happen to peak
            // together: on loud, dense material that is a few percent of
            // edges, for any hold. A gain that rides the fade (the defect
            // this is here for) goes past that bound.
            const std::size_t fade_from = edge, fade_to = edge + std::size_t(0.07 * o.sr);
            double fade_pk = 0, live_pk = 0, hold_pk = 0;
            for (int side = 0; side < 2; ++side) {
                const auto& x = side ? pressed.out.r : pressed.out.l;
                const auto& c = side ? control.out.r : control.out.l;
                fade_pk = std::max(fade_pk, sample_peak(x, fade_from, fade_to));
                live_pk = std::max(live_pk, sample_peak(c, fade_from, fade_to));
                // The held sound (engage) or the live sound (release) after it.
                hold_pk = std::max(hold_pk, sample_peak(edge_kind ? c : x, fade_to, fade_to + std::size_t(0.3 * o.sr)));
                if (edge_kind) hold_pk = std::max(hold_pk, sample_peak(x, edge - std::size_t(0.3 * o.sr), edge));
            }
            const double fade_over = db(fade_pk) - db(std::max(live_pk, hold_pk));
            // The costliest render call from the one the edge was delivered in
            // to the end of the engage (a hop of preparation plus the fade).
            double cost = 0.0;
            const std::size_t span = std::size_t(0.08 * o.sr);
            for (std::size_t b = 0; b < pressed.block_start.size(); ++b)
                if (pressed.block_start[b] + pressed.block_frames[b] > delivered
                    && pressed.block_start[b] < delivered + span)
                    cost = std::max(cost, pressed.block_us[b]);
            const bool click = spike > std::max(ctrl_spike + 6.0, 20.0);
            const bool over = fade_over > kEqualPowerSumDb + 0.5 && fade_pk > 1.0;
            const bool dropout = drop > 12.0;
            const bool slow = max_cost_ratio > 0.0 && cost > max_cost_ratio * control_max;
            if (click || over || dropout || slow) ++bad;
            worst_spike[edge_kind] = std::max(worst_spike[edge_kind], spike);
            worst_ctrl_spike[edge_kind] = std::max(worst_ctrl_spike[edge_kind], ctrl_spike);
            worst_tp[edge_kind] = std::max(worst_tp[edge_kind], tp);
            worst_ctrl_tp[edge_kind] = std::max(worst_ctrl_tp[edge_kind], ctrl_tp);
            worst_drop[edge_kind] = std::max(worst_drop[edge_kind], drop);
            worst_cost[edge_kind] = std::max(worst_cost[edge_kind], cost);
            worst_fade_over[edge_kind] = std::max(worst_fade_over[edge_kind], fade_over);
            if (!o.quiet)
                std::printf("  tap %2zu %-7s @%8zu  spike %5.1f dB (ctrl %5.1f) at %+6.1f ms  "
                            "truepeak %+5.2f dBTP (ctrl %+5.2f)  fade peak %+5.2f dB over live/hold  "
                            "dropout %5.1f dB  cost %5.0f us%s%s%s%s\n",
                            k, edge_kind ? "release" : "engage", delivered, spike, ctrl_spike,
                            1000.0 * (double(where) - double(edge)) / o.sr, db(tp), db(ctrl_tp),
                            fade_over, drop, cost,
                            click ? "  CLICK" : "", over ? "  FADE-OVER" : "",
                            dropout ? "  DROPOUT" : "", slow ? "  SLOW" : "");
        }
    }
    for (int e = 0; e < 2; ++e)
        std::printf("%-7s worst: spike %5.1f dB (ctrl %5.1f)  truepeak %+5.2f dBTP (ctrl %+5.2f)  "
                    "fade over live/hold %+5.2f dB  dropout %5.1f dB  cost %5.0f us "
                    "(%.1fx the costliest call without freeze)\n",
                    e ? "release" : "engage", worst_spike[e], worst_ctrl_spike[e], db(worst_tp[e]),
                    db(worst_ctrl_tp[e]), worst_fade_over[e], worst_drop[e], worst_cost[e],
                    worst_cost[e] / std::max(control_max, 1.0));
    int tail_notes = 0, latency_notes = 0, in_render = 0, tail_in_render = 0;
    for (const auto& n : pressed.notes) {
        if (n.id == kAudioUnitProperty_TailTime) ++tail_notes; else ++latency_notes;
        if (n.render_call >= 0) ++in_render;
        if (n.render_call >= 0 && n.id == kAudioUnitProperty_TailTime) ++tail_in_render;
    }
    std::printf("property notifications: %d TailTime, %d Latency, %d from inside AudioUnitRender\n",
                tail_notes, latency_notes, in_render);
    if (!o.quiet)
        for (const auto& n : pressed.notes)
            std::printf("  %s -> %g  render call %lld (sample %lld)\n",
                        n.id == kAudioUnitProperty_TailTime ? "TailTime" : "Latency", n.value,
                        n.render_call, n.sample);
    if (o.notify_reset) std::printf("host resets requested by tail changes: %d\n", pressed.resets);
    // A Latency notification follows a Latency-mode change (a state load
    // here), not a tap; a TailTime one is what a Freeze edge used to raise.
    if (forbid_notifications && tail_in_render > 0) {
        std::printf("FAIL: the unit notified TailTime from inside a render call\n");
        ++bad;
    }
    if (max_cost_ratio > 0.0)
        std::printf("cost gate: an edge may cost at most %.1fx the costliest call without freeze (%.0f us)\n",
                    max_cost_ratio, max_cost_ratio * control_max);
    std::printf("%s: %d problem(s)\n", bad ? "FAIL" : "OK", bad);
    return bad ? 1 : 0;
}
