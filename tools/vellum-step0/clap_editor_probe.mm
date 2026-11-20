// Offscreen proof through the real plug-in editor path. It loads a built CLAP
// bundle as a host would: clap_entry -> factory -> plugin, then clap.gui
// create (cocoa, embedded) -> set_size -> set_parent on an NSView that is in
// no window, so nothing is ordered on screen. The plug-in's editor is created
// by its own EditorFactory, and Pulp's PluginViewHost::create() makes the
// native host view. The probe forces the host view's -updateLayer (its paint)
// and reads the CGImage back from its layer. Then it changes Mix the way a host
// does (clap.params flush with a CLAP_EVENT_PARAM_VALUE), drives the host
// timer the editor registered (the UI tick that pumps parameter listeners),
// paints again and compares the frames.
//
// usage: clap_editor_probe <bundle.clap> <out-dir> [param-id] [plain-value]
// exit 0 only when every check passes.
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <clap/clap.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

struct Timer { clap_id id; uint32_t ms; };
static std::vector<Timer> g_timers;
static bool g_callback_requested = false;

static bool reg_timer(const clap_host_t*, uint32_t ms, clap_id* id) {
    *id = static_cast<clap_id>(g_timers.size() + 1);
    g_timers.push_back({*id, ms});
    std::printf("host: plug-in registered timer %u (%u ms)\n", *id, ms);
    return true;
}
static bool unreg_timer(const clap_host_t*, clap_id id) {
    for (auto& t : g_timers) if (t.id == id) t.id = CLAP_INVALID_ID;
    return true;
}
static const clap_host_timer_support_t s_timer = {reg_timer, unreg_timer};
static void gui_hints_changed(const clap_host_t*) {}
static bool gui_request_resize(const clap_host_t*, uint32_t, uint32_t) { return false; }
static bool gui_request_show(const clap_host_t*) { return false; }
static bool gui_request_hide(const clap_host_t*) { return false; }
static void gui_closed(const clap_host_t*, bool) {}
static const clap_host_gui_t s_host_gui = {gui_hints_changed, gui_request_resize, gui_request_show,
                                           gui_request_hide, gui_closed};
static void params_rescan(const clap_host_t*, clap_param_rescan_flags) {}
static void params_clear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
static void params_request_flush(const clap_host_t*) {}
static const clap_host_params_t s_host_params = {params_rescan, params_clear, params_request_flush};

static const void* host_get_extension(const clap_host_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &s_timer;
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &s_host_gui;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &s_host_params;
    return nullptr;
}
static void host_request_restart(const clap_host_t*) {}
static void host_request_process(const clap_host_t*) {}
static void host_request_callback(const clap_host_t*) { g_callback_requested = true; }

static clap_host_t g_host = {CLAP_VERSION, nullptr, "vm1 editor probe", "pulp-validation", "",
                             "1.0", host_get_extension, host_request_restart,
                             host_request_process, host_request_callback};

// Input event list holding at most one param event.
static clap_event_param_value_t g_ev;
static int g_ev_count = 0;
static uint32_t in_size(const clap_input_events_t*) { return g_ev_count; }
static const clap_event_header_t* in_get(const clap_input_events_t*, uint32_t) {
    return &g_ev.header;
}
static bool out_push(const clap_output_events_t*, const clap_event_header_t*) { return true; }

struct Frame { std::vector<uint8_t> rgba; size_t w = 0, h = 0; CGImageRef image = nullptr; };

static Frame read_layer(NSView* v) {
    Frame f;
    const size_t w = static_cast<size_t>(std::max(1.0, v.bounds.size.width));
    const size_t h = static_cast<size_t>(std::max(1.0, v.bounds.size.height));
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr pixelsWide:w pixelsHigh:h bitsPerSample:8
        samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
        bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:w * 4 bitsPerPixel:32];
    NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:gc];
    [v drawRect:v.bounds];
    [gc flushGraphics];
    [NSGraphicsContext restoreGraphicsState];
    CGImageRef img = rep.CGImage;
    if (!img) return f;
    f.w = CGImageGetWidth(img); f.h = CGImageGetHeight(img);
    f.rgba.assign(f.w * f.h * 4, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef c = CGBitmapContextCreate(f.rgba.data(), f.w, f.h, 8, f.w * 4, cs,
                                            kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(c, CGRectMake(0, 0, f.w, f.h), img);
    CGContextRelease(c); CGColorSpaceRelease(cs);
    f.image = CGImageRetain(img);
    return f;
}

static bool write_png(CGImageRef img, const std::string& path) {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef d = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    if (!d) return false;
    CGImageDestinationAddImage(d, img, nullptr);
    bool ok = CGImageDestinationFinalize(d);
    CFRelease(d);
    return ok;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 3) { std::fprintf(stderr, "usage: %s bundle.clap out-dir [id] [value]\n", argv[0]); return 2; }
        const std::string bundle = argv[1], out = argv[2];
        const clap_id pid = argc > 3 ? static_cast<clap_id>(std::atoi(argv[3])) : 1;
        const double newval = argc > 4 ? std::atof(argv[4]) : 25.0;
        std::string name = bundle.substr(bundle.find_last_of('/') + 1);
        name = name.substr(0, name.find('.'));
        const std::string bin = bundle + "/Contents/MacOS/" + name;
        void* h = dlopen(bin.c_str(), RTLD_LOCAL | RTLD_NOW);
        if (!h) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
        auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
        if (!entry || !entry->init(bundle.c_str())) { std::fprintf(stderr, "clap_entry init failed\n"); return 2; }
        auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
        std::printf("plug-in: %s (%s)\n", desc->name, desc->id);
        const clap_plugin_t* plugin = factory->create_plugin(factory, &g_host, desc->id);
        if (!plugin || !plugin->init(plugin)) { std::fprintf(stderr, "plugin init failed\n"); return 2; }

        auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        auto* gui = static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
        auto* timer = static_cast<const clap_plugin_timer_support_t*>(plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT));
        check(params != nullptr, "plug-in exposes clap.params");
        check(gui != nullptr, "plug-in exposes clap.gui (it has an editor)");
        if (!params || !gui) return 1;
        clap_param_info_t info{};
        params->get_info(plugin, 0, &info);
        double v0 = 0; params->get_value(plugin, pid, &v0);
        std::printf("param %u '%s' = %.2f (range %.1f..%.1f)\n", pid, info.name, v0, info.min_value, info.max_value);

        check(gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false), "editor supports embedded cocoa");
        check(gui->create(plugin, CLAP_WINDOW_API_COCOA, false), "gui.create (the plug-in's EditorFactory)");
        uint32_t w = 0, hgt = 0;
        gui->get_size(plugin, &w, &hgt);
        std::printf("editor size %ux%u\n", w, hgt);
        // A parent view in no window: nothing can be ordered on screen.
        NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, w, hgt)];
        clap_window_t win{}; win.api = CLAP_WINDOW_API_COCOA; win.cocoa = (__bridge void*)parent;
        check(gui->set_parent(plugin, &win), "gui.set_parent onto an NSView in no window");
        gui->show(plugin);
        check(parent.subviews.count == 1, "the editor attached one native host view");
        if (parent.subviews.count != 1) return 1;
        NSView* hostview = parent.subviews[0];
        [hostview setWantsLayer:YES];
        [hostview setFrame:NSMakeRect(0, 0, w, hgt)];
        std::printf("host view class %s, window %p, layer %p\n",
                    class_getName([hostview class]), (__bridge void*)hostview.window,
                    (__bridge void*)hostview.layer);
        check(hostview.window == nil, "no window exists for the editor");

        auto tick = [&] {
            for (int i = 0; i < 4; ++i) {
                for (auto& t : g_timers) if (t.id != CLAP_INVALID_ID && timer) timer->on_timer(plugin, t.id);
                if (g_callback_requested) { g_callback_requested = false; plugin->on_main_thread(plugin); }
            }
        };
        auto paint = [&] {
            [hostview updateLayer];
            return read_layer(hostview);
        };
        tick();
        Frame a = paint(), a2 = paint();
        check(!a.rgba.empty(), "the host view's paint put a frame in its layer");
        check(a.rgba == a2.rgba, "control: two paints of the same state are byte-identical");
        // Negative control: a DAW flush of the current value must not repaint.
        g_ev = {};
        g_ev.header.size = sizeof(g_ev);
        g_ev.header.type = CLAP_EVENT_PARAM_VALUE;
        g_ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        g_ev.param_id = pid;
        g_ev.note_id = -1; g_ev.port_index = -1; g_ev.channel = -1; g_ev.key = -1;
        g_ev.value = v0;
        g_ev_count = 1;
        clap_input_events_t same_in{nullptr, in_size, in_get};
        clap_output_events_t same_out{nullptr, out_push};
        params->flush(plugin, &same_in, &same_out);
        g_ev_count = 0;
        tick();
        Frame same = paint();
        check(same.rgba == a.rgba, "negative control: flushing the current value changes 0 pixels");
        if (same.image) CGImageRelease(same.image);
        if (a.image) write_png(a.image, out + "/editor-mix-before.png");
        std::printf("frame %zux%zu\n", a.w, a.h);

        // Host-side change, the way a DAW sends it outside process().
        g_ev = {};
        g_ev.header.size = sizeof(g_ev);
        g_ev.header.type = CLAP_EVENT_PARAM_VALUE;
        g_ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        g_ev.param_id = pid;
        g_ev.note_id = -1; g_ev.port_index = -1; g_ev.channel = -1; g_ev.key = -1;
        g_ev.value = newval;
        g_ev_count = 1;
        clap_input_events_t in{nullptr, in_size, in_get};
        clap_output_events_t outq{nullptr, out_push};
        params->flush(plugin, &in, &outq);
        g_ev_count = 0;
        double v1 = 0; params->get_value(plugin, pid, &v1);
        std::printf("after flush: param %u = %.2f\n", pid, v1);
        check(v1 == newval, "the plug-in took the host's new value");
        tick();
        Frame b = paint();
        if (b.image) write_png(b.image, out + "/editor-mix-after.png");
        long changed = 0;
        size_t minx = SIZE_MAX, miny = SIZE_MAX, maxx = 0, maxy = 0;
        if (b.rgba.size() == a.rgba.size())
            for (size_t y = 0; y < a.h; ++y)
                for (size_t x = 0; x < a.w; ++x) {
                    const size_t i = (y * a.w + x) * 4;
                    if (std::memcmp(&a.rgba[i], &b.rgba[i], 4) != 0) {
                        ++changed;
                        if (x < minx) minx = x; if (y < miny) miny = y;
                        if (x > maxx) maxx = x; if (y > maxy) maxy = y;
                    }
                }
        std::printf("changed pixels %ld, bbox x %zu..%zu y %zu..%zu (frame %zux%zu)\n", changed,
                    minx, maxx, miny, maxy, a.w, a.h);
        check(changed > 200, "the Mix change is visible in the editor's painted frame");
        // The knob is 100x100 centred in the editor: the change stays inside it.
        const double sx = double(a.w) / w;
        const double kx = (w - 100) / 2.0 * sx, ky = (hgt - 100) / 2.0 * sx, ks = 100 * sx;
        check(changed > 0 && minx >= kx && maxx < kx + ks && miny >= ky && maxy < ky + ks,
              "every changed pixel lies inside the centred 100x100 knob");

        gui->hide(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        if (a.image) CGImageRelease(a.image);
        if (a2.image) CGImageRelease(a2.image);
        if (b.image) CGImageRelease(b.image);
        std::printf("%s (%d failure(s))\n", failures ? "EDITOR PROOF FAILED" : "EDITOR PROOF PASSED", failures);
        return failures ? 1 : 0;
    }
}
