#include "spectr/mac_haptics.hpp"

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#endif

namespace spectr {

void mac_haptic_alignment_tick() noexcept {
#if defined(__APPLE__)
    void (^performTick)(void) = ^{
        if (@available(macOS 10.11, *)) {
            [[NSHapticFeedbackManager defaultPerformer]
                performFeedbackPattern:NSHapticFeedbackPatternAlignment
                performanceTime:NSHapticFeedbackPerformanceTimeNow];
        }
    };
    // Range input normally arrives on the editor's main thread. Execute there
    // immediately so the alignment tick stays coupled to the discrete visual
    // transition; retain an async hop for bridge callers on worker threads.
    if ([NSThread isMainThread]) performTick();
    else dispatch_async(dispatch_get_main_queue(), performTick);
#endif
}

} // namespace spectr
