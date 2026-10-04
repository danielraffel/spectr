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

int main(int argc, char** argv) {
    std::string write_dir, corpus, shapes_choice = "all", modes_choice = "v1,v2";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--write-corpus") write_dir = next();
        else if (a == "--corpus") corpus = next();
        else if (a == "--shapes") shapes_choice = next();
        else if (a == "--modes") modes_choice = next();
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
        // The measurement window: from 4 s (or 4.5 s past the freeze), to the
        // end. "whole" is from 0.5 s, start-up included.
        const auto from = m.freeze_at > 0.0 ? seconds(m.freeze_at + 4.5) : seconds(4.0);
        const auto whole_from = m.freeze_at > 0.0 ? seconds(m.freeze_at + 0.5) : seconds(0.5);
        const double in_steady = integrated_lufs(in, from), in_whole = integrated_lufs(in, whole_from);
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
                std::printf("{\"material\":\"%s\",\"shape\":\"%s\",\"mode\":\"%s\","
                            "\"error_lu\":%.4f,\"error_whole_lu\":%.4f,\"off_change_lu\":%.4f,"
                            "\"applied_db_end\":%.4f,\"applied_spread_db\":%.4f,"
                            "\"applied_sd_db\":%.5f,\"momentary_sd_on\":%.4f,"
                            "\"momentary_sd_off\":%.4f}\n",
                            json_escape(m.name).c_str(), json_escape(shape.name).c_str(),
                            mode_name(mode), steady, whole, off_steady,
                            static_cast<double>(r.applied_db.back()),
                            static_cast<double>(*hi - *lo), stddev(settled), sd_on, sd_off);
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
