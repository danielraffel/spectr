#pragma once

/// @file freeze_length.hpp
/// Freeze's musical Length: how much of the incoming sound a freeze takes in,
/// as a number of bars.
///
/// A length is EXACT: a whole number of bars plus one fraction of a bar from
/// a fixed, product-defined set. It is never a float, so 1 + 1/12 and
/// 2 + 3/16 serialise and come back exactly. This header is the one
/// definition of that set, of the presets the header dropdown offers, and of
/// what makes a length valid. The editor receives the set and the presets
/// from the processor (see make_freeze_payload_ in editor_bridge.cpp) rather
/// than keeping a copy, so the UI, the model, the plugin state and the tests
/// cannot disagree about which fractions exist.
///
/// Why not pulp::timebase::BeatDivision: that vocabulary is note values
/// measured in quarter notes (dotted and triplet notes down to 1/64). The
/// Length fractions are fractions of a BAR, and several of them (2/3, 5/6,
/// 5/8, 7/8, 15/16) are not note values at all, so no division in that table
/// names them. Its BeatFraction is the same {numerator, denominator} shape
/// used here.
///
/// Seconds come from the host's transport: a bar is
/// time_sig_numerator x (4 / time_sig_denominator) quarter notes, and the
/// host tempo counts quarter notes per minute.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace spectr {

/// The fraction of a bar a Length adds to its whole bars. Persisted by its
/// TEXT ("1/12"), never by this ordinal, so the list may grow; existing
/// entries must keep their text.
enum class LengthFraction : std::uint8_t {
    zero, f1_32, f1_16, f1_12, f1_8, f1_6, f3_16, f1_4, f1_3, f3_8,
    f1_2, f5_8, f2_3, f3_4, f5_6, f7_8, f15_16,
    count
};

struct BarFraction {
    int numerator = 0;
    int denominator = 1;
    std::string_view text;
};

/// THE canonical fraction list, in ascending order.
inline constexpr std::array<BarFraction, static_cast<std::size_t>(LengthFraction::count)>
    kLengthFractions{{
        {0, 1, "0"},     {1, 32, "1/32"}, {1, 16, "1/16"}, {1, 12, "1/12"},
        {1, 8, "1/8"},   {1, 6, "1/6"},   {3, 16, "3/16"}, {1, 4, "1/4"},
        {1, 3, "1/3"},   {3, 8, "3/8"},   {1, 2, "1/2"},   {5, 8, "5/8"},
        {2, 3, "2/3"},   {3, 4, "3/4"},   {5, 6, "5/6"},   {7, 8, "7/8"},
        {15, 16, "15/16"},
    }};

/// Bars run 0..kMaxLengthBars. An implementation limit, not a musical one:
/// the UI does not show it unless it is explaining why a value was refused.
inline constexpr int kMaxLengthBars = 128;

struct FreezeLength {
    int bars = 1;
    LengthFraction fraction = LengthFraction::zero;

    friend constexpr bool operator==(const FreezeLength& a, const FreezeLength& b) noexcept {
        return a.bars == b.bars && a.fraction == b.fraction;
    }
};

/// Why a candidate length was refused.
enum class LengthError : std::uint8_t {
    none,
    bars_below_zero,
    bars_above_limit,
    unknown_fraction,
    zero_length,
};

/// THE validation. Every path that admits a length -- the editor bridge,
/// plugin state, the host parameter and the tests -- goes through here.
constexpr LengthError validate_length(int bars, int fraction_index) noexcept {
    if (bars < 0) return LengthError::bars_below_zero;
    if (bars > kMaxLengthBars) return LengthError::bars_above_limit;
    if (fraction_index < 0
        || fraction_index >= static_cast<int>(LengthFraction::count))
        return LengthError::unknown_fraction;
    if (bars == 0 && fraction_index == 0) return LengthError::zero_length;
    return LengthError::none;
}

constexpr std::optional<FreezeLength> make_length(int bars, int fraction_index) noexcept {
    if (validate_length(bars, fraction_index) != LengthError::none) return std::nullopt;
    return FreezeLength{bars, static_cast<LengthFraction>(fraction_index)};
}

constexpr bool valid_length(const FreezeLength& length) noexcept {
    return validate_length(length.bars, static_cast<int>(length.fraction))
        == LengthError::none;
}

/// The fraction whose text is `text` ("1/8"), or -1. Exact text only: a
/// fraction outside the shipped set cannot be named into the model.
constexpr int fraction_index_from_text(std::string_view text) noexcept {
    for (std::size_t i = 0; i < kLengthFractions.size(); ++i)
        if (kLengthFractions[i].text == text) return static_cast<int>(i);
    return -1;
}

constexpr const BarFraction& fraction_of(const FreezeLength& length) noexcept {
    return kLengthFractions[static_cast<std::size_t>(length.fraction)];
}

/// The length as an exact count of 1/kLengthUnit bars: every fraction in the
/// set divides it, so equal lengths compare equal and none is rounded.
inline constexpr std::int64_t kLengthUnit = 96; // lcm(32, 12, 16, 6, 8, 3)
constexpr std::int64_t length_units(const FreezeLength& length) noexcept {
    const auto& f = fraction_of(length);
    return static_cast<std::int64_t>(length.bars) * kLengthUnit
        + kLengthUnit / f.denominator * f.numerator;
}
static_assert(kLengthUnit % 32 == 0 && kLengthUnit % 12 == 0,
              "every fraction's denominator must divide the unit");

/// The length in bars (a double only where it meets the clock).
constexpr double length_in_bars(const FreezeLength& length) noexcept {
    return static_cast<double>(length_units(length)) / static_cast<double>(kLengthUnit);
}

/// THE way a length is written, wherever it is shown (the header control,
/// the checked custom row, the custom editor's preview, the accessibility
/// label, the host parameter's display): a mixed number, "bar" up to one
/// bar and "bars" past it. "1/32 bar", "7/8 bar", "1 bar", "1 1/8 bars",
/// "2 bars", "2 3/16 bars". No whole bars shows the fraction alone.
inline std::string length_label(const FreezeLength& length) {
    const auto& f = fraction_of(length);
    const char* unit = length_units(length) <= kLengthUnit ? " bar" : " bars";
    if (length.fraction == LengthFraction::zero)
        return std::to_string(length.bars) + unit;
    if (length.bars == 0) return std::string(f.text) + unit;
    return std::to_string(length.bars) + " " + std::string(f.text) + unit;
}

/// The header dropdown's common values, in menu order. The Freeze Length
/// host parameter is an index into this list, with kLengthPresetCustom
/// meaning "the custom length in the plugin state". Shipped values: an
/// index is part of saved sessions and automation, so the list may only
/// grow at the end (before Custom moves, sessions would need migrating).
inline constexpr std::array<FreezeLength, 4> kLengthPresets{{
    {1, LengthFraction::zero}, {2, LengthFraction::zero},
    {4, LengthFraction::zero}, {8, LengthFraction::zero},
}};
inline constexpr int kLengthPresetCustom = static_cast<int>(kLengthPresets.size());
inline constexpr int kDefaultLengthPreset = 0; // 1 bar
inline constexpr FreezeLength kDefaultFreezeLength{1, LengthFraction::zero};

/// The preset a length is, or -1.
constexpr int preset_index_of(const FreezeLength& length) noexcept {
    for (std::size_t i = 0; i < kLengthPresets.size(); ++i)
        if (kLengthPresets[i] == length) return static_cast<int>(i);
    return -1;
}

/// The host parameter's value -> preset index (0..kLengthPresetCustom).
constexpr int length_preset_from_param(float value) noexcept {
    if (!(value == value)) return kDefaultLengthPreset; // NaN
    const float rounded = value < 0.0f ? 0.0f : value + 0.5f;
    const int index = static_cast<int>(rounded);
    return index > kLengthPresetCustom ? kLengthPresetCustom : index;
}

/// Packed for an atomic: bars * 32 + fraction. Always a valid length.
constexpr std::uint32_t pack_length(const FreezeLength& length) noexcept {
    return static_cast<std::uint32_t>(length.bars) * 32u
        + static_cast<std::uint32_t>(length.fraction);
}
constexpr FreezeLength unpack_length(std::uint32_t packed) noexcept {
    const auto fraction = static_cast<int>(packed % 32u);
    const auto bars = static_cast<int>(packed / 32u);
    const auto made = make_length(bars, fraction);
    return made ? *made : kDefaultFreezeLength;
}

/// Quarter notes per bar for a meter; a missing or nonsensical meter is 4/4.
constexpr double quarters_per_bar(int numerator, int denominator) noexcept {
    if (numerator <= 0 || denominator <= 0) return 4.0;
    return static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
}

/// Tempo to use: the host's, or 120 BPM when it gives none.
inline constexpr double kFallbackTempoBpm = 120.0;
constexpr double usable_tempo(double bpm) noexcept {
    return (bpm > 0.0 && bpm < 10000.0) ? bpm : kFallbackTempoBpm;
}

/// Length in seconds at a tempo (quarter notes per minute) and meter:
/// bars x quarters-per-bar x 60 / tempo.
constexpr double length_seconds(const FreezeLength& length, double tempo_bpm,
                                int time_sig_numerator,
                                int time_sig_denominator) noexcept {
    return length_in_bars(length)
        * quarters_per_bar(time_sig_numerator, time_sig_denominator) * 60.0
        / usable_tempo(tempo_bpm);
}

/// The valid length nearest `seconds` at a tempo and meter (an old
/// session's seconds Hold length). Ties go to the shorter length.
inline FreezeLength nearest_length(double seconds, double tempo_bpm,
                                   int time_sig_numerator,
                                   int time_sig_denominator) noexcept {
    const double bar = quarters_per_bar(time_sig_numerator, time_sig_denominator)
                     * 60.0 / usable_tempo(tempo_bpm);
    const double target_units = (seconds > 0.0 ? seconds : 0.0) / bar
                              * static_cast<double>(kLengthUnit);
    FreezeLength best = kDefaultFreezeLength;
    double best_distance = -1.0;
    for (int bars = 0; bars <= kMaxLengthBars; ++bars)
        for (int f = 0; f < static_cast<int>(LengthFraction::count); ++f) {
            const auto candidate = make_length(bars, f);
            if (!candidate) continue;
            const double d = static_cast<double>(length_units(*candidate)) - target_units;
            const double distance = d < 0.0 ? -d : d;
            if (best_distance < 0.0 || distance < best_distance) {
                best = *candidate;
                best_distance = distance;
            }
        }
    return best;
}

} // namespace spectr
