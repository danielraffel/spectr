// Editor-open timeline through the REAL Audio Unit v2 Cocoa view path.
//
// Logic opens an AU v2 editor by asking the unit for kAudioUnitProperty_CocoaUI,
// instantiating the named view-factory class from the component bundle and
// calling -uiViewForAudioUnit:withSize: on its main thread. Whatever that call
// does synchronously is time the host spends showing an empty plug-in window
// (Logic: its header strip, no content). This probe drives exactly that path
// against a built .component, hosted in-process without installing it, and
// timestamps every stage up to the editor being idle and interactive:
//
//   load       bundle loaded + component registered
//   instance   AudioComponentInstanceNew + AudioUnitInitialize
//   factory    -uiViewForAudioUnit:withSize: (the blocking call) and the frame
//              of the NSView it returns (the size the host first sees)
//   attached   view inserted in a window and the window ordered in
//   drawable   first -[CAMetalLayer nextDrawable]
//   present    first Metal drawable present (first frame on screen)
//   idle       the main thread answers a 2 ms heartbeat within 34 ms for
//              250 ms straight after the first present -- the editor can
//              take input without a hitch
//
// The probe never activates or takes focus: it runs with the Accessory
// activation policy (no Dock icon, no menu bar), never calls -activate, and
// orders a borderless window in with -orderFrontRegardless at a position the
// caller chooses (default: off every screen). No audio device is opened.
//
// Each run opens the editor --opens N times in the same process: open 1 is
// cold (first view of a fresh instance), the rest are warm (same instance,
// editor closed and reopened, as when a user toggles the plug-in window).
//
// Usage:
//   Spectr-editor-open-probe --bundle path/to/X.component [--opens 3]
//       [--json out.json] [--onscreen] [--settle-ms 1500] [--min-width W
//        --min-height H]   (fail unless the factory returns >= WxH)
//       [--max-factory-ms M] (fail when any factory call exceeds M ms)
//       [--max-warm-factory-ms M] (the same, for opens 2..N only: the first
//        open of a freshly built binary also pays one-time dyld/Metal costs)
// Exit: 0 ok, 1 a gate failed, 2 setup error, 4 no window server (skip).

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
Clock::time_point g_origin;

double now_ms() {
    return std::chrono::duration<double, std::milli>(Clock::now() - g_origin).count();
}

// Per-open marks written by the swizzled Metal entry points.
double g_first_drawable = -1.0;
double g_first_present = -1.0;
long g_presents = 0;
std::vector<double> g_present_times;

IMP g_orig_next_drawable = nullptr;
id probe_next_drawable(id self, SEL cmd) {
    if (g_first_drawable < 0.0) g_first_drawable = now_ms();
    return reinterpret_cast<id (*)(id, SEL)>(g_orig_next_drawable)(self, cmd);
}

void note_present() {
    ++g_presents;
    if (g_present_times.size() < 4096) g_present_times.push_back(now_ms());
    if (g_first_present < 0.0) g_first_present = now_ms();
}

IMP g_orig_present = nullptr;
void probe_present(id self, SEL cmd) {
    note_present();
    reinterpret_cast<void (*)(id, SEL)>(g_orig_present)(self, cmd);
}
IMP g_orig_cb_present = nullptr;
void probe_cb_present(id self, SEL cmd, id drawable) {
    note_present();
    reinterpret_cast<void (*)(id, SEL, id)>(g_orig_cb_present)(self, cmd, drawable);
}

void swizzle(Class cls, SEL sel, IMP replacement, IMP* original) {
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) return;
    *original = method_setImplementation(m, replacement);
}

// The drawable and command-buffer classes are private to the Metal driver, so
// discover them from live objects rather than naming them.
void install_metal_probes() {
    swizzle([CAMetalLayer class], @selector(nextDrawable),
            reinterpret_cast<IMP>(probe_next_drawable), &g_orig_next_drawable);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return;
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.drawableSize = CGSizeMake(4, 4);
    id<CAMetalDrawable> drawable =
        reinterpret_cast<id (*)(id, SEL)>(g_orig_next_drawable)(layer, @selector(nextDrawable));
    if (drawable)
        swizzle(object_getClass(drawable), @selector(present),
                reinterpret_cast<IMP>(probe_present), &g_orig_present);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (cb)
        swizzle(object_getClass(cb), @selector(presentDrawable:),
                reinterpret_cast<IMP>(probe_cb_present), &g_orig_cb_present);
    [queue release];
}

OSType four_cc(CFDictionaryRef entry, const char* key) {
    auto cf_key = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
    auto value = static_cast<CFStringRef>(CFDictionaryGetValue(entry, cf_key));
    CFRelease(cf_key);
    char text[8] = {};
    if (!value || !CFStringGetCString(value, text, sizeof(text), kCFStringEncodingMacRoman))
        return 0;
    return OSType(std::uint8_t(text[0])) << 24 | OSType(std::uint8_t(text[1])) << 16
         | OSType(std::uint8_t(text[2])) << 8 | OSType(std::uint8_t(text[3]));
}

AudioComponent register_bundle(const std::string& path) {
    auto url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), true);
    CFBundleRef bundle = url ? CFBundleCreate(nullptr, url) : nullptr;
    if (url) CFRelease(url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) {
        std::fprintf(stderr, "cannot load bundle %s\n", path.c_str());
        return nullptr;
    }
    auto list = static_cast<CFArrayRef>(
        CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents")));
    if (!list || CFArrayGetCount(list) < 1) return nullptr;
    auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, 0));
    AudioComponentDescription desc{};
    desc.componentType = four_cc(entry, "type");
    desc.componentSubType = four_cc(entry, "subtype");
    desc.componentManufacturer = four_cc(entry, "manufacturer");
    auto factory_name = static_cast<CFStringRef>(CFDictionaryGetValue(entry, CFSTR("factoryFunction")));
    auto factory = factory_name
        ? reinterpret_cast<AudioComponentFactoryFunction>(
              CFBundleGetFunctionPointerForName(bundle, factory_name))
        : nullptr;
    if (!factory) return nullptr;
    return AudioComponentRegister(&desc, CFSTR("Pulp: editor-open probe (in-process)"), 1, factory);
}

void spin(double ms) {
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:ms / 1000.0]];
}

struct OpenResult {
    double factory_begin = 0, factory_end = 0;
    double view_w = 0, view_h = 0;
    double attached = 0, first_drawable = -1, first_present = -1, idle = -1;
    double max_stall_ms = 0;
    double last_stall_end = -1;   // end of the last main-thread stall > 100 ms
    double content_present = -1;  // first present after that stall
    long presents = 0;
    std::vector<std::pair<double, std::string>> frames;  // t, "WxH"
};

}  // namespace

@interface ProbeFrameObserver : NSObject
@property(nonatomic, assign) OpenResult* result;
@end
@implementation ProbeFrameObserver
- (void)frameChanged:(NSNotification*)note {
    NSView* view = note.object;
    if (_result)
        _result->frames.emplace_back(
            now_ms(), std::to_string(int(view.frame.size.width)) + "x"
                          + std::to_string(int(view.frame.size.height)));
}
@end

int main(int argc, char** argv) {
    g_origin = Clock::now();
    std::string bundle_path, json_path;
    int opens = 3;
    bool onscreen = false;
    double settle_ms = 1500.0, min_w = 0, min_h = 0, max_factory_ms = 0;
    double max_warm_factory_ms = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--bundle") bundle_path = next();
        else if (a == "--opens") opens = std::atoi(next());
        else if (a == "--json") json_path = next();
        else if (a == "--onscreen") onscreen = true;
        else if (a == "--settle-ms") settle_ms = std::atof(next());
        else if (a == "--min-width") min_w = std::atof(next());
        else if (a == "--min-height") min_h = std::atof(next());
        else if (a == "--max-factory-ms") max_factory_ms = std::atof(next());
        else if (a == "--max-warm-factory-ms") max_warm_factory_ms = std::atof(next());
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (bundle_path.empty()) { std::fprintf(stderr, "--bundle is required\n"); return 2; }
    setenv("PULP_AUDIO_DEVICE", "null", 0);
    if (CFDictionaryRef session = CGSessionCopyCurrentDictionary()) {
        CFRelease(session);
    } else {
        std::printf("SKIP: no window server session\n");
        return 4;
    }

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        install_metal_probes();

        const double t_load0 = now_ms();
        AudioComponent comp = register_bundle(bundle_path);
        if (!comp) return 2;
        const double t_load1 = now_ms();
        AudioUnit au = nullptr;
        if (AudioComponentInstanceNew(comp, &au) != noErr || !au) {
            std::fprintf(stderr, "AudioComponentInstanceNew failed\n");
            return 2;
        }
        const double t_new = now_ms();
        AudioUnitInitialize(au);
        const double t_init = now_ms();

        UInt32 size = 0;
        Boolean writable = false;
        if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0,
                                     &size, &writable) != noErr || size == 0) {
            std::fprintf(stderr, "no kAudioUnitProperty_CocoaUI\n");
            return 2;
        }
        std::vector<std::uint8_t> raw(size);
        auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(raw.data());
        if (AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0,
                                 info, &size) != noErr) return 2;
        NSURL* view_bundle_url = (__bridge NSURL*)info->mCocoaAUViewBundleLocation;
        NSString* class_name = (__bridge NSString*)info->mCocoaAUViewClass[0];
        NSBundle* view_bundle = [NSBundle bundleWithURL:view_bundle_url];
        Class factory_class = [view_bundle classNamed:class_name];
        if (!factory_class) factory_class = NSClassFromString(class_name);
        if (!factory_class) { std::fprintf(stderr, "no view factory class\n"); return 2; }
        const double t_cocoa = now_ms();

        std::vector<OpenResult> results(static_cast<std::size_t>(opens));
        ProbeFrameObserver* observer = [[ProbeFrameObserver alloc] init];
        int failures = 0;
        for (int n = 0; n < opens; ++n) {
            OpenResult& r = results[static_cast<std::size_t>(n)];
            g_first_drawable = g_first_present = -1.0;
            g_presents = 0;
            g_present_times.clear();
            observer.result = &r;

            id<AUCocoaUIBase> factory = [[factory_class alloc] init];
            r.factory_begin = now_ms();
            NSView* view = [factory uiViewForAudioUnit:au withSize:NSMakeSize(0, 0)];
            r.factory_end = now_ms();
            [(id)factory release];
            if (!view) { std::fprintf(stderr, "factory returned nil\n"); return 2; }
            r.view_w = view.frame.size.width;
            r.view_h = view.frame.size.height;
            view.postsFrameChangedNotifications = YES;
            [[NSNotificationCenter defaultCenter] addObserver:observer
                                                     selector:@selector(frameChanged:)
                                                         name:NSViewFrameDidChangeNotification
                                                       object:view];

            // Off every screen unless asked: a borderless window may sit
            // outside the screen union, and the probe never takes focus.
            const NSRect frame = onscreen
                ? NSMakeRect(40, 40, r.view_w, r.view_h)
                : NSMakeRect(-30000, -30000, r.view_w, r.view_h);
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:NSWindowStyleMaskBorderless
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
            window.releasedWhenClosed = NO;
            [window setContentView:[[[NSView alloc] initWithFrame:NSMakeRect(0, 0, r.view_w, r.view_h)] autorelease]];
            [window.contentView addSubview:view];
            [window orderFrontRegardless];
            r.attached = now_ms();

            // Idle detector: a 2 ms heartbeat on the main queue. The editor is
            // "interactive" once a first frame is on screen and no heartbeat
            // has been late by more than 34 ms for 250 ms straight.
            double last_beat = now_ms();
            double quiet_since = -1.0;
            const double deadline = r.attached + 15000.0;
            while (now_ms() < deadline) {
                spin(2.0);
                const double t = now_ms();
                const double gap = t - last_beat;
                last_beat = t;
                if (gap > 100.0) r.last_stall_end = t;
                if (g_first_present >= 0.0) {
                    if (gap > r.max_stall_ms && t > g_first_present) r.max_stall_ms = gap;
                    if (gap > 34.0) quiet_since = t;
                    else if (quiet_since < 0.0) quiet_since = t;
                    if (r.idle < 0.0 && t - quiet_since >= 250.0) r.idle = quiet_since;
                } else if (gap > r.max_stall_ms) {
                    r.max_stall_ms = gap;
                }
                if (r.idle >= 0.0 && t - r.attached >= settle_ms) break;
            }
            r.first_drawable = g_first_drawable;
            // "Content" = the first frame presented after the last long
            // main-thread stall: with a deferred document load the first
            // present is the empty editor and the document lands after the
            // stall that evaluates it. Without a stall it is the first frame.
            for (double t : g_present_times) {
                if (t >= r.last_stall_end) { r.content_present = t; break; }
            }
            r.first_present = g_first_present;
            r.presents = g_presents;

            [[NSNotificationCenter defaultCenter] removeObserver:observer];
            observer.result = nullptr;
            [view removeFromSuperview];
            [window orderOut:nil];
            [window close];
            [window release];
            spin(300.0);  // let the view dealloc and the editor close

            const auto rel = [&](double t) { return t < 0.0 ? -1.0 : t - r.factory_begin; };
            std::printf("open %d (%s): factory %.1f ms -> view %.0fx%.0f | drawable +%.1f "
                        "present +%.1f content +%.1f idle +%.1f ms | presents %ld | "
                        "worst main stall %.1f ms\n",
                        n + 1, n == 0 ? "cold" : "warm", r.factory_end - r.factory_begin,
                        r.view_w, r.view_h, rel(r.first_drawable), rel(r.first_present),
                        rel(r.content_present), rel(r.idle), r.presents, r.max_stall_ms);
            for (auto& [t, s] : r.frames)
                std::printf("    frame change +%.1f ms -> %s\n", t - r.factory_begin, s.c_str());
            if (min_w > 0 && (r.view_w < min_w || r.view_h < min_h)) {
                std::printf("FAIL: view %.0fx%.0f is smaller than %.0fx%.0f at creation\n",
                            r.view_w, r.view_h, min_w, min_h);
                ++failures;
            }
            if (max_factory_ms > 0 && r.factory_end - r.factory_begin > max_factory_ms) {
                std::printf("FAIL: factory %.1f ms exceeds %.1f ms\n",
                            r.factory_end - r.factory_begin, max_factory_ms);
                ++failures;
            }
            if (n > 0 && max_warm_factory_ms > 0
                && r.factory_end - r.factory_begin > max_warm_factory_ms) {
                std::printf("FAIL: warm factory %.1f ms exceeds %.1f ms\n",
                            r.factory_end - r.factory_begin, max_warm_factory_ms);
                ++failures;
            }
            if (r.first_present < 0.0) {
                std::printf("FAIL: no frame was presented\n");
                ++failures;
            }
        }
        std::printf("setup: load %.1f ms, InstanceNew %.1f ms, Initialize %.1f ms, CocoaUI %.1f ms\n",
                    t_load1 - t_load0, t_new - t_load1, t_init - t_new, t_cocoa - t_init);

        if (!json_path.empty()) {
            FILE* f = std::fopen(json_path.c_str(), "w");
            if (f) {
                std::fprintf(f, "{\"load_ms\":%.3f,\"instance_new_ms\":%.3f,\"initialize_ms\":%.3f,"
                                "\"opens\":[", t_load1 - t_load0, t_new - t_load1, t_init - t_new);
                for (std::size_t i = 0; i < results.size(); ++i) {
                    const auto& r = results[i];
                    const auto rel = [&](double t) { return t < 0.0 ? -1.0 : t - r.factory_begin; };
                    std::fprintf(f, "%s{\"factory_ms\":%.3f,\"view_w\":%.0f,\"view_h\":%.0f,"
                                    "\"first_drawable_ms\":%.3f,\"first_present_ms\":%.3f,"
                                    "\"content_present_ms\":%.3f,\"idle_ms\":%.3f,"
                                    "\"presents\":%ld,\"max_stall_ms\":%.3f}",
                                 i ? "," : "", r.factory_end - r.factory_begin, r.view_w, r.view_h,
                                 rel(r.first_drawable), rel(r.first_present),
                                 rel(r.content_present), rel(r.idle), r.presents, r.max_stall_ms);
                }
                std::fprintf(f, "]}\n");
                std::fclose(f);
            }
        }
        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
        [observer release];
        return failures ? 1 : 0;
    }
}
