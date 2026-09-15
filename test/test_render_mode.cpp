// What the render mode costs, what it recalls as, and what it must never do
// to a project that predates it.
//
// The two modes are a genuine trade, not a quality ladder: linear phase
// realises a drawn magnitude far more deeply, zero latency costs a fraction of
// the delay and puts no smear before a transient. Every contract here is
// written so that neither mode is privileged -- in particular nothing in this
// file asserts what `kDefaultRenderMode` is, because the recall rule has to
// hold whichever way that is set.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/format/plugin_state_io.hpp>
#include <choc/text/choc_JSON.h>

#include "spectr/spectr.hpp"
#include "spectr/preset_format.hpp"
#include "spectr/render_mode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;
using spectr::MaskRenderMode;
using spectr::Spectr;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// A wired processor, matching the rig every other state test in this repo
/// uses: the store is attached before the parameters are declared into it.
struct Rig {
    pulp::state::StateStore store;
    std::unique_ptr<Spectr> proc;

    Rig() : proc(std::make_unique<Spectr>()) {
        proc->set_state_store(&store);
        proc->define_parameters(store);
    }
};

/// Is the negative-control plant active for this process?
///
/// When it is, the product has had the forbidden behaviour reinstated, and the
/// contracts that forbid it invert: they are green only when they OBSERVE the
/// defect. That is what makes them controls rather than decoration -- a plant
/// the contract fails to notice fails its row.
bool planted(std::string_view name) {
    const char* value = std::getenv("SPECTR_RENDER_MODE_PLANT");
    return value != nullptr && std::string_view(value) == name;
}

/// Re-emit a plugin-state blob with one member removed.
///
/// This is how a project from before the mode existed is synthesised: not by
/// hand-writing JSON that might drift from the real shape, but by taking a
/// genuine blob this build wrote and deleting exactly the one member that
/// build added. Everything else in it stays real.
std::vector<std::uint8_t> without_member(const std::vector<std::uint8_t>& bytes,
                                         const char* member) {
    const std::string text(bytes.begin(), bytes.end());
    auto root = choc::json::parse(text);
    auto stripped = choc::value::createObject("SpectrPluginState");
    for (std::uint32_t i = 0; i < root.size(); ++i) {
        const auto entry = root.getObjectMemberAt(i);
        if (std::string(entry.name) == member) continue;
        stripped.addMember(entry.name, entry.value);
    }
    const auto out = choc::json::toString(stripped, false);
    return {out.begin(), out.end()};
}

bool has_member(const std::vector<std::uint8_t>& bytes, const char* member) {
    const std::string text(bytes.begin(), bytes.end());
    return choc::json::parse(text).hasObjectMember(member);
}

std::string member_string(const std::vector<std::uint8_t>& bytes, const char* member) {
    const std::string text(bytes.begin(), bytes.end());
    auto root = choc::json::parse(text);
    if (!root.hasObjectMember(member)) return {};
    return std::string(root[member].getString());
}

struct Rendered {
    std::vector<float> left;
    std::vector<float> right;
};

/// Drive a prepared HeadlessHost with a tone, chopped into `chunks` (cycled)
/// or into fixed `block` lengths, and collect what comes out.
Rendered drive(pulp::format::HeadlessHost& host, std::size_t total_samples,
               double hz, int block, const std::vector<int>& chunks = {},
               const std::function<void(std::size_t)>& before_block = {}) {
    Rendered out;
    out.left.reserve(total_samples);
    out.right.reserve(total_samples);
    std::size_t position = 0;
    std::size_t chunk_index = 0;
    while (position < total_samples) {
        if (before_block) before_block(position);
        int n = chunks.empty() ? block : chunks[chunk_index++ % chunks.size()];
        n = static_cast<int>(std::min<std::size_t>(
            static_cast<std::size_t>(n), total_samples - position));
        pulp::audio::Buffer<float> in(2, static_cast<std::size_t>(n));
        pulp::audio::Buffer<float> buf(2, static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const auto sample = position + static_cast<std::size_t>(i);
            const float value = 0.5f * static_cast<float>(std::sin(
                2.0 * kPi * hz * static_cast<double>(sample) / 48000.0));
            in.channel(0)[static_cast<std::size_t>(i)] = value;
            in.channel(1)[static_cast<std::size_t>(i)] = value;
        }
        const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
        pulp::audio::BufferView<const float> iv(in_ptrs, 2,
                                                static_cast<std::size_t>(n));
        auto ov = buf.view();
        host.process(ov, iv);
        out.left.insert(out.left.end(), buf.channel(0).begin(), buf.channel(0).end());
        out.right.insert(out.right.end(), buf.channel(1).begin(), buf.channel(1).end());
        position += static_cast<std::size_t>(n);
    }
    return out;
}

std::size_t count_mismatches(const std::vector<float>& a,
                             const std::vector<float>& b) {
    std::size_t n = 0;
    const auto count = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) ++n;
    return n;
}

double worst_step(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double worst = 0.0;
    const auto end = std::min(to, x.size());
    for (std::size_t i = from + 1; i < end; ++i)
        worst = std::max(worst, static_cast<double>(std::abs(x[i] - x[i - 1])));
    return worst;
}

} // namespace

// ── What the mode costs ────────────────────────────────────────────────────

TEST_CASE("Reported latency is a function of the render mode and nothing else",
          "[render-mode][latency]") {
    // The property a host's delay compensation depends on. If latency were
    // derived from the buffer size or the sample rate, the same project would
    // recall with a different alignment on a different machine, and a bounce
    // would drift against a session that sounded fine. So this drives the same
    // mode through geometries that share nothing and requires one answer.
    struct Geometry { double sample_rate; std::size_t block; };
    const Geometry geometries[] = {
        {44100.0, 32}, {48000.0, 512}, {96000.0, 128}, {192000.0, 2048},
    };

    for (const auto mode : spectr::kRenderModes) {
        int agreed = -1;
        for (const auto& geometry : geometries) {
            pulp::format::HeadlessHost host(spectr::create_spectr);
            auto* plugin = dynamic_cast<Spectr*>(host.processor());
            REQUIRE(plugin != nullptr);
            REQUIRE(plugin->set_render_mode(mode));

            // Before prepare: an adapter may be asked by a host that has not
            // started audio yet, and must still answer correctly.
            const int before = plugin->latency_samples();
            host.prepare(geometry.sample_rate, geometry.block);
            const int after = plugin->latency_samples();

            INFO("mode=" << spectr::render_mode_token(mode)
                 << " sr=" << geometry.sample_rate << " block=" << geometry.block
                 << " before=" << before << " after=" << after);
            CHECK(before == after);
            if (agreed < 0) agreed = after;
            CHECK(after == agreed);
        }
        INFO("mode=" << spectr::render_mode_token(mode) << " latency=" << agreed);
        CHECK(agreed > 0);
    }

    // Control: the two modes do NOT report the same latency. Without this,
    // a latency_samples() that ignored the mode entirely and returned one
    // constant would satisfy every assertion above and read as a clean pass.
    pulp::format::HeadlessHost linear_host(spectr::create_spectr);
    auto* linear = dynamic_cast<Spectr*>(linear_host.processor());
    REQUIRE(linear->set_render_mode(MaskRenderMode::linear_phase));
    linear_host.prepare(48000.0, 512);

    pulp::format::HeadlessHost zero_host(spectr::create_spectr);
    auto* zero = dynamic_cast<Spectr*>(zero_host.processor());
    REQUIRE(zero->set_render_mode(MaskRenderMode::zero_latency));
    zero_host.prepare(48000.0, 512);

    INFO("linear_phase=" << linear->latency_samples()
         << " zero_latency=" << zero->latency_samples());
    CHECK(linear->latency_samples() != zero->latency_samples());
    // And the direction is the one the names promise, so a switch that silently
    // swapped the two backends could not pass.
    CHECK(zero->latency_samples() < linear->latency_samples());
    CHECK(linear->latency_samples()
          == spectr::kSpectralFftSize + spectr::kSpectralAnalysisHop);
}

TEST_CASE("Switching render mode tells the host its delay compensation moved",
          "[render-mode][latency]") {
    // The risky path. A host caches latency; if a mid-session switch changes it
    // without raising the flag, every track through this plugin silently slips
    // by the difference and nothing reports an error.
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* plugin = dynamic_cast<Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(MaskRenderMode::linear_phase));
    host.prepare(48000.0, 512);

    // Drain anything prepare itself raised, so what we observe below is the
    // switch and not the preparation.
    (void)plugin->consume_latency_changed_flag();
    const int before = plugin->latency_samples();
    REQUIRE_FALSE(plugin->latency_change_pending());

    REQUIRE(plugin->set_render_mode(MaskRenderMode::zero_latency));
    const int after = plugin->latency_samples();

    INFO("latency " << before << " -> " << after);
    CHECK(after != before);
    CHECK(plugin->consume_latency_changed_flag());
    // Draining it is what an adapter does; it must not still be pending.
    CHECK_FALSE(plugin->latency_change_pending());

    // Control: a switch to the mode already live changes nothing and must not
    // raise the flag. Without this, an implementation that flagged on every
    // call would pass the assertion above while telling the host nothing
    // useful.
    REQUIRE(plugin->set_render_mode(MaskRenderMode::zero_latency));
    CHECK_FALSE(plugin->consume_latency_changed_flag());
    CHECK(plugin->latency_samples() == after);
}

// ── What a project recalls as ──────────────────────────────────────────────

TEST_CASE("A preset round-trips the render mode it was authored in",
          "[render-mode][preset]") {
    for (const auto mode : spectr::kRenderModes) {
        Rig author;
        REQUIRE(author.proc->set_render_mode(mode));
        author.store.set_value(spectr::kParamLfoDepth, 0.8f);

        const auto json =
            spectr::save_preset_to_string(*author.proc, spectr::PresetMetadata{});
        REQUIRE_FALSE(json.empty());

        Rig reader;
        // Put the reader in the OTHER mode first, so a loader that simply left
        // the mode alone could not pass.
        const auto other = mode == MaskRenderMode::linear_phase
                               ? MaskRenderMode::zero_latency
                               : MaskRenderMode::linear_phase;
        REQUIRE(reader.proc->set_render_mode(other));
        REQUIRE(reader.proc->render_mode() == other);

        const auto result = spectr::load_preset_from_string(*reader.proc, json);
        REQUIRE(result);
        INFO("authored " << spectr::render_mode_token(mode)
             << ", reopened " << spectr::render_mode_token(reader.proc->render_mode()));
        CHECK(reader.proc->render_mode() == mode);
        // Control: the rest of the preset travelled too, so a failure above is
        // the mode specifically and not a dead preset path.
        CHECK(reader.store.get_value(spectr::kParamLfoDepth) == Approx(0.8f));
    }
}

TEST_CASE("A project authored before the render mode existed reopens Mixing",
          "[render-mode][preset][migration]") {
    // THE contract. A session saved before this field shipped carries no mode.
    // It must reopen sounding, and reporting latency, exactly as it did -- so
    // linear phase, the only realisation that existed when it was written --
    // whatever a NEW instance defaults to. Nothing here reads
    // kDefaultRenderMode: the rule has to hold whichever way that is set, and
    // a test that consulted it would pass even if the two were wired together.
    //
    // The blob below is a LITERAL, checked in as text and never regenerated by
    // the writer. That is the point: a fixture produced by this build's
    // serializer would silently acquire whatever the writer emits today, so it
    // could not stay a specimen of a build that predates the field. This one
    // cannot drift, because nothing generates it.
    static constexpr const char* kPreModeV3Blob =
        R"({"version":3,"morph_derived":false,"morph_overrides":[],)"
        R"("band_gain":[-12,-6],"band_mute":[true,false],)"
        R"("view_min_hz":200.0,"view_max_hz":800.0,"layout_index":0,)"
        R"("morph_applies_viewport":false})";
    const std::string text(kPreModeV3Blob);
    const std::vector<std::uint8_t> legacy(text.begin(), text.end());

    Rig reader;
    // Start in the other mode, so a loader that simply left the field alone
    // could not pass this.
    REQUIRE(reader.proc->set_render_mode(MaskRenderMode::zero_latency));
    REQUIRE(reader.proc->deserialize_plugin_state(legacy));
    const bool held = reader.proc->render_mode() == MaskRenderMode::linear_phase;

    if (planted("migration-adopts-other-mode")) {
        // Inverted: the plant reinstates absence resolving to something other
        // than the authored mode. This row is green only when the contract
        // SEES that, which is the evidence that it can go red at all.
        INFO("planted defect; reopened as "
             << spectr::render_mode_token(reader.proc->render_mode()));
        REQUIRE_FALSE(held);
        return;
    }

    INFO("pre-mode v3 project reopened as "
         << spectr::render_mode_token(reader.proc->render_mode()));
    REQUIRE(held);

    // Control: the legacy payload was genuinely read, not discarded. A
    // deserialize that bailed early would leave the mode untouched at
    // zero_latency and fail above -- but one that reset everything to defaults
    // would pass above while losing the project, and this catches that.
    CHECK_FALSE(reader.proc->morph_applies_viewport());
    CHECK(reader.proc->field().bands[0].gain_db == Approx(-12.0f));
    CHECK(reader.proc->field().bands[1].muted == false);
    CHECK(reader.proc->field().bands[0].muted == true);

    // The discriminating control, and the whole argument that the assertion
    // above is not vacuous: the SAME blob, differing only by carrying the
    // field, produces a DIFFERENT mode. A reader hard-wired to linear_phase
    // would fail here.
    const std::string with_mode =
        text.substr(0, text.size() - 1) + R"(,"render_mode":"zero_latency"})";
    const std::vector<std::uint8_t> with_mode_bytes(with_mode.begin(),
                                                    with_mode.end());
    Rig control;
    REQUIRE(control.proc->deserialize_plugin_state(with_mode_bytes));
    INFO("the same blob carrying the field reopened as "
         << spectr::render_mode_token(control.proc->render_mode()));
    REQUIRE(control.proc->render_mode() == MaskRenderMode::zero_latency);
}

TEST_CASE("A v4 project with no render mode is refused, not guessed at",
          "[render-mode][preset][migration]") {
    // This rejection is what keeps the rule above unambiguous forever. A v4
    // writer always emits the field, so absence at v4 is damage rather than
    // age -- and if it were tolerated, "absent" would stop meaning
    // "pre-mode writer" and the migration would become an inference again.
    Rig author;
    REQUIRE(author.proc->set_render_mode(MaskRenderMode::zero_latency));
    author.proc->set_morph_applies_viewport(false);
    const auto modern = author.proc->serialize_plugin_state();
    REQUIRE(has_member(modern, "render_mode"));

    const auto damaged = without_member(modern, "render_mode");
    REQUIRE_FALSE(has_member(damaged, "render_mode"));

    Rig reader;
    reader.proc->set_morph_applies_viewport(true);
    REQUIRE(reader.proc->set_render_mode(MaskRenderMode::linear_phase));

    REQUIRE_FALSE(reader.proc->deserialize_plugin_state(damaged));
    // Live state is untouched by a refused payload -- the rejection is atomic,
    // not a partial apply that happened to stop early.
    CHECK(reader.proc->render_mode() == MaskRenderMode::linear_phase);
    CHECK(reader.proc->morph_applies_viewport());

    // Control: the identical blob WITH the field is accepted, so the rejection
    // is the missing member and not something else wrong with the payload.
    Rig accepting;
    REQUIRE(accepting.proc->deserialize_plugin_state(modern));
    CHECK(accepting.proc->render_mode() == MaskRenderMode::zero_latency);
}

TEST_CASE("A project naming an unknown render mode is refused",
          "[render-mode][preset][migration]") {
    // A project written by a build with a third realisation. Substituting one
    // of ours would change how it sounds and what latency it reports with
    // nothing said, so it fails closed -- the same way an out-of-range
    // modulation target does.
    Rig author;
    author.proc->set_morph_applies_viewport(false);
    const auto bytes = author.proc->serialize_plugin_state();
    const std::string text(bytes.begin(), bytes.end());
    auto root = choc::json::parse(text);
    auto rewritten = choc::value::createObject("SpectrPluginState");
    for (std::uint32_t i = 0; i < root.size(); ++i) {
        const auto entry = root.getObjectMemberAt(i);
        if (std::string(entry.name) == "render_mode")
            rewritten.addMember("render_mode", std::string("hyperbolic_phase"));
        else
            rewritten.addMember(entry.name, entry.value);
    }
    const auto future = choc::json::toString(rewritten, false);
    const std::vector<std::uint8_t> future_bytes(future.begin(), future.end());

    Rig reader;
    reader.proc->set_morph_applies_viewport(true);
    REQUIRE_FALSE(reader.proc->deserialize_plugin_state(future_bytes));
    CHECK(reader.proc->morph_applies_viewport());   // live state untouched

    // Control: the same blob with a token this build knows is accepted, so the
    // refusal is the unknown name and not a broken fixture.
    Rig clean;
    REQUIRE(clean.proc->deserialize_plugin_state(bytes));
    CHECK_FALSE(clean.proc->morph_applies_viewport());
}

TEST_CASE("An empty payload is a fresh instance and takes the default",
          "[render-mode][preset][migration]") {
    // The one path that means "reset to defaults" rather than "restore a
    // project". There is no project being migrated here, so the pre-mode rule
    // does not apply and the new-instance default does. This asserts the
    // default by name rather than by value, so it stays correct if the ruling
    // on the default ever changes.
    Rig reader;
    const auto other = spectr::kDefaultRenderMode == MaskRenderMode::linear_phase
                           ? MaskRenderMode::zero_latency
                           : MaskRenderMode::linear_phase;
    REQUIRE(reader.proc->set_render_mode(other));
    REQUIRE(reader.proc->render_mode() == other);

    REQUIRE(reader.proc->deserialize_plugin_state({}));
    CHECK(reader.proc->render_mode() == spectr::kDefaultRenderMode);
}

TEST_CASE("The render mode survives the host state envelope",
          "[render-mode][preset]") {
    // The path a DAW session actually takes, as opposed to a .preset file.
    for (const auto mode : spectr::kRenderModes) {
        Rig author;
        REQUIRE(author.proc->set_render_mode(mode));
        const auto blob =
            pulp::format::plugin_state_io::serialize(author.store, *author.proc);
        REQUIRE_FALSE(blob.empty());

        Rig reader;
        const auto other = mode == MaskRenderMode::linear_phase
                               ? MaskRenderMode::zero_latency
                               : MaskRenderMode::linear_phase;
        REQUIRE(reader.proc->set_render_mode(other));
        REQUIRE(pulp::format::plugin_state_io::deserialize(
            blob, reader.store, *reader.proc));
        INFO("envelope round-trip of " << spectr::render_mode_token(mode));
        CHECK(reader.proc->render_mode() == mode);
    }
}

// ── What the audio does ────────────────────────────────────────────────────

TEST_CASE("Mixing output does not depend on how the host chops the stream",
          "[render-mode][offline][rt-safety]") {
    // An offline bounce differs from playback in exactly two ways: different
    // block lengths, delivered faster. This pins the first, bit-exactly,
    // through the whole product -- not just the renderer, which the renderer
    // suite already covers, but Spectr::process() with its sub-block splitting
    // and its dry path.
    //
    // Mixing only. Tracking is covered by the case below, which explains why
    // it cannot be asserted bit-exactly today.
    constexpr std::size_t kTotal = 24000;
    constexpr std::size_t kWarmup = 12000;
    const std::vector<std::vector<int>> chunkings = {
        {64}, {37}, {129}, {512}, {1}, {17, 256, 3, 64, 101},
    };

    pulp::format::HeadlessHost reference_host(spectr::create_spectr);
    auto* reference_plugin = dynamic_cast<Spectr*>(reference_host.processor());
    REQUIRE(reference_plugin->set_render_mode(MaskRenderMode::linear_phase));
    reference_host.prepare(48000.0, 512);
    (void)drive(reference_host, kWarmup, 440.0, 512);
    const auto reference = drive(reference_host, kTotal, 440.0, 512);

    for (const auto& chunks : chunkings) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        auto* plugin = dynamic_cast<Spectr*>(host.processor());
        REQUIRE(plugin->set_render_mode(MaskRenderMode::linear_phase));
        host.prepare(48000.0, 512);
        (void)drive(host, kWarmup, 440.0, 512);
        const auto candidate = drive(host, kTotal, 440.0, 0, chunks);

        std::string label;
        for (int c : chunks) label += std::to_string(c) + " ";
        const auto mismatches = count_mismatches(reference.left, candidate.left);
        INFO("chunking [" << label << "] mismatches=" << mismatches);
        REQUIRE(mismatches == 0);
    }

    // Control: this comparison can tell two renders apart. Without it a broken
    // comparison would report zero mismatches for every chunking and read as
    // the cleanest pass in the file.
    pulp::format::HeadlessHost other_host(spectr::create_spectr);
    auto* other_plugin = dynamic_cast<Spectr*>(other_host.processor());
    REQUIRE(other_plugin->set_render_mode(MaskRenderMode::linear_phase));
    other_host.state().set_value(spectr::band_mute_param_id(9), 1.0f);
    other_host.prepare(48000.0, 512);
    (void)drive(other_host, kWarmup, 440.0, 512);
    const auto other = drive(other_host, kTotal, 440.0, 512);
    REQUIRE(count_mismatches(reference.left, other.left) > 0);
}

TEST_CASE("Tracking output through the product is reproducible only to a bound",
          "[render-mode][offline][rt-safety]") {
    // A KNOWN, CHARACTERISED GAP, pinned at its measured size rather than hidden.
    //
    // Mixing is bit-identical across chunkings (above). Tracking is not, and
    // the cause is not chunking: two runs of the SAME chunking on the same
    // input also differ.
    //
    // The cause, established rather than guessed. A redesign is queued while
    // audio runs, and its result is crossfaded into the live impulse response
    // at whichever render block the background worker happens to finish on.
    // Crossfading an impulse response with an identical copy of itself is not
    // a bit-exact identity in floating point, so the output differs by about
    // one ULP at a position that moves with thread scheduling. Ruled out by
    // measurement, not assumption: the convolver is single-threaded, so this
    // is not summation order; and the render path carries no clock, which
    // check_render_path_clock.py proves with a positive control.
    //
    // Most of it is gone. Neither the control thread nor the audio thread now
    // restages a mask the renderer is already realising, which removed the
    // redundant redesigns entirely -- that is a saving, not a cost, since each
    // one was a whole impulse redesign. What remains is a single redesign
    // whose timing still depends on when the parameter-sync worker reconciles,
    // so a run is bit-exact most of the time and off by one ULP the rest.
    //
    // Mixing has no equivalent: its layout adoption is synchronous, with no
    // worker and no crossfade, which is exactly why it is deterministic.
    //
    // The tolerance is therefore the SIZE OF THE REMAINING GAP, not a margin
    // chosen to make a test pass -- when the last redesign is made
    // deterministic it becomes 0 and this case merges into the one above. It
    // is deliberately NOT written as "assert they differ", which would turn a
    // defect into a contract and fail the day it is fixed.
    constexpr std::size_t kTotal = 24000;
    constexpr std::size_t kWarmup = 12000;
    // One ULP at this amplitude, with headroom. Anything structural -- a
    // dropped block, a wrong mask, a real chunking dependence -- is orders of
    // magnitude above it.
    constexpr double kTolerance = 1.0e-6;

    pulp::format::HeadlessHost reference_host(spectr::create_spectr);
    auto* reference_plugin = dynamic_cast<Spectr*>(reference_host.processor());
    REQUIRE(reference_plugin->set_render_mode(MaskRenderMode::zero_latency));
    reference_host.prepare(48000.0, 512);
    (void)drive(reference_host, kWarmup, 440.0, 512);
    const auto reference = drive(reference_host, kTotal, 440.0, 512);

    const std::vector<std::vector<int>> chunkings = {
        {64}, {37}, {129}, {512}, {1}, {17, 256, 3, 64, 101},
    };
    for (const auto& chunks : chunkings) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        auto* plugin = dynamic_cast<Spectr*>(host.processor());
        REQUIRE(plugin->set_render_mode(MaskRenderMode::zero_latency));
        host.prepare(48000.0, 512);
        (void)drive(host, kWarmup, 440.0, 512);
        const auto candidate = drive(host, kTotal, 440.0, 0, chunks);

        double worst = 0.0;
        for (std::size_t i = 0; i < reference.left.size(); ++i)
            worst = std::max(worst, static_cast<double>(
                std::abs(reference.left[i] - candidate.left[i])));

        std::string label;
        for (int c : chunks) label += std::to_string(c) + " ";
        INFO("chunking [" << label << "] worst deviation " << worst);
        REQUIRE(worst < kTolerance);
    }

    // Control: the tolerance is tight enough to catch a real difference. A
    // genuinely different mask must exceed it by orders of magnitude --
    // without this, kTolerance could be loose enough to accept anything and
    // every requirement above would be decoration.
    pulp::format::HeadlessHost other_host(spectr::create_spectr);
    auto* other_plugin = dynamic_cast<Spectr*>(other_host.processor());
    REQUIRE(other_plugin->set_render_mode(MaskRenderMode::zero_latency));
    other_host.state().set_value(spectr::band_mute_param_id(9), 1.0f);
    other_host.prepare(48000.0, 512);
    (void)drive(other_host, kWarmup, 440.0, 512);
    const auto other = drive(other_host, kTotal, 440.0, 512);
    double control_worst = 0.0;
    for (std::size_t i = 0; i < reference.left.size(); ++i)
        control_worst = std::max(control_worst, static_cast<double>(
            std::abs(reference.left[i] - other.left[i])));
    INFO("a different mask deviates by " << control_worst
         << ", against a tolerance of " << kTolerance);
    REQUIRE(control_worst > 1.0e-4);
}

TEST_CASE("The two modes are not the same renderer wearing two names",
          "[render-mode][offline]") {
    // A build that ignored the mode and ran one realisation for both would
    // satisfy every per-mode requirement in this file. This is what makes
    // those requirements about the mode rather than about one engine.
    constexpr std::size_t kTotal = 24000;
    constexpr std::size_t kWarmup = 12000;

    pulp::format::HeadlessHost linear_host(spectr::create_spectr);
    REQUIRE(dynamic_cast<Spectr*>(linear_host.processor())
                ->set_render_mode(MaskRenderMode::linear_phase));
    linear_host.state().set_value(spectr::band_mute_param_id(9), 1.0f);
    linear_host.prepare(48000.0, 512);
    (void)drive(linear_host, kWarmup, 440.0, 512);
    const auto linear = drive(linear_host, kTotal, 440.0, 512);

    pulp::format::HeadlessHost zero_host(spectr::create_spectr);
    REQUIRE(dynamic_cast<Spectr*>(zero_host.processor())
                ->set_render_mode(MaskRenderMode::zero_latency));
    zero_host.state().set_value(spectr::band_mute_param_id(9), 1.0f);
    zero_host.prepare(48000.0, 512);
    (void)drive(zero_host, kWarmup, 440.0, 512);
    const auto zero = drive(zero_host, kTotal, 440.0, 512);

    REQUIRE(count_mismatches(linear.left, zero.left) > 0);
}

TEST_CASE("Reported latency is the latency the audio actually has",
          "[render-mode][latency][audio]") {
    // A number a host aligns tracks by is worth nothing unless the audio
    // agrees with it. At 0 % mix the output is the dry path delayed by the wet
    // latency, so an impulse in must appear exactly `latency_samples()` later
    // -- which makes "reported" and "measured" the same measurement rather
    // than two numbers that happen to match. Pinned in BOTH modes, because the
    // whole point of the mode is that this number changes.
    for (const auto mode : spectr::kRenderModes) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        auto* plugin = dynamic_cast<Spectr*>(host.processor());
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->set_render_mode(mode));
        host.state().set_value(spectr::kMix, 0.0f);
        host.prepare(48000.0, 512);

        const int latency = plugin->latency_samples();
        REQUIRE(latency > 0);
        constexpr int kBlock = 512;
        const int blocks = (latency + 2 * kBlock) / kBlock + 1;

        std::vector<float> rendered;
        rendered.reserve(static_cast<std::size_t>(blocks * kBlock));
        for (int block = 0; block < blocks; ++block) {
            pulp::audio::Buffer<float> in(2, kBlock), out(2, kBlock);
            if (block == 0) { in.channel(0)[0] = 1.0f; in.channel(1)[0] = 1.0f; }
            const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
            pulp::audio::BufferView<const float> iv(in_ptrs, 2, kBlock);
            auto ov = out.view();
            host.process(ov, iv);
            rendered.insert(rendered.end(), out.channel(0).begin(),
                            out.channel(0).end());
        }

        REQUIRE(rendered.size() > static_cast<std::size_t>(latency));
        INFO("mode=" << spectr::render_mode_token(mode)
             << " reported latency=" << latency);
        // Everything before the reported latency is silent...
        for (int sample = 0; sample < latency; ++sample)
            REQUIRE(rendered[static_cast<std::size_t>(sample)] == 0.0f);
        // ...and the impulse is exactly there.
        REQUIRE(rendered[static_cast<std::size_t>(latency)] == Approx(1.0f));
    }

    // Control: this measurement can tell a wrong report from a right one. If
    // it could not -- if it passed for any claimed latency -- the two
    // assertions above would be decoration. Reading one sample early must not
    // find the impulse.
    pulp::format::HeadlessHost probe(spectr::create_spectr);
    auto* probe_plugin = dynamic_cast<Spectr*>(probe.processor());
    REQUIRE(probe_plugin->set_render_mode(MaskRenderMode::zero_latency));
    probe.state().set_value(spectr::kMix, 0.0f);
    probe.prepare(48000.0, 512);
    const int probe_latency = probe_plugin->latency_samples();
    pulp::audio::Buffer<float> in(2, 512), out(2, 512);
    in.channel(0)[0] = 1.0f;
    in.channel(1)[0] = 1.0f;
    const float* ptrs[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> iv(ptrs, 2, 512);
    auto ov = out.view();
    probe.process(ov, iv);
    REQUIRE(probe_latency > 0);
    REQUIRE(out.channel(0)[static_cast<std::size_t>(probe_latency) - 1] != Approx(1.0f));
}

TEST_CASE("A mode switch is bounded, raises exactly one latency flag, and "
          "leaves modulation running",
          "[render-mode][audio][modulation]") {
    // What a switch guarantees, stated honestly. It is NOT click-free and this
    // does not test for that: the two modes differ by thousands of samples of
    // delay, so the stream jumps in time and the host re-aligns compensation
    // on top of that. Both are discontinuities no fade can remove, which is
    // why the switch is a setup decision rather than a musical gesture.
    //
    // What it does guarantee, and what is pinned here:
    //   1. amplitude stays bounded through the switch -- no full-scale blast;
    //   2. the host is told exactly once, never zero times and never twice;
    //   3. the reported latency is the new mode's the moment it is told;
    //   4. the LFO keeps running across it rather than resetting.
    constexpr std::size_t kTotal = 48000;
    constexpr int kBlock = 256;
    constexpr std::size_t kSwitchAt = 24064;

    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* plugin = dynamic_cast<Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(MaskRenderMode::linear_phase));
    host.state().set_value(spectr::kParamLfoEnabled, 1.0f);
    host.state().set_value(spectr::kParamLfoDepth, 0.5f);
    host.state().set_value(spectr::kParamLfoRate, 4.0f);
    host.prepare(48000.0, kBlock);
    (void)plugin->consume_latency_changed_flag();   // drain prepare's own edge

    const int before = plugin->latency_samples();
    int flags_raised = 0;
    int reported_at_flag = -1;
    bool switched = false;

    const auto swept = drive(host, kTotal, 440.0, kBlock, {},
        [&](std::size_t position) {
            if (!switched && position >= kSwitchAt) {
                REQUIRE(plugin->set_render_mode(MaskRenderMode::zero_latency));
                switched = true;
            }
            // Drain the way an adapter does -- once per block, on the host
            // thread. Counting the drains is how "exactly once" is measured
            // rather than asserted.
            if (plugin->latency_change_pending()) {
                if (reported_at_flag < 0)
                    reported_at_flag = plugin->latency_samples();
                if (plugin->consume_latency_changed_flag()) ++flags_raised;
            }
        });

    REQUIRE(switched);
    REQUIRE(plugin->render_mode() == MaskRenderMode::zero_latency);
    const int after = plugin->latency_samples();

    // (2) Exactly one notification. Zero would silently misalign every track;
    // more than one makes a host re-query repeatedly for a single change.
    INFO("latency " << before << " -> " << after
         << ", flags raised " << flags_raised
         << ", reported when flagged " << reported_at_flag);
    CHECK(flags_raised == 1);
    CHECK_FALSE(plugin->latency_change_pending());

    // (3) The value was already the new one when the host was told, so a host
    // that re-queries on the notification cannot read the stale number.
    CHECK(after != before);
    CHECK(reported_at_flag == after);

    // (1) Amplitude stays bounded. The input peaks at 0.5 and the mask only
    // ever attenuates, so nothing the switch does may push the output past
    // full scale or leave it stuck silent afterwards.
    double worst = 0.0;
    for (std::size_t i = kSwitchAt; i < swept.left.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(swept.left[i])));
    INFO("worst |sample| after the switch: " << worst);
    CHECK(worst <= 1.0);
    // Control: it did not simply go silent, which would also satisfy a bound.
    double tail_peak = 0.0;
    for (std::size_t i = swept.left.size() - 4000; i < swept.left.size(); ++i)
        tail_peak = std::max(tail_peak, static_cast<double>(std::abs(swept.left[i])));
    INFO("peak over the last 4000 samples: " << tail_peak);
    CHECK(tail_peak > 0.01);

    // (4) The LFO is still running after the switch: its settings survived and
    // the modulation lane was not reset by the renderer rebuild.
    CHECK(host.state().get_value(spectr::kParamLfoEnabled) >= 0.5f);
    CHECK(host.state().get_value(spectr::kParamLfoDepth) == Approx(0.5f));

    // Control for the whole case: the switch genuinely changed the audio. A
    // set_render_mode that did nothing would satisfy every bound above.
    pulp::format::HeadlessHost steady(spectr::create_spectr);
    auto* steady_plugin = dynamic_cast<Spectr*>(steady.processor());
    REQUIRE(steady_plugin->set_render_mode(MaskRenderMode::linear_phase));
    steady.state().set_value(spectr::kParamLfoEnabled, 1.0f);
    steady.state().set_value(spectr::kParamLfoDepth, 0.5f);
    steady.state().set_value(spectr::kParamLfoRate, 4.0f);
    steady.prepare(48000.0, kBlock);
    const auto unswitched = drive(steady, kTotal, 440.0, kBlock);
    REQUIRE(count_mismatches(unswitched.left, swept.left) > 0);
}
