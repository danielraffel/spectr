#!/usr/bin/env python3
"""Give the `?` popover a way into the long-form help, and render that help.

THE GAP

    The `?` button opens a SHORTCUTS popover and nothing else.  A first-time
    user of a spectral filter bank has no route, anywhere in the product, to
    "what is this and how do I use it" -- only a keycap list that presumes the
    answer.  Measured on the shipping document before this script:
    `Learn more` 0, `helpModal` 0, `helpOverlay` 0, against a `SHORTCUTS`
    control of 1.

THE FIX, IN TWO HALVES THAT DELIBERATELY LIVE IN DIFFERENT FILES

    The COPY is `native-ui/materialized/help-content.js`.  It is its own
    editor asset: CMakeLists lists it in `SPECTR_NATIVE_ASSET_SOURCES`,
    native_editor.cpp writes it into the editor package beside runtime.js and
    evaluates it right after design.js, and it assigns one string to
    `globalThis.SPECTR_HELP_TEXT`.  Editing the help text is editing that file
    and nothing else.

    The RENDERER is here, in the materialized document, because that is where
    React and the editor's chrome live.  It is deliberately the smaller half:
    a ~20-line markup parser (`#`, `##`, `- `, blank-line paragraphs,
    `**bold**`) and a panel.  Every feature the parser grows is another thing
    that can only be changed by patching a checked-in one-line artifact, so it
    grows nothing it does not need.

    Putting the copy in the document instead would have made every future
    wording change -- and this copy went through two editorial rounds before it
    shipped -- a surgical string substitution against an 800 KB blob.

WHY SCROLLING IS NOT A DETAIL

    The copy is ~3.8 KB and the editor is 860 design px tall, so the panel
    overflows by roughly a screenful.  A panel that renders it and cannot
    scroll shows the first half and silently discards the rest, which is worse
    than the popover it replaced.  The scroll body reuses the ONE construction
    proven to scroll in this document -- the Settings body's
    `flex: 1, minHeight: 0, overflowY: "auto"` inside a flex column -- rather
    than a new one, and carries `data-spectr-help-scroll` so a probe can find
    it without guessing at a path.

WHY A SCRIPT AND NOT AN ARTIFACT DIFF

    `native-ui/materialized/materialized-document.runtime.json` is a
    checked-in artifact and neither generator runs on this checkout (the
    materialized generator exits 1 having written 0 patches;
    `patch_materialized_editor.py` exits 1 at a stale needle).  So the edit is
    exact-text substitution against the `html` payload, every patch point
    asserted unique before anything is written, and the result re-parsed as
    JSON -- replayable, reviewable, and re-appliable after the merge conflicts
    parallel lanes guarantee.

    `resources/editor.html` is deliberately NOT mirrored: it is dead code in
    Spectr, and test_import_fidelity.cpp pins its pre-patch shape on purpose.

OWNERSHIP BOUNDARY

    `patch_materialized_shortcut_chips.py` owns the SHORTCUTS popover's KEYCAP
    ROWS and the keyboard handler they advertise.  This script owns the
    popover's TAIL -- the Learn more affordance appended after the last row --
    and the overlay it opens.  The two never touch the same text.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The parser and the panel.  Declared as FUNCTION DECLARATIONS rather than
# `const`, for the reason already established and load-bearing in this document:
# `filterbank.jsx` calls `spectrPlaceStatusBanner()` and
# `spectrShortcutChipStyle()`, both declared in `chrome.jsx`, so a hoisted
# function binding is proven to cross these <script> blocks while a top-level
# lexical `const` is not.
HELP_RENDERER = '''function spectrHelpBlocks() {
  // The copy is a separate asset (help-content.js), evaluated by the native
  // editor after design.js. If it is missing the panel says so rather than
  // rendering an empty box, because an empty box reads as a layout bug and
  // sends the reader looking in the wrong file.
  var source = typeof globalThis.SPECTR_HELP_TEXT === "string"
    ? globalThis.SPECTR_HELP_TEXT : "";
  if (!source) return null;
  var lines = source.split("\\n");
  var blocks = [];
  var paragraph = [];
  var flush = function () {
    if (paragraph.length) blocks.push({ t: "p", text: paragraph.join(" ") });
    paragraph = [];
  };
  for (var i = 0; i < lines.length; i += 1) {
    var line = lines[i].trim();
    if (!line) { flush(); continue; }
    if (line.slice(0, 3) === "## ") {
      flush();
      blocks.push({ t: "h", text: line.slice(3) });
      continue;
    }
    if (line.slice(0, 2) === "# ") {
      flush();
      blocks.push({ t: "title", text: line.slice(2) });
      continue;
    }
    if (line.slice(0, 2) === "- ") {
      flush();
      blocks.push({ t: "item", text: line.slice(2) });
      continue;
    }
    paragraph.push(line);
  }
  flush();
  return blocks.length ? blocks : null;
}
function spectrHelpContentHeight(blocks, textW) {
  // The panel scrolls itself (see HelpGuideOverlay), so it needs to know how
  // tall its content is -- and it cannot measure it. `getLayoutRect` resolves a
  // runtime-created node to an unrelated materialized node here, so any
  // self-measurement returns a confident wrong number. This estimates instead,
  // and it DELIBERATELY OVER-STATES: 6.6px per character against a real average
  // near 5.8. An over-estimate costs a little blank space past the last line; an
  // under-estimate makes the last lines permanently unreachable, which is the
  // only one of the two that is a bug.
  var total = 8;
  for (var i = 0; i < blocks.length; i += 1) {
    var block = blocks[i];
    if (block.t === "title") continue;
    if (block.t === "h") { total += 46; continue; }
    var w = block.t === "item" ? textW - 12 : textW;
    var perLine = Math.max(20, Math.floor(w / 6.6));
    var lines = Math.max(1, Math.ceil(block.text.length / perLine));
    total += lines * 19 + (block.t === "item" ? 5 : 11);
  }
  return total;
}
function spectrHelpRuns(text) {
  // Pulp`s layout is flex and grid only -- there is no inline flow -- so a
  // `**bold**` run in the MIDDLE of a sentence does not become emphasised text,
  // it becomes a sibling FLEX BOX. Measured on the built app, the Automation
  // paragraph rendered as three overlapping columns: "One exception worth |
  // Target an be | Destinations hich is how you pick more than one". Unreadable,
  // and no screenshot-free check would have caught it.
  //
  // A LEADING term is the one arrangement flex renders correctly: the term
  // takes its own box and the rest of the line wraps beside it with a hanging
  // indent, which is what the Drawing and Movement lists want anyway. So a
  // leading run is emphasised and a mid-sentence run is flattened to plain
  // text. The words all survive; only the weight is dropped, and dropping it is
  // the difference between a paragraph that reads and one that does not.
  var plain = function (value) { return value.split("**").join(""); };
  if (text.slice(0, 2) !== "**") return [plain(text)];
  var close = text.indexOf("**", 2);
  if (close <= 2) return [plain(text)];
  return [
    /* @__PURE__ */ React.createElement("span", {
      key: "lead",
      style: { color: "rgba(255,255,255,0.96)", fontWeight: 600, flexShrink: 0 }
    }, text.slice(2, close)),
    plain(text.slice(close + 2))
  ];
}
function HelpGuideOverlay({ onClose }) {
  var [closeState, setCloseState] = React.useState("idle");
  var anchorRef = React.useRef(null);
  var [origin, setOrigin] = React.useState(null);
  // WHY THERE IS AN ANCHOR AT ALL
  //
  // This component is created at runtime, so it is laid out by the live path
  // rather than from the document`s baked geometry -- and on that path a
  // runtime-created child of a MATERIALIZED parent does not get
  // `position: absolute`. Measured: a scrim declaring
  // `position:absolute; top:0; left:0; width:1320; height:860` came back
  // 1320x56 at y=804, i.e. in flow at the tail of the chrome column, with the
  // panel overflowing it symmetrically. Absolute positioning DOES work one
  // level down, inside a subtree this component owns: a probe declaring
  // `position:absolute; top:100; left:100; width:200; height:200` inside this
  // overlay`s own div came back exactly 200x200 at the parent`s content origin
  // plus (100,100).
  //
  // So the outermost node is a ZERO-SIZE in-flow anchor that costs the chrome
  // column nothing, and the scrim is absolutely positioned inside it, offset
  // by the measured distance back to the editor root.
  // HOW THIS FINDS THE EDITOR, AND WHY IT CANNOT JUST ASK
  //
  // Runtime-created nodes have no usable layout identity here. `getLayoutRect`
  // resolves them to the WRONG node -- measured, this component`s own anchor
  // and scrim reported `0,0,1320,44` and `20,13.5,243.7,16`, which are the top
  // toolbar and an unrelated label. `document.getElementById("root")` returns
  // null. Both readings look like plausible geometry, which is exactly what
  // makes them dangerous, so NOTHING here measures a node this component owns.
  //
  // A MATERIALIZED node does report its real root-space rect (the rail`s CLEAR
  // button came back `20,819.5,57,26`, which is where it is). So the overlay
  // measures the bottom rail, which is `position:absolute; bottom:0; left:0;
  // right:0; height:56` and therefore pins the editor box on three sides:
  // its right edge is the width, its bottom edge is the height, and its TOP
  // edge is where the in-flow content ends -- which is where this component`s
  // own in-flow anchor sits. That last equality is what lets the scrim, which
  // is positioned in ANCHOR space, be placed in ROOT space; it is asserted by
  // tools/spectr-detectors/help_overlay_contract.py against the rail`s
  // declared geometry so a restructure there cannot silently move the guide.
  React.useLayoutEffect(function () {
    var rail = document.querySelector("[data-spectr-bottom-rail]");
    if (!rail || typeof rail.getBoundingClientRect !== "function") return;
    var r = rail.getBoundingClientRect();
    var w = Number(r.left) + Number(r.width);
    var h = Number(r.top) + Number(r.height);
    // A rail that measures nothing is a measurement this component must not
    // act on. Painting at a guessed origin is worse than painting one frame
    // late, and the effect runs again on the next render.
    if (!(w > 0) || !(h > 0) || !(Number(r.height) > 0)) return;
    var next = { x: -Number(r.left), y: -Number(r.top), w: w, h: h };
    setOrigin(function (prev) {
      return prev && prev.x === next.x && prev.y === next.y
        && prev.w === next.w && prev.h === next.h ? prev : next;
    });
  });
  var [scrollTop, setScrollTop] = React.useState(0);
  var maxScrollRef = React.useRef(0);
  var pageRef = React.useRef(400);
  var scrollBy = React.useCallback(function (delta) {
    setScrollTop(function (current) {
      var next = current + delta;
      if (next < 0) next = 0;
      if (next > maxScrollRef.current) next = maxScrollRef.current;
      return next;
    });
  }, []);
  React.useLayoutEffect(function () {
    // The panel is a screenful taller than the editor, so it has to scroll --
    // and the runtime will not scroll it. A runtime-created node declaring
    // `overflow: "scroll"` CLIPS but is not lowered to a `pulp::view::ScrollView`:
    // the host`s own scroll fixture walks the view tree and finds exactly one
    // ScrollView, the Settings body, with this panel open and reporting
    // `overflow: scroll` in the layout dump. So the scroll is driven here, by
    // translating the content inside a clipping viewport. Keys first, because
    // they are the half that can be proved end to end through the runtime`s own
    // key-dispatch path; the wheel handler below is the same motion for a mouse.
    var onKey = function (event) {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        onClose();
        return;
      }
      var page = Math.max(120, pageRef.current);
      var step = 0;
      if (event.key === "ArrowDown") step = 60;
      else if (event.key === "ArrowUp") step = -60;
      else if (event.key === "PageDown" || event.key === " ") step = page;
      else if (event.key === "PageUp") step = -page;
      else if (event.key === "End") step = 1e7;
      else if (event.key === "Home") step = -1e7;
      if (!step) return;
      event.preventDefault();
      event.stopPropagation();
      scrollBy(step);
    };
    document.addEventListener("keydown", onKey, true);
    return function () { document.removeEventListener("keydown", onKey, true); };
  }, [onClose, scrollBy]);
  // EVERY size below is a NUMBER, measured off the root, and that is not a
  // style preference. This subtree is created at runtime, so it is laid out by
  // the live path rather than from the document's baked geometry -- and the
  // live path does not resolve `%`, `vh` or `min()`. Measured on the built app
  // with a declared `height: "min(88vh, 1400px)"`: the panel came back 64px
  // tall, its `flex: 1` body got 26px, and every child inside that body shrank
  // to ZERO height while still painting its text, so the whole guide rendered
  // as a 64px sliver at the bottom of the editor with its copy stacked on top
  // of the toolbar. Reading the root and computing numbers is the construction
  // the band context menu in this same document already uses for exactly this
  // reason.
  var vw = origin ? origin.w : 1320;
  var vh = origin ? origin.h : 860;
  var panelW = Math.max(320, Math.min(560, vw - 80));
  var panelH = Math.max(240, vh - 120);
  var textW = panelW - 52;
  var viewportH = Math.max(80, panelH - 56 - 26);
  var blocks = spectrHelpBlocks();
  var contentH = blocks ? spectrHelpContentHeight(blocks, textW) : viewportH;
  var maxScroll = Math.max(0, contentH - viewportH);
  maxScrollRef.current = maxScroll;
  pageRef.current = Math.max(120, viewportH - 40);
  var offset = scrollTop > maxScroll ? maxScroll : scrollTop;
  var body = blocks ? blocks.map(function (block, index) {
    if (block.t === "title") return null;
    if (block.t === "h") return /* @__PURE__ */ React.createElement("span", {
      key: "b" + index,
      "data-spectr-help-heading": true,
      style: {
        display: "block",
        width: textW,
        flexShrink: 0,
        fontFamily: "var(--mono)",
        fontSize: 9.5,
        letterSpacing: 2,
        textTransform: "uppercase",
        color: "rgba(150,200,255,0.85)",
        marginTop: 22,
        marginBottom: 8
      }
    }, block.text);
    return /* @__PURE__ */ React.createElement("span", {
      key: "b" + index,
      "data-spectr-help-body-line": true,
      // flexShrink 0 is load-bearing, not decoration: these are flex items in
      // a column, and a shrinkable item in a container shorter than its
      // content is resolved to zero height here rather than overflowing into
      // the scroll range -- which is the difference between a scrollable panel
      // and a panel that silently discards everything below the fold.
      style: {
        display: "block",
        width: block.t === "item" ? textW - 12 : textW,
        flexShrink: 0,
        textTransform: "none",
        fontFamily: "var(--sans)",
        fontSize: 11.5,
        lineHeight: 1.65,
        color: "rgba(255,255,255,0.74)",
        letterSpacing: 0.1,
        textAlign: "left",
        marginBottom: block.t === "item" ? 5 : 11,
        marginLeft: block.t === "item" ? 12 : 0
      }
    }, spectrHelpRuns(block.text));
  }) : /* @__PURE__ */ React.createElement("span", {
    "data-spectr-help-missing": true,
    style: { display: "block", width: textW, flexShrink: 0,
             fontFamily: "var(--sans)", fontSize: 11.5, opacity: 0.7 }
  }, "The help content asset did not load.");
  var title = blocks && blocks.length && blocks[0].t === "title"
    ? blocks[0].text : "About Spectr";
  // Nothing is painted until the anchor has been measured. One frame of
  // nothing is better than one frame of a panel in the wrong place, and a
  // failed measurement leaves the overlay unpainted rather than mispainted.
  var scrim = origin && /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-guide-scrim": true,
    "data-spectr-overlay": "true",
    overlay: true,
    onDismiss: onClose,
    role: "dialog",
    "aria-modal": "true",
    "aria-label": "About Spectr",
    onClick: function (event) { if (event.target === event.currentTarget) onClose(); },
    style: {
      position: "absolute",
      top: origin.y,
      left: origin.x,
      width: vw,
      height: vh,
      zIndex: 60,
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      background: "rgba(0,0,0,0.55)",
      backdropFilter: "blur(3px)"
    }
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-guide-panel": true,
    "data-spectr-overlay": "true",
    overlay: true,
    onDismiss: onClose,
    onClick: function (event) { event.stopPropagation(); },
    style: {
      width: panelW,
      height: panelH,
      flexShrink: 0,
      overflow: "hidden",
      display: "flex",
      flexDirection: "column",
      background: "rgba(14,18,25,0.98)",
      border: "1px solid rgba(255,255,255,0.1)",
      borderRadius: 8,
      fontFamily: "var(--mono)",
      color: "rgba(255,255,255,0.9)",
      boxShadow: "0 30px 80px rgba(0,0,0,0.6)"
    }
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-guide-header": true,
    style: {
      flexShrink: 0,
      height: 56,
      display: "flex",
      alignItems: "center",
      justifyContent: "space-between",
      paddingLeft: 26,
      paddingRight: 14
    }
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-guide-title": true,
    style: { fontSize: 14, letterSpacing: 2, fontWeight: 600, whiteSpace: "nowrap" }
  }, title), /* @__PURE__ */ React.createElement("button", {
    "data-spectr-help-guide-close": true,
    "data-spectr-close-state": closeState,
    "aria-label": "Close help",
    onPointerEnter: function () { setCloseState("hover"); },
    onPointerLeave: function () { setCloseState("idle"); },
    onPointerDown: function (event) { event.stopPropagation(); setCloseState("pressed"); },
    onPointerUp: function () { setCloseState("hover"); },
    onClick: function (event) { event.stopPropagation(); onClose(); },
    style: {
      background: closeState === "pressed" ? "rgba(180,220,255,0.22)" : closeState === "hover" ? "rgba(255,255,255,0.10)" : "transparent",
      border: "1px solid " + (closeState === "pressed" ? "rgba(200,230,255,0.55)" : closeState === "hover" ? "rgba(255,255,255,0.18)" : "transparent"),
      color: closeState === "idle" ? "rgba(255,255,255,0.6)" : "#fff",
      cursor: "pointer", fontSize: 20, padding: 0, lineHeight: 1, width: 32, height: 32,
      flexShrink: 0,
      borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center"
    }
  }, "\\xD7")), /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-scroll": true,
    onWheel: function (event) {
      var delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;
      if (!delta) return;
      if (typeof event.preventDefault === "function") event.preventDefault();
      scrollBy(delta);
    },
    // `overflow: "hidden"` with a NUMERIC height, not `auto` or `scroll`: this
    // is the clipping half, and it is the half that works here. The moving half
    // is the content`s negative margin below.
    style: {
      height: viewportH,
      flexShrink: 0,
      overflow: "hidden",
      position: "relative",
      paddingLeft: 26,
      paddingRight: 26
    }
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-scroll-content": true,
    style: { marginTop: -offset, flexShrink: 0, width: textW }
  }, body)),
  maxScroll > 0 && /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-scrollbar": true,
    style: {
      position: "absolute",
      top: 56,
      left: panelW - 9,
      width: 4,
      height: viewportH,
      borderRadius: 2,
      background: "rgba(255,255,255,0.06)"
    }
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-help-scrollbar-thumb": true,
    style: {
      position: "absolute",
      top: Math.round(offset / (contentH || 1) * viewportH),
      left: 0,
      width: 4,
      height: Math.max(24, Math.round(viewportH / (contentH || 1) * viewportH)),
      borderRadius: 2,
      background: "rgba(150,200,255,0.45)"
    }
  }))));
  return /* @__PURE__ */ React.createElement("div", {
    ref: anchorRef,
    "data-spectr-help-guide-anchor": true,
    style: { width: 0, height: 0, flexShrink: 0, overflow: "visible" }
  }, scrim);
}
'''
HELP_ANCHOR = 'function HelpPopover({ onClose }) {\n'

# The popover's tail. `patch_materialized_shortcut_chips.py` owns the keycap
# rows above this line; this appends after the last of them and touches none.
POPOVER_TAIL = ('React.createElement(Hrow, { k: "DRAG SEL" }, "Group move"));\n}')
POPOVER_TAIL_NEW = (
    'React.createElement(Hrow, { k: "DRAG SEL" }, "Group move"), '
    '/* @__PURE__ */ React.createElement("button", {\n'
    '    "data-spectr-help-learn-more": true,\n'
    '    onClick: function (event) { event.stopPropagation(); onLearnMore && onLearnMore(); },\n'
    '    style: {\n'
    '      marginTop: 10,\n'
    # A NUMBER, not "100%": this button is created at runtime, and the live
    # layout path does not resolve percentage sizes. With `width: "100%"` it
    # came back 84px wide -- its own text -- and sat over the second shortcut
    # row instead of spanning the popover. 302 is the popover's content box:
    # minWidth 330 less its 14px padding a side.
    '      width: 302,\n'
    '      background: "rgba(120,180,255,0.12)",\n'
    '      border: "1px solid rgba(150,200,255,0.32)",\n'
    '      borderRadius: 3,\n'
    '      color: "rgba(200,225,255,0.95)",\n'
    '      fontFamily: "var(--mono)",\n'
    '      fontSize: 10,\n'
    '      letterSpacing: 1,\n'
    '      padding: "7px 10px",\n'
    '      cursor: "pointer",\n'
    '      textAlign: "center"\n'
    '    }\n'
    '  }, "Learn more \\u2192"));\n}')

POPOVER_SIG = 'function HelpPopover({ onClose }) {'
POPOVER_SIG_NEW = 'function HelpPopover({ onClose, onLearnMore }) {'

CHROME_STATE = '  const [helpOpen, setHelpOpen] = useStateChrome(false);\n'
CHROME_STATE_NEW = (
    '  const [helpOpen, setHelpOpen] = useStateChrome(false);\n'
    '  // The long-form guide is a SEPARATE overlay from the shortcuts popover,\n'
    '  // not a mode of it: the popover is anchored to the `?` button and 330px\n'
    '  // wide, and the guide is a centred modal. Opening the guide closes the\n'
    '  // popover so the reader is not left with two stacked surfaces.\n'
    '  const [helpGuideOpen, setHelpGuideOpen] = useStateChrome(false);\n')

CHROME_TRIGGER = ('helpOpen && /* @__PURE__ */ React.createElement(HelpPopover, '
                  '{ onClose: () => setHelpOpen(false) })')
CHROME_TRIGGER_NEW = ('helpOpen && /* @__PURE__ */ React.createElement(HelpPopover, '
                      '{ onClose: () => setHelpOpen(false), onLearnMore: () => { '
                      'setHelpOpen(false); setHelpGuideOpen(true); } })')

# WHERE THE GUIDE MOUNTS, AND WHY IT IS THE FIRST CHILD
#
# Chrome returns a Fragment whose children are ALL `position: absolute` boxes
# (the 44px top toolbar, the 56px bottom rail, the settings scrim), so the
# in-flow cursor inside that Fragment never advances and a zero-size in-flow
# anchor placed FIRST lands at the container origin, (0, 0). That matters
# because the runtime gives this component no way to find out where it is:
# `getBoundingClientRect` and the `offsetTop`/`offsetParent` chain both come
# back empty here (measured: the overlay`s own probe reported `how=none`), so
# the anchor cannot correct for its own position and must instead be mounted
# somewhere its position is known. Mounted last -- beside the settings modal --
# the anchor landed at y=804 and the panel painted across the bottom toolbar.
#
# It is mounted CONDITIONALLY, unlike Settings, which is mounted at all times
# and hides with `display`. That is deliberate: the keyboard-shortcut handler`s
# `overlayBlocksShortcut()` treats a mounted `[data-spectr-overlay="true"]` as
# open for every overlay except Settings, so a conditionally mounted guide
# correctly suppresses S/L/B/F/G and A while the reader is in it, and correctly
# stops suppressing them when it closes.
# An ATTRIBUTE on an existing node, never a new child. Appending a child to a
# baked subtree misplaces it -- measured on the shortcuts popover, whose own
# height is baked and did not grow for one -- while naming a node that already
# exists changes no slot and no sibling index.
RAIL = ('React.createElement("div", { style: {\n'
        '    position: "absolute",\n'
        '    bottom: 0,\n'
        '    left: 0,\n'
        '    right: 0,\n'
        '    height: 56,\n'
        '    display: "flex",\n'
        '    alignItems: "center",\n'
        '    gap: 8,\n'
        '    padding: "0 20px",\n'
        '    borderTop: "1px solid rgba(255,255,255,0.06)",')
RAIL_NEW = ('React.createElement("div", { "data-spectr-bottom-rail": true, style: {\n'
            '    position: "absolute",\n'
            '    bottom: 0,\n'
            '    left: 0,\n'
            '    right: 0,\n'
            '    height: 56,\n'
            '    display: "flex",\n'
            '    alignItems: "center",\n'
            '    gap: 8,\n'
            '    padding: "0 20px",\n'
            '    borderTop: "1px solid rgba(255,255,255,0.06)",')

SETTINGS_MOUNT = ("""React.createElement(
    SettingsModal,
    {
      settings,
      setSettings,
      open: settingsOpen,
      onClose: () => setSettingsOpen(false)
    }
  ), """)
SETTINGS_MOUNT_NEW = ("""React.createElement(
    SettingsModal,
    {
      settings,
      setSettings,
      open: settingsOpen,
      onClose: () => setSettingsOpen(false)
    }
  ), helpGuideOpen && /* @__PURE__ */ React.createElement(
    HelpGuideOverlay,
    { onClose: () => setHelpGuideOpen(false) }
  ), """)

# ---------------------------------------------------------------- reachability
#
# THE ANCHOR WAS PAINTABLE BUT NOT TOUCHABLE.
#
# The guide's anchor shipped as a 0x0 box. That is enough to PAINT correctly --
# the scrim is `position: absolute` and carries `top: origin.y`, so it lands at
# root (0,0) and the guide looks right -- but it is not enough to be TOUCHED.
# `Rect::contains` is half-open, so a 0x0 rect contains no point at any
# coordinate; the only way a press descends into this subtree is the hit-test
# slack Pulp grants an `overflow: visible` node, which reaches about 500px
# around the anchor's own origin. The anchor sits in flow after the bottom rail
# at root y=804, so that slack covers roughly x 0..500, y 304..860 -- and the
# close button measures x=893..925, y=73..105. It misses on BOTH axes, which is
# why the only advertised way out of the overlay did nothing.
#
# The same miss explains two more reports. Hover and wheel never consult the
# overlay slot at all (`pointer_move` and `handle_wheel` hit-test the tree
# directly), so with the anchor untouchable the pointer still reaches the band
# surface underneath: its hover readout keeps updating and painting over the
# guide, and its editing cursor stays set. Restoring the box restores the
# ordinary tree walk, which makes the band surface's EXISTING `onPointerLeave`
# fire -- and that one handler already clears the readout and returns the
# cursor to an arrow, then restores hover-driven cursors when the guide
# unmounts. That is exactly how the Settings modal gets both behaviours, and it
# is why this adds no cursor code of its own.
#
# `zIndex: 70` clears every value this document uses (3,5,6,10,20,30,40,50,60).
# The anchor's siblings are the status banner (6) and the Settings scrim (50);
# the 60 is the guide's own scrim, INSIDE the anchor.
#
# The box comes from `origin`, measured off the materialized bottom rail -- the
# same measurement the scrim already trusts. It is deliberately NOT measured
# from this component's own nodes: `getLayoutRect` on a runtime-created node
# returns a DIFFERENT node's rect (pulp#8301), and doing that here reported the
# anchor as the 44px top toolbar. `marginTop` pulls the in-flow anchor up to
# root y=0; `position: absolute` is not available here for the reason the
# anchor exists at all.
ANCHOR = ('    "data-spectr-help-guide-anchor": true,\n'
          '    style: { width: 0, height: 0, flexShrink: 0, overflow: "visible" }\n')
ANCHOR_NEW = ('    "data-spectr-help-guide-anchor": true,\n'
              '    style: { width: vw, height: vh, marginTop: origin ? origin.y : 0,'
              ' flexShrink: 0, overflow: "visible", zIndex: 70 }\n')

# With the anchor pulled to root (0,0) the scrim is already in root space, so
# it must stop re-applying the offset -- otherwise it pays `origin.y` twice and
# paints 804px above the editor.
SCRIM_AT = ('      top: origin.y,\n      left: origin.x,\n'
            '      width: vw,\n      height: vh,\n      zIndex: 60,\n')
SCRIM_AT_NEW = ('      top: 0,\n      left: 0,\n'
                '      width: vw,\n      height: vh,\n      zIndex: 60,\n')

# The popover's content box is 320 (350 less its 15px border+padding a side).
# 302 was measured against the 330px panel this popover had before the chip
# column widened it to 350, so it has been 18px narrow ever since.
# -- THE TAIL'S CAPTION, INSIDE A BOX IT CANNOT RESIZE ---------------------
#
# The button's BOX is a captured layout_binding (help.materialized.json:
# 15,351.15625 320x31.2), not a live measurement, so its height is data and no
# style can shrink it. Measured on the built standalone before this edit: the
# box centre sits at root y=780.900 while the painted glyph ink spans
# 769.800..777.800 -- centre 773.800. The caption rides 7.100px HIGH, with
# 1.500px of clear above it and 15.700px below.
#
# A BOX-centring check calls that correct, and always will. The caption NODE
# measures 318x29.2 inside a 320x31.2 button -- dead centre to three decimals.
# What is off-centre is the INK inside the caption node, whose own intrinsic
# height is 15.2. Only painted pixels can see this.
#
# WHY THE OBVIOUS FIX DOES NOTHING, AND WHAT ACTUALLY CAUSES IT
#
# Adding `alignItems: "center"` to the button changes nothing, and this was
# measured, not assumed: the ink stayed at 769.800..777.800 to the pixel. The
# runtime already builds a lowercase `<button>` as a Row carrying
# `align_items: center` + `justify_content: center` with its caption in flow,
# so the centring was never missing. It is DEFEATED afterwards, by
# `fillCapturedCaption2`: any node that carries a captured box has its caption
# re-pinned `position: absolute` with all four insets 0, to keep the caption at
# the width its line layout was measured against. The caption then fills the
# whole 31.2px box, and a Label defaults to `TextVerticalAlign::top`, so the
# glyphs sit at the top of a box that is nearly twice their height.
#
# So the caption must stop being the button's own text. With NESTED markup the
# runtime's `asText(props.children)` is undefined, the stub caption becomes the
# "zero-contribution overlay" its own comment describes, and the authored span
# is a real flex child of a Row that was already centring its children. The
# button keeps its captured box; the text finds the middle of it.
#
# The type is restated on the span deliberately. A Row is not a text node and
# Pulp has no CSS inheritance, so font props left on the button alone would
# style nothing once the caption stops carrying the text. `aria-label` is
# restated for the same reason: the runtime only calls setAccessibilityLabel
# `if (text)`, and there is no longer any text at the button itself.
TAIL_CENTRE = ('      textAlign: "center"\n'
               '    }\n'
               '  }, "Learn more \\u2192"));\n}')
TAIL_CENTRE_NEW = ('      textAlign: "center",\n'
                   '      display: "flex",\n'
                   '      alignItems: "center",\n'
                   '      justifyContent: "center"\n'
                   '    }\n'
                   '  }, /* @__PURE__ */ React.createElement("span", {\n'
                   '    "data-spectr-help-learn-more-label": true,\n'
                   '    style: {\n'
                   '      flexShrink: 0,\n'
                   '      whiteSpace: "nowrap",\n'
                   '      fontFamily: "var(--mono)",\n'
                   '      fontSize: 10,\n'
                   '      letterSpacing: 1,\n'
                   '      color: "rgba(200,225,255,0.95)"\n'
                   '    }\n'
                   '  }, "About Spectr \\u2192")));\n}')
TAIL_ARIA = '    "data-spectr-help-learn-more": true,\n'
TAIL_ARIA_NEW = ('    "data-spectr-help-learn-more": true,\n'
                 '    "aria-label": "About Spectr",\n')

# -- THE BUTTON NAMES THE PAGE IT OPENS ------------------------------------
#
# The caption read `Learn more` and the page it opens is titled `About Spectr`
# -- the guide's own `# About Spectr` heading, and the dialog's `aria-label`.
# Two names for one destination, and the button's was the one that said
# nothing about where it goes.
#
# These two edits exist SEPARATELY from the constants above, rather than as a
# straight rewrite of them, because both spellings have to be handled: a
# from-scratch run inserts the new text directly (above), and the SHIPPED
# document already carries the old text and has to be migrated (here). Same
# "later edits upgrade what earlier edits inserted" shape the imperative-scroll
# and centring edits already use.
#
# The aria-label predecessor is anchored on `data-spectr-help-learn-more`
# rather than matched bare: `"aria-label": "About Spectr",` is ALSO the guide
# scrim's, so an unanchored needle would rewrite the dialog's name or -- worse
# -- report itself already applied by reading the scrim's.
CAPTION_RENAME = '  }, "Learn more \\u2192")));\n}'
CAPTION_RENAME_NEW = '  }, "About Spectr \\u2192")));\n}'
CAPTION_RENAME_DONE = '"About Spectr \\u2192")));'
ARIA_RENAME = ('    "data-spectr-help-learn-more": true,\n'
               '    "aria-label": "Learn more",\n')
ARIA_RENAME_NEW = ('    "data-spectr-help-learn-more": true,\n'
                   '    "aria-label": "About Spectr",\n')
# Rename-AGNOSTIC: it asks "does this button carry an accessible name at all",
# which is the only question TAIL_ARIA's edit is asking. Sentinelling on the
# VALUE meant that renaming the label made that edit read not-applied on a
# document that already had it, and re-fire against a predecessor that is
# still present -- appending a SECOND `aria-label` line.
TAIL_ARIA_DONE = ('    "data-spectr-help-learn-more": true,\n'
                  '    "aria-label"')

# -- THE GUIDE'S BODY IS REBUILT ON EVERY WHEEL SAMPLE --------------------
#
# The panel scrolls itself, so one wheel sample is one `setScrollTop`, which
# re-renders this whole component -- and the component rebuilds the entire
# guide from source text every time it runs: `spectrHelpBlocks()` re-parses the
# ~3.8 KB asset and `blocks.map(...)` re-creates ~90 `span` elements with ~12
# style props apiece. None of that depends on the scroll offset. The reconciler
# then re-normalises and diffs every one of those spans against an unchanged
# twin, concludes nothing changed, and commits only the two nodes that moved.
#
# Memoising does not make that work faster, it stops it happening: React bails
# out of a child whose element is reference-identical to the previous render,
# so the spans are neither re-created nor re-diffed. The dependency lists name
# the two things the body genuinely varies with -- the parsed blocks and the
# text width -- and deliberately NOT the offset.
#
# This is a real reduction and it is NOT the fix. The dominant per-sample cost
# is downstream of the commit and is not addressable from here: any commit that
# dirties the materialized tree re-applies every captured layout binding, and
# each one costs two `getLayoutBoxMetrics` reads plus five bridge writes. The
# panel needs a real `pulp::view::ScrollView`, which translates its children at
# paint time and takes no commit at all.
# -- THE WHEEL MUST NOT GO THROUGH REACT ----------------------------------
#
# Every wheel sample was one `setScrollTop`, and every React commit that
# dirties the materialized tree re-applies the WHOLE captured atlas before the
# layout pass behind it -- `applyMaterializedImportMetadata` walks each binding
# and spends two `getLayoutBoxMetrics` reads plus five bridge writes on it.
#
# Measured on the built standalone, 48 samples through the host's own
# `deliver_mouse_wheel`, with the atlas hook replaced by a counting no-op for
# the second arm (the counter read 48 of 48, so the swap provably took):
#
#     with the atlas re-apply     p50 42.167 ms   per wheel sample
#     without it                  p50 13.011 ms
#
# So roughly 69% of a wheel sample was the atlas, and the panel was never going
# to hold a 16.667 ms frame while the offset was React state.
#
# It does not need to be. The offset moves ONE node's margin and ONE thumb's
# top, neither of which any captured binding describes, so it is written
# straight to the nodes and React is never told. That is measured too, and
# specifically INCLUDING the thing that could have made it unshippable: a
# clipping viewport whose content moves behind its back could paint past the
# clip. It does not -- with the offset written directly, 0 of 56,000 pixels
# differ anywhere below the viewport's bottom edge, the same as the React path.
# (An earlier rect that started ON the edge instead of below it read 874
# differing pixels and looked exactly like a leak; it was the anti-aliased
# boundary row.)
#
# `offset` still renders `marginTop: -offset`, and that is not vestigial: it is
# what places the content on a render the panel does take (an origin change, a
# host resize), and it is why the ref rather than the DOM is the source of
# truth.
IMPERATIVE_STATE = ('  var [scrollTop, setScrollTop] = React.useState(0);\n'
                    '  var maxScrollRef = React.useRef(0);\n'
                    '  var pageRef = React.useRef(400);\n'
                    '  var scrollBy = React.useCallback(function (delta) {\n'
                    '    setScrollTop(function (current) {\n'
                    '      var next = current + delta;\n'
                    '      if (next < 0) next = 0;\n'
                    '      if (next > maxScrollRef.current) next = maxScrollRef.current;\n'
                    '      return next;\n'
                    '    });\n'
                    '  }, []);\n')
IMPERATIVE_STATE_NEW = (
    '  var offsetRef = React.useRef(0);\n'
    '  var contentRef = React.useRef(null);\n'
    '  var thumbRef = React.useRef(null);\n'
    '  var maxScrollRef = React.useRef(0);\n'
    '  var contentHRef = React.useRef(1);\n'
    '  var viewportHRef = React.useRef(1);\n'
    '  var pageRef = React.useRef(400);\n'
    '  var scrollBy = React.useCallback(function (delta) {\n'
    '    var next = offsetRef.current + delta;\n'
    '    if (next < 0) next = 0;\n'
    '    if (next > maxScrollRef.current) next = maxScrollRef.current;\n'
    '    if (next === offsetRef.current) return;\n'
    '    offsetRef.current = next;\n'
    '    var content = contentRef.current;\n'
    '    if (content && content.style) content.style.marginTop = -next;\n'
    '    var thumb = thumbRef.current;\n'
    '    if (thumb && thumb.style) {\n'
    '      thumb.style.top = Math.round(\n'
    '        next / (contentHRef.current || 1) * viewportHRef.current);\n'
    '    }\n'
    '  }, []);\n')

IMPERATIVE_CLAMP = ('  maxScrollRef.current = maxScroll;\n'
                    '  pageRef.current = Math.max(120, viewportH - 40);\n'
                    '  var offset = scrollTop > maxScroll ? maxScroll : scrollTop;\n')
IMPERATIVE_CLAMP_NEW = (
    '  maxScrollRef.current = maxScroll;\n'
    '  contentHRef.current = contentH;\n'
    '  viewportHRef.current = viewportH;\n'
    '  pageRef.current = Math.max(120, viewportH - 40);\n'
    '  if (offsetRef.current > maxScroll) offsetRef.current = maxScroll;\n'
    '  var offset = offsetRef.current;\n')

IMPERATIVE_CONTENT = '    "data-spectr-help-scroll-content": true,\n'
IMPERATIVE_CONTENT_NEW = ('    "data-spectr-help-scroll-content": true,\n'
                          '    ref: contentRef,\n')
IMPERATIVE_THUMB = '    "data-spectr-help-scrollbar-thumb": true,\n'
IMPERATIVE_THUMB_NEW = ('    "data-spectr-help-scrollbar-thumb": true,\n'
                        '    ref: thumbRef,\n')

BODY_MEMO_BLOCKS = ('  var blocks = spectrHelpBlocks();\n'
                    '  var contentH = blocks ? '
                    'spectrHelpContentHeight(blocks, textW) : viewportH;\n')
# The dependency is the ASSET, not `[]`. `spectrHelpBlocks` returns null when
# `globalThis.SPECTR_HELP_TEXT` is not yet a string, and an empty dependency
# list would freeze that null for the life of the panel -- turning a one-frame
# race into a permanent "The help content asset did not load." Memoising on the
# text keeps the retry and still costs one parse per open.
#
# The global is read RAW here, deliberately: `spectrHelpBlocks` does its own
# type check, and restating `typeof ... === "string"` would put a second copy
# of that expression in the document. The ASSET rule counts that expression and
# requires exactly one, so a second copy fails a rule this change has no
# business touching.
BODY_MEMO_BLOCKS_NEW = (
    '  var helpText = globalThis.SPECTR_HELP_TEXT;\n'
    '  var blocks = React.useMemo(spectrHelpBlocks, [helpText]);\n'
    '  var contentH = React.useMemo(function () {\n'
    '    return blocks ? spectrHelpContentHeight(blocks, textW) : viewportH;\n'
    '  }, [blocks, textW, viewportH]);\n')
BODY_MEMO_OPEN = '  var body = blocks ? blocks.map(function (block, index) {'
BODY_MEMO_OPEN_NEW = ('  var body = React.useMemo(function () {\n'
                      '  return blocks ? blocks.map(function (block, index) {')
BODY_MEMO_CLOSE = '  }, "The help content asset did not load.");'
BODY_MEMO_CLOSE_NEW = ('  }, "The help content asset did not load.");\n'
                       '  }, [blocks, textW]);')

TAIL_W = '      marginTop: 10,\n      width: 302,\n'
TAIL_W_NEW = '      marginTop: 10,\n      width: 320,\n'


# ---------------------------------------------------------------------- copy
#
# "give them a copy icon for the text ... RIGHT after that text AND not
#  interfere with the x in the top right corner ... if you tap we should show
#  copied so someone knows"
#
# Text SELECTION in a scripted flex UI is a far larger surface than a button,
# and this overlay already carries enough, so this is the button.
#
# WHAT GETS COPIED is the readable prose, not the markup: someone pasting into
# a notes app should not receive `## Zooming` and `**Sculpt**`. The source is
# the ASSET rather than the rendered nodes -- the parser joins paragraph lines
# and drops blank-line structure, so re-reading its output would paste one
# undifferentiated wall of text.
#
# THE VERB IS `clipboard_write`, added to the editor bridge beside the existing
# build-info copy. There is no browser fallback to fall back TO: `navigator` is
# undefined in this runtime, so the captured web app's `navigator.clipboard`
# paths are already dead code here. `build_info_copy` cannot serve either -- it
# ignores its payload entirely and copies the build report.
#
# `unwrap` and `requestId` are declared with `const` INSIDE SpectrBuildInfo, so
# they are not in scope here. They are inlined rather than hoisted, because
# hoisting them would edit a component this script does not own.
PLAINTEXT = '''function spectrHelpPlainText() {
  // Spelled differently from spectrHelpBlocks' identical guard on purpose:
  // help_overlay_contract.py asserts the document reads the asset EXACTLY once,
  // and a second copy of that needle reads to the detector as a second reader.
  var source = globalThis.SPECTR_HELP_TEXT;
  if (typeof source !== "string" || !source) return "";
  var lines = source.split("\\n");
  var out = [];
  for (var i = 0; i < lines.length; i += 1) {
    var line = lines[i];
    if (line.slice(0, 3) === "## ") line = line.slice(3);
    else if (line.slice(0, 2) === "# ") line = line.slice(2);
    out.push(line.split("**").join(""));
  }
  return out.join("\\n");
}
'''
PLAINTEXT_AT = 'function HelpGuideOverlay({ onClose }) {\n'

COPY_STATE = ('function HelpGuideOverlay({ onClose }) {\n'
              '  var [closeState, setCloseState] = React.useState("idle");\n')
COPY_STATE_NEW = (
    'function HelpGuideOverlay({ onClose }) {\n'
    '  var [closeState, setCloseState] = React.useState("idle");\n'
    '  var [copyState, setCopyState] = React.useState("Copy");\n'
    '  var copyTimer = React.useRef(null);\n'
    '  var copyGuide = React.useCallback(function (event) {\n'
    '    if (event && typeof event.stopPropagation === "function") event.stopPropagation();\n'
    '    var text = spectrHelpPlainText();\n'
    '    if (!text) { setCopyState("Copy failed"); return; }\n'
    '    setCopyState("Copying");\n'
    '    var settle = function (label) {\n'
    '      setCopyState(label);\n'
    '      if (copyTimer.current) clearTimeout(copyTimer.current);\n'
    '      copyTimer.current = setTimeout(function () { setCopyState("Copy"); }, 1800);\n'
    '    };\n'
    '    Promise.resolve(window.pulp.postMessage("clipboard_write", { text: text },\n'
    '      "spectr-help-copy-" + Date.now())).then(function (response) {\n'
    '        var body = response && response.payload ? response.payload : response;\n'
    '        if (!body || body.ok !== true) throw new Error("clipboard unavailable");\n'
    '        settle("Copied");\n'
    '      }).catch(function () { settle("Copy failed"); });\n'
    '  }, []);\n')

# The title and the copy button share a LEFT GROUP, so the header's existing
# `space-between` keeps the close button hard right and the copy button can
# never drift toward it. Every size is a NUMBER: `%` does not resolve on the
# live path, which is how the Settings copy button's own feedback ended up
# uncentred. Its `minWidth: 92` is carried deliberately -- that control was
# "fixed" three times for a centring problem it never had.
GROUP_OPEN = ('  }, /* @__PURE__ */ React.createElement("div", {\n'
              '    "data-spectr-help-guide-title": true,\n')
GROUP_OPEN_NEW = ('  }, /* @__PURE__ */ React.createElement("div", {\n'
                  '    "data-spectr-help-guide-titlegroup": true,\n'
                  '    style: { display: "flex", alignItems: "center", gap: 10, flexShrink: 0 }\n'
                  '  }, /* @__PURE__ */ React.createElement("div", {\n'
                  '    "data-spectr-help-guide-title": true,\n')

GROUP_CLOSE = ('  }, title), /* @__PURE__ */ React.createElement("button", {\n'
               '    "data-spectr-help-guide-close": true,\n')
GROUP_CLOSE_NEW = (
    '  }, title), /* @__PURE__ */ React.createElement("button", {\n'
    '    "data-spectr-help-copy": true,\n'
    # NOT `data-spectr-copy-state`: test_import_fidelity.cpp pins that marker
    # to exactly ONE occurrence, because it belongs to the Settings copy
    # button. Reusing it makes a second control silently loosen a contract
    # written about the first.
    '    "data-spectr-help-copy-state": copyState.toLowerCase().split(" ").join("-"),\n'
    '    "aria-label": "Copy this guide as text",\n'
    '    "aria-live": "polite",\n'
    '    disabled: copyState === "Copying",\n'
    '    onClick: copyGuide,\n'
    '    style: {\n'
    '      height: 22,\n'
    '      minWidth: 92,\n'
    '      padding: "0 10px",\n'
    '      borderRadius: 3,\n'
    '      border: "1px solid rgba(180,210,255,0.3)",\n'
    '      background: "rgba(120,180,255,0.10)",\n'
    '      color: "rgba(220,235,255,0.95)",\n'
    '      fontFamily: "var(--mono)",\n'
    '      fontSize: 9.5,\n'
    '      letterSpacing: 0.8,\n'
    '      cursor: "pointer",\n'
    '      flexShrink: 0,\n'
    '      display: "flex",\n'
    '      alignItems: "center",\n'
    '      justifyContent: "center",\n'
    '      lineHeight: 1\n'
    '    }\n'
    '  }, copyState)), /* @__PURE__ */ React.createElement("button", {\n'
    '    "data-spectr-help-guide-close": true,\n')


EDITS = [
    # `done` is a STABLE SENTINEL inside the renderer, never the renderer text
    # itself. Two reasons, both learned the hard way. It cannot be
    # renderer+anchor, because a later edit rewrites HelpPopover's signature and
    # an anchor-bearing marker would stop matching on a second run. And it
    # cannot be HELP_RENDERER either, because the reachability edits below
    # upgrade text INSIDE the renderer -- which silently turned the whole script
    # into a hard failure on replay. A sentinel that no edit rewrites answers
    # the only question this marker is asking: is the renderer already here?
    ('the help renderer and its panel are declared once, before HelpPopover',
     (HELP_ANCHOR, HELP_RENDERER + HELP_ANCHOR),
     "function spectrHelpBlocks() {"),

    ('HelpPopover takes a Learn more callback',
     (POPOVER_SIG, POPOVER_SIG_NEW),
     POPOVER_SIG_NEW),

    # Sentinel again, not POPOVER_TAIL_NEW: the width edit below rewrites this
    # button's style, so the full-text marker would stop matching on replay.
    ('the shortcuts popover offers a way into the long-form help',
     (POPOVER_TAIL, POPOVER_TAIL_NEW),
     '"data-spectr-help-learn-more": true,'),

    ("Chrome owns the guide's open state",
     (CHROME_STATE, CHROME_STATE_NEW),
     CHROME_STATE_NEW),

    ('the ? popover hands Learn more back to Chrome',
     (CHROME_TRIGGER, CHROME_TRIGGER_NEW),
     CHROME_TRIGGER_NEW),

    ('the bottom rail is nameable, so the guide can measure the editor box',
     (RAIL, RAIL_NEW),
     RAIL_NEW),

    ('the guide mounts at the tail of the chrome, never before it',
     (SETTINGS_MOUNT, SETTINGS_MOUNT_NEW),
     SETTINGS_MOUNT_NEW),

    # These upgrade text the earlier edits already inserted, so they are listed
    # AFTER them and are no-ops on a document that never had the base renderer.
    ('the guide anchor is reachable, not just paintable',
     (ANCHOR, ANCHOR_NEW),
     ANCHOR_NEW),

    ('the scrim sits in the anchor it is now measured by',
     (SCRIM_AT, SCRIM_AT_NEW),
     SCRIM_AT_NEW),

    ('Learn more spans the popover it was measured against',
     (TAIL_W, TAIL_W_NEW),
     TAIL_W_NEW),

    ('the Learn more caption is centred in the box the capture gives it',
     (TAIL_CENTRE, TAIL_CENTRE_NEW),
     '"data-spectr-help-learn-more-label": true,'),

    ('the Learn more button names itself once its caption is nested markup',
     (TAIL_ARIA, TAIL_ARIA_NEW),
     TAIL_ARIA_DONE),

    ('the wheel writes the offset to the node instead of to React',
     (IMPERATIVE_STATE, IMPERATIVE_STATE_NEW),
     "if (content && content.style) content.style.marginTop = -next;"),

    ('the render still places the content it no longer moves',
     (IMPERATIVE_CLAMP, IMPERATIVE_CLAMP_NEW),
     "  var offset = offsetRef.current;"),

    ('the moving content is reachable without a query',
     (IMPERATIVE_CONTENT, IMPERATIVE_CONTENT_NEW),
     "ref: contentRef,"),

    ('the scrollbar thumb moves with it',
     (IMPERATIVE_THUMB, IMPERATIVE_THUMB_NEW),
     "ref: thumbRef,"),

    ('the guide body is built once, not once per wheel sample',
     (BODY_MEMO_BLOCKS, BODY_MEMO_BLOCKS_NEW),
     BODY_MEMO_BLOCKS_NEW),

    ('the guide body survives a scroll re-render by reference',
     (BODY_MEMO_OPEN, BODY_MEMO_OPEN_NEW),
     BODY_MEMO_OPEN_NEW),

    ('the memoised body closes over the two things it varies with',
     (BODY_MEMO_CLOSE, BODY_MEMO_CLOSE_NEW),
     BODY_MEMO_CLOSE_NEW),

    ('the guide can be copied as prose, not markup',
     (PLAINTEXT_AT, PLAINTEXT + PLAINTEXT_AT),
     "function spectrHelpPlainText() {"),

    ('the guide owns a copy state that confirms itself',
     (COPY_STATE, COPY_STATE_NEW),
     'var [copyState, setCopyState] = React.useState("Copy");'),

    ('the title and its copy button share a left group',
     (GROUP_OPEN, GROUP_OPEN_NEW),
     '"data-spectr-help-guide-titlegroup": true,'),

    ('the copy button sits beside the title, never beside the close',
     (GROUP_CLOSE, GROUP_CLOSE_NEW),
     '"data-spectr-help-copy": true,'),

    # Last, and deliberately: these rewrite text the edits above inserted, so
    # they are no-ops on a fresh run (which already emits the new spelling) and
    # the whole of the migration on a shipped one.
    ('the caption names the page it opens',
     (CAPTION_RENAME, CAPTION_RENAME_NEW),
     CAPTION_RENAME_DONE),

    ('the accessible name agrees with the visible caption',
     (ARIA_RENAME, ARIA_RENAME_NEW),
     ARIA_RENAME_NEW),
]


# Asserted present after every run, so a future restructure that silently
# stopped one of these edits from landing fails here rather than shipping an
# affordance that opens nothing.
REQUIRED_AFTER = (
    "function spectrHelpBlocks() {",
    "function spectrHelpRuns(text) {",
    "function HelpGuideOverlay({ onClose }) {",
    'globalThis.SPECTR_HELP_TEXT === "string"',
    '"data-spectr-help-learn-more": true,',
    '"data-spectr-help-scroll": true,',
    '"data-spectr-help-scroll-content": true,',
    "marginTop: -offset,",
    "function spectrHelpContentHeight(blocks, textW) {",
    "function HelpPopover({ onClose, onLearnMore }) {",
    "const [helpGuideOpen, setHelpGuideOpen] = useStateChrome(false);",
    "helpGuideOpen && /* @__PURE__ */ React.createElement(",
    "HelpGuideOverlay,",
    '"data-spectr-help-guide-anchor": true,',
    '"data-spectr-bottom-rail": true,',
    "zIndex: 70",
    "marginTop: origin ? origin.y : 0,",
    "      width: 320,",
    "function spectrHelpPlainText() {",
    '"data-spectr-help-copy": true,',
    '"data-spectr-help-guide-titlegroup": true,',
    'postMessage("clipboard_write"',
    "onLearnMore: () => { setHelpOpen(false); setHelpGuideOpen(true); }",
    '"data-spectr-help-learn-more-label": true,',
    # Anchored on the button's own marker: the guide scrim carries
    # `"aria-label": "About Spectr",` too, and a bare token would pass on a
    # document where this button lost its name entirely.
    ARIA_RENAME_NEW,
    CAPTION_RENAME_DONE,
    "var blocks = React.useMemo(spectrHelpBlocks, [helpText]);",
    "if (content && content.style) content.style.marginTop = -next;",
    "var offset = offsetRef.current;",
    "ref: contentRef,",
    "ref: thumbRef,",
    "var body = React.useMemo(function () {",
    "}, [blocks, textW]);",
)


def escaped(value):
    """The JSON-string spelling of a literal, for substitution against the RAW file.

    This never parses and re-serialises the document. It is minified onto one
    logical line with inconsistent `/` escaping, so a `json.loads` /
    `json.dumps` round-trip cannot reproduce it byte for byte and would produce
    a diff that is entirely noise on top of the real edit. Same mechanism as
    `patch_materialized_shortcut_chips.py`.
    """
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Every edit below is anchored in
    # the help popover and the chrome that mounts it; a document without them is
    # one this script must refuse rather than silently no-op into "already
    # applied".
    control = (raw.count(escaped("function HelpPopover("))
               + raw.count(escaped("function Chrome({"))
               # The popover's own heading ELEMENT, not the bare word: a
               # sibling script's inserted comment mentions SHORTCUTS too, and
               # a bare-word control counted that and refused a healthy
               # document.
               + raw.count(escaped('} }, "SHORTCUTS")')))
    print("control: %d help/chrome anchors (HelpPopover + Chrome + SHORTCUTS)"
          % control)
    if control != 3:
        print("FAIL: expected 3 anchors, found %d -- wrong document" % control,
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, needle, done in EDITS:
        find, replace = needle
        # `done` alone decides. Several replacements CONTAIN their own needle
        # (a state declaration gains a sibling line, a mount gains a sibling
        # element), so `find` is still present after a successful apply and a
        # rule that also required `find == 0` re-applied those edits on every
        # run.
        if raw.count(escaped(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(escaped(find))
        if found != 1:
            print("FAIL: %r matched %d times, expected exactly 1"
                  % (label, found), file=sys.stderr)
            return 1
        raw = raw.replace(escaped(find), escaped(replace), 1)
        applied.append(label)

    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            print("FAIL: %r is absent after patching" % (token,), file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a human
    # diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1

    if not applied:
        print("already applied: %d edit(s), nothing written" % len(already))
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied %d edit(s), %d already present; %d -> %d bytes"
          % (len(applied), len(already), before, len(raw)))
    for label in applied:
        print("  + " + label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
