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
//       [--pixels] read back a grid of pixels from every presented drawable
//        (GPU readback, never a screenshot) and report what each frame showed
//       [--capture-frames DIR] (implies --pixels) keep EVERY presented frame
//        (half resolution), classify it pixel by pixel, and write the open
//        sequence to DIR as openN-<index>-<ms>-<class>.png + openN-frames.json
//       [--settled-bg RRGGBB] the editor's own background (default: the
//        settled frame's dominant colour)
//       [--max-offbrand-frames N] (with --pixels: fail when more than N
//        presented frames are neither the background -- plus chrome already
//        in its final place -- before the editor reaches its settled look,
//        nor that look after; a stub/placeholder colour fails this)
//       [--expect-first-ui] (with --pixels: fail unless the first presented
//        frame is already the editor's UI -- the content-first contract; the
//        preferred-size frame a host then scales into a smaller view counts)
//       [--view-first] with --expect-first-rgb, also require the first
//        presented frame to be that colour (a view-first open's empty frame)
//       [--expect-first-rgb RRGGBB] (with --pixels: fail unless the backing
//        layer shown before the first frame, and the first presented frame at
//        every sample, are this colour within a small tolerance)
//        On-brand-but-empty (first present -> first frame that looks like the
//        settled editor, when nothing before it was off-brand) is reported
//        per open and in the JSON as onbrand_empty_ms; tracked, not gated.
//       [--view-size WxH] resize the returned view before showing it, as a
//        host does (e.g. the minimum editor size)
// Exit: 0 ok, 1 a gate failed, 2 setup error, 4 no window server (skip).

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <unistd.h>
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

// ── Presented-frame pixel readback (--pixels) ───────────────────────────
//
// Every presented drawable's texture is sampled on a kGridW x kGridH grid of
// cell centres. The copy is a GPU blit into a shared buffer, never a screen
// capture, so it needs no screen-recording permission and sees exactly the
// pixels the plug-in handed the compositor.
//
// Ordering: on the -[MTLCommandBuffer presentDrawable:] path the blit runs
// from that command buffer's completion handler, so it observes the finished
// frame. On the -[CAMetalDrawable present] path (Dawn) the renderer has
// already committed and waited for scheduling; the blit is issued on a
// separate queue and the result is cross-checked by --pixels-verify, which
// re-reads the same texture 20 ms later and counts disagreements.
constexpr int kGridW = 8;
constexpr int kGridH = 6;
constexpr int kSamples = kGridW * kGridH;
struct FrameSample {
    double t = 0;
    // CLOCK_UPTIME_RAW at the present call: the clock a Perfetto trace of the
    // plug-in uses on macOS, so frames join the trace's spans directly.
    std::uint64_t uptime_ns = 0;
    std::uint32_t rgb[kSamples] = {};
    bool ok = false;
    // --capture-frames: the whole frame at half resolution, packed RGB.
    std::shared_ptr<std::vector<std::uint8_t>> image;
    std::uint32_t image_w = 0, image_h = 0;
};
bool g_sample_pixels = false;
long g_settled_bg = -1;
bool g_verify_pixels = false;
long g_verify_mismatch = 0;
std::mutex g_samples_mu;
std::vector<FrameSample> g_samples;
id<MTLCommandQueue> g_readback_queue = nil;
const char* g_present_path = "none";

bool read_grid(id<MTLTexture> tex, std::uint32_t* out) {
    if (!tex || tex.framebufferOnly || tex.width < kGridW || tex.height < kGridH) return false;
    if (!g_readback_queue) g_readback_queue = [tex.device newCommandQueue];
    id<MTLBuffer> buf = [tex.device newBufferWithLength:kSamples * 4
                                                options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_readback_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    for (int gy = 0; gy < kGridH; ++gy) {
        for (int gx = 0; gx < kGridW; ++gx) {
            const NSUInteger x = (2 * gx + 1) * tex.width / (2 * kGridW);
            const NSUInteger y = (2 * gy + 1) * tex.height / (2 * kGridH);
            const NSUInteger i = NSUInteger(gy * kGridW + gx);
            [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0
                     sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(1, 1, 1)
                         toBuffer:buf destinationOffset:i * 4
            destinationBytesPerRow:4 destinationBytesPerImage:4];
        }
    }
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    const bool ok = cb.status == MTLCommandBufferStatusCompleted;
    if (ok) {
        const auto* px = static_cast<const std::uint8_t*>(buf.contents);
        const bool bgra = tex.pixelFormat == MTLPixelFormatBGRA8Unorm
                       || tex.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB;
        for (int i = 0; i < kSamples; ++i) {
            const std::uint8_t* p = px + i * 4;
            const std::uint32_t r = bgra ? p[2] : p[0], g = p[1], b = bgra ? p[0] : p[2];
            out[i] = (r << 16) | (g << 8) | b;
        }
    }
    [buf release];
    return ok;
}

// --capture-frames DIR: keep EVERY presented frame (GPU readback at half
// resolution, never a screenshot) so the open sequence can be written out as an
// image sequence and classified pixel by pixel, not only on the sample grid.
std::string g_capture_dir;

bool read_half_frame(id<MTLTexture> tex, FrameSample& s) {
    const NSUInteger w = tex.width, h = tex.height, row = w * 4;
    id<MTLBuffer> buf = [tex.device newBufferWithLength:row * h
                                                options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_readback_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(w, h, 1) toBuffer:buf destinationOffset:0
      destinationBytesPerRow:row destinationBytesPerImage:row * h];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    const bool ok = cb.status == MTLCommandBufferStatusCompleted;
    if (ok) {
        const bool bgra = tex.pixelFormat == MTLPixelFormatBGRA8Unorm
                       || tex.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB;
        const auto* px = static_cast<const std::uint8_t*>(buf.contents);
        s.image_w = std::uint32_t(w / 2);
        s.image_h = std::uint32_t(h / 2);
        s.image = std::make_shared<std::vector<std::uint8_t>>(
            std::size_t(s.image_w) * s.image_h * 3);
        auto* out = s.image->data();
        for (std::uint32_t y = 0; y < s.image_h; ++y) {
            const std::uint8_t* src = px + std::size_t(y * 2) * row;
            for (std::uint32_t x = 0; x < s.image_w; ++x, out += 3) {
                const std::uint8_t* p = src + std::size_t(x * 2) * 4;
                out[0] = bgra ? p[2] : p[0];
                out[1] = p[1];
                out[2] = bgra ? p[0] : p[2];
            }
        }
    }
    [buf release];
    return ok;
}

bool write_png(const FrameSample& f, const std::string& path) {
    if (!f.image) return false;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CFDataRef data = CFDataCreate(nullptr, f.image->data(), CFIndex(f.image->size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGImageRef img = CGImageCreate(f.image_w, f.image_h, 8, 24, f.image_w * 3, cs,
                                   static_cast<CGBitmapInfo>(kCGImageAlphaNone), provider,
                                   nullptr, false, kCGRenderingIntentDefault);
    bool ok = false;
    if (img) {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
            (__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
        if (dst) {
            CGImageDestinationAddImage(dst, img, nullptr);
            ok = CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
        CGImageRelease(img);
    }
    CGDataProviderRelease(provider);
    CFRelease(data);
    CGColorSpaceRelease(cs);
    return ok;
}

void record_sample(id<MTLTexture> tex, double t, std::uint64_t uptime_ns) {
    FrameSample s;
    s.t = t;
    s.uptime_ns = uptime_ns;
    s.ok = read_grid(tex, s.rgb);
    if (s.ok && !g_capture_dir.empty()) {
        std::size_t held = 0;
        {
            std::lock_guard<std::mutex> lock(g_samples_mu);
            held = g_samples.size();
        }
        if (held < 400) read_half_frame(tex, s);  // past that, keep only the grid
    }
    if (s.ok && g_verify_pixels) {
        std::uint32_t again[kSamples];
        usleep(20000);
        if (read_grid(tex, again))
            for (int i = 0; i < kSamples; ++i)
                if (again[i] != s.rgb[i]) { ++g_verify_mismatch; break; }
    }
    std::lock_guard<std::mutex> lock(g_samples_mu);
    if (g_samples.size() < 4096) g_samples.push_back(s);
}

IMP g_orig_present = nullptr;
void probe_present(id self, SEL cmd) {
    note_present();
    if (g_sample_pixels) {
        g_present_path = "drawable-present";
        record_sample(static_cast<id<CAMetalDrawable>>(self).texture, now_ms(),
                      clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
    }
    reinterpret_cast<void (*)(id, SEL)>(g_orig_present)(self, cmd);
}
IMP g_orig_cb_present = nullptr;
void probe_cb_present(id self, SEL cmd, id drawable) {
    note_present();
    if (g_sample_pixels) {
        g_present_path = "commandbuffer-presentDrawable";
        const double t = now_ms();
        const std::uint64_t uptime_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        id<MTLTexture> tex = [static_cast<id<CAMetalDrawable>>(drawable).texture retain];
        [static_cast<id<MTLCommandBuffer>>(self) addCompletedHandler:^(id<MTLCommandBuffer>) {
            record_sample(tex, t, uptime_ns);
            [tex release];
        }];
    }
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
    // --pixels
    std::vector<FrameSample> samples;
    double look_ready = -1;        // first frame matching the settled look
    std::uint32_t settled_bg = 0;  // dominant colour of the settled look
    long offbrand_frames = 0;      // frames before look_ready not in settled_bg
    double offbrand_visible_ms = 0; // how long an off-brand frame stayed on screen
    // On-brand but empty: from the first presented frame to the first frame
    // that looks like the settled editor, when every frame before it was the
    // editor's own background. Tracked, not gated: it is the time a user stares
    // at the right colour with nothing on it.
    double onbrand_empty_ms = -1;
    long unreadable_frames = 0;
    long layer_rgb = -1;           // backing layer colour when ordered in
    // Per-frame colour class counts (see classify_frame).
    long class_background = 0, class_ui = 0, class_navy = 0, class_other = 0;
    std::vector<const char*> frame_class;
};

// The SDK's former pre-document colour. Named so a frame showing it is
// reported as exactly that defect rather than as "other".
constexpr std::uint32_t kStubNavy = 0x1E1E2E;

bool near_rgb(std::uint32_t a, std::uint32_t b, int tol);

std::uint32_t pixel_at(const std::vector<std::uint8_t>& img, std::size_t i) {
    return (std::uint32_t(img[i * 3]) << 16) | (std::uint32_t(img[i * 3 + 1]) << 8)
         | img[i * 3 + 2];
}

bool near_rgb(std::uint32_t a, std::uint32_t b, int tol) {
    for (int sh = 0; sh <= 16; sh += 8) {
        const int d = int((a >> sh) & 0xff) - int((b >> sh) & 0xff);
        if (d > tol || d < -tol) return false;
    }
    return true;
}

// Most frequent colour on the grid, and how many samples carry it (exact).
std::pair<std::uint32_t, int> dominant(const FrameSample& f) {
    std::map<std::uint32_t, int> counts;
    for (std::uint32_t c : f.rgb) ++counts[c];
    std::pair<std::uint32_t, int> best{0, 0};
    for (auto& [c, n] : counts)
        if (n > best.second) best = {c, n};
    return best;
}

// Mean absolute RGB difference of 16x12 block means, each image sampled at its
// own size: how far a frame is from the settled look regardless of scale. A
// content-first editor presents its document at the preferred size inside the
// host's view-creation call; a host that then resizes the view shows that frame
// scaled until the next present. Under a pinned design viewport that is the
// settled UI at another scale, not an off-brand image.
static double scaled_block_distance(const FrameSample& a, const FrameSample& b) {
    const int BX = 16, BY = 12;
    auto means = [&](const FrameSample& f, int bx, int by, double out[3]) {
        out[0] = out[1] = out[2] = 0;
        std::size_t n = 0;
        for (std::uint32_t y = by * f.image_h / BY; y < (by + 1) * f.image_h / BY; ++y)
            for (std::uint32_t x = bx * f.image_w / BX; x < (bx + 1) * f.image_w / BX; ++x, ++n) {
                const auto c = pixel_at(*f.image, std::size_t(y) * f.image_w + x);
                out[0] += (c >> 16) & 255; out[1] += (c >> 8) & 255; out[2] += c & 255;
            }
        if (n) for (int k = 0; k < 3; ++k) out[k] /= double(n);
    };
    double total = 0;
    for (int by = 0; by < BY; ++by)
        for (int bx = 0; bx < BX; ++bx) {
            double ma[3], mb[3];
            means(a, bx, by, ma); means(b, bx, by, mb);
            for (int k = 0; k < 3; ++k) total += std::fabs(ma[k] - mb[k]);
        }
    return total / (BX * BY * 3);
}

void analyse_pixels(OpenResult& r) {
    {
        std::lock_guard<std::mutex> lock(g_samples_mu);
        r.samples = g_samples;
    }
    std::sort(r.samples.begin(), r.samples.end(),
              [](const FrameSample& a, const FrameSample& b) { return a.t < b.t; });
    const FrameSample* last = nullptr;
    for (auto& f : r.samples) {
        if (f.ok) last = &f;
        else ++r.unreadable_frames;
    }
    if (!last) return;
    r.settled_bg = dominant(*last).first;
    for (auto& f : r.samples) {
        if (!f.ok) continue;
        int match = 0;
        for (int i = 0; i < kSamples; ++i)
            if (near_rgb(f.rgb[i], last->rgb[i], 10)) ++match;
        if (match * 10 >= kSamples * 9) { r.look_ready = f.t; break; }
    }
    // Before the editor looks like itself, a frame may show only the settled
    // look's background (`--settled-bg`, else the settled grid's dominant
    // colour): anything else is a stub/placeholder colour, and it stays on
    // screen until the next present replaces it.
    if (g_settled_bg >= 0) r.settled_bg = std::uint32_t(g_settled_bg);
    double offbrand_since = -1;
    for (auto& f : r.samples) {
        if (!f.ok) continue;
        if (r.look_ready >= 0 && f.t >= r.look_ready) break;
        bool off = false;
        for (int i = 0; i < kSamples && !off; ++i)
            off = !near_rgb(f.rgb[i], r.settled_bg, 8);
        if (off) {
            ++r.offbrand_frames;
            if (offbrand_since < 0) offbrand_since = f.t;
        } else if (offbrand_since >= 0) {
            r.offbrand_visible_ms += f.t - offbrand_since;
            offbrand_since = -1;
        }
    }
    if (offbrand_since >= 0 && r.look_ready >= 0)
        r.offbrand_visible_ms += r.look_ready - offbrand_since;

    // Classify every frame. With a captured image the test is every pixel;
    // otherwise the sample grid. "Ready" is the first frame that matched the
    // settled look.
    //   background  every pixel is the settled background (within 8/255)
    //   settling    before ready, every pixel is either the background or
    //               already what the ready frame shows there (chrome the
    //               editor draws before its document, e.g. a resize grip)
    //   ui          the ready frame and after
    //   navy        >= 25% of pixels are the SDK's former stub colour (or,
    //               from ready on, more than 0.5%: stub-coloured bars)
    //   other       anything else -- an off-brand frame
    // An already-final pixel may differ from the ready frame by anti-aliasing
    // alone (chrome drawn before the document lands a fraction of a pixel
    // apart); 24/255 admits that and nothing that reads as another colour.
    constexpr int kFinalTol = 24;
    const FrameSample* ready = nullptr;
    for (auto& f : r.samples)
        if (f.ok && r.look_ready >= 0 && f.t >= r.look_ready) { ready = &f; break; }
    for (auto& f : r.samples) {
        const char* cls = "other";
        if (!f.ok) {
            cls = "unreadable";
        } else {
            std::size_t total = 0, off_bg = 0, off_both = 0, navy = 0;
            const bool images = f.image && ready && ready->image
                && ready->image->size() == f.image->size();
            if (f.image) {
                total = f.image->size() / 3;
                for (std::size_t i = 0; i < total; ++i) {
                    const auto c = pixel_at(*f.image, i);
                    const bool bg = near_rgb(c, r.settled_bg, 8);
                    if (!bg) ++off_bg;
                    if (!bg && !(images && near_rgb(c, pixel_at(*ready->image, i), kFinalTol)))
                        ++off_both;
                    if (near_rgb(c, kStubNavy, 2)) ++navy;
                }
            } else {
                total = kSamples;
                for (int i = 0; i < kSamples; ++i) {
                    const bool bg = near_rgb(f.rgb[i], r.settled_bg, 8);
                    if (!bg) ++off_bg;
                    if (!bg && !(ready && near_rgb(f.rgb[i], ready->rgb[i], kFinalTol))) ++off_both;
                    if (near_rgb(f.rgb[i], kStubNavy, 2)) ++navy;
                }
            }
            const bool past = r.look_ready >= 0 && f.t >= r.look_ready;
            const bool scaled_ui = !past && ready && f.image && ready->image && f.image_w && ready->image_w
                && (f.image_w != ready->image_w || f.image_h != ready->image_h)
                && scaled_block_distance(f, *ready) < 3.0;
            if (navy * 4 >= total || (past && navy * 200 > total))
                cls = "navy";
            else if (past || scaled_ui)
                cls = "ui";
            else if (off_bg == 0)
                cls = "background";
            else if (off_both == 0)
                cls = "settling";
        }
        r.frame_class.push_back(cls);
        if (r.onbrand_empty_ms < 0 && std::strcmp(cls, "ui") == 0 && !r.samples.empty())
            r.onbrand_empty_ms = f.t - r.samples.front().t;
        if (std::strcmp(cls, "background") == 0 || std::strcmp(cls, "settling") == 0)
            ++r.class_background;
        else if (std::strcmp(cls, "ui") == 0) ++r.class_ui;
        else if (std::strcmp(cls, "navy") == 0) ++r.class_navy;
        else if (std::strcmp(cls, "other") == 0) ++r.class_other;
    }
}

// Write the open's frame sequence: every frame up to five past look-ready,
// plus the last, as PNGs, and one JSON line per presented frame.
void write_capture(const OpenResult& r, int open_index, double origin) {
    if (g_capture_dir.empty()) return;
    char name[512];
    std::snprintf(name, sizeof(name), "%s/open%d-frames.json", g_capture_dir.c_str(), open_index);
    FILE* json = std::fopen(name, "w");
    if (json) std::fprintf(json, "[\n");
    long after_ready = 0;
    for (std::size_t i = 0; i < r.samples.size(); ++i) {
        const auto& f = r.samples[i];
        const bool past = r.look_ready >= 0 && f.t >= r.look_ready;
        const bool keep = !past || after_ready++ < 5 || i + 1 == r.samples.size();
        std::string png;
        if (keep && f.image) {
            std::snprintf(name, sizeof(name), "%s/open%d-%03zu-%07.1fms-%s.png",
                          g_capture_dir.c_str(), open_index, i, f.t - origin, r.frame_class[i]);
            if (write_png(f, name)) png = name;
        }
        if (json)
            std::fprintf(json,
                         "%s{\"index\":%zu,\"t_ms\":%.3f,\"uptime_ns\":%llu,"
                         "\"class\":\"%s\",\"centre\":\"%06X\",\"png\":\"%s\"}",
                         i ? ",\n" : "", i, f.t - origin,
                         static_cast<unsigned long long>(f.uptime_ns), r.frame_class[i],
                         f.rgb[(kGridH / 2) * kGridW + kGridW / 2], png.c_str());
    }
    if (json) {
        std::fprintf(json, "\n]\n");
        std::fclose(json);
    }
}

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
    long max_offbrand = -1;
    long expect_first = -1;
    bool expect_view_first = false;
    bool expect_first_ui = false;
    double view_size_w = 0, view_size_h = 0;
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
        else if (a == "--pixels") g_sample_pixels = true;
        else if (a == "--capture-frames") { g_sample_pixels = true; g_capture_dir = next(); }
        else if (a == "--view-size") {
            const char* v = next();
            if (std::sscanf(v, "%lfx%lf", &view_size_w, &view_size_h) != 2) {
                std::fprintf(stderr, "--view-size wants WxH\n");
                return 2;
            }
        }
        else if (a == "--pixels-verify") g_sample_pixels = g_verify_pixels = true;
        else if (a == "--max-offbrand-frames") max_offbrand = std::atol(next());
        else if (a == "--settled-bg") g_settled_bg = std::strtol(next(), nullptr, 16);
        else if (a == "--expect-first-rgb") expect_first = std::strtol(next(), nullptr, 16);
        else if (a == "--view-first") expect_view_first = true;
        else if (a == "--expect-first-ui") expect_first_ui = true;
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
            {
                std::lock_guard<std::mutex> lock(g_samples_mu);
                g_samples.clear();
            }
            observer.result = &r;

            id<AUCocoaUIBase> factory = [[factory_class alloc] init];
            r.factory_begin = now_ms();
            NSView* view = [factory uiViewForAudioUnit:au withSize:NSMakeSize(0, 0)];
            r.factory_end = now_ms();
            [(id)factory release];
            if (!view) { std::fprintf(stderr, "factory returned nil\n"); return 2; }
            if (view_size_w > 0 && view_size_h > 0)
                [view setFrameSize:NSMakeSize(view_size_w, view_size_h)];  // host resize
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
            if (CGColorRef bg = view.layer.backgroundColor) {
                CGColorSpaceRef srgb_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                CGColorRef srgb = CGColorCreateCopyByMatchingToColorSpace(
                    srgb_space, kCGRenderingIntentDefault, bg, nullptr);
                CGColorSpaceRelease(srgb_space);
                if (srgb && CGColorGetNumberOfComponents(srgb) >= 3) {
                    const CGFloat* c = CGColorGetComponents(srgb);
                    r.layer_rgb = (long(c[0] * 255.0 + 0.5) << 16) | (long(c[1] * 255.0 + 0.5) << 8)
                                | long(c[2] * 255.0 + 0.5);
                }
                if (srgb) CGColorRelease(srgb);
            }
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
            spin(60.0);  // let in-flight readbacks land
            if (g_sample_pixels) {
                analyse_pixels(r);
                write_capture(r, n + 1, r.factory_begin);
            }

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
            if (g_sample_pixels) {
                std::printf("    pixels (%s): %zu frames read, %ld unreadable | first frame "
                            "dominant #%06X | settled dominant #%06X | look-ready +%.1f ms "
                            "| off-brand frames before look-ready %ld, on screen %.1f ms\n",
                            g_present_path, r.samples.size(), r.unreadable_frames,
                            r.samples.empty() ? 0u : dominant(r.samples.front()).first,
                            r.settled_bg, rel(r.look_ready), r.offbrand_frames,
                            r.offbrand_visible_ms);
                std::uint32_t prev = 0xffffffff;
                int shown = 0;
                for (auto& f : r.samples) {
                    if (!f.ok) continue;
                    const auto d = dominant(f);
                    const std::uint32_t key = d.first ^ std::uint32_t(d.second << 24);
                    if (key == prev && f.t < r.look_ready) continue;
                    prev = key;
                    std::printf("      +%7.1f ms  dominant #%06X x%2d/%d  centre #%06X%s\n",
                                rel(f.t), d.first, d.second, kSamples,
                                f.rgb[(kGridH / 2) * kGridW + kGridW / 2],
                                f.t == r.look_ready ? "  <- look-ready" : "");
                    if (f.t >= r.look_ready && ++shown >= 2) break;
                }
                std::printf("    frame classes: background/settling %ld, ui %ld, navy %ld, other %ld | "
                            "backing layer before first frame #%06lX | on-brand-but-empty %.1f ms\n",
                            r.class_background, r.class_ui, r.class_navy, r.class_other,
                            r.layer_rgb,
                            r.class_navy + r.class_other == 0 ? r.onbrand_empty_ms : -1.0);
                if (max_offbrand >= 0 && r.class_navy + r.class_other > max_offbrand) {
                    std::printf("FAIL: %ld off-brand frame(s) (%ld navy, %ld other): neither "
                                "the editor's background nor its settled look (max %ld)\n",
                                r.class_navy + r.class_other, r.class_navy, r.class_other,
                                max_offbrand);
                    ++failures;
                }
                if (expect_first >= 0 && !near_rgb(std::uint32_t(r.layer_rgb < 0 ? 0 : r.layer_rgb),
                                                   std::uint32_t(expect_first), 2)) {
                    std::printf("FAIL: the backing layer shown before the first frame is #%06lX, "
                                "not #%06lX\n", r.layer_rgb, expect_first);
                    ++failures;
                }
                // A content-first editor's first presented frame is its
                // document, so the first-frame colour check applies only to a
                // view-first open (PULP_EDITOR_OPEN=view-first): there the
                // first frame must be the declared background.
                if (expect_first >= 0 && !r.samples.empty() && expect_view_first) {
                    const auto& f0 = r.samples.front();
                    bool all = f0.ok;
                    for (int i = 0; all && i < kSamples; ++i)
                        all = near_rgb(f0.rgb[i], std::uint32_t(expect_first), 8);
                    if (!all) {
                        std::printf("FAIL: first presented frame is not #%06lX everywhere "
                                    "(dominant #%06X)\n", expect_first, dominant(f0).first);
                        ++failures;
                    }
                }
                // Content-first: the host's first image of the editor is its
                // document, never an empty frame (the SDK mounts it inside the
                // view-creation call). PULP_EDITOR_OPEN=view-first fails this.
                if (expect_first_ui && !r.frame_class.empty()
                    && std::strcmp(r.frame_class.front(), "ui") != 0) {
                    std::printf("FAIL: the first presented frame is %s, not the editor's UI "
                                "(content-first open)\n", r.frame_class.front());
                    ++failures;
                }
                if (r.samples.empty()) {
                    std::printf("FAIL: --pixels read no presented frame\n");
                    ++failures;
                }
            }
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
        if (g_verify_pixels)
            std::printf("pixels-verify: %ld frame(s) read differently 20 ms later\n",
                        g_verify_mismatch);
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
                                    "\"presents\":%ld,\"max_stall_ms\":%.3f,"
                                    "\"look_ready_ms\":%.3f,\"offbrand_frames\":%ld,"
                                    "\"offbrand_visible_ms\":%.3f,\"onbrand_empty_ms\":%.3f,"
                                    "\"first_frame_rgb\":\"%06X\",\"settled_rgb\":\"%06X\"}",
                                 i ? "," : "", r.factory_end - r.factory_begin, r.view_w, r.view_h,
                                 rel(r.first_drawable), rel(r.first_present),
                                 rel(r.content_present), rel(r.idle), r.presents, r.max_stall_ms,
                                 rel(r.look_ready), r.class_navy + r.class_other,
                                 r.offbrand_visible_ms,
                                 r.class_navy + r.class_other == 0 ? r.onbrand_empty_ms : -1.0,
                                 r.samples.empty() ? 0u : dominant(r.samples.front()).first,
                                 r.settled_bg);
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
