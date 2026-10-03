// Out-of-process AU editor open, the way Logic does it: the unit is
// instantiated with kAudioComponentInstantiation_LoadOutOfProcess (it runs in
// AUHostingService), and the editor comes back as a view controller whose view
// is a remote view the window server composites from the service process.
// Never activates; Accessory policy; borderless window off every screen.
//
// The plug-in process cannot be instrumented from here, so every distinct
// image of the host's own window is read back (a process may read its own
// windows without screen-recording permission) and classified: host-empty
// (the magenta host backdrop: nothing composited yet), background, settling
// (background plus chrome already where it settles), edge-lag (pixels within
// 12 px of an edge while the remote content and the host converge on one
// size), resize-lag (the editor's previous-size frame, cropped), ui, navy
// (the SDK's former default) and other. --gate fails on navy or other.
//
// The component must be discoverable by AUHostingService, i.e. installed. Use
// a development identity (SPECTR_DEV_IDENTITY / SPECTR_DEV_AU_SUBTYPE) in the
// user's ~/Library/Audio/Plug-Ins/Components and remove it afterwards.
// Reopening the same remote instance's editor never paints here, so
// --fresh-instance instantiates a new unit per open.
//
// Usage: Spectr-editor-open-oop-probe --sub SpNs [--mfr Pulp] [--opens 3]
//          [--fresh-instance] [--view-size WxH] [--watch-ms 3500]
//          [--settled-bg 05070A] [--gate] [--out DIR]
#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudioKit/CoreAudioKit.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>
#import <ImageIO/ImageIO.h>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static auto g0 = std::chrono::steady_clock::now();
static double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0).count(); }
static void spin(double ms) { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:ms / 1000.0]]; }
static OSType fcc(const char* s) { return OSType(s[0]) << 24 | OSType(s[1]) << 16 | OSType(s[2]) << 8 | OSType(s[3]); }

static long layer_rgb(CALayer* l) {
    if (!l || !l.backgroundColor) return -1;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef c = CGColorCreateCopyByMatchingToColorSpace(cs, kCGRenderingIntentDefault, l.backgroundColor, nullptr);
    CGColorSpaceRelease(cs);
    if (!c) return -1;
    const CGFloat* k = CGColorGetComponents(c);
    long v = (long(k[0] * 255 + .5) << 16) | (long(k[1] * 255 + .5) << 8) | long(k[2] * 255 + .5);
    CGColorRelease(c);
    return v;
}
static void dump_layers(CALayer* l, int depth, std::string& out) {
    if (!l || depth > 8) return;
    char b[256];
    snprintf(b, sizeof b, "%*s%s bg=%06lX opaque=%d %.0fx%.0f\n", depth * 2, "", object_getClassName(l), layer_rgb(l),
             l.opaque, l.bounds.size.width, l.bounds.size.height);
    out += b;
    for (CALayer* s in l.sublayers) dump_layers(s, depth + 1, out);
}

// Own-window read-back through the window-list API (deprecated, looked up at
// run time). A process may read its own windows without screen-recording
// permission, and the window server composites the remote editor into ours,
// so this sees what the host's window really shows. Returns an RGB image at
// nominal (1x) resolution.
typedef CGImageRef (*WinImageFn)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
struct Shot { double t; std::vector<uint8_t> rgb; size_t w = 0, h = 0; };
static const uint8_t* at(const Shot& a, const Shot& b, size_t p);
static bool shoot(NSWindow* win, Shot& s) {
    static WinImageFn fn = (WinImageFn)dlsym(RTLD_DEFAULT, "CGWindowListCreateImage");
    if (!fn) return false;
    CGImageRef img = fn(CGRectNull, kCGWindowListOptionIncludingWindow, (CGWindowID)win.windowNumber,
                        kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);
    if (!img) return false;
    s.w = CGImageGetWidth(img); s.h = CGImageGetHeight(img);
    std::vector<uint8_t> px(s.w * s.h * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px.data(), s.w, s.h, 8, s.w * 4, cs, kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(ctx, CGRectMake(0, 0, s.w, s.h), img);
    CGContextRelease(ctx); CGColorSpaceRelease(cs); CGImageRelease(img);
    s.rgb.resize(s.w * s.h * 3);
    for (size_t i = 0; i < s.w * s.h; ++i) { s.rgb[i*3] = px[i*4]; s.rgb[i*3+1] = px[i*4+1]; s.rgb[i*3+2] = px[i*4+2]; }
    return true;
}
static bool near(const uint8_t* p, uint32_t c, int t) {
    return abs(int(p[0]) - int(c >> 16)) <= t && abs(int(p[1]) - int((c >> 8) & 255)) <= t && abs(int(p[2]) - int(c & 255)) <= t;
}
// Pixel of `b` at the position of pixel p of `a` (images can differ by a few
// pixels while the remote view settles its size).
static const uint8_t* at(const Shot& a, const Shot& b, size_t p) {
    size_t x = (p % a.w) * b.w / a.w, y = (p / a.w) * b.h / a.h;
    return &b.rgb[(y * b.w + x) * 3];
}
static bool near2(const uint8_t* p, const uint8_t* q, int t) {
    return abs(p[0]-q[0]) <= t && abs(p[1]-q[1]) <= t && abs(p[2]-q[2]) <= t;
}
static bool same(const Shot& a, const Shot& b) { return a.rgb == b.rgb; }
// Is pixel p of `s` already what the final image shows? The remote view can
// settle its size by a few pixels after it first appears, which moves chrome
// anchored to an edge (the resize grip) relative to the final frame, so look
// within the size difference around both the proportional and the
// bottom-right-anchored position.
static bool matches_final(const Shot& s, const Shot& last, size_t p) {
    const long x = long(p % s.w), y = long(p / s.w);
    const long dw = labs(long(last.w) - long(s.w)) + 1, dh = labs(long(last.h) - long(s.h)) + 1;
    const long ax[2] = {x * long(last.w) / long(s.w), x + long(last.w) - long(s.w)};
    const long ay[2] = {y * long(last.h) / long(s.h), y + long(last.h) - long(s.h)};
    const uint8_t* px = &s.rgb[p * 3];
    for (int k = 0; k < 2; ++k)
        for (long yy = ay[k] - dh; yy <= ay[k] + dh; ++yy)
            for (long xx = ax[k] - dw; xx <= ax[k] + dw; ++xx) {
                if (xx < 0 || yy < 0 || xx >= long(last.w) || yy >= long(last.h)) continue;
                if (near2(px, &last.rgb[(size_t(yy) * last.w + size_t(xx)) * 3], 24)) return true;
            }
    return false;
}
static void save_png(const Shot& s, const std::string& path) {
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CFDataRef data = CFDataCreate(nullptr, s.rgb.data(), CFIndex(s.rgb.size()));
    CGDataProviderRef pr = CGDataProviderCreateWithCFData(data);
    CGImageRef img = CGImageCreate(s.w, s.h, 8, 24, s.w * 3, cs, (CGBitmapInfo)kCGImageAlphaNone, pr, nullptr, false, kCGRenderingIntentDefault);
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    if (dst && img) { CGImageDestinationAddImage(dst, img, nullptr); CGImageDestinationFinalize(dst); }
    if (dst) CFRelease(dst); if (img) CGImageRelease(img); CGDataProviderRelease(pr); CFRelease(data); CGColorSpaceRelease(cs);
}

int main(int argc, char** argv) {
    const char* sub = "Spec"; const char* mfr = "Pulp"; int opens = 3; std::string out;
    uint32_t bg = 0x05070A; double watch_ms = 4000; double vw = 0, vh = 0; bool gate = false; bool fresh = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto nx = [&] { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--sub") sub = nx(); else if (a == "--mfr") mfr = nx();
        else if (a == "--opens") opens = atoi(nx()); else if (a == "--out") out = nx();
        else if (a == "--settled-bg") bg = strtoul(nx(), nullptr, 16);
        else if (a == "--watch-ms") watch_ms = atof(nx());
        else if (a == "--view-size") sscanf(nx(), "%lfx%lf", &vw, &vh);
        else if (a == "--gate") gate = true;
        else if (a == "--fresh-instance") fresh = true;
    }
    int failures = 0;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        AudioComponentDescription d{fcc("aufx"), fcc(sub), fcc(mfr), 0, 0};
        __block AUAudioUnit* au = nil; __block NSError* err = nil; __block bool done = false;
        double t0 = now_ms();
        [AUAudioUnit instantiateWithComponentDescription:d options:kAudioComponentInstantiation_LoadOutOfProcess
                                       completionHandler:^(AUAudioUnit* a, NSError* e) { au = [a retain]; err = [e retain]; done = true; }];
        while (!done && now_ms() - t0 < 20000) spin(5);
        if (!au) { printf("instantiate failed: %s\n", err ? err.description.UTF8String : "timeout"); return 2; }
        printf("instantiated %s/%s out of process in %.1f ms (%s)\n", sub, mfr, now_ms() - t0, object_getClassName(au));
        for (int n = 0; n < opens; ++n) {
            if (fresh && n > 0) {
                [au release]; au = nil; done = false;
                [AUAudioUnit instantiateWithComponentDescription:d options:kAudioComponentInstantiation_LoadOutOfProcess
                                               completionHandler:^(AUAudioUnit* a, NSError* e) { au = [a retain]; done = true; }];
                double i0 = now_ms();
                while (!done && now_ms() - i0 < 20000) spin(5);
                if (!au) { printf("re-instantiate failed\n"); return 2; }
            }
            __block NSViewController* vc = nil; __block bool got = false;
            double f0 = now_ms();
            [au requestViewControllerWithCompletionHandler:^(AUViewControllerBase* v) { vc = [v retain]; got = true; }];
            while (!got && now_ms() - f0 < 20000) spin(1);
            double f1 = now_ms();
            if (!vc) { printf("open %d: no view controller\n", n + 1); ++failures; continue; }
            NSView* v = vc.view;
            NSSize sz = v.frame.size; if (sz.width < 10) sz = NSMakeSize(990, 645);
            if (vw > 0) sz = NSMakeSize(vw, vh);
            NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(-30000, -30000, sz.width, sz.height)
                                                      styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
            w.releasedWhenClosed = NO;
            // Magenta host backdrop: any pixel the editor does not cover shows
            // as magenta, so "the editor drew nothing yet" cannot pass as a
            // dark editor background.
            w.backgroundColor = [NSColor colorWithSRGBRed:1 green:0 blue:1 alpha:1];
            NSView* content = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, sz.width, sz.height)] autorelease];
            w.contentView = content;
            v.frame = content.bounds;
            [content addSubview:v];
            [w orderFrontRegardless];
            std::vector<Shot> shots;
            double a = now_ms();
            while (now_ms() - a < watch_ms) {
                Shot s; s.t = now_ms() - f0;
                if (shoot(w, s) && (shots.empty() || !same(shots.back(), s))) shots.push_back(std::move(s));
                spin(4);
            }
            // Classify. ready = first distinct image that matches the last one
            // in >= 98% of pixels (within 24/255).
            const Shot& last = shots.back();
            // ready = first image that already shows the settled editor: of
            // the pixels where the LAST image is not the background, >= 95%
            // match it. Comparing all pixels would call a background-only
            // frame "ready", because a dark editor is mostly background.
            size_t ready = shots.size() - 1;
            for (size_t i = 0; i < shots.size(); ++i) {
                size_t m = 0, fg = 0, N = last.w * last.h;
                for (size_t p = 0; p < N; ++p) {
                    if (near(&last.rgb[p*3], bg, 8)) continue;
                    ++fg; m += near2(&last.rgb[p*3], at(last, shots[i], p), 24);
                }
                if (fg == 0 || m * 20 >= fg * 19) { ready = i; break; }
            }
            int c_bg = 0, c_set = 0, c_ui = 0, c_navy = 0, c_host = 0, c_other = 0, c_edge = 0, c_rs = 0;
            double rs_ms = 0;
            double edge_ms = 0;
            double first_bg = -1, offbrand_ms = 0;
            printf("open %d: view controller %.1f ms -> %s %.0fx%.0f\n", n + 1, f1 - f0, object_getClassName(v), sz.width, sz.height);
            for (size_t i = 0; i < shots.size(); ++i) {
                const Shot& s = shots[i]; size_t N = s.w * s.h, navy = 0, mag = 0, offbg = 0, offboth = 0, edge_black = 0;
                for (size_t p = 0; p < N; ++p) {
                    const uint8_t* px = &s.rgb[p*3];
                    navy += near(px, 0x1E1E2E, 2); mag += near(px, 0xFF00FF, 8);
                    bool b = near(px, bg, 8); offbg += !b;
                    if (!b && !matches_final(s, last, p)) {
                        ++offboth;
                        // Pure black within 12 px of an edge: the host's remote
                        // view showing past the editor while the two sizes
                        // converge (host-owned, not a colour the editor drew).
                        const size_t x = p % s.w, y = p / s.w;
                        // Any off pixel within 12 px of an edge: either the
                        // host's remote view showing past the editor (black)
                        // or edge-anchored chrome (the resize grip) a pixel or
                        // two from where it settles, while the remote content
                        // and the host converge on one size.
                        if (x < 12 || y < 12 || x + 12 >= s.w || y + 12 >= s.h) ++edge_black;
                    }
                }
                const char* cls;
                if (mag * 2 >= N) cls = "host-empty";            // nothing composited yet
                else if (navy * 4 >= N) cls = "navy";
                else if (i >= ready) cls = "ui";
                else if (offbg == 0) cls = "background";
                else if (offboth == 0) cls = "settling";
                else if (offboth == edge_black) cls = "edge-lag";
                // The remote editor has not yet taken the host's size: the
                // window shows the editor's previous-size frame cropped (and
                // the host backdrop beside it). Converges once the plug-in
                // process's main thread handles the resize.
                else if (labs(long(s.w) - long(last.w)) > 2 || labs(long(s.h) - long(last.h)) > 2 ||
                         mag * 200 > N) cls = "resize-lag";
                else cls = "other";
                double until = i + 1 < shots.size() ? shots[i+1].t : s.t;
                if (!strcmp(cls, "host-empty")) ++c_host; else if (!strcmp(cls, "navy")) ++c_navy;
                else if (!strcmp(cls, "ui")) ++c_ui; else if (!strcmp(cls, "background")) ++c_bg;
                else if (!strcmp(cls, "settling")) ++c_set;
                else if (!strcmp(cls, "edge-lag")) { ++c_edge; edge_ms += until - s.t; }
                else if (!strcmp(cls, "resize-lag")) { ++c_rs; rs_ms += until - s.t; }
                else ++c_other;
                if ((!strcmp(cls, "background") || !strcmp(cls, "settling") || !strcmp(cls, "edge-lag")) && first_bg < 0) first_bg = s.t;
                if (!strcmp(cls, "navy") || !strcmp(cls, "other")) offbrand_ms += until - s.t;
                const uint8_t* ctr = &s.rgb[((s.h/2) * s.w + s.w/2) * 3];
                printf("  +%7.1f ms  %-10s centre #%02X%02X%02X  navy %4.1f%%  host-backdrop %4.1f%%\n", s.t, cls, ctr[0], ctr[1], ctr[2],
                       100.0 * navy / N, 100.0 * mag / N);
                if (!out.empty()) { char nm[512]; snprintf(nm, sizeof nm, "%s/open%d-%03zu-%07.1fms-%s.png", out.c_str(), n + 1, i, s.t, cls); save_png(s, nm); }
            }
            const double ready_t = shots[ready].t;
            const double empty_t = first_bg >= 0 ? ready_t - first_bg : 0;
            printf("  classes: host-empty %d, background %d, settling %d, edge-lag %d (%.1f ms), resize-lag %d (%.1f ms), ui %d, navy %d, other %d | look-ready +%.1f ms | on-brand-but-empty %.1f ms | off-brand on screen %.1f ms\n",
                   c_host, c_bg, c_set, c_edge, edge_ms, c_rs, rs_ms, c_ui, c_navy, c_other, ready_t, empty_t, offbrand_ms);
            if (gate && (c_navy + c_other) > 0) { printf("FAIL: %d off-brand frame(s)\n", c_navy + c_other); ++failures; }
            [v removeFromSuperview]; [w orderOut:nil]; [w close]; [w release]; [vc release];
            spin(400);
        }
    }
    return failures ? 1 : 0;
}
