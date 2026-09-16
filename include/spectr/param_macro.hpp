#pragma once

// A bank of macro offsets, each addressed to a SUBSET of a fixed slot set.
//
// This file is deliberately domain-free. It knows nothing about bands, dB,
// mutes, audio or Spectr — it owns exactly one idea: which slots each macro
// reaches, and that the reaching ones SUM. Everything that needs a domain
// (what a value means, which slots are eligible, what range the result must
// land in) is supplied by the caller through the `contribution` functor.
//
// It is shaped for `pulp/state/param_macro.hpp` and carries no Spectr
// includes, so it can be lifted upstream verbatim once a Pulp release and
// the SDK repin it would queue behind have landed. Until then it lives here
// and Spectr is its only caller.
//
// WHY A FUNCTOR RATHER THAN A FLAGS ARGUMENT. The eligibility rules are the
// part that differs per domain and per call site — Spectr withholds an
// offset from a muted slot and from a slot above the visible count, and both
// of those are facts about a BandField that this file must not learn. A
// functor keeps the sum here (where two threads must agree on it exactly)
// and the policy there (where the domain lives), so the audio thread and the
// control thread can share this function without sharing a copy of the
// policy.
//
// NO CLAMP. Summing is associative and commutative; clamping is neither.
// Clamping per macro would make the result depend on the order the macros
// happen to be visited, which is precisely the ordering state a summing
// overlay exists to avoid. The caller sums here and clamps ONCE, in its own
// domain's range.

#include <array>
#include <bitset>
#include <cstddef>

namespace spectr {

/// The slots one macro addresses, as a bit per canonical slot index.
template <std::size_t SlotCount>
using MacroMembership = std::bitset<SlotCount>;

/// `MacroCount` macros over `SlotCount` slots: what each one addresses, and
/// what each one is currently worth.
///
/// Membership and value are carried together because a resolve needs both and
/// they must describe the same instant. They are SOURCED differently, though,
/// and a caller has to respect that: in Spectr the value is a host-automatable
/// parameter read per block, while the membership is editor state that reaches
/// the audio thread through a publication — so the bank is assembled at the
/// point of use rather than stored as one object.
template <std::size_t MacroCount, std::size_t SlotCount>
struct MacroBank {
    std::array<MacroMembership<SlotCount>, MacroCount> members{};
    std::array<float, MacroCount> values{};

    static constexpr std::size_t macro_count() noexcept { return MacroCount; }
    static constexpr std::size_t slot_count() noexcept { return SlotCount; }
};

/// Sum every macro's contribution into the slots it addresses.
///
/// `contribution(macro_index, slot_index, macro_value) -> float` returns what
/// that macro contributes to that slot. Return 0 to withhold it — that is how
/// a caller expresses "this slot is not eligible" without this function having
/// to know why.
///
/// The result is a per-slot offset, unclamped, in whatever unit the functor
/// returned. A slot no macro addresses is exactly 0, so a caller can treat 0
/// as "nothing to do" and leave the underlying value bit-identical.
template <std::size_t MacroCount, std::size_t SlotCount, typename Contribution>
constexpr std::array<float, SlotCount> resolve_macro_offsets(
    const MacroBank<MacroCount, SlotCount>& bank,
    Contribution&& contribution) {
    std::array<float, SlotCount> out{};
    for (std::size_t macro = 0; macro < MacroCount; ++macro) {
        const auto& membership = bank.members[macro];
        // An unassigned macro cannot reach anything, whatever it is worth.
        // Skipping it here is a cost gate only: the loop below would add
        // nothing for every slot anyway.
        if (membership.none()) continue;
        const float value = bank.values[macro];
        for (std::size_t slot = 0; slot < SlotCount; ++slot) {
            if (!membership.test(slot)) continue;
            out[slot] += contribution(macro, slot, value);
        }
    }
    return out;
}

} // namespace spectr
