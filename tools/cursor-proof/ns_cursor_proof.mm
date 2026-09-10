// Observe the last hop of the cursor chain instead of transcribing it.
//
// The in-app probe stops at `View::CursorStyle`, because a headless capture has
// no cursor on screen to photograph. That leaves one unobserved step: the
// mapping `set_ns_cursor_for_style` performs from a CursorStyle to a real
// NSCursor. Naming the expected NSCursor in a table is not evidence about it --
// it is the same class of claim as "the value reaches the runtime".
//
// So this links the SHIPPING function out of the SDK's own static library,
// calls it, and reads back what AppKit says is current. A mismatch fails.

#import <AppKit/AppKit.h>
#include <pulp/view/view.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace pulp::view::mac_geometry {
void set_ns_cursor_for_style(pulp::view::View::CursorStyle style);
}

int main() {
    using CS = pulp::view::View::CursorStyle;
    struct Case { CS style; const char* row; NSCursor* expected; const char* name; };

    @autoreleasepool {
        // A planted negative, so a green run is known to be capable of red.
        // The plant swaps ONE expectation for a cursor the style does not map
        // to; a check that stays green under it is not checking anything.
        const bool plant = std::getenv("NS_CURSOR_PROOF_PLANT") != nullptr;
        std::vector<Case> cases = {
            {CS::crosshair, "CUR-1", [NSCursor crosshairCursor], "crosshairCursor"},
            {CS::grab, "CUR-2", [NSCursor openHandCursor], "openHandCursor"},
            {CS::grabbing, "CUR-3", [NSCursor closedHandCursor], "closedHandCursor"},
            {CS::horizontal_resize, "CUR-4", [NSCursor resizeLeftRightCursor],
             "resizeLeftRightCursor"},
        };

        if (plant) {
            cases[0].expected = [NSCursor IBeamCursor];
            cases[0].name = "IBeamCursor(PLANTED)";
            std::printf("CONTROL: planted CUR-1 expecting IBeamCursor\n");
        }

        int failures = 0;
        // A control that MUST separate the cases. If every style produced the
        // same NSCursor -- or if `currentCursor` never tracked `set` at all --
        // each assertion below would pass or fail together and prove nothing
        // about the mapping. Distinct pointers are what make the four checks
        // four checks.
        for (std::size_t i = 0; i < cases.size(); ++i)
            for (std::size_t j = i + 1; j < cases.size(); ++j)
                if (cases[i].expected == cases[j].expected) {
                    std::printf("BROKEN: %s and %s expect the same NSCursor "
                                "instance, so this test cannot tell them apart\n",
                                cases[i].name, cases[j].name);
                    return 4;
                }

        for (const auto& c : cases) {
            pulp::view::mac_geometry::set_ns_cursor_for_style(c.style);
            NSCursor* got = [NSCursor currentCursor];
            const bool ok = (got == c.expected);
            std::printf("%s  %-6s style -> %-24s current=%s\n",
                        ok ? "OK  " : "RED ", c.row, c.name,
                        got == c.expected ? c.name
                                          : [[got description] UTF8String]);
            if (!ok) ++failures;
        }

        // A deliberate mismatch, so a green run is known to be capable of red.
        pulp::view::mac_geometry::set_ns_cursor_for_style(CS::crosshair);
        if (!plant && [NSCursor currentCursor] == [NSCursor openHandCursor]) {
            std::printf("BROKEN: crosshair read back as openHandCursor; "
                        "currentCursor is not tracking set\n");
            return 4;
        }
        std::printf("CONTROL: crosshair does NOT read back as openHandCursor, "
                    "so a wrong mapping would be visible here\n");

        if (failures) {
            std::printf("RED    %d of %zu cursor styles mapped to the wrong "
                        "NSCursor\n", failures, cases.size());
            return 1;
        }
        std::printf("GREEN  every CursorStyle the CUR rows depend on maps to "
                    "the NSCursor a person would recognise\n");
    }
    return 0;
}
