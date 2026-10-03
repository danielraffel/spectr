#include "spectr/spectr.hpp"
#include <pulp/format/au_v2_entry.hpp>

namespace {

// AU v2 is the ONE format Spectr ships where the user has no way to resize the
// editor: the format hands a size plugin-ward once at view creation and never
// again, and Logic's plug-in window offers no grow area. So this build — and
// only this build — draws its own corner grip. See
// `spectr::set_editor_owns_resize_grip` for why the other formats must not.
//
// Asserted from a static initializer because the decision belongs to the linked
// entry point and there is no wrapper-type query on `Processor` to ask instead.
// Safe against static-init order: the flag it writes is a namespace-scope
// `std::atomic<bool>` with a constexpr constructor, so it is constant-
// initialized before any dynamic initializer in the image can run.
const bool g_au_v2_owns_resize_grip = [] {
    spectr::set_editor_owns_resize_grip(true);
    return true;
}();

}  // namespace

// ── Offline-render session lifetime ──────────────────────────────────────────
//
// Pulp's AU v2 adapter answers kAudioUnitProperty_OfflineRender and reports it
// as ProcessContext::is_offline(). Its flag lives until the host writes it
// back, which is the property's contract -- but an offline block makes Spectr
// wait (up to kOfflineBlockWaitBudget) for its design workers, so a host that
// set the flag for one bounce and never cleared it would hold every later
// realtime block. This subclass adds only that lifetime: a host write survives
// exactly ONE re-initialization (a host may set the flag and THEN re-initialize
// for the bounce, or set it before the first Initialize), and a write older
// than the previous Initialize is a previous session's and is dropped. Reset
// deliberately does not clear it: hosts reset at transport start, which can
// come after they set the flag for the bounce. Delete this class, and go back
// to PULP_AU_PLUGIN, when Pulp's adapter scopes the flag to a session itself.
namespace spectr::detail {

class OfflineSessionAU : public pulp::format::au::PulpAUEffect {
public:
    explicit OfflineSessionAU(AudioComponentInstance ci) : PulpAUEffect(ci) {}

    OSStatus SetProperty(AudioUnitPropertyID id, AudioUnitScope scope,
                         AudioUnitElement element, const void* data,
                         UInt32 size) override {
        const OSStatus status = PulpAUEffect::SetProperty(id, scope, element, data, size);
        if (id == kAudioUnitProperty_OfflineRender && status == noErr)
            offline_written_since_initialize_ = true;
        return status;
    }

    OSStatus Initialize() override {
        if (!offline_written_since_initialize_) {
            const UInt32 realtime = 0;
            PulpAUEffect::SetProperty(kAudioUnitProperty_OfflineRender,
                                      kAudioUnitScope_Global, 0, &realtime,
                                      sizeof(realtime));
        }
        offline_written_since_initialize_ = false;
        return PulpAUEffect::Initialize();
    }

private:
    bool offline_written_since_initialize_ = false;
};

}  // namespace spectr::detail

#define SPECTR_AU_V2_PLUGIN(ClassName, factory_fn)                              \
    PULP_REGISTER_PLUGIN(factory_fn)                                           \
    class ClassName : public spectr::detail::OfflineSessionAU {                 \
    public:                                                                    \
        explicit ClassName(AudioComponentInstance ci) : OfflineSessionAU(ci) {} \
    };                                                                         \
    AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, ClassName)

#if defined(SPECTR_WEBVIEW_REFERENCE)
SPECTR_AU_V2_PLUGIN(SpectrWebViewReferenceAU, spectr::create_spectr)
#elif defined(SPECTR_DEV_IDENTITY)
// One level of indirection so SPECTR_DEV_AU_CLASS expands before the entry
// macro pastes it into the factory name the Info.plist declares.
#define SPECTR_AU_ENTRY(ClassName, factory) SPECTR_AU_V2_PLUGIN(ClassName, factory)
SPECTR_AU_ENTRY(SPECTR_DEV_AU_CLASS, spectr::create_spectr)
#elif defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
SPECTR_AU_V2_PLUGIN(SpectrNativePreviewAU, spectr::create_spectr)
#else
SPECTR_AU_V2_PLUGIN(SpectrAU, spectr::create_spectr)
#endif
