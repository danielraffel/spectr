#include <catch2/catch_test_macros.hpp>

#include "spectr_editor_assets_data.hpp"
#ifdef SPECTR_NATIVE_EDITOR
#include "spectr_native_assets_data.hpp"
#endif

#include <pulp/runtime/crypto.hpp>
#include <pulp/view/design_sources.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

// These values were produced from the immutable Claude Design export after
// decoding/gunzipping its assets. UUIDs are deliberately excluded: Claude
// regenerates them even when the payload is byte-for-byte equivalent.
constexpr std::string_view kAssetSetDigest =
    "6215ee5a9f65ade3626e63c4f973e579f123625239ba57c8f5db61121ccc5e0a";
constexpr std::string_view kTemplateDigest =
    "837fe1182d68abab5944570cd35bea85a2e5d10c6ef8d524a6e7e65b83caca9e";
constexpr std::string_view kAdapterDigest =
        "be530437fac55f44ca401cb31bc5cc2ebc970a202597e4791107b1905fc4a103";

struct CanonicalBundle {
    std::string asset_set_digest;
    std::string template_digest;
};

std::string embedded_html() {
    return {reinterpret_cast<const char*>(spectr_editor::editor_html),
            spectr_editor::editor_html_size};
}

std::size_t count_occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t pos = 0; (pos = text.find(needle, pos)) != text.npos;
         pos += needle.size())
        ++count;
    return count;
}

void replace_all(std::string& text, std::string_view needle,
                 std::string_view replacement) {
    for (std::size_t pos = 0; (pos = text.find(needle, pos)) != text.npos;) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

CanonicalBundle canonicalize(const pulp::view::ClaudeBundle& bundle) {
    struct AssetRow {
        std::string uuid;
        std::string hash;
        std::string mime;
        std::size_t size;
    };
    std::vector<AssetRow> rows;
    std::string normalized = bundle.template_html;
    for (const auto& asset : bundle.assets) {
        const auto hash = pulp::runtime::sha256_hex(asset.data.data(), asset.data.size());
        rows.push_back({asset.uuid, hash, asset.mime, asset.data.size()});
        replace_all(normalized, asset.uuid, "asset:sha256:" + hash);
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return std::tie(a.hash, a.mime, a.size) < std::tie(b.hash, b.mime, b.size);
    });
    std::string asset_set;
    for (const auto& row : rows) {
        if (!asset_set.empty()) asset_set.push_back('\n');
        asset_set += row.hash + "\t" + row.mime + "\t" + std::to_string(row.size);
    }
    return {pulp::runtime::sha256_hex(asset_set),
            pulp::runtime::sha256_hex(normalized)};
}

std::string outer_adapter(std::string_view html) {
    constexpr std::string_view open = "<script>";
    constexpr std::string_view close = "</script>";
    const auto begin = html.find(open);
    if (begin == html.npos) return {};
    const auto content = begin + open.size();
    const auto end = html.find(close, content);
    if (end == html.npos) return {};
    return std::string(html.substr(content, end - content));
}

struct ContractMarker {
    std::string_view label;
    std::string_view text;
    std::size_t expected_count = 1;
};

constexpr std::array kStructureMarkers{
    ContractMarker{"footer:Chrome", "function Chrome("},
    ContractMarker{"filter-bank", "function FilterBank("},
    ContractMarker{"popup:ContextMenu", "function ContextMenu("},
    ContractMarker{"popup:EditModePopover", "function EditModePopover("},
    ContractMarker{"popup:AnalyzerPopover", "function AnalyzerPopover("},
    ContractMarker{"popup:PickerDropdown", "function PickerDropdown("},
    ContractMarker{"popup:ThemeDropdown", "function ThemeDropdown("},
    ContractMarker{"popup:MetaphorDropdown", "function MetaphorDropdown("},
    ContractMarker{"popup:HelpPopover", "function HelpPopover("},
};

constexpr std::array kPatchNeedles{
    ContractMarker{"filter-signature", "function FilterBank({ settings, onStateChange, sharedState, onStatus, dspMode, editMode, analyzerMode, onEditModeChange }) {"},
    ContractMarker{"gain-refs", "  const targetGainsRef = useRef(new Array(N).fill(0));\n  const renderGainsRef = useRef(new Array(N).fill(0));"},
    ContractMarker{"bank-ref", "  const bankRef = useAppR(null);\n\n  const [info, setInfo] = useAppS({ N: settings.bandCount, zoom: '1.00' });"},
    ContractMarker{"default-pattern", "  // Load default pattern once on mount\n  useAppE(() => {\n    const t = setTimeout(() => {"},
    ContractMarker{"chrome-signature", "function Chrome({ settings, setSettings, bankRef, info, status, dspMode, setDspMode, editMode, setEditMode, analyzerMode, setAnalyzerMode, snapshotStatus, patterns, onApplyPattern, onOpenPatternManager, onClearAll, onResetAll, allMuted }) {"},
    ContractMarker{"zoom-label", "<span className=\"tnum\">{info.zoom}× zoom</span>"},
    ContractMarker{"chrome-call", "      <Chrome\n        settings={settings}"},
    ContractMarker{"dsp-mode-ref", "const dspModeRef = useRef(dspMode || 'iir');"},
    ContractMarker{"dsp-state", "const [dspMode, setDspMode] = useAppS('iir');"},
    ContractMarker{"band-count", "  \"bandCount\": 64,"},
    ContractMarker{"chrome-menu-state", "  const setAnalyzerMenu = (v) => setOpenMenu(typeof v === 'function' ? (v(analyzerMenu) ? 'analyzer' : null) : (v ? 'analyzer' : null));"},
    ContractMarker{"bands-menu-root", "<div style={{ position: 'relative' }}>\n            <button onClick={() => setBandsMenu(v => !v)} style={{"},
    ContractMarker{"settings-picker", "function PickerDropdown({ value, options, onChange, placeholder, renderPreview, renderOption, width = 260 }) {\n  const [open, setOpen] = React.useState(false);"},
    ContractMarker{"settings-modal", "function SettingsModal({ settings, setSettings, onClose }) {\n  const persist = (patch) => {"},
    ContractMarker{"pattern-manager", "  const [showImport, setShowImport] = usePM(false);\n\n  const factory = window.Spectr.FACTORY_PATTERNS;"},
    ContractMarker{"mute-timing-state", "  const lastTapRef = useRef(null); // { band, t } — for double-tap-to-mute detection"},
    ContractMarker{"mute-render-transition", "          rg[i] = smooth(rg[i], -1.02, dt * 26);\n          if (rg[i] < -1.01) rg[i] = -Infinity; // latch"},
    ContractMarker{"finite-canvas-gains", "    const effectiveGains = rg;"},
    // Every band gain reaches a canvas Y through gainToY. These pin the three
    // projection sites the adapter rewrites; drift in any of them silently
    // restores an axis-floor or non-finite coordinate for a muted band.
    ContractMarker{"gain-projector", "function gainToY(g, zeroY, halfH) {\n  if (isMuted(g)) return zeroY + halfH; // bottom\n  return zeroY - g * halfH;\n}"},
    ContractMarker{"band-body-projection", "      const topY = zeroY - Math.max(gval, 0) * halfH;\n      const botY = zeroY - Math.min(gval, 0) * halfH;"},
    ContractMarker{"response-spline-projection", "        y: zeroY - effectiveGains[i] * halfH,"},
    ContractMarker{"band-geometry-seam", "        isSel: selection.has(i),\n      };\n    }"},
    ContractMarker{"mute-pointer-up", "      // A quick double-tap (same band, <350ms since last tap) toggles mute.\n      // Single taps do nothing — prevents accidental muting."},
    ContractMarker{"tap-jitter-boundary", "      if (Math.abs(dx) > 3 || Math.abs(dy) > 3) p.didDrag = true;\n      const curBand = findBand(x, g);"},
    ContractMarker{"shortcut-ownership", "      's': 'sculpt', 'l': 'level', 'b': 'boost', 'f': 'flare', 'g': 'glide'"},
    ContractMarker{"shortcut-hints", "hint: 'S'", 2},
    ContractMarker{"help-shortcuts", "      <Hrow k=\"S / L / B\">Sculpt · Level · Boost</Hrow>"},
    ContractMarker{"help-mute", "      <Hrow k=\"DBL-CLICK\">Toggle mute (−∞)</Hrow>"},
    ContractMarker{"gain-pointer-origin", "      mode: 'gain',\n      editMode: editModeRef.current,\n      startX: x, startY: y,\n      lastX: x, lastY: y,"},
    ContractMarker{"context-menu-overlay", "    <div ref={ref}\n      style={{"},
    ContractMarker{"context-menu-items", "  const Item = ({ label, hint, onClick, disabled, danger, sub }) => (\n    <button\n      onClick="},
    ContractMarker{"canvas-geometry", "const r = wrap.getBoundingClientRect();", 2},
    ContractMarker{"canvas-render-ref", "  const rafRef = useRef(0);\n  const timeRef = useRef(0);"},
    ContractMarker{"canvas-first-paint", "    rafRef.current = requestAnimationFrame(draw);\n    return () => cancelAnimationFrame(rafRef.current);"},
    ContractMarker{"canvas-sized-paint", "        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);\n      }\n    };\n    resize();"},
    ContractMarker{"canvas-render-publication", "  }, [view, N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, hover, marquee, selection, snapshots, morph, dspMode]);\n\n  function drawGrid(ctx, g) {"},
    // The animation loop pins whichever renderAll existed when its effect last
    // ran, and its dependency list omits the display-mode selector. The adapter
    // rewrites this call to go through renderAllRef instead.
    ContractMarker{"canvas-loop-paint", "      renderAll();\n      rafRef.current = requestAnimationFrame(draw);\n    };"},
    // The hover readout floats above the cursor and the status banner owns a
    // fixed slot at the top of the plot; the adapter raises this lower bound so
    // the two never paint over one another.
    ContractMarker{"hover-readout-clamp", "    const ty = clamp(y - 30, g.inner.y + 2, g.inner.y + g.inner.h);"},
    // Six drawn-edit sites, each of which decided mute for itself: SCULPT and
    // LEVEL simply wrote a finite gain, BOOST, FLARE, GLIDE and the group drag
    // short-circuited to the sentinel. The adapter routes all six through one
    // decision point, so drift in any of them silently restores a per-mode
    // policy and the "Redraw unmutes" setting stops meaning anything.
    ContractMarker{"edit-mode-ref", "  const editModeRef = useRef(editMode || 'sculpt');"},
    ContractMarker{"bank-settings-destructure", "  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, motionMode, showMinimap, showRulers, theme } = settings;"},
    ContractMarker{"mute-policy-group", "        for (const [i, v0] of p.groupStart.entries()) {\n          if (isMuted(v0)) { map.set(i, -Infinity); continue; }\n          map.set(i, clamp(v0 + delta, -1, 1));\n        }\n        commitMany(map);"},
    ContractMarker{"mute-policy-sculpt", "        map.set(curBand, newG);\n        commitMany(map);"},
    ContractMarker{"mute-policy-level", "        for (const b of p.paintedBands) map.set(b, newG); // all painted follow\n        commitMany(map);"},
    ContractMarker{"mute-policy-boost", "          const v0 = p.startSnap[b];\n          if (isMuted(v0)) { map.set(b, -Infinity); continue; }\n          map.set(b, clamp(v0 * k, -1, 1));"},
    ContractMarker{"mute-policy-flare", "          const v0 = p.startSnap[b];\n          if (isMuted(v0)) { map.set(b, -Infinity); continue; }\n          const sign = v0 >= 0 ? 1 : -1;"},
    ContractMarker{"mute-policy-glide", "          const v0 = p.startSnap[i];\n          if (isMuted(v0)) { map.set(i, -Infinity); continue; }\n          map.set(i, clamp(v0 + (newG - v0) * w, -1, 1));"},
    ContractMarker{"overflow-fit-view", "              <button onClick={() => { act(b => b.resetView())(); setOverflowMenu(false); }} style={menuItem}>FIT VIEW</button>"},
};

constexpr std::array kGestureMarkers{
    ContractMarker{"drag-threshold", "      if (Math.abs(dx) > 3 || Math.abs(dy) > 3) p.didDrag = true;"},
    ContractMarker{"shift-selection", "      pointerRef.current = { mode: 'shift-select', band };"},
};

constexpr std::array kResizeMarkers{
    ContractMarker{"fixed-design-canvas", "width: ${editorGeometry.designWidth}px !important;", 2},
    ContractMarker{"resize-geometry", "const editorGeometry = Object.freeze({"},
    ContractMarker{"native-viewport-fit", "const scale = Math.min("},
    ContractMarker{"native-viewport-resize", "window.addEventListener('resize', resizeFixedDesign);"},
    ContractMarker{"fixed-design-center", "'translate(-50%, -50%) scale(' + scale + ')'"},
    ContractMarker{"resize-text-selection", "input:not([type]), input[type=\"text\"], input[type=\"search\"], textarea,"},
    ContractMarker{"live-viewport-ref", "const [reactView, setReactView] = useState(initialView);"},
    ContractMarker{"live-left-right-resize", "commitLiveViewport({ lmin, lmax });"},
    ContractMarker{"live-center-pan", "commitLiveViewport({ lmin, lmax: lmin + span });"},
    ContractMarker{"final-viewport-snapshot", "setView({ ...viewRef.current });\n      wrapRef.current.style.cursor = p.mode === 'minimap-resize' ? 'col-resize' : 'grab';"},
    ContractMarker{"viewport-oracle-snapshot", "reactView: { ...reactView }"},
};

constexpr std::array kForbiddenProductResizeMarkers{
    ContractMarker{"product-resize-grip", "spectr-resize-grip"},
    ContractMarker{"product-resize-status", "spectr-resize-status"},
    ContractMarker{"product-resize-request", "editor_resize_request"},
    ContractMarker{"product-resize-pointer-capture", "resizeGrip.setPointerCapture"},
};

std::vector<std::string> structure_errors(std::string_view source) {
    std::vector<std::string> errors;
    if (count_occurrences(source, "<canvas") != 3) errors.emplace_back("canvas-count");
    for (const auto& marker : kStructureMarkers)
        if (count_occurrences(source, marker.text) != marker.expected_count)
            errors.emplace_back(marker.label);
    return errors;
}

std::vector<std::string> patch_errors(std::string_view source) {
    std::vector<std::string> errors;
    for (const auto& marker : kPatchNeedles)
        if (count_occurrences(source, marker.text) != marker.expected_count)
            errors.emplace_back(marker.label);
    return errors;
}

std::vector<std::string> gesture_errors(std::string_view source) {
    std::vector<std::string> errors;
    for (const auto& marker : kGestureMarkers)
        if (count_occurrences(source, marker.text) != marker.expected_count)
            errors.emplace_back(marker.label);
    return errors;
}

std::vector<std::string> resize_errors(std::string_view source) {
    std::vector<std::string> errors;
    for (const auto& marker : kResizeMarkers)
        if (count_occurrences(source, marker.text) != marker.expected_count)
            errors.emplace_back(marker.label);
    for (const auto& marker : kForbiddenProductResizeMarkers)
        if (count_occurrences(source, marker.text) != 0)
            errors.emplace_back(marker.label);
    return errors;
}

bool contains(const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void erase_once(std::string& source, std::string_view needle) {
    const auto pos = source.find(needle);
    REQUIRE(pos != source.npos);
    source.erase(pos, needle.size());
}

} // namespace

TEST_CASE("import fidelity: embedded Claude payload and adapter match Release 1 oracle") {
    const auto html = embedded_html();
    const auto bundle = pulp::view::parse_claude_bundle(html);
    REQUIRE(bundle.has_value());
    REQUIRE(bundle->assets.size() == 16);
    REQUIRE(bundle->template_html.size() == 186383);

    const auto canonical = canonicalize(*bundle);
    CHECK(canonical.asset_set_digest == kAssetSetDigest);
    CHECK(canonical.template_digest == kTemplateDigest);

    const auto adapter = outer_adapter(html);
    REQUIRE_FALSE(adapter.empty());
    CHECK(pulp::runtime::sha256_hex(adapter) == kAdapterDigest);
    auto mutated_adapter = adapter;
    mutated_adapter.front() ^= 1;
    CHECK(pulp::runtime::sha256_hex(mutated_adapter) != kAdapterDigest);
    CHECK(adapter.find("throw new Error('Spectr source patch point missing: ' + label)") != adapter.npos);
    CHECK(adapter.find("new DOMParser().parseFromString(template, 'text/html')") != adapter.npos);
    CHECK(adapter.find("window.Babel.transformScriptTags()") != adapter.npos);
    CHECK(adapter.find("window.pulp.postMessage('processing_state_set'") != adapter.npos);
    CHECK(adapter.find("nativeDirectPublicationSignatureRef") != adapter.npos);
    CHECK(adapter.find(
        "publicationSignature === nativeDirectPublicationSignatureRef.current")
          != adapter.npos);
    CHECK(adapter.find("window.pulp.on('processing_state_hydrate'") != adapter.npos);
    CHECK(adapter.find("window.pulp.on(\n        'analyzer_frame', acceptAnalyzerFrame)")
          != adapter.npos);
    CHECK(adapter.find("data-spectr-pattern-manage") != adapter.npos);
    CHECK(adapter.find("data-spectr-filter-canvas") != adapter.npos);
    CHECK(adapter.find("const StableContextMenu = React.memo(ContextMenu")
          != adapter.npos);
    CHECK(adapter.find("<StableContextMenu") != adapter.npos);
    CHECK(adapter.find("if (!nativeAnalyzerFrame) return 0") != adapter.npos);
    CHECK(adapter.find("window.SpectrAnalyzer.sample(lf, t, 'visible')")
          != adapter.npos);
    CHECK(adapter.find("window.SpectrAnalyzer.sample(lf, tNow, 'visible')")
          != adapter.npos);
    CHECK(adapter.find("window.SpectrAnalyzer.sample(lf, timeRef.current, 'overview')")
          != adapter.npos);
    CHECK(adapter.find("window.SpectrSignal.sample(logFrequency, time)")
          != adapter.npos);
    CHECK(adapter.find("nativeAnalyzerFrame = null") != adapter.npos);
    CHECK(adapter.find("rejected malformed native analyzer frame") != adapter.npos);
    CHECK(adapter.find("data-spectr-menu-root") != adapter.npos);
    // Browser previews do not run through Pulp's native semantic popup
    // dispatcher, so the authored adapter retains a browser-only fallback.
    // The materialization patcher removes it from the shipping native document,
    // whose popupKind assertions below pin native Pulp ownership.
    CHECK(count_occurrences(adapter, "document.activeElement.click()") == 1);
    CHECK(adapter.find("Browser previews need this contract locally") != adapter.npos);
    CHECK(adapter.find("data-spectr-menu-trigger aria-haspopup=\"listbox\"")
          != adapter.npos);
    CHECK(adapter.find("data-spectr-menu-options data-spectr-overlay=\"true\"")
          != adapter.npos);
    CHECK(adapter.find("fixedDesignSurface.style.transform") != adapter.npos);
    CHECK(adapter.find("wrapRef.current.clientWidth / rect.width") != adapter.npos);
    CHECK(adapter.find("truthful mask visualization selector") != adapter.npos);
    CHECK(adapter.find("finite hover gain display") != adapter.npos);
    CHECK(adapter.find("deterministic shift mute brush start") != adapter.npos);
    CHECK(adapter.find("stable settings controls") != adapter.npos);
    CHECK(adapter.find("settings close hit target") != adapter.npos);
    CHECK(adapter.find("frequency ruler and minimap clear fixed chrome") != adapter.npos);
    CHECK(adapter.find("minimap behavior oracle seam") != adapter.npos);
    CHECK(adapter.find("canvas interaction does not select text") != adapter.npos);
    CHECK(adapter.find("native snapshot authority helpers") != adapter.npos);
    CHECK(adapter.find("single-authority snapshot recall and morph") != adapter.npos);
    CHECK(adapter.find("center-origin unmute pulse") != adapter.npos);
    CHECK(adapter.find("finite canvas gain geometry") != adapter.npos);
    CHECK(adapter.find("finite hover gain display") != adapter.npos);
    CHECK(adapter.find("guarded band gain projection") != adapter.npos);
    CHECK(adapter.find("band body geometry uses guarded projection") != adapter.npos);
    CHECK(adapter.find("response spline uses guarded projection") != adapter.npos);
    CHECK(adapter.find("band body geometry oracle seam") != adapter.npos);
    // A muted band must not be projected to the axis floor. The adapter both
    // removes the floor return from gainToY and stops drawMaskResponse from
    // reintroducing it, so neither form may survive in the emitted source.
    CHECK(adapter.find("if (!Number.isFinite(g)) return zeroY;") != adapter.npos);
    CHECK(adapter.find("return zeroY - clamp(g, -1.02, 1.02) * halfH;") != adapter.npos);
    CHECK(adapter.find("gainToY(isMuted(tg[i]) ? 0 : rg[i], g.zeroY, g.halfH)")
          != adapter.npos);
    CHECK(adapter.find("gainToY(Math.max(gval, 0), zeroY, halfH)") != adapter.npos);
    CHECK(adapter.find("gainToY(effectiveGains[i], zeroY, halfH)") != adapter.npos);
    CHECK(count_occurrences(adapter, "isMuted(tg[i]) ? g.zeroY + g.halfH") == 0);
    CHECK(count_occurrences(adapter, "if (isMuted(g)) return zeroY + halfH") == 1);
    CHECK(adapter.find("Never seed a synchronous WebKit paint") != adapter.npos);
    CHECK(adapter.find("independent analyzer dBFS ruler") != adapter.npos);
    CHECK(adapter.find("scale() {") != adapter.npos);
    CHECK(adapter.find("data-spectr-settings-panel") != adapter.npos);
    CHECK(adapter.find("data-spectr-morph") != adapter.npos);
    CHECK(adapter.find("deterministic canvas first paint") != adapter.npos);
    CHECK(adapter.find("deterministic canvas paint after sizing") != adapter.npos);
    CHECK(adapter.find("const renderAllRef = useRef(null)") != adapter.npos);
    CHECK(adapter.find("if (renderAllRef.current) renderAllRef.current()") != adapter.npos);
    CHECK(adapter.find("renderAllRef.current = renderAll") != adapter.npos);
    // The loop must paint the latest renderer, or a display-only state change
    // (BARS / RESPONSE / BOTH) never reaches the canvas until an unrelated
    // dependency remounts the loop.
    CHECK(adapter.find("animation loop paints the latest canvas renderer")
          != adapter.npos);
    CHECK(adapter.find("(renderAllRef.current || renderAll)()") != adapter.npos);
    // One message at a time: the readout clears the banner's slot, and the
    // banner replaces its own text rather than letting two messages stack.
    CHECK(adapter.find("hover readout clears the status banner slot") != adapter.npos);
    CHECK(adapter.find("clamp(y - 30, g.inner.y + 20, g.inner.y + g.inner.h)")
          != adapter.npos);
    CHECK(adapter.find("status banner replaces one message at a time") != adapter.npos);
    CHECK(adapter.find("shownRef.current !== display") != adapter.npos);
    // One mute policy for drawing, consulted by every mode.
    CHECK(adapter.find("redraw-unmutes overflow toggle") != adapter.npos);
    CHECK(adapter.find("data-spectr-redraw-unmutes") != adapter.npos);
    CHECK(adapter.find("const commitDrawnGains = (map) => {") != adapter.npos);
    CHECK(adapter.find("const editBaseGain = (value, index) => {") != adapter.npos);
    CHECK(count_occurrences(adapter, "commitDrawnGains(map);") == 6);
    CHECK(adapter.find("defers the mute decision") != adapter.npos);
    CHECK(adapter.find("Apply synchronously when it is ready") != adapter.npos);
    CHECK(adapter.find("-Infinity is categorical state, never an interpolation operand")
          != adapter.npos);
    CHECK(adapter.find("if (!isMuted(rg[i]))") != adapter.npos);
    CHECK(adapter.find("if (isMuted(rg[i]) || !Number.isFinite(rg[i])) rg[i] = 0")
          != adapter.npos);
    CHECK(adapter.find("unmutePulseRef.current[i] - dt * 3.5") != adapter.npos);
    CHECK(adapter.find("const restored = Number.isFinite(authoredDb)") != adapter.npos);
    CHECK(adapter.find("p.mode === 'gain' && p.button === 0 && !p.didDrag")
          != adapter.npos);
    CHECK(adapter.find("commitGain(p.band, isMuted(cur) ? restored : -Infinity)")
          != adapter.npos);
    CHECK(adapter.find("startClientX: e.clientX, startClientY: e.clientY")
          != adapter.npos);
    CHECK(adapter.find("button: e.button") != adapter.npos);
    CHECK(adapter.find("const clientDx = e.clientX - p.startClientX") != adapter.npos);
    CHECK(adapter.find("if (!p.didDrag) return;\n      const curBand = findBand(x, g);")
          != adapter.npos);
    CHECK(adapter.find("'1': 'sculpt', '2': 'level', '3': 'boost', '4': 'flare', '5': 'glide'")
          != adapter.npos);
    CHECK(adapter.find("e.isComposing || e.keyCode === 229 || e.repeat")
          != adapter.npos);
    CHECK(adapter.find("e.metaKey || e.ctrlKey || e.altKey || e.shiftKey")
          != adapter.npos);
    CHECK(adapter.find("document.querySelector('[data-spectr-overlay=\"true\"]')")
          != adapter.npos);
    CHECK(adapter.find("data-spectr-band-context-menu=\"true\" role=\"menu\" aria-label=\"Band actions\"")
          != adapter.npos);
    CHECK(adapter.find("<button role=\"menuitem\"") != adapter.npos);
    CHECK(adapter.find("<Hrow k=\"CLICK\">Toggle mute (−∞)</Hrow>")
          != adapter.npos);
    CHECK(adapter.find("<Hrow k=\"1 / 2 / 3\">Sculpt · Level · Boost</Hrow>")
          != adapter.npos);
    CHECK(adapter.find("<Hrow k=\"4 / 5\">Flare · Glide</Hrow>")
          != adapter.npos);
    CHECK(adapter.find("<Hrow k=\"6\">Cycle analyzer</Hrow>") != adapter.npos);
    CHECK(adapter.find("\"hint: 'S'\", \"hint: '1'\"") != adapter.npos);
    CHECK(adapter.find("\"hint: 'G'\", \"hint: '5'\"") != adapter.npos);
    CHECK(adapter.find("replaceAllSpectrSource(\"hint: 'S'\"") != adapter.npos);
    // The old terms occur only inside the exact source needles being removed;
    // the adapter digest and desired replacements above pin the emitted path.
    CHECK(count_occurrences(adapter, "lastTapRef.current") == 3);
    CHECK(count_occurrences(adapter, "'s': 'sculpt'") == 1);

    CHECK(structure_errors(bundle->template_html).empty());
    CHECK(patch_errors(bundle->template_html).empty());
    CHECK(gesture_errors(bundle->template_html).empty());
    const auto resize_contract_errors = resize_errors(adapter);
    for (const auto& error : resize_contract_errors) {
        INFO("resize contract error: " << error);
        CHECK(error.empty());
    }
    CHECK(resize_contract_errors.empty());
}

TEST_CASE("import fidelity oracle detects payload mutations") {
    auto bundle = pulp::view::parse_claude_bundle(embedded_html());
    REQUIRE(bundle.has_value());
    bundle->template_html[0] ^= 1;
    CHECK(canonicalize(*bundle).template_digest != kTemplateDigest);
    bundle = pulp::view::parse_claude_bundle(embedded_html());
    REQUIRE(bundle.has_value());
    REQUIRE_FALSE(bundle->assets.front().data.empty());
    bundle->assets.front().data.front() ^= 1;
    CHECK(canonicalize(*bundle).asset_set_digest != kAssetSetDigest);
}

TEST_CASE("import structure contract detects missing canvas footer and popup") {
    const auto bundle = pulp::view::parse_claude_bundle(embedded_html());
    REQUIRE(bundle.has_value());
    SECTION("canvas") {
        auto source = bundle->template_html;
        erase_once(source, "<canvas");
        CHECK(contains(structure_errors(source), "canvas-count"));
    }
    SECTION("footer") {
        auto source = bundle->template_html;
        erase_once(source, "function Chrome(");
        CHECK(contains(structure_errors(source), "footer:Chrome"));
    }
    SECTION("popup") {
        auto source = bundle->template_html;
        erase_once(source, "function EditModePopover(");
        CHECK(contains(structure_errors(source), "popup:EditModePopover"));
    }
}

TEST_CASE("import adapter contract detects pinned core source-needle drift") {
    const auto bundle = pulp::view::parse_claude_bundle(embedded_html());
    REQUIRE(bundle.has_value());
    for (const auto& marker : kPatchNeedles) {
        INFO(marker.label);
        auto source = bundle->template_html;
        erase_once(source, marker.text);
        CHECK(contains(patch_errors(source), marker.label));
    }
}

TEST_CASE("import gesture contract detects drag and Shift-selection mutations") {
    const auto bundle = pulp::view::parse_claude_bundle(embedded_html());
    REQUIRE(bundle.has_value());
    for (const auto& marker : kGestureMarkers) {
        INFO(marker.label);
        auto source = bundle->template_html;
        erase_once(source, marker.text);
        CHECK(contains(gesture_errors(source), marker.label));
    }
}

TEST_CASE("import adapter contract detects native viewport resize mutations") {
    const auto source = outer_adapter(embedded_html());
    REQUIRE_FALSE(source.empty());
    for (const auto& marker : kResizeMarkers) {
        INFO(marker.label);
        auto mutated = source;
        erase_once(mutated, marker.text);
        CHECK(contains(resize_errors(mutated), marker.label));
    }
    for (const auto& marker : kForbiddenProductResizeMarkers) {
        INFO(marker.label);
        auto mutated = source;
        mutated += marker.text;
        CHECK(contains(resize_errors(mutated), marker.label));
    }
}

#ifdef SPECTR_NATIVE_EDITOR

// The shipping editor is native-ui/materialized/materialized-document.runtime.json.
// tools/patch_materialized_editor.py is its idempotent compatibility recipe;
// these checks assert the emitted document as a separate shipping surface so a
// source-only fix cannot be mistaken for a native fix.
TEST_CASE("materialized editor document carries the adapter's editor fixes") {
    const std::string document{
        reinterpret_cast<const char*>(spectr_native::materialized_document_runtime_json),
        spectr_native::materialized_document_runtime_json_size};

    SECTION("the animation loop paints the latest canvas renderer") {
        // Without this the loop keeps calling the renderAll it captured when its
        // effect last ran, and its dependency list omits the display-mode
        // selector -- so BARS / RESPONSE / BOTH change nothing on screen until
        // an unrelated dependency happens to remount the loop.
        CHECK(count_occurrences(document, "(renderAllRef.current || renderAll)();") == 1);
        CHECK(count_occurrences(
                  document,
                  "      renderAll();\\n      rafRef.current = requestAnimationFrame(draw);")
              == 0);
    }

    SECTION("hover feedback uses the unified status banner") {
        CHECK(count_occurrences(document, "const hoverRef = useRef(null);") == 1);
        CHECK(count_occurrences(document, "const currentHover = hoverRef.current;") == 1);
        CHECK(count_occurrences(document,
                                "if (!currentHover || currentHover.mini) return;")
              == 1);
        CHECK(count_occurrences(document, "updateLiveHoverStatus();") == 1);
        CHECK(count_occurrences(document, "const tw = ctx.measureText(label).width + 18;") == 0);
    }

    SECTION("the status banner replaces one message at a time") {
        CHECK(count_occurrences(document, "const generationRef = useRefChrome(0);") == 1);
        CHECK(count_occurrences(document, "const statusRefreshAtRef = useRef(0);") == 1);
        CHECK(count_occurrences(
                  document,
                  "const t = setTimeout(() => {\\n      setVisible(false);\\n"
                  "      setText(\\\"\\\");\\n    }, 1400);")
              == 0);
        CHECK(document.find("if (generation !== generationRef.current) return;")
              != document.npos);
        CHECK(document.find("const timer2 = hide(120);") != document.npos);
        CHECK(document.find("now - statusRefreshAtRef.current >= 700")
              != document.npos);
        CHECK(document.find("statusRefreshAtRef.current = now;") != document.npos);
        CHECK(document.find("}, 150);") == document.npos);
        CHECK(document.find(
                  "const holdMs = /\\\\b(?:MUTED|UNMUTED)\\\\b/.test(display) ? 2000 : 1400;")
              != document.npos);
        CHECK(document.find(
                  "requestAnimationFrame(() => window.dispatchEvent(new Event(\\\"resize\\\")));")
              != document.npos);
        CHECK(document.find("shell.style.width = Math.max") == document.npos);
    }

    SECTION("the status banner is centered, padded, and smoothly content-sized") {
        CHECK(count_occurrences(
                  document,
                  "width: Math.max(96, Math.min(520, text.length * 8 + 28)),")
              == 1);
        CHECK(count_occurrences(document, "padding: \\\"0 14px\\\"") == 1);
        CHECK(count_occurrences(
                  document,
                  "transition: \\\"width 0.18s ease, opacity 0.15s ease\\\"")
              == 1);
        CHECK(count_occurrences(document, "width: 240,") == 0);
    }

    SECTION("live status text cannot invalidate layout when its content changes") {
        CHECK(count_occurrences(
                  document,
                  "style: { display: \\\"block\\\", textAlign: \\\"center\\\", "
                  "width: \\\"100%\\\", height: \\\"100%\\\", "
                  "lineHeight: \\\"26px\\\", whiteSpace: \\\"nowrap\\\" }")
              == 1);
        CHECK(count_occurrences(
                  document,
                  "data-spectr-status-text\": \"true\", style: { display: \"block\"")
              == 0);
    }

    SECTION("edge labels and dropdown controls retain intentional rendering") {
        CHECK(document.find("restoreMenuFocus") == document.npos);
        CHECK(count_occurrences(document, "popupKind: \\\"listbox\\\"") == 2);
        CHECK(count_occurrences(document, "popupKind: \\\"menu\\\"") == 2);
        CHECK(count_occurrences(document, "ctx.setLineDash([2, 2]);") == 0);
        CHECK(count_occurrences(
                  document,
                  "ctx.font = \\\"10px JetBrains Mono, monospace\\\";\\n"
                  "        ctx.textAlign = \\\"center\\\";\\n"
                  "        ctx.textBaseline = \\\"middle\\\";")
              == 1);
        CHECK(count_occurrences(
                  document,
                  "background: \\\"rgba(255,255,255,0.025)\\\",\\n"
                  "  border: \\\"1px solid transparent\\\"")
              == 1);
        CHECK(count_occurrences(
                  document,
                  "minWidth: 40,\\n"
                  "        minHeight: 26,\\n"
                  "        boxSizing: \\\"border-box\\\",\\n"
                  "        display: \\\"inline-flex\\\",\\n"
                  "        alignItems: \\\"center\\\",\\n"
                  "        justifyContent: \\\"center\\\",\\n"
                  "        lineHeight: 1")
              == 1);
    }

    SECTION("minimap cursor feedback distinguishes idle drag and band editing") {
        CHECK(count_occurrences(
                  document,
                  "wrapRef.current.style.cursor = mm === \\\"left\\\" || mm === \\\"right\\\" ? \\\"col-resize\\\" : \\\"grabbing\\\";")
              == 1);
        CHECK(count_occurrences(
                  document,
                  "wrapRef.current.style.cursor = \\\"crosshair\\\";")
              == 1);
        CHECK(count_occurrences(
                  document,
                  "mm === \\\"left\\\" || mm === \\\"right\\\"\\n        ? \\\"col-resize\\\"")
              == 1);
        CHECK(document.find("for (let i = 1; i < N; i++)") != document.npos);
        CHECK(document.find("for (let i = 0; i <= N; i++)") == document.npos);
    }

    SECTION("remaining standalone chrome stays aligned") {
        CHECK(count_occurrences(document, "top: 76,") == 1);
        CHECK(count_occurrences(
                  document,
                  "height: 26,\\n        display: \\\"inline-flex\\\",\\n"
                  "        alignItems: \\\"center\\\",\\n"
                  "        justifyContent: \\\"center\\\",\\n"
                  "        lineHeight: 1")
              == 1);
        CHECK(count_occurrences(
                  document,
                  "style: { marginLeft: 6, display: \\\"inline-flex\\\", "
                  "alignItems: \\\"center\\\", lineHeight: 1 }")
              == 3);
        CHECK(count_occurrences(
                  document,
                  "\"letter_spacing\":0.5}},\"boxes\":[{\"left\":0,"
                  "\"top\":3,\"width\":13,\"height\":13,"
                  "\"start\":0,\"length\":2}]},{\"index\":9")
              == 1);
    }

    SECTION("every edit mode defers its mute decision to one place") {
        // The behavioural proof is Spectr-browser-mute-modes, which drives all
        // five modes through their real pointer handlers. This is the emitted
        // -document half: a mode that kept a policy of its own would still make
        // that lane fail, but only here does the SHIPPING document say so.
        CHECK(count_occurrences(document, "const commitDrawnGains = (map) => {") == 1);
        CHECK(count_occurrences(document, "commitDrawnGains(map);") == 6);
        CHECK(count_occurrences(document, "commitMany(map, true);") == 1);
        CHECK(count_occurrences(document, "commitMany(held, true);") == 1);
        CHECK(count_occurrences(document, "if (!deferReact) setGains") == 2);
        CHECK(count_occurrences(document,
                  "setGains(targetGainsRef.current.slice());") == 1);
        CHECK(count_occurrences(document,
                  "reactGains: Array.from(gains)") == 1);
        CHECK(count_occurrences(document,
                  "updateLiveHoverStatus();") == 1);
        CHECK(count_occurrences(document,
                  "const commitLiveViewport = (next) => {") == 1);
        CHECK(count_occurrences(document,
                  "commitLiveViewport({ lmin, lmax });") == 1);
        CHECK(count_occurrences(document,
                  "commitLiveViewport({ lmin, lmax: lmin + span });") == 1);
        CHECK(count_occurrences(document,
                  "setView({ ...viewRef.current });") == 1);
        CHECK(count_occurrences(document,
                  "reactView: { ...reactView }") == 1);
        CHECK(count_occurrences(document, "const editBaseGain = (value, index) => {") == 1);
        CHECK(count_occurrences(document, "unmuteOnDrawRef.current") == 2);
        // No drawn-edit site may short-circuit a muted band to the sentinel
        // before the shared policy has decided anything.
        CHECK(count_occurrences(document, "map.set(b, -Infinity);") == 0);
        CHECK(count_occurrences(document, "map.set(i, -Infinity);\\n            continue;") == 0);
    }

    SECTION("the redraw-unmutes setting ships, its control deliberately does not") {
        // The default is `unmuteOnDraw !== false` at every read, not a key in
        // the captured #tweak-defaults block, so that node stays byte-identical.
        CHECK(count_occurrences(document, "unmuteOnDrawRef") == 3);
        CHECK(count_occurrences(document, "\\\"unmuteOnDraw\\\":") == 0);
        // The one place source and shipping document diverge on purpose. The
        // native materialized runtime paints a child ADDED to a container at
        // that container's first child position, on top of what is already
        // there -- reproduced by screenshot in the settings STRUCTURE group,
        // the settings MOTION group, and the overflow menu. resources/editor.html
        // carries the toggle so it appears the moment the document is
        // re-materialized; mirroring it today would ship an unreadable overlap.
        // If this starts failing because the control IS present, the document
        // was re-materialized -- delete this expectation, do not re-hide it.
        CHECK(count_occurrences(document, "data-spectr-redraw-unmutes") == 0);
        CHECK(count_occurrences(document, "REDRAW UNMUTES") == 0);
    }

    SECTION("a muted band still rests on the 0 dB line") {
        // Pins danielraffel/spectr#44 in the emitted document: the response
        // curve must project a muted band to zeroY, never to the axis floor.
        CHECK(count_occurrences(
                  document,
                  "const y = isMuted(tg[i]) ? g.zeroY : g.zeroY - rendered * g.halfH;")
              == 1);
        CHECK(count_occurrences(document, "isMuted(tg[i]) ? g.zeroY + g.halfH") == 0);
    }
}

TEST_CASE("materialized mode and visual contracts detect every severed fix") {
    const std::string document{
        reinterpret_cast<const char*>(spectr_native::materialized_document_runtime_json),
        spectr_native::materialized_document_runtime_json_size};
    constexpr std::array markers{
        ContractMarker{"bridge-message", R"(postMessage(\"mode_set\")"},
        ContractMarker{"motion", R"(spectrPublishMode(\"motion\")", 3},
        ContractMarker{"edit", R"(spectrPublishMode(\"edit\")", 3},
        ContractMarker{"analyzer", R"(spectrPublishMode(\"analyzer\")", 2},
        ContractMarker{"visualization", R"(spectrPublishMode(\"visualization\")"},
        ContractMarker{"native-listbox-popup-ownership", "popupKind: \\\"listbox\\\"", 2},
        ContractMarker{"native-menu-popup-ownership", "popupKind: \\\"menu\\\"", 2},
        ContractMarker{"pointer-owned-hover", "const currentHover = hoverRef.current;"},
        ContractMarker{"live-hover-publication", "updateLiveHoverStatus();"},
        ContractMarker{"guide-only-hover", "if (!currentHover || currentHover.mini) return;"},
        ContractMarker{"generation-safe-status", "const generationRef = useRefChrome(0);"},
        ContractMarker{"active-status-renewal", "now - statusRefreshAtRef.current >= 700"},
        ContractMarker{"inactivity-status-clear", "const timer2 = hide(120);"},
        ContractMarker{"longer-mute-status", "const holdMs = /\\\\b(?:MUTED|UNMUTED)\\\\b/.test(display) ? 2000 : 1400;"},
        ContractMarker{"content-sized-banner", "width: Math.max(96, Math.min(520, text.length * 8 + 28)),"},
        ContractMarker{"symmetric-banner-padding", "padding: \\\"0 14px\\\""},
        ContractMarker{"smooth-banner-resize", "transition: \\\"width 0.18s ease, opacity 0.15s ease\\\""},
        ContractMarker{"aligned-edge-label", "ctx.font = \\\"10px JetBrains Mono, monospace\\\";\\n        ctx.textAlign = \\\"center\\\";\\n        ctx.textBaseline = \\\"middle\\\";"},
        ContractMarker{"dropdown-surface", "background: \\\"rgba(255,255,255,0.025)\\\",\\n  border: \\\"1px solid transparent\\\""},
        ContractMarker{"aligned-band-count", "minWidth: 40,\\n        minHeight: 26,\\n        boxSizing: \\\"border-box\\\",\\n        display: \\\"inline-flex\\\",\\n        alignItems: \\\"center\\\",\\n        justifyContent: \\\"center\\\",\\n        lineHeight: 1"},
        ContractMarker{"native-band-count-spacing", "lineHeight: 1, paddingLeft: 4"},
        ContractMarker{"banner-below-plot-line", "top: 76,"},
        ContractMarker{"centered-rail-button", "height: 26,\\n        display: \\\"inline-flex\\\",\\n        alignItems: \\\"center\\\",\\n        justifyContent: \\\"center\\\",\\n        lineHeight: 1"},
        ContractMarker{"aligned-rail-chevrons", "style: { marginLeft: 6, display: \\\"inline-flex\\\", alignItems: \\\"center\\\", lineHeight: 1 }", 3},
        ContractMarker{"aligned-band-binding", "\"letter_spacing\":0.5}},\"boxes\":[{\"left\":0,\"top\":3,\"width\":13,\"height\":13,\"start\":0,\"length\":2}]},{\"index\":9"},
        ContractMarker{"band-dropdown-surface", "background: info.N === n ? \\\"rgba(120,180,255,0.18)\\\" : \\\"rgba(255,255,255,0.03)\\\","},
        ContractMarker{"edit-dropdown-surface", "background: active ? \\\"rgba(120,180,255,0.14)\\\" : \\\"rgba(255,255,255,0.025)\\\","},
        ContractMarker{"analyzer-dropdown-surface", "background: active ? \\\"rgba(255,255,255,0.08)\\\" : \\\"rgba(255,255,255,0.025)\\\","},
        ContractMarker{"settings-dropdown-surface", "background: active ? \\\"rgba(120,180,255,0.16)\\\" : \\\"rgba(255,255,255,0.025)\\\","},
        ContractMarker{"minimap-edge-resize-cursor", "mm === \\\"left\\\" || mm === \\\"right\\\"\\n        ? \\\"col-resize\\\""},
        ContractMarker{"minimap-press-cursor", "wrapRef.current.style.cursor = mm === \\\"left\\\" || mm === \\\"right\\\" ? \\\"col-resize\\\" : \\\"grabbing\\\";"},
        ContractMarker{"band-crosshair-cursor", "wrapRef.current.style.cursor = \\\"crosshair\\\";"},
        ContractMarker{"status-info-toggle", "data-spectr-status-info-toggle"},
        ContractMarker{"status-info-suppression", "settings.statusInfo === false", 3},
        ContractMarker{"selected-preset-label", "data-spectr-selected-preset"},
        ContractMarker{"selected-preset-identity", "const [selectedPatternId, setSelectedPatternId] = useAppS(null);"},
        ContractMarker{"selected-preset-authoritative-label", "[...window.Spectr.FACTORY_PATTERNS, ...userPatterns].find((pattern) => pattern.id === selectedPatternId)?.name || \"PRESETS\";"},
        ContractMarker{"selected-preset-applied-identity", "setSelectedPatternId(p.id);"},
        ContractMarker{"build-info-component", "function SpectrBuildInfo() {"},
        ContractMarker{"build-info-get", "postMessage(\\\"build_info_get\\\""},
        ContractMarker{"build-info-copy", "postMessage(\\\"build_info_copy\\\""},
        ContractMarker{"build-info-copy-success", "settleCopyState(\\\"COPIED\\\")"},
        ContractMarker{"build-info-copy-failure", "settleCopyState(\\\"COPY UNAVAILABLE\\\")"},
        ContractMarker{"build-info-unique-request-ids", "window.__spectrBuildInfoRequestSerial = (Number(window.__spectrBuildInfoRequestSerial) || 0) + 1;"},
        ContractMarker{"build-info-effect-replay-lifetime", "mountedRef.current = true;"},
        ContractMarker{"build-info-unmount-cleanup", "mountedRef.current = false;"},
        ContractMarker{"build-info-late-copy-guard", "if (!mountedRef.current) return;"},
        ContractMarker{"build-info-default-on", "\\\"showBuildInfo\\\": true"},
        ContractMarker{"build-info-optional", "settings.showBuildInfo !== false", 2},
        ContractMarker{"build-info-toggle", "data-spectr-build-info-toggle"},
        ContractMarker{"build-info-product-sha", "[\\\"SPECTR SHA\\\", info.product_sha || \\\"UNKNOWN\\\"]"},
        ContractMarker{"build-info-product-dirty", "info.product_provenance_known ? info.product_dirty ? \\\"DIRTY\\\" : \\\"CLEAN\\\" : \\\"UNKNOWN\\\""},
        ContractMarker{"build-info-sdk-dirty", "[\\\"SDK SOURCE\\\", info.sdk_provenance_exact ? info.sdk_dirty ? \\\"DIRTY\\\" : \\\"CLEAN\\\" : \\\"UNKNOWN\\\"]"},
    };
    const auto errors = [&](std::string_view candidate) {
        std::vector<std::string> result;
        for (const auto& marker : markers)
            if (count_occurrences(candidate, marker.text) != marker.expected_count)
                result.emplace_back(marker.label);
        return result;
    };
    for (const auto& marker : markers) {
        INFO(marker.label);
        CHECK(count_occurrences(document, marker.text) == marker.expected_count);
    }
    for (const auto& marker : markers) {
        INFO(marker.label);
        auto mutated = document;
        erase_once(mutated, marker.text);
        CHECK(contains(errors(mutated), marker.label));
    }
}

TEST_CASE("materialized build-info geometry contracts detect every severed fix") {
    const std::string runtime{
        reinterpret_cast<const char*>(spectr_native::runtime_js),
        spectr_native::runtime_js_size};
    constexpr std::array markers{
        ContractMarker{"build-info-feedback-slot", "g5.setFlex(String(feedbackId), \\\"height\\\", 108)"},
        ContractMarker{"build-info-stable-slot", "g5.setTop(String(aboutId), 774)"},
        ContractMarker{"build-info-provenance-height", "g5.setFlex(String(aboutId), \\\"height\\\", 252)"},
        ContractMarker{"build-info-scroll-extent", "const authoredContentHeight = 1044;"},
    };
    const auto errors = [&](std::string_view candidate) {
        std::vector<std::string> result;
        for (const auto& marker : markers)
            if (count_occurrences(candidate, marker.text) != marker.expected_count)
                result.emplace_back(marker.label);
        return result;
    };
    for (const auto& marker : markers) {
        INFO(marker.label);
        CHECK(count_occurrences(runtime, marker.text) == marker.expected_count);
    }
    for (const auto& marker : markers) {
        INFO(marker.label);
        auto mutated = runtime;
        erase_once(mutated, marker.text);
        CHECK(contains(errors(mutated), marker.label));
    }
}

#endif // SPECTR_NATIVE_EDITOR
