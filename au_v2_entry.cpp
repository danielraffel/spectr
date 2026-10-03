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

// ── Offline-render shim: DELETE ON THE SDK BUMP that ships it in Pulp ────────
//
// A host bouncing faster than realtime (Logic, REAPER) writes
// kAudioUnitProperty_OfflineRender before the render. Pulp 0.895.1's AU v2
// adapter does not implement that property, so the write fails and every
// block reports realtime; Spectr then cannot tell it must wait for its
// mask-design worker, and a bounce diverges from playback. Pulp now handles
// the property itself and reports it as ProcessContext::is_offline() (the
// "report AU v2 offline bounces through ProcessContext" change). Once the
// pinned SDK contains it, delete this class and go back to PULP_AU_PLUGIN:
// the base GetProperty/SetProperty then answer the property and Spectr reads
// the flag from ProcessContext.
namespace spectr::detail {

class OfflineRenderShimAU : public pulp::format::au::PulpAUEffect {
public:
    explicit OfflineRenderShimAU(AudioComponentInstance ci) : PulpAUEffect(ci) {}

    OSStatus GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope,
                             AudioUnitElement element, UInt32& size,
                             bool& writable) override {
        if (id == kAudioUnitProperty_OfflineRender) {
            if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
            size = sizeof(UInt32);
            writable = true;
            return noErr;
        }
        return PulpAUEffect::GetPropertyInfo(id, scope, element, size, writable);
    }

    OSStatus GetProperty(AudioUnitPropertyID id, AudioUnitScope scope,
                         AudioUnitElement element, void* data) override {
        if (id == kAudioUnitProperty_OfflineRender) {
            if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
            if (!data) return kAudioUnitErr_InvalidPropertyValue;
            auto* processor = spectr_processor_();
            *static_cast<UInt32*>(data) =
                processor && processor->host_offline_render() ? 1u : 0u;
            return noErr;
        }
        return PulpAUEffect::GetProperty(id, scope, element, data);
    }

    OSStatus SetProperty(AudioUnitPropertyID id, AudioUnitScope scope,
                         AudioUnitElement element, const void* data,
                         UInt32 size) override {
        if (id == kAudioUnitProperty_OfflineRender) {
            if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
            if (!data || size < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            auto* processor = spectr_processor_();
            if (!processor) return kAudioUnitErr_Uninitialized;
            processor->set_host_offline_render(*static_cast<const UInt32*>(data) != 0);
            return noErr;
        }
        return PulpAUEffect::SetProperty(id, scope, element, data, size);
    }

private:
    // The adapter keeps its Processor private; the editor-context property is
    // the public seam that hands it out, so the shim reads it from there.
    spectr::Spectr* spectr_processor_() {
        pulp::format::au::PulpEditorContext context;
        if (PulpAUEffect::GetProperty(pulp::format::au::kPulpEditorContextProperty,
                                      kAudioUnitScope_Global, 0, &context) != noErr)
            return nullptr;
        return dynamic_cast<spectr::Spectr*>(context.processor);
    }
};

}  // namespace spectr::detail

#define SPECTR_AU_V2_PLUGIN(ClassName, factory_fn)                              \
    PULP_REGISTER_PLUGIN(factory_fn)                                           \
    class ClassName : public spectr::detail::OfflineRenderShimAU {              \
    public:                                                                    \
        explicit ClassName(AudioComponentInstance ci) : OfflineRenderShimAU(ci) {} \
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
