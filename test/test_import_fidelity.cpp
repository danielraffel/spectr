#include <catch2/catch_test_macros.hpp>

#include "spectr_editor_assets_data.hpp"

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
    "22a4a7d78433a20edfc5eee3e2d7b1401b07840457e761e12a0ce45dcad290a6";
constexpr std::string_view kAdapterDigest =
    "d99c9861e9928cc8737d612c7f1068112f55e6a476cdc20b529a2a2a65a1ddd9";

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
};

constexpr std::array kGestureMarkers{
    ContractMarker{"drag-threshold", "      if (Math.abs(dx) > 3 || Math.abs(dy) > 3) p.didDrag = true;"},
    ContractMarker{"shift-selection", "      pointerRef.current = { mode: 'shift-select', band };"},
};

constexpr std::array kResizeMarkers{
    ContractMarker{"fixed-design-canvas", "width: ${editorGeometry.designWidth}px !important;", 2},
    ContractMarker{"resize-geometry", "const editorGeometry = Object.freeze({"},
    ContractMarker{"resize-grip-hit-target", "resizeGrip.id = 'spectr-resize-grip';"},
    ContractMarker{"resize-grip-pointer-capture", "resizeGrip.setPointerCapture(event.pointerId);"},
    ContractMarker{"preserving-native-request", "window.pulp.postMessage('editor_resize_request', payload,"},
    ContractMarker{"resize-selection-guard", "user-select:none', '-webkit-user-select:none'"},
    ContractMarker{"resize-text-selection", "input:not([type]), input[type=\"text\"], input[type=\"search\"], textarea,"},
    ContractMarker{"resize-sequence", "const sequence = ++resizeSequence;"},
    ContractMarker{"resize-coalescing", "queuedResize = request;"},
    ContractMarker{"resize-refusal", "showResizeFailure('HOST REFUSED RESIZE')"},
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
    REQUIRE(bundle->template_html.size() == 186251);

    const auto canonical = canonicalize(*bundle);
    CHECK(canonical.asset_set_digest == kAssetSetDigest);
    CHECK(canonical.template_digest == kTemplateDigest);

    const auto adapter = outer_adapter(html);
    REQUIRE_FALSE(adapter.empty());
    CHECK(pulp::runtime::sha256_hex(adapter) == kAdapterDigest);
    CHECK(adapter.find("throw new Error('Spectr source patch point missing: ' + label)") != adapter.npos);
    CHECK(adapter.find("new DOMParser().parseFromString(template, 'text/html')") != adapter.npos);
    CHECK(adapter.find("window.Babel.transformScriptTags()") != adapter.npos);
    CHECK(adapter.find("window.pulp.postMessage('processing_state_set'") != adapter.npos);
    CHECK(adapter.find("window.pulp.on('processing_state_hydrate'") != adapter.npos);
    CHECK(adapter.find("window.pulp.on(\n        'analyzer_frame', acceptAnalyzerFrame)")
          != adapter.npos);
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
    CHECK(adapter.find("document.activeElement.click()") != adapter.npos);
    CHECK(adapter.find("fixedDesignSurface.style.transform") != adapter.npos);
    CHECK(adapter.find("wrapRef.current.clientWidth / rect.width") != adapter.npos);
    CHECK(adapter.find("static engine identity badge") != adapter.npos);
    CHECK(adapter.find("stable settings controls") != adapter.npos);
    CHECK(adapter.find("settings close hit target") != adapter.npos);
    CHECK(adapter.find("minimap clears bottom action rail") != adapter.npos);
    CHECK(adapter.find("minimap behavior oracle seam") != adapter.npos);
    CHECK(adapter.find("canvas interaction does not select text") != adapter.npos);
    CHECK(adapter.find("native snapshot authority helpers") != adapter.npos);
    CHECK(adapter.find("single-authority snapshot recall and morph") != adapter.npos);
    CHECK(adapter.find("center-origin unmute pulse") != adapter.npos);
    CHECK(adapter.find("independent analyzer dBFS ruler") != adapter.npos);
    CHECK(adapter.find("scale() {") != adapter.npos);
    CHECK(adapter.find("data-spectr-settings-panel") != adapter.npos);
    CHECK(adapter.find("data-spectr-morph") != adapter.npos);
    CHECK(adapter.find("deterministic canvas first paint") != adapter.npos);
    CHECK(adapter.find("deterministic canvas paint after sizing") != adapter.npos);
    CHECK(adapter.find("const renderAllRef = useRef(null)") != adapter.npos);
    CHECK(adapter.find("if (renderAllRef.current) renderAllRef.current()") != adapter.npos);
    CHECK(adapter.find("renderAllRef.current = renderAll") != adapter.npos);
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
    CHECK(adapter.find("data-spectr-overlay=\"true\" role=\"menu\" aria-label=\"Band actions\"")
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
    CHECK(resize_errors(adapter).empty());
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

TEST_CASE("import adapter contract detects proportional resize mutations") {
    const auto source = outer_adapter(embedded_html());
    REQUIRE_FALSE(source.empty());
    for (const auto& marker : kResizeMarkers) {
        INFO(marker.label);
        auto mutated = source;
        erase_once(mutated, marker.text);
        CHECK(contains(resize_errors(mutated), marker.label));
    }
}
