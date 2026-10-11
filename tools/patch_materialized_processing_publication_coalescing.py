#!/usr/bin/env python3
"""Make materialized Spectr state publication one-per-frame.

The authored importer adapter carries the durable source transformation in
``resources/editor.html``.  This small replayable recipe keeps the checked-in
materialized runtime in sync when a fresh materialization starts from the
pre-coalescing runtime artifact.

Usage:
    python3 tools/patch_materialized_processing_publication_coalescing.py [runtime.json]

The path defaults to ``native-ui/materialized/materialized-document.runtime.json``.
The edit is idempotent and fails closed if an old or new patch point is
ambiguous.  It snapshots the latest complete payload, advances the command
sequence at input time, flushes on the next animation frame, and flushes the
release handler synchronously so a final edit cannot be lost.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

DEFAULT_PATH = Path("native-ui/materialized/materialized-document.runtime.json")

OLD_REFS = '''  const nativeDirectPublicationSignatureRef = useRef("");
  const nativeCommandSequenceRef = useRef(0);
'''
NEW_REFS = '''  const nativeDirectPublicationSignatureRef = useRef("");
  const nativePublicationFrameRef = useRef(0);
  const nativePendingPublicationRef = useRef(null);
  const nativeCommandSequenceRef = useRef(0);
'''

OLD_QUEUE = '''  const queueNativeProcessingStatePublication = () => {
    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
    const current = targetGainsRef.current;
    const live = spectrPublicationProvenance();
    const muted = current.map((value) => isMuted(value));
    const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);
    nativeDirectPublicationSignatureRef.current = JSON.stringify([
      N, gainDb2, muted, live.lmin, live.lmax
    ]);
    ++nativeCommandSequenceRef.current;
    Promise.resolve(window.pulp.postMessage("processing_state_set", {
      n_visible: N,
      gain_db: gainDb2,
      muted,
      min_hz: Math.pow(10, live.lmin),
      max_hz: Math.pow(10, live.lmax),
      drawn_revision: live.drawn
    }, "spectr-processing-state")).catch((error) => {
      console.error("[Spectr] native direct-edit publication failed", error);
    });
  };
'''
NEW_QUEUE = '''  const sendNativeProcessingStatePublication = (publication) => {
    try {
      Promise.resolve(window.pulp.postMessage("processing_state_set", publication, "spectr-processing-state")).catch((error) => {
        console.error("[Spectr] native direct-edit publication failed", error);
      });
    } catch (error) {
      console.error("[Spectr] native direct-edit publication failed", error);
    }
  };
  const flushNativeProcessingStatePublication = () => {
    const frame = nativePublicationFrameRef.current;
    nativePublicationFrameRef.current = 0;
    if (frame) cancelAnimationFrame(frame);
    const publication = nativePendingPublicationRef.current;
    nativePendingPublicationRef.current = null;
    if (publication) sendNativeProcessingStatePublication(publication);
  };
  const queueNativeProcessingStatePublication = () => {
    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
    const current = targetGainsRef.current;
    const live = spectrPublicationProvenance();
    const muted = current.map((value) => isMuted(value));
    const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);
    // Advance the command sequence at user intent time, before the frame
    // flush, so an older async native response cannot overwrite this edit.
    ++nativeCommandSequenceRef.current;
    // Stamp the signature when queued so the render effect cannot publish a
    // duplicate while the coalesced frame is waiting. Copy the payload now;
    // a later host projection cannot replace the user's latest edit.
    nativeDirectPublicationSignatureRef.current = JSON.stringify([
      N, gainDb2, muted, live.lmin, live.lmax
    ]);
    nativePendingPublicationRef.current = {
      n_visible: N,
      gain_db: gainDb2,
      muted,
      min_hz: Math.pow(10, live.lmin),
      max_hz: Math.pow(10, live.lmax),
      drawn_revision: live.drawn
    };
    if (!nativePublicationFrameRef.current)
      nativePublicationFrameRef.current = requestAnimationFrame(() => {
        nativePublicationFrameRef.current = 0;
        flushNativeProcessingStatePublication();
      });
  };
  // Pointerup bubbles after the target handler, so the final edit is flushed
  // immediately while move samples within one frame still coalesce.
  useEffect(() => {
    const flush = () => flushNativeProcessingStatePublication();
    window.addEventListener("pointerup", flush);
    window.addEventListener("pointercancel", flush);
    return () => {
      window.removeEventListener("pointerup", flush);
      window.removeEventListener("pointercancel", flush);
      flushNativeProcessingStatePublication();
    };
  }, []);
'''

OLD_POINTER_WITH_PREFLUSH = '''  const onPointerUp = (e) => {
    flushNativeProcessingStatePublication();
    const p = pointerRef.current;'''
POINTER_WITHOUT_PREFLUSH = '''  const onPointerUp = (e) => {
    const p = pointerRef.current;'''


OLD_SURFACE = '''      onPointerUp,
      onPointerLeave:'''
NEW_SURFACE = '''      onPointerUp: (event) => { onPointerUp(event); flushNativeProcessingStatePublication(); },
      onPointerLeave:'''


def remove_pointer_preflush(html: str) -> tuple[str, str]:
    # A release handler must compute its final edit before the wrapper flushes.
    # Accept both the pre-coalescing source (no-op) and the earlier coalescing
    # patch (remove only its unsafe leading flush) so the recipe is replayable
    # against either materialized artifact.
    if html.count(OLD_POINTER_WITH_PREFLUSH) == 1:
        return html.replace(OLD_POINTER_WITH_PREFLUSH, POINTER_WITHOUT_PREFLUSH, 1), 'removed pointer preflush'
    if html.count(POINTER_WITHOUT_PREFLUSH) == 1:
        return html, 'pointer preflush already absent'
    raise SystemExit('FAIL pointer release order: expected exactly one old or new handler')


def apply_one(html: str, label: str, old: str, new: str) -> tuple[str, str]:
    if old in html:
        count = html.count(old)
        if count != 1:
            raise SystemExit(f"FAIL {label}: old patch point occurs {count} times")
        return html.replace(old, new, 1), f"applied {label}"
    if html.count(new) == 1:
        return html, f"already applied {label}"
    raise SystemExit(f"FAIL {label}: neither old nor new patch point occurs exactly once")


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PATH
    if len(sys.argv) > 2:
        raise SystemExit("usage: patch_materialized_processing_publication_coalescing.py [runtime.json]")
    document = json.loads(path.read_text(encoding="utf-8"))
    html = document.get("html")
    if not isinstance(html, str) or not html:
        raise SystemExit(f"FAIL {path}: no usable html field")
    messages = []
    html, message = remove_pointer_preflush(html)
    messages.append(message)
    for label, old, new in (
        ("publication refs", OLD_REFS, NEW_REFS),
        ("coalesced publication queue", OLD_QUEUE, NEW_QUEUE),
        ("surface release flush", OLD_SURFACE, NEW_SURFACE),
    ):
        html, message = apply_one(html, label, old, new)
        messages.append(message)
    document["html"] = html
    path.write_text(json.dumps(document, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    for message in messages:
        print(message)
    print(f"written {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
