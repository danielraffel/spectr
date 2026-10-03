// LFO routing automation through the REAL Audio Unit, offline.
//
// The headless routing tests drive the Processor through HeadlessHost. This
// hosts the built .component in-process (no install, no audio device, no
// window) and plays host automation of the routing lanes the way a DAW does:
// sample-accurate AudioUnitScheduleParameters events, an Amount ramp written
// as one event per render call, and destination switches flipped mid-render.
// Every switch edge is scored against a CONTROL render with the same LFO on
// the same destinations held steady:
//
//   * spike -- the whitened-residual discontinuity detector shared with
//              au_freeze_probe.cpp (dB above the local residual); an edge
//              fails as a click above max(control + 6 dB, 20 dB);
//   * step  -- the largest jump between consecutive 1 ms peak envelopes of a
//              steady 2 kHz tone (its envelope is the mask's gain on it); an
//              edge fails above max(control + 0.5 dB, 1.6 dB), the Amount
//              ramp above the control's own largest step + 1 dB;
//   * cost  -- the costliest render call near the edge against the
//              costliest call of the control render (--max-cost-ratio).
//
// Usage: Spectr-au-routes-probe --bundle path/to/X.component
//            [--sr 48000] [--block 128] [--max-cost-ratio R] [--gate-cost]
//        Spectr-au-routes-probe --bundle X.component --offline-equivalence
//            [--no-offline-flag]
//
// Pacing. A host renders in real time and the mask a route stages is designed
// on a worker that keeps up with that. This probe renders back to back, so
// after every render call it waits until spectr_mask_design_backlog_v1() and
// spectr_param_sync_backlog_v1() read zero -- the pacing a real-time host gives that worker -- rather than
// measuring how far a loaded machine let the render outrun it (4/10 runs
// failed under load before it did).
//
// Cost is tracked, not gated, by default: the render-call cost is wall-clock
// time on a shared machine, and under load it moved by more than the ratio it
// was gated at. --gate-cost restores the gate for a quiet machine.
//
// --offline-equivalence renders one automated Intensity ramp plus LFO routes
// to Intensity and the band window twice: PACED (the waits above, the AU told
// nothing) and UNPACED with kAudioUnitProperty_OfflineRender set, as a
// faster-than-real-time bounce is. With the offline fix the AU waits for its
// own workers and the two match sample for sample; --no-offline-flag renders
// the unpaced pass without the property (the negative control, which differs
// on a loaded machine). Exit 0 match, 1 mismatch.
//
// What this can and cannot see. The renderer spreads every mask swap over a
// crossfade (up to 18 ms), so even an UN-ramped switch reaches the audio at
// about 1 dB/ms -- inside the free-running LFO's own range. This probe is
// therefore a host-path gate (the lanes arrive sample-accurately through a
// real AU, no switch clicks, no render call blows the budget, nothing moves
// faster than the modulation itself), not a detector for a lost route ramp:
// with SPECTR_MODULATION_PLANT=route-step it still passes. The ramp is proven
// at the field level, where it is 2.1 vs 12.0 dB per block
// (test_modulation_routing.cpp, Spectr-route-smoothness-negative-control).
// Exit: 0 clean, 1 a click or slow edge, 2 setup error.

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kOrder = 32;

// Parameter IDs (docs/parameter-surface.md).
constexpr AudioUnitParameterID kLfoEnabled = 4000, kLfoShape = 4001, kLfoRate = 4002,
                               kLfoDepth = 4003, kCenter = 3001, kWidth = 3002;
constexpr AudioUnitParameterID route_on(int t) {
    // Targets 0..7 at 4020 + t; the level targets (Intensity 8, Mix 9,
    // Output 10) in their own block from 4060.
    return AudioUnitParameterID(t >= 8 ? 4052 + t : 4020 + t);
}
constexpr AudioUnitParameterID route_amount(int t) { return route_on(t) + 10; }
constexpr AudioUnitParameterID band_gain(int b) { return AudioUnitParameterID(1000 + b); }

struct Options {
    double sr = 48000.0;
    UInt32 block = 128;
    std::string bundle;
    double max_cost_ratio = 0.0;
    bool gate_cost = false;
    bool offline_equivalence = false;
    bool offline_flag = true;
};

// How a render call is paced against the AU's own mask-design worker.
enum class Pacing {
    paced,            // wait for the design backlog after every call (a real-time host)
    offline_flagged,  // no wait; kAudioUnitProperty_OfflineRender = 1 (a bounce)
    unpaced,          // no wait, no flag: what a bounce was before the AU heard it
};

using BacklogFn = std::uint64_t (*)();
BacklogFn design_backlog = nullptr;
BacklogFn param_sync_backlog = nullptr;

// Block until the mask-design worker has caught up with every layout the last
// render staged. Event-driven on the counter; a worker that never drains is a
// failure to run, not a stale measurement.
void await_design_worker() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (design_backlog() != 0 || param_sync_backlog() != 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "mask-design worker never drained its backlog (%llu)\n",
                         static_cast<unsigned long long>(design_backlog()));
            std::exit(2);
        }
        std::this_thread::yield();
    }
}

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

double spike_db(const std::vector<double>& e, std::size_t lead, double sr) {
    const auto outer = std::size_t(0.004 * sr), inner = std::size_t(0.0005 * sr);
    std::vector<double> prefix(e.size() + 1, 0.0);
    for (std::size_t i = 0; i < e.size(); ++i) prefix[i + 1] = prefix[i] + e[i] * e[i];
    const auto energy = [&](std::size_t a, std::size_t b) { return prefix[b] - prefix[a]; };
    double worst = -200.0;
    for (std::size_t n = std::max(lead, outer); n + outer < e.size() && n + lead < e.size(); ++n) {
        const double around = energy(n - outer, n - inner) + energy(n + inner + 1, n + outer + 1);
        const double rms = std::sqrt(around / double(2 * (outer - inner)) + 1e-24);
        worst = std::max(worst, 20.0 * std::log10(std::fabs(e[n]) / rms + 1e-12));
    }
    return worst;
}

double ms_step_db(const std::vector<float>& x, std::size_t from, std::size_t to, double sr) {
    const auto w = std::size_t(0.001 * sr);
    double prev = -200.0, largest = 0.0;
    for (std::size_t s = from; s + w <= to; s += w) {
        float peak = 1e-9f;
        for (std::size_t i = s; i < s + w; ++i) peak = std::max(peak, std::fabs(x[i]));
        const double d = 20.0 * std::log10(peak);
        if (prev > -60.0 && d > -60.0) largest = std::max(largest, std::fabs(d - prev));
        prev = d;
    }
    return largest;
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
    static AudioComponent registered = nullptr;
    if (registered) return registered;
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
    param_sync_backlog = reinterpret_cast<BacklogFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("spectr_param_sync_backlog_v1")));
    if (!design_backlog || !param_sync_backlog) {
        std::fprintf(stderr, "%s does not export the spectr_*_backlog_v1 counters\n", path.c_str());
        return nullptr;
    }
    registered = AudioComponentRegister(&desc, CFSTR("Pulp: routes probe (in-process)"), 1, factory);
    return registered;
}

struct Event { std::size_t at; AudioUnitParameterID id; float value; };

struct Input { std::vector<float> x; std::size_t pos = 0; };

OSStatus input_cb(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                  UInt32 frames, AudioBufferList* io) {
    auto* in = static_cast<Input*>(ctx);
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
        auto* d = static_cast<float*>(io->mBuffers[b].mData);
        for (UInt32 i = 0; i < frames; ++i) {
            const std::size_t at = in->pos + i;
            d[i] = at < in->x.size() ? in->x[at] : 0.0f;
        }
    }
    return noErr;
}

struct Render { std::vector<float> out; std::vector<double> us; std::vector<std::size_t> start; };

Render render(const Options& o, Input& input, std::vector<Event> events,
              Pacing pacing = Pacing::paced) {
    AudioComponent comp = register_bundle(o.bundle);
    if (!comp) { std::fprintf(stderr, "cannot load %s\n", o.bundle.c_str()); std::exit(2); }
    AudioUnit au = nullptr;
    if (AudioComponentInstanceNew(comp, &au) != noErr || !au) std::exit(2);
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = o.sr;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mFramesPerPacket = 1; fmt.mChannelsPerFrame = 2; fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = 4; fmt.mBytesPerPacket = 4;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));
    UInt32 maxf = 4096;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof(maxf));
    AURenderCallbackStruct cb{&input_cb, &input};
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (AudioUnitInitialize(au) != noErr) std::exit(2);
    if (pacing == Pacing::offline_flagged) {
        UInt32 offline = 1;
        if (AudioUnitSetProperty(au, kAudioUnitProperty_OfflineRender, kAudioUnitScope_Global, 0,
                                 &offline, sizeof(offline)) != noErr) {
            std::fprintf(stderr, "the AU rejected kAudioUnitProperty_OfflineRender\n");
            std::exit(2);
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    Render r;
    const std::size_t total = input.x.size();
    r.out.assign(total, 0.0f);
    std::vector<float> l(4096), rr(4096);
    std::size_t pos = 0, next = 0;
    while (pos < total) {
        const UInt32 frames = UInt32(std::min<std::size_t>(o.block, total - pos));
        std::vector<AudioUnitParameterEvent> batch;
        while (next < events.size() && events[next].at < pos + frames) {
            AudioUnitParameterEvent ev{};
            ev.scope = kAudioUnitScope_Global;
            ev.parameter = events[next].id;
            ev.eventType = kParameterEvent_Immediate;
            ev.eventValues.immediate.bufferOffset = UInt32(events[next].at > pos ? events[next].at - pos : 0);
            ev.eventValues.immediate.value = events[next].value;
            batch.push_back(ev);
            ++next;
        }
        if (!batch.empty()) AudioUnitScheduleParameters(au, batch.data(), UInt32(batch.size()));
        auto* abl = static_cast<AudioBufferList*>(std::calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer)));
        abl->mNumberBuffers = 2;
        abl->mBuffers[0] = {1, frames * 4, l.data()};
        abl->mBuffers[1] = {1, frames * 4, rr.data()};
        AudioTimeStamp ts{};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        ts.mSampleTime = Float64(pos);
        AudioUnitRenderActionFlags flags = 0;
        input.pos = pos;
        const auto t0 = std::chrono::steady_clock::now();
        const OSStatus st = AudioUnitRender(au, &flags, &ts, 0, frames, abl);
        const auto t1 = std::chrono::steady_clock::now();
        if (st != noErr) { std::fprintf(stderr, "render error %d\n", int(st)); std::exit(2); }
        const auto* ol = static_cast<const float*>(abl->mBuffers[0].mData);
        std::copy(ol, ol + frames, r.out.begin() + long(pos));
        std::free(abl);
        r.us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        r.start.push_back(pos);
        pos += frames;
        if (pacing == Pacing::paced) await_design_worker();
    }
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    return r;
}

// Paced vs offline-flagged render of the same automation. See the header.
int offline_equivalence(const Options& o) {
    const double seconds = 6.0;
    Input input;
    input.x.resize(std::size_t(seconds * o.sr));
    // Broadband material (three tones and a deterministic noise floor), so the
    // mask's whole shape -- not one bin's gain -- reaches the output.
    std::uint32_t seed = 0x5eed1234u;
    for (std::size_t n = 0; n < input.x.size(); ++n) {
        const double t = double(n) / o.sr;
        seed = seed * 1664525u + 1013904223u;
        const double noise = (double(seed >> 8) / double(1u << 24) - 0.5) * 0.05;
        input.x[n] = float(0.15 * std::sin(2 * kPi * 220 * t) + 0.1 * std::sin(2 * kPi * 2000 * t)
                           + 0.08 * std::sin(2 * kPi * 7000 * t) + noise);
    }
    const auto at = [&](double s) { return std::size_t(s * o.sr); };
    constexpr AudioUnitParameterID kIntensity = 5000;
    std::vector<Event> ev;
    ev.push_back({0, kCenter, 3.15f});
    ev.push_back({0, kWidth, 1.2f});
    for (int b = 0; b < 32; ++b)
        ev.push_back({0, band_gain(b), (b % 4 < 2) ? 0.0f : -30.0f});
    ev.push_back({0, kLfoEnabled, 1.0f});
    ev.push_back({0, kLfoShape, 0.0f});
    ev.push_back({0, kLfoRate, 1.0f});
    ev.push_back({0, kLfoDepth, 1.0f});
    for (int t = 0; t < 6; ++t) { ev.push_back({0, route_on(t), 0.0f}); ev.push_back({0, route_amount(t), 1.0f}); }
    // An LFO on Intensity and on the band window (Band shift), and a host
    // Intensity ramp 0 -> 100 % -> 30 %, one event per render call.
    ev.push_back({0, route_on(8), 1.0f});
    ev.push_back({0, route_amount(8), 0.6f});
    ev.push_back({0, route_on(4), 1.0f});
    ev.push_back({0, route_amount(4), 0.5f});
    for (std::size_t s = 0; s < at(seconds); s += o.block) {
        const double t = double(s) / o.sr;
        const double v = t < 3.0 ? t / 3.0 : 1.0 - 0.7 * std::min(1.0, (t - 3.0) / 2.0);
        ev.push_back({s, kIntensity, float(100.0 * v)});
    }

    Input a_in = input, b_in = input;
    const Render paced = render(o, a_in, ev, Pacing::paced);
    const Render bounce = render(o, b_in, ev, o.offline_flag ? Pacing::offline_flagged : Pacing::unpaced);
    double max_diff = 0.0, ref_peak = 0.0;
    std::size_t first = paced.out.size(), differing = 0;
    for (std::size_t n = 0; n < paced.out.size(); ++n) {
        const double d = std::fabs(double(paced.out[n]) - double(bounce.out[n]));
        ref_peak = std::max(ref_peak, std::fabs(double(paced.out[n])));
        if (d > 1e-6) { ++differing; first = std::min(first, n); }
        max_diff = std::max(max_diff, d);
    }
    const double diff_db = max_diff > 0.0 ? 20.0 * std::log10(max_diff / std::max(ref_peak, 1e-9)) : -999.0;
    std::printf("offline equivalence (%s): max |paced - bounce| = %.3g (%.1f dB re peak), "
                "%zu samples differ by > 1e-6, first at %.3f s\n",
                o.offline_flag ? "OfflineRender set" : "NO offline flag -- negative control",
                max_diff, diff_db, differing,
                first < paced.out.size() ? double(first) / o.sr : -1.0);
    // Tight: the two renders take the same samples through the same adopted
    // masks, so anything above float rounding is a schedule difference.
    const bool match = max_diff <= 1e-5;
    std::printf("%s\n", match ? "MATCH" : "DIFFER");
    return match ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--bundle") o.bundle = next();
        else if (a == "--sr") o.sr = std::atof(next().c_str());
        else if (a == "--block") o.block = UInt32(std::atoi(next().c_str()));
        else if (a == "--max-cost-ratio") o.max_cost_ratio = std::atof(next().c_str());
        else if (a == "--gate-cost") o.gate_cost = true;
        else if (a == "--offline-equivalence") o.offline_equivalence = true;
        else if (a == "--no-offline-flag") o.offline_flag = false;
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (o.bundle.empty()) { std::fprintf(stderr, "--bundle is required\n"); return 2; }
    if (o.offline_equivalence) return offline_equivalence(o);

    // Material: one steady tone. Its envelope is exactly the gain the mask
    // gives it, so a 1 ms envelope step is the gain step itself, and the
    // whitener predicts it so well that any discontinuity stands out.
    const double seconds = 8.0;
    Input input;
    input.x.resize(std::size_t(seconds * o.sr));
    for (std::size_t n = 0; n < input.x.size(); ++n) {
        const double t = double(n) / o.sr;
        input.x[n] = float(0.3 * std::sin(2 * kPi * 2000 * t));
    }
    const auto at = [&](double s) { return std::size_t(s * o.sr); };

    // Shared set-up: a one-decade-and-a-bit window and a smooth contour, so a
    // moving window is audible as a sweep; LFO 1 sine at 1 beat (0.5 s), full
    // depth; all routes off.
    std::vector<Event> base;
    base.push_back({0, kCenter, 3.15f});
    base.push_back({0, kWidth, 1.2f});
    for (int b = 0; b < 32; ++b) {
        const double x = (b + 0.5) / 32.0;
        base.push_back({0, band_gain(b), float(-18.0 + 18.0 * 0.5 * (1 - std::cos(2 * kPi * x)))});
    }
    base.push_back({0, kLfoEnabled, 1.0f});
    base.push_back({0, kLfoShape, 0.0f});
    base.push_back({0, kLfoRate, 1.0f});
    base.push_back({0, kLfoDepth, 1.0f});
    for (int t = 0; t < 6; ++t) { base.push_back({0, route_on(t), 0.0f}); base.push_back({0, route_amount(t), 1.0f}); }
    // The Output level target (10) as well: +-6 dB after Auto Gain, ramped
    // per sample. Auto Gain keeps its new-instance default (on): a static
    // shape is a constant gain, so the reference is the LFO alone.
    base.push_back({0, route_on(10), 0.0f});
    base.push_back({0, route_amount(10), 1.0f});

    // Each switch lands at an LFO crest or trough (the 0.5 s sine peaks at
    // 0.125 and 0.375 s into its cycle), where an un-ramped switch would move
    // the sound by the full excursion. Each edge is judged against a
    // REFERENCE render in which the same destination runs steadily the whole
    // time: a switch may move the sound no faster than that modulation itself
    // does.
    enum Ref { kBank, kPosition, kZoom, kOutput };
    // `gain_ramp`: the edge is a pure broadband gain ramp (the Output target),
    // which the whitened-residual detector cannot judge -- the whitener
    // predicts a steady tone, so ANY change of a gain's slope, however slow,
    // leaves a residual it scores as a spike (a 60 ms, 6 dB linear fade scored
    // 21-34 dB). Such an edge is judged by the envelope step against its
    // reference instead, and its spike is printed for the record.
    struct Edge { const char* name; double when; Ref ref; bool gain_ramp = false; };
    std::vector<Edge> edges;
    std::vector<Event> automated = base;
    // Bank, its Amount ramped 0 -> 100 % over 2 s (one event per render
    // call, as a host writes a drawn ramp), then switched off at a crest.
    automated.push_back({0, route_on(0), 1.0f});
    for (std::size_t s = 0; s <= at(2.0); s += o.block)
        automated.push_back({s, route_amount(0), float(double(s) / double(at(2.0)))});
    automated.push_back({at(2.625), route_on(0), 0.0f}); edges.push_back({"Bank off (crest)", 2.625, kBank});
    automated.push_back({at(3.125), route_on(4), 1.0f}); edges.push_back({"Band shift on (crest)", 3.125, kPosition});
    automated.push_back({at(3.875), route_on(4), 0.0f}); edges.push_back({"Band shift off (trough)", 3.875, kPosition});
    automated.push_back({at(4.625), route_on(5), 1.0f}); edges.push_back({"Band spread on (crest)", 4.625, kZoom});
    automated.push_back({at(5.125), route_amount(5), 0.2f}); edges.push_back({"Band spread Depth 100->20%", 5.125, kZoom});
    automated.push_back({at(5.875), route_on(5), 0.0f}); edges.push_back({"Band spread off (trough)", 5.875, kZoom});
    automated.push_back({at(6.625), route_on(0), 1.0f}); edges.push_back({"Bank on (crest)", 6.625, kBank});
    automated.push_back({at(7.125), route_on(10), 1.0f}); edges.push_back({"Output on (crest)", 7.125, kOutput, true});

    // The Output edge comes after Bank is back on, so its reference runs both.
    const auto reference = [&](int t, int also = -1) {
        std::vector<Event> ev = base;
        ev.push_back({0, route_on(t), 1.0f});
        if (also >= 0) ev.push_back({0, route_on(also), 1.0f});
        Input in = input;
        return render(o, in, ev);
    };
    const Render refs[4] = {reference(0), reference(4), reference(5), reference(10, 0)};
    const Render& ctrl = refs[kBank];
    Input ai = input;
    const Render autom = render(o, ai, automated);
    double ref_step[4];
    for (int r = 0; r < 4; ++r) ref_step[r] = ms_step_db(refs[r].out, at(1.0), at(7.5), o.sr);
    std::printf("free-running largest 1 ms step: Bank %.2f dB, Band shift %.2f dB, Band spread %.2f dB, "
                "Output %.2f dB\n", ref_step[0], ref_step[1], ref_step[2], ref_step[3]);
    if (const char* dump = std::getenv("SPECTR_ROUTES_DUMP")) {
        FILE* f = std::fopen(dump, "w");
        for (std::size_t n = 0; n < ctrl.out.size(); ++n)
            std::fprintf(f, "%g %g\n", ctrl.out[n], autom.out[n]);
        std::fclose(f);
    }
    // The control's 99th-percentile call, not its single costliest: one
    // scheduler hiccup on a shared machine is not the plug-in's cost.
    std::vector<double> sorted(ctrl.us.begin() + 8, ctrl.us.end());
    std::sort(sorted.begin(), sorted.end());
    if (const char* dump = std::getenv("SPECTR_ROUTES_DUMP")) {
        FILE* f = std::fopen(dump, "w");
        for (std::size_t n = 0; n < ctrl.out.size(); ++n)
            std::fprintf(f, "%g %g\n", ctrl.out[n], autom.out[n]);
        std::fclose(f);
    }
    const double ctrl_max = sorted[sorted.size() * 99 / 100];

    int bad = 0;
    const std::size_t lead = at(0.035);
    std::printf("%-26s %10s %10s %10s %10s %10s\n", "edge", "spike dB", "ref dB", "1ms step", "ref step", "cost us");
    for (const auto& e : edges) {
        const std::size_t edge = at(e.when);
        const std::size_t from = edge - at(0.01) - lead, to = edge + at(0.2) + lead;
        const auto& ref = refs[e.ref];
        const auto a = fit_whitener(ref.out, edge - at(0.3), edge);
        const double spike = spike_db(residual(autom.out, a, from, to), lead, o.sr);
        const double cspike = spike_db(residual(ref.out, a, from, to), lead, o.sr);
        const double step = ms_step_db(autom.out, edge - at(0.02), edge + at(0.3), o.sr);
        const double cstep = ref_step[e.ref];
        double cost = 0.0;
        for (std::size_t b = 0; b < autom.start.size(); ++b)
            if (autom.start[b] + o.block > edge && autom.start[b] < edge + at(0.08))
                cost = std::max(cost, autom.us[b]);
        const bool click = !e.gain_ramp && spike > std::max(cspike + 6.0, 20.0);
        // The tone's envelope is the gain the mask gives it: a switch may move
        // it no faster than the same modulation running freely, + 0.5 dB/ms.
        const bool stepped = step > cstep + 0.5;
        const bool slow = o.max_cost_ratio > 0 && cost > o.max_cost_ratio * ctrl_max;
        if (click || (slow && o.gate_cost) || stepped) ++bad;
        std::printf("%-26s %10.1f %10.1f %10.2f %10.2f %10.0f%s%s%s\n", e.name, spike, cspike, step, cstep, cost,
                    click ? "  CLICK" : "",
                    slow ? (o.gate_cost ? "  SLOW" : "  slow (tracked)") : "",
                    stepped ? "  STEP" : "");
    }
    // The Amount ramp: 1 ms envelope steps against the control's over the
    // same span (both carry the same LFO motion once the ramp is up).
    const double ramp_step = ms_step_db(autom.out, at(0.3), at(2.4), o.sr);
    const double ctrl_step = ref_step[kBank];
    std::printf("Depth ramp 0->100%% over 2 s: largest 1 ms step %.2f dB (control %.2f dB)\n",
                ramp_step, ctrl_step);
    if (ramp_step > ctrl_step + 0.5) { ++bad; std::printf("  RAMP STEP\n"); }
    std::printf("control p99 render call %.0f us (budget %.0f us)\n", ctrl_max,
                1e6 * o.block / o.sr);
    std::printf("%s: %d problem(s)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}
