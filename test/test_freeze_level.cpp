// Freeze should sound like the sound simply continuing: no level jump, no
// true-peak excursion, no change of pitch or tone colour when the hold takes
// over -- at the default hold length and at the longest one.
//
// Every row renders the product (the Spectr processor under a HeadlessHost,
// Freeze pressed with a sample-accurate parameter event, as a host delivers a
// tap) and compares the output just before the press with the output once the
// hold has settled. The reference the user compared by ear, bendr-pulp's
// freeze (RealtimePitchTimeProcessor as bendr-pulp configures it), is rendered
// on the same input by the report so its numbers sit beside the product's.
//
// Measures: loudness is BS.1770 K-weighted mean square over 400 ms windows
// (short-term LUFS without the gate); true peak is the 4x-oversampled sample
// peak; tone colour is the spectral centroid and octave-band levels of the
// mean power spectrum; pitch is the frequency of spectral peaks (Hann window,
// 16x zero-padded, parabolic interpolation on log magnitude).

#include <catch2/catch_test_macros.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/signal/fft.hpp>
#include <pulp/signal/realtime_pitch_time_processor.hpp>
#include <pulp/state/parameter_event_queue.hpp>

#include "spectr/freeze_source.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

using spectr::FreezeSource;
using spectr::MaskRenderMode;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::size_t samples(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * kRate));
}

struct Stereo {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
    void resize(std::size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
};

// ── Material ───────────────────────────────────────────────────────────────
// Deterministic synthesis only. Every programme is steady from well before the
// press: what is held is what is playing.

void normalise(Stereo& s, double peak) {
    double m = 1e-9;
    for (std::size_t n = 0; n < s.size(); ++n)
        m = std::max({m, std::abs(double(s.l[n])), std::abs(double(s.r[n]))});
    const double g = peak / m;
    for (std::size_t n = 0; n < s.size(); ++n) { s.l[n] *= float(g); s.r[n] *= float(g); }
}

void add_room(Stereo& s, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 0.001);
    for (std::size_t n = 0; n < s.size(); ++n) { s.l[n] += float(g(rng)); s.r[n] += float(g(rng)); }
}

double saw(double phase, double f) {
    double v = 0.0;
    for (int h = 1; h * f < 16000.0; ++h) v += std::sin(h * phase) / h;
    return v;
}

/// A 440 Hz sine at -10.5 dBFS.
Stereo sine_tone(double seconds) {
    Stereo s; s.resize(samples(seconds));
    for (std::size_t n = 0; n < s.size(); ++n)
        s.l[n] = s.r[n] = float(0.3 * std::sin(2 * kPi * 440.0 * double(n) / kRate));
    return s;
}

/// A steady C major triad of band-limited saws, the image slightly wide.
Stereo saw_chord(double seconds) {
    Stereo s; s.resize(samples(seconds));
    static constexpr double notes[] = {261.63, 329.63, 392.0};
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        double l = 0, r = 0;
        for (int v = 0; v < 3; ++v) {
            l += saw(2 * kPi * notes[v] * t + v, notes[v]);
            r += saw(2 * kPi * notes[v] * t + v + 0.4, notes[v]);
        }
        s.l[n] = float(l); s.r[n] = float(r);
    }
    normalise(s, 0.5);
    add_room(s, 3);
    return s;
}

/// A synth pad: A minor, three detuned saws a note (beating, as pads do),
/// through a gentle one-pole low-pass, voices spread across the image.
/// `change_at` > 0 moves to F major there (for the long-hold rows).
Stereo synth_pad(double seconds, double change_at = -1.0) {
    Stereo s; s.resize(samples(seconds));
    static constexpr double am[] = {220.0, 261.63, 329.63, 440.0};
    static constexpr double fm[] = {174.61, 220.0, 261.63, 349.23};
    static constexpr double detune[] = {-0.004, 0.0, 0.0042};
    double lp_l = 0, lp_r = 0;
    const double a = 1.0 - std::exp(-2 * kPi * 2500.0 / kRate);
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        // A 20 ms join so the input itself has no click.
        const double w = change_at > 0 ? std::clamp((t - change_at) / 0.02, 0.0, 1.0) : 0.0;
        double l = 0, r = 0;
        for (int v = 0; v < 4; ++v)
            for (int d = 0; d < 3; ++d) {
                const double pan = (d - 1) * 0.35 + (v % 2 ? 0.1 : -0.1);
                double x = 0;
                if (w < 1) x += (1 - w) * saw(2 * kPi * am[v] * (1 + detune[d]) * t + v + 2 * d, am[v]);
                if (w > 0) x += w * saw(2 * kPi * fm[v] * (1 + detune[d]) * t + v + 2 * d, fm[v]);
                l += x * (0.5 - pan); r += x * (0.5 + pan);
            }
        lp_l += a * (l - lp_l); lp_r += a * (r - lp_r);
        s.l[n] = float(lp_l); s.r[n] = float(lp_r);
    }
    normalise(s, 0.5);
    add_room(s, 5);
    return s;
}

/// Steady pink-ish noise (a texture, the case where magnitude averaging
/// changes the level most).
Stereo texture(double seconds) {
    Stereo s; s.resize(samples(seconds));
    std::mt19937 rng(9);
    std::normal_distribution<double> g(0.0, 1.0);
    double b[2][3] = {};
    for (std::size_t n = 0; n < s.size(); ++n)
        for (int ch = 0; ch < 2; ++ch) {
            const double x = g(rng);
            b[ch][0] = 0.99765 * b[ch][0] + x * 0.0990460;
            b[ch][1] = 0.96300 * b[ch][1] + x * 0.2965164;
            b[ch][2] = 0.57000 * b[ch][2] + x * 1.0526913;
            (ch ? s.r : s.l)[n] = float(b[ch][0] + b[ch][1] + b[ch][2] + x * 0.1848);
        }
    normalise(s, 0.5);
    return s;
}

// ── Measures ───────────────────────────────────────────────────────────────

struct Biquad {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double operator()(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/// BS.1770 K-weighting at 48 kHz.
std::vector<double> k_weighted(const std::vector<float>& x) {
    Biquad shelf{1.53512485958697, -2.69169618940638, 1.19839281085285,
                 -1.69065929318241, 0.73248077421585};
    Biquad hp{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    std::vector<double> y(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) y[n] = hp(shelf(x[n]));
    return y;
}

struct Weighted { std::vector<double> l, r; };
Weighted k_weighted(const Stereo& s) { return {k_weighted(s.l), k_weighted(s.r)}; }

double lufs(const Weighted& k, std::size_t from, std::size_t n) {
    double e = 0;
    for (std::size_t i = from; i < from + n; ++i) e += k.l[i] * k.l[i] + k.r[i] * k.r[i];
    return -0.691 + 10 * std::log10(e / double(n) + 1e-30);
}

/// 4x-oversampled peak in dBFS (windowed-sinc interpolation, 64 taps).
double true_peak_db(const Stereo& s, std::size_t from, std::size_t to) {
    constexpr int kTaps = 16; // each side
    double peak = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const auto& x = ch ? s.r : s.l;
        for (std::size_t n = std::max<std::size_t>(from, kTaps); n < std::min(to, x.size() - kTaps); ++n)
            for (int p = 0; p < 4; ++p) {
                double v = 0;
                if (p == 0) v = x[n];
                else {
                    const double frac = p / 4.0;
                    for (int k = -kTaps + 1; k <= kTaps; ++k) {
                        const double d = k - frac;
                        const double sinc = std::sin(kPi * d) / (kPi * d);
                        const double win = 0.5 + 0.5 * std::cos(kPi * d / kTaps);
                        v += x[n + std::size_t(k + kTaps) - kTaps] * sinc * win;
                    }
                }
                peak = std::max(peak, std::abs(v));
            }
    }
    return 20 * std::log10(peak + 1e-30);
}

/// Mean power spectrum (Hann 8192, hop 2048, both channels) over [from, to).
std::vector<double> mean_spectrum(const Stereo& s, std::size_t from, std::size_t to) {
    constexpr int N = 8192;
    static const pulp::signal::Fft fft(N);
    std::vector<float> buf(N);
    std::vector<std::complex<float>> spec(N);
    std::vector<double> power(N / 2 + 1, 0.0);
    int frames = 0;
    for (std::size_t at = from; at + N <= to; at += 2048)
        for (int ch = 0; ch < 2; ++ch) {
            const auto& x = ch ? s.r : s.l;
            for (int n = 0; n < N; ++n)
                buf[std::size_t(n)] = float(x[at + std::size_t(n)] * (0.5 - 0.5 * std::cos(2 * kPi * n / N)));
            fft.forward_real(buf.data(), spec.data());
            for (int k = 0; k <= N / 2; ++k) power[std::size_t(k)] += std::norm(spec[std::size_t(k)]);
            ++frames;
        }
    for (auto& p : power) p /= std::max(frames, 1);
    return power;
}

double centroid_hz(const std::vector<double>& power) {
    double num = 0, den = 0;
    const double bin = kRate / double((power.size() - 1) * 2);
    for (std::size_t k = 1; k < power.size(); ++k) {
        const double f = double(k) * bin;
        if (f < 20.0 || f > 20000.0) continue;
        num += f * power[k]; den += power[k];
    }
    return num / std::max(den, 1e-30);
}

/// Level of each octave band 63 Hz .. 8 kHz, dB.
std::vector<double> octave_bands(const std::vector<double>& power) {
    const double bin = kRate / double((power.size() - 1) * 2);
    std::vector<double> out;
    for (double centre = 62.5; centre <= 8000.0; centre *= 2) {
        double e = 0;
        for (std::size_t k = 1; k < power.size(); ++k) {
            const double f = double(k) * bin;
            if (f >= centre / std::sqrt(2.0) && f < centre * std::sqrt(2.0)) e += power[k];
        }
        out.push_back(10 * std::log10(e + 1e-30));
    }
    return out;
}

/// The `count` strongest spectral peaks of the left channel over
/// [from, from + n), by frequency. Hann, zero-padded to 262144, parabolic on
/// log magnitude; peaks closer than 20 Hz to a stronger one are skipped.
std::vector<double> peaks_hz(const Stereo& s, std::size_t from, std::size_t n, int count) {
    constexpr int N = 262144;
    static const pulp::signal::Fft fft(N);
    std::vector<float> buf(N, 0.0f);
    std::vector<std::complex<float>> spec(N);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = float(s.l[from + i] * (0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n))));
    fft.forward_real(buf.data(), spec.data());
    std::vector<double> mag(N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) mag[std::size_t(k)] = std::log(std::abs(spec[std::size_t(k)]) + 1e-20);
    const double bin = kRate / N;
    std::vector<std::pair<double, double>> found; // (log mag, hz)
    for (std::size_t k = 2; k + 2 < mag.size() && double(k) * bin < 5000.0; ++k)
        if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1]) {
            const double a = mag[k - 1], b = mag[k], c = mag[k + 1];
            const double d = 0.5 * (a - c) / (a - 2 * b + c);
            found.push_back({b, (double(k) + d) * bin});
        }
    std::sort(found.begin(), found.end(), [](auto& x, auto& y) { return x.first > y.first; });
    std::vector<double> out;
    for (const auto& [m, hz] : found) {
        if (int(out.size()) == count) break;
        if (std::any_of(out.begin(), out.end(), [&](double o) { return std::abs(o - hz) < 20.0; })) continue;
        out.push_back(hz);
    }
    std::sort(out.begin(), out.end());
    return out;
}

double cents(double a, double b) { return 1200.0 * std::log2(b / a); }

// ── Renderers ──────────────────────────────────────────────────────────────

struct Render {
    Stereo out;
    int latency = 0;
    std::size_t engaged_at = 0; // output sample the hold first reached; 0 = never
};

/// The Spectr processor, Freeze pressed at `press` by a parameter event.
Render render_spectr(const Stereo& in, std::size_t press, double hold_seconds,
                     MaskRenderMode mode, int block = 256, std::size_t release = SIZE_MAX) {
    pulp::format::HeadlessHost host{spectr::create_spectr};
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(mode));
    plugin->set_freeze_hold_seconds(hold_seconds);
    host.prepare(kRate, block);
    Render r;
    r.latency = plugin->latency_samples();
    r.out.resize(in.size());
    pulp::midi::MidiBuffer mi, mo;
    for (std::size_t pos = 0; pos < in.size(); pos += std::size_t(block)) {
        const auto n = std::min<std::size_t>(std::size_t(block), in.size() - pos);
        pulp::state::ParameterEventQueue events;
        pulp::format::ProcessContext ctx;
        const bool press_here = press >= pos && press < pos + n;
        const bool release_here = release >= pos && release < pos + n;
        if (press_here) REQUIRE(events.push({spectr::kParamFreeze, std::int32_t(press - pos), 1.0f, 0}));
        if (release_here) REQUIRE(events.push({spectr::kParamFreeze, std::int32_t(release - pos), 0.0f, 0}));
        pulp::audio::Buffer<float> ib(2, n), ob(2, n);
        std::copy_n(in.l.begin() + long(pos), n, ib.channel(0).begin());
        std::copy_n(in.r.begin() + long(pos), n, ib.channel(1).begin());
        const float* ip[] = {ib.channel(0).data(), ib.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, n);
        auto ov = ob.view();
        host.process(ov, iv, mi, mo, events, ctx);
        if (press_here) host.state().set_value(spectr::kParamFreeze, 1.0f);
        if (release_here) host.state().set_value(spectr::kParamFreeze, 0.0f);
        if (r.engaged_at == 0 && plugin->freeze_source().hold_audible())
            r.engaged_at = pos + n + std::size_t(r.latency);
        std::copy(ob.channel(0).begin(), ob.channel(0).end(), r.out.l.begin() + long(pos));
        std::copy(ob.channel(1).begin(), ob.channel(1).end(), r.out.r.begin() + long(pos));
    }
    return r;
}

/// bendr-pulp's freeze: RealtimePitchTimeProcessor configured as bendr-pulp's
/// reference processor configures it, unity pitch, 100% wet. It has no hold
/// length: its capture is FreezeHold's reference 8 frames.
Render render_bendr(const Stereo& in, std::size_t press, std::size_t release = SIZE_MAX) {
    pulp::signal::RealtimePitchTimeProcessor pitch;
    pulp::signal::RealtimePitchTimeConfig config;
    config.quality = pulp::signal::PitchTimeQuality::quality;
    config.channels = 2;
    config.max_block = 256;
    config.formant_mode = pulp::signal::FormantMode::preserve;
    config.noise_morphing = true;
    REQUIRE(pitch.prepare(kRate, config) == pulp::signal::PitchTimePrepareStatus::prepared);
    pitch.set_pitch_semitones(0.0f);
    pitch.set_formant_semitones(0.0f);
    Render r;
    r.latency = pitch.latency_samples();
    r.out.resize(in.size());
    std::size_t pos = 0;
    while (pos < in.size()) {
        std::size_t end = std::min<std::size_t>(pos + 256, in.size());
        for (auto edge : {press, release}) if (edge > pos && edge < end) end = edge;
        pitch.set_frozen(pos >= press && pos < release);
        const float* i[] = {in.l.data() + pos, in.r.data() + pos};
        float* o[] = {r.out.l.data() + pos, r.out.r.data() + pos};
        pitch.process(i, o, int(end - pos));
        if (r.engaged_at == 0 && pitch.is_frozen()) r.engaged_at = end + std::size_t(r.latency);
        pos = end;
    }
    return r;
}

// ── The comparison ─────────────────────────────────────────────────────────

struct Change {
    double d_lufs = 0;        // settled hold minus live, 400 ms windows
    double d_lufs_late = 0;   // the same, 1.5 s into the hold
    double tp_live = 0;       // true peak of the live output over the 2 s before
    double tp_hold = 0;       // true peak of the hold over its first 2 s
    double d_centroid = 0;    // percent
    double worst_band = 0;    // largest octave-band change, dB
    double worst_cents = 0;   // largest peak-frequency change, cents
    double engage_ms = -1;    // press to the hold first audible
    double settling = 0;      // the hold's first 300 ms after the fade minus its level 1.5-2.5 s in
};

/// Compare the render's live output before the press with its held output.
/// `window_from` (seconds before the press) is where the loudness reference
/// starts: the live window the hold is meant to stand for.
Change compare(const Render& r, std::size_t press, int peaks, double live_window = 0.4) {
    const std::size_t lat = std::size_t(r.latency);
    const std::size_t p = press + lat;
    const auto k = k_weighted(r.out);
    const std::size_t w = samples(0.4);
    Change c;
    const std::size_t live_from = p - samples(0.1) - samples(live_window);
    const double live = lufs(k, live_from, samples(live_window));
    c.d_lufs = lufs(k, p + samples(0.4), w) - live;
    c.d_lufs_late = lufs(k, p + samples(1.5), w) - live;
    c.settling = lufs(k, p + samples(0.07), samples(0.3)) - lufs(k, p + samples(1.5), samples(1.0));
    c.tp_live = true_peak_db(r.out, p - samples(2.0), p - samples(0.05));
    c.tp_hold = true_peak_db(r.out, p + samples(0.2), p + samples(2.2));
    const auto before = mean_spectrum(r.out, live_from, p - samples(0.1));
    const auto after = mean_spectrum(r.out, p + samples(0.4), p + samples(0.4) + samples(live_window));
    c.d_centroid = 100.0 * (centroid_hz(after) / centroid_hz(before) - 1.0);
    const auto bb = octave_bands(before), ba = octave_bands(after);
    for (std::size_t i = 0; i < bb.size(); ++i)
        if (bb[i] > -60.0) c.worst_band = std::max(c.worst_band, std::abs(ba[i] - bb[i]));
    if (peaks > 0) {
        // Each of the strongest partials before the press, against the
        // nearest partial found after it.
        const auto pb = peaks_hz(r.out, p - samples(0.5), w, peaks);
        const auto pa = peaks_hz(r.out, p + samples(0.4), w, 3 * peaks);
        for (const double hz : pb) {
            double best = 1e9;
            for (const double a : pa) if (std::abs(cents(hz, a)) < std::abs(best)) best = cents(hz, a);
            c.worst_cents = std::max(c.worst_cents, std::abs(best));
        }
    }
    if (r.engaged_at > 0) c.engage_ms = double(r.engaged_at - lat - press) * 1000.0 / kRate;
    return c;
}

void print_row(const char* who, const char* what, const Change& c) {
    std::printf("%-22s %-12s dLUFS %+6.2f (late %+6.2f) | TP live %6.2f hold %6.2f (%+5.2f) | "
                "centroid %+6.1f%% | worst band %5.2f dB | pitch %6.2f c | engage %7.1f ms | settling %+5.2f dB\n",
                who, what, c.d_lufs, c.d_lufs_late, c.tp_live, c.tp_hold, c.tp_hold - c.tp_live,
                c.d_centroid, c.worst_band, c.worst_cents, c.engage_ms, c.settling);
}

void write_wav(const std::string& path, const Stereo& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const std::uint32_t frames = std::uint32_t(s.size());
    const std::uint32_t data = frames * 2 * 4;
    const auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(3); u16(2); u32(48000); u32(48000 * 8); u16(8); u16(32);
    std::fwrite("data", 1, 4, f); u32(data);
    for (std::size_t n = 0; n < s.size(); ++n) { std::fwrite(&s.l[n], 4, 1, f); std::fwrite(&s.r[n], 4, 1, f); }
    std::fclose(f);
}

/// Trim a render's latency off its front so every file lines up with the input.
Stereo aligned(const Render& r) {
    Stereo s;
    const auto lat = std::size_t(r.latency);
    s.l.assign(r.out.l.begin() + long(lat), r.out.l.end());
    s.r.assign(r.out.r.begin() + long(lat), r.out.r.end());
    return s;
}

struct Programme { const char* name; Stereo audio; int peaks; };

const std::vector<Programme>& programmes() {
    static const std::vector<Programme> p = {
        {"sine 440", sine_tone(6.0), 1},
        {"saw chord", saw_chord(6.0), 3},
        {"synth pad", synth_pad(6.0), 0},
        {"texture", texture(6.0), 0},
    };
    return p;
}

constexpr double kHolds[] = {FreezeSource::kDefaultHoldSeconds, FreezeSource::kMaxHoldSeconds};
const char* hold_name(double s) { return s < 1.0 ? "85 ms" : "2 s"; }

} // namespace

TEST_CASE("Freeze level report: loudness, peak, colour and pitch across the engage",
          "[.][freeze-level-report]") {
    const std::size_t press = samples(3.0);
    for (const auto& prog : programmes()) {
        std::printf("-- %s\n", prog.name);
        for (const double hold : kHolds)
            for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
                const auto r = render_spectr(prog.audio, press, hold, mode);
                const std::string who = std::string("Spectr ")
                    + (mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking");
                print_row(who.c_str(), hold_name(hold), compare(r, press, prog.peaks));
            }
        print_row("bendr-pulp", "fixed", compare(render_bendr(prog.audio, press), press, prog.peaks));
        // The programme's own drift between the same two windows, unpressed.
        print_row("unpressed (control)", "-", compare(render_spectr(prog.audio, SIZE_MAX - 1, kHolds[0],
                                                                    MaskRenderMode::zero_latency),
                                                      press, prog.peaks));
    }

    // A pad that changed chord 1.4 s before the press: a 2 s hold spans both.
    const auto change = synth_pad(6.0, 1.6);
    std::printf("-- synth pad, chord change 1.4 s before the press (loudness against the 2 s window)\n");
    for (const double hold : kHolds) {
        const auto r = render_spectr(change, press, hold, MaskRenderMode::zero_latency);
        print_row("Spectr Tracking", hold_name(hold), compare(r, press, 0, hold < 1.0 ? 0.4 : 1.8));
    }
    print_row("bendr-pulp", "fixed", compare(render_bendr(change, press), press, 0));

    // A press 1 s after the stream starts, at the longest hold.
    std::printf("-- press 1 s after the stream starts\n");
    for (const double hold : kHolds) {
        const auto r = render_spectr(programmes()[2].audio, samples(1.0), hold, MaskRenderMode::zero_latency);
        std::printf("Spectr Tracking %-6s engage %.1f ms after the press\n", hold_name(hold),
                    r.engaged_at ? double(r.engaged_at - std::size_t(r.latency) - samples(1.0)) * 1000.0 / kRate : -1.0);
    }

    // Files to A/B by ear: one input, the pad changing chord, Freeze pressed
    // at 3 s and released at 6 s. Written only when asked for.
    if (const char* dir = std::getenv("SPECTR_FREEZE_LEVEL_WAV_DIR")) {
        const char* tag = std::getenv("SPECTR_FREEZE_LEVEL_WAV_TAG");
        const std::string t = tag ? tag : "spectr";
        const auto input = synth_pad(8.0, 1.6);
        const std::size_t release = samples(6.0);
        write_wav(std::string(dir) + "/input.wav", input);
        for (const double hold : kHolds) {
            const auto r = render_spectr(input, press, hold, MaskRenderMode::zero_latency, 256, release);
            write_wav(std::string(dir) + "/" + t + "-" + (hold < 1.0 ? "85ms" : "2s") + ".wav", aligned(r));
            const auto b = render_bendr(input, press, release);
            write_wav(std::string(dir) + "/bendr-" + (hold < 1.0 ? "85ms" : "2s") + ".wav", aligned(b));
        }
    }
}

TEST_CASE("Freeze engage trace: 10 ms loudness across the first 600 ms", "[.][freeze-engage-trace]") {
    // Short-term K-weighted level, 10 ms windows, relative to the settled
    // hold (1.5-2.5 s after the press). A single smooth transition reads as a
    // monotone glide from the live level to 0 dB; a second onset reads as a
    // step or a bump after the fade.
    const std::size_t press = samples(3.0);
    const char* only = std::getenv("SPECTR_FREEZE_TRACE_ONLY");
    for (const auto& prog : programmes()) {
        if (only && std::string(only) != prog.name) continue;
        const auto trace = [&](const char* who, const Render& r) {
            const auto k = k_weighted(r.out);
            const std::size_t p = press + std::size_t(r.latency);
            const double settled = lufs(k, p + samples(1.5), samples(1.0));
            const double live = lufs(k, p - samples(0.5), samples(0.4));
            std::printf("%-10s %-16s live %+5.2f |", prog.name, who, live - settled);
            for (int ms = -20; ms < 600; ms += 10)
                std::printf(" %+5.1f", lufs(k, p + samples(ms / 1000.0), samples(0.01)) - settled);
            std::printf("\n");
        };
        trace("spectr-85ms", render_spectr(prog.audio, press, kHolds[0], MaskRenderMode::zero_latency));
        trace("spectr-2s", render_spectr(prog.audio, press, kHolds[1], MaskRenderMode::zero_latency));
        trace("bendr", render_bendr(prog.audio, press));
        // Unpressed: the programme's own 10 ms fluctuation.
        trace("unpressed", render_spectr(prog.audio, SIZE_MAX - 1, kHolds[0], MaskRenderMode::zero_latency));
    }
}

// ── Acceptance ─────────────────────────────────────────────────────────────

namespace {

/// The hold's level against the live output over the stretch of input it was
/// analysed from (the capture window plus one analysis window, ending at the
/// press), and the hold's first 300 ms after the fade against its own level
/// 1.5-2.5 s in.
struct Level { double against_window = 0; double settling = 0; };

Level level_of(const Render& r, std::size_t press, double hold_seconds) {
    const auto k = k_weighted(r.out);
    const std::size_t p = press + std::size_t(r.latency);
    const std::size_t analysed = samples(hold_seconds) + FreezeSource::kFftSize;
    Level l;
    l.against_window = lufs(k, p + samples(0.4), samples(1.0)) - lufs(k, p - analysed, analysed);
    l.settling = lufs(k, p + samples(0.07), samples(0.3)) - lufs(k, p + samples(1.5), samples(1.0));
    return l;
}

} // namespace

TEST_CASE("The hold is stationary from its first frame", "[freeze][level]") {
    // Heard as "it freezes something, then freezes it": a hold that enters as
    // a coherent copy of the moment and sags, over a few hundred ms, into a
    // quieter steady state -- neighbouring bins of one partial drifting apart
    // at the different frequencies they measured, and a level match that
    // kept following them. The first 300 ms after the fade must already be
    // the settled hold.
    const std::size_t press = samples(3.0);
    const Stereo change = synth_pad(6.0, 1.6);
    const std::pair<const char*, const Stereo*> material[] = {
        {"saw chord", &programmes()[1].audio}, {"synth pad", &programmes()[2].audio},
        {"texture", &programmes()[3].audio}, {"pad after a chord change", &change}};
    for (const auto& [name, audio] : material)
        for (const double hold : kHolds) {
            const auto r = render_spectr(*audio, press, hold, MaskRenderMode::zero_latency);
            const auto l = level_of(r, press, hold);
            INFO(name << " at " << hold_name(hold) << ": the first 300 ms against the settled hold "
                 << l.settling << " dB");
            CHECK(std::abs(l.settling) <= 0.5);
        }
    // Control: the same instrument reads a planted sag of the size the hold
    // had -- the unpressed chord with 1.3 dB more gain at the engage, gone
    // 0.5 s later.
    auto r = render_spectr(programmes()[1].audio, SIZE_MAX - 1, kHolds[0], MaskRenderMode::zero_latency);
    const std::size_t p = press + std::size_t(r.latency);
    for (std::size_t n = p; n < p + samples(0.5); ++n) {
        const double g = std::pow(10.0, 1.3 * (1.0 - double(n - p) / double(samples(0.5))) / 20.0);
        r.out.l[n] = float(r.out.l[n] * g);
        r.out.r[n] = float(r.out.r[n] * g);
    }
    CHECK(level_of(r, press, kHolds[0]).settling > 0.5);
}

TEST_CASE("The hold plays at the level, peak and pitch of the sound it holds", "[freeze][level]") {
    const std::size_t press = samples(3.0);
    for (const auto& prog : programmes())
        for (const double hold : kHolds)
            for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
                const auto r = render_spectr(prog.audio, press, hold, mode);
                const auto l = level_of(r, press, hold);
                const auto c = compare(r, press, prog.peaks);
                INFO(prog.name << " at " << hold_name(hold) << ", "
                     << (mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking")
                     << ": hold against the analysed window " << l.against_window << " dB, true peak "
                     << c.tp_hold << " against live " << c.tp_live << " dBTP, worst partial "
                     << c.worst_cents << " cents");
                CHECK(std::abs(l.against_window) <= 0.5);
                if (prog.peaks > 0) CHECK(c.worst_cents <= 2.0);
                // Deterministic material only: a noise hold's peaks are as
                // random as the noise's own.
                if (std::string(prog.name) != "texture") CHECK(c.tp_hold <= c.tp_live + 0.5);
            }
}

