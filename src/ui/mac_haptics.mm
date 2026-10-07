#include "spectr/mac_haptics.hpp"

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#endif

namespace spectr {

void mac_haptic_alignment_tick() noexcept {
#if defined(__APPLE__)
    dispatch_async(dispatch_get_main_queue(), ^{
        if (@available(macOS 10.11, *)) {
            [[NSHapticFeedbackManager defaultPerformer]
                performFeedbackPattern:NSHapticFeedbackPatternAlignment
                performanceTime:NSHapticFeedbackPerformanceTimeNow];
        }
    });
#endif
}

} // namespace spectr
