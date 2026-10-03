// Editor-open contract for plug-in hosts.
//
// A host builds the editor view synchronously and shows nothing of it until
// that call returns. In Logic the plug-in window is just Logic's header strip
// (power, preset, Editor, link) for the whole call, then grows to the editor's
// size. Evaluating the captured document inside that call made the header-only
// window last ~1.4 s on an M5 Max and much longer under load
// (tools/editor_open_probe.mm measures it through the real AU v2 Cocoa view).
//
// These tests pin the contract that removes that phase: inside a host's
// view-creation call create_view() hands back a correctly sized root whose
// session has NOT evaluated anything yet, and the document mounts on the
// view's second frame, at the size the host reported.
//
// On an SDK with view-first loading (PULP_VIEW_HAS_DEFERRED_DOCUMENT_LOAD)
// Pulp owns this: ViewBridge holds view::ScopedDeferredDocumentLoad around
// create_view() for every plug-in format and the session evaluates from its
// second idle poll. On SDK 0.890.1 the AU v2 entry point declares
// set_editor_defers_document_load(true) and Spectr's frame tick evaluates;
// that branch goes with the SDK bump.

#include "spectr/spectr.hpp"
#include "spectr/editor_resize.hpp"
#include "spectr/param_surface.hpp"

#include <catch2/catch_test_macros.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/scripted_ui.hpp>
#include <pulp/view/widget_bridge.hpp>
#include <pulp/view/widgets.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace {

using pulp::view::View;

struct DeferralFlag {
    explicit DeferralFlag(bool value) { spectr::set_editor_defers_document_load(value); }
    ~DeferralFlag() { spectr::set_editor_defers_document_load(false); }
};

const pulp::view::Label* find_label(const View& view, std::string_view text) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view);
        label != nullptr && label->text() == text)
        return label;
    for (std::size_t index = 0; index < view.child_count(); ++index)
        if (const auto* match = find_label(*view.child_at(index), text))
            return match;
    return nullptr;
}

struct EditorHarness {
    pulp::state::StateStore store;
    spectr::Spectr processor;
    pulp::view::FrameClock clock;
    std::unique_ptr<View> root;

    EditorHarness() {
        processor.set_state_store(&store);
        processor.define_parameters(store);
        pulp::format::PrepareContext prepare;
        prepare.sample_rate = 48000.0;
        prepare.max_buffer_size = 256;
        prepare.input_channels = 2;
        prepare.output_channels = 2;
        processor.prepare(prepare);
    }
    ~EditorHarness() {
        if (root) processor.on_view_closed(*root);
    }
    // `deferred` opens the view the way a plug-in host does: inside the
    // view-creation guard Pulp's ViewBridge holds for hosted editors.
    void open_view(bool deferred = false) {
#if defined(PULP_VIEW_HAS_DEFERRED_DOCUMENT_LOAD)
        std::optional<pulp::view::ScopedDeferredDocumentLoad> guard;
        if (deferred) guard.emplace();
#else
        (void)deferred;
#endif
        root = processor.create_view();
        REQUIRE(root != nullptr);
        root->set_bounds({0, 0, 1320, 860});
        root->set_frame_clock(&clock);
        root->layout_children();
        processor.on_view_opened(*root);
    }
    // One host frame: the idle pump polls the session, then the frame clock
    // ticks -- the order ViewBridge's idle pump and the GPU host run them.
    void frame() {
        if (auto* session = processor.active_scripted_ui()) session->poll();
        clock.tick(1.0f / 60.0f);
    }
    pulp::view::WidgetBridge* bridge() {
        auto* session = processor.active_scripted_ui();
        return session ? session->bridge() : nullptr;
    }
};

}  // namespace

TEST_CASE("the editor reports its full preferred size before it has evaluated anything",
          "[editor-open]") {
    // The size a host sizes its window to is a compile-time constant, never
    // something the document computes, so it is correct at view creation.
    EditorHarness h;
    const auto size = h.processor.view_size();
    CHECK(size.preferred_width >= 990u);
    CHECK(size.preferred_height >= 645u);
}

TEST_CASE("a deferred editor returns unevaluated and mounts on its second frame",
          "[editor-open]") {
    DeferralFlag defer{true};
    EditorHarness h;
    h.open_view(/*deferred=*/true);

    // Returned to the host before any script ran: a session exists (the
    // adapter needs it to wire the GPU surface), its realm does not.
    REQUIRE(h.processor.active_scripted_ui() != nullptr);
    CHECK(h.bridge() == nullptr);
    CHECK(find_label(*h.root, "CLEAR") == nullptr);

    // Host resizes before the first frame are kept, not dropped.
    h.processor.on_view_resized(*h.root, 990, 645);
    CHECK(h.bridge() == nullptr);

    // Frame 1 paints the empty editor; nothing is evaluated yet.
    h.frame();
    CHECK(h.bridge() == nullptr);

    // Frame 2 evaluates the document.
    h.frame();
    REQUIRE(h.bridge() != nullptr);
    for (int frame = 0; frame < 16; ++frame) h.frame();
    CHECK(find_label(*h.root, "CLEAR") != nullptr);
    // Under the pinned viewport the root stays at the authored box whatever
    // host size was reported before the document existed.
    CHECK(h.root->bounds().width == static_cast<float>(spectr::kEditorDesignWidth));
    CHECK(h.root->bounds().height == static_cast<float>(spectr::kEditorDesignHeight));
}

TEST_CASE("a deferred editor closed before its second frame tears down cleanly",
          "[editor-open]") {
    DeferralFlag defer{true};
    EditorHarness h;
    h.open_view(/*deferred=*/true);
    h.frame();
    h.processor.on_view_closed(*h.root);
    h.root.reset();
    // The analyzer subscription went with the editor; ticking must not reach
    // a stale load.
    h.clock.tick(1.0f / 60.0f);
    h.clock.tick(1.0f / 60.0f);
    CHECK(h.processor.active_scripted_ui() == nullptr);

    // And a reopen evaluates normally.
    h.open_view(/*deferred=*/true);
    h.frame();
    h.frame();
    CHECK(h.bridge() != nullptr);
}

TEST_CASE("without deferral the document is mounted when create_view returns",
          "[editor-open]") {
    // The default every in-process harness relies on (tests, native-shot).
    DeferralFlag defer{false};
    EditorHarness h;
    h.open_view();
    CHECK(h.bridge() != nullptr);
}

#if defined(PULP_VIEW_HAS_DEFERRED_DOCUMENT_LOAD)
#include <pulp/view/js_engine.hpp>

TEST_CASE("an editor open evaluates the document once and a reopen reuses its bytecode",
          "[editor-open]") {
    // Counts, not wall time: a first load must not also evaluate the
    // document on a throwaway probe realm, and a reopen in the same process
    // must read the runtime bundle's compiled bytecode instead of parsing it.
    EditorHarness h;
    h.open_view(/*deferred=*/true);
    h.frame();
    h.frame();
    REQUIRE(h.bridge() != nullptr);
    CHECK(h.processor.active_scripted_ui()->probe_realm_evaluations() == 0);

    h.processor.on_view_closed(*h.root);
    h.root.reset();
    const auto before = pulp::view::script_bytecode_cache_stats();
    h.open_view(/*deferred=*/true);
    h.frame();
    h.frame();
    REQUIRE(h.bridge() != nullptr);
    const auto after = pulp::view::script_bytecode_cache_stats();
    CHECK(after.hits > before.hits);
    CHECK(after.compiles == before.compiles);
    CHECK(h.processor.active_scripted_ui()->probe_realm_evaluations() == 0);
}
#endif

namespace {
// load_script returns nothing, so a value rides back as an exception's text.
std::string read_js(pulp::view::WidgetBridge& bridge, const std::string& expr) {
    try {
        bridge.load_script("throw new Error('PULPVALUE:' + (" + expr + ") + ':PULPEND');",
                           "spectr-editor-open-read");
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        const auto at = msg.find("PULPVALUE:");
        const auto end = msg.find(":PULPEND", at);
        if (at != std::string::npos && end != std::string::npos)
            return msg.substr(at + 10, end - at - 10);
        return "error:" + msg;
    }
    return "no-value";
}

long read_js_int(pulp::view::WidgetBridge& bridge, const std::string& expr) {
    const auto text = read_js(bridge, "String(" + expr + ")");
    try {
        return std::stol(text);
    } catch (...) {
        FAIL("not an integer: " << expr << " -> " << text);
    }
    return -1;
}
}  // namespace

TEST_CASE("an editor open commits nothing after the document mounts",
          "[editor-open]") {
    // Counts, not wall time. Every React commit that touches a host node
    // re-applies the captured document (20-45 ms each on this one), and the
    // open used to make six after the mount: a promise-driven modulation
    // read (2), build info (1), the hydrate frame (1), a passive effect that
    // handed the hydrate to the bank (1) and the tracing badge (1). The
    // editor now reads the processor's state for its first render
    // (tools/patch_materialized_hydrate_before_first_render.py) and the late
    // mounts happen in the mount's layout pass, so nothing is left to commit.
    // Counters: tools/patch_materialized_runtime_commit_stats.py.
    EditorHarness h;
    h.open_view(/*deferred=*/true);
    for (int frame = 0; frame < 120; ++frame) h.frame();
    REQUIRE(h.bridge() != nullptr);
    auto& bridge = *h.bridge();
    const auto stats = read_js(bridge, "JSON.stringify(globalThis.__pulpCommitStats__)");
    INFO("commit stats: " << stats);
    const long mount = read_js_int(bridge, "globalThis.__pulpCommitStats__.mount_commits");
    const long commits = read_js_int(bridge, "globalThis.__pulpCommitStats__.commits");
    const long reapplies = read_js_int(bridge, "globalThis.__pulpCommitStats__.reapplies");
    const long full = read_js_int(bridge, "globalThis.__pulpCommitStats__.full_reapplies");
    REQUIRE(mount >= 1);
    // The mount itself: the root render plus its layout-effect flush.
    CHECK(mount <= 2);
    CHECK(commits - mount <= 1);
    CHECK(reapplies <= mount + 1);
    // Only the mount's own first commit re-applies the whole document.
    CHECK(full == 1);
    // And the editor is hydrated, not merely quiet.
    CHECK(read_js(bridge,
        "String(globalThis.__spectrTestHooks && globalThis.__spectrTestHooks.appState"
        " ? globalThis.__spectrTestHooks.appState().nativeHydrated : 'no-hooks')") == "true");
}

TEST_CASE("an editor opens showing a non-default session without a hydrate commit",
          "[editor-open]") {
    // The first render reads the processor, so a session that is not the
    // default still mounts showing it -- not defaults that a later hydrate
    // corrects -- and still commits nothing after the mount.
    EditorHarness h;
    h.store.set_value(spectr::kParamBandCount, 48.0f);
    h.store.set_value(spectr::band_gain_param_id(0), 12.0f);
    REQUIRE(h.processor.apply_surface_params(false));
    h.open_view(/*deferred=*/true);
    for (int frame = 0; frame < 120; ++frame) h.frame();
    REQUIRE(h.bridge() != nullptr);
    auto& bridge = *h.bridge();
    INFO("commit stats: " << read_js(bridge, "JSON.stringify(globalThis.__pulpCommitStats__)"));
    CHECK(read_js_int(bridge, "globalThis.__pulpCommitStats__.commits")
          - read_js_int(bridge, "globalThis.__pulpCommitStats__.mount_commits") <= 1);
    INFO("processor n: " << read_js(bridge, "JSON.parse(globalThis.__spectrEditorDispatch(JSON.stringify({type:'processing_state_get',payload:{},id:'t'}))).n_visible"));
    CHECK(read_js_int(bridge, "globalThis.__spectrTestHooks.appState().settings.bandCount") == 48);
    CHECK(read_js_int(bridge, "globalThis.__spectrTestHooks.renderState().nVisible") == 48);
    CHECK(read_js(bridge,
        "globalThis.__spectrTestHooks.renderState().targetGains[0].toFixed(3)") == "0.500");
}
