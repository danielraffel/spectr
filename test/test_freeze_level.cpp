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
#include <array>
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
Stereo synth_pad(double seconds, double change_at = -1.0, bool detuned = true) {
    Stereo s; s.resize(samples(seconds));
    static constexpr double am[] = {220.0, 261.63, 329.63, 440.0};
    static constexpr double fm[] = {174.61, 220.0, 261.63, 349.23};
    static constexpr double spread[] = {-0.004, 0.0, 0.0042};
    const double detune[] = {detuned ? spread[0] : 0.0, 0.0, detuned ? spread[2] : 0.0};
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
    std::int64_t loop_length = 0; // the loop's period, if it looped
};

/// The Spectr processor, Freeze pressed at `press` by a parameter event.
Render render_spectr(const Stereo& in, std::size_t press, double hold_seconds,
                     MaskRenderMode mode, int block = 256, std::size_t release = SIZE_MAX) {
    pulp::format::HeadlessHost host{spectr::create_spectr};
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(mode));
    plugin->set_freeze_seconds_override(hold_seconds);
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
    r.loop_length = plugin->freeze_source().looping() ? plugin->freeze_source().loop_length() : 0;
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
const char* hold_name(double s) { return s < 0.1 ? "85 ms" : s < 1.0 ? "0.2 s" : "2 s"; }
/// Hold lengths the hold is spectral at: the default and the longest below
/// the loop.
constexpr double kSpectralHolds[] = {FreezeSource::kDefaultHoldSeconds, 0.2};

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
        for (const double hold : kSpectralHolds) {
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
        for (const double hold : {kSpectralHolds[0], kSpectralHolds[1], FreezeSource::kMaxHoldSeconds})
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


// ── The loop ───────────────────────────────────────────────────────────────

namespace {

/// A phrase: a note every 0.25 s from a seeded sequence (a harmonic tone with
/// a short attack and a decay), so every quarter second of it differs from
/// the next and a loop of it can be told from any other stretch.
Stereo phrase(double seconds) {
    Stereo s; s.resize(samples(seconds));
    std::mt19937 rng(17);
    std::uniform_int_distribution<int> step(0, 11);
    const double note = 0.25;
    for (std::size_t k = 0; double(k) * note < seconds; ++k) {
        const double hz = 196.0 * std::pow(2.0, step(rng) / 12.0);
        const auto start = samples(double(k) * note);
        for (std::size_t i = 0; i < samples(note) && start + i < s.size(); ++i) {
            const double t = double(i) / kRate;
            const double env = std::min(1.0, t / 0.005) * std::exp(-t / 0.12);
            double v = 0;
            for (int h = 1; h <= 6; ++h) v += std::sin(2 * kPi * h * hz * t) / (h * h);
            s.l[start + i] += float(0.3 * env * v);
            s.r[start + i] += float(0.28 * env * v);
        }
    }
    add_room(s, 7);
    return s;
}

double correlation(const std::vector<float>& a, std::size_t from_a,
                   const std::vector<float>& b, std::size_t from_b, std::size_t n) {
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        ab += double(a[from_a + i]) * b[from_b + i];
        aa += double(a[from_a + i]) * a[from_a + i];
        bb += double(b[from_b + i]) * b[from_b + i];
    }
    return ab / std::sqrt(aa * bb + 1e-30);
}

/// A linear-prediction whitener: a waveform discontinuity is energy the
/// signal's own recent past does not predict (as in test_freeze_click.cpp).
constexpr int kOrder = 32;

/// Prediction-error filter [1, a1..ap] fitted to x[from, to) (Hann windowed
/// autocorrelation, Levinson-Durbin, a tiny ridge so silence is well posed).
std::vector<double> fit_whitener(const std::vector<float>& x, std::size_t from, std::size_t to) {
    const std::size_t n = to - from;
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1)));
    std::vector<double> r(kOrder + 1, 0.0);
    for (int k = 0; k <= kOrder; ++k)
        for (std::size_t i = std::size_t(k); i < n; ++i) r[std::size_t(k)] += w[i] * w[i - std::size_t(k)];
    r[0] = r[0] * (1.0 + 1e-6) + 1e-12;
    std::vector<double> a(kOrder + 1, 0.0), tmp(kOrder + 1);
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
        for (int k = 0; k <= kOrder && n >= std::size_t(k); ++k)
            acc += a[std::size_t(k)] * x[n - std::size_t(k)];
        e[n - from] = acc;
    }
    return e;
}


/// The worst whitened sample over [from, to) of `x` against the whitened
/// RMS in the 4 ms either side of it (excluding its own 0.5 ms), in dB; the
/// predictor fitted to the 0.3 s of `x` before `from`.
double spike_db(const Stereo& x, std::size_t from, std::size_t to) {
    const std::size_t outer = samples(0.004), inner = samples(0.0005);
    double worst = -200.0;
    for (const auto* c : {&x.l, &x.r}) {
        const auto a = fit_whitener(*c, from - samples(0.3), from);
        const auto e = residual(*c, a, from - outer, to + outer);
        std::vector<double> prefix(e.size() + 1, 0.0);
        for (std::size_t i = 0; i < e.size(); ++i) prefix[i + 1] = prefix[i] + e[i] * e[i];
        for (std::size_t n = outer; n + outer < e.size(); ++n) {
            const double around = prefix[n - inner] - prefix[n - outer]
                                + prefix[n + outer + 1] - prefix[n + inner + 1];
            const double rms = std::sqrt(around / double(2 * (outer - inner)) + 1e-24);
            worst = std::max(worst, 20.0 * std::log10(std::abs(e[n]) / rms + 1e-12));
        }
    }
    return worst;
}

/// The freeze source alone, Freeze on from `press` to the end.
struct Looped {
    Stereo out;
    std::int64_t length = 0;
    std::size_t engaged = 0; // the sample the engage fade starts at
    /// Where the loop's first pass began: the engage, which is the loop's
    /// own seam (its start plays there on every later pass).
    std::size_t origin() const { return engaged; }
};

Looped loop_source(const Stereo& in, std::size_t press, double hold, int crossfade = 0) {
    FreezeSource source;
    REQUIRE(source.prepare(kRate, 2));
    source.set_hold_seconds(hold);
    if (crossfade > 0) source.set_crossfade_samples(crossfade);
    Looped r;
    r.out.resize(in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += 128) {
        const auto n = std::min<std::size_t>(128, in.size() - pos);
        source.set_frozen(pos >= press);
        const float* i[] = {in.l.data() + pos, in.r.data() + pos};
        float* o[] = {r.out.l.data() + pos, r.out.r.data() + pos};
        source.process_block(i, o, 2, int(n));
        // Blocks of 128 end on every hop boundary, where the fade starts.
        if (r.engaged == 0 && source.hold_audible()) r.engaged = pos + n;
    }
    REQUIRE(source.looping());
    r.length = source.loop_length();
    return r;
}

} // namespace

TEST_CASE("A long Hold length loops the last Hold-length seconds of the input", "[freeze][loop]") {
    // Hold length is the length of what is frozen and looped: at 2 s the
    // output after the press repeats the 2 s of input before it, pass after
    // pass -- through the product, in both Latency modes. Measured as the
    // correlation of the output with the unpressed output one and two loop
    // lengths earlier. Control: the same instrument a quarter second off
    // the loop's lag reads the phrase's other notes, not a repeat.
    const auto input = phrase(10.0);
    const std::size_t press = samples(4.0);
    for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        const auto pressed = render_spectr(input, press, 2.0, mode);
        const auto live = render_spectr(input, SIZE_MAX - 1, 2.0, mode);
        const std::size_t p = press + std::size_t(pressed.latency);
        const std::size_t n = samples(1.5);
        // The loop's length: the lag, within 10 ms of 2 s, at which the
        // first pass best matches the live output.
        std::size_t length = samples(2.0);
        double best = -2.0;
        for (std::size_t lag = samples(1.99); lag <= samples(2.01); ++lag) {
            const double c = correlation(pressed.out.l, p + samples(0.1), live.out.l,
                                         p + samples(0.1) - lag, samples(0.2));
            if (c > best) { best = c; length = lag; }
        }
        INFO((mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking") << ": loop of "
             << double(length) / kRate << " s");
        // The processor keeps a loop to its length exactly (a musical
        // Length must stay on the host's grid), so the period is 2 s to the
        // sample; the first pass may start a few ms either side of 2 s back,
        // where the engage's seam matched best.
        INFO("loop period " << pressed.loop_length << " samples");
        CHECK(pressed.loop_length == std::int64_t(samples(2.0)));
        const auto period = std::size_t(pressed.loop_length);
        // First pass (after the engage fade), second pass (after its seam).
        const double first = correlation(pressed.out.l, p + samples(0.1), live.out.l,
                                         p + samples(0.1) - length, n);
        const double second = correlation(pressed.out.l, p + period + samples(0.1), live.out.l,
                                          p + samples(0.1) - length, n);
        const double off = correlation(pressed.out.l, p + samples(0.1), live.out.l,
                                       p + samples(0.1) - length + samples(0.25), n);
        INFO("first pass " << first << ", second pass " << second << ", a quarter second off " << off);
        CHECK(first > 0.99);
        CHECK(second > 0.99);
        CHECK(off < 0.5);
    }
}

TEST_CASE("A loop's seams are level and click-free", "[freeze][loop]") {
    // Every pass through the loop's start crossfades from the audio that
    // followed its end into its start. Across each seam, over the fade: the
    // output's level against the level either side of it (within 1 dB, on
    // the steady material -- the phrase's notes rise and fall by
    // themselves), and its worst spike -- a whitened sample against its own
    // neighbourhood, as the click tests score one -- against the worst the
    // loop has anywhere else (no more than 3 dB above it). Control: a
    // -20 dBFS step planted at a seam (a plain click), which the spike
    // measure must see.
    const std::pair<const char*, Stereo> material[] = {
        {"phrase", phrase(8.0)}, {"synth pad", synth_pad(8.0)},
        {"saw chord", saw_chord(8.0)}, {"texture", texture(8.0)}};
    const auto kink = [](const Stereo& x, std::size_t from, std::size_t to) {
        return spike_db(x, from, to);
    };
    const auto rms = [](const Stereo& x, std::size_t from, std::size_t n) {
        double e = 0;
        for (std::size_t i = from; i < from + n; ++i) e += double(x.l[i]) * x.l[i] + double(x.r[i]) * x.r[i];
        return 10 * std::log10(e / double(2 * n) + 1e-30);
    };
    const std::size_t press = samples(3.0);
    const std::size_t fade = samples(FreezeSource::kCrossfadeSeconds);
    for (const auto& [name, input] : material)
        for (const double hold : {0.5, 2.0}) {
            const auto r = loop_source(input, press, hold);
            const auto length = std::size_t(r.length);
            double worst_level = 0, worst_kink = -200, elsewhere = -200;
            for (int pass = 1; pass <= 2; ++pass) {
                const std::size_t seam = r.origin() + std::size_t(pass) * length;
                const double at_seam = rms(r.out, seam, fade);
                const double before = rms(r.out, seam - fade, fade);
                const double after = rms(r.out, seam + fade, fade);
                worst_level = std::max(worst_level, std::abs(at_seam - 0.5 * (before + after)));
                worst_kink = std::max(worst_kink, kink(r.out, seam, seam + fade));
                elsewhere = std::max(elsewhere, kink(r.out, seam + 2 * fade, seam + length / 2));
            }
            INFO(name << ", " << hold << " s loop: level across the seam " << worst_level
                 << " dB; worst spike " << worst_kink << " dB against " << elsewhere << " dB elsewhere");
            if (std::string(name) != "phrase") CHECK(worst_level < 1.0);
            CHECK(worst_kink <= elsewhere + 3.0);
        }
    // Control: a step planted at a seam of the pad's loop.
    auto planted = loop_source(material[1].second, press, 0.5);
    const std::size_t seam = planted.origin() + std::size_t(planted.length);
    for (std::size_t n = seam; n < planted.out.size(); ++n) { planted.out.l[n] += 0.1f; planted.out.r[n] += 0.1f; }
    const double hard = kink(planted.out, seam, seam + fade);
    const double rest = kink(planted.out, seam + 2 * fade, seam + std::size_t(planted.length) / 2);
    INFO("planted step: spike " << hard << " dB against " << rest << " dB elsewhere");
    CHECK(hard > rest + 3.0);
}

TEST_CASE("A long Hold length engages at once, looping what has been heard", "[freeze][loop]") {
    // A 2 s Hold length pressed 1 s after the stream starts used to wait for
    // 2 s of input to analyse (1.2 s of nothing happening). It loops the 1 s
    // there is, now; with less than kLoopMinSeconds heard it waits for that.
    const auto input = phrase(4.0);
    for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        const auto r = render_spectr(input, samples(1.0), 2.0, mode);
        const double engage_ms = r.engaged_at
            ? double(r.engaged_at - std::size_t(r.latency) - samples(1.0)) * 1000.0 / kRate : -1.0;
        INFO("pressed 1 s in: engaged " << engage_ms << " ms after the press");
        CHECK(engage_ms >= 0.0);
        CHECK(engage_ms < 30.0);
    }
    const auto early = loop_source(input, samples(1.0), 2.0);
    INFO("loop of " << double(early.length) / kRate << " s");
    CHECK(double(early.length) / kRate > 0.95);
    const auto soon = loop_source(input, samples(0.1), 2.0);
    const double soon_s = double(soon.engaged) / kRate;
    INFO("pressed 0.1 s in: engaged at " << soon_s << " s");
    CHECK(soon_s >= FreezeSource::kLoopMinSeconds);
    CHECK(soon_s < FreezeSource::kLoopMinSeconds + 0.05);
}

TEST_CASE("Hold lengths below the loop hold the spectrum", "[freeze][loop]") {
    FreezeSource source;
    REQUIRE(source.prepare(kRate, 2));
    const auto input = phrase(2.0);
    for (const double hold : {FreezeSource::kDefaultHoldSeconds, 0.2, 0.25}) {
        source.reset();
        source.set_hold_seconds(hold);
        std::vector<float> l(input.l), r(input.r);
        for (std::size_t pos = 0; pos < l.size(); pos += 256) {
            source.set_frozen(pos >= samples(1.0));
            float* io[] = {l.data() + pos, r.data() + pos};
            const float* in[] = {input.l.data() + pos, input.r.data() + pos};
            source.process_block(in, io, 2, int(std::min<std::size_t>(256, l.size() - pos)));
        }
        INFO("hold " << hold << " s");
        REQUIRE(source.hold_audible());
        CHECK(source.looping() == (hold >= FreezeSource::kLoopMinSeconds));
    }
}

// ── The engage's pitch, 5 ms at a time ─────────────────────────────────────

namespace {

/// Complex demodulation of `x` at `hz` with a 40 ms Hann window centred on
/// `centre`: a partial's phasor there, little disturbed by partials more
/// than 50 Hz away.
std::complex<double> demodulate(const std::vector<float>& x, std::size_t centre, double hz) {
    const std::size_t half = samples(0.02);
    std::complex<double> z{};
    for (std::size_t i = 0; i < 2 * half; ++i) {
        const std::size_t n = centre - half + i;
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(2 * half));
        const double ph = -2 * kPi * hz * double(n) / kRate;
        z += w * double(x[n]) * std::complex<double>(std::cos(ph), std::sin(ph));
    }
    return z;
}

/// The partial's frequency over [t, t + 5 ms], as cents from `hz`.
double cents_at(const std::vector<float>& x, std::size_t t, double hz) {
    const std::size_t step = samples(0.005);
    double d = std::arg(demodulate(x, t + step, hz) / demodulate(x, t, hz));
    return 1200.0 * std::log2((hz + d / (2 * kPi * 0.005)) / hz);
}

struct PitchTrace {
    double worst = 0;             // largest |pressed - unpressed| over the trace, cents
    std::vector<double> cents;    // per 5 ms, the worst partial's deviation
};

/// From 20 ms before the hold is first heard to 300 ms after, every 5 ms:
/// each partial's frequency in the pressed render minus the same partial's in
/// the unpressed one at the same moment. What the live input does by itself
/// (beating, a neighbour's leakage) is in both and cancels; what the engage
/// adds is left.
PitchTrace pitch_trace(const Render& pressed, const Render& live, std::size_t engage,
                       const std::vector<double>& partials) {
    PitchTrace t;
    for (std::size_t at = engage - samples(0.02); at < engage + samples(0.3); at += samples(0.005)) {
        double worst = 0;
        for (const double hz : partials) {
            const double d = cents_at(pressed.out.l, at, hz) - cents_at(live.out.l, at, hz);
            if (std::abs(d) > std::abs(worst)) worst = d;
        }
        t.cents.push_back(worst);
        t.worst = std::max(t.worst, std::abs(worst));
    }
    return t;
}

const std::vector<double>& partials_of(const std::string& name) {
    static const std::vector<double> sine{440.0}, chord{261.63, 329.63, 392.0},
        pad{220.0, 261.63, 329.63, 440.0};
    return name == "sine 440" ? sine : name == "saw chord" ? chord : pad;
}

} // namespace

TEST_CASE("Freeze engage pitch and loudness trace", "[.][freeze-pitch-trace]") {
    const std::size_t press = samples(3.0);
    for (const auto& prog : programmes()) {
        if (prog.peaks == 0 && std::string(prog.name) != "synth pad") continue;
        const auto live = render_spectr(prog.audio, SIZE_MAX - 1, kHolds[0], MaskRenderMode::zero_latency);
        for (const double hold : kHolds) {
            const auto r = render_spectr(prog.audio, press, hold, MaskRenderMode::zero_latency);
            const std::size_t engage = r.engaged_at - std::size_t(r.latency);
            const auto p = pitch_trace(r, live, engage, partials_of(prog.name));
            std::printf("%-10s %-6s pitch (cents, 5 ms) worst %5.2f |", prog.name, hold_name(hold), p.worst);
            for (double c : p.cents) std::printf(" %+.1f", c);
            const auto k = k_weighted(r.out);
            const auto kl = k_weighted(live.out);
            std::printf("\n%-10s %-6s loudness vs unpressed (dB, 10 ms) |", prog.name, hold_name(hold));
            for (std::size_t at = engage - samples(0.02); at < engage + samples(0.3); at += samples(0.01))
                std::printf(" %+.1f", lufs(k, at, samples(0.01)) - lufs(kl, at, samples(0.01)));
            std::printf("\n");
        }
    }
}

// ── Repeats at the engage ──────────────────────────────────────────────────

namespace {
struct BandBiquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static BandBiquad bandpass(double hz, double q) {
        const double w = 2.0 * kPi * hz / kRate, alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        BandBiquad f;
        f.b0 = alpha / a0; f.b1 = 0.0; f.b2 = -alpha / a0;
        f.a1 = -2.0 * std::cos(w) / a0; f.a2 = (1.0 - alpha) / a0;
        return f;
    }
    void retune(double hz, double q) {
        const auto n = bandpass(hz, q);
        b0 = n.b0; b1 = n.b1; b2 = n.b2; a1 = n.a1; a2 = n.a2;
    }
    double operator()(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// 120 BPM: kick on 1 and 3, snare on 2 and 4, closed hats on eighths with an
// open hat on the last one of each bar, and a sub bass line.
Stereo drum_loop(double seconds) {
    Stereo s; s.resize(samples(seconds));
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const double beat = 0.5;
    const auto hit = [&](double at, auto&& voice, double length) {
        const auto start = samples(at);
        for (std::size_t i = 0; i < samples(length) && start + i < s.size(); ++i) {
            const double t = double(i) / kRate;
            const auto [l, r] = voice(t);
            s.l[start + i] += float(l);
            s.r[start + i] += float(r);
        }
    };
    for (double bar = 0.0; bar < seconds; bar += 4 * beat) {
        for (int b = 0; b < 4; ++b) {
            const double at = bar + b * beat;
            if (b % 2 == 0) {
                double phase = 0.0;
                hit(at, [&](double t) {
                    const double f = 48.0 + 110.0 * std::exp(-t / 0.035);
                    phase += 2.0 * kPi * f / kRate;
                    const double v = 0.9 * std::sin(phase) * std::exp(-t / 0.28)
                                   + (t < 0.003 ? 0.3 * u(rng) * (1.0 - t / 0.003) : 0.0);
                    return std::pair{v, v};
                }, 0.9);
            } else {
                BandBiquad body = BandBiquad::bandpass(1800.0, 0.7);
                hit(at, [&](double t) {
                    const double v = 0.55 * body(u(rng)) * std::exp(-t / 0.13)
                                   + 0.3 * std::sin(2.0 * kPi * 185.0 * t) * std::exp(-t / 0.07);
                    return std::pair{v * 0.95, v};
                }, 0.6);
            }
        }
        for (int e = 0; e < 8; ++e) {
            const double at = bar + e * beat / 2.0 + (e % 2 ? 0.012 : 0.0); // a little swing
            const double decay = e == 7 ? 0.18 : 0.025;
            double prev = 0.0;
            hit(at, [&](double t) {
                const double x = u(rng);
                const double v = 0.22 * (x - prev) * std::exp(-t / decay);
                prev = x;
                return std::pair{v * 0.7, v};
            }, decay * 6);
        }
        // Sub bass, one note per beat.
        static constexpr double notes[] = {55.0, 55.0, 65.41, 49.0};
        for (int b = 0; b < 4; ++b) {
            const double f = notes[b];
            hit(bar + b * beat, [&](double t) {
                const double env = std::min(1.0, t / 0.01) * std::exp(-t / 0.4);
                const double v = 0.35 * env * std::sin(2.0 * kPi * f * t);
                return std::pair{v, v};
            }, beat);
        }
    }
    normalise(s, 0.7);
    add_room(s, 12);
    return s;
}

// A voice-like source: a glottal pulse train with a moving pitch through three
// formant resonators that step between vowels, syllable envelopes, and
// fricative noise in the gaps.
Stereo speech_like(double seconds) {
    Stereo s; s.resize(samples(seconds));
    static constexpr double vowels[][3] = {
        {730, 1090, 2440}, {270, 2290, 3010}, {300, 870, 2240},
        {530, 1840, 2480}, {570, 840, 2410}, {660, 1720, 2410}};
    std::mt19937 rng(31);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::uniform_int_distribution<int> pick(0, 5);
    std::array<BandBiquad, 3> formant{BandBiquad::bandpass(730, 8), BandBiquad::bandpass(1090, 10),
                                  BandBiquad::bandpass(2440, 12)};
    std::array<double, 3> f_now{730, 1090, 2440}, f_target = f_now;
    double glottal_phase = 0.0, glottal = 0.0, fric_prev = 0.0;
    double syllable_start = 0.0, syllable_length = 0.2;
    bool voiced = true;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        if (t >= syllable_start + syllable_length) {
            syllable_start = t;
            voiced = (pick(rng) != 0);
            syllable_length = voiced ? 0.12 + 0.04 * pick(rng) : 0.06 + 0.01 * pick(rng);
            const auto& v = vowels[pick(rng)];
            for (int k = 0; k < 3; ++k) f_target[k] = v[k];
        }
        for (int k = 0; k < 3; ++k) {
            f_now[k] += 0.004 * (f_target[k] - f_now[k]);
            if (n % 32 == 0) formant[k].retune(f_now[k], 6.0 + 3.0 * k);
        }
        const double f0 = 125.0 + 30.0 * std::sin(2.0 * kPi * 0.6 * t)
                        + 12.0 * std::sin(2.0 * kPi * 3.1 * t);
        glottal_phase += f0 / kRate;
        double pulse = 0.0;
        if (glottal_phase >= 1.0) { glottal_phase -= 1.0; pulse = 1.0; }
        glottal = 0.96 * glottal + pulse;               // a soft glottal shape
        const double into = glottal;
        const double local = t - syllable_start;
        const double env = std::min(1.0, local / 0.02)
                         * std::min(1.0, (syllable_length - local) / 0.03);
        double v = 0.0;
        if (voiced)
            v = env * (formant[0](into) * 1.0 + formant[1](into) * 0.6 + formant[2](into) * 0.35);
        else {
            const double x = u(rng);
            v = env * 0.25 * (x - fric_prev);
            fric_prev = x;
            (void)formant[0](0.0); (void)formant[1](0.0); (void)formant[2](0.0);
        }
        s.l[n] = float(v);
        s.r[n] = float(v);
    }
    normalise(s, 0.6);
    add_room(s, 32);
    return s;
}


/// 1 ms energies of the first difference of `x` (an HF envelope).
std::vector<double> envelope_ms(const std::vector<float>& x, std::size_t from, std::size_t n) {
    std::vector<double> e;
    const std::size_t ms = samples(0.001);
    for (std::size_t at = from; at + ms <= from + n; at += ms) {
        double v = 0;
        for (std::size_t i = at; i < at + ms; ++i) { const double d = double(x[i]) - x[i - 1]; v += d * d; }
        e.push_back(std::log(v + 1e-12));
    }
    return e;
}

/// The strongest self-similarity of the HF envelope over [from, from + n):
/// the largest correlation of the envelope with itself `lag` ms later, for
/// lags 8-150 ms, and that lag.
std::pair<double, int> self_similarity(const std::vector<float>& x, std::size_t from, std::size_t n) {
    const auto e = envelope_ms(x, from, n + samples(0.15));
    const std::size_t len = n / samples(0.001);
    double best = -1; int at = 0;
    for (int lag = 15; lag <= 200; ++lag) {
        double ab = 0, aa = 0, bb = 0, ma = 0, mb = 0;
        for (std::size_t i = 0; i < len; ++i) { ma += e[i]; mb += e[i + std::size_t(lag)]; }
        ma /= double(len); mb /= double(len);
        for (std::size_t i = 0; i < len; ++i) {
            const double a = e[i] - ma, b = e[i + std::size_t(lag)] - mb;
            ab += a * b; aa += a * a; bb += b * b;
        }
        const double c = ab / std::sqrt(aa * bb + 1e-30);
        if (c > best) { best = c; at = lag; }
    }
    return {best, at};
}

} // namespace

TEST_CASE("Freeze engage repeat report", "[.][freeze-repeat-report]") {
    const std::size_t press = samples(3.0);
    const std::pair<const char*, Stereo> material[] = {
        {"drums", drum_loop(7.0)}, {"voice", speech_like(7.0)}, {"phrase", phrase(7.0)},
        {"synth pad", synth_pad(7.0)}, {"saw chord", saw_chord(7.0)}, {"texture", texture(7.0)}};
    for (const auto& [name, in] : material) {
        const auto row = [&](const char* who, const Render& r) {
            const std::size_t e = r.engaged_at ? r.engaged_at : press + std::size_t(r.latency);
            const auto first = self_similarity(r.out.l, e + samples(0.06), samples(0.25));
            const auto settled = self_similarity(r.out.l, e + samples(1.5), samples(0.25));
            // Waveform: the first 60-310 ms against itself one lag later.
            double best = -1; int best_lag = 0;
            for (int lag = 10; lag <= 200; ++lag) {
                const double c = correlation(r.out.l, e + samples(0.06), r.out.l, e + samples(0.06) + samples(lag / 1000.0), samples(0.25));
                if (c > best) { best = c; best_lag = lag; }
            }
            double sbest = -1; int sbest_lag = 0;
            for (int lag = 10; lag <= 200; ++lag) {
                const double c = correlation(r.out.l, e + samples(1.5), r.out.l, e + samples(1.5) + samples(lag / 1000.0), samples(0.25));
                if (c > sbest) { sbest = c; sbest_lag = lag; }
            }
            std::printf("   waveform: first %.2f at %d ms, settled %.2f at %d ms\n", best, best_lag, sbest, sbest_lag);
            std::printf("%-9s %-14s first 300 ms: %.2f at %3d ms | settled: %.2f at %3d ms\n",
                        name, who, first.first, first.second, settled.first, settled.second);
        };
        row("unpressed", render_spectr(in, SIZE_MAX - 1, kHolds[0], MaskRenderMode::zero_latency));
        row("spectr 85 ms", render_spectr(in, press, kHolds[0], MaskRenderMode::zero_latency));
        row("spectr 2 s", render_spectr(in, press, kHolds[1], MaskRenderMode::zero_latency));
        row("bendr", render_bendr(in, press));
    }
}

TEST_CASE("The engage bends no partial", "[freeze][level][pitch]") {
    // Heard as a brief doubled, chorused sound at the press: the hold
    // entering a quarter cycle away from the live partials, so the fade
    // swept each partial's phase by 90 degrees over 48 ms -- a +20 cent
    // glide on a 440 Hz sine, +37 on a saw chord. Each partial's frequency,
    // every 5 ms from 20 ms before the hold is heard to 300 ms after, in the
    // pressed render minus the unpressed one, must stay within 2 cents. The
    // pad here is not detuned: a detuned pad beats, and a steady hold of it
    // by design does not, so the two would differ for that reason alone.
    // Control: a fade from the unpressed sine into itself 0.25 ms later
    // (a 40 degree turn at 440 Hz) must read more than 2 cents.
    const std::size_t press = samples(3.0);
    const std::pair<const char*, Stereo> material[] = {
        {"sine 440", sine_tone(6.0)}, {"saw chord", saw_chord(6.0)},
        {"steady pad", synth_pad(6.0, -1.0, false)}};
    for (const auto& [name, in] : material)
        for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
            const auto live = render_spectr(in, SIZE_MAX - 1, kHolds[0], mode);
            for (const double hold : kSpectralHolds) {
                const auto r = render_spectr(in, press, hold, mode);
                const auto p = pitch_trace(r, live, r.engaged_at, partials_of(
                    std::string(name) == "steady pad" ? "synth pad" : name));
                INFO(name << " at " << hold_name(hold) << ", "
                     << (mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking")
                     << ": worst partial " << p.worst << " cents");
                CHECK(p.worst <= 2.0);
            }
        }
    // The control.
    auto live = render_spectr(material[0].second, SIZE_MAX - 1, kHolds[0], MaskRenderMode::zero_latency);
    Render bent = live;
    const std::size_t fade = samples(FreezeSource::kCrossfadeSeconds), delay = samples(0.00025);
    for (std::size_t n = press; n < bent.out.size(); ++n) {
        const double q = std::min(1.0, double(n - press) / double(fade));
        const double a = std::cos(q * kPi / 2), b = std::sin(q * kPi / 2);
        bent.out.l[n] = float(a * live.out.l[n] + b * live.out.l[n - delay]);
    }
    const auto c = pitch_trace(bent, live, press, {440.0});
    INFO("planted 40 degree fade: " << c.worst << " cents");
    CHECK(c.worst > 2.0);
}

TEST_CASE("A loop's engage is its own seam", "[freeze][loop]") {
    // The engage fades the live input into the loop's start; every later
    // pass fades the audio that followed the loop's end -- the same live
    // input -- into the same start. With the loop ending where the engage
    // begins, the first fade and the second pass's seam are the same samples.
    // When the loop ended a hop before the engage, the engage blended the
    // live input with audio a hop into the loop: two moments that were never
    // matched, heard as a brief doubling at the press.
    const std::pair<const char*, Stereo> material[] = {
        {"phrase", phrase(8.0)}, {"synth pad", synth_pad(8.0)}, {"saw chord", saw_chord(8.0)}};
    const std::size_t press = samples(3.0);
    const std::size_t fade = samples(FreezeSource::kCrossfadeSeconds);
    for (const auto& [name, input] : material)
        for (const double hold : {0.5, 2.0}) {
            const auto r = loop_source(input, press, hold);
            const std::size_t e = r.engaged, seam = r.engaged + std::size_t(r.length);
            double diff = 0, power = 0;
            for (std::size_t i = 0; i < fade; ++i) {
                const double a = r.out.l[e + i], b = r.out.l[seam + i];
                diff += (a - b) * (a - b);
                power += a * a;
            }
            const double db = 10 * std::log10(diff / (power + 1e-30) + 1e-30);
            INFO(name << ", " << hold << " s: engage against the second pass's seam " << db << " dB");
            CHECK(db < -60.0);
        }
}
