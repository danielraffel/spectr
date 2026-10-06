// Delete on SDK bump: once Spectr pins a Pulp SDK that provides
// pulp_add_sparkle(), the SDK's standalone host owns this ("Check for
// Updates…" in the app menu, scheduled checks in Developer-ID builds), and
// cmake/SpectrSparkle.cmake stops compiling this file.
//
// Linked into Spectr.app only (the Spectr_Standalone target); no plug-in
// bundle compiles it. It reaches Sparkle through the Objective-C runtime, the
// same way the SDK does, so it needs no Sparkle headers.

#import <Cocoa/Cocoa.h>
#import <Security/Security.h>
#include <objc/message.h>

#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {

id g_controller = nil;

Class updater_class() {
    Class cls = NSClassFromString(@"SPUStandardUpdaterController");
    if (cls == Nil) return Nil;
    NSString* frameworks = [[[NSBundle mainBundle] privateFrameworksPath] stringByStandardizingPath];
    NSString* sparkle = [[[NSBundle bundleForClass:cls] bundlePath] stringByStandardizingPath];
    if (frameworks.length == 0 || ![sparkle hasPrefix:[frameworks stringByAppendingString:@"/"]])
        return Nil;
    return cls;
}

bool developer_id_signed() {
    SecCodeRef code = nullptr;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &code) != errSecSuccess || !code) return false;
    SecStaticCodeRef static_code = nullptr;
    bool team = false;
    if (SecCodeCopyStaticCode(code, kSecCSDefaultFlags, &static_code) == errSecSuccess && static_code) {
        CFDictionaryRef info = nullptr;
        if (SecCodeCopySigningInformation(static_code, kSecCSSigningInformation, &info) == errSecSuccess && info) {
            auto t = static_cast<CFStringRef>(CFDictionaryGetValue(info, kSecCodeInfoTeamIdentifier));
            team = t && CFGetTypeID(t) == CFStringGetTypeID() && CFStringGetLength(t) > 0;
            CFRelease(info);
        }
        CFRelease(static_code);
    }
    CFRelease(code);
    return team;
}

void ensure_controller() {
    if (g_controller != nil) return;
    Class cls = updater_class();
    if (cls == Nil) return;
    SEL init = NSSelectorFromString(@"initWithStartingUpdater:updaterDelegate:userDriverDelegate:");
    id obj = [cls alloc];
    if (![obj respondsToSelector:init]) return;
    g_controller = reinterpret_cast<id (*)(id, SEL, BOOL, id, id)>(objc_msgSend)(obj, init, YES, nil, nil);
}

bool headless_launch() {
    // A headless / screenshot standalone runs with the accessory activation
    // policy (no Dock icon, no menu bar); it gets no menu item and no checks.
    return NSApp == nil || [NSApp activationPolicy] != NSApplicationActivationPolicyRegular;
}

} // namespace

@interface SpectrUpdaterMenuTarget : NSObject
- (void)checkForUpdates:(id)sender;
@end

@implementation SpectrUpdaterMenuTarget
- (void)checkForUpdates:(id)sender {
    (void)sender;
    ensure_controller();
    SEL check = NSSelectorFromString(@"checkForUpdates:");
    if (g_controller != nil && [g_controller respondsToSelector:check])
        reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(g_controller, check, nil);
}
@end

namespace {

void install_updater() {
    id feed = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUFeedURL"];
    if (![feed isKindOfClass:[NSString class]] || [(NSString*)feed length] == 0) return;
    if (updater_class() == Nil || headless_launch()) return;
    const char* override_value = std::getenv("PULP_STANDALONE_UPDATER");
    const bool off = override_value && (!std::strcmp(override_value, "0") ||
                                        !strcasecmp(override_value, "off") ||
                                        !strcasecmp(override_value, "false"));
    if (off) return;
    const bool forced_on = override_value && (!std::strcmp(override_value, "1") ||
                                              !strcasecmp(override_value, "on") ||
                                              !strcasecmp(override_value, "true"));

    NSMenu* app_menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    if (app_menu != nil) {
        static SpectrUpdaterMenuTarget* target = [[SpectrUpdaterMenuTarget alloc] init];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"Check for Updates…"
                                                      action:@selector(checkForUpdates:)
                                               keyEquivalent:@""];
        [item setTarget:target];
        [app_menu insertItem:item atIndex:0];
        [item release];
        // Keep the item visually grouped apart from Settings / Quit.
        if ([app_menu numberOfItems] > 1 && ![[app_menu itemAtIndex:1] isSeparatorItem])
            [app_menu insertItem:[NSMenuItem separatorItem] atIndex:1];
    }
    if (forced_on || developer_id_signed()) ensure_controller();
}

__attribute__((constructor)) void spectr_register_updater_shim() {
    // Runs before main(); NSApp does not exist yet. The SDK installs the app
    // menu before [NSApp run], so the launch notification sees it.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationDidFinishLaunchingNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) { install_updater(); }];
}

} // namespace
