// Auto Gain v2 transients: what happens when the material, the transport or
// Freeze changes, measured from the change rather than after it settles.
//
// Each case reports, per change, T1dB -- the applied gain's time to within
// 1 dB of where it settles -- and the largest error of the output's momentary
// (400 ms, K-weighted) loudness against a reference render with AUTO off and
// a flat shape, which is what a perfect Auto Gain would sound like.
//
// Gates: T1dB <= 1.0 s after a locate (warm), <= 1.5 s after a change of
// material or a Freeze release; max momentary error <= 6 LU through a locate,
// a host reset and a Freeze engage or release. An ABRUPT change of material
// cannot meet 6 LU with any causal Auto Gain: when the needed gain jumps by
// 24 dB, even a perfect compensator that reacts 10 ms after the switch leaves
// the 400 ms momentary window 10 log10((0.01 x 251 + 0.39) / 0.4) = 8.6 LU
// over, and one that needs a single 170 ms analysis window 20.3 LU. Those
// cases are gated on T1dB and on how long the error stays above 6 LU
// (<= 1.5 s), and their maximum is reported. Negative controls
// (CMakeLists.txt):
//
//   SPECTR_LEVEL_PLANT=autogain-v2-reset-on-seek    the first v2 forgot the
//                                                   material at every locate
//   SPECTR_LEVEL_PLANT=autogain-v2-stale-on-change  the first v2's transitions:
//                                                   no change detection, no
//                                                   level-drop rule, no Freeze
//                                                   leg switch, 6 dB/s only

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/plugin_state_io.hpp>
#include <choc/text/choc_JSON.h>

#include "autogain_harness.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace autogain_harness;
using Catch::Approx;

namespace {

constexpr double kSeekT1dbGate = 1.0;
constexpr double kChangeT1dbGate = 1.5;
constexpr double kMomentaryGate = 6.0;
constexpr double kOverMomentaryGateSeconds = 1.5;

const Shape kHighBoost = region("high broad +24", 24, 31, 24.0f);
const Shape kLowBoost = region("low broad +24", 0, 9, 24.0f);
const Shape kFlat{"flat", {}};

struct Worst {
    double t1db = 0.0, momentary = 0.0, over = 0.0;
    void add(const Transition& t) {
        t1db = std::max(t1db, t.t1db);
        momentary = std::max(momentary, t.max_momentary_error);
        over = std::max(over, t.seconds_over_6lu);
    }
};

} // namespace

TEST_CASE("Auto Gain v2 keeps its level across a locate",
          "[level][autogain][v2][transient][audio]") {
    // Play, locate (a host reset) every 3 s. The material is unchanged, so
    // the right gain after a locate is the gain before it.
    const auto n = seconds(15.0);
    struct Case { const char* name; Stereo in; Shape shape; };
    const Case cases[] = {{"bass line", bass_line(n), kHighBoost},
                          {"drum loop", drum_loop(n), kLowBoost},
                          {"vocal", vocal(n), kHighBoost}};
    RenderOptions o;
    for (double t = 3.0; t < 15.0; t += 3.0) o.resets.push_back(seconds(t));
    Worst worst;
    for (const auto& c : cases) {
        const auto v2 = render(c.in, c.shape, Mode::v2, o);
        const auto ref = render(c.in, kFlat, Mode::off, o);
        for (double t = 3.0; t < 15.0; t += 3.0) {
            const auto tr = transition(v2, ref, t, t + 2.9, t + 2.9);
            std::printf("[autogain-v2-transient] locate %-9s at %4.1f s: T1dB %.2f s, "
                        "max momentary error %.2f LU, gain %+.2f dB\n",
                        c.name, t, tr.t1db, tr.max_momentary_error, tr.settled_db);
            worst.add(tr);
        }
    }
    std::printf("[autogain-v2-transient] locate: worst T1dB %.2f s, worst momentary error "
                "%.2f LU\n", worst.t1db, worst.momentary);
    CHECK(worst.t1db <= kSeekT1dbGate);
    CHECK(worst.momentary <= kMomentaryGate);
}

TEST_CASE("Auto Gain v2 keeps a playing hold's level across a host reset",
          "[level][autogain][v2][transient][freeze][audio]") {
    // Bass frozen at 3 s, the live input turns to hats at 5 s, a host reset
    // at 7 s while the hold plays on.
    const Stereo in = concat(bass_line(seconds(5.0)), hats(seconds(10.0)));
    RenderOptions o;
    o.freeze_at = seconds(3.0);
    o.resets = {seconds(7.0)};
    const auto v2 = render(in, kHighBoost, Mode::v2, o);
    const auto ref = render(in, kFlat, Mode::off, o);
    const auto tr = transition(v2, ref, 7.0, 14.0, 14.0);
    std::printf("[autogain-v2-transient] host reset mid-hold: T1dB %.2f s, max momentary "
                "error %.2f LU\n", tr.t1db, tr.max_momentary_error);
    CHECK(tr.t1db <= kSeekT1dbGate);
    CHECK(tr.max_momentary_error <= kMomentaryGate);
}

TEST_CASE("Auto Gain v2 follows a Freeze engage and release",
          "[level][autogain][v2][transient][freeze][audio]") {
    // Bass line; Freeze at 4.5 s; the live input turns to hats at 5 s;
    // release at 9 s, after which the mask shapes the live hats.
    const Stereo in = concat(bass_line(seconds(5.0)), hats(seconds(15.0)));
    RenderOptions o;
    o.freeze_at = seconds(4.5);
    o.release_at = seconds(9.0);
    o.finish = [](pulp::format::HeadlessHost&, spectr::Spectr& plugin) {
        const auto& sp = plugin.auto_gain_material().spectrum();
        std::printf("[autogain-v2-transient] Freeze case: %llu restarts, %llu level drops\n",
                    static_cast<unsigned long long>(sp.restarts()),
                    static_cast<unsigned long long>(sp.level_drops()));
    };
    const auto v2 = render(in, kHighBoost, Mode::v2, o);
    const auto ref = render(in, kFlat, Mode::off, o);
    const auto engage = transition(v2, ref, 4.5, 9.0, 8.9);
    const auto release = transition(v2, ref, 9.0, 20.0, 19.9);
    std::printf("[autogain-v2-transient] Freeze engage: T1dB %.2f s, max momentary error "
                "%.2f LU; release: T1dB %.2f s, max momentary error %.2f LU (gain %+.2f dB "
                "held, %+.2f dB after)\n", engage.t1db, engage.max_momentary_error,
                release.t1db, release.max_momentary_error, engage.settled_db,
                release.settled_db);
    CHECK(engage.t1db <= kChangeT1dbGate);
    CHECK(engage.max_momentary_error <= kMomentaryGate);
    CHECK(release.t1db <= kChangeT1dbGate);
    CHECK(release.max_momentary_error <= kMomentaryGate);
    // Control: the release really moves the gain (bass held -> hats live).
    REQUIRE(std::abs(release.settled_db - engage.settled_db) > 10.0);
}

TEST_CASE("Auto Gain v2 follows a change of material to quieter and louder",
          "[level][autogain][v2][transient][audio]") {
    const auto half = seconds(8.0);
    const double quieter = std::pow(10.0, -15.0 / 20.0);
    struct Case { const char* name; Stereo in; };
    const Case cases[] = {
        {"bass -> hats", concat(bass_line(half), hats(half))},
        {"bass -> hats -15 dB", concat(bass_line(half), scaled(hats(half), quieter))},
        {"hats -> bass -15 dB", concat(hats(half), scaled(bass_line(half), quieter))},
        {"hats -15 dB -> bass", concat(scaled(hats(half), quieter), bass_line(half))},
        {"pink -> vocal -15 dB", concat(pink(half, 3u), scaled(vocal(half), quieter))},
    };
    Worst worst;
    for (const auto& c : cases) {
        const auto v2 = render(c.in, kHighBoost, Mode::v2);
        const auto ref = render(c.in, kFlat, Mode::off);
        const auto tr = transition(v2, ref, 8.0, 16.0, 15.9);
        const auto off = render(c.in, kHighBoost, Mode::off);
        const auto untouched = transition(off, ref, 8.0, 16.0, 15.9);
        std::printf("[autogain-v2-transient] %-21s T1dB %.2f s, max momentary error %.2f LU, "
                    "%.2f s over 6 LU (AUTO off: %.2f LU, %.2f s) (gain %+.2f dB)\n", c.name,
                    tr.t1db, tr.max_momentary_error, tr.seconds_over_6lu,
                    untouched.max_momentary_error, untouched.seconds_over_6lu, tr.settled_db);
        worst.add(tr);
    }
    std::printf("[autogain-v2-transient] material change: worst T1dB %.2f s, worst momentary "
                "error %.2f LU, worst time over 6 LU %.2f s\n", worst.t1db, worst.momentary,
                worst.over);
    CHECK(worst.t1db <= kChangeT1dbGate);
    CHECK(worst.over <= kOverMomentaryGateSeconds);
}

TEST_CASE("Auto Gain v2 follows material that switches every 4 s",
          "[level][autogain][v2][transient][audio]") {
    const auto part = seconds(4.0);
    Stereo in = bass_line(part);
    in = concat(in, hats(part));
    in = concat(in, vocal(part));
    in = concat(in, drum_loop(part));
    in = concat(in, bass_line(part));
    const auto v2 = render(in, kHighBoost, Mode::v2);
    const auto ref = render(in, kFlat, Mode::off);
    Worst worst;
    for (double t = 4.0; t < 20.0; t += 4.0) {
        const auto tr = transition(v2, ref, t, t + 3.9, t + 3.9);
        std::printf("[autogain-v2-transient] switch at %4.1f s: T1dB %.2f s, max momentary "
                    "error %.2f LU, %.2f s over 6 LU (gain %+.2f dB)\n", t, tr.t1db,
                    tr.max_momentary_error, tr.seconds_over_6lu, tr.settled_db);
        worst.add(tr);
    }
    CHECK(worst.t1db <= kChangeT1dbGate);
    CHECK(worst.over <= kOverMomentaryGateSeconds);
}

TEST_CASE("Auto Gain v2 converges quickly from a cold start",
          "[level][autogain][v2][transient][audio]") {
    // No history at all (a new instance): starts at v1's answer and must
    // reach the material's own quickly.
    const auto n = seconds(10.0);
    struct Case { const char* name; Stereo in; Shape shape; };
    const Case cases[] = {
        {"bass line", bass_line(n), kHighBoost},
        {"hats", hats(n), region("high broad -24", 24, 31, -24.0f)},
        {"sine 1k", sine(n), region("mid narrow -24", band_of(1000.0), band_of(1000.0), -24.0f)},
        {"drum loop", drum_loop(n), kLowBoost},
    };
    Worst worst;
    for (const auto& c : cases) {
        const auto v2 = render(c.in, c.shape, Mode::v2);
        const auto ref = render(c.in, kFlat, Mode::off);
        const auto tr = transition(v2, ref, 0.0, 5.0, 9.9);
        std::printf("[autogain-v2-transient] cold start %-9s T1dB %.2f s, max momentary error "
                    "%.2f LU, %.2f s over 6 LU (gain %+.2f dB)\n", c.name, tr.t1db,
                    tr.max_momentary_error, tr.seconds_over_6lu, tr.settled_db);
        worst.add(tr);
    }
    CHECK(worst.t1db <= kChangeT1dbGate);
}

TEST_CASE("Auto Gain v2 starts warm from a saved session",
          "[level][autogain][v2][transient][state][audio]") {
    // Play 10 s, save; a new instance restores that session and plays the
    // same material from the start: no cold start, and the same gain.
    const Stereo in = bass_line(seconds(10.0));
    std::vector<std::uint8_t> blob;
    RenderOptions save;
    save.finish = [&](pulp::format::HeadlessHost& host, spectr::Spectr& plugin) {
        blob = pulp::format::plugin_state_io::serialize(host.state(), plugin);
    };
    const auto first = render(in, kHighBoost, Mode::v2, save);
    REQUIRE_FALSE(blob.empty());
    RenderOptions restore;
    restore.setup = [&](pulp::format::HeadlessHost& host, spectr::Spectr& plugin) {
        REQUIRE(pulp::format::plugin_state_io::deserialize(blob, host.state(), plugin));
    };
    const auto again = render(in, kHighBoost, Mode::v2, restore);
    // The same saved state is the same start: a second restore (a bounce)
    // renders the same samples, here with a different host block size.
    RenderOptions bounce = restore;
    bounce.chunks = {37, 4096, 1000};
    const auto replay = render(in, kHighBoost, Mode::v2, bounce);
    std::size_t differ = 0;
    for (std::size_t i = 0; i < in.size(); ++i)
        differ += std::abs(again.out.l[i] - replay.out.l[i]) > 1.0e-6f;
    std::printf("[autogain-v2-transient] two renders from one saved state: %zu samples differ "
                "by > 1e-6\n", differ);
    CHECK(differ == 0);
    const auto cold = render(in, kHighBoost, Mode::v2);
    std::printf("[autogain-v2-transient] warm restore: first block %+.2f dB (saved session "
                "ended at %+.2f dB; a cold start begins at %+.2f dB)\n",
                again.applied_db.front(), first.applied_db.back(), cold.applied_db.front());
    CHECK(again.applied_db.front() == Approx(first.applied_db.back()).margin(1.0));
    double worst = 0.0;
    for (const float g : again.applied_db)
        worst = std::max(worst, std::abs(static_cast<double>(g) - first.applied_db.back()));
    CHECK(worst <= 1.0);
    // Control: without the saved estimate the start really is far off.
    REQUIRE(std::abs(cold.applied_db.front() - first.applied_db.back()) > 6.0);
}

TEST_CASE("A session that saved AUTO on before v2 keeps v1 until AUTO is toggled",
          "[level][autogain][v2][state]") {
    using spectr::AutoGainModel;
    pulp::format::HeadlessHost writer(spectr::create_spectr);
    auto* w = writer.processor_as<spectr::Spectr>();
    writer.state().set_value(spectr::kParamAutoGain, 1.0f);
    auto blob = pulp::format::plugin_state_io::serialize(writer.state(), *w);
    // A pre-v2 writer: the same blob without the auto_gain_* members.
    const auto strip = [](std::vector<std::uint8_t> state_blob, spectr::Spectr& plugin) {
        auto supplemental = plugin.serialize_plugin_state();
        const std::string text(supplemental.begin(), supplemental.end());
        auto root = choc::json::parse(text);
        auto stripped = choc::value::createObject("SpectrPluginState");
        for (std::uint32_t i = 0; i < root.size(); ++i) {
            const auto entry = root.getObjectMemberAt(i);
            const std::string name(entry.name);
            if (name == "auto_gain_model" || name == "auto_gain_estimate") continue;
            stripped.addMember(entry.name, entry.value);
        }
        const auto out = choc::json::toString(stripped, false);
        (void)state_blob;
        return std::vector<std::uint8_t>(out.begin(), out.end());
    };
    const auto old_supplemental = strip(blob, *w);

    pulp::format::HeadlessHost reader(spectr::create_spectr);
    auto* r = reader.processor_as<spectr::Spectr>();
    reader.state().set_value(spectr::kParamAutoGain, 1.0f);
    REQUIRE(r->deserialize_plugin_state(old_supplemental));
    CHECK(r->auto_gain_model() == AutoGainModel::reference_v1);
    CHECK(r->auto_gain_legacy_v1());

    // It plays v1: bass line + top octaves +24 reads v1's -20.6 dB.
    reader.prepare(kRate, kBlock);
    for (std::size_t i = 24; i < 32; ++i)
        reader.state().set_value(spectr::band_gain_param_id(i), 24.0f);
    const Stereo in = bass_line(seconds(6.0));
    std::vector<float> l(kBlock), rr(kBlock);
    const auto play = [&](std::size_t from, std::size_t to) {
        for (std::size_t pos = from; pos + kBlock <= to; pos += kBlock) {
            const float* ip[] = {in.l.data() + pos, in.r.data() + pos};
            float* op[] = {l.data(), rr.data()};
            pulp::audio::BufferView<const float> iv(ip, 2, kBlock);
            pulp::audio::BufferView<float> ov(op, 2, kBlock);
            reader.process(ov, iv);
        }
    };
    play(0, seconds(2.0));
    const float legacy_gain = r->auto_gain_applied_db();
    std::printf("[autogain-v2-transient] pre-v2 session, AUTO on: model %d, gain %+.2f dB\n",
                static_cast<int>(r->auto_gain_model()), legacy_gain);
    CHECK(legacy_gain == Approx(-20.6f).margin(0.5f));
    // Saving it again keeps v1.
    {
        const auto resaved = r->serialize_plugin_state();
        const std::string text(resaved.begin(), resaved.end());
        const auto root = choc::json::parse(text);
        CHECK(root["auto_gain_model"].getWithDefault<int64_t>(0) == 1);
    }
    // The user switches AUTO off and on: v2 from then on.
    reader.state().set_value(spectr::kParamAutoGain, 0.0f);
    play(seconds(2.0), seconds(2.5));
    reader.state().set_value(spectr::kParamAutoGain, 1.0f);
    play(seconds(2.5), seconds(6.0));
    std::printf("[autogain-v2-transient] after AUTO off/on: model %d, gain %+.2f dB\n",
                static_cast<int>(r->auto_gain_model()), r->auto_gain_applied_db());
    CHECK(r->auto_gain_model() == AutoGainModel::material_v2);
    CHECK_FALSE(r->auto_gain_legacy_v1());
    CHECK(std::abs(r->auto_gain_applied_db()) < 1.0f);

    // A pre-v2 session that saved AUTO off, and every new instance, run v2.
    pulp::format::HeadlessHost off_reader(spectr::create_spectr);
    auto* o = off_reader.processor_as<spectr::Spectr>();
    off_reader.state().set_value(spectr::kParamAutoGain, 0.0f);
    REQUIRE(o->deserialize_plugin_state(old_supplemental));
    CHECK(o->auto_gain_model() == AutoGainModel::material_v2);
    pulp::format::HeadlessHost fresh(spectr::create_spectr);
    CHECK(fresh.processor_as<spectr::Spectr>()->auto_gain_model() == AutoGainModel::material_v2);
    // A current save with AUTO on keeps v2.
    pulp::format::HeadlessHost current(spectr::create_spectr);
    auto* c = current.processor_as<spectr::Spectr>();
    current.state().set_value(spectr::kParamAutoGain, 1.0f);
    REQUIRE(c->deserialize_plugin_state(w->serialize_plugin_state()));
    CHECK(c->auto_gain_model() == AutoGainModel::material_v2);
}
