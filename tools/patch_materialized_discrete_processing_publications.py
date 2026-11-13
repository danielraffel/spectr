#!/usr/bin/env python3
"""Flush native processing publications for discrete context-menu actions.

The authored importer adapter adds the same callback edits to a fresh browser
materialization. This replayable patch keeps the checked-in native runtime in
sync: drag samples remain coalesced until a frame, while discrete menu actions
flush the complete state before the standalone acceptance surface reads it.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

DEFAULT_PATH = Path("native-ui/materialized/materialized-document.runtime.json")

EDITS = (
    (
        "band context mute",
        '''        onMuteBand: (b) => {
          const cur = targetGainsRef.current[b];
          commitGain(b, isMuted(cur) ? restoreMutedGain(b) : -Infinity);
          if (onStatus) onStatus(isMuted(cur) ? `BAND ${b + 1} UNMUTED` : `BAND ${b + 1} MUTED`);
        },''',
        '''        onMuteBand: (b) => {
          const cur = targetGainsRef.current[b];
          commitGain(b, isMuted(cur) ? restoreMutedGain(b) : -Infinity);
          flushNativeProcessingStatePublication();
          if (onStatus) onStatus(isMuted(cur) ? `BAND ${b + 1} UNMUTED` : `BAND ${b + 1} MUTED`);
        },''',
    ),
    (
        "band context reset",
        '''        onZeroBand: (b) => { commitGain(b, 0); if (onStatus) onStatus(`BAND ${b + 1} RESET`); },''',
        '''        onZeroBand: (b) => {
          commitGain(b, 0);
          flushNativeProcessingStatePublication();
          if (onStatus) onStatus(`BAND ${b + 1} RESET`);
        },''',
    ),
    (
        "band context solo",
        '''          commitMany(map);
          if (onStatus) onStatus(`BAND ${b + 1} SOLO`);''',
        '''          commitMany(map);
          flushNativeProcessingStatePublication();
          if (onStatus) onStatus(`BAND ${b + 1} SOLO`);''',
    ),
    (
        "selection zero",
        '''          commitMany(map);
          if (onStatus) onStatus(`${map.size} BAND${map.size === 1 ? "" : "S"} ZEROED`);''',
        '''          commitMany(map);
          flushNativeProcessingStatePublication();
          if (onStatus) onStatus(`${map.size} BAND${map.size === 1 ? "" : "S"} ZEROED`);''',
    ),
    (
        "selection mute",
        '''          if (!result) { if (onStatus) onStatus("NO SELECTION"); return; }
          if (onStatus) onStatus(`${result.count} BAND${result.count === 1 ? "" : "S"} ${result.muted ? "MUTED" : "UNMUTED"}`);''',
        '''          if (!result) { if (onStatus) onStatus("NO SELECTION"); return; }
          flushNativeProcessingStatePublication();
          if (onStatus) onStatus(`${result.count} BAND${result.count === 1 ? "" : "S"} ${result.muted ? "MUTED" : "UNMUTED"}`);''',
    ),
    (
        "fit view",
        '''        onFitView: () => {
          setView({ lmin: Math.log10(20), lmax: Math.log10(2e4) });
          if (onStatus) onStatus("VIEW \\u2192 20 Hz \\u2013 20 kHz");
        }''',
        '''        onFitView: () => {
          setView({ lmin: Math.log10(20), lmax: Math.log10(2e4) });
          queueNativeProcessingStatePublication();
          flushNativeProcessingStatePublication();
          if (onStatus) onStatus("VIEW \\u2192 20 Hz \\u2013 20 kHz");
        }''',
    ),
)


def apply_one(html: str, label: str, old: str, new: str) -> tuple[str, str]:
    old_count = html.count(old)
    new_count = html.count(new)
    if old_count == 1:
        return html.replace(old, new, 1), f"applied {label}"
    if old_count == 0 and new_count == 1:
        return html, f"already applied {label}"
    raise SystemExit(
        f"FAIL {label}: old={old_count} new={new_count}, expected one old or one new"
    )


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PATH
    if len(sys.argv) > 2:
        raise SystemExit("usage: patch_materialized_discrete_processing_publications.py [runtime.json]")
    document = json.loads(path.read_text(encoding="utf-8"))
    html = document.get("html")
    if not isinstance(html, str) or not html:
        raise SystemExit(f"FAIL {path}: no usable html field")
    messages = []
    for label, old, new in EDITS:
        html, message = apply_one(html, label, old, new)
        messages.append(message)
    document["html"] = html
    path.write_text(
        json.dumps(document, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    for message in messages:
        print(message)
    print(f"written {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
