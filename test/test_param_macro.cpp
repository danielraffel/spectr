// Band-group macros — the host-automatable overlay.
//
// A macro is one host parameter that adds dB to a user-chosen subset of the
// 64 canonical band slots. The reason it exists is a GESTURE problem, not a
// DSP one: dragging a selection used to commit one `Band NN Gain` write per
// selected band, each with its own host gesture bracket, so a host's
// parameter-learn latched onto whichever band was written first and the user
// ended up automating one arbitrary band instead of the group they drew.
//
// The tests below are ordered by what they protect:
//   - the pure resolver's arithmetic (summing, addressing, withholding);
//   - the domain rules layered on it (clamp once, skip muted, skip inert);
//   - that a macro move is ONE bracket on ONE parameter, which is the
//     property the whole feature is for;
//   - that the audio thread and the control thread compose it identically.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/state/store.hpp>

#include "spectr/spectr.hpp"

#include <algorithm>
#include <vector>

using Catch::Approx;

namespace {

struct Wired {
    pulp::state::StateStore         store;
    std::unique_ptr<spectr::Spectr> proc;

    Wired() : proc(std::make_unique<spectr::Spectr>()) {
        proc->set_state_store(&store);
        proc->define_parameters(store);
    }
};

// The documented ID scheme (docs/parameter-surface.md), spelled as literals
// so a careless edit to the production constant cannot move the test's
// expectation with it.
constexpr pulp::state::ParamID kMacroBase = 4200;
constexpr pulp::state::ParamID kGainBase  = 1000;

/// Every macro contributes its full value — the identity functor, used to
/// test the resolver's addressing and summing without any domain policy.
constexpr auto kWholeValue = [](std::size_t, std::size_t, float v) { return v; };

} // namespace

// ── The pure resolver ────────────────────────────────────────────────────

TEST_CASE("macro resolver: a macro reaches exactly the slots it addresses") {
    spectr::BandMacroBank bank;
    // A deliberately SCATTERED set. Contiguity is the easy case and is not
    // what macros are for: the feature exists so a user can drive bands that
    // are nowhere near each other as one lane.
    bank.members[0].set(3);
    bank.members[0].set(17);
    bank.members[0].set(63);
    bank.values[0] = 6.0f;

    const auto offsets = spectr::resolve_macro_offsets(bank, kWholeValue);

    CHECK(offsets[3] == Approx(6.0f));
    CHECK(offsets[17] == Approx(6.0f));
    CHECK(offsets[63] == Approx(6.0f));
    // The control for the three assertions above: every slot the macro does
    // NOT address must be exactly zero. Without this the test would pass just
    // as well if the resolver wrote its value everywhere.
    std::size_t reached = 0;
    for (std::size_t slot = 0; slot < spectr::kMaxBands; ++slot)
        if (offsets[slot] != 0.0f) ++reached;
    CHECK(reached == 3);
}

TEST_CASE("macro resolver: overlapping macros sum, in any order") {
    spectr::BandMacroBank bank;
    bank.members[0].set(10);
    bank.members[1].set(10);
    bank.members[2].set(10);
    bank.values[0] = 3.0f;
    bank.values[1] = -1.5f;
    bank.values[2] = 0.25f;

    const auto offsets = spectr::resolve_macro_offsets(bank, kWholeValue);
    CHECK(offsets[10] == Approx(3.0f - 1.5f + 0.25f));

    // Commutativity is the property that makes "no ordering state" true, so
    // it is asserted rather than assumed: re-labelling which macro holds
    // which value must not move the result.
    spectr::BandMacroBank reordered;
    reordered.members[0].set(10);
    reordered.members[1].set(10);
    reordered.members[2].set(10);
    reordered.values[0] = 0.25f;
    reordered.values[1] = 3.0f;
    reordered.values[2] = -1.5f;
    CHECK(spectr::resolve_macro_offsets(reordered, kWholeValue)[10]
          == Approx(offsets[10]));
}

TEST_CASE("macro resolver: an empty membership contributes nothing") {
    spectr::BandMacroBank bank;
    // Values on every macro, members on none. A macro that is worth
    // something but addresses nothing is the ordinary state of three of the
    // four macros in a real session, so it must be exactly inert.
    for (std::size_t m = 0; m < spectr::kMacroCount; ++m)
        bank.values[m] = 24.0f;

    const auto offsets = spectr::resolve_macro_offsets(bank, kWholeValue);
    for (std::size_t slot = 0; slot < spectr::kMaxBands; ++slot)
        CHECK(offsets[slot] == 0.0f);

    // Positive control on the same instrument: give ONE macro a member and
    // the identical call must now report something. A test whose only
    // assertion is "everything is zero" cannot tell an inert feature from a
    // broken measurement.
    bank.members[2].set(41);
    CHECK(spectr::resolve_macro_offsets(bank, kWholeValue)[41] == Approx(24.0f));
}

TEST_CASE("macro resolver: the functor decides what a macro withholds") {
    spectr::BandMacroBank bank;
    bank.members[0].set(1);
    bank.members[0].set(2);
    bank.values[0] = 8.0f;

    // The resolver owns no policy of its own: a functor returning 0 for a
    // slot is how a caller expresses ineligibility, and it must be honoured
    // per slot rather than per macro.
    const auto offsets = spectr::resolve_macro_offsets(
        bank, [](std::size_t, std::size_t slot, float v) {
            return slot == 1 ? 0.0f : v;
        });
    CHECK(offsets[1] == 0.0f);
    CHECK(offsets[2] == Approx(8.0f));
}

// ── The band-domain rules ────────────────────────────────────────────────

TEST_CASE("macro field: offsets are shape-preserving and clamped once") {
    spectr::BandField field;
    field.bands[0].gain_db = -12.0f;
    field.bands[1].gain_db = 0.0f;
    field.bands[2].gain_db = 20.0f;

    spectr::BandMacroBank bank;
    for (std::size_t slot = 0; slot < 3; ++slot) bank.members[0].set(slot);
    bank.values[0] = 6.0f;

    spectr::apply_macro_offsets(field, 32, bank);

    // Shape preserved: every member moved by the same amount, so the contour
    // the user drew inside the group survives.
    CHECK(field.bands[0].gain_db == Approx(-6.0f));
    CHECK(field.bands[1].gain_db == Approx(6.0f));
    // ...except where the band range stops it. 20 + 6 saturates at +24.
    CHECK(field.bands[2].gain_db == Approx(24.0f));
}

TEST_CASE("macro field: two macros on one band clamp after summing, not before") {
    spectr::BandField field;
    field.bands[5].gain_db = 0.0f;

    spectr::BandMacroBank bank;
    bank.members[0].set(5);
    bank.members[1].set(5);
    // +20 then -20. Clamping per macro would pin the intermediate at +20 and
    // land on 0 by luck here, so the case is chosen to expose the ORDER
    // dependence instead: +20 and -8 must give +12 whichever is applied
    // first, and a per-macro clamp of the +20 intermediate would still give
    // +12. The clamp difference shows at the boundary below.
    bank.values[0] = 20.0f;
    bank.values[1] = -8.0f;
    spectr::apply_macro_offsets(field, 32, bank);
    CHECK(field.bands[5].gain_db == Approx(12.0f));

    // The discriminating case: +20 and +20 on a band at -24. Summed first
    // that is -24 + 40 = +16. Clamped per macro it would be clamp(-24+20) =
    // -4, then clamp(-4+20) = +16 as well -- so use an asymmetric pair that
    // actually separates them: +30 is out of a macro's own range, so instead
    // drive two macros that individually saturate the band and must not.
    spectr::BandField boundary;
    boundary.bands[0].gain_db = -24.0f;
    spectr::BandMacroBank pair;
    pair.members[0].set(0);
    pair.members[1].set(0);
    pair.values[0] = 24.0f;
    pair.values[1] = -24.0f;
    spectr::apply_macro_offsets(boundary, 32, pair);
    // Sum first: -24 + 24 - 24 = -24. A per-macro clamp would give
    // clamp(-24+24)=0 then clamp(0-24)=-24 in one order but
    // clamp(-24-24)=-24 then clamp(-24+24)=0 in the other -- order-dependent,
    // which is exactly what summing-then-clamping removes.
    CHECK(boundary.bands[0].gain_db == Approx(-24.0f));
}

TEST_CASE("macro field: a muted member gets no offset and stays muted") {
    spectr::BandField field;
    field.bands[4].gain_db = -3.0f;
    field.bands[4].muted = true;
    field.bands[5].gain_db = -3.0f;

    spectr::BandMacroBank bank;
    bank.members[0].set(4);
    bank.members[0].set(5);
    bank.values[0] = 12.0f;

    spectr::apply_macro_offsets(field, 32, bank);

    // The muted member keeps BOTH its authored level and its mute. Keeping
    // the level matters the instant the user unmutes: they get back what they
    // drew, not whatever the macro happened to be worth.
    CHECK(field.bands[4].gain_db == Approx(-3.0f));
    CHECK(field.bands[4].muted);
    // The unmuted sibling is the positive control — same macro, same value.
    // Without it, a resolver that withheld EVERY offset would pass.
    CHECK(field.bands[5].gain_db == Approx(9.0f));
}

TEST_CASE("macro field: slots at or above the visible count are inert") {
    spectr::BandField field;
    spectr::BandMacroBank bank;
    bank.members[0].set(10);
    bank.members[0].set(40);
    bank.values[0] = 6.0f;

    spectr::apply_macro_offsets(field, 32, bank);
    CHECK(field.bands[10].gain_db == Approx(6.0f));
    CHECK(field.bands[40].gain_db == Approx(0.0f));

    // Widening the layout must REACH the member that was out of view, which
    // is what makes "membership survives a band-count change" true rather
    // than merely un-crashing.
    spectr::BandField wider;
    spectr::apply_macro_offsets(wider, 64, bank);
    CHECK(wider.bands[40].gain_db == Approx(6.0f));
}

// ── The gesture contract: one bracket, one parameter ─────────────────────

TEST_CASE("macro drag emits one gesture bracket on one parameter") {
    Wired w;
    std::vector<pulp::state::ParamID> begins;
    std::vector<pulp::state::ParamID> ends;
    std::vector<pulp::state::ParamID> changed;
    w.store.set_gesture_callbacks(
        [&begins](pulp::state::ParamID id) { begins.push_back(id); },
        [&ends](pulp::state::ParamID id) { ends.push_back(id); });
    auto listener = w.store.add_listener(
        [&changed](pulp::state::ParamID id, float) { changed.push_back(id); },
        pulp::state::ListenerThread::Main);

    // A macro driving a scattered group — the shape a user gets from a
    // marquee selection across the spectrum.
    spectr::MacroMembership<spectr::kMaxBands> members;
    for (const std::size_t slot : {2u, 9u, 21u, 30u}) members.set(slot);
    REQUIRE(w.proc->set_macro_members(0, members));

    begins.clear();
    ends.clear();
    changed.clear();

    // One drag: many value updates between one start and one end.
    w.proc->begin_param_gesture_epoch();
    for (int step = 0; step < 24; ++step)
        REQUIRE(w.proc->set_macro_value(0, static_cast<float>(step) * 0.5f));
    w.proc->end_param_gesture_epoch();

    // THE contract. A host's parameter-learn records the first parameter it
    // sees a gesture on, so "exactly one begin, on the macro" is the whole
    // difference between the user automating their group and the user
    // automating whichever band happened to be written first.
    CHECK(begins.size() == 1);
    CHECK(ends.size() == 1);
    CHECK(begins.front() == kMacroBase);
    CHECK(ends.front() == kMacroBase);
    CHECK(w.store.open_gesture_count() == 0);

    // No band lane was touched at all — not written, not gestured. A macro
    // that echoed to its members would re-create the very problem it exists
    // to remove, and would do it invisibly because the SOUND would be right.
    const auto is_band_gain = [](pulp::state::ParamID id) {
        return id >= kGainBase
            && id < kGainBase + static_cast<int>(spectr::kMaxBands);
    };
    CHECK(std::none_of(begins.begin(), begins.end(), is_band_gain));
    CHECK(std::none_of(ends.begin(), ends.end(), is_band_gain));
    std::size_t band_writes = 0;
    for (const auto id : changed)
        if (id >= kGainBase && id < kGainBase + static_cast<int>(spectr::kMaxBands))
            ++band_writes;
    CHECK(band_writes == 0);
    CHECK(std::count(changed.begin(), changed.end(), kMacroBase) > 0);

    // The value really did land, so the assertions above are about a drag
    // that happened rather than a drag that was dropped.
    CHECK(w.store.get_value(kMacroBase) == Approx(11.5f));
}

TEST_CASE("macro drag: the old group-drag shape is what it replaces") {
    // The contrast that gives the test above its meaning. Moving the same
    // four bands the way a group drag used to — by writing their gain lanes —
    // emits FOUR brackets on four different parameters, and a host's learn
    // takes the first. This is not an assertion about desired behaviour; it
    // is the measurement of the defect, kept here so the macro test's "one"
    // is visibly a change from "four" rather than a number with no scale.
    Wired w;
    std::vector<pulp::state::ParamID> begins;
    w.store.set_gesture_callbacks(
        [&begins](pulp::state::ParamID id) { begins.push_back(id); },
        [](pulp::state::ParamID) {});

    auto field = w.proc->field();
    for (const std::size_t slot : {2u, 9u, 21u, 30u})
        field.bands[slot].gain_db = 6.0f;
    REQUIRE(w.proc->replace_processing_state(
        field, w.proc->viewport(), w.proc->layout()));

    std::size_t band_begins = 0;
    for (const auto id : begins)
        if (id >= kGainBase && id < kGainBase + static_cast<int>(spectr::kMaxBands))
            ++band_begins;
    CHECK(band_begins == 4);
}

// ── Membership state ─────────────────────────────────────────────────────

TEST_CASE("macro membership is kept across a band-count change") {
    Wired w;
    spectr::MacroMembership<spectr::kMaxBands> members;
    members.set(5);
    members.set(40);
    REQUIRE(w.proc->set_macro_members(1, members));

    // Widen, then narrow, then widen again. The first move is what makes the
    // narrowing a real change rather than a no-op against the default
    // 32-band layout — without it `apply_surface_params` sees no drift and
    // the test would be asserting that nothing happened.
    w.store.set_value(3003, 64.0f);
    REQUIRE(w.proc->apply_surface_params(true));
    REQUIRE(spectr::visible_count(w.proc->layout()) == 64);

    // Narrowing makes slot 40 inert, but must not forget it: the layout is a
    // projection onto the first N slots, not a reshaping of state.
    w.store.set_value(3003, 32.0f);
    REQUIRE(w.proc->apply_surface_params(true));
    REQUIRE(spectr::visible_count(w.proc->layout()) == 32);
    CHECK(w.proc->macro_members(1).test(40));

    // Widening again has to restore it intact.
    w.store.set_value(3003, 64.0f);
    REQUIRE(w.proc->apply_surface_params(true));
    CHECK(w.proc->macro_members(1).test(5));
    CHECK(w.proc->macro_members(1).test(40));
}

TEST_CASE("macro index out of range is refused, not clamped") {
    Wired w;
    spectr::MacroMembership<spectr::kMaxBands> members;
    members.set(0);
    CHECK_FALSE(w.proc->set_macro_members(spectr::kMacroCount, members));
    CHECK_FALSE(w.proc->set_macro_value(spectr::kMacroCount, 1.0f));
    // The control: the last legal index must work, so the refusal above is
    // about the boundary and not about the call being broken outright.
    CHECK(w.proc->set_macro_members(spectr::kMacroCount - 1, members));
    CHECK(w.proc->set_macro_value(spectr::kMacroCount - 1, 1.0f));
}

// ── Audio / control parity ───────────────────────────────────────────────

TEST_CASE("the audio owner composes macros exactly as the control thread does") {
    constexpr std::size_t block_size = 256;
    pulp::format::HeadlessHost host(spectr::create_spectr);
    host.prepare(48000.0, block_size);
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);

    spectr::MacroMembership<spectr::kMaxBands> members;
    for (const std::size_t slot : {1u, 6u, 19u, 27u}) members.set(slot);
    REQUIRE(plugin->set_macro_members(0, members));
    spectr::MacroMembership<spectr::kMaxBands> overlap;
    overlap.set(6);   // deliberately shared with macro 0
    overlap.set(11);
    REQUIRE(plugin->set_macro_members(1, overlap));

    pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
    const float* input_channels[] = {
        in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> input(input_channels, 2, block_size);
    auto output = out.view();

    for (std::size_t block = 0; block < 8; ++block) {
        pulp::state::ParameterEventQueue events;
        // A non-flat drawn curve, so a macro that replaced levels instead of
        // offsetting them would be visible in the comparison.
        for (std::size_t band = 0; band < 32; ++band)
            REQUIRE(events.push({spectr::band_gain_param_id(band), 0,
                                 -9.0f + static_cast<float>(band % 5), 0}));
        REQUIRE(events.push({spectr::band_mute_param_id(19), 0, 1.0f, 0}));
        REQUIRE(events.push({spectr::macro_param_id(0), 0, 7.0f, 0}));
        REQUIRE(events.push({spectr::macro_param_id(1), 0, -4.0f, 0}));
        // An LFO is enabled only so the audio owner publishes the frame this
        // test reads. The comparison is against `pre_field`, which is the
        // post-morph, post-macro, PRE-LFO field, so the oscillator's value
        // never enters the assertion.
        REQUIRE(events.push({spectr::kParamLfoEnabled, 0, 1.0f, 0}));
        REQUIRE(events.push({spectr::kParamLfoDepth, 0, 1.0f, 0}));
        host.process(output, input, events);
    }

    const auto& snapshot = plugin->read_modulated_field();
    REQUIRE(snapshot.active);

    // The control thread's own answer, composed through the same shared
    // function over its own canonical state.
    const auto state = plugin->processing_state_snapshot();
    auto expected = state.field;
    spectr::apply_macro_offsets(expected, spectr::visible_count(state.layout),
                                plugin->macro_bank());

    const auto visible = spectr::visible_count(state.layout);
    for (std::size_t slot = 0; slot < visible; ++slot) {
        INFO("slot " << slot);
        CHECK(snapshot.pre_field.bands[slot].gain_db
              == Approx(expected.bands[slot].gain_db));
        CHECK(snapshot.pre_field.bands[slot].muted
              == expected.bands[slot].muted);
    }

    // The parity assertion above is only meaningful if the macros actually
    // moved something. Slot 1 is in macro 0 only (+7), slot 6 is in both
    // (+7-4 = +3), slot 11 is in macro 1 only (-4), and slot 19 is muted so
    // it gets nothing. Asserting the arithmetic here is the positive control
    // for the loop: without it, two identically-broken paths would agree.
    CHECK(snapshot.pre_field.bands[1].gain_db
          == Approx(state.field.bands[1].gain_db + 7.0f));
    CHECK(snapshot.pre_field.bands[6].gain_db
          == Approx(state.field.bands[6].gain_db + 3.0f));
    CHECK(snapshot.pre_field.bands[11].gain_db
          == Approx(state.field.bands[11].gain_db - 4.0f));
    CHECK(snapshot.pre_field.bands[19].gain_db
          == Approx(state.field.bands[19].gain_db));
    // A slot in no macro is untouched — the control that proves the offsets
    // are addressed rather than broadcast.
    CHECK(snapshot.pre_field.bands[3].gain_db
          == Approx(state.field.bands[3].gain_db));
}
