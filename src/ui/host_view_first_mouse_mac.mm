// A click into the editor must act on the control under it even when the
// editor's window is not the key window.
//
// AppKit hands a mouse-down in a non-key window to the view only if that view
// answers YES to -acceptsFirstMouse:. NSView answers NO, and the Pulp SDK this
// build links (v0.884.0) does not override it on its plug-in host views, so the
// first press after the user has touched anything else in the DAW — the track
// list, the transport, another plug-in — only makes the editor's window key and
// never reaches the control. The second press works. In Logic that reads as a
// button that responds on one part of it and not another, because the user
// retries somewhere else on the button and the retry is the one that lands.
//
// This is a framework defect and is fixed in Pulp's plugin_view_host_mac.mm.
// Until Spectr's SDK carries that fix, add the override here. class_addMethod
// refuses to replace a method the class already implements itself, so once
// the SDK defines -acceptsFirstMouse: this installs nothing and is inert.
//
// The host view classes are renamed per binary (PulpGpuPluginView_Spectr_AU in
// the AU, PulpGpuPluginView in a test binary), so they are found by prefix
// among the classes of THIS image only, never another plug-in's copy.

#import <AppKit/AppKit.h>
#include <dlfcn.h>
#include <objc/runtime.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace spectr {

namespace {

BOOL accepts_first_mouse(id, SEL, NSEvent*) { return YES; }

bool is_plugin_host_view_class(const char* name) {
    return std::strncmp(name, "PulpGpuPluginView", 17) == 0
           || std::strncmp(name, "PulpPluginView", 14) == 0;
}

int g_installed = 0;

}  // namespace

// Returns how many host view classes gained the override (0 once the SDK has
// its own). Idempotent and cheap after the first call.
int install_host_view_first_mouse() {
    static std::once_flag once;
    std::call_once(once, [] {
        Dl_info info{};
        if (dladdr(reinterpret_cast<const void*>(&install_host_view_first_mouse),
                   &info) == 0
            || info.dli_fname == nullptr)
            return;
        Method base = class_getInstanceMethod([NSView class],
                                              @selector(acceptsFirstMouse:));
        if (base == nullptr) return;
        const char* types = method_getTypeEncoding(base);
        unsigned int count = 0;
        const char** names = objc_copyClassNamesForImage(info.dli_fname, &count);
        if (names == nullptr) return;
        for (unsigned int index = 0; index < count; ++index) {
            if (!is_plugin_host_view_class(names[index])) continue;
            Class cls = objc_getClass(names[index]);
            if (cls == nil || ![cls isSubclassOfClass:[NSView class]]) continue;
            if (class_addMethod(cls, @selector(acceptsFirstMouse:),
                                reinterpret_cast<IMP>(&accepts_first_mouse),
                                types))
                ++g_installed;
        }
        std::free(names);
    });
    return g_installed;
}

}  // namespace spectr
