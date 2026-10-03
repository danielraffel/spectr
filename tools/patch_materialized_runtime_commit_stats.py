#!/usr/bin/env python3
"""Count the vendored @pulp/react runtime's commits and captured re-applies.

WHY THIS EXISTS

    Every React commit that mutated a host node re-applies captured import
    metadata, and on this document one re-apply costs 20-45 ms. The editor
    open used to make six such commits after the mount; how many it makes is
    the number an editor-open gate has to hold, and wall time is too noisy to
    hold it. These counters make it a deterministic count.

WHAT IT CHANGES

    resetAfterCommit keeps `globalThis.__pulpCommitStats__`:

      commits         every commit the reconciler finished
      reapplies       commits that re-applied captured metadata
      full_reapplies  re-applies with no scope (the whole document)
      reapply_ms      wall time spent inside those re-applies
      mount_commits   commits made by the document's mount (the root render
                      and the layout-effect flush it runs before returning)

    Integers and one clock read per re-applying commit; nothing is allocated.
    When a test sets `globalThis.__pulpCommitLog__ = []`, each commit also
    appends {reapply, full, scope, ms, stack}: the JS stack names what caused
    the commit (a promise job, an animation frame, a scheduler timer).

DELETE WHEN

    Never on its own: it is a measurement surface. If the regenerated
    runtime.js carries equivalent counters, point the editor-open gate at
    those and drop this transplant.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "g4.__pulpCommitStats__"

GATE_ANCHOR = (
    "      const shouldReapply = unscopedReason || materializedDirtyIds.size > 0;\n"
)
GATE_INSERT = (
    "      // Commit counters for the editor-open gate "
    "(tools/patch_materialized_runtime_commit_stats.py).\n"
    "      const commitStats = g4.__pulpCommitStats__ || (g4.__pulpCommitStats__ = {\n"
    "        commits: 0, reapplies: 0, full_reapplies: 0, reapply_ms: 0 });\n"
    "      commitStats.commits += 1;\n"
    "      const commitLog = Array.isArray(g4.__pulpCommitLog__) ? g4.__pulpCommitLog__ : null;\n"
    "      const commitClock = typeof __performanceNow__ === \"function\" ? __performanceNow__ : null;\n"
    "      const commitStart = commitClock ? commitClock() : 0;\n"
)

APPLY_ANCHOR = (
    "      if (shouldReapply) {\n"
    "        materializedHookApplied = metadataHook;\n"
    "        if (typeof metadataHook === \"function\") metadataHook(scope);\n"
    "      }\n"
)
APPLY_REPLACEMENT = (
    "      if (shouldReapply) {\n"
    "        materializedHookApplied = metadataHook;\n"
    "        if (typeof metadataHook === \"function\") metadataHook(scope);\n"
    "        commitStats.reapplies += 1;\n"
    "        if (scope === null) commitStats.full_reapplies += 1;\n"
    "      }\n"
)

TAIL_ANCHOR = (
    "      if (shouldReapply) {\n"
    "        requestLayoutFlush(() => {\n"
    "          if (typeof g4.layout === \"function\") call2(\"layout\");\n"
    "        });\n"
    "      }\n"
    "    },\n"
)
TAIL_REPLACEMENT = (
    "      if (shouldReapply) {\n"
    "        requestLayoutFlush(() => {\n"
    "          if (typeof g4.layout === \"function\") call2(\"layout\");\n"
    "        });\n"
    "      }\n"
    "      const commitMs = commitClock ? commitClock() - commitStart : 0;\n"
    "      if (shouldReapply) commitStats.reapply_ms += commitMs;\n"
    "      if (commitLog && commitLog.length < 256) commitLog.push({\n"
    "        reapply: shouldReapply, full: shouldReapply && scope === null,\n"
    "        scope: scope ? scope.length : 0, ms: commitMs,\n"
    "        stack: String(new Error().stack || \"\").split(\"\\n\").slice(1, 14).join(\" | \") });\n"
    "    },\n"
)


MOUNT_ANCHOR = "  activeNativeRoot.render(capturedRootElement);\n"
MOUNT_INSERT = (
    "  // Everything the mount committed, including the layout-effect flush\n"
    "  // render() runs before it returns; later commits are post-mount.\n"
    "  if (g5.__pulpCommitStats__)\n"
    "    g5.__pulpCommitStats__.mount_commits = g5.__pulpCommitStats__.commits;\n"
)


def replace_once(raw, anchor, replacement, what):
    count = raw.count(anchor)
    if count != 1:
        sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (what, count))
    return raw.replace(anchor, replacement, 1)


def main():
    raw = open(PATH, encoding="utf-8").read()
    if MARKER in raw:
        print("already applied  commit stats")
        return 0
    raw = replace_once(raw, GATE_ANCHOR, GATE_ANCHOR + GATE_INSERT, "dirty gate")
    raw = replace_once(raw, APPLY_ANCHOR, APPLY_REPLACEMENT, "re-apply")
    raw = replace_once(raw, TAIL_ANCHOR, TAIL_REPLACEMENT, "commit tail")
    raw = replace_once(raw, MOUNT_ANCHOR, MOUNT_ANCHOR + MOUNT_INSERT, "mount")
    with open(PATH, "w", encoding="utf-8") as stream:
        stream.write(raw)
    print("applied  commit stats")
    return 0


if __name__ == "__main__":
    sys.exit(main())
