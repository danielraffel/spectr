// Auto Gain v2 (include/spectr/auto_gain_material.hpp): the fast,
// deterministic gates. The bigger corpus sweep is tools/autogain_sweep.cpp
// (advisory; tools/autogain_corpus_report.py drives it and writes the report).
//
// Each gate has a control that must fail, registered as a ctest:
//
//   SPECTR_LEVEL_PLANT=autogain-v2-unweighted    accuracy gate must fail
//   SPECTR_LEVEL_PLANT=autogain-v2-no-smoothing  pumping gate must fail
//
// and v1 itself is the in-test control for the narrow-material cases: it must
// show the large error v2 exists to remove.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "autogain_harness.hpp"

#include <cstdio>

using namespace autogain_harness;
using Catch::Approx;

namespace {

struct Case {
    const char* material;
    const Stereo* in;
    bool narrow;
};

} // namespace

TEST_CASE("Auto Gain v2 holds narrow material where v1 misses",
          "[level][autogain][v2][audio][loudness]") {
    // 10 s of each, measured from 4 s: v2 starts from v1's estimate and has
    // converged on the material by then (the start-up is its own case below).
    const auto n = seconds(10.0);
    const Stereo bass = bass_line(n), hat = hats(n), tone = sine(n), voice = vocal(n),
                 noise = pink(n, 5u), drums = drum_loop(n);
    const std::vector<Case> cases = {
        {"bass line", &bass, true}, {"hats", &hat, true}, {"sine 1k", &tone, true},
        {"vocal", &voice, true}, {"pink", &noise, false}, {"drum loop", &drums, false},
    };
    std::vector<Shape> shapes = {
        region("high broad +24", 24, 31, 24.0f),
        region("high narrow +12", band_of(10000.0), band_of(10000.0), 12.0f),
        region("low broad +12", 0, 9, 12.0f),
        region("mid narrow -12", band_of(1000.0), band_of(1000.0), -12.0f),
        region("low broad -24", 0, 9, -24.0f),
    };
    {
        Shape mixed = region("mid broad +12", 12, 22, 12.0f);
        mixed.name += " @mix50";
        mixed.mix = 50.0f;
        shapes.push_back(mixed);
    }
    const auto from = seconds(4.0);
    std::vector<double> v2_abs;
    double bass_high_v1 = 0.0;
    int narrow_cases = 0, narrow_better = 0;
    std::string not_better;
    for (const auto& c : cases) {
        for (const auto& shape : shapes) {
            const auto r1 = render(*c.in, shape, Mode::v1);
            const auto r2 = render(*c.in, shape, Mode::v2);
            std::string why;
            REQUIRE(finite_and_sane(r2.out, &why));
            const double e1 = loudness_error(*c.in, r1, from);
            const double e2 = loudness_error(*c.in, r2, from);
            std::printf("[autogain-v2] %-10s %-24s v1 %+6.2f LU  v2 %+6.2f LU\n",
                        c.material, shape.name.c_str(), e1, e2);
            v2_abs.push_back(std::abs(e2));
            if (std::string(c.material) == "bass line" && shape.name == "high broad +24")
                bass_high_v1 = e1;
            // "Strictly better on narrow material": wherever v1 is off by more
            // than half a LU on a narrow source, v2 must be closer.
            if (c.narrow && std::abs(e1) > 0.5) {
                ++narrow_cases;
                if (std::abs(e2) < std::abs(e1)) ++narrow_better;
                else not_better += std::string(c.material) + "/" + shape.name + " ";
            }
        }
    }
    const double p95 = percentile(v2_abs, 0.95);
    const double worst = *std::max_element(v2_abs.begin(), v2_abs.end());
    std::printf("[autogain-v2] accuracy: v2 |err| p95 %.2f LU, worst %.2f LU over %zu renders; "
                "narrow cases better than v1: %d/%d; v1 bass/high +24 %+.2f LU\n",
                p95, worst, v2_abs.size(), narrow_better, narrow_cases, bass_high_v1);
    // Control: v1 really is far off on the case v2 exists for, so this
    // measurement can see the difference.
    REQUIRE(std::abs(bass_high_v1) >= 6.0);
    INFO("not better than v1: " << not_better);
    CHECK(p95 <= 1.5);
    CHECK(worst <= 3.0);
    CHECK(narrow_better == narrow_cases);
}

TEST_CASE("Auto Gain v2 does not pump on steady material",
          "[level][autogain][v2][audio][pumping]") {
    // Steady material through a static shape: the applied gain must hold and
    // the output's loudness must fluctuate no more than with AUTO off (same
    // shape, so the only difference is the gain Auto Gain applies).
    const auto n = seconds(20.0);
    const auto from = seconds(6.0);
    struct Steady { const char* name; Stereo in; };
    const Steady materials[] = {{"drum loop", drum_loop(n)}, {"pink", pink(n, 41u)},
                                {"bass line", bass_line(n)}};
    const Shape shapes[] = {region("low broad +12", 0, 9, 12.0f),
                            region("high broad -12", 24, 31, -12.0f)};
    double worst_spread = 0.0, worst_extra_sd = 0.0;
    std::uint64_t worst_restarts = 0;
    for (const auto& m : materials)
        for (const auto& shape : shapes) {
            const auto off = render(m.in, shape, Mode::off);
            std::uint64_t restarts = 0;
            RenderOptions count;
            count.finish = [&](pulp::format::HeadlessHost&, spectr::Spectr& plugin) {
                restarts = plugin.auto_gain_material().spectrum().restarts();
            };
            const auto on = render(m.in, shape, Mode::v2, count);
            worst_restarts = std::max(worst_restarts, restarts);
            std::vector<float> settled;
            for (std::size_t b = 0; b < on.block_end.size(); ++b)
                if (on.block_end[b] > from) settled.push_back(on.applied_db[b]);
            const auto [lo, hi] = std::minmax_element(settled.begin(), settled.end());
            const double spread = *hi - *lo;
            const double sd_off = stddev(loudness_series(off.out, from, /*momentary=*/true));
            const double sd_on = stddev(loudness_series(on.out, from, /*momentary=*/true));
            const double st_off = stddev(loudness_series(off.out, from, false));
            const double st_on = stddev(loudness_series(on.out, from, false));
            std::printf("[autogain-v2] pumping %-10s %-16s applied spread %.3f dB (sd %.4f); "
                        "momentary sd off %.3f on %.3f LU; short-term sd off %.3f on %.3f LU\n",
                        m.name, shape.name.c_str(), spread, stddev(settled),
                        sd_off, sd_on, st_off, st_on);
            worst_spread = std::max(worst_spread, spread);
            worst_extra_sd = std::max(worst_extra_sd, sd_on - sd_off);
        }
    std::printf("[autogain-v2] pumping: worst applied spread %.3f dB, worst extra momentary "
                "sd %.3f LU\n", worst_spread, worst_extra_sd);
    std::printf("[autogain-v2] pumping: change detector restarts on steady material: %llu\n",
                static_cast<unsigned long long>(worst_restarts));
    CHECK(worst_spread <= 0.5);
    CHECK(worst_extra_sd <= 0.05);
    // Steady material is never mistaken for a change of material.
    CHECK(worst_restarts == 0);
}

TEST_CASE("Auto Gain v2 does not depend on how the host chops the stream",
          "[level][autogain][v2][offline][rt-safety]") {
    // A bounce differs from playback in block lengths and speed. The v2
    // estimator runs on a frame grid counted in samples and its targets land
    // on the frame's sample, so every chunking must give the same output:
    // bit for bit in Mixing (whose mask adoption is synchronous), and within
    // the renderer's own characterised one-ULP gap in Tracking
    // (test_render_mode.cpp, "Tracking output ... reproducible only to a
    // bound"). The material changes halfway, so the gain is really moving.
    const auto half = seconds(1.5);
    const Stereo in = concat(hats(half, 3u), pink(half, 9u));
    const Shape shape = region("low broad +12", 0, 9, 12.0f);
    const std::vector<std::vector<int>> chunkings = {
        {512}, {64}, {37}, {1}, {4096}, {17, 256, 3, 64, 101}, {2048}, {1000},
    };
    for (const auto mode : {spectr::MaskRenderMode::linear_phase,
                            spectr::MaskRenderMode::zero_latency}) {
        RenderOptions base;
        base.render_mode = mode;
        base.chunks = {512};
        const auto reference = render(in, shape, Mode::v2, base);
        double moved = 0.0;
        for (std::size_t b = 1; b < reference.applied_db.size(); ++b)
            moved = std::max(moved, static_cast<double>(std::abs(
                reference.applied_db[b] - reference.applied_db[0])));
        for (const auto& chunks : chunkings) {
            RenderOptions o = base;
            o.chunks = chunks;
            const auto candidate = render(in, shape, Mode::v2, o);
            std::size_t mismatches = 0;
            double worst = 0.0;
            for (std::size_t i = 0; i < in.size(); ++i) {
                const double d = std::max(std::abs(reference.out.l[i] - candidate.out.l[i]),
                                          std::abs(reference.out.r[i] - candidate.out.r[i]));
                if (d != 0.0) ++mismatches;
                worst = std::max(worst, d);
            }
            std::string label;
            for (int c : chunks) label += std::to_string(c) + " ";
            std::printf("[autogain-v2] chunking %-14s [%s] mismatches %zu, worst %.3g "
                        "(applied gain moved %.2f dB)\n",
                        std::string(spectr::render_mode_token(mode)).c_str(), label.c_str(),
                        mismatches, worst, moved);
            INFO("mode " << spectr::render_mode_token(mode) << " chunking [" << label << "]");
            if (mode == spectr::MaskRenderMode::linear_phase) CHECK(mismatches == 0);
            else CHECK(worst < 1.0e-6);
        }
        // Controls: the gain really moved during the render, and the
        // comparison can tell a different gain apart.
        REQUIRE(moved > 1.0);
        const auto other = render(in, region("high broad +6", 24, 31, 6.0f), Mode::v2, base);
        double control = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i)
            control = std::max(control, static_cast<double>(
                std::abs(reference.out.l[i] - other.out.l[i])));
        REQUIRE(control > 1.0e-4);
    }
}

TEST_CASE("Auto Gain v2 starts from v1's estimate and holds through silence",
          "[level][autogain][v2][audio]") {
    // Start-up: before any material the estimate is v1's reference, so the
    // first block applies (within the bin grid's discretisation) v1's gain.
    const Shape shape = region("high broad +12", 24, 31, 12.0f);
    const Stereo gaps = pink_with_gaps(seconds(16.0));
    const auto v1 = render(gaps, shape, Mode::v1);
    const auto v2 = render(gaps, shape, Mode::v2);
    std::printf("[autogain-v2] start-up: first block v1 %+.3f dB, v2 %+.3f dB\n",
                v1.applied_db.front(), v2.applied_db.front());
    CHECK(v2.applied_db.front() == Approx(v1.applied_db.front()).margin(0.25));

    // Silence: 3 s of pink, 2 s of digital silence, repeated. Across each gap
    // the applied gain must hold -- not drift toward the noise floor, not
    // reset to the prior.
    const auto at = [&](double s) {
        const auto target = seconds(s);
        for (std::size_t b = 0; b < v2.block_end.size(); ++b)
            if (v2.block_end[b] >= target) return static_cast<double>(v2.applied_db[b]);
        return static_cast<double>(v2.applied_db.back());
    };
    double worst = 0.0;
    for (const double gap_start : {8.0, 13.0}) {
        // From just before the gap to just before the material returns.
        const double before = at(gap_start - 0.05), end = at(gap_start + 1.95);
        std::printf("[autogain-v2] gap at %.0f s: applied %+.3f dB before, %+.3f dB at its end\n",
                    gap_start, before, end);
        worst = std::max(worst, std::abs(end - before));
    }
    CHECK(worst <= 0.05);
    // Control: the gate really saw the gaps.
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* plugin = host.processor_as<spectr::Spectr>();
    host.state().set_value(spectr::kParamAutoGain, 1.0f);
    host.prepare(kRate, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    for (std::size_t pos = 0; pos + kBlock <= gaps.size(); pos += kBlock) {
        const float* ip[] = {gaps.l.data() + pos, gaps.r.data() + pos};
        float* op[] = {l.data(), r.data()};
        pulp::audio::BufferView<const float> iv(ip, 2, kBlock);
        pulp::audio::BufferView<float> ov(op, 2, kBlock);
        host.process(ov, iv);
    }
    const auto& material = plugin->auto_gain_material();
    std::printf("[autogain-v2] frames: %llu observed, %llu gated as silence\n",
                static_cast<unsigned long long>(material.observed_frames()),
                static_cast<unsigned long long>(material.gated_frames()));
    REQUIRE(material.gated_frames() > 25);
    REQUIRE(material.observed_frames() > 75);
}

TEST_CASE("While frozen, Auto Gain v2 weighs the held material",
          "[level][autogain][v2][freeze][audio]") {
    // A bass line is frozen at 4.5 s; from 5 s the LIVE input is hats. The
    // mask shapes the held bass, so boosting the top octaves barely changes
    // the loudness and v2 must barely cut -- following the live hats it would
    // cut ~20 dB. The reference is the same frozen render with AUTO off and
    // a flat shape.
    const Stereo in = concat(bass_line(seconds(5.0)), hats(seconds(10.0)));
    const Shape boost = region("high broad +24", 24, 31, 24.0f);
    const Shape flat{"flat", {}};
    RenderOptions o;
    o.freeze_at = seconds(4.5);
    const auto reference = render(in, flat, Mode::off, o);
    const auto v2 = render(in, boost, Mode::v2, o);
    const auto v1 = render(in, boost, Mode::v1, o);
    const auto from = seconds(9.0);
    const double ref = integrated_lufs(reference.out, from);
    const double e2 = integrated_lufs(v2.out, from) - ref;
    const double e1 = integrated_lufs(v1.out, from) - ref;
    std::printf("[autogain-v2] freeze (bass held, hats live): reference %.2f LUFS; "
                "v1 %+.2f LU, v2 %+.2f LU (applied v2 %+.2f dB)\n",
                ref, e1, e2, v2.applied_db.back());
    REQUIRE(ref > -40.0);                 // the hold is really playing
    REQUIRE(std::abs(e1) > 6.0);          // control: the shape-only answer is far off
    CHECK(std::abs(e2) <= 1.5);
}

TEST_CASE("A shape edit under Auto Gain v2 settles quickly",
          "[level][autogain][v2][audio][transient]") {
    // Pink noise, flat until 6 s, then the top octaves +12 dB in one edit.
    // With AUTO off that is a lasting jump; with v2 the level returns to
    // where it was within the gain ramp, and never jumps as far.
    const Stereo in = pink(seconds(12.0), 19u);
    const auto edit_at = seconds(6.0);
    bool edited = false;
    RenderOptions o;
    o.before = [&](std::size_t pos, pulp::format::HeadlessHost& host, spectr::Spectr&) {
        if (pos >= edit_at && !edited && (edited = true))
            for (std::size_t i = 24; i < 32; ++i)
                host.state().set_value(spectr::band_gain_param_id(i), 12.0f);
    };
    const Shape flat{"flat", {}};
    const auto off = render(in, flat, Mode::off, o);
    const auto on = render(in, flat, Mode::v2, o);
    const auto mom_off = loudness_series(off.out, seconds(5.0), true);
    const auto mom_on = loudness_series(on.out, seconds(5.0), true);
    // Series index k is the block ending at 5 s + 0.4 s + k blocks.
    const auto index_of = [&](double s) {
        return static_cast<std::size_t>((s - 5.4) * kRate / kBlock);
    };
    const double before = mom_on[index_of(5.9)];
    double peak = 0.0, settle_s = 0.0;
    for (std::size_t k = index_of(6.0); k < mom_on.size(); ++k) {
        const double d = mom_on[k] - before;
        peak = std::max(peak, std::abs(d));
        if (std::abs(d) > 1.0) settle_s = 5.4 + static_cast<double>(k) * kBlock / kRate - 6.0;
    }
    const double off_jump = mom_off[index_of(8.0)] - mom_off[index_of(5.9)];
    std::printf("[autogain-v2] shape edit: AUTO off jumps %+.2f LU; v2 peak excursion %.2f LU, "
                "back within 1 LU %.2f s after the edit\n", off_jump, peak, settle_s);
    REQUIRE(off_jump > 4.0);              // the edit really is loud without AUTO
    CHECK(peak < off_jump);
    CHECK(settle_s <= 1.0);
}
