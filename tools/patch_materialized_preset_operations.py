#!/usr/bin/env python3
"""Make every preset operation either do what it says or say what it did.

Four defects, all in the preset manager, all with the same shape: a control
the user presses, and no way to tell from the product what happened.

DELETE WAS INERT, NOT UNREACHABLE. This is the one that was reported, and the
two diagnoses call for opposite repairs, so it was measured rather than
guessed. `tools/native_shot.cpp`'s SPECTR_PRESET_OPS block presses DELETE
twice: once at the centre of the rect it paints, through View::hit_test, and
once by selector through `__pulpActivateMaterializedElement__`, which bypasses
hit-testing entirely. Both left the user list at the same length, and the
second reported why:

    'confirm' is not defined

`confirm` is a browser dialog. This runtime is not a browser and defines no
such global, so the ONLY statement in the delete handler threw before `del`
was ever called -- on every press, by every route, since the action shipped.
The button is reachable (the same probe reports it REACHABLE while APPLY,
SET AS DEFAULT and DUPLICATE are not, which is a separate layout defect owned
elsewhere and deliberately untouched here).

Restored as a dialog rather than as an unconfirmed delete. The panel's own
PatternSaveDialog is the model: an overlay at z-index 40, outside the action
row entirely, so this adds no element to a row whose geometry another change
is already in flight against.

A SEARCH THAT MATCHED NOTHING SAID THE LIBRARY WAS EMPTY. The USER list
printed "no user patterns -- click SAVE CURRENT below" from
`filteredUser.length === 0`, which is equally true when eight presets exist
and the query matches none of them. Measured with two presets saved: the
heading read `USER . 0` and the panel invited the user to save their first
one. That reading is worse than unhelpful -- it tells someone their presets
are gone.

EXPORT (FILE) REPORTED SUCCESS AND WROTE NOTHING. The export body builds a
Blob, takes an object URL, and clicks a detached anchor. Blob, URL and
document.createElement all EXIST in this runtime, so the body runs clean to
its last line and announces EXPORTED -- while no file sink exists for the
anchor to deliver to. A false success is the worst of the four: the user has
been told their preset is saved somewhere.

Detected by FileReader, not by the bridge. `window.pulp` is present in the
WebView reference build too, where downloads genuinely work, so it is the
wrong discriminator. FileReader is the same capability class -- local file
I/O -- is present in every real browser, and is undefined here. Where it is
absent the export goes to the clipboard, which the same probe proved works
(writeText resolves and readText reads the value back), and says so.

DUPLICATE AND SET AS DEFAULT WERE ERASED BY THE NEXT COMMAND. Both wrote React
state directly and sent nothing to the native library. Every other pattern
command's response REPLACES that state wholesale from the library, so one
later save silently discarded them. This is invisible to a count -- the sweep
that found it read DUPLICATE as 1->2 and the following save as 2->2, because
the copy dying and the save landing cancelled -- and needs preset ids to see.

Both are wired to bridge verbs whose C++ already exists and is unit-tested:
`PatternLibrary::duplicate` and `PatternLibrary::set_default` had zero
production callers. The verbs are added in src/editor_bridge.cpp.

OWNERSHIP. Each region below has one writer. The action-button ROW's geometry,
arrow-key navigation, and IMPORT FILE belong to the concurrent
band-menu/relayout lane and are not touched: nothing here adds, removes or
resizes a control in `[data-spectr-manager-actions]`.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`, so a
rebuild is REQUIRED before any native test reflects this patch. `Encoding
binary asset materialized-document.runtime.json` in the build log is the
proof; "Built target" is not.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Every edit is (label, old, new). `old` must occur EXACTLY once in the
# decoded page; anything else is a drifted patch point and stops the run
# rather than guessing which occurrence was meant.
EDITS = []


def edit(label, old, new):
    EDITS.append((label, old, new))


# --- 1. DELETE: a confirmation the runtime can actually show ---------------
edit(
    "delete-routes-to-a-dialog",
    '      onDelete: () => {\n'
    '        if (confirm(`Delete "${selected.name}"?`)) del(selected.id);\n'
    '      },',
    '      // `confirm` is a browser dialog and this runtime defines no such\n'
    '      // global, so this handler threw on every press by every route and\n'
    '      // DELETE never ran. Ask in a panel the runtime can paint instead.\n'
    '      onDelete: () => setPendingDelete(selected),',
)

# The dialog's state, next to the manager's other cells.
edit(
    "delete-dialog-state",
    '  const [importText, setImportText] = usePM("");\n'
    '  const [showImport, setShowImport] = usePM(false);',
    '  const [importText, setImportText] = usePM("");\n'
    '  const [showImport, setShowImport] = usePM(false);\n'
    '  // The preset a DELETE press is asking about, or null. Holding the\n'
    '  // pattern rather than its id keeps the name in the prompt correct\n'
    '  // even if the list re-renders underneath the dialog.\n'
    '  const [pendingDelete, setPendingDelete] = usePM(null);',
)

# Clear it whenever the manager opens, beside the search reset, so a dialog
# left standing by a dismissal does not reappear on the next open.
# A SEPARATE effect, not a fourth statement in the reset-on-open one.
#
# tools/spectr-detectors/preset_manager_reset_and_chrome.py counts that effect's
# EXACT three-statement body plus `}, [open]);` as the literal proving the
# shipped reset-on-open fix is still there. Adding a statement inside it made
# that count read 0 -- the fix reported as VANISHED -- and the detector
# self-test rejected this change on those grounds. The behaviour was never
# affected; only the needle was.
#
# Keeping the guarded region byte-identical is the right repair. Loosening a
# detector so a later edit fits is how a detector stops discriminating, and
# this one is the only guard on that fix.
edit(
    "delete-dialog-resets-on-open",
    '    setQuery("");\n'
    '    setShowImport(false);\n'
    '    setImportText("");\n'
    '  }, [open]);',
    '    setQuery("");\n'
    '    setShowImport(false);\n'
    '    setImportText("");\n'
    '  }, [open]);\n'
    '  // Dismissing the panel with a delete confirmation standing must not\n'
    '  // bring that confirmation back on the next open. Keyed on [open]\n'
    '  // alone for the same reason the reset above is: `onClose` is an\n'
    '  // inline arrow at the App call site and a fresh identity on every\n'
    '  // render, so an effect keyed on it re-runs constantly.\n'
    '  usePE(() => {\n'
    '    if (!open) return;\n'
    '    setPendingDelete(null);\n'
    '  }, [open]);',
)

# --- 2. the empty USER list vs a search that matched nothing ---------------
edit(
    "empty-state-distinguishes-no-match",
    'filteredUser.length === 0 && /* @__PURE__ */ React.createElement("div", '
    '{ style: { padding: "8px 14px", fontSize: 10, opacity: 0.4, fontStyle: '
    '"italic" } }, "no user patterns \\u2014 click SAVE CURRENT below")',
    'filteredUser.length === 0 && /* @__PURE__ */ React.createElement("div", '
    '{ "data-spectr-user-empty": userPatterns.length === 0 ? "library" : '
    '"no-match", style: { padding: "8px 14px", fontSize: 10, opacity: 0.4, '
    'fontStyle: "italic" } }, '
    '// An empty USER list and a query that matched none of a full one are '
    'the same length and are not the same fact. Reporting them the same way '
    'told a user with presets saved that they had none, and invited them to '
    'save their first.\n'
    '  userPatterns.length === 0 ? "no user patterns \\u2014 click SAVE '
    'CURRENT below" : `no user preset matches \\u201C${query}\\u201D '
    '\\u2014 ${userPatterns.length} saved`)',
)

# --- 2b. clearing the search must not crash the panel ----------------------
# The search field is the ONLY input in this panel that read `e.target.value`
# raw; its three siblings all go through `String(spectrInputValue(event) ?? "")`,
# which is the shape someone already hardened them into. Emptying the field
# delivers a change whose value does not survive that raw read, `query` becomes
# undefined, and the very next line of the render is
# `query.toLowerCase()` inside `userPatterns.filter` -- so clearing a search
# throws out of PatternManager's render rather than restoring the list.
# Reproduced in tools/native_shot.cpp: setting a non-empty query filters
# normally, and setting an empty one throws `not a function` with
# `filter (native) / PatternManager` on the stack.
edit(
    "search-reads-its-value-safely",
    'onChange: (e) => setQuery(e.target.value),',
    'onChange: (e) => setQuery(String(spectrInputValue(e) ?? "")),',
)


# --- 3. EXPORT: no file sink here, so do not claim a file ------------------
edit(
    "export-selected-tells-the-truth",
    '    } else {\n'
    '      const blob = new Blob([json], { type: "application/json" });\n'
    '      const url = URL.createObjectURL(blob);\n'
    '      const a = document.createElement("a");\n'
    '      a.href = url;\n'
    '      a.download = `spectr-${selected.name.replace(/[^a-z0-9]+/gi, "-").'
    'toLowerCase()}.json`;\n'
    '      a.click();\n'
    '      URL.revokeObjectURL(url);\n'
    '      onStatus && onStatus("EXPORTED");\n'
    '    }',
    '    } else if (!canDownloadFile()) {\n'
    '      await exportToClipboard(json, "EXPORTED TO CLIPBOARD");\n'
    '    } else {\n'
    '      const blob = new Blob([json], { type: "application/json" });\n'
    '      const url = URL.createObjectURL(blob);\n'
    '      const a = document.createElement("a");\n'
    '      a.href = url;\n'
    '      a.download = `spectr-${selected.name.replace(/[^a-z0-9]+/gi, "-").'
    'toLowerCase()}.json`;\n'
    '      a.click();\n'
    '      URL.revokeObjectURL(url);\n'
    '      onStatus && onStatus("EXPORTED");\n'
    '    }',
)

edit(
    "export-all-tells-the-truth",
    '    } else {\n'
    '      const blob = new Blob([json], { type: "application/json" });\n'
    '      const url = URL.createObjectURL(blob);\n'
    '      const a = document.createElement("a");\n'
    '      a.href = url;\n'
    '      a.download = `spectr-patterns.json`;\n'
    '      a.click();\n'
    '      URL.revokeObjectURL(url);\n'
    '      onStatus && onStatus("EXPORTED ALL");\n'
    '    }',
    '    } else if (!canDownloadFile()) {\n'
    '      await exportToClipboard(json, "EXPORTED ALL TO CLIPBOARD");\n'
    '    } else {\n'
    '      const blob = new Blob([json], { type: "application/json" });\n'
    '      const url = URL.createObjectURL(blob);\n'
    '      const a = document.createElement("a");\n'
    '      a.href = url;\n'
    '      a.download = `spectr-patterns.json`;\n'
    '      a.click();\n'
    '      URL.revokeObjectURL(url);\n'
    '      onStatus && onStatus("EXPORTED ALL");\n'
    '    }',
)

edit(
    "export-file-sink-capability",
    '  const exportSelected = async (mode) => {',
    '  // Whether an anchor click can deliver a file HERE. Not `window.pulp`:\n'
    '  // that is present in the WebView reference build too, where downloads\n'
    '  // genuinely work. FileReader is the same capability -- local file I/O\n'
    '  // -- is present in every real browser, and is undefined in this\n'
    '  // runtime, where the whole export body runs clean and delivers\n'
    '  // nowhere.\n'
    '  const canDownloadFile = () => typeof Blob === "function"\n'
    '    && typeof URL !== "undefined"\n'
    '    && typeof URL.createObjectURL === "function"\n'
    '    && typeof FileReader === "function";\n'
    '  const exportToClipboard = async (json, okStatus) => {\n'
    '    try {\n'
    '      await navigator.clipboard.writeText(json);\n'
    '      onStatus && onStatus(okStatus);\n'
    '    } catch {\n'
    '      onStatus && onStatus("EXPORT UNAVAILABLE HERE");\n'
    '    }\n'
    '  };\n'
    '  const exportSelected = async (mode) => {',
)

# --- 4. DUPLICATE and SET AS DEFAULT reach the library ---------------------
edit(
    "duplicate-reaches-the-library",
    '  const duplicate = (id) => {\n'
    '    const src = [...factory, ...userPatterns].find((p2) => p2.id === id);\n'
    '    if (!src) return;\n'
    '    const gains = src.source === "factory" ? window.Spectr.factoryGains('
    'src.id, 128) : window.Spectr.fromCanonical(src.gains);\n'
    '    const p = window.Spectr.makeUserPattern(src.name + " COPY", gains);\n'
    '    setUserPatterns([...userPatterns, p]);\n'
    '    setSelectedId(p.id);\n'
    '    onStatus && onStatus(`DUPLICATED`);\n'
    '  };',
    '  // Sends the copy to the library when one owns the patterns. Writing\n'
    '  // React state alone -- which is all this did -- survives until the\n'
    '  // next pattern command, whose response replaces that state wholesale\n'
    '  // from a library the copy never reached. The loss is invisible to a\n'
    '  // row count: a duplicate dying and a save landing cancel out.\n'
    '  const duplicate = (id) => {\n'
    '    const src = [...factory, ...userPatterns].find((p2) => p2.id === id);\n'
    '    if (!src) return;\n'
    '    if (onDuplicatePattern) {\n'
    '      onDuplicatePattern(id);\n'
    '      return;\n'
    '    }\n'
    '    const gains = src.source === "factory" ? window.Spectr.factoryGains('
    'src.id, 128) : window.Spectr.fromCanonical(src.gains);\n'
    '    const p = window.Spectr.makeUserPattern(src.name + " COPY", gains);\n'
    '    setUserPatterns([...userPatterns, p]);\n'
    '    setSelectedId(p.id);\n'
    '    onStatus && onStatus(`DUPLICATED`);\n'
    '  };',
)

edit(
    "set-default-reaches-the-library",
    '      onSetDefault: () => {\n'
    '        setDefaultId(selected.id);\n'
    '        onStatus && onStatus(`DEFAULT \\u2192 ${selected.name}`);\n'
    '      },',
    '      // Same erasure as DUPLICATE, and even harder to see: the COUNT of\n'
    '      // starred rows is 1 before and after whatever happens, because a\n'
    '      // factory preset is default at rest. Only the marked row\'s id\n'
    '      // shows the revert.\n'
    '      onSetDefault: () => {\n'
    '        if (onSetDefaultPattern) {\n'
    '          onSetDefaultPattern(selected.id);\n'
    '          return;\n'
    '        }\n'
    '        setDefaultId(selected.id);\n'
    '        onStatus && onStatus(`DEFAULT \\u2192 ${selected.name}`);\n'
    '      },',
)

edit(
    "manager-accepts-the-two-new-handlers",
    'function PatternManager({ open, onClose, userPatterns, setUserPatterns, '
    'defaultId, setDefaultId, N, onApply, currentGains, onStatus, '
    'onRequestSave, onRenamePattern, onDeletePattern }) {',
    'function PatternManager({ open, onClose, userPatterns, setUserPatterns, '
    'defaultId, setDefaultId, N, onApply, currentGains, onStatus, '
    'onRequestSave, onRenamePattern, onDeletePattern, onDuplicatePattern, '
    'onSetDefaultPattern }) {',
)

# The dialog itself, and its mount. Rendered as an absolutely-positioned
# sibling of the manager panel inside the same overlay, so it adds nothing to
# the panel's flex column and nothing to the action row -- both of which are
# geometry another change owns. `position: absolute` takes it out of the
# overlay's flex flow, so the panel stays centred; the capture in
# tools/native_shot.cpp proves that rather than assuming it.
DELETE_DIALOG = (
    'function PatternDeleteDialog({ pattern, onConfirm, onCancel }) {\n'
    '  usePE(() => {\n'
    '    if (!pattern) return;\n'
    '    // Escape cancels, Return confirms. Captured, and stopped, because\n'
    '    // the manager\'s own Return handler would otherwise apply the very\n'
    '    // preset this is asking to delete.\n'
    '    const onKey = (event) => {\n'
    '      if (event.key === "Escape") {\n'
    '        event.preventDefault();\n'
    '        event.stopPropagation();\n'
    '        onCancel();\n'
    '        return;\n'
    '      }\n'
    '      if (event.key !== "Enter") return;\n'
    '      event.preventDefault();\n'
    '      event.stopPropagation();\n'
    '      onConfirm();\n'
    '    };\n'
    '    document.addEventListener("keydown", onKey, true);\n'
    '    return () => document.removeEventListener("keydown", onKey, true);\n'
    '  }, [pattern, onConfirm, onCancel]);\n'
    '  if (!pattern) return null;\n'
    '  return /* @__PURE__ */ React.createElement(\n'
    '    "div",\n'
    '    {\n'
    '      "data-spectr-overlay": "true",\n'
    '      "data-spectr-delete-dialog": true,\n'
    '      role: "dialog",\n'
    '      "aria-modal": "true",\n'
    '      "aria-label": "Confirm delete preset",\n'
    '      onClick: (event) => { event.stopPropagation(); onCancel(); },\n'
    '      style: {\n'
    '        position: "absolute",\n'
    '        inset: 0,\n'
    '        zIndex: 40,\n'
    '        background: "rgba(5,7,10,.72)",\n'
    '        display: "flex",\n'
    '        alignItems: "center",\n'
    '        justifyContent: "center"\n'
    '      }\n'
    '    },\n'
    '    /* @__PURE__ */ React.createElement("div", { '
    '"data-spectr-delete-panel": true, "data-spectr-overlay": "true", '
    'overlay: true, onDismiss: onCancel, onClick: (event) => '
    'event.stopPropagation(), style: {\n'
    '      width: 360,\n'
    '      padding: 18,\n'
    '      background: "rgba(12,16,22,.99)",\n'
    '      border: "1px solid rgba(240,150,160,.35)",\n'
    '      borderRadius: 5,\n'
    '      fontFamily: "var(--mono)",\n'
    '      color: "#fff"\n'
    '    } }, /* @__PURE__ */ React.createElement("div", { style: '
    '{ fontSize: 11, letterSpacing: 2, marginBottom: 12 } }, "DELETE PRESET"),'
    ' /* @__PURE__ */ React.createElement("div", { '
    '"data-spectr-delete-prompt": true, style: { fontSize: 11, lineHeight: '
    '1.5, opacity: 0.8, wordBreak: "break-word" } }, `Delete \\u201C'
    '${pattern.name}\\u201D? This cannot be undone.`), '
    '/* @__PURE__ */ React.createElement("div", { style: { display: "flex", '
    'justifyContent: "flex-end", gap: 7, marginTop: 14 } }, '
    '/* @__PURE__ */ React.createElement(MBtn, { action: "delete-cancel", '
    'onClick: onCancel }, "CANCEL"), /* @__PURE__ */ React.createElement('
    'MBtn, { action: "delete-confirm", danger: true, onClick: onConfirm }, '
    '"DELETE")))\n'
    '  );\n'
    '}\n')

edit(
    "delete-dialog-component",
    'function PatternSaveDialog({ open, defaultName, onSave, onCancel }) {',
    DELETE_DIALOG + 'function PatternSaveDialog({ open, defaultName, onSave, '
    'onCancel }) {',
)

# Mount it as the last child of the manager's OUTER overlay div -- the one
# whose closing parens end the component -- not inside the panel.
edit(
    "delete-dialog-mount",
    '/* @__PURE__ */ React.createElement(MBtn, { onClick: () => '
    'exportAll("file") }, "EXPORT ALL (FILE)"), '
    '/* @__PURE__ */ React.createElement(MBtn, { onClick: () => '
    'exportAll("clipboard") }, "EXPORT ALL (CLIP)"))));\n}',
    '/* @__PURE__ */ React.createElement(MBtn, { onClick: () => '
    'exportAll("file") }, "EXPORT ALL (FILE)"), '
    '/* @__PURE__ */ React.createElement(MBtn, { onClick: () => '
    'exportAll("clipboard") }, "EXPORT ALL (CLIP)"))), '
    '/* @__PURE__ */ React.createElement(PatternDeleteDialog, { '
    'pattern: pendingDelete, onCancel: () => setPendingDelete(null), '
    'onConfirm: () => { const target = pendingDelete; '
    'setPendingDelete(null); if (target) del(target.id); } }));\n}',
)

# --- 5. App passes the two library-backed handlers ------------------------
edit(
    "app-duplicate-and-set-default-commands",
    '  const deletePattern = useAppC((id) => patternCommand(',
    '  // Both of these used to be done inside the manager by writing React\n'
    '  // state, which the next command\'s response overwrote from a library\n'
    '  // they never reached. The C++ behind both verbs already existed and\n'
    '  // was unit-tested; only the callers were missing.\n'
    '  const duplicatePattern = useAppC((id) => patternCommand(\n'
    '    "duplicate_pattern",\n'
    '    { id },\n'
    '    () => setUserPatterns((current) => {\n'
    '      const src = [...window.Spectr.FACTORY_PATTERNS, ...current]\n'
    '        .find((pattern) => pattern.id === id);\n'
    '      if (!src) return current;\n'
    '      const gains = src.source === "factory"\n'
    '        ? window.Spectr.factoryGains(src.id, 128)\n'
    '        : window.Spectr.fromCanonical(src.gains);\n'
    '      return [...current,\n'
    '        window.Spectr.makeUserPattern(src.name + " COPY", gains)];\n'
    '    })\n'
    '  ).then((body) => {\n'
    '    if (body || !nativeBridgeAvailable) fireStatus("DUPLICATED");\n'
    '  }), [fireStatus, nativeBridgeAvailable, patternCommand]);\n'
    '  const setDefaultPattern = useAppC((id) => patternCommand(\n'
    '    "set_default_pattern",\n'
    '    { id },\n'
    '    () => setDefaultId(id)\n'
    '  ).then((body) => {\n'
    '    if (body || !nativeBridgeAvailable) fireStatus("DEFAULT SET");\n'
    '  }), [fireStatus, nativeBridgeAvailable, patternCommand]);\n'
    '  const deletePattern = useAppC((id) => patternCommand(',
)

edit(
    "app-passes-the-new-handlers",
    '      onRenamePattern: renamePattern,\n'
    '      onDeletePattern: deletePattern\n'
    '    }',
    '      onRenamePattern: renamePattern,\n'
    '      onDeletePattern: deletePattern,\n'
    '      onDuplicatePattern: duplicatePattern,\n'
    '      onSetDefaultPattern: setDefaultPattern\n'
    '    }',
)


def enc(snippet):
    """The document is stored as a JSON STRING, so every literal quote in the
    page is a backslash-escaped quote in the file. Patching the decoded text
    and re-dumping would also be byte-identical here (measured), but raw
    surgery on the escaped form cannot reformat anything it was not asked to
    touch, which is the property that matters on an 800KB single-line artifact
    three changes are in flight against."""
    return json.dumps(snippet)[1:-1]


def main():
    with open(PATH, "r", encoding="utf-8") as fh:
        raw = fh.read()
    if '\\"html\\"' not in raw and '"html"' not in raw:
        print("no html member: this is not the shipping document",
              file=sys.stderr)
        return 2

    applied, already, failed = [], [], []
    for label, old, new in EDITS:
        enc_new = enc(new)
        if enc_new in raw:
            already.append(label)
            continue
        enc_old = enc(old)
        hits = raw.count(enc_old)
        if hits != 1:
            failed.append("%s: patch point occurs %d times, expected 1"
                          % (label, hits))
            continue
        raw = raw.replace(enc_old, enc_new, 1)
        applied.append(label)

    if failed:
        for line in failed:
            print("FAIL " + line, file=sys.stderr)
        print("nothing written -- a drifted patch point is never guessed at",
              file=sys.stderr)
        return 1

    for label in already:
        print("already applied: " + label)
    if not applied:
        print("all %d edits already applied; nothing written" % len(EDITS))
        return 0

    # Prove the result still parses and still carries a page, before it
    # replaces a working artifact.
    try:
        doc = json.loads(raw)
    except ValueError as exc:
        print("refusing to write: the patched document is not valid JSON "
              "(%s)" % exc, file=sys.stderr)
        return 1
    if not isinstance(doc.get("html"), str) or len(doc["html"]) < 100000:
        print("refusing to write: the patched document carries no page",
              file=sys.stderr)
        return 1

    with open(PATH, "w", encoding="utf-8") as fh:
        fh.write(raw)
    for label in applied:
        print("applied: " + label)
    print("wrote %s" % os.path.relpath(PATH, REPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
