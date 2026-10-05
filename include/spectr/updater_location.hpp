#pragma once

// Where Spectr.app must be for Sparkle's scheduled checks to make sense.
//
// The update is the full installer package, and it installs to
// /Applications/Spectr.app -- not over whichever copy is running. A copy
// anywhere else (left in Downloads, moved to ~/Applications, a second copy on
// another volume) therefore never changes when "it" updates: after the
// install it is still the old version, so the next scheduled check offers the
// same update again, forever. The rule: scheduled checks run only from the
// install location; a manual "Check for Updates..." still works, after a
// warning. Not applied yet -- Pulp's standalone host owns the updater and has
// no hook for it (docs/updates.md) -- so only Spectr-test reads this today.

#include <string_view>

namespace spectr {

inline constexpr std::string_view kUpdaterInstallLocation = "/Applications/Spectr.app";

/// `bundle_path` is the running app's bundle path with symlinks resolved and
/// no trailing slash (the caller standardizes it).
[[nodiscard]] constexpr bool updater_runs_from_install_location(
    std::string_view bundle_path) noexcept {
    while (bundle_path.size() > 1 && bundle_path.back() == '/')
        bundle_path.remove_suffix(1);
    return bundle_path == kUpdaterInstallLocation;
}

} // namespace spectr
