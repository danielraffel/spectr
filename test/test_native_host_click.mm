// Clicks delivered the way a DAW delivers them: through the real Pulp plug-in
// host NSView, AppKit's own hit testing, and NSWindow event dispatch.
//
// The n1 rig (test_native_state_parity.cpp) proves the view tree answers a
// press anywhere in a control's painted box. It cannot see anything AppKit
// decides before the tree is asked, and that is where the reported "the A / B
// snapshot buttons only work on part of the button" in Logic came from: a press
// that lands while the editor's window is not key only made the window key and
// never reached the control. The user's retry landed elsewhere on the button
// and worked, which reads as a dead region. Measured before the fix, with the
// editor window not key: first press dispatched 0 clicks, the next one 1.

#import <AppKit/AppKit.h>

#include "spectr/spectr.hpp"

#include <catch2/catch_test_macros.hpp>
#include <pulp/format/editor_idle_pump.hpp>
#include <pulp/format/plugin_descriptor.hpp>
#include <pulp/format/view_bridge.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/plugin_view_host.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widget_bridge.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

using pulp::view::View;

namespace {

void pump(double seconds) {
    NSDate* until = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([until timeIntervalSinceNow] > 0) {
        @autoreleasepool {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) [NSApp sendEvent:event];
        }
    }
}

const View* find_id(const View& view, std::string_view id) {
    if (view.id() == id) return &view;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        if (const auto* match = find_id(*view.child_at(index), id)) return match;
    return nullptr;
}

pulp::view::Point root_origin(const View& view) {
    float x = 0.0f, y = 0.0f;
    for (const auto* node = &view; node != nullptr; node = node->parent()) {
        x += node->bounds().x;
        y += node->bounds().y;
    }
    return {x, y};
}

// The AU v2 editor exactly as Logic gets it: ViewBridge + PluginViewHost at the
// preferred size, the design viewport pinned and top-aligned, parented into a
// host-owned window. The window is transparent so the test paints nothing on
// the desktop; AppKit still routes events to it.
struct HostedEditor {
    pulp::state::StateStore store;
    spectr::Spectr processor;
    std::unique_ptr<pulp::format::ViewBridge> bridge;
    std::unique_ptr<pulp::view::PluginViewHost> host;
    NSWindow* window = nil;
    NSView* view = nil;
    static constexpr uint32_t kWidth = 990, kHeight = 645;

    explicit HostedEditor(bool use_gpu) {
        processor.set_state_store(&store);
        processor.define_parameters(store);
        pulp::format::PrepareContext prepare;
        prepare.sample_rate = 48000.0;
        prepare.max_buffer_size = 256;
        prepare.input_channels = 2;
        prepare.output_channels = 2;
        processor.prepare(prepare);
        bridge = std::make_unique<pulp::format::ViewBridge>(
            processor, store, pulp::format::ViewBridge::Options::hosted_editor());
        std::string error;
        REQUIRE(bridge->open(&error));
        pulp::view::PluginViewHost::Options options;
        options.size = {kWidth, kHeight};
        options.use_gpu = use_gpu;
        host = pulp::view::PluginViewHost::create(*bridge->view(), options);
        REQUIRE(host != nullptr);
        const auto& hints = bridge->size_hints();
        pulp::format::configure_native_viewport(*host, hints);
        if (pulp::format::should_pin_design_viewport(hints))
            host->set_design_viewport_top_align(true);
        host->set_idle_callback(pulp::format::make_editor_idle_pump(*bridge));
        window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, kWidth, kHeight)
                                             styleMask:NSWindowStyleMaskTitled
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        window.alphaValue = 0.0;
        window.releasedWhenClosed = NO;
        host->attach_to_parent((__bridge void*)window.contentView);
        view = (__bridge NSView*)host->native_handle();
        REQUIRE(view != nil);
        [window orderFront:nil];
        pump(1.5);
        bridge->scripted_ui()->bridge()->load_script(R"js((() => {
          globalThis.__hostClicks = 0;
          const inner = globalThis.__dispatch__;
          if (typeof inner !== 'function') throw new Error('no __dispatch__');
          globalThis.__dispatch__ = function (id, event, payload) {
            if (event === 'click') globalThis.__hostClicks++;
            return inner.call(this, id, event, payload);
          };
        })();)js", "spectr-host-click-counter");
    }

    // Leak deliberately: the host and bridge own a live display link and GPU
    // surfaces that a process-exit teardown order in a test binary does not
    // need to prove anything about. The window is ordered out so nothing is
    // left on screen.
    ~HostedEditor() {
        [window orderOut:nil];
        host.release();
        bridge.release();
    }

    int clicks() {
        try {
            bridge->scripted_ui()->bridge()->load_script(
                "throw new Error('HOSTCLICKS:' + globalThis.__hostClicks);",
                "spectr-host-click-read");
        } catch (const std::exception& error) {
            const std::string message = error.what();
            const auto marker = message.find("HOSTCLICKS:");
            if (marker != std::string::npos)
                return std::atoi(message.c_str() + marker + 11);
        }
        FAIL("host click counter was not readable");
        return -1;
    }

    // Root (design) point -> window point, through the host's own forward
    // design-viewport transform and AppKit's unflipped view space.
    NSPoint window_point(pulp::view::Point root_pt) {
        float sx = 1.0f, sy = 1.0f, tx = 0.0f, ty = 0.0f;
        host->design_viewport_transform(sx, sy, tx, ty);
        const NSPoint local = NSMakePoint(root_pt.x * sx + tx,
                                          view.bounds.size.height - (root_pt.y * sy + ty));
        return [view convertPoint:local toView:nil];
    }

    // One press + release through NSWindow, which is where AppKit decides
    // whether a press in a non-key window reaches the view at all.
    void click(NSPoint at) {
        for (NSEventType type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
            NSEvent* event = [NSEvent mouseEventWithType:type
                                                location:at
                                           modifierFlags:0
                                               timestamp:0
                                            windowNumber:window.windowNumber
                                                 context:nil
                                             eventNumber:0
                                              clickCount:1
                                                pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
            [window sendEvent:event];
            pump(0.04);
        }
        pump(0.3);
    }
};

// Another window of the same app takes key, the way Logic's arrange window
// does whenever the user works outside the plug-in.
NSWindow* make_other_key_window() {
    NSWindow* other = [[NSWindow alloc] initWithContentRect:NSMakeRect(1200, 40, 120, 80)
                                                  styleMask:NSWindowStyleMaskTitled
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    other.alphaValue = 0.0;
    other.releasedWhenClosed = NO;
    [other makeKeyAndOrderFront:nil];
    pump(0.3);
    return other;
}

bool window_server_available() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp activateIgnoringOtherApps:YES];
    return NSScreen.mainScreen != nil;
}

}  // namespace

TEST_CASE("a press in a non-key editor window acts on the control under it",
          "[native-host][first-mouse]") {
    if (!window_server_available()) SKIP("no window server: AppKit event routing is unmeasurable");
    for (const bool use_gpu : {true, false}) {
        INFO((use_gpu ? "GPU" : "CPU") << " plug-in host view");
        HostedEditor editor(use_gpu);
        CHECK([editor.view acceptsFirstMouse:nil]);
        NSWindow* other = make_other_key_window();

        for (const char* id : {"spectr-snapshot-capture-a", "spectr-snapshot-capture-b"}) {
            INFO("control " << id);
            const auto* control = find_id(*editor.bridge->view(), id);
            REQUIRE(control != nullptr);
            const auto origin = root_origin(*control);
            const auto size = control->bounds();
            // The bottom band the report named, and the centre.
            for (const float fy : {0.9f, 0.5f}) {
                [other makeKeyAndOrderFront:nil];
                pump(0.2);
                // Control: the editor window really is not key, so this press
                // is exactly the one AppKit gates on -acceptsFirstMouse:.
                REQUIRE(NSApp.keyWindow != editor.window);
                const auto before = editor.clicks();
                editor.click(editor.window_point(
                    {origin.x + size.width * 0.5f, origin.y + size.height * fy}));
                CHECK(editor.clicks() == before + 1);
            }
        }
        [other orderOut:nil];
    }
}

TEST_CASE("the snapshot buttons take a click anywhere in their painted box through the host view",
          "[native-host][tap-targets]") {
    if (!window_server_available()) SKIP("no window server: AppKit event routing is unmeasurable");
    HostedEditor editor(/*use_gpu=*/true);
    [editor.window makeKeyAndOrderFront:nil];
    pump(0.2);
    for (const char* id : {"spectr-snapshot-capture-a", "spectr-snapshot-capture-b",
                           "spectr-snapshot-recall-a", "spectr-snapshot-recall-b"}) {
        INFO("control " << id);
        const auto* control = find_id(*editor.bridge->view(), id);
        REQUIRE(control != nullptr);
        // An empty RECALL slot refuses a press by design. The CAPTURE sweeps
        // above it in this list fill both slots, so every RECALL is live here.
        REQUIRE(control->enabled());
        const auto origin = root_origin(*control);
        const auto size = control->bounds();
        // A 7x5 grid over the painted box, half a design pixel in from every
        // edge so each sample is unambiguously inside what the user sees.
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 7; ++col) {
                const float x = origin.x + 0.5f + (size.width - 1.0f) * col / 6.0f;
                const float y = origin.y + 0.5f + (size.height - 1.0f) * row / 4.0f;
                CAPTURE(x, y);
                const auto before = editor.clicks();
                editor.click(editor.window_point({x, y}));
                CHECK(editor.clicks() == before + 1);
            }
        }
    }
}
