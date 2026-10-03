// SDK shim -- delete on the Pulp SDK bump that defines
// PULP_FORMAT_HAS_EDITOR_BACKGROUND (Processor::editor_background()).
//
// Before a plug-in editor's first Metal frame lands, the window server shows
// the host view's backing layer. The SDK this build links seeds that layer with
// its own default navy, so a DAW opening Spectr showed navy until the first
// frame. Recolour it to Spectr's background as soon as the editor is attached;
// the AU v2 entry point attaches before it hands the view to the host, so the
// DAW never composites the old colour.

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cstdint>

namespace spectr {

void apply_host_view_background(void* native_view, std::uint32_t rgb) {
    if (native_view == nullptr) return;
    NSView* view = (__bridge NSView*)native_view;
    if (![view isKindOfClass:[NSView class]] || view.layer == nil) return;
    const CGFloat components[4] = {((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0,
                                   (rgb & 0xff) / 255.0, 1.0};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef color = CGColorCreate(space, components);
    CGColorSpaceRelease(space);
    view.layer.backgroundColor = color;
    CGColorRelease(color);
}

}  // namespace spectr
