// Auto Gain v1 vs v2 corpus sweep (ADVISORY; not a ctest).
//
//   Spectr-autogain-sweep --write-corpus DIR
//       writes the harness's generated materials as raw interleaved stereo
//       float32 at 48 kHz (DIR/<name>.f32), for the report script to convert,
//       register (quality-lab corpus.py) and hand back.
//   Spectr-autogain-sweep --corpus LIST.tsv [--shapes all|quick] [--modes v1,v2]
//       renders every material in LIST (name <TAB> path.f32 <TAB> flags) through
//       Spectr for every shape and mode and prints one JSON object per line.
//
// Flags in LIST: `freeze@S` engages Freeze at S seconds and measures against
// the same frozen render with AUTO off and a flat shape (the held material's
// own loudness) instead of the input.
//
// Everything is measured with the shared harness (test/autogain_harness.hpp):
// HeadlessHost renders, BS.1770 loudness from Pulp's MultiChannelMeter.
// tools/autogain_corpus_report.py drives it and writes the report.

#include "autogain_harness.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace autogain_harness;

namespace {

void write_f32(const std::string& path, const Stereo& s) {
    std::ofstream f(path, std::ios::binary);
    for (std::size_t i = 0; i < s.size(); ++i) {
        f.write(reinterpret_cast<const char*>(&s.l[i]), sizeof(float));
        f.write(reinterpret_cast<const char*>(&s.r[i]), sizeof(float));
    }
}

Stereo read_f32(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    const auto bytes = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    const std::size_t frames = bytes / (2 * sizeof(float));
    Stereo s = Stereo::zeros(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        f.read(reinterpret_cast<char*>(&s.l[i]), sizeof(float));
        f.read(reinterpret_cast<char*>(&s.r[i]), sizeof(float));
    }
    return s;
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (const char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

struct Material {
    std::string name, path;
    double freeze_at = 0.0;
};

} // namespace

// ── Transients (--transients) ──────────────────────────────────────────────
// Every case is measured from the event: T1dB, max momentary error against a
// flat AUTO-off render of the same scenario, and time above 6 LU.
void transients(Mode mode, const char* label) {
    const auto row = [&](const char* scenario, const char* event, const Transition& t) {
        std::printf("{\"scenario\":\"%s\",\"event\":\"%s\",\"mode\":\"%s\","
                    "\"t1db\":%.3f,\"max_momentary_error\":%.3f,\"seconds_over_6lu\":%.3f,"
                    "\"settled_db\":%.3f}\n", scenario, event, label, t.t1db,
                    t.max_momentary_error, t.seconds_over_6lu, t.settled_db);
        std::fflush(stdout);
    };
    const Shape high = region("high broad +24", 24, 31, 24.0f);
    const Shape low = region("low broad +24", 0, 9, 24.0f);
    const Shape flat{"flat", {}};
    {   // cold start
        const auto n = seconds(10.0);
        struct C { const char* name; Stereo in; Shape shape; };
        const C cases[] = {{"cold start: bass line, high +24", bass_line(n), high},
                           {"cold start: vocal, high +24", vocal(n), high},
                           {"cold start: hats, high -24", hats(n), region("h", 24, 31, -24.0f)},
                           {"cold start: sine 1k, mid narrow -24", sine(n),
                            region("m", band_of(1000.0), band_of(1000.0), -24.0f)},
                           {"cold start: drum loop, low +24", drum_loop(n), low},
                           {"cold start: pink, low +12", pink(n, 5u), region("l", 0, 9, 12.0f)}};
        for (const auto& c : cases) {
            const auto r = render(c.in, c.shape, mode);
            const auto ref = render(c.in, flat, Mode::off);
            row(c.name, "start", transition(r, ref, 0.0, 6.0, 9.9));
        }
    }
    {   // locate every 3 s
        const auto n = seconds(15.0);
        struct C { const char* name; Stereo in; Shape shape; };
        const C cases[] = {{"locate every 3 s: bass line, high +24", bass_line(n), high},
                           {"locate every 3 s: drum loop, low +24", drum_loop(n), low},
                           {"locate every 3 s: vocal, mid narrow -12", vocal(n),
                            region("m", band_of(1000.0), band_of(1000.0), -12.0f)}};
        RenderOptions o;
        for (double t = 3.0; t < 15.0; t += 3.0) o.resets.push_back(seconds(t));
        for (const auto& c : cases) {
            const auto r = render(c.in, c.shape, mode, o);
            const auto ref = render(c.in, flat, Mode::off, o);
            Transition worst;
            for (double t = 3.0; t < 15.0; t += 3.0) {
                const auto tr = transition(r, ref, t, t + 2.9, t + 2.9);
                worst.t1db = std::max(worst.t1db, tr.t1db);
                worst.max_momentary_error = std::max(worst.max_momentary_error,
                                                     tr.max_momentary_error);
                worst.seconds_over_6lu = std::max(worst.seconds_over_6lu, tr.seconds_over_6lu);
                worst.settled_db = tr.settled_db;
            }
            row(c.name, "each locate (worst)", worst);
        }
    }
    {   // host reset mid-hold
        const Stereo in = concat(bass_line(seconds(5.0)), hats(seconds(10.0)));
        RenderOptions o;
        o.freeze_at = seconds(3.0);
        o.resets = {seconds(7.0)};
        const auto r = render(in, high, mode, o);
        const auto ref = render(in, flat, Mode::off, o);
        row("host reset mid-hold: bass held, hats live, high +24", "reset at 7 s",
            transition(r, ref, 7.0, 14.0, 14.0));
    }
    {   // Freeze engage and release
        const Stereo in = concat(bass_line(seconds(5.0)), hats(seconds(15.0)));
        RenderOptions o;
        o.freeze_at = seconds(4.5);
        o.release_at = seconds(9.0);
        const auto r = render(in, high, mode, o);
        const auto ref = render(in, flat, Mode::off, o);
        row("Freeze: bass held at 4.5 s, hats live from 5 s, high +24", "engage",
            transition(r, ref, 4.5, 9.0, 8.9));
        row("Freeze: bass held at 4.5 s, hats live from 5 s, high +24", "release at 9 s",
            transition(r, ref, 9.0, 20.0, 19.9));
    }
    {   // material changes
        const auto half = seconds(8.0);
        const double q = std::pow(10.0, -15.0 / 20.0);
        struct C { const char* name; Stereo in; };
        const C cases[] = {
            {"change at 8 s: bass -> hats, high +24", concat(bass_line(half), hats(half))},
            {"change at 8 s: bass -> hats -15 dB (quieter after loud)",
             concat(bass_line(half), scaled(hats(half), q))},
            {"change at 8 s: hats -> bass -15 dB", concat(hats(half), scaled(bass_line(half), q))},
            {"change at 8 s: hats -15 dB -> bass (loud after quiet)",
             concat(scaled(hats(half), q), bass_line(half))},
            {"change at 8 s: pink -> vocal -15 dB", concat(pink(half, 3u), scaled(vocal(half), q))},
        };
        for (const auto& c : cases) {
            const auto r = render(c.in, high, mode);
            const auto ref = render(c.in, flat, Mode::off);
            row(c.name, "change", transition(r, ref, 8.0, 16.0, 15.9));
        }
    }
    {   // switches every 4 s
        const auto part = seconds(4.0);
        Stereo in = bass_line(part);
        in = concat(in, hats(part));
        in = concat(in, vocal(part));
        in = concat(in, drum_loop(part));
        in = concat(in, bass_line(part));
        const auto r = render(in, high, mode);
        const auto ref = render(in, flat, Mode::off);
        const char* names[] = {"bass -> hats", "hats -> vocal", "vocal -> drums", "drums -> bass"};
        int k = 0;
        for (double t = 4.0; t < 20.0; t += 4.0, ++k) {
            char label[96];
            std::snprintf(label, sizeof(label), "switch every 4 s, high +24: %s", names[k]);
            row(label, "switch", transition(r, ref, t, t + 3.9, t + 3.9));
        }
    }
}

int main(int argc, char** argv) {
    std::string write_dir, corpus, shapes_choice = "all", modes_choice = "v1,v2";
    std::string transient_label;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--write-corpus") write_dir = next();
        else if (a == "--corpus") corpus = next();
        else if (a == "--shapes") shapes_choice = next();
        else if (a == "--modes") modes_choice = next();
        else if (a == "--transients") transient_label = next();
    }
    if (!transient_label.empty()) {
        transients(transient_label == "v1" ? Mode::v1 : Mode::v2, transient_label.c_str());
        return 0;
    }
    if (!write_dir.empty()) {
        const auto n = seconds(12.0);
        write_f32(write_dir + "/pink.f32", pink(n, 5u));
        write_f32(write_dir + "/bass_line.f32", bass_line(n));
        write_f32(write_dir + "/vocal_buzz.f32", vocal(n));
        write_f32(write_dir + "/hats.f32", hats(n));
        write_f32(write_dir + "/synth_pad.f32", pad(n));
        write_f32(write_dir + "/drum_loop_synth.f32", drum_loop(n));
        write_f32(write_dir + "/sine_1k.f32", sine(n));
        write_f32(write_dir + "/pink_with_gaps.f32", pink_with_gaps(seconds(16.0)));
        write_f32(write_dir + "/freeze_bass_then_hats.f32",
                  concat(bass_line(seconds(5.0)), hats(seconds(10.0))));
        std::printf("wrote corpus to %s\n", write_dir.c_str());
        return 0;
    }
    if (corpus.empty()) {
        std::fprintf(stderr, "usage: --write-corpus DIR | --corpus LIST.tsv\n");
        return 2;
    }
    std::vector<Material> materials;
    {
        std::ifstream list(corpus);
        std::string line;
        while (std::getline(list, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            Material m;
            std::string flags;
            std::getline(fields, m.name, '\t');
            std::getline(fields, m.path, '\t');
            std::getline(fields, flags, '\t');
            if (flags.rfind("freeze@", 0) == 0) m.freeze_at = std::atof(flags.c_str() + 7);
            materials.push_back(m);
        }
    }
    std::vector<Shape> shapes = sweep_shapes();
    if (shapes_choice == "quick")
        shapes = {region("high broad +24", 24, 31, 24.0f), region("low broad +12", 0, 9, 12.0f),
                  region("high broad -12", 24, 31, -12.0f), region("mid narrow -12", 18, 18, -12.0f)};
    std::vector<Mode> modes;
    if (modes_choice.find("v1") != std::string::npos) modes.push_back(Mode::v1);
    if (modes_choice.find("v2") != std::string::npos) modes.push_back(Mode::v2);
    if (modes_choice.find("off") != std::string::npos) modes.push_back(Mode::off);

    for (const auto& m : materials) {
        const Stereo in = read_f32(m.path);
        RenderOptions options;
        if (m.freeze_at > 0.0) options.freeze_at = seconds(m.freeze_at);
        // The measurement windows: "steady" from 4 s (or 4.5 s past the
        // freeze) to the end; "whole" from 0 s (from the freeze for a Freeze
        // case), start-up included.
        const auto from = m.freeze_at > 0.0 ? seconds(m.freeze_at + 4.5) : seconds(4.0);
        const auto whole_from = m.freeze_at > 0.0 ? seconds(m.freeze_at) : std::size_t{0};
        const double in_steady = integrated_lufs(in, from), in_whole = integrated_lufs(in, whole_from);
        // Momentary / short-term references: a flat render with AUTO off.
        const auto flat_ref = render(in, Shape{"flat", {}}, Mode::off, options);
        for (const auto& shape : shapes) {
            double ref_steady = in_steady, ref_whole = in_whole;
            if (m.freeze_at > 0.0) {
                // The held material's own loudness at this Mix: the same
                // frozen render, flat shape, AUTO off.
                Shape flat{"flat", {}};
                flat.mix = shape.mix;
                const auto held = render(in, flat, Mode::off, options);
                ref_steady = integrated_lufs(held.out, from);
                ref_whole = integrated_lufs(held.out, whole_from);
            }
            const auto off = render(in, shape, Mode::off, options);
            const double off_steady = integrated_lufs(off.out, from) - ref_steady;
            for (const auto mode : modes) {
                const auto r = render(in, shape, mode, options);
                const double steady = integrated_lufs(r.out, from) - ref_steady;
                const double whole = integrated_lufs(r.out, whole_from) - ref_whole;
                std::vector<float> settled;
                for (std::size_t b = 0; b < r.block_end.size(); ++b)
                    if (r.block_end[b] > from) settled.push_back(r.applied_db[b]);
                const auto [lo, hi] = std::minmax_element(settled.begin(), settled.end());
                const double sd_on = stddev(loudness_series(r.out, from, true));
                const double sd_off = stddev(loudness_series(off.out, from, true));
                // Whole-render momentary error vs the flat reference, from 0.4 s
                // (the first full momentary window).
                const auto whole_tr = transition(r, flat_ref,
                    static_cast<double>(whole_from) / kRate + 0.4, 1.0e9);
                std::printf("{\"material\":\"%s\",\"shape\":\"%s\",\"mode\":\"%s\","
                            "\"error_lu\":%.4f,\"error_whole_lu\":%.4f,\"off_change_lu\":%.4f,"
                            "\"applied_db_end\":%.4f,\"applied_spread_db\":%.4f,"
                            "\"applied_sd_db\":%.5f,\"momentary_sd_on\":%.4f,"
                            "\"momentary_sd_off\":%.4f,\"max_momentary_error_whole\":%.3f,"
                            "\"seconds_over_6lu_whole\":%.3f}\n",
                            json_escape(m.name).c_str(), json_escape(shape.name).c_str(),
                            mode_name(mode), steady, whole, off_steady,
                            static_cast<double>(r.applied_db.back()),
                            static_cast<double>(*hi - *lo), stddev(settled), sd_on, sd_off,
                            whole_tr.max_momentary_error, whole_tr.seconds_over_6lu);
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
